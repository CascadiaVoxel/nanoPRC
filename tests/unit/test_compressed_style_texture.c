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

/* A compressed tessellation can name its faces' style itself, in its
   line_attribute_array, where an entry is the style index plus 1 and 0 names no
   style. When the part tree gives a face no style and the tessellation names one
   style for all its faces, that is a texture, and the tessellation has texture
   data, the face is textured (is_texture, and the picture). Files in prc-db
   (a3drtemplate_01, 3dpdf-AEC-OfficeBuilding) are written that way, and Adobe
   Acrobat draws the texture on a3drtemplate_01. An entry of 0 names no style,
   and a face of a file whose style is named by nothing stays plain
   (smallBMS-(3D-pdf).stream-13).

   The file is a textured quad written with the picture on the item's style, which
   gives the one texture style and writes line attributes of 0. Each case then
   changes what the reader sees, after the file is read and before the model tree
   is built: whether the item keeps its style, and what the tessellation's line
   attribute is. */

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

static const char *TEST_PRC_FILENAME = "test_compressed_style_texture_output.prc";

/* 2x2 RGB */
static const uint8_t TEST_PIXELS[2 * 2 * 3] = {
    0xFF, 0x00, 0x00,   0x00, 0xFF, 0x00,
    0x00, 0x00, 0xFF,   0xFF, 0xFF, 0x00
};

static const double QUAD_POS[4 * 3] = { 0, 0, 0,  1, 0, 0,  1, 1, 0,  0, 1, 0 };
static const double QUAD_UV[4 * 2] = { 0.1, 0.2,  0.9, 0.2,  0.9, 0.8,  0.1, 0.8 };
static const uint32_t QUAD_TRIS[6] = { 0, 1, 2,  0, 2, 3 };

static int
write_textured_quad(prc_context *ctx)
{
    prc_write_global_tables tables;
    prc_write_tess_entry tess_entry;
    prc_write_rep_item ri;
    prc_write_tree_node root;
    uint32_t k;
    int code;

    PRC_ASSERT_EQ(prc_write_global_tables_init(ctx, &tables), 0);

    memset(&tess_entry, 0, sizeof(tess_entry));
    tess_entry.kind = PRC_WRITE_TESS_KIND_COMPRESSED;
    tess_entry.positions = QUAD_POS;
    tess_entry.num_positions = 4;
    tess_entry.tri_indices = QUAD_TRIS;
    tess_entry.num_triangles = 2;
    tess_entry.tex_coords = QUAD_UV;
    tess_entry.num_tex_coords = 4;
    tess_entry.tex_indices = QUAD_TRIS;

    memset(&ri, 0, sizeof(ri));
    ri.kind = PRC_WRITE_RI_SURFACE;
    ri.biased_tessellation_index = 1;
    ri.has_material = 1;
    ri.material_color[0] = ri.material_color[1] = ri.material_color[2] = 1.0;
    ri.material_alpha = 1.0;
    ri.has_texture = 1;
    ri.texture_image = TEST_PIXELS;
    ri.texture_image_size = sizeof(TEST_PIXELS);
    ri.texture_format = PRC_API_WRITE_TEXTURE_RGB;
    ri.texture_width = 2;
    ri.texture_height = 2;

    memset(&root, 0, sizeof(root));
    root.rep_items = &ri;
    root.num_rep_items = 1;
    for (k = 0; k < 3; k++)
    {
        root.bbox_min[k] = 1e300;
        root.bbox_max[k] = -1e300;
    }
    for (k = 0; k < 4 * 3; k++)
    {
        if (QUAD_POS[k] < root.bbox_min[k % 3]) root.bbox_min[k % 3] = QUAD_POS[k];
        if (QUAD_POS[k] > root.bbox_max[k % 3]) root.bbox_max[k % 3] = QUAD_POS[k];
    }

    code = prc_write_prc_file(ctx, TEST_PRC_FILENAME, NULL, &tables, &root, &tess_entry, 1);
    prc_write_global_tables_free(ctx, &tables);
    return code;
}

/* Reads the file back, first changing what the reader finds: item_keeps_style 0
   takes the style off the representation item, so that the tree names none, and
   line_attribute is set as the tessellation's line attribute for every face (-1
   leaves the file's, LINE_ATTRIBUTE_TEXTURE_STYLE sets the file's texture style). Returns is_texture of the face the public API gives. */
/* mapping_attributes to give the file's one texture definition. The writer states none
   (0); the files that Adobe Acrobat textures this way (a3drtemplate_01) state diffuse, so
   that is the default here, and the cases at the end change it. -1 leaves the file's. */
static int g_mapping_attributes = PRC_texture_mapping_attributes_diffuse;

/* line_attribute value that stands for the file's texture style (its index plus 1) */
#define LINE_ATTRIBUTE_TEXTURE_STYLE 1000

static int
face_is_texture(prc_context *ctx, int item_keeps_style, int line_attribute, int *texture_style_biased)
{
    prc_data *pd;
    prc_api_data data;
    prc_api_product *model_tree = NULL;
    uint32_t num_parts = 0, num_products = 0, num_markups = 0;
    uint32_t num_tess = 0, num_line_tess = 0, num_extra = 0;
    prc_api_tess *tesses;
    uint8_t has_lines = 0;
    uint32_t k;
    int is_texture;

    pd = (prc_data *)prc_api_open_contents(ctx, TEST_PRC_FILENAME);
    PRC_ASSERT_NOT_NULL(pd);
    data = (prc_api_data)pd;

    /* The file names a texture style from the item, so that the first case is
       the unchanged file. Which of the file's styles that is, is what the
       tessellation has to name (plus 1) */
    {
        prc_file_struct_internal_global_data *g = &pd->file_struct[0].globals->global_data;
        uint32_t item_style = pd->file_struct[0].tree->parts[0].rep_items[0].item_content.base.graphics_content.biased_index_of_line_style;

        PRC_ASSERT(item_style >= 1 && item_style <= g->style_count);
        PRC_ASSERT(g->styles[item_style - 1].biased_color_index > 0);
        PRC_ASSERT(g->materials[g->styles[item_style - 1].biased_color_index - 1].biased_texture_definition_index > 0);
        *texture_style_biased = (int)item_style;
        if (g_mapping_attributes >= 0)
        {
            uint32_t td;

            for (td = 0; td < g->texture_count; td++)
                g->textures[td].texture_mapping_attributes = (PRC_texture_mapping_attributes_t)g_mapping_attributes;
        }
        if (!item_keeps_style)
        {
            /* no node of the tree names a style: the item, its part and the products */
            uint32_t n;

            pd->file_struct[0].tree->parts[0].rep_items[0].item_content.base.graphics_content.biased_index_of_line_style = 0;
            pd->file_struct[0].tree->parts[0].base.graphics_content.biased_index_of_line_style = 0;
            for (n = 0; n < pd->file_struct[0].tree->product_count; n++)
                pd->file_struct[0].tree->products[n].base.graphics_content.biased_index_of_line_style = 0;
        }
        if (line_attribute == LINE_ATTRIBUTE_TEXTURE_STYLE)
            line_attribute = (int)item_style;
    }

    if (line_attribute >= 0)
    {
        prc_tess_3d_compressed *t = pd->file_struct[0].tessellation->tess[0].tess_3d_compressed;

        PRC_ASSERT_NOT_NULL(t);
        PRC_ASSERT(t->line_attribute_array_size > 0);
        PRC_ASSERT_NOT_NULL(t->line_attribute_array);
        for (k = 0; k < t->line_attribute_array_size; k++)
            t->line_attribute_array[k] = line_attribute;
        /* the decoder's view of it: the style index, with 0 for no style as well */
        if (t->triangle_styles != NULL)
            for (k = 0; k < t->triangle_face_array_size; k++)
                t->triangle_styles[k] = line_attribute > 0 ? (uint32_t)(line_attribute - 1) : 0;
    }

    PRC_ASSERT(prc_api_prep_model_tree(ctx, data, &num_parts, &num_products, &num_markups) >= 0);
    PRC_ASSERT(prc_api_create_model_tree(ctx, data, &model_tree, num_parts, num_products, num_markups) >= 0);
    PRC_ASSERT(prc_api_get_number_tessellations(ctx, data, model_tree, &num_tess,
        &num_line_tess, &num_extra, 0) >= 0);
    PRC_ASSERT(num_tess >= 1);
    tesses = (prc_api_tess *)calloc(num_tess, sizeof(prc_api_tess));
    PRC_ASSERT_NOT_NULL(tesses);

    tesses[0].num_faces = prc_api_get_number_faces(ctx, data, 0);
    PRC_ASSERT(tesses[0].num_faces >= 1);
    tesses[0].tess_faces = (prc_api_face *)calloc(tesses[0].num_faces, sizeof(prc_api_face));
    PRC_ASSERT_NOT_NULL(tesses[0].tess_faces);
    PRC_ASSERT(prc_api_initialize_tessellation(ctx, data, model_tree, 0, &tesses[0], NULL, &has_lines) >= 0);
    PRC_ASSERT_EQ(tesses[0].type, PRC_API_TESS_3D_Compressed);
    PRC_ASSERT(prc_api_get_tessellation_vertices(ctx, data, 0, 0, &tesses[0].tess_faces[0], &tesses[0]) >= 0);
    is_texture = tesses[0].tess_faces[0].is_texture ? 1 : 0;
    if (is_texture)
    {
        PRC_ASSERT_EQ(tesses[0].tess_faces[0].texture.width, 2);
        PRC_ASSERT_EQ(tesses[0].tess_faces[0].texture.height, 2);
    }

    prc_api_release_data(ctx, data, tesses, num_tess, NULL, 0, NULL, 0, model_tree);
    return is_texture;
}

int
main(void)
{
    prc_context *ctx;
    int style = 0;

    PRC_TEST_BEGIN("compressed tessellation: texture from the style the tessellation names");

    ctx = prc_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);
    PRC_ASSERT_EQ(write_textured_quad(ctx), 0);

    /* The tree names the texture style, as the writer does: textured, with or
       without the tessellation naming anything */
    PRC_ASSERT_EQ(face_is_texture(ctx, 1, -1, &style), 1);

    /* The tree names none and the tessellation names none (line attribute 0):
       plain, as smallBMS */
    PRC_ASSERT_EQ(face_is_texture(ctx, 0, 0, &style), 0);

    /* The tree names none and the tessellation names the texture style:
       textured, as a3drtemplate_01 */
    PRC_ASSERT_EQ(face_is_texture(ctx, 0, LINE_ATTRIBUTE_TEXTURE_STYLE, &style), 1);

    /* ... and the same line attribute off by one either way names a style
       that is not the texture (or none): plain, and no failure */
    PRC_ASSERT_EQ(face_is_texture(ctx, 0, style + 1, &style), 0);
    if (style > 1)
        PRC_ASSERT_EQ(face_is_texture(ctx, 0, style - 1, &style), 0);

    /* The texture definition states no mapping attributes (0), or only a bump map
       (0x2): the picture does not apply as a diffuse map, and the face stays
       plain even when the tessellation names the texture style (as in
       unolinea-house-dwg, where Acrobat draws the style's colour, not the picture) */
    g_mapping_attributes = 0;
    PRC_ASSERT_EQ(face_is_texture(ctx, 0, LINE_ATTRIBUTE_TEXTURE_STYLE, &style), 0);
    g_mapping_attributes = PRC_texture_mapping_attributes_bump;
    PRC_ASSERT_EQ(face_is_texture(ctx, 0, LINE_ATTRIBUTE_TEXTURE_STYLE, &style), 0);
    /* ... a map that is diffuse among others still applies */
    g_mapping_attributes = PRC_texture_mapping_attributes_diffuse | PRC_texture_mapping_attributes_specular;
    PRC_ASSERT_EQ(face_is_texture(ctx, 0, LINE_ATTRIBUTE_TEXTURE_STYLE, &style), 1);
    g_mapping_attributes = PRC_texture_mapping_attributes_diffuse;

    prc_release_context(ctx);
    remove(TEST_PRC_FILENAME);

    PRC_TEST_END;
}
