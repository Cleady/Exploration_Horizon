#include <glad/glad.h>
#include "Texture.hpp"
#include "stb_image.h"
#include <iostream>

GLuint loadTexture(const char* path) {
    int width = 0, height = 0, nrChannels = 0;

    // Flip stays DISABLED on purpose: OpenGL's texture coordinate v = 0 is the
    // first row in memory, which then equals the TOP row of the image file.
    // The block meshes are authored for this convention (vMin = tile top row),
    // and the cross-sprite billboards sample their tiles accordingly in
    // addCrossSpriteVertices(). Enabling the flip here would mirror the whole
    // atlas and turn every grass/stone/side tile upside down instead.
    stbi_set_flip_vertically_on_load(false);

    unsigned char* data = stbi_load(path, &width, &height, &nrChannels, 4);
    if (!data) {
        std::string fallbackPath = "../" + std::string(path);
        data = stbi_load(fallbackPath.c_str(), &width, &height, &nrChannels, 4);
    }

    if (data) {
        std::cout << "Image Dimensions: " << width << "x" << height << ", Channels: " << nrChannels << std::endl;

        GLuint textureID;
        glGenTextures(1, &textureID);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureID);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

        // Texture atlas: sample level 0 with nearest-neighbor for crisp pixels.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        stbi_image_free(data);
        std::cout << "[TEXTURE SUCCESS] Loaded: " << path << " (ID: " << textureID << ")" << std::endl;
        return textureID;
    } else {
        std::cerr << "CRITICAL ERROR: Failed to load texture asset at " << path << "!" << std::endl;
        return 0;
    }
}

Texture::Texture(const char* imagePath) : width(0), height(0), nrChannels(0) {
    ID = loadTexture(imagePath);
}

Texture::~Texture() {
    glDeleteTextures(1, &ID);
}

void Texture::bind(unsigned int slot) const {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, ID);
}
