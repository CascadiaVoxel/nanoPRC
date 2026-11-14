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

#include "shader.h"
#include "texture.h"
#include <stdlib.h>
#include <stdio.h>

bool Shader::load(const char *vertSource, const char *fragSource)
{
    GLuint vert, frag;
    char log[512];
    int rc;

    vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 1, &vertSource, NULL);
    glCompileShader(vert);

    glGetShaderiv(vert, GL_COMPILE_STATUS, &rc);
    if (!rc)
    {
        glGetShaderInfoLog(vert, sizeof(log), NULL, log);
        printf("glCompileShader failed (vertex): %s\n", log);
        glDeleteShader(vert);
        return false;
    }

    frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag, 1, &fragSource, NULL);
    glCompileShader(frag);

    glGetShaderiv(frag, GL_COMPILE_STATUS, &rc);
    if (!rc)
    {
        glGetShaderInfoLog(frag, sizeof(log), NULL, log);
        printf("glCompileShader failed (fragment): %s\n", log);
        glDeleteShader(vert);
        glDeleteShader(frag);
        return false;
    }

    _prog = glCreateProgram();
    glAttachShader(_prog, vert);
    glAttachShader(_prog, frag);
    glLinkProgram(_prog);

    glGetProgramiv(_prog, GL_LINK_STATUS, &rc);
    if (!rc)
    {
        glGetProgramInfoLog(_prog, sizeof(log), NULL, log);
        printf("glLinkProgram failed: %s\n", log);
        glDeleteShader(vert);
        glDeleteShader(frag);
        glDeleteProgram(_prog);
        _prog = 0;
        return false;
    }

    glDeleteShader(vert);
    glDeleteShader(frag);
    return true;
}


void Shader::use() const
{
    glUseProgram(_prog);
}

int Shader::getUniformLocation(const char *name) const
{
    return glGetUniformLocation(_prog, name);
}

void Shader::setBool(int location, bool value)
{
    glUniform1i(location, value);
}

void Shader::setInt(int location, int value)
{
    glUniform1i(location, value);
}

void Shader::setFloat(int location, float value)
{
    glUniform1f(location, value);
}

void Shader::setVector2(int location, const Vector2 &value)
{
    glUniform2fv(location, 1, (float *)&value);
}

void Shader::setVector3(int location, const Vector3 &value)
{
    glUniform3fv(location, 1, (float *)&value);
}

void Shader::setMatrix4(int location, const Matrix4 &value)
{
    glUniformMatrix4fv(location, 1, GL_FALSE, (float *)&value);
}

void Shader::setTexture2d(int location, const Texture &texture, int unit)
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, texture.id());
    glUniform1i(location, unit);
}

Shader::Shader() : _prog(0)
{
}

Shader::~Shader()
{
    if (_prog)
        glDeleteProgram(_prog);
}
