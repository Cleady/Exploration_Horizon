#include "Hotbar.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

Hotbar::Hotbar() : m_selected(0) {
    m_slots[0] = BlockType::GRASS;
    m_slots[1] = BlockType::DIRT;
    m_slots[2] = BlockType::STONE;
    m_slots[3] = BlockType::LOG;
    m_slots[4] = BlockType::LEAVES;
    m_slots[5] = BlockType::SAND;
    m_slots[6] = BlockType::PLANKS;
    m_slots[7] = BlockType::GLASS;
    m_slots[8] = BlockType::COBBLESTONE;

    // Unit quad [0,1]^2 with UVs (6 vertices, two triangles)
    float quad[] = {
        0.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 1.0f, 0.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f
    };

    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);

    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

    GLsizei stride = 4 * sizeof(float);
    glEnableVertexAttribArray(0); // aPos
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1); // aUV
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));

    glBindVertexArray(0);
}

Hotbar::~Hotbar() {
    glDeleteVertexArrays(1, &m_VAO);
    glDeleteBuffers(1, &m_VBO);
}

void Hotbar::selectSlot(int slot) {
    m_selected = ((slot % SLOT_COUNT) + SLOT_COUNT) % SLOT_COUNT;
}

void Hotbar::cycleSlot(int delta) {
    selectSlot(m_selected + (delta > 0 ? 1 : -1));
}

void Hotbar::render(Shader& shader, GLuint atlasTexture, int screenW, int screenH) const {
    shader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlasTexture);
    shader.setInt("uTexture", 0);

    // Orthographic projection in pixel space with (0,0) at the TOP-left corner.
    glm::mat4 projection = glm::ortho(0.0f, (float)screenW, (float)screenH, 0.0f, -1.0f, 1.0f);
    shader.setMat4("uProjection", projection);

    const float slotSize    = 56.0f;  // outer frame size
    const float iconSize    = 46.0f;  // block icon size inside the frame
    const float gap         = 8.0f;
    const float marginBottom = 18.0f;

    const float totalW = SLOT_COUNT * slotSize + (SLOT_COUNT - 1) * gap;
    const float startX = ((float)screenW - totalW) * 0.5f;
    const float slotY  = (float)screenH - slotSize - marginBottom;

    glBindVertexArray(m_VAO);

    for (int i = 0; i < SLOT_COUNT; ++i) {
        float x = startX + i * (slotSize + gap);
        bool selected = (i == m_selected);

        // 1) Slot frame: white border when selected, dim grey otherwise.
        glm::mat4 frameModel =
            glm::translate(glm::mat4(1.0f), glm::vec3(x, slotY, 0.0f)) *
            glm::scale(glm::mat4(1.0f), glm::vec3(slotSize, slotSize, 1.0f));
        shader.setMat4("uModel", frameModel);
        shader.setBool("uUseTexture", false);
        shader.setVec4("uColor", selected ? glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)
                                          : glm::vec4(0.30f, 0.30f, 0.30f, 0.85f));
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // 2) Block icon: the TOP-face texture of the slot's block type.
        float inset = (slotSize - iconSize) * 0.5f;
        glm::mat4 iconModel =
            glm::translate(glm::mat4(1.0f), glm::vec3(x + inset, slotY + inset, 0.0f)) *
            glm::scale(glm::mat4(1.0f), glm::vec3(iconSize, iconSize, 1.0f));
        UVRect uv = getBlockFaceUV(m_slots[i], BlockFace::TOP);
        shader.setMat4("uModel", iconModel);
        shader.setBool("uUseTexture", true);
        shader.setVec4("uRectUV", glm::vec4(uv.uMin, uv.vMin, uv.uMax, uv.vMax));
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    glBindVertexArray(0);
}
