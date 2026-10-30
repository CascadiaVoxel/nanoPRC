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

/* Triangle winding through a COMPRESSED tessellation, written and read back
   by the real parser and decoder.

   Written without normals, the file says to recalculate them, and the
   decoder takes each triangle's winding from its normal_is_reversed bit
   alone. Each input triangle is found among the decoded ones by its three
   positions, and its corners must come back in the same cyclic order: the
   input order or a rotation of it, never reversed. A writer that set the bit
   per chain rather than per triangle reversed every other triangle of a
   strip, and fails here. */

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

static const char *TEST_PRC_FILENAME = "test_compressed_winding_write_output.prc";

typedef struct
{
    const double *positions;
    uint32_t num_positions;
    const double *normals;      /* NULL: recalculated */
    const uint32_t *norm_indices;
    uint32_t num_normals;
    const uint32_t *tris;
    uint32_t num_triangles;
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

/* Reads the file back and checks every input triangle's winding. */
static void
check_winding(prc_context *ctx, const test_mesh *m)
{
    prc_data *pd;
    prc_tess_3d_compressed *t;
    uint32_t tri, c, d, k, num_decoded, reversed = 0;

    pd = (prc_data *)prc_api_open_contents(ctx, TEST_PRC_FILENAME);
    if (pd == NULL)
        prc_print_error_stack(ctx);
    PRC_ASSERT_NOT_NULL(pd);
    PRC_ASSERT_EQ(pd->file_struct[0].tessellation->tess_count, 1);
    t = pd->file_struct[0].tessellation->tess[0].tess_3d_compressed;
    PRC_ASSERT_NOT_NULL(t);
    num_decoded = t->triangle_face_array_size;
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
            /* Same cyclic order: input corner c+1 follows input corner c. */
            if ((matched[1] - matched[0] + 3) % 3 != 1 || (matched[2] - matched[1] + 3) % 3 != 1)
                reversed++;
        }
        PRC_ASSERT(found);
    }
    if (reversed != 0)
        printf("    %u of %u triangles came back reversed\n", reversed, m->num_triangles);
    PRC_ASSERT_EQ(reversed, 0);

    prc_api_release_data(ctx, (prc_api_data)pd, NULL, 0, NULL, 0, NULL, 0, NULL);
}

/* Unit cube, corner i at (i & 1, (i >> 1) & 1, (i >> 2) & 1), each face
   counter-clockwise seen from outside. */
static const uint32_t CUBE_QUADS[6][4] = {
    { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 },
    { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 }
};

static void
test_cube(prc_context *ctx)
{
    double pos[8 * 3], normals[6 * 3];
    uint32_t tris[12 * 3], nidx[12 * 3];
    uint32_t i, f, axis, n = 0;
    test_mesh m;

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
        uint32_t best = 0;

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

    memset(&m, 0, sizeof(m));
    m.positions = pos; m.num_positions = 8;
    m.tris = tris; m.num_triangles = 12;

    printf("  sub-case: cube, normals recalculated\n");
    PRC_ASSERT_EQ(write_mesh(ctx, &m), 0);
    check_winding(ctx, &m);

    printf("  sub-case: cube, normals supplied\n");
    m.normals = normals; m.num_normals = 6; m.norm_indices = nidx;
    PRC_ASSERT_EQ(write_mesh(ctx, &m), 0);
    check_winding(ctx, &m);
}

/* A flat strip of 16 triangles facing +z, then the same strip wound to face
   -z: one chain, so every triangle after the first grows from its
   neighbour. */
static void
test_strip(prc_context *ctx)
{
    double pos[18 * 3];
    uint32_t tris[16 * 3];
    uint32_t i, n = 0;
    test_mesh m;

    for (i = 0; i < 9; i++)
    {
        pos[i * 3 + 0] = (double)i; pos[i * 3 + 1] = 0.0; pos[i * 3 + 2] = 0.0;
        pos[(9 + i) * 3 + 0] = (double)i; pos[(9 + i) * 3 + 1] = 1.0; pos[(9 + i) * 3 + 2] = 0.0;
    }
    for (i = 0; i < 8; i++)
    {
        uint32_t a = i, b = i + 1, c = 9 + i + 1, d = 9 + i;
        tris[n * 3 + 0] = a; tris[n * 3 + 1] = b; tris[n * 3 + 2] = c; n++;
        tris[n * 3 + 0] = a; tris[n * 3 + 1] = c; tris[n * 3 + 2] = d; n++;
    }
    memset(&m, 0, sizeof(m));
    m.positions = pos; m.num_positions = 18;
    m.tris = tris; m.num_triangles = n;

    printf("  sub-case: flat strip, 16 triangles, facing +z\n");
    PRC_ASSERT_EQ(write_mesh(ctx, &m), 0);
    check_winding(ctx, &m);

    printf("  sub-case: the same strip facing -z\n");
    for (i = 0; i < n; i++)
    {
        uint32_t tmp = tris[i * 3 + 1];
        tris[i * 3 + 1] = tris[i * 3 + 2];
        tris[i * 3 + 2] = tmp;
    }
    PRC_ASSERT_EQ(write_mesh(ctx, &m), 0);
    check_winding(ctx, &m);
}

/* A 6x6-vertex curved grid, 50 triangles: several chains, most triangles
   growing from a neighbour. */
static void
test_grid(prc_context *ctx)
{
    double pos[36 * 3];
    uint32_t tris[50 * 3];
    uint32_t x, y, n = 0;
    test_mesh m;

    printf("  sub-case: 6x6 curved grid, normals recalculated\n");
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
    memset(&m, 0, sizeof(m));
    m.positions = pos; m.num_positions = 36;
    m.tris = tris; m.num_triangles = n;
    PRC_ASSERT_EQ(write_mesh(ctx, &m), 0);
    check_winding(ctx, &m);
}

int
main(void)
{
    prc_context *ctx;

    PRC_TEST_BEGIN("compressed tessellation winding write");

    ctx = prc_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    test_cube(ctx);
    test_strip(ctx);
    test_grid(ctx);

    prc_release_context(ctx);
    remove(TEST_PRC_FILENAME);

    PRC_TEST_END;
}
