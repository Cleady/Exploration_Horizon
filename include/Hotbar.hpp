#ifndef HOTBAR_HPP
#define HOTBAR_HPP

#include "Block.hpp"
#include "Shader.hpp"
#include <glad/glad.h>

class Hotbar {
public:
    static const int SLOT_COUNT = 9;

    Hotbar();
    ~Hotbar();

    void selectSlot(int slot);        // 0..5, wraps around
    void cycleSlot(int delta);        // scroll wheel: positive = next slot
    int getSelectedSlot() const { return m_selected; }
    BlockType getSelectedBlockType() const { return m_slots[m_selected]; }

    // Draw the 3-slot hotbar at the bottom-center of the screen (orthographic overlay).
    void render(Shader& shader, GLuint atlasTexture, int screenW, int screenH) const;

private:
    int m_selected;
    BlockType m_slots[SLOT_COUNT];
    GLuint m_VAO, m_VBO;
};

#endif // HOTBAR_HPP
