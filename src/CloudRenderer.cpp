#include "CloudRenderer.hpp"
#include "Shader.hpp"
#include "Texture.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glad/glad.h>

CloudRenderer::CloudRenderer() {
    // ---- Shaders ----
    m_shader = new Shader("assets/shaders/cloud.vs", "assets/shaders/cloud.fs");

    // ---- Texture Setup (GL_NEAREST & GL_REPEAT for crisp world-space grid) ----
    m_texture = new Texture("assets/textures/clouds.png");
    glBindTexture(GL_TEXTURE_2D, m_texture->ID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);

    // ---- Geometry (Flat horizontal Quad in X-Z plane, vec3 aPos at location 0) ----
    float verts[] = {
        -0.5f, 0.0f, -0.5f,
         0.5f, 0.0f, -0.5f,
         0.5f, 0.0f,  0.5f,
        -0.5f, 0.0f, -0.5f,
         0.5f, 0.0f,  0.5f,
        -0.5f, 0.0f,  0.5f,
    };
    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0); // location 0: aPos (vec3)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);
}

CloudRenderer::~CloudRenderer() {
    glDeleteVertexArrays(1, &m_VAO);
    glDeleteBuffers(1, &m_VBO);
    delete m_shader;
    delete m_texture;
}

void CloudRenderer::render(const glm::vec3& cameraPos,
                           const glm::mat4& view,
                           const glm::mat4& projection,
                           float             time) const {
    // Model matrix centered overhead on player XZ at altitude Y = 192.0f
    // Quad size expanded to 2048.0f x 2048.0f on XZ plane to cover the far horizon
    glm::mat4 model = glm::translate(glm::mat4(1.0f),
                                     glm::vec3(cameraPos.x, 192.0f, cameraPos.z))
                    * glm::scale(glm::mat4(1.0f),
                                 glm::vec3(2048.0f, 1.0f, 2048.0f));

    // Render Pass States
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE); // Read depth (terrain in front occludes cloud), DO NOT write depth
    glDisable(GL_CULL_FACE);

    m_shader->use();
    m_shader->setMat4 ("model",        model);
    m_shader->setMat4 ("view",         view);
    m_shader->setMat4 ("projection",   projection);
    m_shader->setFloat("uTime",        time);
    m_shader->setVec3 ("uCameraPos",   cameraPos);
    m_shader->setInt  ("cloudTexture", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texture->ID);

    glBindVertexArray(m_VAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glDepthMask(GL_TRUE); // Restore depth write
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
}
