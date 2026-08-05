#include "World.hpp"

// Compile stb_perlin's implementation exactly once, in this translation unit.
#define STB_PERLIN_IMPLEMENTATION
#include "stb_perlin.h"
#include <iostream>
#include <algorithm>
#include <cmath>

// Fractal Brownian Motion: sums `octaves` layers of Perlin noise (each octave
// doubles frequency and halves amplitude). Output stays in roughly [-1, +1].
static float fbm(float x, float z, int octaves, float lacunarity, float gain) {
    float sum = 0.0f;
    float amp = 1.0f;
    float freq = 1.0f;
    float norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += stb_perlin_noise3(x * freq, z * freq, 0.0f, 0, 0, 0) * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return sum / norm;
}

// Hermite smoothstep: 0 below edge0, 1 above edge1, eased in between.
static float smoothstep(float edge0, float edge1, float x) {
    float t = std::max(0.0f, std::min(1.0f, (x - edge0) / (edge1 - edge0)));
    return t * t * (3.0f - 2.0f * t);
}

// Cross-sprite plants (tall grass, flowers) are non-solid: they never block
// movement, rays, or face-culling, and behave like AIR for the water mesh.
static bool isNonSolid(BlockType t) {
    return t == BlockType::AIR || t == BlockType::TALL_GRASS
        || t == BlockType::RED_ROSE || t == BlockType::YELLOW_FLOWER;
}

// ==========================================================
// Lifecycle
// ==========================================================

World::World() {
    init();
}

World::~World() {
    // Stop the background generator and wait for it to finish writing blocks
    // before tearing down storage / GPU buffers.
    cancelled.store(true);
    if (genThread.joinable()) genThread.join();

    for (const auto& chunk : chunks) {
        if (!chunk) continue;
        if (chunk->VAO != 0) glDeleteVertexArrays(1, &chunk->VAO);
        if (chunk->VBO != 0) glDeleteBuffers(1, &chunk->VBO);
        if (chunk->waterVAO != 0) glDeleteVertexArrays(1, &chunk->waterVAO);
        if (chunk->waterVBO != 0) glDeleteBuffers(1, &chunk->waterVBO);
    }
}

void World::init() {
    // Guard against double calls (would reallocate chunks and leak GPU buffers).
    if (!chunks.empty()) return;

    // 1. Allocate one Chunk + one Heightmap per chunk in the 16x16 grid.
    chunks.resize((size_t)CHUNK_COUNT_X * CHUNK_COUNT_Z);
    heightmaps.resize((size_t)CHUNK_COUNT_X * CHUNK_COUNT_Z);

    for (int cx = 0; cx < CHUNK_COUNT_X; ++cx) {
        for (int cz = 0; cz < CHUNK_COUNT_Z; ++cz) {
            size_t index = (size_t)cx * CHUNK_COUNT_Z + cz;
            chunks[index] = std::make_shared<Chunk>(cx, cz);
            heightmaps[index] = std::make_unique<Heightmap>(
                glm::ivec2(cx * CHUNK_SIZE, cz * CHUNK_SIZE), CHUNK_SIZE, CHUNK_SIZE);
        }
    }

    // 2. Generation runs on a background worker thread (pure CPU - no GL), so
    //    building 1024 chunks of heightfield + terrain + features never blocks
    //    the render thread. GPU meshing is deferred to the main thread via the
    //    mesh queue drained in update().
    genThread = std::thread([this] {
        // 2a. Heightfield pass: noise pipeline + world-wide normalisation fills
        // every chunk's heightmap, then rivers & lakes are carved into it.
        generateHeightfield();
        if (cancelled.load()) return;

        // 2b. Column fill pass: layered surface rules (grass/dirt/stone on dry
        // land; sand river/lake beds + water filling below WATER_LEVEL).
        for (int x = 0; x < WORLD_SIZE_X; ++x) {
            for (int z = 0; z < WORLD_SIZE_Z; ++z) {
                generateColumnTerrain(x, z, getSurfaceHeight(x, z));
            }
        }
        if (cancelled.load()) return;

        // 2c. Sand beaches along the river/lake shorelines.
        generateShorelines();

        // 3. Decorate the finished surface: dense forest (oak & pine), tall
        //    grass, flowers. Runs after all terrain so features sit on the
        //    final heightmap and can safely span chunk borders.
        generateFeatures();

        // 4. Mark every chunk as generated so meshing can proceed.
        for (const auto& chunk : chunks) {
            if (chunk) chunk->generated = true;
        }

        std::cout << "[WORLD] Terrain generated: " << (CHUNK_COUNT_X * CHUNK_COUNT_Z)
                  << " chunks (" << WORLD_SIZE_X << "x" << WORLD_SIZE_Y << "x" << WORLD_SIZE_Z
                  << " blocks) with serpentine rivers & lakes, water level "
                  << WATER_LEVEL << "." << std::endl;

        // Hand every chunk to the main thread for GPU meshing.
        {
            std::lock_guard<std::mutex> lock(meshMutex);
            meshQueue.reserve((size_t)CHUNK_COUNT_X * CHUNK_COUNT_Z);
            for (int cx = 0; cx < CHUNK_COUNT_X; ++cx)
                for (int cz = 0; cz < CHUNK_COUNT_Z; ++cz)
                    meshQueue.push_back(cx * CHUNK_COUNT_Z + cz);
        }
        terrainReady.store(true);
    });
}

void World::update() {
    // Stream queued chunk meshes onto the GPU in bounded batches (main thread).
    drainMeshQueue();
}

bool World::isTerrainReady() const {
    return terrainReady.load();
}

// Meshes up to MESH_CHUNKS_PER_FRAME queued chunks. GL calls must run on the
// main thread, so the background thread only builds block data and this queue
// feeds the render thread a few meshes per frame - 1024 chunks stream in
// without freezing the window.
void World::drainMeshQueue() {
    if (!terrainReady.load()) return;

    std::vector<int> batch;
    {
        std::lock_guard<std::mutex> lock(meshMutex);
        size_t take = std::min<size_t>(MESH_CHUNKS_PER_FRAME, meshQueue.size());
        if (take == 0) return;
        batch.assign(meshQueue.begin(), meshQueue.begin() + (int)take);
        meshQueue.erase(meshQueue.begin(), meshQueue.begin() + (int)take);
    }
    for (int idx : batch) {
        int cx = idx / CHUNK_COUNT_Z;
        int cz = idx % CHUNK_COUNT_Z;
        generateChunkMesh(cx, cz);
    }

    if (meshQueue.empty() && !meshLogDone) {
        meshLogDone = true;
        std::cout << "[WORLD SUCCESS] Meshed " << (CHUNK_COUNT_X * CHUNK_COUNT_Z)
                  << " chunk meshes, total " << getVertexCount() << " vertices." << std::endl;
        std::cout << "[WORLD] Render distance: " << RENDER_DISTANCE
                  << " chunks around player chunk." << std::endl;
    }
}

// ==========================================================
// Coordinate helpers (replacing the old C-style macros)
// ==========================================================

glm::ivec3 World::posToOffset(const glm::ivec3& pos) {
    // The chunk grid starts at the world origin, so the offset of a world
    // position is the position itself. (chunksOrigin is kept as a member for
    // future non-zero origins; this helper is static per the OOP spec.)
    return pos;
}

glm::ivec3 World::posToChunkPos(const glm::ivec3& pos) {
    glm::ivec3 offset = posToOffset(pos);
    return glm::ivec3(
        (int)std::floor((double)offset.x / CHUNK_SIZE),
        (int)std::floor((double)offset.y / CHUNK_SIZE),
        (int)std::floor((double)offset.z / CHUNK_SIZE)
    );
}

bool World::inBounds(const glm::ivec3& offset) const {
    return offset.x >= 0 && offset.x < WORLD_SIZE_X &&
           offset.y >= 0 && offset.y < WORLD_SIZE_Y &&
           offset.z >= 0 && offset.z < WORLD_SIZE_Z;
}

size_t World::getChunkIndex(const glm::ivec3& offset) const {
    int cx = (int)std::floor((double)offset.x / CHUNK_SIZE);
    int cz = (int)std::floor((double)offset.z / CHUNK_SIZE);
    cx = std::max(0, std::min(CHUNK_COUNT_X - 1, cx));
    cz = std::max(0, std::min(CHUNK_COUNT_Z - 1, cz));
    return (size_t)cx * CHUNK_COUNT_Z + cz;
}

// ==========================================================
// Internal block access (chunk-backed, OOB-safe)
// ==========================================================

Chunk* World::chunkAt(int chunkX, int chunkZ) {
    if (chunkX < 0 || chunkX >= CHUNK_COUNT_X || chunkZ < 0 || chunkZ >= CHUNK_COUNT_Z)
        return nullptr;
    return chunks[(size_t)chunkX * CHUNK_COUNT_Z + chunkZ].get();
}

const Chunk* World::chunkAt(int chunkX, int chunkZ) const {
    if (chunkX < 0 || chunkX >= CHUNK_COUNT_X || chunkZ < 0 || chunkZ >= CHUNK_COUNT_Z)
        return nullptr;
    return chunks[(size_t)chunkX * CHUNK_COUNT_Z + chunkZ].get();
}

BlockType World::blockRef(int x, int y, int z) const {
    if (x < 0 || x >= WORLD_SIZE_X || y < 0 || y >= WORLD_SIZE_Y || z < 0 || z >= WORLD_SIZE_Z)
        return BlockType::AIR;
    const Chunk* chunk = chunkAt(x / CHUNK_SIZE, z / CHUNK_SIZE);
    if (!chunk) return BlockType::AIR;
    return chunk->blocks[(size_t)(y * CHUNK_SIZE + (x % CHUNK_SIZE)) * CHUNK_SIZE + (z % CHUNK_SIZE)];
}

void World::setBlockRaw(int x, int y, int z, BlockType type) {
    if (x < 0 || x >= WORLD_SIZE_X || y < 0 || y >= WORLD_SIZE_Y || z < 0 || z >= WORLD_SIZE_Z)
        return;
    Chunk* chunk = chunkAt(x / CHUNK_SIZE, z / CHUNK_SIZE);
    if (!chunk) return;
    chunk->blocks[(size_t)(y * CHUNK_SIZE + (x % CHUNK_SIZE)) * CHUNK_SIZE + (z % CHUNK_SIZE)] = type;
}

// ==========================================================
// Terrain generation
// ==========================================================

// ==========================================================
// Heightfield generation (noise pipeline, no water)
//
// Two-field pipeline: a low-frequency FBM supplies broad rolling plains (the
// standard ground level), and a second, higher-frequency field is gated and
// curve-shaped into hills that rise up to HILL_AMP_Y blocks above the local
// plain. The curve exponent (< 1) steepens the flanks while flattening the
// crowns, and the gentler HILL_CURVE makes the flanks taper instead of forming
// boxy cliffs. A final "dirt apron" pass raises the foot of any tall wall by a
// tapering 1..APRON_MAX_H ramp dilated outward over APRON_SPREAD columns, so
// hills melt smoothly into the surrounding plains. Because a fixed 256x256
// world covers only a few periods of low-frequency noise (which biases
// stb_perlin's raw output), both fields are normalised to [0,1] across the
// whole world first. Heights are evaluated once in world space, so chunk
// borders are seamless.
// ==========================================================
void World::generateHeightfield() {
    // Pass A: sample both raw noise fields and track their world-wide min/max.
    std::vector<float> plainField((size_t)WORLD_SIZE_X * WORLD_SIZE_Z);
    std::vector<float> hillField((size_t)WORLD_SIZE_X * WORLD_SIZE_Z);
    float pMin = 1e9f, pMax = -1e9f, hMin = 1e9f, hMax = -1e9f;

    for (int z = 0; z < WORLD_SIZE_Z; ++z) {
        for (int x = 0; x < WORLD_SIZE_X; ++x) {
            // Broad rolling plains: 4-octave FBM, no warping.
            float p = fbm(x * PLAIN_FREQ, z * PLAIN_FREQ, 4, 2.0f, 0.5f);

            // Localised hills: an independent higher-frequency map, offset so it
            // never correlates with the plains field.
            float h = fbm(x * HILL_FREQ + 700.0f, z * HILL_FREQ + 700.0f,
                          HILL_OCTAVES, 2.0f, 0.5f);

            size_t i = (size_t)z * WORLD_SIZE_X + x;
            plainField[i] = p;
            hillField[i] = h;
            pMin = std::min(pMin, p); pMax = std::max(pMax, p);
            hMin = std::min(hMin, h); hMax = std::max(hMax, h);
        }
    }

    // Pass B: combine the normalised fields into a world-wide height array.
    std::vector<int> heights((size_t)WORLD_SIZE_X * WORLD_SIZE_Z);
    for (int z = 0; z < WORLD_SIZE_Z; ++z) {
        for (int x = 0; x < WORLD_SIZE_X; ++x) {
            size_t i = (size_t)z * WORLD_SIZE_X + x;

            // Gentle rolling ground level (plains 34..42).
            float pn = (plainField[i] - pMin) / (pMax - pMin);
            float height = PLAIN_MIN_Y + pn * (PLAIN_MAX_Y - PLAIN_MIN_Y);

            // Hills: gate the normalised hill field (flat on plains, full-
            // strength on strong crests), apply the curve exponent so flanks
            // rise steeply while crowns flatten, then scale by the raw field so
            // individual crests vary between ~small and full HILL_AMP_Y.
            float hn = (hillField[i] - hMin) / (hMax - hMin);
            float gate = smoothstep(HILL_GATE_LO, HILL_GATE_HI, hn);
            height += std::pow(gate, HILL_CURVE) * HILL_AMP_Y * hn;

            heights[i] = static_cast<int>(std::lround(height));
        }
    }

    // Pass C: dirt apron at cliff bases. Columns at the foot of a tall wall (a
    // neighbour >= APRON_TRIGGER taller) are raised by a 1..APRON_MAX_H ramp;
    // the ramp is then dilated downhill/level (never uphill) so it tapers
    // outward over APRON_SPREAD columns instead of a sharp 90-degree drop.
    std::vector<int> apron(heights.size(), 0);
    const int dx[4] = { 1, -1, 0, 0 };
    const int dz[4] = { 0, 0, 1, -1 };
    for (int z = 0; z < WORLD_SIZE_Z; ++z) {
        for (int x = 0; x < WORLD_SIZE_X; ++x) {
            const size_t i = (size_t)z * WORLD_SIZE_X + x;
            int wall = 0;
            for (int k = 0; k < 4; ++k) {
                const int nx = x + dx[k], nz = z + dz[k];
                if (nx < 0 || nx >= WORLD_SIZE_X || nz < 0 || nz >= WORLD_SIZE_Z) continue;
                wall = std::max(wall, heights[(size_t)nz * WORLD_SIZE_X + nx] - heights[i]);
            }
            if (wall >= APRON_TRIGGER)
                apron[i] = std::min(APRON_MAX_H, wall - (APRON_TRIGGER - 1));
        }
    }
    for (int spread = 1; spread < APRON_SPREAD; ++spread) {
        std::vector<int> next = apron;
        for (int z = 0; z < WORLD_SIZE_Z; ++z) {
            for (int x = 0; x < WORLD_SIZE_X; ++x) {
                const size_t i = (size_t)z * WORLD_SIZE_X + x;
                for (int k = 0; k < 4; ++k) {
                    const int nx = x + dx[k], nz = z + dz[k];
                    if (nx < 0 || nx >= WORLD_SIZE_X || nz < 0 || nz >= WORLD_SIZE_Z) continue;
                    const size_t j = (size_t)nz * WORLD_SIZE_X + nx;
                    if (heights[j] >= heights[i])                 // downhill/level only
                        next[i] = std::max(next[i], apron[j] - 1);
                }
            }
        }
        apron = std::move(next);
    }
    for (size_t i = 0; i < heights.size(); ++i)
        heights[i] += apron[i];

    // Pass C2: river trenches + lake basins. Runs after the apron so channels
    // cut cleanly through cliff feet; beds are clamped to MIN_TERRAIN_Y.
    carveRiversAndLakes(heights);

    // Pass D: cache each column in its owning chunk's heightmap.
    for (int z = 0; z < WORLD_SIZE_Z; ++z) {
        for (int x = 0; x < WORLD_SIZE_X; ++x) {
            const size_t i = (size_t)z * WORLD_SIZE_X + x;
            int cx = x / CHUNK_SIZE;
            int cz = z / CHUNK_SIZE;
            heightmaps[(size_t)cx * CHUNK_COUNT_Z + cz]
                ->set(x % CHUNK_SIZE, z % CHUNK_SIZE, heights[i], CHUNK_SIZE);
        }
    }
}

// Carves wide serpentine river trenches and large lake basins into the finished
// height field. Rivers use |Perlin| as a ridge field (values near zero are the
// channel centreline), domain-warped so the channel meanders. A generous
// threshold + low frequency opens the channel to ~6-15 blocks wide, and a
// flat-bottomed U-trough profile (full depth across the inner band, smoothstep
// ramp at the edges) replaces a sharp V so the bed is smooth and bowl/flat.
// Lakes use a very low-frequency noise field whose lowest values pull the
// terrain down across a wide radius. Both are clamped to MIN_TERRAIN_Y so beds
// sit a few blocks below WATER_LEVEL but never dig into the world floor.
void World::carveRiversAndLakes(std::vector<int>& heights) const {
    const size_t n = (size_t)WORLD_SIZE_X * WORLD_SIZE_Z;
    std::vector<float> river(n), lake(n);
    float rMin = 1e9f, rMax = -1e9f;
    float lMin = 1e9f, lMax = -1e9f;

    for (int z = 0; z < WORLD_SIZE_Z; ++z) {
        for (int x = 0; x < WORLD_SIZE_X; ++x) {
            const size_t i = (size_t)z * WORLD_SIZE_X + x;
            // Domain warping BEFORE the river ridge: displace the sample
            // coordinates with a low-frequency, large-amplitude field so the
            // |noise| valleys become long, organic meanders instead of
            // repetitive oxbow loops. The two axes use decorrelated seeds so
            // the path cannot fold into ring-like shapes.
            const float warpX = fbm(x * RIVER_WARP_FREQ, z * RIVER_WARP_FREQ, 2, 2.0f, 0.5f);
            const float warpZ = fbm(x * RIVER_WARP_FREQ + 52.0f, z * RIVER_WARP_FREQ + 1.3f,
                                    2, 2.0f, 0.5f);
            const float wx = x + warpX * RIVER_WARP_AMP;
            const float wz = z + warpZ * RIVER_WARP_AMP;
            river[i] = std::abs(stb_perlin_noise3(wx * RIVER_FREQ, wz * RIVER_FREQ, 0.0f, 0, 0, 0));
            // Single octave on purpose: the extra octaves of a full FBM would
            // punch small threshold islands into the middle of the basin and
            // fragment the lake into ponds. One smooth field => one continuous
            // body of water.
            lake[i] = fbm(x * LAKE_FREQ, z * LAKE_FREQ, 1, 2.0f, 0.5f);

            // Track world-wide min/max so both fields are normalised below
            // (thresholds stay stable regardless of raw stb_perlin bias).
            rMin = std::min(rMin, river[i]); rMax = std::max(rMax, river[i]);
            lMin = std::min(lMin, lake[i]);   lMax = std::max(lMax, lake[i]);
        }
    }

    // Flat-bottomed U-trough depth factor: v is the normalised field value
    // (0 = channel centre / basin middle, THRESH = outer edge). Full depth is
    // held across the inner ~60% of the channel, then a smoothstep ramp eases
    // the walls up to the plain - wide rivers read as shallow open water with
    // gently sloping shores rather than steep V-cuts.
    auto trough = [](float v, float thresh, float depth) -> float {
        if (v >= thresh) return 0.0f;
        const float k = 1.0f - v / thresh;         // 1 centre .. 0 edge
        const float d = std::min(1.0f, k * 2.5f);  // flat across the inner 60%
        const float s = d * d * (3.0f - 2.0f * d); // smoothstep edge ramp
        return s * depth;
    };

    const float rSpan = std::max(rMax - rMin, 1e-9f);   // guard degenerate fields
    const float lSpan = std::max(lMax - lMin, 1e-9f);

    for (int z = 0; z < WORLD_SIZE_Z; ++z) {
        for (int x = 0; x < WORLD_SIZE_X; ++x) {
            const size_t i = (size_t)z * WORLD_SIZE_X + x;
            int& h = heights[i];
            const int baseH = h;

            // River fade: carving is strongest on low ground and tapers to a
            // gentle dry gully on higher ground, keeping wide channels from
            // flooding the whole world while preserving hill base transitions.
            const float rFade = std::clamp(1.0f - (float)(baseH - WATER_LEVEL) / 10.0f, 0.0f, 1.0f);

            // Wide river trench: U-shaped carve where normalised |noise| < threshold.
            const float rn = (river[i] - rMin) / rSpan;
            h -= (int)std::lround(trough(rn, RIVER_THRESH, (float)RIVER_DEPTH) * rFade);

            // Unified lake basin: the wide flat-bottom trough (inner 60% at full
            // depth) drops the whole basin cleanly below WATER_LEVEL as one open
            // body of water. A gentler 16-block fade keeps lakes off high
            // hilltops; the floor guarantee below clamps the flat zone under the
            // waterline so the water body is never fragmented by dry islands.
            const float lFade = std::clamp(1.0f - (float)(baseH - WATER_LEVEL) / 16.0f, 0.0f, 1.0f);
            const float ln = (lake[i] - lMin) / lSpan;
            h -= (int)std::lround(trough(ln, LAKE_THRESH, (float)LAKE_DEPTH) * lFade);
            if (lFade > 0.0f && ln < LAKE_THRESH * 0.6f && h >= WATER_LEVEL)
                h = WATER_LEVEL - 1;   // flat basin floor always sits under water

            h = std::max(h, MIN_TERRAIN_Y);   // hard floor so beds stay shallow
        }
    }
}

// Strict sand guardrail: SAND may ONLY be placed on the single ring of dry
// columns IMMEDIATELY adjacent (1 block horizontally) to a water block at
// Y = WATER_LEVEL, and only on low banks (surface exactly WATER_LEVEL or
// WATER_LEVEL + 1) where the normalised [0,1] shore noise clears a high
// threshold. That yields sand on ~15-20% of river/lake edges; the other ~80%
// keep green GRASS touching the water. Columns 2+ steps from the edge are never
// touched (they stay GRASS / natural dirt-cliff layering). Underwater beds
// remain SAND via generateColumnTerrain.
void World::generateShorelines() {
    // Sample the shore-noise field world-wide and normalise to [0,1] so the
    // high threshold selects a stable ~15-20% of edges.
    const size_t n = (size_t)WORLD_SIZE_X * WORLD_SIZE_Z;
    std::vector<float> shore(n);
    float sMin = 1e9f, sMax = -1e9f;
    for (int z = 0; z < WORLD_SIZE_Z; ++z) {
        for (int x = 0; x < WORLD_SIZE_X; ++x) {
            const size_t i = (size_t)z * WORLD_SIZE_X + x;
            shore[i] = fbm(x * SHORE_FREQ, z * SHORE_FREQ, 2, 2.0f, 0.5f);
            sMin = std::min(sMin, shore[i]); sMax = std::max(sMax, shore[i]);
        }
    }
    const float sSpan = std::max(sMax - sMin, 1e-9f);

    const int dx[4] = { 1, -1, 0, 0 };
    const int dz[4] = { 0, 0, 1, -1 };
    for (int x = 0; x < WORLD_SIZE_X; ++x) {
        for (int z = 0; z < WORLD_SIZE_Z; ++z) {
            const int h = getSurfaceHeight(x, z);

            // Guardrail 1: only waterline banks (Y == WATER_LEVEL or +1).
            if (h != WATER_LEVEL && h != WATER_LEVEL + 1) continue;

            // Guardrail 2: only IMMEDIATE water neighbours. A neighbour column
            // whose surface sits below WATER_LEVEL holds a WATER block at
            // Y = WATER_LEVEL right next to this column.
            bool immediateWater = false;
            for (int k = 0; k < 4; ++k) {
                const int nx = x + dx[k], nz = z + dz[k];
                if (nx < 0 || nx >= WORLD_SIZE_X || nz < 0 || nz >= WORLD_SIZE_Z) continue;
                if (getSurfaceHeight(nx, nz) < WATER_LEVEL) { immediateWater = true; break; }
            }
            if (!immediateWater) continue;

            // Guardrail 3: rare sand patches only - high threshold (~15-20%).
            const float sn = (shore[(size_t)z * WORLD_SIZE_X + x] - sMin) / sSpan;
            if (sn > SHORE_SAND_THRESH) {
                setBlockRaw(x, h, z, BlockType::SAND);
                if (h - 1 >= 0) setBlockRaw(x, h - 1, z, BlockType::SAND);  // beach depth
            }
            // else: keep GRASS - green grass meets the water.
        }
    }
    std::cout << "[WORLD] Shorelines placed: strict 1-block rare sand patches (~15-20% of edges)." << std::endl;
}

void World::generateColumnTerrain(int worldX, int worldZ, int surfaceHeight) {
    // Layered terrain. DRY land (surface >= WATER_LEVEL): GRASS over a thin
    // DIRT_DEPTH (= 2) soil cap, then STONE - the shallow cap keeps STONE
    // exposed on steep edges (GRASS -> DIRT -> 1-3 STONE band), unchanged from
    // before. UNDERWATER columns (surface < WATER_LEVEL, i.e. carved river/lake
    // beds): SAND top + SAND sub-layer over STONE, with open air above filled
    // with translucent WATER up to WATER_LEVEL.
    //
    // Ore veins: while filling the STONE layer, a single-octave 3D Perlin field
    // is sampled per block so threshold crossings cluster into natural veins
    // (Ore generation ONLY ever replaces STONE - the surface/dirt/sand branches
    // below never touch the ore check).
    const bool underwater = surfaceHeight < WATER_LEVEL;
    for (int y = 0; y <= surfaceHeight; ++y) {
        if (y == surfaceHeight) {
            setBlockRaw(worldX, y, worldZ, underwater ? BlockType::SAND : BlockType::GRASS);
        } else if (y >= surfaceHeight - (underwater ? 1 : DIRT_DEPTH)) {
            setBlockRaw(worldX, y, worldZ, underwater ? BlockType::SAND : BlockType::DIRT);
        } else {
            // Deep rock layer - STONE with coal & iron ore veins. The 3D noise
            // is sampled at block coordinates (no offset needed: stb_perlin is
            // already spatially decorrelated) so chunk borders stay seamless.
            float oreNoise = stb_perlin_noise3(worldX * ORE_FREQ, y * ORE_FREQ,
                                               worldZ * ORE_FREQ, 0, 0, 0);

            // Cliff-face exposure: a stone block sits on a VISIBLE cliff wall
            // when a horizontal neighbour column's terrain surface drops below
            // this block's height (that neighbour is AIR beside it). The
            // heightmap is fully generated before the column fill pass, so
            // these lookups are valid for every column - and the lower ore
            // thresholds below put noticeably denser veins on exposed stone
            // faces (readable from across rivers/lakes) than buried rock.
            const bool cliffFace =
                getSurfaceHeight(worldX - 1, worldZ) < y ||
                getSurfaceHeight(worldX + 1, worldZ) < y ||
                getSurfaceHeight(worldX, worldZ - 1) < y ||
                getSurfaceHeight(worldX, worldZ + 1) < y;

            if (oreNoise > (cliffFace ? ORE_COAL_CLIFF_THRESH : ORE_COAL_THRESH)) {
                // ~4% on cliff faces / ~2% buried: dark speckled coal clusters
                // in small 2-5 block veins, all depths.
                setBlockRaw(worldX, y, worldZ, BlockType::COAL_ORE);
            } else if (oreNoise < (cliffFace ? ORE_IRON_CLIFF_THRESH : ORE_IRON_THRESH)) {
                // Iron stays rarer than coal (~1.2-1.5% on cliffs) and prefers
                // lower rock: full density below ORE_IRON_MAX_Y, fading to
                // zero by ORE_IRON_FADE_Y so high mountain tops stay bare.
                float fade = 1.0f;
                if (y > ORE_IRON_MAX_Y) {
                    fade = 1.0f - (float)(y - ORE_IRON_MAX_Y)
                                      / (float)(ORE_IRON_FADE_Y - ORE_IRON_MAX_Y);
                    fade = std::max(0.0f, std::min(1.0f, fade));
                }
                if (hashNoise3(worldX, y, worldZ) < fade)
                    setBlockRaw(worldX, y, worldZ, BlockType::IRON_ORE);
                else
                    setBlockRaw(worldX, y, worldZ, BlockType::STONE);
            } else {
                setBlockRaw(worldX, y, worldZ, BlockType::STONE);
            }
        }
    }

    // Water filling: every open-air block from the bed surface up to
    // WATER_LEVEL becomes WATER (no-op for dry columns where surface >= level).
    for (int y = surfaceHeight + 1; y <= WATER_LEVEL; ++y)
        setBlockRaw(worldX, y, worldZ, BlockType::WATER);
}

// ==========================================================
// Feature & foliage decoration
// ==========================================================

// Surface height cached during terrain generation (heightmap lookup). The
// explicit world-bounds guard matters: for x = -1, C++ truncates -1/16 to 0 so
// the chunk check alone would pass and then index the heightmap with a negative
// local coordinate (-1 % 16 = -1) - an out-of-bounds read.
int World::getSurfaceHeight(int x, int z) const {
    if (x < 0 || x >= WORLD_SIZE_X || z < 0 || z >= WORLD_SIZE_Z)
        return -1;
    int cx = x / CHUNK_SIZE;
    int cz = z / CHUNK_SIZE;
    const Heightmap* hm = heightmaps[(size_t)cx * CHUNK_COUNT_Z + cz].get();
    if (!hm) return -1;
    return static_cast<int>(hm->get(x % CHUNK_SIZE, z % CHUNK_SIZE, CHUNK_SIZE));
}

// Deterministic per-column pseudo-random value in [0, 1] - an integer hash, so
// feature placement is stable across runs without repeating noise patterns.
float World::hashNoise(int x, int z) const {
    uint64_t h = (uint64_t)x * 374761393u + (uint64_t)z * 668265263u + seed * 1442695040888963407ull;
    h = (h ^ (h >> 13)) * 1274126177ull;
    h = h ^ (h >> 16);
    return (float)(h & 0xFFFF) / 65535.0f;
}

// Deterministic per-block pseudo-random value in [0, 1] - same integer-hash
// family as hashNoise with the y axis mixed in, used for the iron depth fade.
float World::hashNoise3(int x, int y, int z) const {
    uint64_t h = (uint64_t)x * 374761393u + (uint64_t)y * 668265263u
               + (uint64_t)z * 2246822519u + seed * 1442695040888963407ull;
    h = (h ^ (h >> 13)) * 1274126177ull;
    h = h ^ (h >> 16);
    return (float)(h & 0xFFFF) / 65535.0f;
}

void World::generateFeatures() {
    for (int x = 0; x < WORLD_SIZE_X; ++x) {
        for (int z = 0; z < WORLD_SIZE_Z; ++z) {
            int h = getSurfaceHeight(x, z);
            if (h < 2 || h >= WORLD_SIZE_Y - 8) continue;  // room for the canopy

            BlockType top = getBlock(x, h, z);

            // --- Trees & vegetation grow only on GRASS columns. Every surface
            // column in this biome is grass-topped (see generateColumnTerrain),
            // so this guard only skips accidental non-surface blocks. ---
            if (top != BlockType::GRASS) continue;

            // --- Dense forest: 2% tree chance in light forest, up to 4% in the
            // jungle cores, modulated by a low-frequency forest mask. The thin
            // 2-block soil cap still lets trees, flowers and tall grass root
            // freely on every grass surface, including hilltops and apron edges. ---
            if (getBlock(x, h + 1, z) == BlockType::AIR) {
                float forest = fbm(x * 0.008f, z * 0.008f, 3, 2.0f, 0.5f);
                float density = 0.02f + std::max(0.0f, forest) * 0.02f;  // [0.02, 0.04]

                if (hashNoise(x, z) < density) {
                    // Spacing guardrail: skip if an earlier-spawned trunk sits
                    // within MIN_TREE_DIST blocks. Trunks always start at
                    // surface+1, so one probe per neighbour column suffices
                    // (earlier columns in scan order are already placed).
                    bool crowded = false;
                    for (int dx = -MIN_TREE_DIST; dx <= MIN_TREE_DIST && !crowded; ++dx) {
                        for (int dz = -MIN_TREE_DIST; dz <= MIN_TREE_DIST; ++dz) {
                            if (dx * dx + dz * dz > MIN_TREE_DIST * MIN_TREE_DIST) continue;
                            int nh = getSurfaceHeight(x + dx, z + dz);
                            if (nh >= 0 && getBlock(x + dx, nh + 1, z + dz) == BlockType::LOG) {
                                crowded = true;
                                break;
                            }
                        }
                    }
                    if (crowded) continue;

                    // Oak on gentle slopes, tall pine where the tree noise is high.
                    bool pine = fbm(x * 0.05f, z * 0.05f, 1, 2.0f, 0.5f) > 0.25f;
                    generateTree(x, h + 1, z, pine, hashNoise(x * 7 + 1, z * 13 + 5));
                    continue;
                }
            }

            // --- Thick foliage: 2D cross-sprites on top of the grass. ~13% of
            // columns get vegetation; of those, 85% is tall grass and the rest
            // splits evenly between red roses and yellow buttercups (7.5%
            // each). Dead bush is never spawned. ---
            if (getBlock(x, h + 1, z) == BlockType::AIR) {
                // Density roll: overall ~FOLIAGE_DENSITY per grass column.
                if (hashNoise(x + 31, z + 17) < FOLIAGE_DENSITY) {
                    // Species roll among the spawned vegetation: 85% tall grass,
                    // 7.5% red rose, 7.5% yellow buttercup.
                    float s = hashNoise(x + 47, z + 61);
                    if (s < GRASS_SHARE) {
                        setBlockRaw(x, h + 1, z, BlockType::TALL_GRASS);
                    } else if (s < GRASS_SHARE + ROSE_SHARE) {
                        setBlockRaw(x, h + 1, z, BlockType::RED_ROSE);
                    } else {
                        setBlockRaw(x, h + 1, z, BlockType::YELLOW_FLOWER);
                    }
                }
            }
        }
    }
    std::cout << "[WORLD] Features placed: dense forest, tall grass and flowers." << std::endl;
}

// Builds a tree rooted at (bx, by, bz) = first trunk block. The leaf canopy is
// generated first, then the LOG trunk is written AFTER it so the wooden core is
// guaranteed to penetrate up through the middle of the leaf layers (stopping
// one block below the absolute top leaf), overwriting any leaf on the trunk
// axis. A final pass thins fully-interior leaves for a softer canopy. Every
// write goes through setBlockRaw which bounds-checks against the whole world,
// so canopies that cross chunk borders (or the world edge) are silently
// clipped - never an out-of-bounds crash.
void World::generateTree(int bx, int by, int bz, bool pine, float r) {
    if (pine) {
        // Pine: tall thin trunk with layered foliage rings. Canopy first, then
        // the LOG core through the rings, stopping 1 block below the cone cap.
        int trunkH = 5 + (int)(r * 3.0f);                 // 5-7
        int topY = by + trunkH;

        std::vector<glm::ivec3> leafCells;
        for (int ly = 0; ly < 4; ++ly) {
            int layerY = topY - 1 - ly;
            int radius = (ly >= 3) ? 1 : 2;               // top ring shrinks
            for (int dx = -radius; dx <= radius; ++dx) {
                for (int dz = -radius; dz <= radius; ++dz) {
                    if (std::abs(dx) == radius && std::abs(dz) == radius)
                        continue;                         // rounded corners
                    int wx = bx + dx, wz = bz + dz;
                    if (getBlock(wx, layerY, wz) == BlockType::AIR) {
                        setBlockRaw(wx, layerY, wz, BlockType::LEAVES);
                        leafCells.push_back(glm::ivec3(wx, layerY, wz));
                    }
                }
            }
        }
        setBlockRaw(bx, topY, bz, BlockType::LEAVES);     // cone cap
        leafCells.push_back(glm::ivec3(bx, topY, bz));

        // Trunk penetration: LOG column written AFTER the canopy so it always
        // runs up through the rings, overwriting any leaf on the trunk axis.
        for (int i = 0; i < trunkH; ++i)
            setBlockRaw(bx, by + i, bz, BlockType::LOG);

        thinInteriorLeaves(leafCells);
    } else {
        // Oak: short trunk + round leafy canopy. The canopy is generated first,
        // then the LOG trunk is written AFTER it so the wooden core is
        // guaranteed to run up through the middle of the leaf layers (authentic
        // Minecraft oak), stopping one block below the absolute top leaf.
        int trunkH = 4 + (int)(r * 2.0f);                 // 4-5 base blocks
        int trunkTopY = by + trunkH;                      // trunk tip: 1 block below the top leaf

        // 1. Canopy: 5x5 lower + middle layers, 3x3 top cap (AIR-checked so
        //    terrain or other blocks are never replaced).
        std::vector<glm::ivec3> leafCells;
        for (int dy = -1; dy <= 1; ++dy) {
            int layerY = trunkTopY + dy;
            int radius = (dy == 1) ? 1 : 2;               // 3x3 cap on top, 5x5 below
            for (int dx = -radius; dx <= radius; ++dx) {
                for (int dz = -radius; dz <= radius; ++dz) {
                    if (radius == 2 && std::abs(dx) == radius && std::abs(dz) == radius)
                        continue;                         // rounded corners
                    int wx = bx + dx, wz = bz + dz;
                    if (getBlock(wx, layerY, wz) == BlockType::AIR) {
                        setBlockRaw(wx, layerY, wz, BlockType::LEAVES);
                        leafCells.push_back(glm::ivec3(wx, layerY, wz));
                    }
                }
            }
        }

        // 2. Trunk penetration: LOG column written AFTER the canopy so the
        //    central core always extends up through the leaves, overwriting
        //    any leaf that occupied the trunk axis.
        for (int i = by; i <= trunkTopY; ++i)
            setBlockRaw(bx, i, bz, BlockType::LOG);

        // 3. Inner-leaf thinning for a softer, lighter canopy.
        thinInteriorLeaves(leafCells);
    }
}

// Removes ~25% of fully-interior leaves (all six neighbours are LEAVES) so
// light filters through the canopy instead of a solid heavy block. The hash
// is deterministic per cell, so thinning is stable across runs. Cells that
// were overwritten by the trunk (now LOG) are skipped.
void World::thinInteriorLeaves(const std::vector<glm::ivec3>& leaves) {
    for (const glm::ivec3& p : leaves) {
        if (blockRef(p.x, p.y, p.z) != BlockType::LEAVES)
            continue;                                     // not a leaf anymore (trunk axis)
        if (blockRef(p.x + 1, p.y, p.z) != BlockType::LEAVES ||
            blockRef(p.x - 1, p.y, p.z) != BlockType::LEAVES ||
            blockRef(p.x, p.y + 1, p.z) != BlockType::LEAVES ||
            blockRef(p.x, p.y - 1, p.z) != BlockType::LEAVES ||
            blockRef(p.x, p.y, p.z + 1) != BlockType::LEAVES ||
            blockRef(p.x, p.y, p.z - 1) != BlockType::LEAVES)
            continue;                                     // edge leaf: keep for the outline
        if (hashNoise(p.x + p.y * 31, p.z + p.y * 17) < 0.25f)
            setBlockRaw(p.x, p.y, p.z, BlockType::AIR);   // thin the interior
    }
}

// ==========================================================
// Meshing & rendering
// ==========================================================

bool World::isSolidFaceExposed(int x, int y, int z) const {
    // A solid block's face is visible when the neighbour is out of bounds,
    // non-solid (AIR / plants), transparent WATER, translucent LEAVES, or
    // cutout GLASS. Including cutout types here ensures opaque blocks adjacent
    // to them still emit their faces so they show through the cutout holes.
    if (x < 0 || x >= WORLD_SIZE_X || y < 0 || y >= WORLD_SIZE_Y || z < 0 || z >= WORLD_SIZE_Z)
        return true;
    BlockType n = blockRef(x, y, z);
    return isNonSolid(n) || n == BlockType::WATER
                         || n == BlockType::LEAVES
                         || n == BlockType::GLASS;
}

void World::addFaceVertices(std::vector<float>& vertices, float x, float y, float z,
                            BlockFace face, BlockType type, float alpha) {
    UVRect uv = getBlockFaceUV(type, face);

    switch (face) {
        case BlockFace::TOP:
            vertices.insert(vertices.end(), {
                x,        y + 1.0f, z,           uv.uMin, uv.vMin, alpha,
                x,        y + 1.0f, z + 1.0f,    uv.uMin, uv.vMax, alpha,
                x + 1.0f, y + 1.0f, z + 1.0f,    uv.uMax, uv.vMax, alpha,

                x,        y + 1.0f, z,           uv.uMin, uv.vMin, alpha,
                x + 1.0f, y + 1.0f, z + 1.0f,    uv.uMax, uv.vMax, alpha,
                x + 1.0f, y + 1.0f, z,           uv.uMax, uv.vMin, alpha
            });
            break;

        case BlockFace::BOTTOM:
            vertices.insert(vertices.end(), {
                x,        y, z + 1.0f,    uv.uMin, uv.vMax, alpha,
                x,        y, z,           uv.uMin, uv.vMin, alpha,
                x + 1.0f, y, z,           uv.uMax, uv.vMin, alpha,

                x,        y, z + 1.0f,    uv.uMin, uv.vMax, alpha,
                x + 1.0f, y, z,           uv.uMax, uv.vMin, alpha,
                x + 1.0f, y, z + 1.0f,    uv.uMax, uv.vMax, alpha
            });
            break;

        case BlockFace::FRONT:
            vertices.insert(vertices.end(), {
                x,        y,        z + 1.0f,    uv.uMin, uv.vMax, alpha,
                x + 1.0f, y,        z + 1.0f,    uv.uMax, uv.vMax, alpha,
                x + 1.0f, y + 1.0f, z + 1.0f,    uv.uMax, uv.vMin, alpha,

                x,        y,        z + 1.0f,    uv.uMin, uv.vMax, alpha,
                x + 1.0f, y + 1.0f, z + 1.0f,    uv.uMax, uv.vMin, alpha,
                x,        y + 1.0f, z + 1.0f,    uv.uMin, uv.vMin, alpha
            });
            break;

        case BlockFace::BACK:
            vertices.insert(vertices.end(), {
                x + 1.0f, y,        z,    uv.uMin, uv.vMax, alpha,
                x,        y,        z,    uv.uMax, uv.vMax, alpha,
                x,        y + 1.0f, z,    uv.uMax, uv.vMin, alpha,

                x + 1.0f, y,        z,    uv.uMin, uv.vMax, alpha,
                x,        y + 1.0f, z,    uv.uMax, uv.vMin, alpha,
                x + 1.0f, y + 1.0f, z,    uv.uMin, uv.vMin, alpha
            });
            break;

        case BlockFace::LEFT:
            vertices.insert(vertices.end(), {
                x, y,        z,           uv.uMin, uv.vMax, alpha,
                x, y,        z + 1.0f,    uv.uMax, uv.vMax, alpha,
                x, y + 1.0f, z + 1.0f,    uv.uMax, uv.vMin, alpha,

                x, y,        z,           uv.uMin, uv.vMax, alpha,
                x, y + 1.0f, z + 1.0f,    uv.uMax, uv.vMin, alpha,
                x, y + 1.0f, z,           uv.uMin, uv.vMin, alpha
            });
            break;

        case BlockFace::RIGHT:
            vertices.insert(vertices.end(), {
                x + 1.0f, y,        z + 1.0f,    uv.uMin, uv.vMax, alpha,
                x + 1.0f, y,        z,           uv.uMax, uv.vMax, alpha,
                x + 1.0f, y + 1.0f, z,           uv.uMax, uv.vMin, alpha,

                x + 1.0f, y,        z + 1.0f,    uv.uMin, uv.vMax, alpha,
                x + 1.0f, y + 1.0f, z,           uv.uMax, uv.vMin, alpha,
                x + 1.0f, y + 1.0f, z + 1.0f,    uv.uMin, uv.vMin, alpha
            });
            break;
    }
}

void World::addCrossSpriteVertices(std::vector<float>& vertices, float x, float y, float z,
                                   BlockType type, float alpha) {
    // Two perpendicular quads form an X-shape billboard (Minecraft-style plant
    // sprite). The footprint is inset 0.15 so the blades never poke into the
    // neighbouring blocks, and the quad spans the full block height.
    UVRect uv = getBlockFaceUV(type, BlockFace::TOP); // whole-tile sprite UV

    // The atlas is uploaded with stbi flip DISABLED, so v = 0 is the TOP row of
    // the file and vMin < vMax goes top -> bottom of the tile. The sprite's
    // visible pixels sit at the BOTTOM of the tile (vMax), so the billboard's
    // bottom vertex must sample vMax and its top vertex vMin - otherwise the
    // plant renders upside down.
    const float vBottom = uv.vMax;
    const float vTop = uv.vMin;

    const float in = 0.15f;
    const float x0 = x + in, x1 = x + 1.0f - in;
    const float z0 = z + in, z1 = z + 1.0f - in;

    // Quad 1: diagonal (x0,z0) -> (x1,z1)
    vertices.insert(vertices.end(), {
        x0, y,     z0,    uv.uMin, vBottom, alpha,
        x1, y,     z1,    uv.uMax, vBottom, alpha,
        x1, y + 1.0f, z1, uv.uMax, vTop, alpha,

        x0, y,     z0,    uv.uMin, vBottom, alpha,
        x1, y + 1.0f, z1, uv.uMax, vTop, alpha,
        x0, y + 1.0f, z0, uv.uMin, vTop, alpha
    });

    // Quad 2: diagonal (x1,z0) -> (x0,z1)
    vertices.insert(vertices.end(), {
        x1, y,     z0,    uv.uMin, vBottom, alpha,
        x0, y,     z1,    uv.uMax, vBottom, alpha,
        x0, y + 1.0f, z1, uv.uMax, vTop, alpha,

        x1, y,     z0,    uv.uMin, vBottom, alpha,
        x0, y + 1.0f, z1, uv.uMax, vTop, alpha,
        x1, y + 1.0f, z0, uv.uMin, vTop, alpha
    });
}

void World::generateChunkMesh(int chunkX, int chunkZ) {
    Chunk* chunk = chunkAt(chunkX, chunkZ);
    if (!chunk || !chunk->generated) return;

    // Three vertex pools:
    //   vertices   – opaque geometry (Pass 1): grass/dirt/stone/sand/log/plants
    //   waterVerts – transparent water surface (Pass 2)
    //   leavesVerts– translucent leaves (Pass 3): drawn depth-mask=FALSE so
    //                alpha holes never occlude the opaque blocks behind them
    std::vector<float> vertices;
    std::vector<float> waterVerts;
    std::vector<float> leavesVerts;

    int startX = chunkX * CHUNK_SIZE;
    int startZ = chunkZ * CHUNK_SIZE;

    for (int x = startX; x < startX + CHUNK_SIZE; ++x) {
        for (int y = 0; y < WORLD_SIZE_Y; ++y) {
            for (int z = startZ; z < startZ + CHUNK_SIZE; ++z) {
                BlockType type = blockRef(x, y, z);
                if (type == BlockType::AIR) continue;

                float fx = static_cast<float>(x);
                float fy = static_cast<float>(y);
                float fz = static_cast<float>(z);

                // Cross-sprite plants are drawn as billboards, never as cubes.
                if (type == BlockType::TALL_GRASS || type == BlockType::RED_ROSE
                    || type == BlockType::YELLOW_FLOWER) {
                    addCrossSpriteVertices(vertices, fx, fy, fz, type);
                    continue;
                }

                if (type == BlockType::WATER) {
                    // Surface-only water topology: a lake/river is treated as a
                    // single surface volume, NOT stacked transparent cubes.
                    //   - TOP face: only when the block above is exactly AIR
                    //     (the water surface plane).
                    //   - NO internal faces ever: adjacent WATER blocks share no
                    //     faces, and no BOTTOM faces exist. The entire interior
                    //     underneath the surface plane is empty mesh.
                    //   - Side faces ONLY where the horizontal neighbour is AIR
                    //     (a waterfall / exposed water edge). Against solid
                    //     terrain (GRASS/DIRT/STONE/SAND) or other WATER blocks
                    //     no side face is generated. Checked independently of the
                    //     TOP condition so the top-most waterfall block also gets
                    //     its side face (no 1-block hole at waterfall tops).
                    // blockRef() is OOB-safe and resolves cross-chunk boundary
                    // lookups (x/z at 0 or 15) into the adjacent chunk's storage,
                    // so no dummy vertical walls appear at chunk borders - the
                    // surface stays continuous across the whole lake.
                    if (blockRef(x, y + 1, z) == BlockType::AIR)
                        addFaceVertices(waterVerts, fx, fy, fz, BlockFace::TOP,   type, 0.65f);
                    if (blockRef(x - 1, y, z) == BlockType::AIR)
                        addFaceVertices(waterVerts, fx, fy, fz, BlockFace::LEFT,  type, 0.65f);
                    if (blockRef(x + 1, y, z) == BlockType::AIR)
                        addFaceVertices(waterVerts, fx, fy, fz, BlockFace::RIGHT, type, 0.65f);
                    if (blockRef(x, y, z - 1) == BlockType::AIR)
                        addFaceVertices(waterVerts, fx, fy, fz, BlockFace::BACK,  type, 0.65f);
                    if (blockRef(x, y, z + 1) == BlockType::AIR)
                        addFaceVertices(waterVerts, fx, fy, fz, BlockFace::FRONT, type, 0.65f);
                    continue;
                } else if (type == BlockType::LEAVES) {
                    // Fancy Leaves: routed to the cutout pool (Pass 3).
                    // All 6 faces always emitted; inner leaves dim slightly.
                    bool inner = blockRef(x + 1, y, z) == BlockType::LEAVES
                              && blockRef(x - 1, y, z) == BlockType::LEAVES
                              && blockRef(x, y + 1, z) == BlockType::LEAVES
                              && blockRef(x, y - 1, z) == BlockType::LEAVES
                              && blockRef(x, y, z + 1) == BlockType::LEAVES
                              && blockRef(x, y, z - 1) == BlockType::LEAVES;
                    const float alpha = inner ? 0.75f : 1.0f;
                    addFaceVertices(leavesVerts, fx, fy, fz, BlockFace::TOP,    type, alpha);
                    addFaceVertices(leavesVerts, fx, fy, fz, BlockFace::BOTTOM, type, alpha);
                    addFaceVertices(leavesVerts, fx, fy, fz, BlockFace::LEFT,   type, alpha);
                    addFaceVertices(leavesVerts, fx, fy, fz, BlockFace::RIGHT,  type, alpha);
                    addFaceVertices(leavesVerts, fx, fy, fz, BlockFace::BACK,   type, alpha);
                    addFaceVertices(leavesVerts, fx, fy, fz, BlockFace::FRONT,  type, alpha);
                } else if (type == BlockType::GLASS) {
                    // Glass: same alpha-cutout pass as Leaves. All 6 faces
                    // always emitted (glass panes visible from any angle).
                    // alpha = 1.0 throughout - no inner-darkening needed.
                    addFaceVertices(leavesVerts, fx, fy, fz, BlockFace::TOP,    type, 1.0f);
                    addFaceVertices(leavesVerts, fx, fy, fz, BlockFace::BOTTOM, type, 1.0f);
                    addFaceVertices(leavesVerts, fx, fy, fz, BlockFace::LEFT,   type, 1.0f);
                    addFaceVertices(leavesVerts, fx, fy, fz, BlockFace::RIGHT,  type, 1.0f);
                    addFaceVertices(leavesVerts, fx, fy, fz, BlockFace::BACK,   type, 1.0f);
                    addFaceVertices(leavesVerts, fx, fy, fz, BlockFace::FRONT,  type, 1.0f);
                } else {
                    // Hidden Face Culling: emit a face only when the neighbour is
                    // out of bounds, AIR, or WATER (so the lake bed shows through
                    // the translucent water). Shared walls between neighbouring
                    // chunks are read from the other chunk's storage, so they are
                    // culled on both sides.
                    if (isSolidFaceExposed(x, y + 1, z))
                        addFaceVertices(vertices, fx, fy, fz, BlockFace::TOP, type);
                    if (isSolidFaceExposed(x, y - 1, z))
                        addFaceVertices(vertices, fx, fy, fz, BlockFace::BOTTOM, type);
                    if (isSolidFaceExposed(x - 1, y, z))
                        addFaceVertices(vertices, fx, fy, fz, BlockFace::LEFT, type);
                    if (isSolidFaceExposed(x + 1, y, z))
                        addFaceVertices(vertices, fx, fy, fz, BlockFace::RIGHT, type);
                    if (isSolidFaceExposed(x, y, z - 1))
                        addFaceVertices(vertices, fx, fy, fz, BlockFace::BACK, type);
                    if (isSolidFaceExposed(x, y, z + 1))
                        addFaceVertices(vertices, fx, fy, fz, BlockFace::FRONT, type);
                }
            }
        }
    }

    // Shared VAO/VBO upload for both mesh pools (pos + uv + alpha, stride 6).
    auto uploadMesh = [](GLuint& vao, GLuint& vbo, const std::vector<float>& data) {
        if (vao == 0) glGenVertexArrays(1, &vao);
        if (vbo == 0) glGenBuffers(1, &vbo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STATIC_DRAW);

        GLsizei stride = 6 * sizeof(float);

        // Position attribute (location = 0)
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);

        // Texture UV attribute (location = 1)
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));

        // Alpha attribute (location = 2) - per-vertex translucency (inner leaves)
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (void*)(5 * sizeof(float)));

        glBindVertexArray(0);
    };

    // Pass 1 buffers: opaque mesh. Skipped when empty.
    chunk->vertexCount = static_cast<int>(vertices.size()) / 6;
    if (chunk->vertexCount > 0)
        uploadMesh(chunk->VAO, chunk->VBO, vertices);

    // Pass 2 buffers: transparent water surface mesh. Dry chunks allocate nothing.
    chunk->waterVertexCount = static_cast<int>(waterVerts.size()) / 6;
    if (chunk->waterVertexCount > 0)
        uploadMesh(chunk->waterVAO, chunk->waterVBO, waterVerts);

    // Pass 3 buffers: transparent leaves mesh. Chunks with no trees allocate nothing.
    chunk->leavesVertexCount = static_cast<int>(leavesVerts.size()) / 6;
    if (chunk->leavesVertexCount > 0)
        uploadMesh(chunk->leavesVAO, chunk->leavesVBO, leavesVerts);
}

void World::render(int playerChunkX, int playerChunkZ) const {
    // Only iterate the chunk sub-range within the render radius (clamped to world).
    int xMin = std::max(0, playerChunkX - RENDER_DISTANCE);
    int xMax = std::min(CHUNK_COUNT_X - 1, playerChunkX + RENDER_DISTANCE);
    int zMin = std::max(0, playerChunkZ - RENDER_DISTANCE);
    int zMax = std::min(CHUNK_COUNT_Z - 1, playerChunkZ + RENDER_DISTANCE);

    // ============================================================
    // Pass 1 - Opaque + Flora geometry
    // Grass/dirt/stone/sand/log + cross-sprite plants (tall grass,
    // flowers). Cross-sprites are baked into the same VBO as opaque
    // cubes. Culling is DISABLED for the whole pass so the X-shaped
    // plant quads render from every camera angle - the depth buffer
    // alone handles solid cube occlusion correctly.
    // Depth writes ON so solid blocks write correct depth values.
    // ============================================================
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);   // flora (cross-sprites) must be 2-sided
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (int cx = xMin; cx <= xMax; ++cx) {
        for (int cz = zMin; cz <= zMax; ++cz) {
            const Chunk* chunk = chunkAt(cx, cz);
            if (chunk && chunk->vertexCount > 0 && chunk->VAO != 0) {
                glBindVertexArray(chunk->VAO);
                glDrawArrays(GL_TRIANGLES, 0, chunk->vertexCount);
            }
        }
    }
    glBindVertexArray(0);

    // ============================================================
    // Pass 2 - Transparent water surface
    // Surface-only topology (TOP + exposed side faces only).
    // glDepthMask(GL_FALSE): the water surface must NOT write depth.
    // Without this the translucent water pixels overwrite the depth
    // buffer and occlude the sand/stone lake-bed behind them, making
    // the water appear opaque. With writes OFF, the already-written
    // opaque bed tiles show through the blended water colour.
    // Culling OFF: surface visible from below water too.
    // ============================================================
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);   // don't occlude the lake bed
    glDisable(GL_CULL_FACE);
    // Vibrant tropical cyan-blue water tint: set uColorTint on the currently active shader program.
    // We use raw GL calls so World.cpp doesn't need to include Shader.hpp.
    GLint waterProg = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &waterProg);
    if (waterProg) {
        GLint tintLoc = glGetUniformLocation(waterProg, "uColorTint");
        if (tintLoc >= 0) glUniform4f(tintLoc, 0.2f, 0.55f, 0.85f, 1.0f);
    }
    for (int cx = xMin; cx <= xMax; ++cx) {
        for (int cz = zMin; cz <= zMax; ++cz) {
            const Chunk* chunk = chunkAt(cx, cz);
            if (chunk && chunk->waterVertexCount > 0 && chunk->waterVAO != 0) {
                glBindVertexArray(chunk->waterVAO);
                glDrawArrays(GL_TRIANGLES, 0, chunk->waterVertexCount);
            }
        }
    }
    // Reset tint to neutral so subsequent passes render with no tint.
    if (waterProg) {
        GLint tintLoc = glGetUniformLocation(waterProg, "uColorTint");
        if (tintLoc >= 0) glUniform4f(tintLoc, 1.0f, 1.0f, 1.0f, 1.0f);
    }
    glBindVertexArray(0);

    // ============================================================
    // Pass 3 - Leaves (Alpha Cutout / Minecraft "Fast Leaves" mode)
    // The fragment shader discards every pixel with alpha < 0.5, so the
    // depth buffer only ever receives fully-opaque surviving texels.
    // Because no semi-transparent pixel reaches the depth buffer, draw
    // order between leaf faces is irrelevant - no face-overlap artifact.
    //
    // Key states:
    //   glDepthMask(GL_TRUE)   – depth writes ON (safe: only opaque pixels survive)
    //   glDisable(GL_BLEND)    – no blending needed; output alpha is forced 1.0
    //   glDisable(GL_CULL_FACE)– leaves must render from every angle (inside canopy)
    // ============================================================
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    for (int cx = xMin; cx <= xMax; ++cx) {
        for (int cz = zMin; cz <= zMax; ++cz) {
            const Chunk* chunk = chunkAt(cx, cz);
            if (chunk && chunk->leavesVertexCount > 0 && chunk->leavesVAO != 0) {
                glBindVertexArray(chunk->leavesVAO);
                glDrawArrays(GL_TRIANGLES, 0, chunk->leavesVertexCount);
            }
        }
    }
    glBindVertexArray(0);

    // ---- Restore clean baseline state for callers (HUD / next frame) ----
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
}

int World::getVertexCount() const {
    int total = 0;
    for (const auto& chunk : chunks) {
        if (chunk) total += chunk->vertexCount + chunk->waterVertexCount + chunk->leavesVertexCount;
    }
    return total;
}

// ==========================================================
// Block interaction
// ==========================================================

BlockType World::getBlock(int x, int y, int z) const {
    return blockRef(x, y, z);
}

BlockType World::getBlock(const glm::ivec3& pos) const {
    return getBlock(pos.x, pos.y, pos.z);
}

bool World::setBlock(int x, int y, int z, BlockType type) {
    if (x < 0 || x >= WORLD_SIZE_X || y < 0 || y >= WORLD_SIZE_Y || z < 0 || z >= WORLD_SIZE_Z)
        return false;

    int cx = x / CHUNK_SIZE;
    int cz = z / CHUNK_SIZE;
    Chunk* chunk = chunkAt(cx, cz);
    if (!chunk) return false;

    BlockType& cell = chunk->blocks[(size_t)(y * CHUNK_SIZE + (x % CHUNK_SIZE)) * CHUNK_SIZE + (z % CHUNK_SIZE)];
    if (cell == type)
        return false;
    cell = type;

    // Rebuild the chunk containing the change. If the block sits on a chunk
    // border, also rebuild the neighbouring chunk(s) whose culled faces could
    // be affected by the change.
    generateChunkMesh(cx, cz);

    if (x % CHUNK_SIZE == 0 && cx > 0)
        generateChunkMesh(cx - 1, cz);
    if (x % CHUNK_SIZE == CHUNK_SIZE - 1 && cx < CHUNK_COUNT_X - 1)
        generateChunkMesh(cx + 1, cz);
    if (z % CHUNK_SIZE == 0 && cz > 0)
        generateChunkMesh(cx, cz - 1);
    if (z % CHUNK_SIZE == CHUNK_SIZE - 1 && cz < CHUNK_COUNT_Z - 1)
        generateChunkMesh(cx, cz + 1);

    return true;
}

void World::setBlock(const glm::ivec3& pos, BlockType type) {
    setBlock(pos.x, pos.y, pos.z, type);
}

bool World::isBlockSolidAt(int x, int y, int z) const {
    // Out-of-bounds = solid wall (prevents the player leaving the world).
    if (x < 0 || x >= WORLD_SIZE_X || y < 0 || y >= WORLD_SIZE_Y || z < 0 || z >= WORLD_SIZE_Z)
        return true;
    return isBlockSolid(blockRef(x, y, z));
}

bool World::raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDist, float step,
                    int& hitX, int& hitY, int& hitZ,
                    int& placeX, int& placeY, int& placeZ) const {
    // The block the camera currently occupies is skipped (don't break yourself).
    glm::ivec3 camBlock((int)std::floor(origin.x), (int)std::floor(origin.y), (int)std::floor(origin.z));
    glm::ivec3 prevBlock = camBlock;

    for (float t = 0.0f; t <= maxDist; t += step) {
        glm::vec3 pos = origin + dir * t;
        int bx = (int)std::floor(pos.x);
        int by = (int)std::floor(pos.y);
        int bz = (int)std::floor(pos.z);

        if (bx == camBlock.x && by == camBlock.y && bz == camBlock.z) {
            prevBlock = glm::ivec3(bx, by, bz);
            continue;
        }

        if (bx >= 0 && bx < WORLD_SIZE_X && by >= 0 && by < WORLD_SIZE_Y && bz >= 0 && bz < WORLD_SIZE_Z) {
            // Only stop the ray on blocks that have a full 1×1×1 hitbox.
            // isBlockSolid() skips AIR, WATER, tall grass, and flowers so the
            // ray passes through them and targets solid blocks behind/below
            // (e.g. stone under water, terrain behind flowers).
            if (isBlockSolid(blockRef(bx, by, bz))) {
                hitX = bx; hitY = by; hitZ = bz;

                // Face-normal placement: the ray enters the hit block through the
                // face it shares with the previously sampled block, so the outward
                // normal of that face is (prevBlock - hit). The placement cell is
                // then hit + normal (clamped to unit components for grazing hits).
                glm::ivec3 normal(
                    glm::clamp(prevBlock.x - bx, -1, 1),
                    glm::clamp(prevBlock.y - by, -1, 1),
                    glm::clamp(prevBlock.z - bz, -1, 1)
                );
                placeX = bx + normal.x;
                placeY = by + normal.y;
                placeZ = bz + normal.z;
                return true;
            }
        }
        prevBlock = glm::ivec3(bx, by, bz);
    }
    return false;
}
