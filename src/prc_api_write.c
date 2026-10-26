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

/* Implementation of the public write-facility entry point declared in
   include/prc_api.h. This is a thin wrapper: prc_api_write_node,
   prc_api_write_rep_item, and prc_api_write_tessellation are the same
   types the internal encoder (prc_write_tree.c / prc_write_file_structure.c
   / prc_write_model.c) already works with -- prc_write_tree_node etc. are
   typedef aliases of these public types, not separate copies -- so no
   conversion step is needed here, only supplying the (currently always
   empty) global color/material/style table those internals require. */

#include "../include/prc_api.h"
#include "prc_write_global.h"
#include "prc_write_tree.h"
#include "prc_write_file_structure.h"
#include "prc_write_model.h"

/* Every count, index and pointer the caller passes is checked here, before
   the encoder reads any of it. The encoder trusts what it is given, so a NULL
   array behind a non-zero count, an index past the array it indexes, face
   counts that do not add up, or counts whose products overflow the encoder's
   32-bit buffer sizes all used to crash it or write past a buffer. They are
   now refused with PRC_ERROR_INTERNAL. Totals are checked before any index is
   read, so a count larger than its array is caught without reading past it.

   Checks the encoder already makes (group sizes, a group's normals agreeing
   with its face, textures needing normals) are not repeated. */

/* A cycle in the caller's tree would otherwise be followed for ever. */
#define PRC_WRITE_MAX_TREE_VISITS 100000000u

static int
prc_api_write_refuse(prc_context *ctx, uint32_t entry, const char *what)
{
    prc_error(ctx, PRC_ERROR_INTERNAL, "prc_api_write: tessellation entry %u: %s\n", entry + 1, what);
    return PRC_ERROR_INTERNAL;
}

/* 1 if each of count indices is below limit. */
static int
prc_api_write_indices_ok(const uint32_t *indices, uint64_t count, uint32_t limit)
{
    uint64_t k;

    for (k = 0; k < count; k++)
        if (indices[k] >= limit)
            return 0;
    return 1;
}

static int
prc_api_write_check_group(prc_context *ctx, uint32_t e, const prc_api_write_tessellation *t,
    const prc_api_write_tri_group *g)
{
    if (g->num_vertices > 0 && g->vertex_indices == NULL)
        return prc_api_write_refuse(ctx, e, "a fan or strip has vertices but no vertex_indices");
    if (!prc_api_write_indices_ok(g->vertex_indices, g->num_vertices, t->num_positions))
        return prc_api_write_refuse(ctx, e, "a fan or strip vertex index is past num_positions");
    if (g->normal_indices != NULL &&
        (t->normals == NULL || !prc_api_write_indices_ok(g->normal_indices, g->num_vertices, t->num_normals)))
        return prc_api_write_refuse(ctx, e, "a fan or strip normal index is past num_normals");
    if (g->tex_indices != NULL &&
        (t->tex_coords == NULL || !prc_api_write_indices_ok(g->tex_indices, g->num_vertices, t->num_tex_coords)))
        return prc_api_write_refuse(ctx, e, "a fan or strip texture index is past num_tex_coords");
    return 0;
}

static int
prc_api_write_check_tess(prc_context *ctx, uint32_t e, const prc_api_write_tessellation *t)
{
    uint64_t corners, faces_sum, group_verts = 0, groups = 0;
    uint32_t f, g;
    int code;

    if (t->kind == PRC_API_WRITE_TESS_KIND_WIRE)
    {
        uint64_t total = 0;

        if (t->num_wire_elements > 0 && t->wire_elements == NULL)
            return prc_api_write_refuse(ctx, e, "num_wire_elements is set but wire_elements is NULL");
        for (f = 0; f < t->num_wire_elements; f++)
        {
            const prc_api_write_wire_element *w = &t->wire_elements[f];

            if (w->num_vertices > 0 && w->positions == NULL)
                return prc_api_write_refuse(ctx, e, "a wire element has vertices but no positions");
            total += w->num_vertices;
        }
        /* The encoder sizes a hash table at twice the total, in 32 bits. */
        if (total > 0x7FFFFFFFu)
            return prc_api_write_refuse(ctx, e, "the wire elements hold too many vertices");
        if (t->num_vertex_colors > 0 && t->vertex_colors == NULL)
            return prc_api_write_refuse(ctx, e, "num_vertex_colors is set but vertex_colors is NULL");
        return 0;
    }
    if (t->kind != PRC_API_WRITE_TESS_KIND_TRIANGLES && t->kind != PRC_API_WRITE_TESS_KIND_COMPRESSED)
        return prc_api_write_refuse(ctx, e, "unknown kind");

    if (t->num_positions > 0 && t->positions == NULL)
        return prc_api_write_refuse(ctx, e, "num_positions is set but positions is NULL");
    if (t->norm_indices != NULL && t->normals == NULL)
        return prc_api_write_refuse(ctx, e, "norm_indices is set but normals is NULL");
    if (t->kind == PRC_API_WRITE_TESS_KIND_COMPRESSED && t->normals != NULL && t->norm_indices == NULL)
        return prc_api_write_refuse(ctx, e, "a compressed entry with normals needs norm_indices");
    if (t->num_tex_coords > 0 && t->tex_coords == NULL)
        return prc_api_write_refuse(ctx, e, "num_tex_coords is set but tex_coords is NULL");
    if (t->tex_indices != NULL && t->tex_coords == NULL)
        return prc_api_write_refuse(ctx, e, "tex_indices is set but tex_coords is NULL");
    if (t->num_vertex_colors > 0 && t->vertex_colors == NULL)
        return prc_api_write_refuse(ctx, e, "num_vertex_colors is set but vertex_colors is NULL");

    /* Totals first, in 64 bits: the encoder sizes its index buffer at 9 words
       per triangle plus 3 per fan or strip vertex plus 1 per group, in 32. */
    if (t->face_groups != NULL)
    {
        for (f = 0; f < t->num_faces; f++)
        {
            const prc_api_write_face_groups *fg = &t->face_groups[f];

            if ((fg->num_fans > 0 && fg->fans == NULL) || (fg->num_strips > 0 && fg->strips == NULL))
                return prc_api_write_refuse(ctx, e, "a face declares fans or strips but supplies no array");
            for (g = 0; g < fg->num_fans; g++)
                group_verts += fg->fans[g].num_vertices;
            for (g = 0; g < fg->num_strips; g++)
                group_verts += fg->strips[g].num_vertices;
            groups += (uint64_t)fg->num_fans + fg->num_strips;
        }
    }
    if ((uint64_t)t->num_triangles * 9 + group_verts * 3 + groups > 0xFFFFFFFFu)
        return prc_api_write_refuse(ctx, e, "too many triangles and fan or strip vertices");

    corners = (uint64_t)t->num_triangles * 3;
    if (t->num_triangles > 0 && t->tri_indices == NULL)
        return prc_api_write_refuse(ctx, e, "num_triangles is set but tri_indices is NULL");
    if (t->num_faces > 0)
    {
        if (t->face_tri_counts == NULL)
            return prc_api_write_refuse(ctx, e, "num_faces is set but face_tri_counts is NULL");
        faces_sum = 0;
        for (f = 0; f < t->num_faces; f++)
            faces_sum += t->face_tri_counts[f];
        if (faces_sum != t->num_triangles)
            return prc_api_write_refuse(ctx, e, "face_tri_counts does not add up to num_triangles");
    }

    /* Now every index, against the array it indexes. */
    if (t->num_triangles > 0)
    {
        if (!prc_api_write_indices_ok(t->tri_indices, corners, t->num_positions))
            return prc_api_write_refuse(ctx, e, "a triangle index is past num_positions");
        if (t->norm_indices != NULL && !prc_api_write_indices_ok(t->norm_indices, corners, t->num_normals))
            return prc_api_write_refuse(ctx, e, "a normal index is past num_normals");
        if (t->tex_indices != NULL && !prc_api_write_indices_ok(t->tex_indices, corners, t->num_tex_coords))
            return prc_api_write_refuse(ctx, e, "a texture index is past num_tex_coords");
    }
    if (t->face_groups != NULL)
    {
        for (f = 0; f < t->num_faces; f++)
        {
            const prc_api_write_face_groups *fg = &t->face_groups[f];

            for (g = 0; g < fg->num_fans; g++)
                if ((code = prc_api_write_check_group(ctx, e, t, &fg->fans[g])) != 0)
                    return code;
            for (g = 0; g < fg->num_strips; g++)
                if ((code = prc_api_write_check_group(ctx, e, t, &fg->strips[g])) != 0)
                    return code;
        }
    }
    return 0;
}

/* The tree: no NULL child, no NULL rep_items behind a count, and every item's
   tessellation index inside the table. Walked with a heap stack, so a deep
   tree cannot overflow the C stack, and with a cap on visits, so a cycle
   cannot loop for ever. */
static int
prc_api_write_check_tree(prc_context *ctx, const prc_api_write_node *root, uint32_t num_tess_entries)
{
    const prc_api_write_node **stack;
    uint32_t top = 0, cap = 64;
    uint64_t visits = 0;
    int code = 0;

    stack = (const prc_api_write_node **)prc_malloc(ctx, cap * sizeof(*stack));
    if (stack == NULL)
        return PRC_ERROR_MEMORY;
    stack[top++] = root;
    while (top > 0 && code == 0)
    {
        const prc_api_write_node *node = stack[--top];
        uint32_t k;

        if (++visits > PRC_WRITE_MAX_TREE_VISITS)
        {
            prc_error(ctx, PRC_ERROR_INTERNAL, "prc_api_write: the node tree is too large, or has a cycle\n");
            code = PRC_ERROR_INTERNAL;
            break;
        }
        if (node->num_rep_items > 0 && node->rep_items == NULL)
        {
            prc_error(ctx, PRC_ERROR_INTERNAL, "prc_api_write: a node has num_rep_items but no rep_items\n");
            code = PRC_ERROR_INTERNAL;
            break;
        }
        for (k = 0; k < node->num_rep_items; k++)
        {
            uint32_t bt = node->rep_items[k].biased_tessellation_index;

            if (bt == 0 || bt > num_tess_entries)
            {
                prc_error(ctx, PRC_ERROR_INTERNAL,
                    "prc_api_write: a representation item's biased_tessellation_index %u is not 1 to %u\n",
                    bt, num_tess_entries);
                code = PRC_ERROR_INTERNAL;
                break;
            }
        }
        if (code != 0)
            break;
        if (node->num_children > 0 && node->children == NULL)
        {
            prc_error(ctx, PRC_ERROR_INTERNAL, "prc_api_write: a node has num_children but no children\n");
            code = PRC_ERROR_INTERNAL;
            break;
        }
        for (k = 0; k < node->num_children; k++)
        {
            if (node->children[k] == NULL)
            {
                prc_error(ctx, PRC_ERROR_INTERNAL, "prc_api_write: a node has a NULL child\n");
                code = PRC_ERROR_INTERNAL;
                break;
            }
            if (top == cap)
            {
                const prc_api_write_node **grown;

                if (cap > 0x7FFFFFFFu / 2)
                {
                    code = PRC_ERROR_MEMORY;
                    break;
                }
                grown = (const prc_api_write_node **)prc_realloc(ctx, stack, (size_t)cap * 2 * sizeof(*stack));
                if (grown == NULL)
                {
                    code = PRC_ERROR_MEMORY;
                    break;
                }
                stack = grown;
                cap *= 2;
            }
            stack[top++] = node->children[k];
        }
    }
    prc_free(ctx, stack);
    return code;
}

static int
prc_api_write_check_inputs(prc_context *ctx, const prc_api_write_node *root,
    const prc_api_write_tessellation *tess_entries, uint32_t num_tess_entries)
{
    uint32_t e;
    int code;

    if (num_tess_entries > 0 && tess_entries == NULL)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_api_write: num_tess_entries is set but tess_entries is NULL\n");
        return PRC_ERROR_INTERNAL;
    }
    for (e = 0; e < num_tess_entries; e++)
        if ((code = prc_api_write_check_tess(ctx, e, &tess_entries[e])) != 0)
            return code;
    return prc_api_write_check_tree(ctx, root, num_tess_entries);
}

int
prc_api_write_prc_file(prc_context *ctx, const char *filename,
    const char *model_name, const prc_api_write_node *root,
    const prc_api_write_tessellation *tess_entries, uint32_t num_tess_entries)
{
    prc_write_global_tables tables;
    int code;

    if (ctx == NULL || filename == NULL || root == NULL)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_api_write_prc_file: invalid arguments\n");
        return PRC_ERROR_INTERNAL;
    }
    code = prc_api_write_check_inputs(ctx, root, tess_entries, num_tess_entries);
    if (code != 0)
        return code;

    if (prc_write_global_tables_init(ctx, &tables) != 0)
        return PRC_ERROR_MEMORY;

    code = prc_write_prc_file(ctx, filename, model_name, &tables, root, tess_entries, num_tess_entries);

    prc_write_global_tables_free(ctx, &tables);
    return code;
}

int
prc_api_write_prc_buffer(prc_context *ctx,
    const char *model_name, const prc_api_write_node *root,
    const prc_api_write_tessellation *tess_entries, uint32_t num_tess_entries,
    uint8_t **out_buf, size_t *out_size)
{
    prc_write_global_tables tables;
    int code;

    if (ctx == NULL || root == NULL || out_buf == NULL || out_size == NULL)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_api_write_prc_buffer: invalid arguments\n");
        return PRC_ERROR_INTERNAL;
    }
    code = prc_api_write_check_inputs(ctx, root, tess_entries, num_tess_entries);
    if (code != 0)
        return code;

    if (prc_write_global_tables_init(ctx, &tables) != 0)
        return PRC_ERROR_MEMORY;

    code = prc_write_prc_buffer(ctx, model_name, &tables, root, tess_entries, num_tess_entries, out_buf, out_size);

    prc_write_global_tables_free(ctx, &tables);
    return code;
}

void
prc_api_write_prc_buffer_free(prc_context *ctx, uint8_t *buf)
{
    prc_free(ctx, buf);
}
