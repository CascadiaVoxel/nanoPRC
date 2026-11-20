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

/* A wire tessellation with no points reads as an empty wire.

   Some files in a public corpus hold 3D wire tessellations
   (PRC_TYPE_TESS_3D_Wire) with no coordinates. Reading one used to fail
   with "No wire vertices available", and the demos then gave up on the
   whole file: the viewer drew nothing of an assembly of 2544
   tessellations because of one empty wire. Such a wire has nothing to
   draw; it now reads with no vertices and no line primitives, as an empty
   markup tessellation already does.

   The writer does not write a wire with no points, so the test writes a
   triangle and a three-point wire, then empties the wire as parsed (no
   coordinates, no wire indexes) before reading:

     the wire reads, with no vertices, no line primitives and no box
     the triangle still reads, with its three vertices */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "prc_test.h"
#include "prc_context.h"
#include "prc_data.h"
#include "prc_api.h"

#define TEST_EMPTY_WIRE_FILENAME "test_empty_wire_tessellation_tmp.prc"

static const double tri_positions[9] = { 0, 0, 0,  1, 0, 0,  0, 1, 0 };
static const uint32_t tri_indices[3] = { 0, 1, 2 };
static const uint32_t tri_face_counts[1] = { 1 };
static const float wire_positions[9] = { 0, 0, 1,  1, 0, 1,  1, 1, 1 };

static void
fill_leaf(prc_api_write_node *n, prc_api_write_rep_item *ri, int kind, uint32_t biased_tess, const char *name)
{
    memset(ri, 0, sizeof(*ri));
    ri->kind = kind;
    ri->biased_tessellation_index = biased_tess;
    memset(n, 0, sizeof(*n));
    n->rep_items = ri;
    n->num_rep_items = 1;
    n->name = name;
    n->bbox_max[0] = n->bbox_max[1] = n->bbox_max[2] = 1.0;
}

static void
write_file(prc_context *ctx)
{
    prc_api_write_tessellation tess_w[2];
    prc_api_write_wire_element wire;
    prc_api_write_rep_item items[2];
    prc_api_write_node leaves[2], root;
    prc_api_write_node *kids[2];
    uint8_t *buf = NULL;
    size_t buf_size = 0;
    FILE *fid;

    memset(&tess_w[0], 0, sizeof(tess_w[0]));
    tess_w[0].kind = PRC_API_WRITE_TESS_KIND_TRIANGLES;
    tess_w[0].positions = tri_positions;
    tess_w[0].num_positions = 3;
    tess_w[0].tri_indices = tri_indices;
    tess_w[0].num_triangles = 1;
    tess_w[0].face_tri_counts = tri_face_counts;
    tess_w[0].num_faces = 1;
    tess_w[0].must_calculate_normals = 1;

    memset(&wire, 0, sizeof(wire));
    wire.positions = wire_positions;
    wire.num_vertices = 3;
    memset(&tess_w[1], 0, sizeof(tess_w[1]));
    tess_w[1].kind = PRC_API_WRITE_TESS_KIND_WIRE;
    tess_w[1].wire_elements = &wire;
    tess_w[1].num_wire_elements = 1;

    fill_leaf(&leaves[0], &items[0], PRC_API_WRITE_RI_SURFACE, 1, "triangle");
    fill_leaf(&leaves[1], &items[1], PRC_API_WRITE_RI_WIRE, 2, "wire");
    kids[0] = &leaves[0];
    kids[1] = &leaves[1];
    memset(&root, 0, sizeof(root));
    root.name = "root";
    root.children = kids;
    root.num_children = 2;
    root.bbox_max[0] = root.bbox_max[1] = root.bbox_max[2] = 1.0;

    PRC_ASSERT_EQ(prc_api_write_prc_buffer(ctx, "empty_wire", &root, tess_w, 2, &buf, &buf_size), 0);
    fid = fopen(TEST_EMPTY_WIRE_FILENAME, "wb");
    PRC_ASSERT_NOT_NULL(fid);
    PRC_ASSERT_EQ(fwrite(buf, 1, buf_size, fid), buf_size);
    fclose(fid);
    prc_api_write_prc_buffer_free(ctx, buf);
}

int
main(void)
{
    prc_context *ctx;
    prc_api_data data;
    prc_data *pd;
    prc_tess_3d_wire *parsed_wire = NULL;
    prc_unsigned_int saved_coordinates = 0, saved_indexes = 0;
    uint32_t saved_elements = 0;
    prc_api_product *tree = NULL;
    prc_api_tess *tesses = NULL;
    uint32_t num_parts = 0, num_products = 0, num_markups = 0;
    uint32_t num_tess = 0, num_line = 0, num_exact = 0, k;
    int seen_wire = 0, seen_triangle = 0;

    PRC_TEST_BEGIN("test_empty_wire_tessellation");

    ctx = prc_api_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);
    write_file(ctx);

    data = prc_api_open_contents(ctx, TEST_EMPTY_WIRE_FILENAME);
    if (data == NULL)
        prc_api_print_error_stack(ctx);
    PRC_ASSERT_NOT_NULL(data);

    /* Empty the wire as parsed, as in the corpus files */
    pd = (prc_data *)data;
    for (k = 0; k < pd->file_struct[0].tessellation->tess_count; k++)
        if (pd->file_struct[0].tessellation->tess[k].tess_type == PRC_TYPE_TESS_3D_Wire)
            parsed_wire = pd->file_struct[0].tessellation->tess[k].tess_3d_wire;
    PRC_ASSERT_NOT_NULL(parsed_wire);
    PRC_ASSERT_EQ(parsed_wire->tessellation_coordinates.number_of_coordinates, 9u);
    saved_coordinates = parsed_wire->tessellation_coordinates.number_of_coordinates;
    saved_indexes = parsed_wire->number_of_wire_indexes;
    saved_elements = parsed_wire->number_of_wire_elements;
    parsed_wire->tessellation_coordinates.number_of_coordinates = 0;
    parsed_wire->number_of_wire_indexes = 0;
    parsed_wire->number_of_wire_elements = 0;

    PRC_ASSERT_EQ(prc_api_prep_model_tree(ctx, data, &num_parts, &num_products, &num_markups), 0);
    PRC_ASSERT_EQ(prc_api_create_model_tree(ctx, data, &tree, num_parts, num_products, num_markups), 0);
    PRC_ASSERT_EQ(prc_api_get_number_tessellations(ctx, data, tree, &num_tess, &num_line, &num_exact, 0), 0);
    PRC_ASSERT_EQ(num_tess, 2u);

    tesses = (prc_api_tess *)calloc(num_tess, sizeof(prc_api_tess));
    PRC_ASSERT_NOT_NULL(tesses);
    for (k = 0; k < num_tess; k++)
    {
        prc_api_tess *t = &tesses[k];
        uint8_t has_lines = 0;
        uint32_t nf = prc_api_get_number_faces(ctx, data, k);
        int code;

        t->num_faces = nf;
        t->tess_faces = (prc_api_face *)calloc(nf ? nf : 1, sizeof(prc_api_face));
        PRC_ASSERT_NOT_NULL(t->tess_faces);
        PRC_ASSERT_EQ(prc_api_initialize_tessellation(ctx, data, tree, k, t, NULL, &has_lines), 0);
        if (t->type == PRC_API_TESS_3D_Wire)
        {
            printf("  sub-case: the empty wire reads, with nothing in it\n");
            code = prc_api_get_tessellation_vertices(ctx, data, k, 0, NULL, t);
            if (code < 0)
                prc_api_print_error_stack(ctx);
            PRC_ASSERT_EQ(code, 0);
            PRC_ASSERT_EQ(t->tess_vertices.num_vertices, 0u);
            PRC_ASSERT(t->tess_vertices.vertices == NULL);
            PRC_ASSERT_EQ(t->num_line_primitives, 0u);
            PRC_ASSERT_EQ(t->bound_box_initialized, 0);
            seen_wire = 1;
        }
        else
        {
            printf("  sub-case: the triangle beside it still reads\n");
            PRC_ASSERT_EQ(t->type, PRC_API_TESS_3D);
            PRC_ASSERT_EQ(nf, 1u);
            PRC_ASSERT_EQ(prc_api_get_tessellation_vertices(ctx, data, k, 0, &t->tess_faces[0], t), 0);
            PRC_ASSERT_EQ(t->tess_faces[0].face_vertices.num_vertices, 3u);
            seen_triangle = 1;
        }
    }
    PRC_ASSERT(seen_wire && seen_triangle);

    /* Put the wire back, so that releasing it frees what was parsed */
    parsed_wire->tessellation_coordinates.number_of_coordinates = saved_coordinates;
    parsed_wire->number_of_wire_indexes = saved_indexes;
    parsed_wire->number_of_wire_elements = saved_elements;

    prc_api_release_data(ctx, data, tesses, num_tess, NULL, 0, NULL, 0, tree);
    for (k = 0; k < num_tess; k++)
        free(tesses[k].tess_faces);
    free(tesses);
    PRC_ASSERT_EQ(prc_api_release_context(ctx), 0);
    remove(TEST_EMPTY_WIRE_FILENAME);

    PRC_TEST_END;
}
