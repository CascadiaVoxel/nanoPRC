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

/* A triangle beyond the first two on an edge keeps that edge's position.

   The compressed encoder gives every triangle after the first two on an
   edge its own copies of the edge's two vertices. It used to move those
   copies 50 times the tolerance towards the triangle's third vertex, which
   opened a crack along the edge. The copies now stay where the edge is.

   The mesh written is a fin: three triangles on the edge (0,1), one in
   +y, one in -y and one in +z. With a tolerance of 1e-4, the file read
   back must have:

     all three triangles
     every vertex within the tolerance of a vertex written (the copies
     used to be 5e-3 away)
     the +z triangle's two corners on the edge at the same positions as
     the edge's corners in the other triangles */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "prc_test.h"
#include "prc_context.h"
#include "prc_api.h"

#define TEST_NMCOPIES_FILENAME "test_nonmanifold_copies_in_place_tmp.prc"
#define TEST_NMCOPIES_TOL 1e-4

static const double fin_positions[5 * 3] = {
    0.0, 0.0, 0.0,
    1.0, 0.0, 0.0,
    0.5, 1.0, 0.0,
    0.5, -1.0, 0.0,
    0.5, 0.0, 1.0
};
static const uint32_t fin_indices[3 * 3] = { 0, 1, 2,   1, 0, 3,   0, 1, 4 };
static const uint32_t fin_face_counts[1] = { 3 };

static double
distance(const float *a, const double *b)
{
    double dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return sqrt(dx * dx + dy * dy + dz * dz);
}

static double
distance_ff(const float *a, const float *b)
{
    double dx = (double)a[0] - b[0], dy = (double)a[1] - b[1], dz = (double)a[2] - b[2];
    return sqrt(dx * dx + dy * dy + dz * dz);
}

/* The vertex written nearest to p, and its distance */
static uint32_t
nearest_written(const float *p, double *dist_out)
{
    uint32_t k, best = 0;
    double best_d = 1e300;

    for (k = 0; k < 5; k++)
    {
        double d = distance(p, &fin_positions[k * 3]);
        if (d < best_d)
        {
            best_d = d;
            best = k;
        }
    }
    *dist_out = best_d;
    return best;
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
    uint32_t num_tess = 0, num_line = 0, num_exact = 0, nf, k, c;
    uint8_t has_lines = 0;
    /* For each written vertex, the decoded position of its first corner */
    const float *first_seen[5] = { NULL, NULL, NULL, NULL, NULL };
    uint32_t num_triangles_read = 0;
    size_t p;

    PRC_TEST_BEGIN("test_nonmanifold_copies_in_place");

    ctx = prc_api_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    memset(&tess_w, 0, sizeof(tess_w));
    tess_w.kind = PRC_API_WRITE_TESS_KIND_COMPRESSED;
    tess_w.positions = fin_positions;
    tess_w.num_positions = 5;
    tess_w.tri_indices = fin_indices;
    tess_w.num_triangles = 3;
    tess_w.face_tri_counts = fin_face_counts;
    tess_w.num_faces = 1;
    tess_w.tolerance = prc_write_tol_absolute(TEST_NMCOPIES_TOL);

    memset(&item, 0, sizeof(item));
    item.kind = PRC_API_WRITE_RI_SURFACE;
    item.biased_tessellation_index = 1;
    memset(&leaf, 0, sizeof(leaf));
    leaf.rep_items = &item;
    leaf.num_rep_items = 1;
    leaf.name = "fin";
    leaf.bbox_min[1] = -1.0;
    leaf.bbox_max[0] = leaf.bbox_max[1] = leaf.bbox_max[2] = 1.0;
    kids[0] = &leaf;
    memset(&root, 0, sizeof(root));
    root.name = "root";
    root.children = kids;
    root.num_children = 1;
    root.bbox_min[1] = -1.0;
    root.bbox_max[0] = root.bbox_max[1] = root.bbox_max[2] = 1.0;

    PRC_ASSERT_EQ(prc_api_write_prc_buffer(ctx, "nonmanifold_copies", &root, &tess_w, 1, &buf, &buf_size), 0);
    fid = fopen(TEST_NMCOPIES_FILENAME, "wb");
    PRC_ASSERT_NOT_NULL(fid);
    PRC_ASSERT_EQ(fwrite(buf, 1, buf_size, fid), buf_size);
    fclose(fid);
    prc_api_write_prc_buffer_free(ctx, buf);

    data = prc_api_open_contents(ctx, TEST_NMCOPIES_FILENAME);
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
    PRC_ASSERT(vb->num_vertices > 0);

    /* A compressed tessellation's primitives are the whole mesh, whatever
       the face */
    for (p = 0; p < t->tess_faces[0].num_graphic_primitives; p++)
    {
        PRC_ASSERT_EQ(prc_api_get_graphics_primitive(ctx, data, t, 0, p, &prim), 0);
        PRC_ASSERT_EQ((uint32_t)(prim.num_indices % 3), 0u);
        for (k = 0; k < (uint32_t)prim.num_indices; k += 3)
        {
            num_triangles_read++;
            for (c = 0; c < 3; c++)
            {
                const float *pos;
                double d;
                uint32_t w;

                PRC_ASSERT(prim.indices[k + c] < vb->num_vertices);
                pos = vb->vertices[prim.indices[k + c]].position;
                w = nearest_written(pos, &d);
                printf("  corner at (%g, %g, %g): written vertex %u, %.3g away\n",
                    pos[0], pos[1], pos[2], w, d);
                PRC_ASSERT(d < TEST_NMCOPIES_TOL);
                if (first_seen[w] == NULL)
                    first_seen[w] = pos;
                else
                    PRC_ASSERT(distance_ff(pos, first_seen[w]) < 1e-9);
            }
        }
    }
    printf("  sub-case: all three triangles read, the edge closed\n");
    PRC_ASSERT_EQ(num_triangles_read, 3u);
    for (k = 0; k < 5; k++)
        PRC_ASSERT_NOT_NULL(first_seen[k]);

    prc_api_release_data(ctx, data, tesses, num_tess, NULL, 0, NULL, 0, tree);
    free(t->tess_faces);
    free(tesses);
    PRC_ASSERT_EQ(prc_api_release_context(ctx), 0);
    remove(TEST_NMCOPIES_FILENAME);

    PRC_TEST_END;
}
