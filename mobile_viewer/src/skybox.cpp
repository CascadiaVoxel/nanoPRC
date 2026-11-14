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

#include "skybox.h"
#include "engine.h"

#include <cstddef>

#include "shaders/skybox.vert.h"
#include "shaders/skybox.frag.h"

#define SKYBOX_INDEX_COUNT 36

static const mutil::Vector3 kSkyboxVertices[] = {
    {-1.0f, -1.0f, -1.0f},
    {1.0f, -1.0f, -1.0f},
    {1.0f, 1.0f, -1.0f},
    {-1.0f, 1.0f, -1.0f},
    {-1.0f, -1.0f, 1.0f},
    {1.0f, -1.0f, 1.0f},
    {1.0f, 1.0f, 1.0f},
    {-1.0f, 1.0f, 1.0f}};

static const unsigned int kSkyboxIndices[] = {
    0, 1, 2, 2, 3, 0,
    1, 5, 6, 6, 2, 1,
    7, 6, 5, 5, 4, 7,
    4, 0, 3, 3, 7, 4,
    4, 5, 1, 1, 0, 4,
    3, 2, 6, 6, 7, 3};

void Skybox::render() const
{
    glBindVertexArray(_vao);
    glDrawElements(GL_TRIANGLES, SKYBOX_INDEX_COUNT, GL_UNSIGNED_INT, 0);
}

void Skybox::load()
{
    glGenVertexArrays(1, &_vao);
    glGenBuffers(1, &_vbo);
    glGenBuffers(1, &_ebo);

    glBindVertexArray(_vao);

    /* vertex buffer */
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kSkyboxVertices), kSkyboxVertices, GL_STATIC_DRAW);

    /* element buffer */
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kSkyboxIndices), kSkyboxIndices, GL_STATIC_DRAW);

    /* vertex attributes */

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vector3), (void *)offsetof(Vector3, x));
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

Skybox::Skybox() : _vao(0), _vbo(0), _ebo(0),
                   _sunTightness(500.0f)
{
}

Skybox::~Skybox()
{
    if (_ebo)
        glDeleteBuffers(1, &_ebo);

    if (_vbo)
        glDeleteBuffers(1, &_vbo);

    if (_vao)
        glDeleteVertexArrays(1, &_vao);
}

void SkyboxShader::load()
{
    _shader.load(skybox_vert_source, skybox_frag_source);

    uSkyView = _shader.getUniformLocation("uSkyView");
    uProjection = _shader.getUniformLocation("uProjection");

    uSkyColorHorizon = _shader.getUniformLocation("uSkyColorHorizon");
    uSkyColorZenith = _shader.getUniformLocation("uSkyColorZenith");

    uSunDir = _shader.getUniformLocation("uSunDir");
    uSunColor = _shader.getUniformLocation("uSunColor");
    uSunIntensity = _shader.getUniformLocation("uSunIntensity");
    uSunTightness = _shader.getUniformLocation("uSunTightness");
}

SkyboxShader::SkyboxShader() : uSkyView(-1), uProjection(-1),
                               uSkyColorHorizon(-1), uSkyColorZenith(-1),
                               uSunDir(-1), uSunColor(-1), uSunIntensity(-1)
{
}

SkyboxShader::~SkyboxShader()
{
}
