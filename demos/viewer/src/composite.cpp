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

#include "composite.h"

#include <cstdio>

#include "shaders/composite.vert.h"
#include "shaders/composite.frag.h"

// <vec2 pos, vec2 tex>
static const Vector4 kQuadVertices[] = {
    Vector4(-1.0f, -1.0f, 0.0f, 0.0f),
    Vector4(1.0f, -1.0f, 1.0f, 0.0f),
    Vector4(1.0f, 1.0f, 1.0f, 1.0f),
    Vector4(-1.0f, 1.0f, 0.0f, 1.0f)};

static const GLuint kQuadIndices[] = {
    0, 2, 1,
    0, 3, 2};

void Compositor::render() const
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);

    glBindVertexArray(_vao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}

void Compositor::load()
{
    glGenVertexArrays(1, &_vao);

    glGenBuffers(1, &_vbo);
    glGenBuffers(1, &_ebo);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);

    glBindVertexArray(_vao);

    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kQuadIndices), kQuadIndices, GL_STATIC_DRAW);

    /* Position and Texture Coordinates */
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void Compositor::resize(int width, int height)
{
    /* Unload old objects */
    if (_fbo)
        glDeleteFramebuffers(1, &_fbo);

    if (_texture)
        glDeleteTextures(1, &_texture);

    if (_rbo)
        glDeleteRenderbuffers(1, &_rbo);

    if (_msFbo)
        glDeleteFramebuffers(1, &_msFbo);
    if (_msColorRbo)
        glDeleteRenderbuffers(1, &_msColorRbo);
    if (_msDepthRbo)
        glDeleteRenderbuffers(1, &_msDepthRbo);
    _msFbo = _msColorRbo = _msDepthRbo = 0;

    glGenFramebuffers(1, &_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, _fbo);

    glGenTextures(1, &_texture);
    glBindTexture(GL_TEXTURE_2D, _texture);
    _colorFormat = GL_R11F_G11F_B10F;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R11F_G11F_B10F, width, height, 0, GL_RGB, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _texture, 0);

    {
        const GLenum attachments[1] = {GL_COLOR_ATTACHMENT0};
        glDrawBuffers(1, attachments);
    }

    glGenRenderbuffers(1, &_rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, _rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, _rbo);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        /* Fallback for stricter drivers (notably some macOS stacks). */
        glBindTexture(GL_TEXTURE_2D, _texture);
        _colorFormat = GL_RGBA16F;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _texture, 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            printf("Compositor::resize: Framebuffer is not complete\n");
            exit(1);
        }
    }

    /* The multisampled target the scene is drawn into: 4 samples, or fewer where the driver gives fewer. If
       it cannot be made the scene is drawn straight into the texture's own framebuffer, without anti-aliasing. */
    {
        GLint maxSamples = 0;
        GLsizei samples;
        glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        samples = maxSamples < 4 ? (GLsizei)maxSamples : 4;

        if (samples > 1)
        {
            glGenFramebuffers(1, &_msFbo);
            glBindFramebuffer(GL_FRAMEBUFFER, _msFbo);

            glGenRenderbuffers(1, &_msColorRbo);
            glBindRenderbuffer(GL_RENDERBUFFER, _msColorRbo);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, _colorFormat, width, height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _msColorRbo);

            glGenRenderbuffers(1, &_msDepthRbo);
            glBindRenderbuffer(GL_RENDERBUFFER, _msDepthRbo);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, _msDepthRbo);

            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            {
                glDeleteFramebuffers(1, &_msFbo);
                glDeleteRenderbuffers(1, &_msColorRbo);
                glDeleteRenderbuffers(1, &_msDepthRbo);
                _msFbo = _msColorRbo = _msDepthRbo = 0;
            }
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    _width = width;
    _height = height;
}

void Compositor::resolve() const
{
    if (!_msFbo)
        return;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, _msFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _fbo);
    glBlitFramebuffer(0, 0, _width, _height, 0, 0, _width, _height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
}

Compositor::Compositor() : _fbo(0), _rbo(0), _msFbo(0), _msColorRbo(0), _msDepthRbo(0),
                           _colorFormat(GL_R11F_G11F_B10F), _texture(0),
                           _vao(0), _vbo(0), _ebo(0),
                           _width(0), _height(0),
                           _gamma(2.2f), _exposure(1.0f)
{
}

Compositor::~Compositor()
{
    if (_msFbo)
        glDeleteFramebuffers(1, &_msFbo);

    if (_msColorRbo)
        glDeleteRenderbuffers(1, &_msColorRbo);

    if (_msDepthRbo)
        glDeleteRenderbuffers(1, &_msDepthRbo);

    if (_rbo)
        glDeleteRenderbuffers(1, &_rbo);

    if (_texture)
        glDeleteTextures(1, &_texture);

    if (_fbo)
        glDeleteFramebuffers(1, &_fbo);

    if (_ebo)
        glDeleteBuffers(1, &_ebo);

    if (_vbo)
        glDeleteBuffers(1, &_vbo);

    if (_vao)
        glDeleteVertexArrays(1, &_vao);
}

void CompositeShader::load()
{
    _shader.load(composite_vert_source, composite_frag_source);

    _shader.use();

    uTexture = _shader.getUniformLocation("uTexture");

    uEnableBloom = _shader.getUniformLocation("uEnableBloom");
    uBloom = _shader.getUniformLocation("uBloom");
    uBloomStrength = _shader.getUniformLocation("uBloomStrength");

    uEnableToneMapping = _shader.getUniformLocation("uEnableToneMapping");
    uExposure = _shader.getUniformLocation("uExposure");

    uGamma = _shader.getUniformLocation("uGamma");

    uWireframe = _shader.getUniformLocation("uWireframe");
    uFullbright = _shader.getUniformLocation("uFullbright");

}

CompositeShader::CompositeShader() : uTexture(-1), uBloom(-1), uExposure(-1), uGamma(-1) {}
