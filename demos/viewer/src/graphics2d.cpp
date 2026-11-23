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

#include "graphics2d.h"
#include <string.h>
#include <stdio.h>
#include <algorithm>

#include "shaders/gui.frag.h"
#include "shaders/gui.vert.h"

void Graphics2D::Begin(int w, int h)
{
	_clip_rect[0] = 0;
	_clip_rect[1] = 0;
	_clip_rect[2] = w;
	_clip_rect[3] = h;

	for (size_t i = 0; i < _draw_list_count; ++i)
	{
		DrawList2D &dl = _draw_lists[i];
		dl.command_count = 0;
		dl.vertex_count = 0;
		dl.index_count = 0;
	}

	_draw_list_count = 0;

	_transform = Vector3(0.0f);
	_line_width = 1.0f;
	_color = Vector4(1.0f);
	_texture = 0;
	_font = nullptr;
}

void Graphics2D::SetClip(int x, int y, int w, int h)
{
	_clip_rect[0] = x;
	_clip_rect[1] = y;
	_clip_rect[2] = w;
	_clip_rect[3] = h;
}

IntVector4 Graphics2D::GetClip() const
{
	return IntVector4(_clip_rect[0], _clip_rect[1], _clip_rect[2], _clip_rect[3]);
}

void Graphics2D::SetTransform(const Vector3 &t)
{
	_transform = t;
}

void Graphics2D::SetLineWidth(float width)
{
	_line_width = width;
}

void Graphics2D::SetColor(const Vector4 &color)
{
	_color = color;
}

void Graphics2D::SetTexture(GLuint texture)
{
	if (_texture != texture)
		NewDrawList();
	_texture = texture;
}

void Graphics2D::FillRect(float x0, float y0, float x1, float y1)
{
	DrawList2D *dl;
    float z0, z1;

	if (!_draw_list_count)
		dl = NewDrawList();
	else
		dl = _draw_lists + _draw_list_count - 1;

	x0 += _transform.x;
	y0 += _transform.y;
	x1 += _transform.x;
	y1 += _transform.y;
	z0 = _transform.z;
    z1 = _transform.z;

	DrawCommand2D *cmd = dl->AllocCommand();
	memcpy(cmd->clip_rect, _clip_rect, sizeof(_clip_rect));
	cmd->texture_id = 0;
	cmd->idx_offset = dl->index_count;
	cmd->elem_count = 6;
	cmd->is_text = false;

	dl->EmitQuad(x0, y0, z0, x1, y1, z1, _color);
}

void Graphics2D::DrawRect(float x0, float y0, float x1, float y1)
{
	DrawList2D *dl;
	if (!_draw_list_count)
		dl = NewDrawList();
	else
		dl = _draw_lists + _draw_list_count - 1;

	x0 += _transform.x;
	y0 += _transform.y;
	x1 += _transform.x;
	y1 += _transform.y;

	DrawCommand2D *cmd = dl->AllocCommand();
	memcpy(cmd->clip_rect, _clip_rect, sizeof(_clip_rect));
	cmd->texture_id = 0;
	cmd->idx_offset = dl->index_count;
	cmd->elem_count = 24;
	cmd->is_text = false;

	uint32_t *indices = dl->AllocIndices(24);
	GLsizei base_vertex = dl->vertex_count;

	/* left */

	indices[0] = base_vertex + 0;
	indices[1] = base_vertex + 2;
	indices[2] = base_vertex + 4;

	indices[3] = base_vertex + 4;
	indices[4] = base_vertex + 2;
	indices[5] = base_vertex + 6;

	/* top */

	indices[6] = base_vertex + 0;
	indices[7] = base_vertex + 4;
	indices[8] = base_vertex + 1;

	indices[9] = base_vertex + 4;
	indices[10] = base_vertex + 5;
	indices[11] = base_vertex + 1;

	/* right */

	indices[12] = base_vertex + 1;
	indices[13] = base_vertex + 5;
	indices[14] = base_vertex + 7;

	indices[15] = base_vertex + 1;
	indices[16] = base_vertex + 7;
	indices[17] = base_vertex + 3;

	/* bottom */

	indices[18] = base_vertex + 2;
	indices[19] = base_vertex + 3;
	indices[20] = base_vertex + 6;

	indices[21] = base_vertex + 3;
	indices[22] = base_vertex + 7;
	indices[23] = base_vertex + 6;

	Vertex *vertices = dl->AllocVertices(8);

	/* outer border */

	vertices[0].position.x = x0;
	vertices[0].position.y = y0;
	vertices[0].tex_coords = Vector2(0.0f);
	vertices[0].color = _color;

	vertices[1].position.x = x1;
	vertices[1].position.y = y0;
	vertices[1].tex_coords = Vector2(0.0f);
	vertices[1].color = _color;

	vertices[2].position.x = x0;
	vertices[2].position.y = y1;
	vertices[2].tex_coords = Vector2(0.0f);
	vertices[2].color = _color;

	vertices[3].position.x = x1;
	vertices[3].position.y = y1;
	vertices[3].tex_coords = Vector2(0.0f);
	vertices[3].color = _color;

	/* inner border */

	vertices[4].position.x = x0 + _line_width;
	vertices[4].position.y = y0 + _line_width;
	vertices[4].tex_coords = Vector2(0.0f);
	vertices[4].color = _color;

	vertices[5].position.x = x1 - _line_width;
	vertices[5].position.y = y0 + _line_width;
	vertices[5].tex_coords = Vector2(0.0f);
	vertices[5].color = _color;

	vertices[6].position.x = x0 + _line_width;
	vertices[6].position.y = y1 - _line_width;
	vertices[6].tex_coords = Vector2(0.0f);
	vertices[6].color = _color;

	vertices[7].position.x = x1 - _line_width;
	vertices[7].position.y = y1 - _line_width;
	vertices[7].tex_coords = Vector2(0.0f);
	vertices[7].color = _color;
}

void Graphics2D::SetFont(Font *font)
{
	_font = font;
}

void Graphics2D::DrawText(const char *text, float x, float y, float z)
{
	if (!_font || !text || !text[0])
		return;

	x += _transform.x;
	y += _transform.y;
    z += _transform.z;

	GLuint texture = _font->GetTexture();

	DrawList2D *dl;
	if (!_draw_list_count || texture != _texture)
	{
		dl = NewDrawList();
		/* Remember it: without this every DrawText started a list of its own, and the text a markup took
		   from several text entities (the lines of a label) was cut to the first, as only list 0 is read. */
		_texture = texture;
	}
	else
		dl = _draw_lists + _draw_list_count - 1;

	DrawCommand2D *cmd = dl->AllocCommand();
	memcpy(cmd->clip_rect, _clip_rect, sizeof(_clip_rect));
	cmd->texture_id = texture;
	cmd->idx_offset = dl->index_count;
	cmd->elem_count = 0;
	cmd->is_text = true;

	float left = x;
	for (const char *ptr = text; *ptr; ++ptr)
	{
		char c = *ptr;

		const stbtt_packedchar *pc = _font->GetChar(c);
		if (!pc)
			continue;

		if (c == ' ')
		{
			left += pc->xadvance;
			continue;
		}

		dl->EmitQuad(
			left + pc->xoff, y + pc->yoff, z,
			left + pc->xoff2, y + pc->yoff2, z,
			_color);
		cmd->elem_count += 6;

		/* set texcoords */
		Vertex *vertices = dl->vertices + dl->vertex_count - 4;
		float inv_atlas_size = 1.0f / _font->GetAtlasSize();
		vertices[0].tex_coords = Vector2(pc->x0, pc->y0) * inv_atlas_size;
		vertices[1].tex_coords = Vector2(pc->x1, pc->y0) * inv_atlas_size;
		vertices[2].tex_coords = Vector2(pc->x0, pc->y1) * inv_atlas_size;
		vertices[3].tex_coords = Vector2(pc->x1, pc->y1) * inv_atlas_size;

		left += pc->xadvance;
	}
}

static void CheckGLErrors()
{
	GLenum error;
	while ((error = glGetError()))
	{
		printf("GL Error\n");
		exit(1);
	}
}

void Graphics2D::Render(const Vector2 &dim)
{
	if (_draw_list_count == 0)
		return;

	_shader.use();
	_shader.setInt(_u_texture, 0);

	Matrix4 view = mutil::ortho(
		0.0f, dim.x,
		dim.y, 0.0f
	);

	_shader.setMatrix4(_u_view, view);

	//glEnable(GL_SCISSOR_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBlendEquation(GL_FUNC_ADD);

	glActiveTexture(GL_TEXTURE0);

	glBindVertexArray(_vao);
	glBindBuffer(GL_ARRAY_BUFFER, _vbo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ebo);

	CheckGLErrors();

	for (size_t i = 0; i < _draw_list_count; ++i)
	{
		DrawList2D &draw_list = _draw_lists[i];

		if (_vbo_count < draw_list.vertex_count)
		{
			_vbo_count = draw_list.vertex_count;
			glBufferData(GL_ARRAY_BUFFER, _vbo_count * sizeof(Vertex), NULL, GL_STREAM_DRAW);
			CheckGLErrors();
		}

		if (_ebo_count < draw_list.index_count)
		{
			_ebo_count = draw_list.index_count;
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, _ebo_count * sizeof(uint16_t), NULL, GL_STREAM_DRAW);
			CheckGLErrors();
		}

		glBufferSubData(GL_ARRAY_BUFFER, 0, draw_list.vertex_count * sizeof(Vertex), draw_list.vertices);
		CheckGLErrors();

		glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, draw_list.index_count * sizeof(uint16_t), draw_list.indices);
		CheckGLErrors();

		for (size_t j = 0; j < draw_list.command_count; ++j)
		{
			DrawCommand2D &cmd = draw_list.cmd_list[j];

			glScissor(cmd.clip_rect[0], cmd.clip_rect[1], cmd.clip_rect[2], cmd.clip_rect[3]);

			if (cmd.texture_id)
			{
				_shader.setInt(_u_has_texture, true);
				glBindTexture(GL_TEXTURE_2D, cmd.texture_id);
			}
			else
				_shader.setInt(_u_has_texture, false);

			_shader.setInt(_u_is_text, cmd.is_text);

			CheckGLErrors();

			glDrawElements(GL_TRIANGLES, cmd.elem_count, GL_UNSIGNED_SHORT, (void *)(cmd.idx_offset * sizeof(uint16_t)));
			CheckGLErrors();
		}
	}

	glBindVertexArray(0);

	glDisable(GL_SCISSOR_TEST);
}

Graphics2D::Graphics2D() :
	_u_texture(-1), _u_view(-1), _u_has_texture(-1), _u_is_text(-1),
	_draw_lists(nullptr), _draw_list_count(0), _max_draw_lists(0),
	_line_width(1.0f), _texture(0), _clip_rect{ 0, 0, 0, 0 },
	_vao(0), _font(nullptr),
	_vbo(0), _vbo_count(0),
	_ebo(0), _ebo_count(0)
{
}

void Graphics2D::load()
{
	_shader.load(gui_vert_source, gui_frag_source);

	_u_texture = _shader.getUniformLocation("u_texture");
	_u_view = _shader.getUniformLocation("u_view");
	_u_has_texture = _shader.getUniformLocation("u_has_texture");
	_u_is_text = _shader.getUniformLocation("u_is_text");
}

#if 0
Graphics2D::Graphics2D() :
	_u_texture(-1), _u_view(-1), _u_has_texture(-1), _u_is_text(-1),
	_draw_lists(nullptr), _draw_list_count(0), _max_draw_lists(0),
    _line_width(1.0f), _texture(0), _clip_rect{ 0, 0, 0, 0 },
	_vao(0), _font(nullptr),
	_vbo(0), _vbo_count(0),
	_ebo(0), _ebo_count(0)
{
	glGenVertexArrays(1, &_vao);
	glGenBuffers(1, &_vbo);
	glGenBuffers(1, &_ebo);

	/* Setup vertex array */

	glBindVertexArray(_vao);

	glBindBuffer(GL_ARRAY_BUFFER, _vbo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ebo);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), 0);

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, color));

	glBindVertexArray(0);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	/* load shader */
	_prog->load(gui_vert_source, gui_frag_source);

	_u_texture = _prog->getUniformLocation("u_texture");
	_u_view = _prog->getUniformLocation("u_view");
	_u_has_texture = _prog->getUniformLocation("u_has_texture");
	_u_is_text = _prog->getUniformLocation("u_is_text");
}
#endif

Graphics2D::~Graphics2D()
{
	for (size_t i = 0; i < _max_draw_lists; ++i)
		_draw_lists[i].~DrawList2D();
	free(_draw_lists);

	glDeleteBuffers(1, &_ebo);
	glDeleteBuffers(1, &_vbo);
	glDeleteVertexArrays(1, &_vao);
}

Graphics2D::DrawCommand2D *Graphics2D::DrawList2D::AllocCommand()
{
	command_count++;
	if (command_count > max_commands)
	{
		max_commands += 4;
		cmd_list = (DrawCommand2D *)realloc(cmd_list, sizeof(DrawCommand2D) * max_commands);
		if (!cmd_list)
		{
			printf("Graphics2D::DrawList2D::AllocCommand: realloc failed\n");
			exit(1);
        }
	}

	return cmd_list + command_count - 1;
}

Graphics2D::Vertex *Graphics2D::DrawList2D::AllocVertices(GLsizei count)
{
	vertex_count += count;
	if (vertex_count > max_vertices)
	{
		max_vertices = std::max(vertex_count, max_vertices + 16);
		vertices = (Vertex *)realloc(vertices, sizeof(Vertex) * max_vertices);
		if (!vertices)
		{
			printf("Graphics2D::DrawList2D::AllocVertices: realloc failed\n");
			exit(1);
        }
	}

	return vertices + vertex_count - count;
}

uint32_t *Graphics2D::DrawList2D::AllocIndices(GLsizei count)
{
	index_count += count;
	if (index_count > max_indices)
	{
		max_indices = std::max(index_count, max_indices + 16);
		indices = (uint32_t *)realloc(indices, sizeof(uint32_t) * max_indices);
		if (!indices)
		{
			printf("Graphics2D::DrawList2D::AllocIndices: realloc failed\n");
			exit(1);
        }
	}

	return indices + index_count - count;
}

void Graphics2D::GetIndicesVector(std::vector<unsigned int> &indices)
{
    indices.clear();
	if (_draw_list_count == 0)
        return;

    const uint32_t *srcIndices = _draw_lists->indices;
    for (uint32_t i = 0; i < _draw_lists->index_count; i++)
	{
		indices.push_back(srcIndices[i]);
    }
}

uint32_t Graphics2D::GetNumIndices()
{
	if (_draw_list_count == 0)
        return 0;

	return _draw_lists->index_count;
}

uint32_t Graphics2D::GetNumVertices()
{
	if (_draw_list_count == 0)
		return 0;
	return _draw_lists->vertex_count;
}

void Graphics2D::GetVertices(prc_api_vertex *textVertices, Material in_material)
{
	if (_draw_list_count == 0)
        return;

    const Graphics2D::Vertex *srcVertices = _draw_lists->vertices;
	for (uint32_t i = 0; i < _draw_lists->vertex_count; i++)
	{
		textVertices[i].position[0] = srcVertices[i].position.x;
		textVertices[i].position[1] = srcVertices[i].position.y;
		textVertices[i].position[2] = srcVertices[i].position.z;
		textVertices[i].normal[0] = 0.0f;
		textVertices[i].normal[1] = 0.0f;
		textVertices[i].normal[2] = 1.0f;
		textVertices[i].uv[0] = srcVertices[i].tex_coords.x;
		textVertices[i].uv[1] = srcVertices[i].tex_coords.y;
		textVertices[i].normal_set = 1;
		textVertices[i].uv_set = 1;
		textVertices[i].color[0] = srcVertices[i].color.x;
		textVertices[i].color[1] = srcVertices[i].color.y;
		textVertices[i].color[2] = srcVertices[i].color.z;
		textVertices[i].color[3] = srcVertices[i].color.w;
        textVertices[i].alpha = in_material.alpha;
        textVertices[i].diffuse[0] = in_material.diffuse.x;
        textVertices[i].diffuse[1] = in_material.diffuse.y;
        textVertices[i].diffuse[2] = in_material.diffuse.z;
        textVertices[i].tint[0] = in_material.tint.x;
        textVertices[i].tint[1] = in_material.tint.y;
        textVertices[i].tint[2] = in_material.tint.z;
        textVertices[i].specular[0] = in_material.specular.x;
        textVertices[i].specular[1] = in_material.specular.y;
        textVertices[i].specular[2] = in_material.specular.z;
        textVertices[i].emissive[0] = in_material.emissive.x;
        textVertices[i].emissive[1] = in_material.emissive.y;
        textVertices[i].emissive[2] = in_material.emissive.z;
        textVertices[i].shininess = in_material.shininess;
		textVertices[i].tri_has_material = 0;
        textVertices[i].style_index = 0;
    }
}

void Graphics2D::DrawList2D::EmitQuad(float x0, float y0, float z0, float x1,
	                                  float y1, float z1, const Vector4 &color)
{
	/* write indices */

	uint32_t *indices = AllocIndices(6);
	GLsizei base_vertex = vertex_count;

	indices[0] = base_vertex + 0;
	indices[1] = base_vertex + 1;
	indices[2] = base_vertex + 3;

	indices[3] = base_vertex + 0;
	indices[4] = base_vertex + 3;
	indices[5] = base_vertex + 2;

	/* write vertices */

	Vertex *vertices = AllocVertices(4);

	vertices[0].position.x = x0;
	vertices[0].position.y = y0;
    vertices[0].position.z = z0;
	vertices[0].tex_coords = Vector2(0.0f);
	vertices[0].color = color;

	vertices[1].position.x = x1;
	vertices[1].position.y = y0;
    vertices[1].position.z = z0;
	vertices[1].tex_coords = Vector2(0.0f);
	vertices[1].color = color;

	vertices[2].position.x = x0;
	vertices[2].position.y = y1;
    vertices[2].position.z = z1;
	vertices[2].tex_coords = Vector2(0.0f);
	vertices[2].color = color;

	vertices[3].position.x = x1;
	vertices[3].position.y = y1;
    vertices[3].position.z = z1;
	vertices[3].tex_coords = Vector2(0.0f);
	vertices[3].color = color;
}

Graphics2D::DrawList2D::DrawList2D() :
	cmd_list(nullptr), command_count(0), max_commands(0),
	vertices(nullptr), vertex_count(0), max_vertices(0),
	indices(nullptr), index_count(0), max_indices(0)
{

}

Graphics2D::DrawList2D::~DrawList2D()
{
	free(cmd_list);
	free(vertices);
	free(indices);
}

Graphics2D::DrawList2D *Graphics2D::NewDrawList()
{
	_draw_list_count++;
	if (_draw_list_count > _max_draw_lists)
	{
		size_t new_count = _max_draw_lists + 16;

		_draw_lists = (DrawList2D *)realloc(_draw_lists, sizeof(DrawList2D) * new_count);
		if (!_draw_lists)
		{
			printf("Graphics2D::NewDrawList: realloc failed\n");
			exit(1);
        }

		for (size_t i = _max_draw_lists; i < new_count; ++i)
			new(_draw_lists + i) DrawList2D();

		_max_draw_lists = new_count;
	}

	return _draw_lists + _draw_list_count - 1;
}
