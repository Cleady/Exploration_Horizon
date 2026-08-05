#ifndef TEXTURE_HPP
#define TEXTURE_HPP

#include <glad/glad.h>
#include <string>

class Texture {
public:
    GLuint ID;
    int width;
    int height;
    int nrChannels;

    Texture(const char* imagePath);
    ~Texture();

    void bind(unsigned int slot = 0) const;
};

#endif // TEXTURE_HPP
