#ifndef CLOUD_RENDERER_HPP
#define CLOUD_RENDERER_HPP

#include <glad/glad.h>
#include <glm/glm.hpp>

class Shader;
class Texture;

// ==========================================================================
// CloudRenderer  —  Isolated sky cloud layer.
// Renders a single large horizontal quad textured with clouds.png noise.
// The quad is centered on the player (follows camera XZ) at a fixed altitude
// so clouds always cover the sky within the render distance.
// Integration: construct once after the GL context exists, then call render()
// each frame between the sky clear and world.render().
// ==========================================================================
class CloudRenderer {
public:
    CloudRenderer();
    ~CloudRenderer();

    // Call once per frame. Pass the camera position, view/projection matrices
    // (already computed in the main loop) and the elapsed time in seconds.
    void render(const glm::vec3& cameraPos,
                const glm::mat4& view,
                const glm::mat4& projection,
                float             time) const;

private:
    GLuint   m_VAO     = 0;
    GLuint   m_VBO     = 0;
    Shader*  m_shader  = nullptr;
    Texture* m_texture = nullptr;

    // Tuning constants
    static constexpr float CLOUD_Y      = 128.0f;   // altitude above sea level
    static constexpr float CLOUD_EXTENT = 256.0f;   // half-size = 2 * RD * CHUNK_SIZE (width/depth is 512.0f)
    static constexpr float CLOUD_SPEED  = 0.008f;   // UV scroll speed (units/sec)
};

#endif // CLOUD_RENDERER_HPP
