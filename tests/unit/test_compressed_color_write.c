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

/* Point colours written into a COMPRESSED tessellation and read back by the
   real parser and decoder.

   Colours are per corner, so the fixtures give corners that share a vertex
   different colours: every corner gets a colour of its own, made from its
   triangle and corner number. Each input triangle is found among the decoded
   ones by its three positions, and each of its corners must come back with
   the colour the caller gave that corner. A writer that stored one colour
   per vertex, or wrote a triangle's colours in the wrong corner order,
   fails here. */

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

static const char *TEST_PRC_FILENAME = "test_compressed_color_write_output.prc";

typedef struct
{
    const double *positions;
    uint32_t num_positions;
    const double *normals;      /* NULL: recalculated */
    const uint32_t *norm_indices;
    uint32_t num_normals;
    const uint32_t *tris;
    uint32_t num_triangles;
    const uint8_t *colors;      /* 3 or 4 bytes per corner */
    int have_alpha;
} test_mesh;

static int
write_mesh(prc_context *ctx, const test_mesh *m)
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
    tess_entry.vertex_colors = m->colors;
    tess_entry.num_vertex_colors = m->num_triangles * 3;
    tess_entry.vertex_colors_have_alpha = m->have_alpha;

    memset(&ri, 0, sizeof(ri));
    ri.kind = PRC_WRITE_RI_SURFACE;
    ri.biased_tessellation_index = 1;

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

/* Reads the file back and checks every input corner's colour. */
static void
check_corners(prc_context *ctx, const test_mesh *m)
{
    prc_data *pd;
    prc_tess_3d_compressed *t;
    uint32_t tri, c, d, k, stride = m->have_alpha ? 4u : 3u;
    uint32_t num_decoded;

    pd = (prc_data *)prc_api_open_contents(ctx, TEST_PRC_FILENAME);
    if (pd == NULL)
        prc_print_error_stack(ctx);
    PRC_ASSERT_NOT_NULL(pd);
    PRC_ASSERT_EQ(pd->file_struct[0].tessellation->tess_count, 1);
    t = pd->file_struct[0].tessellation->tess[0].tess_3d_compressed;
    PRC_ASSERT_NOT_NULL(t);

    /* As every independently written file with colours has them: one
       colour per corner, every face coloured, RGB with a 255 fifth byte. */
    PRC_ASSERT_EQ(t->is_point_color, 1);
    num_decoded = t->triangle_face_array_size;
    PRC_ASSERT_EQ(num_decoded, m->num_triangles);
    PRC_ASSERT_EQ(t->point_color_array_size, num_decoded * 15);
    for (k = 0; k < t->face_number; k++)
        PRC_ASSERT_EQ(t->is_point_color_on_face[k], 1);
    for (k = 0; k < num_decoded * 3; k++)
    {
        PRC_ASSERT_EQ(t->point_color_array[k * 5], m->have_alpha ? 1 : 0);
        if (!m->have_alpha)
            PRC_ASSERT_EQ(t->point_color_array[k * 5 + 4], 255);
    }
    PRC_ASSERT_NOT_NULL(t->point_colors_prc_compressed_3d);

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
                const uint8_t *want = &m->colors[((size_t)tri * 3 + c) * stride];
                const float *got = &t->point_colors_prc_compressed_3d[(size_t)(d * 3 + (uint32_t)matched[c]) * 4];

                PRC_ASSERT_NEAR(got[0], want[0] / 255.0, 1e-6);
                PRC_ASSERT_NEAR(got[1], want[1] / 255.0, 1e-6);
                PRC_ASSERT_NEAR(got[2], want[2] / 255.0, 1e-6);
                PRC_ASSERT_NEAR(got[3], m->have_alpha ? want[3] / 255.0 : 1.0, 1e-6);
            }
        }
        PRC_ASSERT(found);
    }

    prc_api_release_data(ctx, (prc_api_data)pd, NULL, 0, NULL, 0, NULL, 0, NULL);
}

/* A colour per corner, different at every corner. */
static void
make_colors(uint8_t *colors, uint32_t num_triangles, int have_alpha)
{
    uint32_t stride = have_alpha ? 4u : 3u, k;

    for (k = 0; k < num_triangles * 3; k++)
    {
        colors[k * stride + 0] = (uint8_t)(10 + 7 * k);
        colors[k * stride + 1] = (uint8_t)(200 - 5 * (k % 3));
        colors[k * stride + 2] = (uint8_t)(k / 3 * 11);
        if (have_alpha)
            colors[k * stride + 3] = (uint8_t)(255 - 9 * k);
    }
}

/* Unit cube, corner i at (i & 1, (i >> 1) & 1, (i >> 2) & 1), each face
   counter-clockwise seen from outside. */
static const uint32_t CUBE_QUADS[6][4] = {
    { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 },
    { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 }
};

static void
build_cube(double *pos, uint32_t *tris, double *normals, uint32_t *nidx)
{
    uint32_t i, f, n = 0;

    for (i = 0; i < 8; i++)
    {
        pos[i * 3 + 0] = (double)(i & 1);
        pos[i * 3 + 1] = (double)((i >> 1) & 1);
        pos[i * 3 + 2] = (double)((i >> 2) & 1);
    }
    for (f = 0; f < 6; f++)
    {
        const uint32_t *q = CUBE_QUADS[f];
        double c[3] = { 0, 0, 0 };
        uint32_t axis, best = 0;

        tris[n * 3 + 0] = q[0]; tris[n * 3 + 1] = q[1]; tris[n * 3 + 2] = q[2]; n++;
        tris[n * 3 + 0] = q[0]; tris[n * 3 + 1] = q[2]; tris[n * 3 + 2] = q[3]; n++;
        for (i = 0; i < 4; i++)
            for (axis = 0; axis < 3; axis++)
                c[axis] += pos[q[i] * 3 + axis] / 4.0;
        for (axis = 0; axis < 3; axis++)
            if (fabs(c[axis] - 0.5) > fabs(c[best] - 0.5))
                best = axis;
        normals[f * 3 + 0] = normals[f * 3 + 1] = normals[f * 3 + 2] = 0.0;
        normals[f * 3 + best] = c[best] > 0.5 ? 1.0 : -1.0;
        for (i = 0; i < 6; i++)
            nidx[f * 6 + i] = f;
    }
}

static void
test_cube(prc_context *ctx)
{
    double pos[8 * 3], normals[6 * 3];
    uint32_t tris[12 * 3], nidx[12 * 3];
    uint8_t colors[12 * 3 * 4];
    test_mesh m;

    build_cube(pos, tris, normals, nidx);
    memset(&m, 0, sizeof(m));
    m.positions = pos; m.num_positions = 8;
    m.tris = tris; m.num_triangles = 12;
    m.colors = colors;

    printf("  sub-case: cube, a colour per corner, normals recalculated\n");
    make_colors(colors, 12, 0);
    PRC_ASSERT_EQ(write_mesh(ctx, &m), 0);
    check_corners(ctx, &m);

    printf("  sub-case: cube, a colour per corner, normals supplied\n");
    m.normals = normals; m.num_normals = 6; m.norm_indices = nidx;
    PRC_ASSERT_EQ(write_mesh(ctx, &m), 0);
    check_corners(ctx, &m);

    printf("  sub-case: cube, RGBA colours\n");
    m.have_alpha = 1;
    make_colors(colors, 12, 1);
    PRC_ASSERT_EQ(write_mesh(ctx, &m), 0);
    check_corners(ctx, &m);
}

/* A 6x6-vertex grid, 50 triangles: long chains, so most triangles grow from
   a neighbour and the traversal's corner order differs from the input's. */
static void
test_grid(prc_context *ctx)
{
    double pos[36 * 3];
    uint32_t tris[50 * 3];
    uint8_t colors[50 * 3 * 3];
    test_mesh m;
    uint32_t x, y, n = 0;

    printf("  sub-case: 6x6 grid, a colour per corner\n");
    for (y = 0; y < 6; y++)
        for (x = 0; x < 6; x++)
        {
            pos[(y * 6 + x) * 3 + 0] = (double)x;
            pos[(y * 6 + x) * 3 + 1] = (double)y;
            pos[(y * 6 + x) * 3 + 2] = 0.2 * sin((double)(x + 2 * y));
        }
    for (y = 0; y < 5; y++)
        for (x = 0; x < 5; x++)
        {
            uint32_t a = y * 6 + x, b = a + 1, c = a + 7, d = a + 6;
            tris[n * 3 + 0] = a; tris[n * 3 + 1] = b; tris[n * 3 + 2] = c; n++;
            tris[n * 3 + 0] = a; tris[n * 3 + 1] = c; tris[n * 3 + 2] = d; n++;
        }
    make_colors(colors, n, 0);
    memset(&m, 0, sizeof(m));
    m.positions = pos; m.num_positions = 36;
    m.tris = tris; m.num_triangles = n;
    m.colors = colors;
    PRC_ASSERT_EQ(write_mesh(ctx, &m), 0);
    check_corners(ctx, &m);
}

static void
test_refusals(prc_context *ctx)
{
    double pos[8 * 3], normals[6 * 3];
    uint32_t tris[12 * 3], nidx[12 * 3];
    uint8_t colors[12 * 3 * 3];
    prc_write_global_tables tables;
    prc_write_tess_entry e;
    prc_write_rep_item ri;
    prc_write_tree_node root;

    printf("  sub-case: a colour count other than one per corner is refused\n");
    build_cube(pos, tris, normals, nidx);
    make_colors(colors, 12, 0);
    PRC_ASSERT_EQ(prc_write_global_tables_init(ctx, &tables), 0);
    memset(&e, 0, sizeof(e));
    e.kind = PRC_WRITE_TESS_KIND_COMPRESSED;
    e.positions = pos; e.num_positions = 8;
    e.tri_indices = tris; e.num_triangles = 12;
    e.vertex_colors = colors; e.num_vertex_colors = 8;   /* one per position */
    memset(&ri, 0, sizeof(ri));
    ri.kind = PRC_WRITE_RI_SURFACE;
    ri.biased_tessellation_index = 1;
    memset(&root, 0, sizeof(root));
    root.rep_items = &ri;
    root.num_rep_items = 1;
    root.bbox_max[0] = root.bbox_max[1] = root.bbox_max[2] = 1.0;
    PRC_ASSERT(prc_write_prc_file(ctx, TEST_PRC_FILENAME, NULL, &tables, &root, &e, 1) != 0);
    prc_write_global_tables_free(ctx, &tables);
}

int
main(void)
{
    prc_context *ctx;

    PRC_TEST_BEGIN("compressed tessellation colour write");

    ctx = prc_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    test_cube(ctx);
    test_grid(ctx);
    test_refusals(ctx);

    prc_release_context(ctx);
    remove(TEST_PRC_FILENAME);

    PRC_TEST_END;
}
