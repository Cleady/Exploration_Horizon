#ifndef BLOCK_HPP
#define BLOCK_HPP

enum class BlockType {
    AIR = 0,
    GRASS = 1,
    DIRT = 2,
    STONE = 3,
    SAND = 4,
    GRAVEL = 5,
    WATER = 6,
    LOG = 7,         // tree trunk (cube, solid)
    LEAVES = 8,      // tree canopy (cube, translucent)
    SNOW = 9,          // snowy mountain cap
    TALL_GRASS = 10,   // 2D cross-sprite plant (non-solid)
    RED_ROSE = 11,     // 2D cross-sprite plant (non-solid)
    YELLOW_FLOWER = 12, // 2D cross-sprite plant (non-solid)
    COAL_ORE = 13,      // ore vein inside STONE (dark speckled tile (4,0))
    IRON_ORE = 14,      // ore vein inside STONE (beige speckled tile (5,0))
    PLANKS = 15,        // wooden planks (solid cube)
    GLASS = 16,         // glass (cutout transparent cube)
    COBBLESTONE = 17    // cobblestone (solid cube)
};

enum class BlockFace {
    TOP,
    BOTTOM,
    LEFT,
    RIGHT,
    FRONT,
    BACK
};

struct UVRect {
    float uMin;
    float vMin;
    float uMax;
    float vMax;
};

UVRect getBlockFaceUV(BlockType type, BlockFace face);

// Returns true if this block type has a full 1×1×1 solid hitbox:
//   - collidable by physics
//   - targetable by the player raycast (can be broken / used as placement surface)
// Returns false for pass-through / non-collidable types (AIR, WATER, plants).
bool isBlockSolid(BlockType type);

#endif // BLOCK_HPP
