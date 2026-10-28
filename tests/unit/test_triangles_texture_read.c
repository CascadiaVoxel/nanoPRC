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

/* Texture coordinates of an uncompressed (TRIANGLES) tessellation, read
   through the public API, where corners that share a position carry
   different UVs.

   The fixture is a band round a cylinder with the texture wrapped once, so
   the column at angle 0 is u = 0 on one side and u = 1 on the other. Every
   corner at a position has the same normal, so only the UV can tell the
   corners apart: the reader must give the seam's two positions two vertices
   each, 16 positions as 18 vertices, and every corner's UV must be among
   them. A reader that kept the first UV it met at each position returns 16
   vertices and loses u = 1 on the seam. */

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

static const char *TEST_PRC_FILENAME = "test_triangles_texture_read_output.prc";

/* 2x2 RGB. The reader takes the texture indices only when the item's style
   carries an image. */
static const uint8_t TEST_PIXELS[2 * 2 * 3] = {
    0xFF, 0x00, 0x00,   0x00, 0xFF, 0x00,
    0x00, 0x00, 0xFF,   0xFF, 0xFF, 0x00
};

#define BAND_N 8

static double g_pos[BAND_N * 2 * 3];
static double g_nrm[BAND_N * 3];
static double g_uvs[(BAND_N + 1) * 2 * 2];
static uint32_t g_tris[BAND_N * 6];
static uint32_t g_nidx[BAND_N * 6];
static uint32_t g_uv_idx[BAND_N * 6];

static void
build_band(void)
{
    uint32_t i, r;

    for (i = 0; i < BAND_N; i++)
    {
        double a = 2.0 * 3.14159265358979323846 * (double)i / BAND_N;
        for (r = 0; r < 2; r++)
        {
            g_pos[(r * BAND_N + i) * 3 + 0] = cos(a);
            g_pos[(r * BAND_N + i) * 3 + 1] = sin(a);
            g_pos[(r * BAND_N + i) * 3 + 2] = (double)r;
        }
        g_nrm[i * 3 + 0] = cos(a);
        g_nrm[i * 3 + 1] = sin(a);
        g_nrm[i * 3 + 2] = 0.0;
    }
    for (i = 0; i <= BAND_N; i++)
    {
        for (r = 0; r < 2; r++)
        {
            g_uvs[(r * (BAND_N + 1) + i) * 2 + 0] = (double)i / BAND_N;
            g_uvs[(r * (BAND_N + 1) + i) * 2 + 1] = 0.25 + 0.5 * (double)r;
        }
    }
    for (i = 0; i < BAND_N; i++)
    {
        uint32_t i1 = (i + 1) % BAND_N;      /* position wraps */
        uint32_t u0 = i, u1 = i + 1;         /* UV column does not */
        uint32_t *t = &g_tris[i * 6];
        uint32_t *q = &g_uv_idx[i * 6];
        uint32_t k;

        t[0] = i;  t[1] = i1;           t[2] = BAND_N + i1;
        t[3] = i;  t[4] = BAND_N + i1;  t[5] = BAND_N + i;
        q[0] = u0; q[1] = u1;                 q[2] = (BAND_N + 1) + u1;
        q[3] = u0; q[4] = (BAND_N + 1) + u1;  q[5] = (BAND_N + 1) + u0;
        for (k = 0; k < 6; k++)
            g_nidx[i * 6 + k] = t[k] % BAND_N;   /* one radial normal per column */
    }
}

static int
write_band(prc_context *ctx)
{
    prc_write_global_tables tables;
    prc_write_tess_entry tess_entry;
    prc_write_rep_item ri;
    prc_write_tree_node root;
    uint32_t face_tris = BAND_N * 2;
    int code;
    uint32_t k;

    PRC_ASSERT_EQ(prc_write_global_tables_init(ctx, &tables), 0);

    memset(&tess_entry, 0, sizeof(tess_entry));
    tess_entry.kind = PRC_WRITE_TESS_KIND_3D;
    tess_entry.positions = g_pos;
    tess_entry.num_positions = BAND_N * 2;
    tess_entry.normals = g_nrm;
    tess_entry.num_normals = BAND_N;
    tess_entry.norm_indices = g_nidx;
    tess_entry.tri_indices = g_tris;
    tess_entry.num_triangles = BAND_N * 2;
    tess_entry.face_tri_counts = &face_tris;
    tess_entry.num_faces = 1;
    tess_entry.tex_coords = g_uvs;
    tess_entry.num_tex_coords = (BAND_N + 1) * 2;
    tess_entry.tex_indices = g_uv_idx;

    memset(&ri, 0, sizeof(ri));
    ri.kind = PRC_WRITE_RI_SURFACE;
    ri.biased_tessellation_index = 1;
    ri.has_material = 1;
    ri.material_color[0] = ri.material_color[1] = ri.material_color[2] = 1.0;
    ri.material_alpha = 1.0;
    ri.has_texture = 1;
    ri.texture_image = TEST_PIXELS;
    ri.texture_image_size = sizeof(TEST_PIXELS);
    ri.texture_format = PRC_API_WRITE_TEXTURE_RGB;
    ri.texture_width = 2;
    ri.texture_height = 2;

    memset(&root, 0, sizeof(root));
    root.rep_items = &ri;
    root.num_rep_items = 1;
    for (k = 0; k < 3; k++)
    {
        root.bbox_min[k] = 1e300;
        root.bbox_max[k] = -1e300;
    }
    for (k = 0; k < BAND_N * 2 * 3; k++)
    {
        if (g_pos[k] < root.bbox_min[k % 3]) root.bbox_min[k % 3] = g_pos[k];
        if (g_pos[k] > root.bbox_max[k % 3]) root.bbox_max[k % 3] = g_pos[k];
    }

    code = prc_write_prc_file(ctx, TEST_PRC_FILENAME, NULL, &tables, &root, &tess_entry, 1);
    prc_write_global_tables_free(ctx, &tables);
    return code;
}

static void
test_seam_band(prc_context *ctx)
{
    prc_api_data data;
    prc_api_product *model_tree = NULL;
    uint32_t num_parts = 0, num_products = 0, num_markups = 0;
    uint32_t num_tess = 0, num_line_tess = 0, num_extra = 0;
    prc_api_tess *tesses;
    const prc_api_tess_vertex_buffer *vb;
    uint8_t has_lines = 0;
    uint32_t c;
    size_t v;

    printf("  sub-case: seam band, u = 0 and u = 1 on one column, shared normals\n");
    build_band();
    PRC_ASSERT_EQ(write_band(ctx), 0);

    data = prc_api_open_contents(ctx, TEST_PRC_FILENAME);
    if (data == NULL)
        prc_print_error_stack(ctx);
    PRC_ASSERT_NOT_NULL(data);
    PRC_ASSERT(prc_api_prep_model_tree(ctx, data, &num_parts, &num_products, &num_markups) >= 0);
    PRC_ASSERT(prc_api_create_model_tree(ctx, data, &model_tree, num_parts, num_products, num_markups) >= 0);
    PRC_ASSERT(prc_api_get_number_tessellations(ctx, data, model_tree, &num_tess,
        &num_line_tess, &num_extra, 0) >= 0);
    PRC_ASSERT(num_tess >= 1);
    tesses = (prc_api_tess *)calloc(num_tess, sizeof(prc_api_tess));
    PRC_ASSERT_NOT_NULL(tesses);
    tesses[0].num_faces = prc_api_get_number_faces(ctx, data, 0);
    PRC_ASSERT_EQ(tesses[0].num_faces, 1);
    tesses[0].tess_faces = (prc_api_face *)calloc(tesses[0].num_faces, sizeof(prc_api_face));
    PRC_ASSERT_NOT_NULL(tesses[0].tess_faces);
    PRC_ASSERT(prc_api_initialize_tessellation(ctx, data, model_tree, 0, &tesses[0], NULL, &has_lines) >= 0);
    PRC_ASSERT_EQ(tesses[0].type, PRC_API_TESS_3D);
    PRC_ASSERT(prc_api_get_tessellation_vertices(ctx, data, 0, 0, &tesses[0].tess_faces[0], &tesses[0]) >= 0);
    PRC_ASSERT(tesses[0].tess_faces[0].is_texture);

    vb = &tesses[0].tess_faces[0].face_vertices;
    PRC_ASSERT_EQ(vb->num_vertices, BAND_N * 2 + 2);

    /* Every corner the caller gave, position and UV, is a vertex. */
    for (c = 0; c < BAND_N * 6; c++)
    {
        const double *p = &g_pos[(size_t)g_tris[c] * 3];
        const double *uv = &g_uvs[(size_t)g_uv_idx[c] * 2];
        int found = 0;

        for (v = 0; v < vb->num_vertices && !found; v++)
        {
            const prc_api_vertex *pv = &vb->vertices[v];
            if (fabs(pv->position[0] - p[0]) < 1e-4 && fabs(pv->position[1] - p[1]) < 1e-4 &&
                fabs(pv->position[2] - p[2]) < 1e-4 && pv->uv_set &&
                fabs(pv->uv[0] - uv[0]) < 1e-6 && fabs(pv->uv[1] - (1.0 - uv[1])) < 1e-6)
                found = 1;
        }
        if (!found)
            printf("    corner %u: no vertex at (%g, %g, %g) with uv (%g, %g)\n",
                c, p[0], p[1], p[2], uv[0], uv[1]);
        PRC_ASSERT(found);
    }

    prc_api_release_data(ctx, data, tesses, num_tess, NULL, 0, NULL, 0, model_tree);
}

int
main(void)
{
    prc_context *ctx;

    PRC_TEST_BEGIN("uncompressed tessellation texture read");

    ctx = prc_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    test_seam_band(ctx);

    prc_release_context(ctx);
    remove(TEST_PRC_FILENAME);

    PRC_TEST_END;
}
