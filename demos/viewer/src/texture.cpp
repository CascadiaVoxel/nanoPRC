/* Copyright (C) 2023-2026 CascadiaVoxel LLC

    nanoPRC is free software: you can redistribute it and/or modify it under
    the terms of the GNU Affero General Public License as published by the
    Free Software Foundation, either version 3 of the License, or (at your
    option) any later version.

    nanoPRC is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public
    License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with nanoPRC. If not, see <https://www.gnu.org/licenses/>.
*/

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

    while (glGetError() != GL_NONE);

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

    // For single-channel textures, apply appropriate swizzle
    if (texture.num_channels == 1)
    {
        if (isFontAtlas)
        {
            // Font atlas: RED channel contains glyph coverage, map to ALPHA
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_ONE);   // R to 1 (white)
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_ONE);   // G to 1 (white)
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_ONE);   // B to 1 (white)
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_RED);   // A to R (glyph coverage)
            //printf("  Applied font atlas swizzle (RED to ALPHA)\n");
        }
        else
        {
            // Grayscale image: RED channel is luminance, map to RGB
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);   // R to R
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);   // G to R (grayscale)
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);   // B to R (grayscale)
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);   // A to 1 (opaque)
            //printf("  Applied grayscale swizzle (RED to RGB)\n");
        }
    }
    else if (texture.num_channels == 2)
    {
        // Grayscale with alpha: RED is luminance, GREEN is alpha
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_GREEN);
    }

    glPixelStoref(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, internalformat, texture.width, texture.height, 0, format, GL_UNSIGNED_BYTE, texture.data);

    GLenum error = glGetError();
    if (error != GL_NONE)
    {
        printf("  ERROR after glTexImage2D: 0x%x\n", error);
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glGenerateMipmap(GL_TEXTURE_2D);

    error = glGetError();
    if (error != GL_NONE)
    {
        printf("  ERROR after glGenerateMipmap: 0x%x\n", error);
    }

    glBindTexture(GL_TEXTURE_2D, 0);

   // printf("  Texture loaded successfully: ID=%u\n\n", _id);
}

Texture::~Texture()
{
    if (_id)
        glDeleteTextures(1, &_id);
}
