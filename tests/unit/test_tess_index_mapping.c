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

/* How a part finds its tessellation in the API's numbering.

   The API numbers tessellations in its own way: entries are made file
   tessellation by file tessellation, one for each part that draws it. Where
   parts share a file tessellation, the API's numbering and the file's
   (biased_tess_index - 1) drift apart from the first shared one on.

   The file written here has four leaves over two file tessellations:

     red     file tessellation 0
     blue    file tessellation 0
     red2    file tessellation 0
     big     file tessellation 1

   giving four API entries, three of the small triangle and then the big one.
   "big" is the leaf the file numbering gets wrong: its file index is 1, which
   in the API's numbering is one of the small triangle's entries. The big
   triangle is five times larger, so drawing the wrong entry is visible.

   Each part must carry its own API entry in part_detail_index (it used to read
   0 for every part), and part->tess must point at that entry. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "prc_test.h"
#include "prc_context.h"
#include "prc_api.h"

#define TEST_MAPPING_FILENAME "test_tess_index_mapping_tmp.prc"

static const double small_positions[3 * 3] = {
    0.0, 0.0, 0.0,
    1.0, 0.0, 0.0,
    0.0, 1.0, 0.0
};
static const double big_positions[3 * 3] = {
    0.0, 0.0, 0.0,
    5.0, 0.0, 0.0,
    0.0, 5.0, 0.0
};
static const uint32_t tri_indices[3] = { 0, 1, 2 };
static const uint32_t tri_face_counts[1] = { 1 };

static void
fill_tess(prc_api_write_tessellation *t, const double *positions)
{
    memset(t, 0, sizeof(*t));
    t->kind = PRC_API_WRITE_TESS_KIND_TRIANGLES;
    t->positions = positions;
    t->num_positions = 3;
    t->tri_indices = tri_indices;
    t->num_triangles = 1;
    t->face_tri_counts = tri_face_counts;
    t->num_faces = 1;
    t->must_calculate_normals = 1;
}

static void
fill_item(prc_api_write_rep_item *ri, uint32_t biased_tess, double r, double g, double b)
{
    memset(ri, 0, sizeof(*ri));
    ri->kind = PRC_API_WRITE_RI_SURFACE;
    ri->biased_tessellation_index = biased_tess;
    ri->has_material = 1;
    ri->material_color[0] = r;
    ri->material_color[1] = g;
    ri->material_color[2] = b;
    ri->material_alpha = 1.0;
}

static void
fill_leaf(prc_api_write_node *n, prc_api_write_rep_item *ri, const char *name)
{
    memset(n, 0, sizeof(*n));
    n->rep_items = ri;
    n->num_rep_items = 1;
    n->name = name;
    n->bbox_max[0] = n->bbox_max[1] = 5.0;
}

/* The part, or representation item, of a leaf that carries a tessellation.
   The tree here is two levels deep, so a direct search is enough. */
static const prc_api_part *
find_drawn_part(const prc_api_product *node, const char *name)
{
    size_t k;

    if (node->name != NULL && strcmp(node->name, name) == 0 && node->part != NULL)
    {
        if (node->part->biased_tess_index > 0)
            return node->part;
        for (k = 0; k < node->part->num_rep_items; k++)
            if (node->part->rep_items[k].biased_tess_index > 0)
                return &node->part->rep_items[k];
    }
    for (k = 0; k < node->num_children; k++)
    {
        const prc_api_part *p = find_drawn_part(&node->children[k], name);
        if (p != NULL)
            return p;
    }
    return NULL;
}

/* Largest x of any vertex an entry draws: 1 for the small triangle, 5 for
   the big one. */
static double
max_x(const prc_api_tess *t)
{
    double m = -1.0;
    uint32_t f;
    size_t v;

    for (f = 0; f < t->num_faces; f++)
    {
        const prc_api_tess_vertex_buffer *buf = &t->tess_faces[f].face_vertices;

        for (v = 0; v < buf->num_vertices; v++)
            if (buf->vertices[v].position[0] > m)
                m = buf->vertices[v].position[0];
    }
    return m;
}

int
main(void)
{
    prc_context *ctx;
    prc_api_write_tessellation tess_w[2];
    prc_api_write_rep_item items[4];
    prc_api_write_node leaves[4], root;
    prc_api_write_node *kids[4];
    uint8_t *buf = NULL;
    size_t buf_size = 0;
    FILE *fid;
    prc_api_data data;
    prc_api_product *tree = NULL;
    prc_api_tess *tesses = NULL;
    uint32_t num_parts = 0, num_products = 0, num_markups = 0;
    uint32_t num_tess = 0, num_line = 0, num_exact = 0, k;
    const prc_api_part *red, *blue, *red2, *big;

    PRC_TEST_BEGIN("tessellation index mapping");

    ctx = prc_api_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    fill_tess(&tess_w[0], small_positions);
    fill_tess(&tess_w[1], big_positions);
    fill_item(&items[0], 1, 1.0, 0.0, 0.0);
    fill_item(&items[1], 1, 0.0, 0.0, 1.0);
    fill_item(&items[2], 1, 1.0, 0.0, 0.0);
    fill_item(&items[3], 2, 0.0, 1.0, 0.0);
    fill_leaf(&leaves[0], &items[0], "red");
    fill_leaf(&leaves[1], &items[1], "blue");
    fill_leaf(&leaves[2], &items[2], "red2");
    fill_leaf(&leaves[3], &items[3], "big");
    for (k = 0; k < 4; k++)
        kids[k] = &leaves[k];
    memset(&root, 0, sizeof(root));
    root.name = "root";
    root.children = kids;
    root.num_children = 4;
    root.bbox_max[0] = root.bbox_max[1] = 5.0;

    PRC_ASSERT_EQ(prc_api_write_prc_buffer(ctx, "mapping_model", &root, tess_w, 2, &buf, &buf_size), 0);
    fid = fopen(TEST_MAPPING_FILENAME, "wb");
    PRC_ASSERT_NOT_NULL(fid);
    PRC_ASSERT_EQ(fwrite(buf, 1, buf_size, fid), buf_size);
    fclose(fid);
    prc_api_write_prc_buffer_free(ctx, buf);

    data = prc_api_open_contents(ctx, TEST_MAPPING_FILENAME);
    if (data == NULL)
        prc_api_print_error_stack(ctx);
    PRC_ASSERT_NOT_NULL(data);
    PRC_ASSERT_EQ(prc_api_prep_model_tree(ctx, data, &num_parts, &num_products, &num_markups), 0);
    PRC_ASSERT_EQ(prc_api_create_model_tree(ctx, data, &tree, num_parts, num_products, num_markups), 0);
    PRC_ASSERT_EQ(prc_api_get_number_tessellations(ctx, data, tree, &num_tess, &num_line, &num_exact, 0), 0);

    /* Two file tessellations, one API entry per part. */
    PRC_ASSERT_EQ(num_tess, 4u);

    red = find_drawn_part(tree, "red");
    blue = find_drawn_part(tree, "blue");
    red2 = find_drawn_part(tree, "red2");
    big = find_drawn_part(tree, "big");
    PRC_ASSERT(red != NULL && blue != NULL && red2 != NULL && big != NULL);

    /* The file's numbering: three share file tessellation 0. */
    PRC_ASSERT_EQ(red->biased_tess_index, 1u);
    PRC_ASSERT_EQ(blue->biased_tess_index, 1u);
    PRC_ASSERT_EQ(red2->biased_tess_index, 1u);
    PRC_ASSERT_EQ(big->biased_tess_index, 2u);

    /* The API's numbering: four distinct entries, in range. */
    PRC_ASSERT(red->part_detail_index < num_tess && blue->part_detail_index < num_tess);
    PRC_ASSERT(red2->part_detail_index < num_tess && big->part_detail_index < num_tess);
    PRC_ASSERT(red->part_detail_index != blue->part_detail_index);
    PRC_ASSERT(red->part_detail_index != red2->part_detail_index);
    PRC_ASSERT(blue->part_detail_index != red2->part_detail_index);
    PRC_ASSERT(big->part_detail_index != red->part_detail_index);
    PRC_ASSERT(big->part_detail_index != blue->part_detail_index);
    PRC_ASSERT(big->part_detail_index != red2->part_detail_index);
    /* The case that used to go wrong: big's file index is not its entry. */
    PRC_ASSERT(big->part_detail_index != big->biased_tess_index - 1);

    tesses = (prc_api_tess *)calloc(num_tess, sizeof(prc_api_tess));
    PRC_ASSERT_NOT_NULL(tesses);
    for (k = 0; k < num_tess; k++)
    {
        uint8_t has_lines = 0;
        uint32_t f, nf = prc_api_get_number_faces(ctx, data, k);

        tesses[k].num_faces = nf;
        tesses[k].tess_faces = (prc_api_face *)calloc(nf, sizeof(prc_api_face));
        PRC_ASSERT_NOT_NULL(tesses[k].tess_faces);
        PRC_ASSERT_EQ(prc_api_initialize_tessellation(ctx, data, tree, k, &tesses[k], NULL, &has_lines), 0);
        for (f = 0; f < nf; f++)
            PRC_ASSERT_EQ(prc_api_get_tessellation_vertices(ctx, data, k, f, &tesses[k].tess_faces[f], &tesses[k]), 0);
    }

    /* Every part points at the entry it draws. */
    PRC_ASSERT(red->tess == &tesses[red->part_detail_index]);
    PRC_ASSERT(blue->tess == &tesses[blue->part_detail_index]);
    PRC_ASSERT(red2->tess == &tesses[red2->part_detail_index]);
    PRC_ASSERT(big->tess == &tesses[big->part_detail_index]);

    /* And the entries hold the right geometry. */
    PRC_ASSERT_NEAR(max_x(&tesses[red->part_detail_index]), 1.0, 1e-4);
    PRC_ASSERT_NEAR(max_x(&tesses[blue->part_detail_index]), 1.0, 1e-4);
    PRC_ASSERT_NEAR(max_x(&tesses[red2->part_detail_index]), 1.0, 1e-4);
    PRC_ASSERT_NEAR(max_x(&tesses[big->part_detail_index]), 5.0, 1e-4);
    /* The file numbering would have drawn a small triangle for big. */
    PRC_ASSERT_NEAR(max_x(&tesses[big->biased_tess_index - 1]), 1.0, 1e-4);

    prc_api_release_data(ctx, data, tesses, num_tess, NULL, 0, NULL, 0, tree);
    prc_api_release_context(ctx);
    remove(TEST_MAPPING_FILENAME);

    PRC_TEST_END;
}
