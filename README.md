Reviewing project files to gather accurate details for the overview.

Searched for regex `Minecraft|Exploration|CloudRenderer|Hotbar|World|Camera|Texture|Shader` (`**/*.{cpp,hpp,md,txt}`), 626 results

Read [](file:///c%3A/Users/anhqu/Documents/Computer%20Science%20Training/PRIVATE%20PROJECT/Exploration_Horizon/CMakeLists.txt)

Read [](file:///c%3A/Users/anhqu/Documents/Computer%20Science%20Training/PRIVATE%20PROJECT/Exploration_Horizon/src/main.cpp#1-1), lines 1 to 220

Checking core gameplay and control details so the overview is accurate.

Read [](file:///c%3A/Users/anhqu/Documents/Computer%20Science%20Training/PRIVATE%20PROJECT/Exploration_Horizon/src/main.cpp#220-220), lines 220 to 420

Read [](file:///c%3A/Users/anhqu/Documents/Computer%20Science%20Training/PRIVATE%20PROJECT/Exploration_Horizon/include/World.hpp#1-1), lines 1 to 220

Read [](file:///c%3A/Users/anhqu/Documents/Computer%20Science%20Training/PRIVATE%20PROJECT/Exploration_Horizon/include/Hotbar.hpp)

Reviewing the build script to include accurate usage instructions.

Read [](file:///c%3A/Users/anhqu/Documents/Computer%20Science%20Training/PRIVATE%20PROJECT/Exploration_Horizon/build.bat)

## Project Overview

**Exploration Horizon** is a personal Minecraft-inspired remake built in C++ with OpenGL. It recreates the feel of a childhood memory of **Exploration Lite**, turning that nostalgia into a 3D voxel world where you can explore, break blocks, place blocks, and move through procedurally generated terrain.

## Purpose

This project is designed as a nostalgic remake of Minecraft-style exploration based on your own childhood memory of **Exploration Lite**. The goal is to capture:
- the sense of open-world exploration
- simple block-based building and mining
- natural terrain, rivers, lakes, and forests
- a cozy retro vibe from the games of your youth

## Key Features

- Procedural voxel world
  - 512 × 512 × 256 block world
  - 32 × 32 chunk grid
  - terrain with hills, plains, rivers, lakes, sand, and forests
- Player mechanics
  - first-person camera
  - walking with gravity
  - flying mode
  - collision detection and smooth wall sliding
- Block interaction
  - left-click to break blocks
  - right-click to place selected block from hotbar
- Hotbar UI
  - 9-slot hotbar
  - mouse wheel or number keys to switch blocks
- Graphics
  - OpenGL rendering
  - textured block atlas
  - sky/cloud layer
  - sun and underwater tint effects
- Modern build setup
  - CMake project
  - GLFW and GLM dependencies fetched automatically
  - Windows build script included

## How to Build & Run

### Windows (recommended)
From the project root, run:
```bat
build.bat
```

That script:
1. creates build
2. configures with CMake
3. builds the project in `Release`
4. falls back to `Debug` if release build fails
5. launches the executable if build succeeds

### Manual build
1. Open command prompt in project root
2. Create build folder:
```bat
mkdir build
cd build
cmake .. -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded
cmake --build . --config Release
```
3. Run:
```bat
build\Release\ExplorationHorizon.exe
```

## Controls

- `W/A/S/D` — move
- `SPACE` — jump
- `LEFT CTRL` — sprint / fast fly
- `F` — toggle fly / gravity mode
- `ALT` — release cursor to resize window
- Mouse movement — look around
- Left click — break block
- Right click — place block
- Scroll wheel — change hotbar slot
- `1`–`9` — select hotbar slot

## Notes

- The project uses a textured block atlas at blocks.png
- Shaders are stored in shaders
- The world is generated in the background to avoid freezing the render thread
- Assets are copied automatically into the build output after compilation

If you want, I can also help you create a polished README.md for the repository.
