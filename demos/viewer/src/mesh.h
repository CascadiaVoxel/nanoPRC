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

#ifndef _MESH_H_
#define _MESH_H_

#include "shader.h"

#include <prc_api.h>

// Same as prc_api_vertex
struct Vertex
{
    Vector3 position;
    Vector3 normal;
    Vector3 color;
    Vector2 texCoords;
};

class Mesh final
{
public:
    void render() const;

    void load(GLenum primitiveType, const Vertex *vertices, GLsizei nVertices,
              const GLuint *indices, GLsizei nIndices);

    constexpr Material *material() { return &_material; }
    constexpr const Material *material() const { return &_material; }

    Mesh();
    ~Mesh();

private:
    GLuint _vao, _vbo, _ebo;
    GLsizei _numIndices;
    GLenum _primitiveType;
    Material _material;
};

class MeshShader final
{
public:
    constexpr Shader &getShader() { return _shader; }
    constexpr const Shader &getShader() const { return _shader; }

    inline void setModel(const Matrix4 &model) { _shader.setMatrix4(uModel, model); }
    inline void setView(const Matrix4 &view) { _shader.setMatrix4(uView, view); }
    inline void setProjection(const Matrix4 &projection) { _shader.setMatrix4(uProjection, projection); }
    /* The pixel size of the viewport drawn to: markup placed in pixels needs it */
    inline void setViewport(float width, float height) { _shader.setVector2(uViewport, Vector2(width, height)); }

    inline void setViewPos(const Vector3 &viewPos)
    {
        _viewPos = viewPos;  // Cache it
        _shader.setVector3(uViewPos, viewPos);
    }
    constexpr Vector3 getViewPos() const { return _viewPos; }


    inline void setDirLight(const DirLight &light)
    {
        _shader.setVector3(uDirLight_direction, light.direction);
        _shader.setVector3(uDirLight_color, light.color);
        _shader.setFloat(uDirLight_intensity, light.intensity);
    }

    inline void setPointLight(const PointLight &light)
    {
        _shader.setVector3(uPointLight_position, light.position);
        _shader.setVector3(uPointLight_color, light.color);
        _shader.setFloat(uPointLight_intensity, light.intensity);
    }

    inline void setSkyColorHorizon(const Vector3 &skyColorHorizon) { _shader.setVector3(uSkyColorHorizon, skyColorHorizon); }
    inline void setSkyColorZenith(const Vector3 &skyColorZenith) { _shader.setVector3(uSkyColorZenith, skyColorZenith); }

    inline void setMaterial(const Material &material)
    {
        _shader.setVector3(uMaterial_diffuse, material.diffuse);
        _shader.setVector3(uMaterial_specular, material.specular);
        _shader.setFloat(uMaterial_shininess, material.shininess);
        _shader.setFloat(uMaterial_alpha, material.alpha);
        _shader.setVector3(uMaterial_tint, material.tint);

        _shader.setBool(uMaterial_hasDiffuseMap, material.diffuseTexture);
        if (material.diffuseTexture)
            _shader.setTexture2d(uMaterial_diffuseMap, *material.diffuseTexture, 0);

        _shader.setBool(uMaterial_hasSpecularMap, material.specularTexture);
        if (material.specularTexture)
            _shader.setTexture2d(uMaterial_specularMap, *material.specularTexture, 1);
    }

    inline void setIsLine(bool isLine)
    {
        _shader.setBool(uIsLine, isLine);
    }

    inline void setVertexHasMaterial(bool hasMaterial)
    {
        _shader.setBool(uVertexMaterial, hasMaterial);
    }

    inline void setLightSpaceMatrix(const Matrix4 &lightSpaceMatrix) { _shader.setMatrix4(uLightSpaceMatrix, lightSpaceMatrix); }
    inline void setShadowMap(GLuint shadowMap) { _shader.setInt(uShadowMap, shadowMap); }
    inline void setEnableShadows(bool enableShadows) { _shader.setBool(uEnableShadows, enableShadows); }

    inline void setWireframe(bool wireframe) { _shader.setBool(uWireframe, wireframe); }
    inline void setFullbright(bool fullbright) { _shader.setBool(uFullbright, fullbright); }
    inline void setAmbientWeight(float ambientWeight) { _shader.setFloat(uAmbientWeight, ambientWeight); }
    inline void setDiffuseWeight(float diffuseWeight) { _shader.setFloat(uDiffuseWeight, diffuseWeight); }
    inline void setDebugMode(int debugMode) { _shader.setInt(uDebugMode, debugMode); }

    void load();

    MeshShader();
    ~MeshShader();

private:
    Shader _shader;

    Vector3 _viewPos;

    int uModel;
    int uView;
    int uProjection;
    int uViewport;

    int uViewPos;

    int uDirLight_direction;
    int uDirLight_color;
    int uDirLight_intensity;
    int uPointLight_position;
    int uPointLight_color;
    int uPointLight_intensity;
    int uSkyColorHorizon;
    int uSkyColorZenith;

    int uMaterial_diffuse;
    int uMaterial_specular;
    int uMaterial_shininess;
    int uMaterial_alpha;
    int uMaterial_tint;
    int uMaterial_diffuseMap;
    int uMaterial_hasDiffuseMap;
    int uMaterial_specularMap;
    int uMaterial_hasSpecularMap;

    int uIsLine;
    int uVertexMaterial;

    int uLightSpaceMatrix;
    int uShadowMap;
    int uEnableShadows;

    int uWireframe;
    int uFullbright;
    int uAmbientWeight;
    int uDiffuseWeight;
    int uDebugMode;
};

#endif // _MESH_H_
