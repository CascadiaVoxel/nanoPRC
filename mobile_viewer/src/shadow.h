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

#ifndef _SHADOW_H_
#define _SHADOW_H_

#include <mutil/mutil.h>
#include "shader.h"

using namespace mutil;

class ShadowMap final
{
public:
    bool load(int resolution);

    inline void bind() const
    {
        glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
    }

    constexpr GLuint depthMap() const { return _depthMap; }

    constexpr const Matrix4 &lightSpaceMatrix() const { return _lightSpaceMatrix; }

    constexpr int resolution() const { return _resolution; }

    void setLightDirection(const Vector3 &direction);

    ShadowMap();
    ~ShadowMap();

private:
    GLuint _fbo;
    GLuint _depthMap; // texture
    int _resolution;

    Matrix4 _lightSpaceMatrix;
    Matrix4 _lightProjection;
};

class ShadowShader final
{
public:
    constexpr Shader &getShader() { return _shader; }
    constexpr const Shader &getShader() const { return _shader; }

    inline void setModel(const Matrix4 &model) { _shader.setMatrix4(uModel, model); }
    inline void setLightSpaceMatrix(const Matrix4 &lightSpaceMatrix) { _shader.setMatrix4(uLightSpaceMatrix, lightSpaceMatrix); }

    void load();

    ShadowShader();
    ~ShadowShader();

private:
    Shader _shader;

    int uModel;
    int uLightSpaceMatrix;
};

#endif // _SHADOW_H_
