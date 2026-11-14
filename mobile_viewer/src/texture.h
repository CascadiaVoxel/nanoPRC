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

#ifndef _TEXTURE_H_
#define _TEXTURE_H_

#include <prc_api.h>
#include "gles3_loader.h"

class Texture
{
public:
    constexpr GLuint id() const { return _id; }

    void retain();
    void release();

    void load(const prc_api_texture &texture, bool isFontAtlas = false);

    constexpr Texture() : _id(0), _refs(0) {}
    ~Texture();

private:
    GLuint _id;
    size_t _refs;
};

#endif // _TEXTURE_H_
