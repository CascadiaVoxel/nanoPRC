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

#include "shadow.h"
#include "engine.h"
#include <cstdio>

#include "shaders/shadow.vert.h"
#include "shaders/shadow.frag.h"

#include "camera.h"

#define SHADOW_MAP_EXTENT 20.0f
#define SHADOW_MAP_PLANE_NEAR 1.0f
#define SHADOW_MAP_PLANE_FAR 100.0f

bool ShadowMap::load(int resolution)
{
    // ... existing FBO and GL_DEPTH_COMPONENT24 texture setup ...

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        printf("ShadowMap::load: Framebuffer is not complete! Status: 0x%x\n", status);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    _lightProjection = mutil::ortho(
        -SHADOW_MAP_EXTENT, SHADOW_MAP_EXTENT,
        -SHADOW_MAP_EXTENT, SHADOW_MAP_EXTENT,
        SHADOW_MAP_PLANE_NEAR, SHADOW_MAP_PLANE_FAR);

    _resolution = resolution;
    return true;
}


void ShadowMap::setLightDirection(const Vector3 &direction)
{
    _lightSpaceMatrix = _lightProjection * mutil::lookAt(-direction * SHADOW_MAP_EXTENT, Vector3(0.0f), kWorldUp);
}

ShadowMap::ShadowMap() : _fbo(0), _depthMap(0)
{
}

ShadowMap::~ShadowMap()
{
}

void ShadowShader::load()
{
    _shader.load(shadow_vert_source, shadow_frag_source);

    _shader.use();

    uModel = _shader.getUniformLocation("uModel");
    uLightSpaceMatrix = _shader.getUniformLocation("uLightSpaceMatrix");
}

ShadowShader::ShadowShader() : uModel(-1), uLightSpaceMatrix(-1)
{
}

ShadowShader::~ShadowShader()
{
}
