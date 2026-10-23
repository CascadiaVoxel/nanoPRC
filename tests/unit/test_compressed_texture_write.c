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

/* Texture coordinates written into a COMPRESSED tessellation and read back by
   the real parser and decoder.

   The check is per corner and does not rely on the encoder's vertex or
   triangle order, which the traversal is free to change: every input
   triangle is found among the decoded ones by its three positions, and each
   of its corners must come back with the UV the caller gave that corner.
   A writer that put the right pairs on the wrong corners, or a seam's UV on
   the other side of it, fails here even though every pair is present. */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "prc_test.h"
#include "prc_context.h"
#include "prc_data.h"
#include "prc_api.h"
#include "prc_write_global.h"
#include "prc_write_tree.h"
#include "prc_write_file_structure.h"
#include "prc_write_model.h"

static const char *TEST_PRC_FILENAME = "test_compressed_texture_write_output.prc";

/* 2x2 RGB, written with the textured cases that read through the public API,
   which reports is_texture only when the item's style carries an image. */
static const uint8_t TEST_PIXELS[2 * 2 * 3] = {
    0xFF, 0x00, 0x00,   0x00, 0xFF, 0x00,
    0x00, 0x00, 0xFF,   0xFF, 0xFF, 0x00
};

typedef struct
{
    const double *positions;
    uint32_t num_positions;
    const double *normals;      /* NULL: recalculated */
    const uint32_t *norm_indices;
    uint32_t num_normals;
    const uint32_t *tris;
    uint32_t num_triangles;
    const double *uvs;
    uint32_t num_uvs;
    const uint32_t *uv_indices;
} test_mesh;

/* texture_function for the textured items write_mesh writes. Left at 0
   (modulate) by every case except test_texture_function. */
static uint32_t g_texture_function = 0;

/* Wrap modes for the textured cases; 0 leaves the default. */
static uint32_t g_wrap_u = 0, g_wrap_v = 0;

/* Texture placement for the textured cases; zeros leave the default. */
static double g_scale[2] = { 0, 0 }, g_offset[2] = { 0, 0 };

static int
write_mesh(prc_context *ctx, const test_mesh *m, int with_image)
{
    prc_write_global_tables tables;
    prc_write_tess_entry tess_entry;
    prc_write_rep_item ri;
    prc_write_tree_node root;
    int code;
    uint32_t k;

    PRC_ASSERT_EQ(prc_write_global_tables_init(ctx, &tables), 0);

    memset(&tess_entry, 0, sizeof(tess_entry));
    tess_entry.kind = PRC_WRITE_TESS_KIND_COMPRESSED;
    tess_entry.positions = m->positions;
    tess_entry.num_positions = m->num_positions;
    tess_entry.normals = m->normals;
    tess_entry.num_normals = m->num_normals;
    tess_entry.norm_indices = m->norm_indices;
    tess_entry.tri_indices = m->tris;
    tess_entry.num_triangles = m->num_triangles;
    tess_entry.tex_coords = m->uvs;
    tess_entry.num_tex_coords = m->num_uvs;
    tess_entry.tex_indices = m->uv_indices;

    memset(&ri, 0, sizeof(ri));
    ri.kind = PRC_WRITE_RI_SURFACE;
    ri.biased_tessellation_index = 1;
    if (with_image)
    {
        ri.has_material = 1;
        ri.material_color[0] = ri.material_color[1] = ri.material_color[2] = 1.0;
        ri.material_alpha = 1.0;
        ri.has_texture = 1;
        ri.texture_image = TEST_PIXELS;
        ri.texture_image_size = sizeof(TEST_PIXELS);
        ri.texture_format = PRC_API_WRITE_TEXTURE_RGB;
        ri.texture_width = 2;
        ri.texture_height = 2;
        ri.texture_function = g_texture_function;
        ri.texture_wrap_u = g_wrap_u;
        ri.texture_wrap_v = g_wrap_v;
        ri.texture_scale[0] = g_scale[0];
        ri.texture_scale[1] = g_scale[1];
        ri.texture_offset[0] = g_offset[0];
        ri.texture_offset[1] = g_offset[1];
    }

    memset(&root, 0, sizeof(root));
    root.rep_items = &ri;
    root.num_rep_items = 1;
    for (k = 0; k < 3; k++)
    {
        root.bbox_min[k] = 1e300;
        root.bbox_max[k] = -1e300;
    }
    for (k = 0; k < m->num_positions * 3; k++)
    {
        if (m->positions[k] < root.bbox_min[k % 3]) root.bbox_min[k % 3] = m->positions[k];
        if (m->positions[k] > root.bbox_max[k % 3]) root.bbox_max[k % 3] = m->positions[k];
    }

    code = prc_write_prc_file(ctx, TEST_PRC_FILENAME, NULL, &tables, &root, &tess_entry, 1);
    prc_write_global_tables_free(ctx, &tables);
    return code;
}

static int
same_position(const double *a, const double *b)
{
    return fabs(a[0] - b[0]) < 1e-4 && fabs(a[1] - b[1]) < 1e-4 && fabs(a[2] - b[2]) < 1e-4;
}

/* Reads the file back and checks every input corner's UV. Returns the
   decoded tessellation's vertex count, for the seam case. */
static int
check_corners(prc_context *ctx, const test_mesh *m)
{
    prc_data *pd;
    prc_tess_3d_compressed *t;
    prc_compressed_texture_parameter *td;
    uint32_t tri, c, d, k, set_bits;
    uint32_t num_decoded;
    int num_vertices;

    pd = (prc_data *)prc_api_open_contents(ctx, TEST_PRC_FILENAME);
    if (pd == NULL)
        prc_print_error_stack(ctx);
    PRC_ASSERT_NOT_NULL(pd);
    PRC_ASSERT_EQ(pd->file_struct[0].tessellation->tess_count, 1);
    t = pd->file_struct[0].tessellation->tess[0].tess_3d_compressed;
    PRC_ASSERT_NOT_NULL(t);

    /* The block's own invariants, as measured on independently written
       files: one bit per corner, a reference for every set bit, a fresh pair
       for every clear one. */
    PRC_ASSERT_EQ(t->no_texture, 0);
    PRC_ASSERT_EQ(t->all_faces_have_texture, 1);
    td = t->texture_data;
    PRC_ASSERT_NOT_NULL(td);
    num_decoded = t->triangle_face_array_size;
    PRC_ASSERT_EQ(td->reference_array_size + td->texture_parameters_size / 2, num_decoded * 3);
    set_bits = 0;
    for (k = 0; k < num_decoded * 3; k++)
        set_bits += (td->binary_texture_data->texture_binary_data[k >> 3] >> (k & 7)) & 1;
    PRC_ASSERT_EQ(set_bits, td->reference_array_size);
    PRC_ASSERT_EQ(td->binary_texture_data->texture_binary_data_size, num_decoded * 3 / 32 + 1);
    PRC_ASSERT_EQ(td->binary_texture_data->last_integer_used_bit_number, num_decoded * 3 % 32);
    for (k = 0; k < td->reference_array_size; k++)
        PRC_ASSERT_EQ(td->reference_array[k], 0);

    /* The decoder kept the block: a block it could not replay is dropped. */
    PRC_ASSERT_NOT_NULL(t->uv_indices_prc_compressed_3d);
    PRC_ASSERT_EQ((uint32_t)t->num_triangle_indices_prc_compressed_3d, num_decoded * 3);
    PRC_ASSERT_EQ(num_decoded, m->num_triangles);

    for (tri = 0; tri < m->num_triangles; tri++)
    {
        int found = 0;

        for (d = 0; d < num_decoded && !found; d++)
        {
            int matched[3] = { -1, -1, -1 };
            int all = 1;

            for (c = 0; c < 3; c++)
            {
                const double *want = &m->positions[(size_t)m->tris[tri * 3 + c] * 3];
                for (k = 0; k < 3; k++)
                {
                    uint32_t vi = t->triangle_indices_prc_compressed_3d[d * 3 + k];
                    if (same_position(want, &t->vertices_prc_compressed_3d[(size_t)vi * 3]))
                        matched[c] = (int)k;
                }
                if (matched[c] < 0)
                    all = 0;
            }
            if (!all)
                continue;

            found = 1;
            for (c = 0; c < 3; c++)
            {
                const double *want = &m->uvs[(size_t)m->uv_indices[tri * 3 + c] * 2];
                uint32_t uv = t->uv_indices_prc_compressed_3d[d * 3 + (uint32_t)matched[c]];

                PRC_ASSERT(uv != PRC_COMPRESSED_TESS_NO_UV);
                PRC_ASSERT(uv < td->texture_parameters_size / 2);
                PRC_ASSERT_NEAR(t->uv_coordinates_3d[uv * 2 + 0], want[0], 1e-6);
                PRC_ASSERT_NEAR(t->uv_coordinates_3d[uv * 2 + 1], want[1], 1e-6);
            }
        }
        PRC_ASSERT(found);
    }

    num_vertices = t->num_vertices_prc_compressed_3d;
    prc_api_release_data(ctx, (prc_api_data)pd, NULL, 0, NULL, 0, NULL, 0, NULL);
    return num_vertices;
}

/* Unit square, one UV per vertex. The texture streams follow the position
   streams here; the seam and grid cases below make them differ. */
static const double QUAD_POS[4 * 3] = { 0, 0, 0,  1, 0, 0,  1, 1, 0,  0, 1, 0 };
static const double QUAD_UV[4 * 2] = { 0.1, 0.2,  0.9, 0.2,  0.9, 0.8,  0.1, 0.8 };
static const uint32_t QUAD_TRIS[6] = { 0, 1, 2,  0, 2, 3 };
static const double QUAD_NORMAL[3] = { 0, 0, 1 };
static const uint32_t QUAD_NIDX[6] = { 0, 0, 0,  0, 0, 0 };

static void
test_quad(prc_context *ctx)
{
    test_mesh m;

    printf("  sub-case: textured quad, normals recalculated\n");
    memset(&m, 0, sizeof(m));
    m.positions = QUAD_POS; m.num_positions = 4;
    m.tris = QUAD_TRIS; m.num_triangles = 2;
    m.uvs = QUAD_UV; m.num_uvs = 4; m.uv_indices = QUAD_TRIS;
    PRC_ASSERT_EQ(write_mesh(ctx, &m, 0), 0);
    PRC_ASSERT_EQ(check_corners(ctx, &m), 4);

    printf("  sub-case: textured quad, normals supplied\n");
    m.normals = QUAD_NORMAL; m.num_normals = 1; m.norm_indices = QUAD_NIDX;
    PRC_ASSERT_EQ(write_mesh(ctx, &m, 0), 0);
    PRC_ASSERT_EQ(check_corners(ctx, &m), 4);
}

/* A band round a cylinder, 8 segments, with the texture wrapped once: the
   column at angle 0 is u = 0 on one side and u = 1 on the other. Positions
   are shared all the way round, so the seam is two vertices whose corners
   carry two UVs each; they come back split, 16 positions as 18 vertices. */
#define BAND_N 8

static void
test_seam_band(prc_context *ctx)
{
    double pos[BAND_N * 2 * 3];
    double uvs[(BAND_N + 1) * 2 * 2];
    uint32_t tris[BAND_N * 6];
    uint32_t uv_idx[BAND_N * 6];
    test_mesh m;
    uint32_t i, r;

    printf("  sub-case: seam band, u = 0 and u = 1 on one column\n");
    for (i = 0; i < BAND_N; i++)
    {
        double a = 2.0 * 3.14159265358979323846 * (double)i / BAND_N;
        for (r = 0; r < 2; r++)
        {
            pos[(r * BAND_N + i) * 3 + 0] = cos(a);
            pos[(r * BAND_N + i) * 3 + 1] = sin(a);
            pos[(r * BAND_N + i) * 3 + 2] = (double)r;
        }
    }
    for (i = 0; i <= BAND_N; i++)
    {
        for (r = 0; r < 2; r++)
        {
            uvs[(r * (BAND_N + 1) + i) * 2 + 0] = (double)i / BAND_N;
            uvs[(r * (BAND_N + 1) + i) * 2 + 1] = (double)r;
        }
    }
    for (i = 0; i < BAND_N; i++)
    {
        uint32_t i1 = (i + 1) % BAND_N;      /* position wraps */
        uint32_t u0 = i, u1 = i + 1;         /* UV column does not */
        uint32_t *t = &tris[i * 6];
        uint32_t *q = &uv_idx[i * 6];

        t[0] = i;  t[1] = i1;           t[2] = BAND_N + i1;
        t[3] = i;  t[4] = BAND_N + i1;  t[5] = BAND_N + i;
        q[0] = u0; q[1] = u1;                 q[2] = (BAND_N + 1) + u1;
        q[3] = u0; q[4] = (BAND_N + 1) + u1;  q[5] = (BAND_N + 1) + u0;
    }

    memset(&m, 0, sizeof(m));
    m.positions = pos; m.num_positions = BAND_N * 2;
    m.tris = tris; m.num_triangles = BAND_N * 2;
    m.uvs = uvs; m.num_uvs = (BAND_N + 1) * 2; m.uv_indices = uv_idx;
    PRC_ASSERT_EQ(write_mesh(ctx, &m, 0), 0);
    PRC_ASSERT_EQ(check_corners(ctx, &m), BAND_N * 2 + 2);
}

/* A 4x4-vertex grid, 18 triangles, UV = position / 3: most corners reach a
   vertex already holding its pair, so the repeat path carries most of the
   block. The pair indices deliberately differ from the vertex indices. */
static void
test_heavy_reuse(prc_context *ctx)
{
    double pos[16 * 3];
    double uvs[16 * 2];
    uint32_t tris[18 * 3];
    uint32_t uv_idx[18 * 3];
    test_mesh m;
    uint32_t x, y, n = 0;

    printf("  sub-case: 4x4 grid, repeats exercised\n");
    for (y = 0; y < 4; y++)
    {
        for (x = 0; x < 4; x++)
        {
            uint32_t v = y * 4 + x;
            uint32_t p = 15 - v;     /* pair order reversed against vertex order */
            pos[v * 3 + 0] = (double)x;
            pos[v * 3 + 1] = (double)y;
            pos[v * 3 + 2] = 0.0;
            uvs[p * 2 + 0] = (double)x / 3.0;
            uvs[p * 2 + 1] = (double)y / 3.0;
        }
    }
    for (y = 0; y < 3; y++)
    {
        for (x = 0; x < 3; x++)
        {
            uint32_t a = y * 4 + x, b = a + 1, c = a + 5, d = a + 4;
            tris[n * 3 + 0] = a; tris[n * 3 + 1] = b; tris[n * 3 + 2] = c; n++;
            tris[n * 3 + 0] = a; tris[n * 3 + 1] = c; tris[n * 3 + 2] = d; n++;
        }
    }
    for (x = 0; x < n * 3; x++)
        uv_idx[x] = 15 - tris[x];

    memset(&m, 0, sizeof(m));
    m.positions = pos; m.num_positions = 16;
    m.tris = tris; m.num_triangles = n;
    m.uvs = uvs; m.num_uvs = 16; m.uv_indices = uv_idx;
    PRC_ASSERT_EQ(write_mesh(ctx, &m, 0), 0);
    PRC_ASSERT_EQ(check_corners(ctx, &m), 16);

    /* 54 corners over 16 vertices: at least 38 must be repeats. */
    {
        prc_data *pd = (prc_data *)prc_api_open_contents(ctx, TEST_PRC_FILENAME);
        PRC_ASSERT_NOT_NULL(pd);
        PRC_ASSERT_EQ(pd->file_struct[0].tessellation->tess[0].tess_3d_compressed->texture_data->reference_array_size, 38);
        prc_api_release_data(ctx, (prc_api_data)pd, NULL, 0, NULL, 0, NULL, 0, NULL);
    }
}

/* The public read path: is_texture on the face and a UV on every vertex,
   with the reader's V flip. */
static void
test_public_api(prc_context *ctx)
{
    test_mesh m;
    prc_api_data data;
    prc_api_product *model_tree = NULL;
    uint32_t num_parts = 0, num_products = 0, num_markups = 0;
    uint32_t num_tess = 0, num_line_tess = 0, num_extra = 0;
    prc_api_tess *tesses;
    uint8_t has_lines = 0;
    size_t v;

    printf("  sub-case: public API reports the texture\n");
    memset(&m, 0, sizeof(m));
    m.positions = QUAD_POS; m.num_positions = 4;
    m.tris = QUAD_TRIS; m.num_triangles = 2;
    m.uvs = QUAD_UV; m.num_uvs = 4; m.uv_indices = QUAD_TRIS;
    PRC_ASSERT_EQ(write_mesh(ctx, &m, 1), 0);

    data = prc_api_open_contents(ctx, TEST_PRC_FILENAME);
    PRC_ASSERT_NOT_NULL(data);
    PRC_ASSERT(prc_api_prep_model_tree(ctx, data, &num_parts, &num_products, &num_markups) >= 0);
    PRC_ASSERT(prc_api_create_model_tree(ctx, data, &model_tree, num_parts, num_products, num_markups) >= 0);
    PRC_ASSERT(prc_api_get_number_tessellations(ctx, data, model_tree, &num_tess,
        &num_line_tess, &num_extra, 0) >= 0);
    PRC_ASSERT(num_tess >= 1);
    tesses = (prc_api_tess *)calloc(num_tess, sizeof(prc_api_tess));
    PRC_ASSERT_NOT_NULL(tesses);

    tesses[0].num_faces = prc_api_get_number_faces(ctx, data, 0);
    PRC_ASSERT(tesses[0].num_faces >= 1);
    tesses[0].tess_faces = (prc_api_face *)calloc(tesses[0].num_faces, sizeof(prc_api_face));
    PRC_ASSERT_NOT_NULL(tesses[0].tess_faces);
    PRC_ASSERT(prc_api_initialize_tessellation(ctx, data, model_tree, 0, &tesses[0], NULL, &has_lines) >= 0);
    PRC_ASSERT_EQ(tesses[0].type, PRC_API_TESS_3D_Compressed);
    PRC_ASSERT(prc_api_get_tessellation_vertices(ctx, data, 0, 0, &tesses[0].tess_faces[0], &tesses[0]) >= 0);

    PRC_ASSERT(tesses[0].tess_faces[0].is_texture);
    PRC_ASSERT_EQ(tesses[0].tess_vertices.num_vertices, 4);
    for (v = 0; v < tesses[0].tess_vertices.num_vertices; v++)
    {
        const prc_api_vertex *pv = &tesses[0].tess_vertices.vertices[v];
        uint32_t q;
        int found = 0;

        PRC_ASSERT(pv->uv_set);
        for (q = 0; q < 4; q++)
        {
            if (fabs(pv->position[0] - QUAD_POS[q * 3 + 0]) < 1e-4 &&
                fabs(pv->position[1] - QUAD_POS[q * 3 + 1]) < 1e-4)
            {
                PRC_ASSERT_NEAR(pv->uv[0], QUAD_UV[q * 2 + 0], 1e-6);
                PRC_ASSERT_NEAR(pv->uv[1], 1.0 - QUAD_UV[q * 2 + 1], 1e-6);
                found = 1;
            }
        }
        PRC_ASSERT(found);
    }

    prc_api_release_data(ctx, data, tesses, num_tess, NULL, 0, NULL, 0, model_tree);
}

/* Writes a textured quad with the given API texture function and returns
   the value stored in the file's one texture definition. */
static uint32_t
written_texture_function(prc_context *ctx, uint32_t api_function)
{
    test_mesh m;
    prc_data *pd;
    const prc_file_struct_internal_global_data *gd;
    uint32_t stored;

    memset(&m, 0, sizeof(m));
    m.positions = QUAD_POS; m.num_positions = 4;
    m.tris = QUAD_TRIS; m.num_triangles = 2;
    m.uvs = QUAD_UV; m.num_uvs = 4; m.uv_indices = QUAD_TRIS;
    g_texture_function = api_function;
    PRC_ASSERT_EQ(write_mesh(ctx, &m, 1), 0);
    g_texture_function = 0;

    pd = (prc_data *)prc_api_open_contents(ctx, TEST_PRC_FILENAME);
    PRC_ASSERT_NOT_NULL(pd);
    gd = &pd->file_struct[0].globals->global_data;
    PRC_ASSERT_EQ(gd->texture_count, 1);
    stored = (uint32_t)gd->textures[0].texture_function;
    prc_api_release_data(ctx, (prc_api_data)pd, NULL, 0, NULL, 0, NULL, 0, NULL);
    return stored;
}

/* The API's values are not the file's: 0 is modulate here so that a zeroed
   item keeps its old behaviour, and the file stores modulate as 1 and replace
   as 2. A writer that copied the API value straight through would store 0
   (unknown) and 1 (modulate) and pass any test that only compared the two. */
static void
test_texture_function(prc_context *ctx)
{
    test_mesh m;

    printf("  sub-case: texture function left at 0 is stored as modulate\n");
    PRC_ASSERT_EQ(written_texture_function(ctx, PRC_API_WRITE_TEXTURE_MODULATE),
        (uint32_t)PRC_texture_function_modulate);

    printf("  sub-case: texture function replace is stored as replace\n");
    PRC_ASSERT_EQ(written_texture_function(ctx, PRC_API_WRITE_TEXTURE_REPLACE),
        (uint32_t)PRC_texture_function_replace);

    printf("  sub-case: an unknown texture function is refused\n");
    memset(&m, 0, sizeof(m));
    m.positions = QUAD_POS; m.num_positions = 4;
    m.tris = QUAD_TRIS; m.num_triangles = 2;
    m.uvs = QUAD_UV; m.num_uvs = 4; m.uv_indices = QUAD_TRIS;
    g_texture_function = 2;
    PRC_ASSERT(write_mesh(ctx, &m, 1) != 0);
    g_texture_function = 0;
}

/* Reads the written file's one texture definition and returns its wrap modes
   as stored in the file. */
static void
written_wrap(prc_context *ctx, int *s, int *t)
{
    prc_data *pd;
    const prc_graph_texture_definition *d;

    pd = (prc_data *)prc_api_open_contents(ctx, TEST_PRC_FILENAME);
    if (pd == NULL)
        prc_print_error_stack(ctx);
    PRC_ASSERT_NOT_NULL(pd);
    PRC_ASSERT_EQ(pd->file_struct[0].globals->global_data.texture_count, 1);
    d = &pd->file_struct[0].globals->global_data.textures[0];
    *s = (int)d->texture_wrapping_mode_s;
    *t = (int)d->texture_wrapping_mode_t;
    prc_api_release_data(ctx, (prc_api_data)pd, NULL, 0, NULL, 0, NULL, 0, NULL);
}

static void
test_texture_wrap(prc_context *ctx)
{
    test_mesh m;
    int s, t;

    memset(&m, 0, sizeof(m));
    m.positions = QUAD_POS; m.num_positions = 4;
    m.tris = QUAD_TRIS; m.num_triangles = 2;
    m.uvs = QUAD_UV; m.num_uvs = 4; m.uv_indices = QUAD_TRIS;

    /* The file's numbers differ from the API's, so a writer that copied the
       API value straight through fails each of these. */
    printf("  sub-case: wrap left at 0 writes repeat on both axes\n");
    g_wrap_u = 0; g_wrap_v = 0;
    PRC_ASSERT_EQ(write_mesh(ctx, &m, 1), 0);
    written_wrap(ctx, &s, &t);
    PRC_ASSERT_EQ(s, 1);
    PRC_ASSERT_EQ(t, 1);

    /* Different modes on the two axes, so swapped axes fail too. */
    printf("  sub-case: clamp to border on u, repeat on v\n");
    g_wrap_u = PRC_API_WRITE_TEXTURE_WRAP_CLAMP_TO_BORDER;
    g_wrap_v = PRC_API_WRITE_TEXTURE_WRAP_REPEAT;
    PRC_ASSERT_EQ(write_mesh(ctx, &m, 1), 0);
    written_wrap(ctx, &s, &t);
    PRC_ASSERT_EQ(s, 2);
    PRC_ASSERT_EQ(t, 1);

    printf("  sub-case: clamp to border on both axes\n");
    g_wrap_u = g_wrap_v = PRC_API_WRITE_TEXTURE_WRAP_CLAMP_TO_BORDER;
    PRC_ASSERT_EQ(write_mesh(ctx, &m, 1), 0);
    written_wrap(ctx, &s, &t);
    PRC_ASSERT_EQ(s, 2);
    PRC_ASSERT_EQ(t, 2);

    /* 2 was clamp to edge before Acrobat showed it as clamp to border. */
    printf("  sub-case: an unknown wrap mode is refused\n");
    g_wrap_u = 0; g_wrap_v = 2;
    PRC_ASSERT(write_mesh(ctx, &m, 1) != 0);
    g_wrap_u = 3; g_wrap_v = 0;
    PRC_ASSERT(write_mesh(ctx, &m, 1) != 0);
    g_wrap_u = g_wrap_v = 0;
}

/* Writes a textured quad with the current g_scale and g_offset, then copies
   out the file's one texture definition and, through the public API, the UV
   at each of the quad's four corners in QUAD_POS order. */
static void
write_and_read_placement(prc_context *ctx, prc_graph_texture_definition *def,
    float uv[4][2])
{
    test_mesh m;
    prc_data *pd;
    prc_api_data data;
    prc_api_product *model_tree = NULL;
    uint32_t num_parts = 0, num_products = 0, num_markups = 0;
    uint32_t num_tess = 0, num_line_tess = 0, num_extra = 0;
    prc_api_tess *tesses;
    uint8_t has_lines = 0;
    size_t v;
    uint32_t q;

    memset(&m, 0, sizeof(m));
    m.positions = QUAD_POS; m.num_positions = 4;
    m.tris = QUAD_TRIS; m.num_triangles = 2;
    m.uvs = QUAD_UV; m.num_uvs = 4; m.uv_indices = QUAD_TRIS;
    PRC_ASSERT_EQ(write_mesh(ctx, &m, 1), 0);

    pd = (prc_data *)prc_api_open_contents(ctx, TEST_PRC_FILENAME);
    PRC_ASSERT_NOT_NULL(pd);
    PRC_ASSERT_EQ(pd->file_struct[0].globals->global_data.texture_count, 1);
    *def = pd->file_struct[0].globals->global_data.textures[0];
    prc_api_release_data(ctx, (prc_api_data)pd, NULL, 0, NULL, 0, NULL, 0, NULL);

    data = prc_api_open_contents(ctx, TEST_PRC_FILENAME);
    PRC_ASSERT_NOT_NULL(data);
    PRC_ASSERT(prc_api_prep_model_tree(ctx, data, &num_parts, &num_products, &num_markups) >= 0);
    PRC_ASSERT(prc_api_create_model_tree(ctx, data, &model_tree, num_parts, num_products, num_markups) >= 0);
    PRC_ASSERT(prc_api_get_number_tessellations(ctx, data, model_tree, &num_tess,
        &num_line_tess, &num_extra, 0) >= 0);
    PRC_ASSERT(num_tess >= 1);
    tesses = (prc_api_tess *)calloc(num_tess, sizeof(prc_api_tess));
    PRC_ASSERT_NOT_NULL(tesses);
    tesses[0].num_faces = prc_api_get_number_faces(ctx, data, 0);
    PRC_ASSERT(tesses[0].num_faces >= 1);
    tesses[0].tess_faces = (prc_api_face *)calloc(tesses[0].num_faces, sizeof(prc_api_face));
    PRC_ASSERT_NOT_NULL(tesses[0].tess_faces);
    PRC_ASSERT(prc_api_initialize_tessellation(ctx, data, model_tree, 0, &tesses[0], NULL, &has_lines) >= 0);
    PRC_ASSERT(prc_api_get_tessellation_vertices(ctx, data, 0, 0, &tesses[0].tess_faces[0], &tesses[0]) >= 0);
    PRC_ASSERT_EQ(tesses[0].tess_vertices.num_vertices, 4);

    for (q = 0; q < 4; q++)
    {
        int found = 0;
        for (v = 0; v < tesses[0].tess_vertices.num_vertices; v++)
        {
            const prc_api_vertex *pv = &tesses[0].tess_vertices.vertices[v];
            if (fabs(pv->position[0] - QUAD_POS[q * 3 + 0]) < 1e-4 &&
                fabs(pv->position[1] - QUAD_POS[q * 3 + 1]) < 1e-4)
            {
                PRC_ASSERT(pv->uv_set);
                uv[q][0] = pv->uv[0];
                uv[q][1] = pv->uv[1];
                found = 1;
            }
        }
        PRC_ASSERT(found);
    }
    prc_api_release_data(ctx, data, tesses, num_tess, NULL, 0, NULL, 0, model_tree);
}

/* Each corner's UV through the public API, against u' = su * u + ou and
   v' = sv * v + ov followed by the reader's V flip. */
static void
check_placed_uvs(float uv[4][2], double su, double sv, double ou, double ov)
{
    uint32_t q;

    for (q = 0; q < 4; q++)
    {
        PRC_ASSERT_NEAR(uv[q][0], su * QUAD_UV[q * 2 + 0] + ou, 1e-5);
        PRC_ASSERT_NEAR(uv[q][1], 1.0 - (sv * QUAD_UV[q * 2 + 1] + ov), 1e-5);
    }
}

static void
test_texture_transform(prc_context *ctx)
{
    prc_graph_texture_definition d;
    float uv[4][2];
    test_mesh m;

    printf("  sub-case: placement left at 0 writes no texture transformation\n");
    g_scale[0] = g_scale[1] = 0; g_offset[0] = g_offset[1] = 0;
    write_and_read_placement(ctx, &d, uv);
    PRC_ASSERT_EQ(d.has_texture_transformation, 0);
    check_placed_uvs(uv, 1, 1, 0, 0);

    /* Different values on the two axes, and a negative offset, so swapped
       axes, a dropped sign or an offset applied before the scale all fail. */
    printf("  sub-case: per-axis scale with an offset\n");
    g_scale[0] = 2; g_scale[1] = 3; g_offset[0] = 0.25; g_offset[1] = -0.5;
    write_and_read_placement(ctx, &d, uv);
    PRC_ASSERT_EQ(d.has_texture_transformation, 1);
    PRC_ASSERT_EQ(d.texture_transformation.invert_s, 0);
    PRC_ASSERT_EQ(d.texture_transformation.invert_t, 0);
    PRC_ASSERT_EQ(d.texture_transformation.transform_2d, 1);
    PRC_ASSERT_EQ(d.texture_transformation.transform.behavior,
        PRC_TRANSFORMATION_Translate | PRC_TRANSFORMATION_NonUniformScale);
    PRC_ASSERT_NEAR(d.texture_transformation.transform.translation.x, 0.25, 1e-12);
    PRC_ASSERT_NEAR(d.texture_transformation.transform.translation.y, -0.5, 1e-12);
    PRC_ASSERT_NEAR(d.texture_transformation.transform.non_uniform_scale.x, 2, 1e-12);
    PRC_ASSERT_NEAR(d.texture_transformation.transform.non_uniform_scale.y, 3, 1e-12);
    check_placed_uvs(uv, 2, 3, 0.25, -0.5);

    printf("  sub-case: the same scale on both axes is written as uniform scale\n");
    g_scale[0] = g_scale[1] = 4; g_offset[0] = g_offset[1] = 0;
    write_and_read_placement(ctx, &d, uv);
    PRC_ASSERT_EQ(d.has_texture_transformation, 1);
    PRC_ASSERT_EQ(d.texture_transformation.transform.behavior, PRC_TRANSFORMATION_Scale);
    PRC_ASSERT_NEAR(d.texture_transformation.transform.scale, 4, 1e-12);
    check_placed_uvs(uv, 4, 4, 0, 0);

    printf("  sub-case: a negative scale or a non-finite offset is refused\n");
    memset(&m, 0, sizeof(m));
    m.positions = QUAD_POS; m.num_positions = 4;
    m.tris = QUAD_TRIS; m.num_triangles = 2;
    m.uvs = QUAD_UV; m.num_uvs = 4; m.uv_indices = QUAD_TRIS;
    g_scale[0] = -1; g_scale[1] = 1; g_offset[0] = g_offset[1] = 0;
    PRC_ASSERT(write_mesh(ctx, &m, 1) != 0);
    g_scale[0] = g_scale[1] = 1; g_offset[1] = NAN;
    PRC_ASSERT(write_mesh(ctx, &m, 1) != 0);
    g_scale[0] = g_scale[1] = 0; g_offset[0] = g_offset[1] = 0;
}

static void
test_refusals(prc_context *ctx)
{
    static const uint32_t bad_uv_idx[6] = { 0, 1, 2,  0, 2, 4 };
    test_mesh m;

    printf("  sub-case: a texture index past the coordinates is refused\n");
    memset(&m, 0, sizeof(m));
    m.positions = QUAD_POS; m.num_positions = 4;
    m.tris = QUAD_TRIS; m.num_triangles = 2;
    m.uvs = QUAD_UV; m.num_uvs = 4; m.uv_indices = bad_uv_idx;
    PRC_ASSERT(write_mesh(ctx, &m, 0) != 0);

    printf("  sub-case: coordinates without indices are refused\n");
    m.uv_indices = NULL;
    PRC_ASSERT(write_mesh(ctx, &m, 0) != 0);
}

int
main(void)
{
    prc_context *ctx;

    PRC_TEST_BEGIN("compressed tessellation texture write");

    ctx = prc_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    test_quad(ctx);
    test_seam_band(ctx);
    test_heavy_reuse(ctx);
    test_public_api(ctx);
    test_texture_function(ctx);
    test_texture_wrap(ctx);
    test_texture_transform(ctx);
    test_refusals(ctx);

    prc_release_context(ctx);
    remove(TEST_PRC_FILENAME);

    PRC_TEST_END;
}
