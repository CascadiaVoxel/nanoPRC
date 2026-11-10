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

/* Vertex colour alpha read back through the public API, 0 to 1 like the
   colour.

   A plain tessellation's and a wire's vertex colours are stored as bytes,
   Table 144 "ColorData", with an alpha byte when is_rgba is set. The API
   gives the red, green and blue as 0 to 1 and must give the alpha the same
   way; it used to give the stored byte, so a half-transparent corner came
   back as 128 and every corner with an alpha byte looked opaque.

   The file written here has three tessellations, each a single triangle
   whose corners are red, green and blue:

     rgba    plain, alpha bytes 255, 160, 40
     rgb     plain, no alpha bytes: every corner opaque
     wire    a closed wire through the same three points, alpha 1, 0.6, 0.2

   Compressed tessellations decode their colours elsewhere and already gave
   0 to 1 (test_compressed_color_write). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "prc_test.h"
#include "prc_context.h"
#include "prc_api.h"

#define TEST_VALPHA_FILENAME "test_vertex_color_alpha_tmp.prc"

static const double tri_positions[3 * 3] = {
    0.0, 0.0, 0.0,
    1.0, 0.0, 0.0,
    0.0, 1.0, 0.0
};
static const float wire_positions[3 * 3] = {
    0.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f
};
static const uint32_t tri_indices[3] = { 0, 1, 2 };
static const uint32_t tri_face_counts[1] = { 1 };
static const uint8_t rgba_bytes[3 * 4] = { 255, 0, 0, 255,   0, 255, 0, 160,   0, 0, 255, 40 };
static const uint8_t rgb_bytes[3 * 3] = { 255, 0, 0,   0, 255, 0,   0, 0, 255 };
static const float wire_colors[3 * 4] = { 1, 0, 0, 1,   0, 1, 0, 0.6f,   0, 0, 1, 0.2f };

/* Corner k of the triangle is red, green or blue as k is 0, 1 or 2. */
static const float corner_rgb[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };

static void
fill_tess(prc_api_write_tessellation *t, const uint8_t *colors, int have_alpha)
{
    memset(t, 0, sizeof(*t));
    t->kind = PRC_API_WRITE_TESS_KIND_TRIANGLES;
    t->positions = tri_positions;
    t->num_positions = 3;
    t->tri_indices = tri_indices;
    t->num_triangles = 1;
    t->face_tri_counts = tri_face_counts;
    t->num_faces = 1;
    t->must_calculate_normals = 1;
    t->vertex_colors = colors;
    t->num_vertex_colors = 3;
    t->vertex_colors_have_alpha = (uint8_t)have_alpha;
}

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

/* The corner (0, 1 or 2) a vertex lies at, or -1. */
static int
corner_of(const prc_api_vertex *v)
{
    int k;

    for (k = 0; k < 3; k++)
        if (v->position[0] > tri_positions[k * 3] - 1e-4 && v->position[0] < tri_positions[k * 3] + 1e-4
            && v->position[1] > tri_positions[k * 3 + 1] - 1e-4 && v->position[1] < tri_positions[k * 3 + 1] + 1e-4)
            return k;
    return -1;
}

/* Every vertex in buf has its corner's colour and the alpha in want_alpha;
   returns which corners were seen, one bit each. */
static unsigned
check_vertices(const prc_api_tess_vertex_buffer *buf, const double want_alpha[3])
{
    unsigned seen = 0;
    size_t i;

    for (i = 0; i < buf->num_vertices; i++)
    {
        const prc_api_vertex *v = &buf->vertices[i];
        int k = corner_of(v);

        PRC_ASSERT(k >= 0);
        seen |= 1u << k;
        PRC_ASSERT_NEAR(v->color[0], corner_rgb[k][0], 1e-6);
        PRC_ASSERT_NEAR(v->color[1], corner_rgb[k][1], 1e-6);
        PRC_ASSERT_NEAR(v->color[2], corner_rgb[k][2], 1e-6);
        PRC_ASSERT_NEAR(v->color[3], want_alpha[k], 1.0 / 510.0);
    }
    return seen;
}

int
main(void)
{
    static const double alpha_rgba[3] = { 1.0, 160.0 / 255.0, 40.0 / 255.0 };
    static const double alpha_rgb[3] = { 1.0, 1.0, 1.0 };
    static const double alpha_wire[3] = { 1.0, 0.6, 0.2 };
    prc_context *ctx;
    prc_api_write_tessellation tess_w[3];
    prc_api_write_wire_element wire;
    prc_api_write_rep_item items[3];
    prc_api_write_node leaves[3], root;
    prc_api_write_node *kids[3];
    uint8_t *buf = NULL;
    size_t buf_size = 0;
    FILE *fid;
    prc_api_data data;
    prc_api_product *tree = NULL;
    prc_api_tess *tesses = NULL;
    uint32_t num_parts = 0, num_products = 0, num_markups = 0;
    uint32_t num_tess = 0, num_line = 0, num_exact = 0, k;

    PRC_TEST_BEGIN("vertex colour alpha");

    ctx = prc_api_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    fill_tess(&tess_w[0], rgba_bytes, 1);
    fill_tess(&tess_w[1], rgb_bytes, 0);
    memset(&wire, 0, sizeof(wire));
    wire.positions = wire_positions;
    wire.num_vertices = 3;
    wire.is_closed = 1;
    wire.colors = wire_colors;
    memset(&tess_w[2], 0, sizeof(tess_w[2]));
    tess_w[2].kind = PRC_API_WRITE_TESS_KIND_WIRE;
    tess_w[2].wire_elements = &wire;
    tess_w[2].num_wire_elements = 1;
    fill_leaf(&leaves[0], &items[0], PRC_API_WRITE_RI_SURFACE, 1, "rgba");
    fill_leaf(&leaves[1], &items[1], PRC_API_WRITE_RI_SURFACE, 2, "rgb");
    fill_leaf(&leaves[2], &items[2], PRC_API_WRITE_RI_WIRE, 3, "wire");
    for (k = 0; k < 3; k++)
        kids[k] = &leaves[k];
    memset(&root, 0, sizeof(root));
    root.name = "root";
    root.children = kids;
    root.num_children = 3;
    root.bbox_max[0] = root.bbox_max[1] = 1.0;

    PRC_ASSERT_EQ(prc_api_write_prc_buffer(ctx, "vertex_color_alpha", &root, tess_w, 3, &buf, &buf_size), 0);
    fid = fopen(TEST_VALPHA_FILENAME, "wb");
    PRC_ASSERT_NOT_NULL(fid);
    PRC_ASSERT_EQ(fwrite(buf, 1, buf_size, fid), buf_size);
    fclose(fid);
    prc_api_write_prc_buffer_free(ctx, buf);

    data = prc_api_open_contents(ctx, TEST_VALPHA_FILENAME);
    if (data == NULL)
        prc_api_print_error_stack(ctx);
    PRC_ASSERT_NOT_NULL(data);
    PRC_ASSERT_EQ(prc_api_prep_model_tree(ctx, data, &num_parts, &num_products, &num_markups), 0);
    PRC_ASSERT_EQ(prc_api_create_model_tree(ctx, data, &tree, num_parts, num_products, num_markups), 0);
    PRC_ASSERT_EQ(prc_api_get_number_tessellations(ctx, data, tree, &num_tess, &num_line, &num_exact, 0), 0);
    PRC_ASSERT_EQ(num_tess, 3u);

    tesses = (prc_api_tess *)calloc(num_tess, sizeof(prc_api_tess));
    PRC_ASSERT_NOT_NULL(tesses);
    for (k = 0; k < num_tess; k++)
    {
        prc_api_tess *t = &tesses[k];
        uint8_t has_lines = 0;
        uint32_t nf = prc_api_get_number_faces(ctx, data, k);

        t->num_faces = nf;
        t->tess_faces = (prc_api_face *)calloc(nf ? nf : 1, sizeof(prc_api_face));
        PRC_ASSERT_NOT_NULL(t->tess_faces);
        PRC_ASSERT_EQ(prc_api_initialize_tessellation(ctx, data, tree, k, t, NULL, &has_lines), 0);
        if (t->type == PRC_API_TESS_3D_Wire)
            PRC_ASSERT_EQ(prc_api_get_tessellation_vertices(ctx, data, k, 0, NULL, t), 0);
        else
        {
            PRC_ASSERT_EQ(t->type, PRC_API_TESS_3D);
            PRC_ASSERT_EQ(nf, 1u);
            PRC_ASSERT_EQ(prc_api_get_tessellation_vertices(ctx, data, k, 0, &t->tess_faces[0], t), 0);
        }
    }

    /* Each leaf's entry, by its part. */
    {
        static const char *names[3] = { "rgba", "rgb", "wire" };
        const double *alphas[3];

        alphas[0] = alpha_rgba;
        alphas[1] = alpha_rgb;
        alphas[2] = alpha_wire;
        for (k = 0; k < 3; k++)
        {
            const prc_api_part *p = find_drawn_part(tree, names[k]);
            const prc_api_tess *t;

            PRC_ASSERT_NOT_NULL(p);
            PRC_ASSERT(p->part_detail_index < num_tess);
            t = &tesses[p->part_detail_index];
            if (k == 2)
            {
                PRC_ASSERT_EQ(t->type, PRC_API_TESS_3D_Wire);
                PRC_ASSERT_EQ(check_vertices(&t->tess_vertices, alphas[k]), 7u);
            }
            else
            {
                PRC_ASSERT_EQ(t->type, PRC_API_TESS_3D);
                PRC_ASSERT_EQ(check_vertices(&t->tess_faces[0].face_vertices, alphas[k]), 7u);
            }
        }
    }

    prc_api_release_data(ctx, data, tesses, num_tess, NULL, 0, NULL, 0, tree);
    for (k = 0; k < num_tess; k++)
        free(tesses[k].tess_faces);
    free(tesses);
    prc_api_release_context(ctx);
    remove(TEST_VALPHA_FILENAME);

    PRC_TEST_END;
}
