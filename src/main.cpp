#include "Window.hpp"
#include "Shader.hpp"
#include "Camera.hpp"
#include "Texture.hpp"
#include "World.hpp"
#include "Hotbar.hpp"
#include "CloudRenderer.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <algorithm>
#include <cmath>

// Camera & Input state - Spawn above the center of the 512x512 procedural world,
// high enough to clear the forest canopy (hilltops top out around y=90),
// looking down over rivers, lakes and rolling terrain.
Camera camera(glm::vec3(256.0f, 150.0f, 380.0f), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, -30.0f);
float lastX = 1280.0f / 2.0f;
float lastY = 720.0f / 2.0f;
bool firstMouse = true;
bool freeCursor = false;   // true while LEFT/RIGHT ALT releases the cursor (window resize)

// Timing
float deltaTime = 0.0f;
float lastFrame = 0.0f;

const float REACH_DISTANCE = 5.0f; // block interaction range

// ==========================================================
// Player AABB Collision
// Eye-level camera: Position = feet + (0, EYE_Y, 0).
// The AABB extends half-width in X/Z around the eye position
// and spans from feet to feet + HEIGHT.
// ==========================================================
static const float PLAYER_W   = 0.6f;   // total width / depth
static const float PLAYER_H   = 1.8f;   // total height
static const float PLAYER_EYE = 1.62f;  // eye offset above feet

// Returns true if the player AABB centred at `eyePos` overlaps any solid block.
static bool playerCollides(const glm::vec3& eyePos, const World& world) {
    float hw   = PLAYER_W * 0.5f;
    float feet = eyePos.y - PLAYER_EYE;

    int x0 = (int)std::floor(eyePos.x - hw);
    int x1 = (int)std::floor(eyePos.x + hw - 0.001f);
    int y0 = (int)std::floor(feet);
    int y1 = (int)std::floor(feet + PLAYER_H - 0.001f);
    int z0 = (int)std::floor(eyePos.z - hw);
    int z1 = (int)std::floor(eyePos.z + hw - 0.001f);

    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z)
                if (world.isBlockSolidAt(x, y, z)) return true;
    return false;
}

// Applies `delta` to `pos` one axis at a time, rejecting any axis component
// that would cause a solid-block overlap. This gives smooth wall-sliding.
static glm::vec3 resolveMovement(const glm::vec3& pos, const glm::vec3& delta, const World& world) {
    glm::vec3 result = pos;

    // X axis
    glm::vec3 tryX = result + glm::vec3(delta.x, 0.0f, 0.0f);
    if (!playerCollides(tryX, world)) result.x = tryX.x;

    // Z axis
    glm::vec3 tryZ = result + glm::vec3(0.0f, 0.0f, delta.z);
    if (!playerCollides(tryZ, world)) result.z = tryZ.z;

    // Y axis
    glm::vec3 tryY = result + glm::vec3(0.0f, delta.y, 0.0f);
    if (!playerCollides(tryY, world)) result.y = tryY.y;

    return result;
}

void processInput(Window& window, Camera& cam, World& world, Hotbar& bar, float dt) {
    GLFWwindow* win = window.getGLFWwindow();
    if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(win, true);
    }

    // Fast-fly / sprint while holding LEFT CTRL (exactly 2x base speed)
    cam.setSprint(glfwGetKey(win, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS);

    // ALT releases the cursor so the OS window can be resized / dragged; the
    // FPS camera is frozen while free. Releasing ALT re-captures the cursor and
    // re-centres the view so it doesn't jump.
    bool altHeld = glfwGetKey(win, GLFW_KEY_LEFT_ALT) == GLFW_PRESS
                || glfwGetKey(win, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
    if (altHeld && !freeCursor) {
        freeCursor = true;
        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        std::cout << "[INPUT] Cursor released (ALT held) - window can be resized." << std::endl;
    } else if (!altHeld && freeCursor) {
        freeCursor = false;
        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        firstMouse = true;   // re-centre so the view doesn't jump
        std::cout << "[INPUT] Cursor captured again (ALT released)." << std::endl;
    }

    // Toggle Fly / Gravity Mode ('F' key debounced)
    static bool fKeyWasPressed = false;
    bool fKeyPressed = (glfwGetKey(win, GLFW_KEY_F) == GLFW_PRESS);
    if (fKeyPressed && !fKeyWasPressed) {
        cam.isFlying = !cam.isFlying;
        cam.verticalVelocity = 0.0f;
        std::cout << "[PHYSICS] Mode: " << (cam.isFlying ? "FLY (Creative)" : "GRAVITY (Walking)") << std::endl;
    }
    fKeyWasPressed = fKeyPressed;

    if (cam.isFlying) {
        // --- Flying Mode (Creative) ---
        glm::vec3 moveDelta(0.0f);
        if (glfwGetKey(win, GLFW_KEY_W)          == GLFW_PRESS) moveDelta += cam.getMoveDelta(FORWARD,  dt);
        if (glfwGetKey(win, GLFW_KEY_S)          == GLFW_PRESS) moveDelta += cam.getMoveDelta(BACKWARD, dt);
        if (glfwGetKey(win, GLFW_KEY_A)          == GLFW_PRESS) moveDelta += cam.getMoveDelta(LEFT,     dt);
        if (glfwGetKey(win, GLFW_KEY_D)          == GLFW_PRESS) moveDelta += cam.getMoveDelta(RIGHT,    dt);
        if (glfwGetKey(win, GLFW_KEY_SPACE)      == GLFW_PRESS) moveDelta += cam.getMoveDelta(UP,       dt);
        if (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) moveDelta += cam.getMoveDelta(DOWN,     dt);
        if (moveDelta != glm::vec3(0.0f))
            cam.Position = resolveMovement(cam.Position, moveDelta, world);
    } else {
        // --- Walking Mode (Gravity Enabled) ---
        // 1. Horizontal movement on XZ plane
        glm::vec3 walkDelta(0.0f);
        if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) walkDelta += cam.getWalkMoveDelta(FORWARD,  dt);
        if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) walkDelta += cam.getWalkMoveDelta(BACKWARD, dt);
        if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) walkDelta += cam.getWalkMoveDelta(LEFT,     dt);
        if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) walkDelta += cam.getWalkMoveDelta(RIGHT,    dt);

        if (walkDelta.x != 0.0f || walkDelta.z != 0.0f) {
            glm::vec3 tryX = cam.Position + glm::vec3(walkDelta.x, 0.0f, 0.0f);
            if (!playerCollides(tryX, world)) cam.Position.x = tryX.x;

            glm::vec3 tryZ = cam.Position + glm::vec3(0.0f, 0.0f, walkDelta.z);
            if (!playerCollides(tryZ, world)) cam.Position.z = tryZ.z;
        }

        // 2. Check if player is grounded (standing on solid ground)
        cam.isGrounded = playerCollides(cam.Position - glm::vec3(0.0f, 0.05f, 0.0f), world);

        // 3. Jump Handling (Space key)
        if (cam.isGrounded && glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS) {
            cam.verticalVelocity = Camera::JUMP_FORCE;
            cam.isGrounded = false;
        }

        // 4. Apply Gravity
        if (!cam.isGrounded) {
            cam.verticalVelocity += Camera::GRAVITY * dt;
        } else if (cam.verticalVelocity < 0.0f) {
            cam.verticalVelocity = 0.0f;
        }

        // 5. Vertical Movement & Floor/Ceiling Collision Resolution
        float vyDelta = cam.verticalVelocity * dt;
        if (vyDelta != 0.0f) {
            glm::vec3 tryY = cam.Position + glm::vec3(0.0f, vyDelta, 0.0f);
            if (!playerCollides(tryY, world)) {
                cam.Position.y = tryY.y;
            } else {
                if (cam.verticalVelocity < 0.0f) {
                    cam.isGrounded = true;
                }
                cam.verticalVelocity = 0.0f;
            }
        }
    }

    if (glfwGetKey(win, GLFW_KEY_1) == GLFW_PRESS) bar.selectSlot(0);
    if (glfwGetKey(win, GLFW_KEY_2) == GLFW_PRESS) bar.selectSlot(1);
    if (glfwGetKey(win, GLFW_KEY_3) == GLFW_PRESS) bar.selectSlot(2);
    if (glfwGetKey(win, GLFW_KEY_4) == GLFW_PRESS) bar.selectSlot(3);
    if (glfwGetKey(win, GLFW_KEY_5) == GLFW_PRESS) bar.selectSlot(4);
    if (glfwGetKey(win, GLFW_KEY_6) == GLFW_PRESS) bar.selectSlot(5);
    if (glfwGetKey(win, GLFW_KEY_7) == GLFW_PRESS) bar.selectSlot(6);
    if (glfwGetKey(win, GLFW_KEY_8) == GLFW_PRESS) bar.selectSlot(7);
    if (glfwGetKey(win, GLFW_KEY_9) == GLFW_PRESS) bar.selectSlot(8);

    // Edge-triggered mouse clicks (break/place once per click, not per frame)
    static bool lastLeft = false;
    static bool lastRight = false;
    bool left = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    bool right = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

    if (left && !lastLeft) {
        int hx, hy, hz, px, py, pz;
        if (world.raycast(cam.Position, cam.Front, REACH_DISTANCE, 0.05f, hx, hy, hz, px, py, pz)) {
            world.setBlock(hx, hy, hz, BlockType::AIR);
            std::cout << "[BREAK] block (" << hx << ", " << hy << ", " << hz << ")" << std::endl;
        }
    }

    if (right && !lastRight) {
        int hx, hy, hz, px, py, pz;
        if (world.raycast(cam.Position, cam.Front, REACH_DISTANCE, 0.05f, hx, hy, hz, px, py, pz)) {
            // Placement target must be inside the world, empty, and must not be
            // inside the player's own block. (getBlock() also returns AIR for
            // out-of-bounds, but the explicit check keeps the intent clear.)
            int camBX = (int)std::floor(cam.Position.x);
            int camBY = (int)std::floor(cam.Position.y);
            int camBZ = (int)std::floor(cam.Position.z);
            if (px >= 0 && px < World::WORLD_SIZE_X &&
                py >= 0 && py < World::WORLD_SIZE_Y &&
                pz >= 0 && pz < World::WORLD_SIZE_Z &&
                !(px == camBX && py == camBY && pz == camBZ) &&
                world.getBlock(px, py, pz) == BlockType::AIR) {
                world.setBlock(px, py, pz, bar.getSelectedBlockType());
                std::cout << "[PLACE] block (" << px << ", " << py << ", " << pz << ")" << std::endl;
            }
        }
    }

    lastLeft = left;
    lastRight = right;
}

int main() {
    Window window(1280, 720, "Exploration Horizon");

    if (!window.init()) {
        std::cerr << "Failed to initialize application window!" << std::endl;
        return -1;
    }

    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;

    // Hotbar (must be created AFTER the GL context exists - its constructor uploads GL buffers)
    Hotbar hotbar;
    CloudRenderer cloudRenderer;  // sky cloud layer (requires GL context)

    // Register Mouse Movement Callback
    window.setMouseCallback([](double xpos, double ypos) {
        float xposF = static_cast<float>(xpos);
        float yposF = static_cast<float>(ypos);

        // While ALT frees the cursor, mouse movement must NOT rotate the camera
        // (so the OS cursor can move freely over the window frame / resize grips).
        if (freeCursor) {
            lastX = xposF;
            lastY = yposF;
            return;
        }

        if (firstMouse) {
            lastX = xposF;
            lastY = yposF;
            firstMouse = false;
        }

        float xoffset = xposF - lastX;
        float yoffset = lastY - yposF;

        lastX = xposF;
        lastY = yposF;

        camera.processMouseMovement(xoffset, yoffset);
    });

    // Mouse scroll wheel cycles the hotbar selection:
    //   scroll UP   (yoffset > 0) -> move selection LEFT
    //   scroll DOWN (yoffset < 0) -> move selection RIGHT
    window.setScrollCallback([&hotbar](double xoffset, double yoffset) {
        if (yoffset > 0) {
            hotbar.selectSlot(hotbar.getSelectedSlot() - 1); // UP -> LEFT
            std::cout << "[HOTBAR] Selected slot " << (hotbar.getSelectedSlot() + 1) << std::endl;
        } else if (yoffset < 0) {
            hotbar.selectSlot(hotbar.getSelectedSlot() + 1); // DOWN -> RIGHT
            std::cout << "[HOTBAR] Selected slot " << (hotbar.getSelectedSlot() + 1) << std::endl;
        }
    });

    // Load Shader Programs (world + HUD + underwater tint)
    Shader shader("assets/shaders/base.vs", "assets/shaders/base.fs");
    Shader hudShader("assets/shaders/hud.vs", "assets/shaders/hud.fs");
    Shader tintShader("assets/shaders/tint.vs", "assets/shaders/tint.fs");

    // Load Texture Atlas (assets/textures/blocks.png)
    Texture textureAtlas("assets/textures/blocks.png");

    // Fullscreen overlay quad (clip-space) used for the underwater blue tint.
    GLfloat overlayVerts[] = {
        -1.0f, -1.0f,  1.0f, -1.0f,  1.0f, 1.0f,
        -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, 1.0f
    };
    GLuint overlayVAO = 0, overlayVBO = 0;
    glGenVertexArrays(1, &overlayVAO);
    glGenBuffers(1, &overlayVBO);
    glBindVertexArray(overlayVAO);
    glBindBuffer(GL_ARRAY_BUFFER, overlayVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(overlayVerts), overlayVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    // Crosshair texture + unit-quad VAO.
    // The quad covers [0,1]^2; at render time it is translated and scaled so
    // its CENTER lands exactly at (screenW/2, screenH/2).
    Texture crosshairTex("assets/textures/crosshair.png");
    //   pos.xy (location 0)   uv (location 1)
    float chQuad[] = {
        0.0f, 0.0f,  0.0f, 0.0f,
        1.0f, 0.0f,  1.0f, 0.0f,
        1.0f, 1.0f,  1.0f, 1.0f,
        0.0f, 0.0f,  0.0f, 0.0f,
        1.0f, 1.0f,  1.0f, 1.0f,
        0.0f, 1.0f,  0.0f, 1.0f,
    };
    GLuint chVAO = 0, chVBO = 0;
    glGenVertexArrays(1, &chVAO);
    glGenBuffers(1, &chVBO);
    glBindVertexArray(chVAO);
    glBindBuffer(GL_ARRAY_BUFFER, chVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(chQuad), chQuad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); // aPos
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1); // aUV
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);
    const float CROSSHAIR_SIZE = 24.0f; // pixels; adjust to taste

    // Sun — world-space horizontal quad, rendered in 3D sky space.
    // Vertex layout matches base.vs: (vec3 aPos, vec2 aTexCoord, float aAlpha).
    // The quad is a unit plane [-0.5, 0.5]^2 in model space; the model matrix
    // scales it to SUN_SIZE world units and translates it above the camera.
    Texture sunTex("assets/textures/sun.png");
    const float SUN_SIZE = 240.0f;
    //  x      y     z     u     v    alpha
    float sunVerts[] = {
        -0.5f, 0.0f, -0.5f,  0.0f, 0.0f,  1.0f,
         0.5f, 0.0f, -0.5f,  1.0f, 0.0f,  1.0f,
         0.5f, 0.0f,  0.5f,  1.0f, 1.0f,  1.0f,
        -0.5f, 0.0f, -0.5f,  0.0f, 0.0f,  1.0f,
         0.5f, 0.0f,  0.5f,  1.0f, 1.0f,  1.0f,
        -0.5f, 0.0f,  0.5f,  0.0f, 1.0f,  1.0f,
    };
    GLuint sunVAO = 0, sunVBO = 0;
    glGenVertexArrays(1, &sunVAO);
    glGenBuffers(1, &sunVBO);
    glBindVertexArray(sunVAO);
    glBindBuffer(GL_ARRAY_BUFFER, sunVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(sunVerts), sunVerts, GL_STATIC_DRAW);
    GLsizei sunStride = 6 * sizeof(float);
    glEnableVertexAttribArray(0); // aPos
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sunStride, (void*)0);
    glEnableVertexAttribArray(1); // aTexCoord
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sunStride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2); // aAlpha
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sunStride, (void*)(5 * sizeof(float)));
    glBindVertexArray(0);

    // Generate the 512x512x256 chunk-based procedural world (32x32 chunks,
    // 1024 total) on a background thread: rolling green plains, soft hills with
    // thin soil caps (stone peeking through on steep slopes), serpentine rivers
    // and occasional lakes with sandy shores, and dense trees on dry land.
    World world;   // starts terrain generation on a background worker thread

    // Loading phase: wait for the CPU terrain pass (rivers, lakes, features).
    // GPU meshing is streamed a few chunks per frame by world.update(), so the
    // 1024-chunk world never freezes the render thread.
    while (!world.isTerrainReady()) {
        glfwPollEvents();
        if (window.shouldClose()) break;
        glClearColor(0.53f, 0.81f, 0.92f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        window.update();
    }

    // Main Game Loop
    int lastChunkX = -999, lastChunkZ = -999;
    while (!window.shouldClose()) {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        processInput(window, camera, world, hotbar, deltaTime);

        // Sky Blue background (#87CEEB)
        glClearColor(0.53f, 0.81f, 0.92f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Activate Shader & Bind Texture Unit 0
        shader.use();
        // Reset colour tint to neutral at the start of each frame.
        // The water pass will override this to deep blue and reset it.
        shader.setVec4("uColorTint", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureAtlas.ID);
        glUniform1i(glGetUniformLocation(shader.ID, "ourTexture"), 0);

        // Dynamic View & Projection matrices from 3D Camera
        glm::mat4 model = glm::mat4(1.0f); // Static World Mesh
        glm::mat4 view = camera.getViewMatrix();
        float aspectRatio = (float)window.getWidth() / (float)window.getHeight();
        glm::mat4 projection = camera.getProjectionMatrix(aspectRatio);

        shader.setMat4("model", model);
        shader.setMat4("view", view);
        shader.setMat4("projection", projection);

        // Player's current chunk coordinate: floor(worldPos / CHUNK_SIZE),
        // clamped to the 16x16 chunk grid.
        int playerChunkX = static_cast<int>(std::floor(camera.Position.x / World::CHUNK_SIZE));
        int playerChunkZ = static_cast<int>(std::floor(camera.Position.z / World::CHUNK_SIZE));
        playerChunkX = std::max(0, std::min(World::CHUNK_COUNT_X - 1, playerChunkX));
        playerChunkZ = std::max(0, std::min(World::CHUNK_COUNT_Z - 1, playerChunkZ));

        // Log once whenever the player moves into a new chunk
        if (playerChunkX != lastChunkX || playerChunkZ != lastChunkZ) {
            std::cout << "[CHUNK] Player chunk: (" << playerChunkX << ", " << playerChunkZ << ")" << std::endl;
            lastChunkX = playerChunkX;
            lastChunkZ = playerChunkZ;
        }

        // ---- Sun (sky object, drawn before world geometry) ----
        // Rendered with depth writes OFF so the sun never occludes terrain:
        // sky-colored pixels behind terrain don't get a sun colour.
        // Alpha blending lets sun.png's transparent border fade smoothly.
        {
            glm::vec3 sunPos = camera.Position + glm::vec3(0.0f, 350.0f, 0.0f);
            glm::mat4 sunModel = glm::translate(glm::mat4(1.0f), sunPos)
                               * glm::scale(glm::mat4(1.0f), glm::vec3(SUN_SIZE, SUN_SIZE, SUN_SIZE));

            shader.use();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, sunTex.ID);
            glUniform1i(glGetUniformLocation(shader.ID, "ourTexture"), 0);
            shader.setMat4("model",      sunModel);
            shader.setMat4("view",       view);
            shader.setMat4("projection", projection);

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);       // don't let the sky quad write depth
            glDisable(GL_CULL_FACE);     // flat quad visible from below
            glDisable(GL_DEPTH_TEST);    // always behind terrain

            glBindVertexArray(sunVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);

            // Restore for world render
            glDepthMask(GL_TRUE);
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
            glDisable(GL_BLEND);

            // Re-bind the atlas texture for the world geometry passes
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, textureAtlas.ID);
            glUniform1i(glGetUniformLocation(shader.ID, "ourTexture"), 0);
            shader.setMat4("model", model);
        }

        // Three-pass world render (opaque+flora → water → leaves).
        // World::render() fully manages its own GL state and restores
        // a clean baseline (depth write ON, cull ON, blend OFF) on return.
        world.render(playerChunkX, playerChunkZ);

        // Underwater blue tint overlay: when the camera drops below the water
        // level, a full-screen translucent blue quad tints the whole view.
        if (camera.Position.y < World::WATER_LEVEL) {
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            tintShader.use();
            tintShader.setVec4("uTint", glm::vec4(0.0f, 0.3f, 0.7f, 0.4f));
            glBindVertexArray(overlayVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);
            // Restore for HUD
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
        }

        // ---- Clouds (rendered AFTER world so terrain depth is fully written) ----
        // glDepthMask(GL_FALSE) inside CloudRenderer means clouds read depth
        // (can't paint over solid blocks in front of them) but never overwrite it.
        cloudRenderer.render(camera.Position, view, projection,
                             static_cast<float>(glfwGetTime()));

        // 2D Hotbar HUD overlay — orthographic, no depth testing.
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDepthMask(GL_TRUE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        hotbar.render(hudShader, textureAtlas.ID, window.getWidth(), window.getHeight());

        // Crosshair — drawn centered on screen after the hotbar.
        // State is already correct (depth OFF, blend ON, ortho via hudShader).
        {
            float sw = (float)window.getWidth();
            float sh = (float)window.getHeight();
            float half = CROSSHAIR_SIZE * 0.5f;

            // Orthographic projection: (0,0) top-left, (sw, sh) bottom-right.
            glm::mat4 proj = glm::ortho(0.0f, sw, sh, 0.0f, -1.0f, 1.0f);
            // Translate so the quad's center sits at (sw/2, sh/2).
            glm::mat4 mdl = glm::translate(glm::mat4(1.0f),
                                           glm::vec3(sw * 0.5f - half, sh * 0.5f - half, 0.0f))
                          * glm::scale(glm::mat4(1.0f),
                                       glm::vec3(CROSSHAIR_SIZE, CROSSHAIR_SIZE, 1.0f));

            hudShader.use();
            hudShader.setMat4("uProjection", proj);
            hudShader.setMat4("uModel",      mdl);
            hudShader.setBool("uUseTexture", true);
            // uRectUV = full texture (0,0) -> (1,1)
            hudShader.setVec4("uRectUV", glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
            hudShader.setInt("uTexture", 1);  // texture unit 1

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, crosshairTex.ID);

            glBindVertexArray(chVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);

            glActiveTexture(GL_TEXTURE0); // restore default unit
        }

        // Restore full 3D state for next frame
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glDisable(GL_BLEND);

        // Stream queued chunk meshes onto the GPU (bounded per frame).
        world.update();

        window.update();
    }

    return 0;
}
