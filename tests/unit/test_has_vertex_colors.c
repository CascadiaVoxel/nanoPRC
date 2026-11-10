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

/* prc_api_face.has_vertex_colors says whether the file stores a colour for
   each vertex.

   Without it a caller cannot tell: when a file stores no vertex colours the
   API still fills every vertex's color, with white or with the face's style
   colour. A file whose stored colours are all white then looks the same as a
   file with none, and a caller that guesses from the values gets it wrong.

   The file written here has four tessellations, each a single triangle:

     plain_white    plain, every corner's stored colour white
     plain_none     plain, no vertex colours
     comp_white     compressed, every corner's stored colour white
     comp_none      compressed, no vertex colours

   The flag must be 1 on the first and third and 0 on the others, although
   the vertices of the first three are all white. (comp_none's vertices are
   0, the default when a compressed face's vertices carry their own style.) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "prc_test.h"
#include "prc_context.h"
#include "prc_api.h"

#define TEST_HASVC_FILENAME "test_has_vertex_colors_tmp.prc"
#define NUM_CASES 4

static const double tri_positions[3 * 3] = {
    0.0, 0.0, 0.0,
    1.0, 0.0, 0.0,
    0.0, 1.0, 0.0
};
static const uint32_t tri_indices[3] = { 0, 1, 2 };
static const uint32_t tri_face_counts[1] = { 1 };
static const uint8_t white_bytes[3 * 3] = { 255, 255, 255,   255, 255, 255,   255, 255, 255 };

static void
fill_tess(prc_api_write_tessellation *t, int compressed, const uint8_t *colors)
{
    memset(t, 0, sizeof(*t));
    t->kind = compressed ? PRC_API_WRITE_TESS_KIND_COMPRESSED : PRC_API_WRITE_TESS_KIND_TRIANGLES;
    t->positions = tri_positions;
    t->num_positions = 3;
    t->tri_indices = tri_indices;
    t->num_triangles = 1;
    t->face_tri_counts = tri_face_counts;
    t->num_faces = 1;
    t->must_calculate_normals = compressed ? 0 : 1;
    t->vertex_colors = colors;
    t->num_vertex_colors = colors != NULL ? 3 : 0;
}

static void
fill_leaf(prc_api_write_node *n, prc_api_write_rep_item *ri, uint32_t biased_tess, const char *name)
{
    memset(ri, 0, sizeof(*ri));
    ri->kind = PRC_API_WRITE_RI_SURFACE;
    ri->biased_tessellation_index = biased_tess;
    memset(n, 0, sizeof(*n));
    n->rep_items = ri;
    n->num_rep_items = 1;
    n->name = name;
    n->bbox_max[0] = n->bbox_max[1] = 1.0;
}

/* The part, or representation item, of a leaf that carries a tessellation,
   as test_tess_index_mapping finds it. */
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

int
main(void)
{
    static const char *names[NUM_CASES] = { "plain_white", "plain_none", "comp_white", "comp_none" };
    static const int compressed[NUM_CASES] = { 0, 0, 1, 1 };
    static const int want_flag[NUM_CASES] = { 1, 0, 1, 0 };
    prc_context *ctx;
    prc_api_write_tessellation tess_w[NUM_CASES];
    prc_api_write_rep_item items[NUM_CASES];
    prc_api_write_node leaves[NUM_CASES], root;
    prc_api_write_node *kids[NUM_CASES];
    uint8_t *buf = NULL;
    size_t buf_size = 0;
    FILE *fid;
    prc_api_data data;
    prc_api_product *tree = NULL;
    prc_api_tess *tesses = NULL;
    uint32_t num_parts = 0, num_products = 0, num_markups = 0;
    uint32_t num_tess = 0, num_line = 0, num_exact = 0, k;

    PRC_TEST_BEGIN("has_vertex_colors");

    ctx = prc_api_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    for (k = 0; k < NUM_CASES; k++)
    {
        fill_tess(&tess_w[k], compressed[k], want_flag[k] ? white_bytes : NULL);
        fill_leaf(&leaves[k], &items[k], k + 1, names[k]);
        kids[k] = &leaves[k];
    }
    memset(&root, 0, sizeof(root));
    root.name = "root";
    root.children = kids;
    root.num_children = NUM_CASES;
    root.bbox_max[0] = root.bbox_max[1] = 1.0;

    PRC_ASSERT_EQ(prc_api_write_prc_buffer(ctx, "has_vertex_colors", &root, tess_w, NUM_CASES, &buf, &buf_size), 0);
    fid = fopen(TEST_HASVC_FILENAME, "wb");
    PRC_ASSERT_NOT_NULL(fid);
    PRC_ASSERT_EQ(fwrite(buf, 1, buf_size, fid), buf_size);
    fclose(fid);
    prc_api_write_prc_buffer_free(ctx, buf);

    data = prc_api_open_contents(ctx, TEST_HASVC_FILENAME);
    if (data == NULL)
        prc_api_print_error_stack(ctx);
    PRC_ASSERT_NOT_NULL(data);
    PRC_ASSERT_EQ(prc_api_prep_model_tree(ctx, data, &num_parts, &num_products, &num_markups), 0);
    PRC_ASSERT_EQ(prc_api_create_model_tree(ctx, data, &tree, num_parts, num_products, num_markups), 0);
    PRC_ASSERT_EQ(prc_api_get_number_tessellations(ctx, data, tree, &num_tess, &num_line, &num_exact, 0), 0);
    PRC_ASSERT_EQ(num_tess, (uint32_t)NUM_CASES);

    tesses = (prc_api_tess *)calloc(num_tess, sizeof(prc_api_tess));
    PRC_ASSERT_NOT_NULL(tesses);
    for (k = 0; k < num_tess; k++)
    {
        prc_api_tess *t = &tesses[k];
        uint8_t has_lines = 0;
        uint32_t nf = prc_api_get_number_faces(ctx, data, k);

        PRC_ASSERT_EQ(nf, 1u);
        t->num_faces = nf;
        t->tess_faces = (prc_api_face *)calloc(nf, sizeof(prc_api_face));
        PRC_ASSERT_NOT_NULL(t->tess_faces);
        PRC_ASSERT_EQ(prc_api_initialize_tessellation(ctx, data, tree, k, t, NULL, &has_lines), 0);
        PRC_ASSERT_EQ(prc_api_get_tessellation_vertices(ctx, data, k, 0, &t->tess_faces[0], t), 0);
    }

    for (k = 0; k < NUM_CASES; k++)
    {
        const prc_api_part *p = find_drawn_part(tree, names[k]);
        const prc_api_tess *t;
        const prc_api_tess_vertex_buffer *vb;
        size_t i;

        PRC_ASSERT_NOT_NULL(p);
        PRC_ASSERT(p->part_detail_index < num_tess);
        t = &tesses[p->part_detail_index];
        PRC_ASSERT_EQ(t->type, compressed[k] ? PRC_API_TESS_3D_Compressed : PRC_API_TESS_3D);
        printf("  %s: has_vertex_colors %d\n", names[k], t->tess_faces[0].has_vertex_colors);
        PRC_ASSERT_EQ(t->tess_faces[0].has_vertex_colors, want_flag[k]);

        /* The colours alone do not tell the first three apart: all white. */
        vb = compressed[k] ? &t->tess_vertices : &t->tess_faces[0].face_vertices;
        PRC_ASSERT(vb->num_vertices > 0);
        for (i = 0; i < vb->num_vertices && k != 3; i++)
        {
            PRC_ASSERT_NEAR(vb->vertices[i].color[0], 1.0, 1e-6);
            PRC_ASSERT_NEAR(vb->vertices[i].color[1], 1.0, 1e-6);
            PRC_ASSERT_NEAR(vb->vertices[i].color[2], 1.0, 1e-6);
        }
    }

    prc_api_release_data(ctx, data, tesses, num_tess, NULL, 0, NULL, 0, tree);
    for (k = 0; k < num_tess; k++)
        free(tesses[k].tess_faces);
    free(tesses);
    prc_api_release_context(ctx);
    remove(TEST_HASVC_FILENAME);

    PRC_TEST_END;
}
