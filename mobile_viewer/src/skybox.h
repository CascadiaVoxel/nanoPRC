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

#ifndef _SKYBOX_H_
#define _SKYBOX_H_

#include "shader.h"

class SkyboxShader;

class Skybox final
{
public:
    void render() const;

    void load();

    constexpr const Vector3 &horizonColor() const { return _horizonColor; }
    constexpr const Vector3 &zenithColor() const { return _zenithColor; }
    constexpr float sunTightness() const { return _sunTightness; }

    constexpr Vector3 *getHorizonColor() { return &_horizonColor; }
    constexpr Vector3 *getZenithColor() { return &_zenithColor; }

    constexpr void setHorizonColor(const Vector3 &color) { _horizonColor = color; }
    constexpr void setZenithColor(const Vector3 &color) { _zenithColor = color; }
    constexpr void setSunTightness(float tightness) { _sunTightness = tightness; }

    Skybox();
    ~Skybox();

private:
    GLuint _vao, _vbo, _ebo;

    Vector3 _horizonColor, _zenithColor;
    float _sunTightness;
};

class SkyboxShader final
{
public:
    constexpr Shader &getShader() { return _shader; }

    inline void setSkyView(const Matrix4 &view) { _shader.setMatrix4(uSkyView, view); }
    inline void setProjection(const Matrix4 &projection) { _shader.setMatrix4(uProjection, projection); }

    inline void setSkyColorHorizon(const Vector3 &color) { _shader.setVector3(uSkyColorHorizon, color); }
    inline void setSkyColorZenith(const Vector3 &color) { _shader.setVector3(uSkyColorZenith, color); }

    inline void setSunDir(const Vector3 &dir) { _shader.setVector3(uSunDir, dir); }
    inline void setSunColor(const Vector3 &color) { _shader.setVector3(uSunColor, color); }
    inline void setSunIntensity(float intensity) { _shader.setFloat(uSunIntensity, intensity); }
    inline void setSunTightness(float tightness) { _shader.setFloat(uSunTightness, tightness); }

    void load();

    SkyboxShader();
    ~SkyboxShader();

private:
    Shader _shader;

    int uSkyView;
    int uProjection;

    int uSkyColorHorizon;
    int uSkyColorZenith;

    int uSunDir;
    int uSunColor;
    int uSunIntensity;
    int uSunTightness;
};

#endif // _SKYBOX_H_
