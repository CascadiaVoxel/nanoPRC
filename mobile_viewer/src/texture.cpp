#include "texture.h"
#include <cstdio>

void Texture::retain()
{
    _refs++;
}

void Texture::release()
{
    if (!--_refs)
        delete this;
}

void Texture::load(const prc_api_texture &texture, bool isFontAtlas)
{
    if (!texture.data)
    {
        printf("  ERROR: texture.data is NULL!\n");
        return;
    }

    if (!_id)
        glGenTextures(1, &_id);

    while (glGetError() != GL_NO_ERROR);

    GLenum format, internalformat;
    switch (texture.num_channels)
    {
    default:
        printf("  ERROR: Invalid num_channels=%u\n", texture.num_channels);
        return;
    case 4:
        format = GL_RGBA;
        internalformat = GL_RGBA8;
        break;
    case 3:
        format = GL_RGB;
        internalformat = GL_RGB8;
        break;
    case 2:
        format = GL_RG;
        internalformat = GL_RG8;
        break;
    case 1:
        format = GL_RED;
        internalformat = GL_R8;
        break;
    }

    glBindTexture(GL_TEXTURE_2D, _id);

    if (texture.num_channels == 1)
    {
        if (isFontAtlas)
        {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_ONE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_ONE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_ONE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_RED);
        }
        else
        {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);
        }
    }

    // Changed from glPixelStoref to glPixelStorei for OpenGL ES 3.0
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, internalformat, texture.width, texture.height, 0, format, GL_UNSIGNED_BYTE, texture.data);

    GLenum error = glGetError();
    if (error != GL_NO_ERROR)
    {
        printf("  ERROR after glTexImage2D: 0x%x\n", error);
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glGenerateMipmap(GL_TEXTURE_2D);

    error = glGetError();
    if (error != GL_NO_ERROR)
    {
        printf("  ERROR after glGenerateMipmap: 0x%x\n", error);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
}

Texture::~Texture()
{
    if (_id)
        glDeleteTextures(1, &_id);
}
