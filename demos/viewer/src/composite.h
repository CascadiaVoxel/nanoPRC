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

#ifndef _COMPOSITE_H_
#define _COMPOSITE_H_

#include "shader.h"

class Compositor
{
public:
    void render() const;

    void load();

    void resize(int width, int height);

    /* The scene is drawn into a multisampled target (anti-aliased edges), when the driver has one;
       resolve() then writes it to the texture, which is what bloom and the composite read. */
    inline void use() const
    {
        glBindFramebuffer(GL_FRAMEBUFFER, _msFbo ? _msFbo : _fbo);
    }

    void resolve() const;

    constexpr GLuint texture() const { return _texture; }

    constexpr int width() const { return _width; }
    constexpr int height() const { return _height; }

    constexpr float &gamma() { return _gamma; }
    constexpr float &exposure() { return _exposure; }

    Compositor();
    ~Compositor();

private:
    GLuint _fbo, _rbo;
    GLuint _msFbo, _msColorRbo, _msDepthRbo; /* 0 where there is no multisampled target */
    GLenum _colorFormat;
    GLuint _texture;

    GLuint _vao, _vbo, _ebo;
    int _width, _height;

    float _gamma;
    float _exposure;
};

class CompositeShader final
{
public:
    constexpr Shader &getShader() { return _shader; }

    inline void setTexture(GLuint unit) { _shader.setInt(uTexture, unit); }

    inline void setEnableBloom(bool enable) { _shader.setBool(uEnableBloom, enable); }
    inline void setBloom(GLuint unit) { _shader.setInt(uBloom, unit); }
    inline void setBloomStrength(float strength) { _shader.setFloat(uBloomStrength, strength); }

    inline void setEnableToneMapping(bool enable) { _shader.setBool(uEnableToneMapping, enable); }
    inline void setExposure(float exposure) { _shader.setFloat(uExposure, exposure); }

    inline void setGamma(float gamma) { _shader.setFloat(uGamma, gamma); }

    inline void setWireframe(bool wireframe) { _shader.setBool(uWireframe, wireframe); }
    inline void setFullbright(bool fullbright) { _shader.setBool(uFullbright, fullbright); }

    void load();

    CompositeShader();

private:
    Shader _shader;

    int uTexture;

    int uEnableBloom;
    int uBloom;
    int uBloomStrength;

    int uEnableToneMapping;
    int uExposure;

    int uGamma;

    int uWireframe;
    int uFullbright;
    int uVertex_hasMaterial;
};

#endif // _COMPOSITE_H_
