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

#ifndef _SHADER_H_
#define _SHADER_H_

#include <mutil/mutil.h>
#include "texture.h"

using namespace mutil;

struct DirLight
{
    Vector3 direction;
    Vector3 color;
    float intensity;
};

struct PointLight
{
    Vector3 position;
    Vector3 color;
    float intensity;
};

struct Material
{
    Vector3 diffuse;
    Vector3 specular;
    Vector3 emissive;
    float shininess = 0.2f;
    Vector3 tint;
    float alpha = 1.0f;

    Texture *diffuseTexture = nullptr;
    Texture *specularTexture = nullptr;

    inline void setDiffuseTexture(Texture *texture)
    {
        if (texture == diffuseTexture)
            return;

        if (diffuseTexture)
            diffuseTexture->release();

        diffuseTexture = texture;

        if (diffuseTexture)
            diffuseTexture->retain();
    }

    inline void setSpecularTexture(Texture *texture)
    {
        if (texture == specularTexture)
            return;

        if (specularTexture)
            specularTexture->release();

        specularTexture = texture;

        if (specularTexture)
            specularTexture->retain();
    }

    inline Material &operator=(const Material &material)
    {
        if (&material == this)
            return *this;

        emissive = material.emissive;
        diffuse = material.diffuse;
        specular = material.specular;
        shininess = material.shininess;
        tint = material.tint;

        setDiffuseTexture(material.diffuseTexture);
        setSpecularTexture(material.specularTexture);

        return *this;
    }

    inline Material &operator=(Material &&material) noexcept
    {
        if (&material == this)
            return *this;

        setDiffuseTexture(nullptr);
        setSpecularTexture(nullptr);

        emissive = material.emissive;
        diffuse = material.diffuse;
        specular = material.specular;
        shininess = material.shininess;
        tint = material.tint;

        diffuseTexture = material.diffuseTexture;
        material.diffuseTexture = nullptr;

        specularTexture = material.specularTexture;
        material.specularTexture = nullptr;

        return *this;
    }

    constexpr Material() :
        emissive(0.0f), diffuse(1.0f), specular(1.0f),
        shininess(16.0f), alpha(1.0f), tint(1.0f),
        diffuseTexture(nullptr), specularTexture(nullptr) {}

    constexpr Material(
        const Vector3 &emissive,
        const Vector3 &diffuse,
        const Vector3 &specular,
        float shininess,
        float alpha,
        const Vector3 &tint
    ) : diffuse(diffuse), specular(specular),
        shininess(shininess), tint(tint) {}

    inline Material(const Material &material) :
        emissive(material.emissive), diffuse(material.diffuse),
        specular(material.specular),
        shininess(material.shininess), tint(material.tint),
        diffuseTexture(nullptr), specularTexture(nullptr)
    {
        setDiffuseTexture(material.diffuseTexture);
        setSpecularTexture(material.specularTexture);
    }

    inline Material(Material &&material) noexcept :
        emissive(material.emissive), diffuse(material.diffuse), specular(material.specular),
        shininess(material.shininess), tint(material.tint),
        diffuseTexture(material.diffuseTexture), specularTexture(material.specularTexture)
    {
        material.diffuseTexture = nullptr;
        material.specularTexture = nullptr;
    }

    inline ~Material()
    {
        if (diffuseTexture)
            diffuseTexture->release();

        if (specularTexture)
            specularTexture->release();
    }
};

class Shader final
{
public:
    bool load(const char *vertSource, const char *fragSource);

    void use() const;

    int getUniformLocation(const char *name) const;

    void setBool(int location, bool value);
    void setInt(int location, int value);
    void setFloat(int location, float value);
    void setVector2(int location, const Vector2 &value);
    void setVector3(int location, const Vector3 &value);
    void setMatrix4(int location, const Matrix4 &value);
    void setTexture2d(int location, const Texture &texture, int unit);

    Shader();
    ~Shader();

private:
    GLuint _prog;
};

#endif // _SHADER_H_
