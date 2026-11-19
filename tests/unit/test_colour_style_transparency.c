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

/* A style's transparency read back through the public API, for a style
   that names a colour as well as one that names a material.

   The style's transparency byte (Table 92 "GraphStyle", 0 transparent to
   255 opaque) used to reach a material face's diffuse alpha only: a face
   whose style named a colour came back opaque, and neither the face nor
   its tessellation ever had has_transparency set, so a viewer had nothing
   to tell it to blend. A machine case drawn see-through by Adobe Acrobat
   came out solid.

   The writer writes a material for each item, so each case writes one
   item with a material, opens the file, and for the colour cases makes the
   item's style name the material's diffuse colour instead, before anything
   reads the style. The four cases:

     material, alpha 1      opaque, no flags
     material, alpha 0.25   diffuse alpha 64/255, flags set
     colour,   no transparency   colour alpha 1, no flags
     colour,   alpha 0.25   colour alpha 64/255, flags set

   0.25 rather than 0.5: see test_write_materials for why 0.5 cannot tell
   opacity from its inverse. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "prc_test.h"
#include "prc_context.h"
#include "prc_api.h"
#include "prc_data.h"

#define TEST_CST_FILENAME "test_colour_style_transparency_tmp.prc"

static const double tri_positions[3 * 3] = {
    0.0, 0.0, 0.0,
    1.0, 0.0, 0.0,
    0.0, 1.0, 0.0
};
static const uint32_t tri_indices[3] = { 0, 1, 2 };
static const uint32_t tri_face_counts[1] = { 1 };

/* A colour unlike the writer's default material, so a style that fell back
   to the default would show. */
static const double item_colour[3] = { 0.2, 0.6, 0.8 };

static void
write_item(prc_context *ctx, double alpha)
{
    prc_api_write_tessellation tess;
    prc_api_write_rep_item item;
    prc_api_write_node leaf, root;
    prc_api_write_node *kids[1];
    uint8_t *buf = NULL;
    size_t buf_size = 0;
    FILE *fid;

    memset(&tess, 0, sizeof(tess));
    tess.kind = PRC_API_WRITE_TESS_KIND_TRIANGLES;
    tess.positions = tri_positions;
    tess.num_positions = 3;
    tess.tri_indices = tri_indices;
    tess.num_triangles = 1;
    tess.face_tri_counts = tri_face_counts;
    tess.num_faces = 1;
    tess.must_calculate_normals = 1;

    memset(&item, 0, sizeof(item));
    item.kind = PRC_API_WRITE_RI_SURFACE;
    item.biased_tessellation_index = 1;
    item.has_material = 1;
    memcpy(item.material_color, item_colour, sizeof(item_colour));
    item.material_alpha = alpha;
    item.material_shininess = 0.5;

    memset(&leaf, 0, sizeof(leaf));
    leaf.rep_items = &item;
    leaf.num_rep_items = 1;
    leaf.name = "leaf";
    leaf.bbox_max[0] = leaf.bbox_max[1] = 1.0;

    kids[0] = &leaf;
    memset(&root, 0, sizeof(root));
    root.name = "root";
    root.children = kids;
    root.num_children = 1;
    root.bbox_max[0] = root.bbox_max[1] = 1.0;

    PRC_ASSERT_EQ(prc_api_write_prc_buffer(ctx, "colour_style_transparency", &root,
        &tess, 1, &buf, &buf_size), 0);
    fid = fopen(TEST_CST_FILENAME, "wb");
    PRC_ASSERT_NOT_NULL(fid);
    PRC_ASSERT_EQ(fwrite(buf, 1, buf_size, fid), buf_size);
    fclose(fid);
    prc_api_write_prc_buffer_free(ctx, buf);
}

/* The style of the file's one representation item. */
static prc_graph_style *
item_style(prc_data *data)
{
    const prc_asm_file_structure_tree *tree = data->file_struct[0].tree;
    prc_file_struct_internal_global_data *gd = &data->file_struct[0].globals->global_data;
    uint32_t i, j;

    PRC_ASSERT_NOT_NULL(tree);
    for (i = 0; i < tree->parts_count; i++)
    {
        for (j = 0; j < tree->parts[i].num_rep_items; j++)
        {
            const prc_ri *ri = &tree->parts[i].rep_items[j];
            uint32_t s;

            if (ri->ri_poly_brep_model == NULL)
                continue;
            s = ri->ri_poly_brep_model->item_content.base.graphics_content.biased_index_of_line_style;
            PRC_ASSERT(s >= 1 && s <= gd->style_count);
            return &gd->styles[s - 1];
        }
    }
    PRC_ASSERT(0);
    return NULL;
}

static void
run_case(prc_context *ctx, const char *label, double alpha, int as_colour)
{
    prc_data *data;
    prc_api_product *model_tree = NULL;
    prc_api_tess *tesses;
    prc_api_face *face;
    prc_graph_style *st;
    uint32_t num_parts = 0, num_products = 0, num_markups = 0;
    uint32_t num_tess = 0, num_line_tess = 0, num_extra = 0;
    uint8_t has_lines = 0;
    float want_alpha;
    uint32_t v;

    printf("  sub-case: %s\n", label);
    write_item(ctx, alpha);

    data = (prc_data *)prc_api_open_contents(ctx, TEST_CST_FILENAME);
    PRC_ASSERT_NOT_NULL(data);

    st = item_style(data);
    PRC_ASSERT(st->is_material);
    PRC_ASSERT_EQ(st->is_transparency, alpha < 1.0 ? 1 : 0);
    if (st->is_transparency)
        PRC_ASSERT_EQ(st->transparency, 64);
    want_alpha = st->is_transparency ? st->transparency / 255.0f : 1.0f;

    if (as_colour)
    {
        const prc_file_struct_internal_global_data *gd = &data->file_struct[0].globals->global_data;
        const prc_graph_material *mt;

        PRC_ASSERT(st->biased_color_index >= 1 && st->biased_color_index <= gd->material_count);
        mt = &gd->materials[st->biased_color_index - 1];
        /* Colour and material indices into the colour table are biased the
           same way, so the material's diffuse index names its colour */
        st->is_material = 0;
        st->biased_color_index = mt->biased_diffuse_index;
    }

    PRC_ASSERT(prc_api_prep_model_tree(ctx, data, &num_parts, &num_products, &num_markups) >= 0);
    PRC_ASSERT(prc_api_create_model_tree(ctx, data, &model_tree, num_parts, num_products, num_markups) >= 0);
    PRC_ASSERT(prc_api_get_number_tessellations(ctx, data, model_tree, &num_tess,
        &num_line_tess, &num_extra, 0) >= 0);
    PRC_ASSERT_EQ(num_tess, 1);
    tesses = (prc_api_tess *)calloc(1, sizeof(prc_api_tess));
    PRC_ASSERT_NOT_NULL(tesses);
    tesses[0].num_faces = prc_api_get_number_faces(ctx, data, 0);
    PRC_ASSERT_EQ(tesses[0].num_faces, 1);
    tesses[0].tess_faces = (prc_api_face *)calloc(1, sizeof(prc_api_face));
    PRC_ASSERT_NOT_NULL(tesses[0].tess_faces);
    PRC_ASSERT(prc_api_initialize_tessellation(ctx, data, model_tree, 0, &tesses[0], NULL, &has_lines) >= 0);
    PRC_ASSERT(prc_api_get_tessellation_vertices(ctx, data, 0, 0, &tesses[0].tess_faces[0], &tesses[0]) >= 0);
    face = &tesses[0].tess_faces[0];

    if (as_colour)
    {
        PRC_ASSERT_EQ(face->is_material, 0);
        PRC_ASSERT(face->face_vertices.num_vertices > 0);
        for (v = 0; v < face->face_vertices.num_vertices; v++)
        {
            const float *c = face->face_vertices.vertices[v].color;

            PRC_ASSERT(c[0] > item_colour[0] - 0.01 && c[0] < item_colour[0] + 0.01);
            PRC_ASSERT(c[1] > item_colour[1] - 0.01 && c[1] < item_colour[1] + 0.01);
            PRC_ASSERT(c[2] > item_colour[2] - 0.01 && c[2] < item_colour[2] + 0.01);
            PRC_ASSERT(c[3] > want_alpha - 0.001f && c[3] < want_alpha + 0.001f);
        }
    }
    else
    {
        PRC_ASSERT_EQ(face->is_material, 1);
        PRC_ASSERT(face->material.diffuse_alpha > want_alpha - 0.001f &&
                   face->material.diffuse_alpha < want_alpha + 0.001f);
    }

    /* The face and its tessellation say whether anything draws less than
       opaque, so a viewer knows to blend it */
    PRC_ASSERT_EQ(face->has_transparency, alpha < 1.0 ? 1 : 0);
    PRC_ASSERT_EQ(tesses[0].has_transparency, alpha < 1.0 ? 1 : 0);

    prc_api_release_data(ctx, (prc_api_data)data, tesses, 1, NULL, 0, NULL, 0, model_tree);
    remove(TEST_CST_FILENAME);
}

int
main(void)
{
    prc_context *ctx;

    PRC_TEST_BEGIN("test_colour_style_transparency");

    ctx = prc_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    run_case(ctx, "material style, opaque", 1.0, 0);
    run_case(ctx, "material style, transparency 64", 0.25, 0);
    run_case(ctx, "colour style, opaque", 1.0, 1);
    run_case(ctx, "colour style, transparency 64", 0.25, 1);

    prc_release_context(ctx);

    PRC_TEST_END;
}
