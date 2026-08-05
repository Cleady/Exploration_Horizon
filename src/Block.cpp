#include "Block.hpp"

static UVRect getTileUV(int tileX, int tileY) {
    float tileSize = 1.0f / 16.0f; // 0.0625f
    UVRect uv;
    uv.uMin = static_cast<float>(tileX) * tileSize;
    uv.uMax = static_cast<float>(tileX + 1) * tileSize;
    uv.vMin = static_cast<float>(tileY) * tileSize;
    uv.vMax = static_cast<float>(tileY + 1) * tileSize;
    return uv;
}

UVRect getBlockFaceUV(BlockType type, BlockFace face) {
    switch (type) {
        case BlockType::GRASS:
            if (face == BlockFace::TOP)
                return getTileUV(0, 0); // Grass Top
            else if (face == BlockFace::BOTTOM)
                return getTileUV(2, 0); // Dirt
            else
                return getTileUV(1, 0); // Grass Side

        case BlockType::DIRT:
            return getTileUV(2, 0);

        case BlockType::STONE:
            return getTileUV(3, 0);

        // Ore veins share the stone base tile family in atlas Row 0:
        // (3,0) plain stone, (4,0) coal ore (black specks), (5,0) iron ore
        // (beige specks), (6,0) gravel.
        case BlockType::COAL_ORE:
            return getTileUV(4, 0);  // dark speckled coal ore

        case BlockType::IRON_ORE:
            return getTileUV(5, 0);  // metallic beige speckled iron ore

        case BlockType::SAND:
            return getTileUV(0, 1);  // beige sand tile

        case BlockType::GRAVEL:
            return getTileUV(6, 0);  // mixed pebble gravel (Row 0, Col 6)

        case BlockType::LOG:
            if (face == BlockFace::TOP || face == BlockFace::BOTTOM)
                return getTileUV(3, 1);  // wood rings
            return getTileUV(2, 1);      // dark bark

        case BlockType::LEAVES:
            return getTileUV(4, 1);  // translucent leafy green

        case BlockType::SNOW:
            return getTileUV(3, 2);  // white snow tile

        case BlockType::TALL_GRASS:
            return getTileUV(2, 3);  // green tall-grass sprite (cross-quad) [Row 3, Col 2]

        case BlockType::RED_ROSE:
            return getTileUV(0, 3);  // red rose sprite (cross-quad) [Row 3, Col 0]

        case BlockType::YELLOW_FLOWER:
            return getTileUV(1, 3);  // yellow buttercup sprite (cross-quad) [Row 3, Col 1]

        // NOTE: atlas tile (3, 3) - the dead bush - is intentionally never
        // mapped to any BlockType, so dry bushes can never appear in the world.

        case BlockType::WATER:
            return getTileUV(0, 15); // blue translucent water tiles fill the bottom atlas row

        case BlockType::PLANKS:
            return getTileUV(6, 1);  // wooden planks tile (col 6, row 1)

        case BlockType::GLASS:
            return getTileUV(1, 1);  // glass tile (col 1, row 1)

        case BlockType::COBBLESTONE:
            return getTileUV(2, 2);  // cobblestone tile (col 2, row 2)

        default:
            return getTileUV(0, 0);
    }
}

// isBlockSolid — canonical hitbox / collision table.
// true  → full 1×1×1 AABB; targetable by raycast, blocks player movement.
// false → pass-through; ray and physics ignore this block.
bool isBlockSolid(BlockType type) {
    switch (type) {
        // --- Solid cubes (full hitbox) ---
        case BlockType::GRASS:
        case BlockType::DIRT:
        case BlockType::STONE:
        case BlockType::SAND:
        case BlockType::GRAVEL:
        case BlockType::LOG:
        case BlockType::LEAVES:
        case BlockType::SNOW:
        case BlockType::COAL_ORE:
        case BlockType::IRON_ORE:
        case BlockType::PLANKS:
        case BlockType::GLASS:
        case BlockType::COBBLESTONE:
            return true;

        // --- Non-collidable / pass-through ---
        case BlockType::AIR:
        case BlockType::WATER:
        case BlockType::TALL_GRASS:
        case BlockType::RED_ROSE:
        case BlockType::YELLOW_FLOWER:
            return false;

        default:
            return true;   // unknown future blocks are solid by default
    }
}
