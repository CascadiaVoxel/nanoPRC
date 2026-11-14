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

#include "mesh.h"
#include "engine.h"
#include <cstdio>
#include <cstddef>

#include "shaders/generic.vert.h"
#include "shaders/generic.frag.h"

const char *vertexShaderSource = "#version 300 es\n"
"layout(location = 0) in vec3 aPos;\n"
"layout(location = 1) in vec3 aNormal;\n"
"layout(location = 2) in vec4 aColor;\n"
"uniform mat4 uMVP;\n" // Matrix uniform input
"out vec4 ourColor;\n"
"void main() {\n"
"    gl_Position = uMVP * vec4(aPos, 1.0);\n" // <-- Apply camera projection
"    ourColor = aColor;\n"
"}\n";

const char *fragmentShaderSource = "#version 300 es\n"
"precision mediump float;\n"
"in vec4 ourColor;\n"
"out vec4 fragColor;\n"
"void main() {\n"
"    fragColor = ourColor;\n"
"}\n";

GLuint CreateShaderProgram(const char *vsSource, const char *fsSource)
{
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vsSource, NULL);
    glCompileShader(vertexShader);

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fsSource, NULL);
    glCompileShader(fragmentShader);

    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return shaderProgram;
}

void Mesh::render() const
{
   // glDisable(GL_BLEND);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);

    // Use polygon offset to prevent Z-fighting
   // glEnable(GL_POLYGON_OFFSET_FILL);
   // glPolygonOffset(2.0f, 2.0f);

    glBindVertexArray(_vao);
    glDrawElements(_primitiveType, _numIndices, GL_UNSIGNED_INT, 0);
}

void Mesh::load(GLenum primitiveType, const Vertex *vertices, GLsizei nVertices,
                const GLuint *indices, GLsizei nIndices)
{
    glGenVertexArrays(1, &_vao);
    glGenBuffers(1, &_vbo);
    glGenBuffers(1, &_ebo);

    glBindVertexArray(_vao);

    /* vertex buffer */
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(GL_ARRAY_BUFFER, nVertices * sizeof(Vertex), vertices, GL_STATIC_DRAW);

    /* element buffer */
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, nIndices * sizeof(unsigned int), indices, GL_STATIC_DRAW);

    /* vertex attributes */

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, position));
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, normal));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, color));
    glEnableVertexAttribArray(2);

    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, texCoords));
    glEnableVertexAttribArray(3);

    glBindVertexArray(0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    _primitiveType = primitiveType;
    _numIndices = nIndices;
}

Mesh::Mesh() : _vao(0), _vbo(0), _ebo(0),
               _numIndices(0), _primitiveType(GL_NONE)
{
}

Mesh::~Mesh()
{
    if (_ebo)
        glDeleteBuffers(1, &_ebo);

    if (_vbo)
        glDeleteBuffers(1, &_vbo);

    if (_vao)
        glDeleteVertexArrays(1, &_vao);
}

void MeshShader::load()
{
    _shader.load(generic_vert_source, generic_frag_source);

    _shader.use();

    uModel = _shader.getUniformLocation("uModel");
    uView = _shader.getUniformLocation("uView");
    uProjection = _shader.getUniformLocation("uProjection");

    uViewPos = _shader.getUniformLocation("uViewPos");

    uDirLight_direction = _shader.getUniformLocation("uDirLight.direction");
    uDirLight_color = _shader.getUniformLocation("uDirLight.color");
    uDirLight_intensity = _shader.getUniformLocation("uDirLight.intensity");
    uPointLight_position = _shader.getUniformLocation("uPointLight.position");
    uPointLight_color = _shader.getUniformLocation("uPointLight.color");
    uPointLight_intensity = _shader.getUniformLocation("uPointLight.intensity");
    uSkyColorHorizon = _shader.getUniformLocation("uSkyColorHorizon");
    uSkyColorZenith = _shader.getUniformLocation("uSkyColorZenith");

    uMaterial_diffuse = _shader.getUniformLocation("uMaterial.diffuse");
    uMaterial_specular = _shader.getUniformLocation("uMaterial.specular");
    uMaterial_shininess = _shader.getUniformLocation("uMaterial.shininess");
    uMaterial_alpha = _shader.getUniformLocation("uMaterial.alpha");
    uMaterial_tint = _shader.getUniformLocation("uMaterial.tint");
    uMaterial_diffuseMap = _shader.getUniformLocation("uMaterial.diffuseMap");
    uMaterial_hasDiffuseMap = _shader.getUniformLocation("uMaterial.hasDiffuseMap");
    uMaterial_specularMap = _shader.getUniformLocation("uMaterial.specularMap");
    uMaterial_hasSpecularMap = _shader.getUniformLocation("uMaterial.hasSpecularMap");

    uIsLine = _shader.getUniformLocation("uIsLine");
    uVertexMaterial = _shader.getUniformLocation("uVertexMaterial");

    uLightSpaceMatrix = _shader.getUniformLocation("uLightSpaceMatrix");
    uShadowMap = _shader.getUniformLocation("uShadowMap");
    uEnableShadows = _shader.getUniformLocation("uEnableShadows");

    uWireframe = _shader.getUniformLocation("uWireframe");
    uFullbright = _shader.getUniformLocation("uFullbright");
    uAmbientWeight = _shader.getUniformLocation("uAmbientWeight");
    uDiffuseWeight = _shader.getUniformLocation("uDiffuseWeight");
    uDebugMode = _shader.getUniformLocation("uDebugMode");
}

MeshShader::MeshShader() : uModel(-1), uView(-1), uProjection(-1),
                           uViewPos(-1),
                           uDirLight_direction(-1), uDirLight_color(-1), uDirLight_intensity(-1),
                           uPointLight_position(-1), uPointLight_color(-1), uPointLight_intensity(-1),
                           uMaterial_diffuse(-1), uMaterial_specular(-1), uMaterial_shininess(-1), uMaterial_tint(-1),
                           uMaterial_diffuseMap(-1), uMaterial_hasDiffuseMap(-1), uIsLine(-1), uEnableShadows(-1),
                           uWireframe(-1), uFullbright(-1), uAmbientWeight(-1), uDiffuseWeight(-1),
                           uDebugMode(-1), uVertexMaterial(-1), uMaterial_alpha(-1)
{
}

MeshShader::~MeshShader()
{
}
