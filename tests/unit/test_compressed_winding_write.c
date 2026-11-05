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
   strip, and fails here.

   Written with normals, the file must keep them: the decoder then derives
   each triangle's bit from the decoded normal at its first corner, and every
   decoded corner normal must be the supplied one. A writer that decided the
   bit from the supplied normal instead could disagree with the decoder, and
   fell back to recalculated normals on the hard-edged cube.

   The meshes run from a 12-triangle cube to a 4,096-triangle torus, whose
   triangles mostly sit far from the start of their chain. */

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

/* Reads the file back and checks every input triangle's winding, and, when
   normals were supplied, that they were stored and came back. */
static void
check_decoded(prc_context *ctx, const test_mesh *m, int check_order)
{
    prc_data *pd;
    prc_tess_3d_compressed *t;
    uint32_t tri, c, d, k, num_decoded, reversed = 0, wrong_normal = 0;

    pd = (prc_data *)prc_api_open_contents(ctx, TEST_PRC_FILENAME);
    if (pd == NULL)
        prc_print_error_stack(ctx);
    PRC_ASSERT_NOT_NULL(pd);
    PRC_ASSERT_EQ(pd->file_struct[0].tessellation->tess_count, 1);
    t = pd->file_struct[0].tessellation->tess[0].tess_3d_compressed;
    PRC_ASSERT_NOT_NULL(t);
    num_decoded = t->triangle_face_array_size;
    PRC_ASSERT_EQ(num_decoded, m->num_triangles);
    if (m->normals != NULL)
        PRC_ASSERT_EQ(t->must_recalculate_normals, 0);

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
            if (check_order &&
                ((matched[1] - matched[0] + 3) % 3 != 1 || (matched[2] - matched[1] + 3) % 3 != 1))
                reversed++;
            if (m->normals != NULL)
            {
                for (c = 0; c < 3; c++)
                {
                    const double *want = &m->normals[(size_t)m->norm_indices[tri * 3 + c] * 3];
                    uint32_t ni = t->normal_indices_prc_compressed_3d[d * 3 + (uint32_t)matched[c]];
                    const double *got = &t->normals_prc_compressed_3d[(size_t)ni * 3];
                    double len = sqrt(want[0] * want[0] + want[1] * want[1] + want[2] * want[2]);

                    /* within 1 degree: the angles are stored in 10 bits */
                    if ((want[0] * got[0] + want[1] * got[1] + want[2] * got[2]) / len < cos(1.0 * 3.14159265358979 / 180.0))
                        wrong_normal++;
                }
            }
        }
        PRC_ASSERT(found);
    }
    if (reversed != 0)
        printf("    %u of %u triangles came back reversed\n", reversed, m->num_triangles);
    PRC_ASSERT_EQ(reversed, 0);
    if (wrong_normal != 0)
        printf("    %u of %u corner normals came back wrong\n", wrong_normal, m->num_triangles * 3);
    PRC_ASSERT_EQ(wrong_normal, 0);

    prc_api_release_data(ctx, (prc_api_data)pd, NULL, 0, NULL, 0, NULL, 0, NULL);
}

static void
check_winding(prc_context *ctx, const test_mesh *m)
{
    check_decoded(ctx, m, 1);
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
    double pos[36 * 3], normals[36 * 3];
    uint32_t tris[50 * 3];
    uint32_t x, y, n = 0;
    test_mesh m;

    printf("  sub-case: 6x6 curved grid, normals recalculated\n");
    for (y = 0; y < 6; y++)
        for (x = 0; x < 6; x++)
        {
            double s = cos((double)(x + 2 * y));
            double nl = sqrt(0.04 * s * s + 0.16 * s * s + 1.0);

            pos[(y * 6 + x) * 3 + 0] = (double)x;
            pos[(y * 6 + x) * 3 + 1] = (double)y;
            pos[(y * 6 + x) * 3 + 2] = 0.2 * sin((double)(x + 2 * y));
            /* the surface's own normal: (-dz/dx, -dz/dy, 1), unitised */
            normals[(y * 6 + x) * 3 + 0] = -0.2 * s / nl;
            normals[(y * 6 + x) * 3 + 1] = -0.4 * s / nl;
            normals[(y * 6 + x) * 3 + 2] = 1.0 / nl;
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

    printf("  sub-case: the same grid, smooth normals supplied\n");
    m.normals = normals; m.num_normals = 36; m.norm_indices = tris;
    PRC_ASSERT_EQ(write_mesh(ctx, &m), 0);
    check_winding(ctx, &m);
}

/* The flat strip again, with every corner's supplied normal lying almost in
   the strip's plane, swept from just below it to just above. Here the
   quantised normal the decoder rebuilds can fall on the other side of the
   plane from the supplied one, so the bit derived from it differs. The
   normals must still be stored and come back; the winding is whichever the
   decoded normal gives, so it is not checked. */
static void
test_edge_on_normals(prc_context *ctx)
{
    double pos[18 * 3], normals[3];
    uint32_t tris[16 * 3], nidx[16 * 3];
    uint32_t i, n = 0, stored = 0, tried = 0;
    int step;
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
    for (i = 0; i < n * 3; i++)
        nidx[i] = 0;
    memset(&m, 0, sizeof(m));
    m.positions = pos; m.num_positions = 18;
    m.tris = tris; m.num_triangles = n;
    m.normals = normals; m.num_normals = 1; m.norm_indices = nidx;

    printf("  sub-case: flat strip, normals within 0.1 degree of its plane\n");
    for (step = -10; step <= 10; step++)
    {
        double e = (double)step * 0.01 * 3.14159265358979 / 180.0;
        prc_data *pd;
        prc_tess_3d_compressed *t;
        int is_stored;

        normals[0] = cos(e) * 0.6;
        normals[1] = cos(e) * 0.8;
        normals[2] = sin(e);
        PRC_ASSERT_EQ(write_mesh(ctx, &m), 0);
        pd = (prc_data *)prc_api_open_contents(ctx, TEST_PRC_FILENAME);
        PRC_ASSERT_NOT_NULL(pd);
        t = pd->file_struct[0].tessellation->tess[0].tess_3d_compressed;
        PRC_ASSERT_NOT_NULL(t);
        is_stored = !t->must_recalculate_normals;
        prc_api_release_data(ctx, (prc_api_data)pd, NULL, 0, NULL, 0, NULL, 0, NULL);
        tried++;
        if (is_stored)
        {
            stored++;
            check_decoded(ctx, &m, 0);
        }
        else
            printf("    elevation %+.2f degrees: normals not stored\n", (double)step * 0.01);
    }
    PRC_ASSERT_EQ(stored, tried);
}

/* A closed torus, 64 x 32 quads, 4,096 triangles, written as one chain:
   most triangles sit hundreds or thousands of steps from its start, which
   the small meshes above never reach. Normals recalculated, then the
   surface's own smooth normals supplied. */
#define TORUS_U 64
#define TORUS_V 32

static void
test_torus(prc_context *ctx)
{
    static double pos[TORUS_U * TORUS_V * 3], normals[TORUS_U * TORUS_V * 3];
    static uint32_t tris[TORUS_U * TORUS_V * 2 * 3];
    uint32_t u, v, n = 0;
    test_mesh m;

    for (u = 0; u < TORUS_U; u++)
        for (v = 0; v < TORUS_V; v++)
        {
            double a = 2.0 * 3.14159265358979 * (double)u / TORUS_U;
            double b = 2.0 * 3.14159265358979 * (double)v / TORUS_V;
            uint32_t k = (u * TORUS_V + v) * 3;

            pos[k + 0] = (3.0 + cos(b)) * cos(a);
            pos[k + 1] = (3.0 + cos(b)) * sin(a);
            pos[k + 2] = sin(b);
            normals[k + 0] = cos(b) * cos(a);
            normals[k + 1] = cos(b) * sin(a);
            normals[k + 2] = sin(b);
        }
    for (u = 0; u < TORUS_U; u++)
        for (v = 0; v < TORUS_V; v++)
        {
            /* counter-clockwise seen from outside */
            uint32_t a = u * TORUS_V + v;
            uint32_t b = ((u + 1) % TORUS_U) * TORUS_V + v;
            uint32_t c = ((u + 1) % TORUS_U) * TORUS_V + (v + 1) % TORUS_V;
            uint32_t d = u * TORUS_V + (v + 1) % TORUS_V;

            tris[n * 3 + 0] = a; tris[n * 3 + 1] = b; tris[n * 3 + 2] = c; n++;
            tris[n * 3 + 0] = a; tris[n * 3 + 1] = c; tris[n * 3 + 2] = d; n++;
        }
    memset(&m, 0, sizeof(m));
    m.positions = pos; m.num_positions = TORUS_U * TORUS_V;
    m.tris = tris; m.num_triangles = n;

    printf("  sub-case: torus, 4096 triangles, normals recalculated\n");
    PRC_ASSERT_EQ(write_mesh(ctx, &m), 0);
    check_winding(ctx, &m);

    printf("  sub-case: the same torus, smooth normals supplied\n");
    m.normals = normals; m.num_normals = TORUS_U * TORUS_V; m.norm_indices = tris;
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
    test_edge_on_normals(ctx);
    test_torus(ctx);

    prc_release_context(ctx);
    remove(TEST_PRC_FILENAME);

    PRC_TEST_END;
}
