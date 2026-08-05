#ifndef WORLD_HPP
#define WORLD_HPP

#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <glm/glm.hpp>
#include <glad/glad.h>
#include "Block.hpp"

// ==========================================================
// Heightmap
// Caches the generated surface height for every column of one
// chunk, so terrain stays consistent between neighbouring
// chunks (and can later be reused for biome/variation passes).
// ==========================================================
struct Heightmap {
    glm::ivec2 offset;               // world origin of the chunk in blocks (x, z)
    std::vector<int64_t> data;       // one surface height per local column
    bool generated = false;

    Heightmap(glm::ivec2 off, size_t width, size_t depth)
        : offset(off), data(width * depth, -1) {}

    inline int64_t get(int x, int z, size_t chunkWidth) const {
        return data[x * chunkWidth + z];
    }

    inline void set(int x, int z, int64_t y, size_t chunkWidth) {
        data[x * chunkWidth + z] = y;
    }
};

// Forward declaration - Chunk is defined after World so it can
// reuse the World dimension constants for its block storage.
class Chunk;

// ==========================================================
// World
// Owns all chunks (smart pointers) + their heightmaps and is
// responsible for terrain generation, mesh building and
// chunk-culled rendering.
// ==========================================================
class World {
public:
    // --- Chunk-based world dimensions ---
    static const int CHUNK_SIZE      = 16;   // blocks per chunk (x/z)
    static const int CHUNK_COUNT_X   = 32;   // 32 x 32 chunks = 1024 loaded
    static const int CHUNK_COUNT_Z   = 32;
    static const int WORLD_SIZE_X    = CHUNK_SIZE * CHUNK_COUNT_X;  // 512
    static const int WORLD_SIZE_Y    = 256;
    static const int WORLD_SIZE_Z    = CHUNK_SIZE * CHUNK_COUNT_Z;  // 512
    static const int RENDER_DISTANCE = 16;   // chunks rendered around the player (covers the 32x32 grid)

    // --- Terrain tuning (rolling plains + hills + rivers & lakes) ---
    static const int WATER_LEVEL    = 62;   // world water level; river/lake beds dip below this
    static const int PLAIN_MIN_Y = 62;      // plains sit at/above water level: only carved rivers/lakes flood
    static const int PLAIN_MAX_Y = 68;
    static const int HILL_AMP_Y   = 26;  // hills rise up to ~26 blocks above the local plain
    static const int HILL_OCTAVES = 4;   // localized hill-field FBM octaves
    static const int DIRT_DEPTH   = 2;   // soil cap: GRASS + 2 DIRT below it, so STONE shows on steep slopes
    static const int APRON_TRIGGER = 4;  // neighbour at least this much taller => cliff foot
    static const int APRON_MAX_H   = 3;  // tallest apron ramp (1-3 blocks) at a cliff base
    static const int APRON_SPREAD  = 3;  // apron tapers outward over this many columns
    static const inline float PLAIN_FREQ   = 0.008f; // broad rolling plains
    static const inline float HILL_FREQ    = 0.02f;  // localized hills (small-to-medium)
    static const inline float HILL_GATE_LO = 0.55f;  // hill-field gate: below -> stays plain
    static const inline float HILL_GATE_HI = 0.70f;  // gate value where hill strength is full
    static const inline float HILL_CURVE   = 0.6f;   // gentler flanks so hills melt into the plains

    // --- River & lake carving (wide rivers + large basins) ---
    // Thresholds are applied to world-wide NORMALISED fields ([0,1]) so the
    // water coverage stays stable regardless of raw stb_perlin bias. RIVER_THRESH
    // is deliberately high (with a low frequency + strong warp) so channels open
    // up into wide, majestic rivers ~6-15 blocks across instead of 1-2 block
    // streams; LAKE_THRESH opens occasional large, contiguous basins.
    static const inline float RIVER_FREQ   = 0.02f;  // lower frequency => fewer, longer, cleaner rivers
    static const inline float RIVER_THRESH = 0.20f;  // normalised |riverNoise| below this => channel
    static const inline int   RIVER_DEPTH  = 5;      // flat-bed depth below the plain (3-5 under water)
    // Domain warping applied BEFORE the river ridge: a low-frequency (wavelength
    // ~200 blocks) displacement of up to +-40 blocks bends the |noise| valleys
    // into long organic meanders and breaks the repetitive oxbow loops that
    // plain |abs(Noise)| produces. The two axes use decorrelated seeds so the
    // path cannot fold back into ring-like shapes.
    static const inline float RIVER_WARP_FREQ = 0.005f;
    static const inline float RIVER_WARP_AMP  = 40.0f;
    static const inline float LAKE_FREQ    = 0.005f; // single smooth field => ONE huge unified lake basin
    static const inline float LAKE_THRESH  = 0.09f;  // normalised lake field below this => basin
    static const inline int   LAKE_DEPTH   = 7;      // basin depth: pulls beds down to WATER_LEVEL - 7
    static const int MIN_TERRAIN_Y = 55;  // hard floor: rivers/lakes never carve below this

    // --- Ore vein tuning (coal + iron inside the STONE layer) ---
    // A single-octave 3D Perlin field is sampled per block at ORE_FREQ scale.
    // Thresholds were tuned against the real stb_perlin_noise3(x*0.15f, y*0.15f,
    // z*0.15f) distribution measured over the world volume:
    //   P(v > +0.47) ~= 4.2%  (coal on exposed cliff faces)
    //   P(v > +0.54) ~= 2.0%  (coal, buried rock)
    //   P(v < -0.56) ~= 1.6%  (iron on exposed cliff faces)
    //   P(v < -0.60) ~= 0.9%  (iron, buried rock)
    // Ore generation ONLY replaces STONE - never dirt/grass/sand/water. The
    // cliff-face threshold is applied when a horizontal neighbour column's
    // surface sits below the block (AIR beside it), so visible stone walls
    // and cliff sides carry noticeably more veins than buried rock.
    static const inline float ORE_FREQ              = 0.15f; // noise sample scale (per block)
    static const inline float ORE_COAL_THRESH       = 0.54f; // buried coal vein threshold (+tail)
    static const inline float ORE_COAL_CLIFF_THRESH = 0.47f; // exposed cliff-face coal (denser)
    static const inline float ORE_IRON_THRESH       = -0.60f; // buried iron vein threshold (-tail)
    static const inline float ORE_IRON_CLIFF_THRESH = -0.56f; // exposed cliff-face iron (denser)
    static const int ORE_IRON_MAX_Y = 60;  // full iron density below this elevation
    static const int ORE_IRON_FADE_Y = 80; // iron fades to zero by this elevation

    // --- Shoreline material (strict sand guardrail) ---
    // Shore noise is normalised world-wide to [0,1]. SAND may only be placed on
    // the single ring of columns IMMEDIATELY adjacent to water at the waterline
    // (surface == WATER_LEVEL or WATER_LEVEL + 1), and only when the high
    // threshold is crossed - so sand covers ~15-20% of river/lake edges and the
    // other ~80% keep green GRASS touching the water.
    static const inline float SHORE_FREQ        = 0.03f; // shore-noise frequency
    static const inline float SHORE_SAND_THRESH = 0.65f; // normalised shore > 0.65 => rare SAND patch
    static const int MESH_CHUNKS_PER_FRAME = 16;  // chunk meshes built per frame on the main thread

    // --- Foliage tuning (forest biome: tall grass + roses + buttercups) ---
    static const inline float FOLIAGE_DENSITY = 0.13f;   // overall ~13% of grass columns get vegetation
    static const inline float GRASS_SHARE     = 0.85f;   // share of vegetation that is tall grass
    static const inline float ROSE_SHARE      = 0.075f;  // share of vegetation that is red roses
                                                         //   remainder (7.5%) = yellow buttercups
    static const int MIN_TREE_DIST = 5;   // min trunk-to-trunk spacing (blocks) - prevents clustered trees

    World();                // calls init() so the terrain exists right away
    ~World();

    void init();            // allocate chunks/heightmaps + start background terrain generation
    void update();          // drain the chunk-mesh queue a few meshes per frame (main thread)

    bool isTerrainReady() const;   // true once the background thread finished all block writes
    void render(int playerChunkX, int playerChunkZ) const;
    int getVertexCount() const;

    // --- Coordinate helpers (replace the old C-style macros) ---
    static glm::ivec3 posToOffset(const glm::ivec3& pos);
    static glm::ivec3 posToChunkPos(const glm::ivec3& pos);
    bool inBounds(const glm::ivec3& offset) const;
    size_t getChunkIndex(const glm::ivec3& offset) const;

    // --- Block interaction (vec3 style) ---
    BlockType getBlock(const glm::ivec3& pos) const;
    void setBlock(const glm::ivec3& pos, BlockType type);

    // --- Block interaction (int style, used by main.cpp) ---
    BlockType getBlock(int x, int y, int z) const;
    bool setBlock(int x, int y, int z, BlockType type);  // true if changed

    // Voxel raycast: step along `dir` (normalized) from `origin` up to `maxDist`
    // with `step` increments. On success fills the hit block coordinate and the
    // placement coordinate (hit block + outward face normal). The block the
    // camera occupies is skipped so you cannot break yourself.
    bool raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDist, float step,
                 int& hitX, int& hitY, int& hitZ,
                 int& placeX, int& placeY, int& placeZ) const;

    // Returns true when the block at (x,y,z) has a full 1×1×1 solid hitbox.
    // Out-of-bounds coordinates return true (world edge = solid wall).
    bool isBlockSolidAt(int x, int y, int z) const;

    // --- Procedural terrain generation (noise pipeline + layering) ---
    void generateColumnTerrain(int worldX, int worldZ, int surfaceHeight);

    // --- Feature & foliage decoration ---
    void generateFeatures();                       // trees + vegetation pass
    void generateTree(int bx, int by, int bz, bool pine, float r);
    int getSurfaceHeight(int x, int z) const;      // cached heightmap lookup

private:
    size_t chunkSize = CHUNK_SIZE;
    glm::ivec3 centerOffset{0};   // reserved for future camera-centered generation
    glm::ivec3 chunksOrigin{0};   // world origin of the chunk grid
    uint64_t seed = 1337;         // reserved for future seeded noise

    // OOP storage: each chunk owns its blocks + GPU mesh, each chunk has a
    // heightmap. Both vectors are indexed by getChunkIndex().
    std::vector<std::shared_ptr<Chunk>> chunks;
    std::vector<std::unique_ptr<Heightmap>> heightmaps;

    // --- Async generation state (CPU work on a worker thread, meshing on main) ---
    std::thread genThread;                       // background terrain-generation thread
    std::atomic<bool> terrainReady{ false };     // all block writes finished (meshing may start)
    std::atomic<bool> cancelled{ false };        // world is being destroyed: stop the worker
    std::vector<int> meshQueue;                  // chunk indices awaiting GPU mesh build
    std::mutex meshMutex;                        // guards meshQueue
    bool meshLogDone = false;                    // print the summary log once, when the queue empties

    void drainMeshQueue();                       // mesh up to MESH_CHUNKS_PER_FRAME queued chunks

    // --- Internal helpers ---
    void generateHeightfield();   // noise pipeline + world-wide normalisation -> heightmaps
    void carveRiversAndLakes(std::vector<int>& heights) const; // trench + basin passes
    void generateShorelines();    // SAND beaches along water edges + SAND river/lake bottoms
    Chunk* chunkAt(int chunkX, int chunkZ);
    const Chunk* chunkAt(int chunkX, int chunkZ) const;
    BlockType blockRef(int x, int y, int z) const;          // OOB-safe read
    void setBlockRaw(int x, int y, int z, BlockType type);  // write without mesh rebuild
    bool isSolidFaceExposed(int x, int y, int z) const;
    void addFaceVertices(std::vector<float>& vertices, float x, float y, float z,
                         BlockFace face, BlockType type, float alpha = 1.0f);
    void addCrossSpriteVertices(std::vector<float>& vertices, float x, float y, float z,
                                BlockType type, float alpha = 1.0f);
    void thinInteriorLeaves(const std::vector<glm::ivec3>& leaves);  // remove ~25% of fully-internal leaves
    float hashNoise(int x, int z) const;    // deterministic per-column pseudo-random [0,1]
    float hashNoise3(int x, int y, int z) const; // deterministic per-block pseudo-random [0,1]
    void generateChunkMesh(int chunkX, int chunkZ);
};

// ==========================================================
// Chunk
// A single 16x16 column of the world (full height). Owns its
// own block storage and GPU mesh buffers (VAO/VBO).
// Block storage index: (y * CHUNK_SIZE + x) * CHUNK_SIZE + z
// ==========================================================
class Chunk {
public:
    glm::ivec2 offset{0};              // chunk coordinates (cx, cz)
    std::vector<BlockType> blocks;     // CHUNK_SIZE * WORLD_SIZE_Y * CHUNK_SIZE
    bool generated = false;

    // Pass 1 (opaque) mesh buffers: grass/dirt/stone/sand/log/plants.
    // Leaves are deliberately excluded - they live in leavesVAO/VBO.
    GLuint VAO = 0;
    GLuint VBO = 0;
    int vertexCount = 0;

    // Pass 2 (transparent water) mesh buffers - surface-only topology: the
    // lake/river is drawn as a single surface volume, never stacked cubes.
    GLuint waterVAO = 0;
    GLuint waterVBO = 0;
    int waterVertexCount = 0;

    // Pass 3 (transparent leaves) mesh buffers - drawn after all opaque
    // geometry with glDepthMask(GL_FALSE) so leaf alpha-holes do NOT occlude
    // opaque blocks (GRASS/DIRT/STONE) that are visible through the gaps.
    GLuint leavesVAO = 0;
    GLuint leavesVBO = 0;
    int leavesVertexCount = 0;

    Chunk(int cx, int cz)
        : offset(cx, cz),
          blocks((size_t)World::CHUNK_SIZE * World::CHUNK_SIZE * World::WORLD_SIZE_Y,
                 BlockType::AIR) {}
};

#endif // WORLD_HPP
