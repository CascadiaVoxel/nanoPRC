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

/* A triangle thinner than the tolerance is kept.

   The compressed encoder used to merge every pair of vertices that fell in
   the same tolerance-sized cell, and then drop each triangle left with two
   corners on one vertex: a scan lost its slivers, without a message, and
   the triangles around them moved onto the merged vertex. It now merges
   only vertices at exactly the same position.

   The mesh is a unit square in z = 0, fanned around two inner vertices,
   4 and 5, a tenth of the tolerance (1e-4) apart and in the same cell. Two
   of its six triangles use the short edge between them.

     written compressed, all six triangles come back, every corner within
     the tolerance of a vertex written
     prc_api_mesh_weld_and_split, which merges within the tolerance, still
     merges 4 and 5: five positions and four triangles */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "prc_test.h"
#include "prc_context.h"
#include "prc_api.h"

#define TEST_SLIVER_FILENAME "test_sliver_preservation_tmp.prc"
#define TEST_SLIVER_TOL 1e-4

/* 4 and 5 sit either side of x = 5000 * tol, the centre of one cell */
static const double sliver_positions[6 * 3] = {
    0.0, 0.0, 0.0,
    1.0, 0.0, 0.0,
    1.0, 1.0, 0.0,
    0.0, 1.0, 0.0,
    0.49999, 0.5, 0.0,
    0.50000, 0.5, 0.0
};
static const uint32_t sliver_indices[6 * 3] = {
    0, 1, 4,   1, 5, 4,   1, 2, 5,   2, 3, 5,   3, 4, 5,   3, 0, 4
};
static const uint32_t sliver_face_counts[1] = { 6 };

static double
nearest_written(const float *p)
{
    uint32_t k;
    double best = 1e300;

    for (k = 0; k < 6; k++)
    {
        double dx = p[0] - sliver_positions[k * 3 + 0];
        double dy = p[1] - sliver_positions[k * 3 + 1];
        double dz = p[2] - sliver_positions[k * 3 + 2];
        double d = sqrt(dx * dx + dy * dy + dz * dz);
        if (d < best)
            best = d;
    }
    return best;
}

static void
check_weld_and_split(prc_context *ctx)
{
    double *pos = NULL;
    uint32_t *idx = NULL;
    uint32_t np = 0, nt = 0;

    printf("  sub-case: prc_api_mesh_weld_and_split still merges within the tolerance\n");
    PRC_ASSERT_EQ(prc_api_mesh_weld_and_split(ctx, sliver_positions, 6, sliver_indices, 6,
        prc_write_tol_absolute(TEST_SLIVER_TOL), &pos, &np, &idx, &nt), 0);
    printf("    %u positions, %u triangles\n", np, nt);
    PRC_ASSERT_EQ(np, 5u);
    PRC_ASSERT_EQ(nt, 4u);
    prc_api_mesh_weld_and_split_free(ctx, pos, idx);
}

int
main(void)
{
    prc_context *ctx;
    prc_api_write_tessellation tess_w;
    prc_api_write_rep_item item;
    prc_api_write_node leaf, root;
    prc_api_write_node *kids[1];
    uint8_t *buf = NULL;
    size_t buf_size = 0;
    FILE *fid;
    prc_api_data data;
    prc_api_product *tree = NULL;
    prc_api_tess *tesses = NULL;
    prc_api_tess *t;
    prc_api_graphic_primitive prim;
    const prc_api_tess_vertex_buffer *vb;
    uint32_t num_parts = 0, num_products = 0, num_markups = 0;
    uint32_t num_tess = 0, num_line = 0, num_exact = 0, nf, k;
    uint8_t has_lines = 0;
    uint32_t num_triangles_read = 0;
    double worst = 0.0;
    size_t p;

    PRC_TEST_BEGIN("test_sliver_preservation");

    /* The two inner vertices are in the same cell, a tenth of the
       tolerance apart, so merging within the tolerance would join them */
    PRC_ASSERT(llround(sliver_positions[4 * 3] / TEST_SLIVER_TOL) == llround(sliver_positions[5 * 3] / TEST_SLIVER_TOL));
    PRC_ASSERT_NEAR(sliver_positions[5 * 3] - sliver_positions[4 * 3], 0.1 * TEST_SLIVER_TOL, 1e-12);

    ctx = prc_api_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    memset(&tess_w, 0, sizeof(tess_w));
    tess_w.kind = PRC_API_WRITE_TESS_KIND_COMPRESSED;
    tess_w.positions = sliver_positions;
    tess_w.num_positions = 6;
    tess_w.tri_indices = sliver_indices;
    tess_w.num_triangles = 6;
    tess_w.face_tri_counts = sliver_face_counts;
    tess_w.num_faces = 1;
    tess_w.tolerance = prc_write_tol_absolute(TEST_SLIVER_TOL);

    memset(&item, 0, sizeof(item));
    item.kind = PRC_API_WRITE_RI_SURFACE;
    item.biased_tessellation_index = 1;
    memset(&leaf, 0, sizeof(leaf));
    leaf.rep_items = &item;
    leaf.num_rep_items = 1;
    leaf.name = "square";
    leaf.bbox_max[0] = leaf.bbox_max[1] = 1.0;
    kids[0] = &leaf;
    memset(&root, 0, sizeof(root));
    root.name = "root";
    root.children = kids;
    root.num_children = 1;
    root.bbox_max[0] = root.bbox_max[1] = 1.0;

    PRC_ASSERT_EQ(prc_api_write_prc_buffer(ctx, "sliver", &root, &tess_w, 1, &buf, &buf_size), 0);
    fid = fopen(TEST_SLIVER_FILENAME, "wb");
    PRC_ASSERT_NOT_NULL(fid);
    PRC_ASSERT_EQ(fwrite(buf, 1, buf_size, fid), buf_size);
    fclose(fid);
    prc_api_write_prc_buffer_free(ctx, buf);

    data = prc_api_open_contents(ctx, TEST_SLIVER_FILENAME);
    if (data == NULL)
        prc_api_print_error_stack(ctx);
    PRC_ASSERT_NOT_NULL(data);
    PRC_ASSERT_EQ(prc_api_prep_model_tree(ctx, data, &num_parts, &num_products, &num_markups), 0);
    PRC_ASSERT_EQ(prc_api_create_model_tree(ctx, data, &tree, num_parts, num_products, num_markups), 0);
    PRC_ASSERT_EQ(prc_api_get_number_tessellations(ctx, data, tree, &num_tess, &num_line, &num_exact, 0), 0);
    PRC_ASSERT_EQ(num_tess, 1u);

    tesses = (prc_api_tess *)calloc(1, sizeof(prc_api_tess));
    PRC_ASSERT_NOT_NULL(tesses);
    t = &tesses[0];
    nf = prc_api_get_number_faces(ctx, data, 0);
    PRC_ASSERT(nf >= 1);
    t->num_faces = nf;
    t->tess_faces = (prc_api_face *)calloc(nf, sizeof(prc_api_face));
    PRC_ASSERT_NOT_NULL(t->tess_faces);
    PRC_ASSERT_EQ(prc_api_initialize_tessellation(ctx, data, tree, 0, t, NULL, &has_lines), 0);
    PRC_ASSERT_EQ(t->type, PRC_API_TESS_3D_Compressed);
    PRC_ASSERT_EQ(prc_api_get_tessellation_vertices(ctx, data, 0, 0, &t->tess_faces[0], t), 0);
    vb = &t->tess_vertices;

    /* A compressed tessellation's primitives are the whole mesh, whatever
       the face */
    for (p = 0; p < t->tess_faces[0].num_graphic_primitives; p++)
    {
        PRC_ASSERT_EQ(prc_api_get_graphics_primitive(ctx, data, t, 0, p, &prim), 0);
        PRC_ASSERT_EQ((uint32_t)(prim.num_indices % 3), 0u);
        num_triangles_read += (uint32_t)(prim.num_indices / 3);
        for (k = 0; k < (uint32_t)prim.num_indices; k++)
        {
            double d;
            PRC_ASSERT(prim.indices[k] < vb->num_vertices);
            d = nearest_written(vb->vertices[prim.indices[k]].position);
            if (d > worst)
                worst = d;
        }
    }
    printf("  sub-case: written compressed, %u of 6 triangles read, worst corner %.3g away\n",
        num_triangles_read, worst);
    PRC_ASSERT_EQ(num_triangles_read, 6u);
    PRC_ASSERT(worst < TEST_SLIVER_TOL);

    prc_api_release_data(ctx, data, tesses, num_tess, NULL, 0, NULL, 0, tree);
    free(t->tess_faces);
    free(tesses);

    check_weld_and_split(ctx);

    PRC_ASSERT_EQ(prc_api_release_context(ctx), 0);
    remove(TEST_SLIVER_FILENAME);

    PRC_TEST_END;
}
