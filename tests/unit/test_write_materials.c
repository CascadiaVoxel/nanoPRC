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

/* Per-representation-item materials through the public write API.

   Two leaves, two different colours, written and reopened. What this pins down
   is that the globals tables really carry both materials and that the two
   items do not share one style -- the failure this is guarding against is a
   writer that registers a material and then hands every item the shared
   default anyway, which would look perfectly fine in the file and render as
   one flat colour.

   The style resolution happens before the globals section is serialised, so a
   regression there shows up as a missing style rather than a wrong one. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "prc_test.h"
#include "prc_context.h"
#include "prc_api.h"
#include "zlib.h"
#include "prc_data.h"
#include "prc_parse_global.h"
#include "prc_internal_api.h"

#define TEST_MATERIALS_FILENAME "test_write_materials_tmp.prc"

/* One triangle, so each leaf has something to hang a material on. */
static const double tri_positions[3 * 3] = {
    0.0, 0.0, 0.0,
    1.0, 0.0, 0.0,
    0.0, 1.0, 0.0
};
static const uint32_t tri_indices[3] = { 0, 1, 2 };
static const uint32_t tri_face_counts[1] = { 1 };

static void
fill_tess(prc_api_write_tessellation *t)
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
    t->crease_angle_degrees = 30.0;
}

/* Three textured items through the public API, the second and third with
   the same image. The file must hold two pictures and two stored images, and
   each item must still reach its own image: following item -> style ->
   texture application -> definition -> picture -> stored bytes, rather than
   counting pictures, catches a writer that shares and then points an item at
   the wrong picture. The shared image is the SECOND picture so that such a
   writer cannot be right by accident: a match on the first picture returns 1
   whether the index is computed correctly or not. */
static void
test_shared_pictures(prc_context *ctx)
{
    static const uint8_t image_x[2 * 2 * 3] = {
        0xFF, 0x00, 0x00,   0x00, 0xFF, 0x00,
        0x00, 0x00, 0xFF,   0xFF, 0xFF, 0x00
    };
    static const uint8_t image_y[2 * 2 * 3] = {
        0x10, 0x20, 0x30,   0x40, 0x50, 0x60,
        0x70, 0x80, 0x90,   0xA0, 0xB0, 0xC0
    };
    static const double uvs[3 * 2] = { 0.0, 0.0,  1.0, 0.0,  0.0, 1.0 };
    const uint8_t *images[3];
    prc_api_write_tessellation tess[3];
    prc_api_write_rep_item items[3];
    prc_api_write_node leaves[3], root;
    prc_api_write_node *kids[3];
    uint8_t *buf = NULL;
    size_t buf_size = 0;
    FILE *fid;
    prc_data *data;
    const prc_file_struct_internal_global_data *gd;
    const prc_asm_file_structure_tree *tree;
    uint32_t found = 0, i, k;

    printf("  sub-case: items with the same image share one picture\n");

    images[0] = image_y;
    images[1] = image_x;
    images[2] = image_x;
    for (k = 0; k < 3; k++)
    {
        fill_tess(&tess[k]);
        tess[k].tex_coords = uvs;
        tess[k].num_tex_coords = 3;
        tess[k].tex_indices = tri_indices;

        memset(&items[k], 0, sizeof(items[k]));
        items[k].kind = PRC_API_WRITE_RI_SURFACE;
        items[k].biased_tessellation_index = k + 1;
        items[k].has_material = 1;
        items[k].material_color[0] = items[k].material_color[1] = items[k].material_color[2] = 1.0;
        items[k].material_alpha = 1.0;
        items[k].has_texture = 1;
        items[k].texture_image = images[k];
        items[k].texture_image_size = sizeof(image_x);
        items[k].texture_format = PRC_API_WRITE_TEXTURE_RGB;
        items[k].texture_width = 2;
        items[k].texture_height = 2;

        memset(&leaves[k], 0, sizeof(leaves[k]));
        leaves[k].rep_items = &items[k];
        leaves[k].num_rep_items = 1;
        leaves[k].bbox_max[0] = leaves[k].bbox_max[1] = 1.0;
        kids[k] = &leaves[k];
    }
    memset(&root, 0, sizeof(root));
    root.name = "root";
    root.children = kids;
    root.num_children = 3;
    root.bbox_max[0] = root.bbox_max[1] = 1.0;

    PRC_ASSERT_EQ(prc_api_write_prc_buffer(ctx, "shared_pictures", &root,
        tess, 3, &buf, &buf_size), 0);
    fid = fopen(TEST_MATERIALS_FILENAME, "wb");
    PRC_ASSERT_NOT_NULL(fid);
    PRC_ASSERT_EQ(fwrite(buf, 1, buf_size, fid), buf_size);
    fclose(fid);
    prc_api_write_prc_buffer_free(ctx, buf);

    data = prc_api_open_contents(ctx, TEST_MATERIALS_FILENAME);
    if (data == NULL)
        prc_api_print_error_stack(ctx);
    PRC_ASSERT_NOT_NULL(data);
    gd = &data->file_struct[0].globals->global_data;
    PRC_ASSERT_EQ(gd->picture_count, 2);
    PRC_ASSERT_NOT_NULL(data->file_struct[0].header);
    PRC_ASSERT_EQ(data->file_struct[0].header->file_count, 2);

    /* The items, in the order written, each followed to its stored image. */
    tree = data->file_struct[0].tree;
    PRC_ASSERT_NOT_NULL(tree);
    for (i = 0; i < tree->parts_count; i++)
    {
        const prc_asm_parts_definition *part = &tree->parts[i];
        uint32_t j;

        for (j = 0; j < part->num_rep_items; j++)
        {
            const prc_ri *ri = &part->rep_items[j];
            const prc_graph_style *st;
            const prc_graph_material *app;
            const prc_graph_texture_definition *def;
            const prc_graph_picture *p;
            const prc_uncomp_block *blk;
            uint32_t si, b;

            if (ri->ri_poly_brep_model == NULL)
                continue;
            PRC_ASSERT(found < 3);
            si = ri->ri_poly_brep_model->item_content.base.graphics_content.biased_index_of_line_style;
            PRC_ASSERT(si >= 1 && si <= gd->style_count);
            st = &gd->styles[si - 1];
            PRC_ASSERT(st->is_material);
            PRC_ASSERT(st->biased_color_index >= 1 && st->biased_color_index <= gd->material_count);
            app = &gd->materials[st->biased_color_index - 1];
            PRC_ASSERT_EQ(app->tag, PRC_TYPE_GRAPH_TextureApplication);
            PRC_ASSERT(app->biased_texture_definition_index >= 1 &&
                       app->biased_texture_definition_index <= gd->texture_count);
            def = &gd->textures[app->biased_texture_definition_index - 1];
            PRC_ASSERT(def->biased_picture_index >= 1 && def->biased_picture_index <= gd->picture_count);
            p = &gd->pictures[def->biased_picture_index - 1];
            PRC_ASSERT(p->biased_uncompressed_file_index >= 1 &&
                       p->biased_uncompressed_file_index <= data->file_struct[0].header->file_count);
            blk = &data->file_struct[0].header->files[p->biased_uncompressed_file_index - 1];
            {
                /* stored as a zlib stream, bottom row first: 2 rows of 6 bytes */
                uint8_t px[sizeof(image_x) + 8];
                uLongf len = sizeof(px);
                PRC_ASSERT_EQ(blk->block[0], 0x78);
                PRC_ASSERT_EQ(uncompress(px, &len, blk->block, blk->block_size), Z_OK);
                PRC_ASSERT_EQ((uint32_t)len, (uint32_t)sizeof(image_x));
                for (b = 0; b < sizeof(image_x); b++)
                    PRC_ASSERT_EQ(px[b], images[found][(1 - b / 6) * 6 + b % 6]);
            }
            found++;
        }
    }
    PRC_ASSERT_EQ(found, 3);

    prc_api_release_data(ctx, (prc_api_data)data, NULL, 0, NULL, 0, NULL, 0, NULL);
    remove(TEST_MATERIALS_FILENAME);
}

static void
test_two_materials(prc_context *ctx)
{
    prc_api_write_tessellation tess[2];
    prc_api_write_rep_item items_a[1];
    prc_api_write_rep_item items_b[1];
    prc_api_write_node leaf_a, leaf_b, root;
    prc_api_write_node *kids[2];
    uint8_t *buf = NULL;
    size_t buf_size = 0;
    FILE *fid;
    prc_data *data = NULL;
    uint32_t num_parts = 0, num_products = 0, num_markups = 0;

    printf("  sub-case: two items, two materials, through the public API\n");

    fill_tess(&tess[0]);
    fill_tess(&tess[1]);

    memset(items_a, 0, sizeof(items_a));
    items_a[0].kind = PRC_API_WRITE_RI_SURFACE;
    items_a[0].biased_tessellation_index = 1;
    items_a[0].has_material = 1;
    items_a[0].material_color[0] = 1.0;   /* red */
    items_a[0].material_color[1] = 0.0;
    items_a[0].material_color[2] = 0.0;
    items_a[0].material_alpha = 1.0;
    items_a[0].material_shininess = 0.5;

    memset(items_b, 0, sizeof(items_b));
    items_b[0].kind = PRC_API_WRITE_RI_SURFACE;
    items_b[0].biased_tessellation_index = 2;
    items_b[0].has_material = 1;
    items_b[0].material_color[0] = 0.0;
    items_b[0].material_color[1] = 0.0;
    items_b[0].material_color[2] = 1.0;   /* blue */
    items_b[0].material_alpha = 0.25;     /* and mostly transparent */
    items_b[0].material_shininess = 0.1;

    memset(&leaf_a, 0, sizeof(leaf_a));
    leaf_a.rep_items = items_a;
    leaf_a.num_rep_items = 1;
    leaf_a.name = "red_leaf";
    leaf_a.bbox_max[0] = leaf_a.bbox_max[1] = 1.0;

    memset(&leaf_b, 0, sizeof(leaf_b));
    leaf_b.rep_items = items_b;
    leaf_b.num_rep_items = 1;
    leaf_b.name = "blue_leaf";
    leaf_b.bbox_max[0] = leaf_b.bbox_max[1] = 1.0;

    kids[0] = &leaf_a;
    kids[1] = &leaf_b;
    memset(&root, 0, sizeof(root));
    root.name = "root";
    root.children = kids;
    root.num_children = 2;
    root.bbox_max[0] = root.bbox_max[1] = 1.0;

    PRC_ASSERT_EQ(prc_api_write_prc_buffer(ctx, "materials_model", &root,
        tess, 2, &buf, &buf_size), 0);
    PRC_ASSERT_NOT_NULL(buf);
    PRC_ASSERT(buf_size > 0);

    fid = fopen(TEST_MATERIALS_FILENAME, "wb");
    PRC_ASSERT_NOT_NULL(fid);
    PRC_ASSERT_EQ(fwrite(buf, 1, buf_size, fid), buf_size);
    fclose(fid);
    prc_api_write_prc_buffer_free(ctx, buf);

    data = prc_api_open_contents(ctx, TEST_MATERIALS_FILENAME);
    if (data == NULL)
        prc_api_print_error_stack(ctx);
    PRC_ASSERT_NOT_NULL(data);

    PRC_ASSERT_EQ(prc_api_prep_model_tree(ctx, data, &num_parts, &num_products,
        &num_markups), 0);

    /* The styles have to reach the items, not merely exist in the tables.
       Registering a material and then handing every item the shared default
       produces a file that looks right in the globals and renders as one flat
       colour -- an earlier version of this test checked only the tables and
       passed against exactly that bug. */
    {
        const prc_asm_file_structure_tree *tree = data->file_struct[0].tree;
        uint32_t styles[2];
        uint32_t found = 0;
        uint32_t i;

        PRC_ASSERT_NOT_NULL(tree);
        for (i = 0; i < tree->parts_count && found < 2; i++)
        {
            const prc_asm_parts_definition *part = &tree->parts[i];
            uint32_t j;

            for (j = 0; j < part->num_rep_items && found < 2; j++)
            {
                const prc_ri *ri = &part->rep_items[j];

                if (ri->ri_poly_brep_model == NULL)
                    continue;
                styles[found++] =
                    ri->ri_poly_brep_model->item_content.base.graphics_content.biased_index_of_line_style;
            }
        }

        PRC_ASSERT_EQ(found, 2);
        /* Each item carries a real style... */
        PRC_ASSERT(styles[0] != 0);
        PRC_ASSERT(styles[1] != 0);
        /* ...and they are not the same one, which is what a writer that
           ignored has_material would produce. */
        PRC_ASSERT(styles[0] != styles[1]);

        /* Distinct is not the same as CORRECT, and the difference is not
           academic: with the colour index written as an entry index rather
           than a double index, these two styles were still distinct and
           still resolved -- to the shared default grey, both of them. Acrobat
           showed grey where red and blue were asked for while this test
           passed. So follow each style the whole way to a colour and compare
           it against what went in.

           style -> material -> ambient colour, unbiasing by three at the last
           step exactly as prc_style_api.c does. */
        {
            const prc_file_struct_internal_global_data *gd =
                &data->file_struct[0].globals->global_data;
            double want[2][3];
            uint32_t k;

            want[0][0] = 1.0; want[0][1] = 0.0; want[0][2] = 0.0;   /* red */
            want[1][0] = 0.0; want[1][1] = 0.0; want[1][2] = 1.0;   /* blue */

            for (k = 0; k < 2; k++)
            {
                uint32_t si = styles[k] - 1;
                uint32_t mi, ci;
                const prc_graph_style *st;
                const prc_graph_material *mt;
                const prc_rgb_color *c;

                PRC_ASSERT(si < gd->style_count);
                st = &gd->styles[si];
                PRC_ASSERT(st->is_material);

                mi = st->biased_color_index - 1;
                PRC_ASSERT(mi < gd->material_count);
                mt = &gd->materials[mi];

                PRC_ASSERT(mt->biased_ambient_index > 0);
                ci = (mt->biased_ambient_index - 1) / 3;
                PRC_ASSERT(ci < gd->color_count);
                c = &gd->colors[ci];

                PRC_ASSERT(c->red   > want[k][0] - 0.02 && c->red   < want[k][0] + 0.02);
                PRC_ASSERT(c->green > want[k][1] - 0.02 && c->green < want[k][1] + 0.02);
                PRC_ASSERT(c->blue  > want[k][2] - 0.02 && c->blue  < want[k][2] + 0.02);

                /* Transparency lives on the style, not the material: a reader
                   takes the effective alpha from is_transparency/transparency
                   and ignores the material's alpha components entirely. The
                   red item asked for alpha 1.0 and must carry no flag; the
                   blue asked for 0.25 and must carry 64.

                   0.25 rather than 0.5, deliberately. The field is an OPACITY
                   byte (7.5.3, "0 (transparent) to 255 (opaque)"), so it is
                   alpha*255 rounded; writing (1-alpha)*255 instead would be
                   inverted in every viewer while round-tripping perfectly
                   through our own reader. At alpha 0.5 both formulas give 128,
                   so that value cannot detect the inversion -- a first version
                   of this assertion used it, and a control confirmed it caught
                   nothing. At 0.25 they give 64 and 191. */
                if (k == 0)
                {
                    PRC_ASSERT_EQ(st->is_transparency, 0);
                }
                else
                {
                    PRC_ASSERT_EQ(st->is_transparency, 1);
                    PRC_ASSERT_EQ(st->transparency, 64);
                }
            }
        }
    }

    /* And the tables really hold both materials. */
    {
        const prc_asm_file_structure_globals *g = data->file_struct[0].globals;
        const prc_file_struct_internal_global_data *gd;
        uint32_t i, found_red = 0, found_blue = 0;

        PRC_ASSERT_NOT_NULL(g);
        gd = &g->global_data;

        PRC_ASSERT(gd->style_count >= 3);      /* default + red + blue */
        PRC_ASSERT(gd->material_count >= 3);
        PRC_ASSERT(gd->color_count >= 3);

        /* Colours are stored as bytes 0..255 after the writer's conversion,
           so look for the two we asked for rather than comparing doubles. */
        for (i = 0; i < gd->color_count; i++)
        {
            const prc_rgb_color *c = &gd->colors[i];

            if (c->red > 0.99 && c->green < 0.01 && c->blue < 0.01)
                found_red = 1;
            if (c->blue > 0.99 && c->red < 0.01 && c->green < 0.01)
                found_blue = 1;
        }
        PRC_ASSERT_EQ(found_red, 1);
        PRC_ASSERT_EQ(found_blue, 1);
    }

    prc_api_release_data(ctx, data, NULL, 0, NULL, 0, NULL, 0, NULL);
    remove(TEST_MATERIALS_FILENAME);
}

/* Writes one leaf holding one item, reopens it, and returns the material the
   public API reports for its first face: the same values the OBJ exporter and
   the viewer see. */
static void
write_and_read_face_material(prc_context *ctx, const prc_api_write_rep_item *item,
    prc_api_material *out)
{
    prc_api_write_tessellation tess[1];
    prc_api_write_rep_item items[1];
    prc_api_write_node leaf, root;
    prc_api_write_node *kids[1];
    uint8_t *buf = NULL;
    size_t buf_size = 0;
    FILE *fid;
    prc_api_data data;
    prc_api_product *model_tree = NULL;
    uint32_t num_parts = 0, num_products = 0, num_markups = 0;
    uint32_t num_tess = 0, num_line_tess = 0, num_extra = 0;
    prc_api_tess *tesses;
    uint8_t has_lines = 0;

    fill_tess(&tess[0]);
    items[0] = *item;

    memset(&leaf, 0, sizeof(leaf));
    leaf.rep_items = items;
    leaf.num_rep_items = 1;
    leaf.name = "leaf";
    leaf.bbox_max[0] = leaf.bbox_max[1] = 1.0;
    kids[0] = &leaf;
    memset(&root, 0, sizeof(root));
    root.name = "root";
    root.children = kids;
    root.num_children = 1;
    root.bbox_max[0] = root.bbox_max[1] = 1.0;

    PRC_ASSERT_EQ(prc_api_write_prc_buffer(ctx, "slots_model", &root,
        tess, 1, &buf, &buf_size), 0);
    fid = fopen(TEST_MATERIALS_FILENAME, "wb");
    PRC_ASSERT_NOT_NULL(fid);
    PRC_ASSERT_EQ(fwrite(buf, 1, buf_size, fid), buf_size);
    fclose(fid);
    prc_api_write_prc_buffer_free(ctx, buf);

    data = prc_api_open_contents(ctx, TEST_MATERIALS_FILENAME);
    PRC_ASSERT_NOT_NULL(data);
    PRC_ASSERT(prc_api_prep_model_tree(ctx, data, &num_parts, &num_products, &num_markups) >= 0);
    PRC_ASSERT(prc_api_create_model_tree(ctx, data, &model_tree, num_parts, num_products, num_markups) >= 0);
    PRC_ASSERT(prc_api_get_number_tessellations(ctx, data, model_tree, &num_tess,
        &num_line_tess, &num_extra, 0) >= 0);
    PRC_ASSERT_EQ(num_tess, 1);
    tesses = (prc_api_tess *)calloc(num_tess, sizeof(prc_api_tess));
    PRC_ASSERT_NOT_NULL(tesses);
    tesses[0].num_faces = prc_api_get_number_faces(ctx, data, 0);
    PRC_ASSERT(tesses[0].num_faces >= 1);
    tesses[0].tess_faces = (prc_api_face *)calloc(tesses[0].num_faces, sizeof(prc_api_face));
    PRC_ASSERT_NOT_NULL(tesses[0].tess_faces);
    PRC_ASSERT(prc_api_initialize_tessellation(ctx, data, model_tree, 0, &tesses[0], NULL, &has_lines) >= 0);
    PRC_ASSERT(prc_api_get_tessellation_vertices(ctx, data, 0, 0, &tesses[0].tess_faces[0], &tesses[0]) >= 0);
    PRC_ASSERT(prc_api_face_is_material(ctx, &tesses[0], 0));

    memset(out, 0, sizeof(*out));
    prc_api_get_face_material(ctx, &tesses[0], out, 0);

    prc_api_release_data(ctx, data, tesses, num_tess, NULL, 0, NULL, 0, model_tree);
    remove(TEST_MATERIALS_FILENAME);
}

static void
assert_rgb_near(const float got[3], const double want[3])
{
    uint32_t c;

    for (c = 0; c < 3; c++)
        PRC_ASSERT(got[c] > want[c] - 0.01 && got[c] < want[c] + 0.01);
}

/* has_material_slots gives each of the four slots its own colour, and the
   shininess is written as given; without it ambient repeats the diffuse
   colour, specular is a dark grey 0.2, emissive is black, and a shininess of 0
   is written as 0.3. Four unrelated colours, so a slot read from the wrong
   index cannot pass by coincidence. */
static void
test_material_slots(prc_context *ctx)
{
    static const double diffuse[3]  = { 0.8, 0.2, 0.1 };
    static const double ambient[3]  = { 0.1, 0.3, 0.6 };
    static const double specular[3] = { 0.9, 0.9, 0.4 };
    static const double emissive[3] = { 0.0, 0.7, 0.2 };
    static const double black[3]    = { 0.0, 0.0, 0.0 };
    static const double grey[3]     = { 0.2, 0.2, 0.2 };
    prc_api_write_rep_item item;
    prc_api_material m;

    printf("  sub-case: separate ambient, specular and emissive colours\n");

    memset(&item, 0, sizeof(item));
    item.kind = PRC_API_WRITE_RI_SURFACE;
    item.biased_tessellation_index = 1;
    item.has_material = 1;
    memcpy(item.material_color, diffuse, sizeof(diffuse));
    item.material_alpha = 1.0;
    item.material_shininess = 0.5;
    item.has_material_slots = 1;
    memcpy(item.material_ambient, ambient, sizeof(ambient));
    memcpy(item.material_specular, specular, sizeof(specular));
    memcpy(item.material_emissive, emissive, sizeof(emissive));

    write_and_read_face_material(ctx, &item, &m);
    assert_rgb_near(m.diffuse, diffuse);
    assert_rgb_near(m.ambient, ambient);
    assert_rgb_near(m.specular, specular);
    assert_rgb_near(m.emissive, emissive);
    PRC_ASSERT_NEAR(m.shininess, 0.5, 1e-6);

    /* With slots, a shininess of 0 is written as 0. */
    item.material_shininess = 0.0;
    write_and_read_face_material(ctx, &item, &m);
    PRC_ASSERT_NEAR(m.shininess, 0.0, 1e-6);
    item.material_shininess = 0.5;

    printf("  sub-case: without has_material_slots, grey specular and black emissive\n");

    /* The slot colours stay filled in, so this also shows they are ignored
       when the flag is clear. */
    item.has_material_slots = 0;
    write_and_read_face_material(ctx, &item, &m);
    assert_rgb_near(m.diffuse, diffuse);
    assert_rgb_near(m.ambient, diffuse);
    assert_rgb_near(m.specular, grey);
    assert_rgb_near(m.emissive, black);
    PRC_ASSERT_NEAR(m.shininess, 0.5, 1e-6);

    /* Without slots, a shininess left at 0 is written as 0.3. */
    item.material_shininess = 0.0;
    write_and_read_face_material(ctx, &item, &m);
    PRC_ASSERT_NEAR(m.shininess, 0.3, 1e-6);
}

/* An item that asks for no material still gets the shared default, which is
   the behaviour every caller written before materials existed relies on. */
static void
test_default_still_applies(prc_context *ctx)
{
    prc_api_write_tessellation tess[1];
    prc_api_write_rep_item items[1];
    prc_api_write_node leaf, root;
    prc_api_write_node *kids[1];
    uint8_t *buf = NULL;
    size_t buf_size = 0;
    FILE *fid;
    prc_data *data = NULL;
    uint32_t num_parts = 0, num_products = 0, num_markups = 0;

    printf("  sub-case: an item with no material keeps the shared default\n");

    fill_tess(&tess[0]);

    memset(items, 0, sizeof(items));
    items[0].kind = PRC_API_WRITE_RI_SURFACE;
    items[0].biased_tessellation_index = 1;
    /* has_material deliberately left 0 */

    memset(&leaf, 0, sizeof(leaf));
    leaf.rep_items = items;
    leaf.num_rep_items = 1;
    leaf.bbox_max[0] = leaf.bbox_max[1] = 1.0;

    kids[0] = &leaf;
    memset(&root, 0, sizeof(root));
    root.children = kids;
    root.num_children = 1;
    root.bbox_max[0] = root.bbox_max[1] = 1.0;

    PRC_ASSERT_EQ(prc_api_write_prc_buffer(ctx, "default_model", &root,
        tess, 1, &buf, &buf_size), 0);
    PRC_ASSERT_NOT_NULL(buf);

    fid = fopen(TEST_MATERIALS_FILENAME, "wb");
    PRC_ASSERT_NOT_NULL(fid);
    PRC_ASSERT_EQ(fwrite(buf, 1, buf_size, fid), buf_size);
    fclose(fid);
    prc_api_write_prc_buffer_free(ctx, buf);

    data = prc_api_open_contents(ctx, TEST_MATERIALS_FILENAME);
    PRC_ASSERT_NOT_NULL(data);
    PRC_ASSERT_EQ(prc_api_prep_model_tree(ctx, data, &num_parts, &num_products,
        &num_markups), 0);

    {
        const prc_asm_file_structure_globals *g = data->file_struct[0].globals;

        PRC_ASSERT_NOT_NULL(g);
        /* Exactly the one default style, and nothing extra registered. */
        PRC_ASSERT_EQ(g->global_data.style_count, 1);
    }

    prc_api_release_data(ctx, data, NULL, 0, NULL, 0, NULL, 0, NULL);
    remove(TEST_MATERIALS_FILENAME);
}

/* A face must name its item's style, and only when it can do so correctly.

   Until this was added the writer emitted size_of_line_attributes = 0 on every
   face -- "no graphics here, inherit from the owner of the TESS_3D" -- so any
   reader resolving style per face found nothing. Our own public API reported
   is_texture = 0 on files whose texture Acrobat displays, because Acrobat
   resolves the part-level style instead and that masked it.

   The negative half matters as much as the positive one: a tessellation shared
   by two items cannot carry either one's style, because the single face record
   would then be wrong for the other. That case must still write 0. */
static void
test_face_names_its_style(prc_context *ctx)
{
    prc_api_write_tessellation tess[1];
    prc_api_write_rep_item items[1];
    prc_api_write_node leaf, root;
    prc_api_write_node *kids[1];
    uint8_t *buf = NULL;
    size_t buf_size = 0;
    FILE *fid;
    prc_data *data = NULL;
    uint32_t num_parts = 0, num_products = 0, num_markups = 0;

    printf("  sub-case: a face names the style of its one styled item\n");

    fill_tess(&tess[0]);

    memset(items, 0, sizeof(items));
    items[0].kind = PRC_API_WRITE_RI_SURFACE;
    items[0].biased_tessellation_index = 1;
    items[0].has_material = 1;
    items[0].material_color[0] = 1.0;
    items[0].material_alpha = 1.0;
    items[0].material_shininess = 0.5;

    memset(&leaf, 0, sizeof(leaf));
    leaf.rep_items = items;
    leaf.num_rep_items = 1;
    leaf.bbox_max[0] = leaf.bbox_max[1] = 1.0;

    kids[0] = &leaf;
    memset(&root, 0, sizeof(root));
    root.children = kids;
    root.num_children = 1;
    root.bbox_max[0] = root.bbox_max[1] = 1.0;

    PRC_ASSERT_EQ(prc_api_write_prc_buffer(ctx, "face_style_model", &root,
        tess, 1, &buf, &buf_size), 0);
    PRC_ASSERT_NOT_NULL(buf);

    fid = fopen(TEST_MATERIALS_FILENAME, "wb");
    PRC_ASSERT_NOT_NULL(fid);
    PRC_ASSERT_EQ(fwrite(buf, 1, buf_size, fid), buf_size);
    fclose(fid);
    prc_api_write_prc_buffer_free(ctx, buf);

    data = prc_api_open_contents(ctx, TEST_MATERIALS_FILENAME);
    PRC_ASSERT_NOT_NULL(data);
    PRC_ASSERT_EQ(prc_api_prep_model_tree(ctx, data, &num_parts, &num_products,
        &num_markups), 0);

    {
        const prc_tess_3d *t3;
        const prc_tess_face *fc;

        PRC_ASSERT_NOT_NULL(data->file_struct[0].tessellation);
        PRC_ASSERT(data->file_struct[0].tessellation->tess_count > 0);
        t3 = data->file_struct[0].tessellation->tess[0].tess_3d;
        PRC_ASSERT_NOT_NULL(t3);
        PRC_ASSERT(t3->number_of_face_tessellation > 0);
        fc = &t3->face_tessellation_data[0];

        /* Table 140: 1 is "one graphic associated with the whole face
           tessellation data", and the entry is (index_of_line_style + 1), so a
           real style is >= 1 and 0 would mean none. */
        PRC_ASSERT_EQ(fc->size_of_line_attributes, 1);
        PRC_ASSERT_NOT_NULL(fc->line_attributes);
        PRC_ASSERT(fc->line_attributes[0] > 0);
        /* 7.8.6.1 points at behavior_bit_field (Table 34); PRC_GRAPHICS_Show
           is what prc_write_tree.c writes for the owning entity. */
        PRC_ASSERT_EQ(fc->behavior, (uint32_t)PRC_GRAPHICS_Show);
    }

    prc_api_release_data(ctx, data, NULL, 0, NULL, 0, NULL, 0, NULL);
    remove(TEST_MATERIALS_FILENAME);
}

/* Two items sharing one tessellation: neither style can be baked into the
   single face record, so it must stay at 0 and both items keep resolving
   through their own item style as before. */
static void
test_shared_tessellation_names_no_style(prc_context *ctx)
{
    prc_api_write_tessellation tess[1];
    prc_api_write_rep_item items_a[1];
    prc_api_write_rep_item items_b[1];
    prc_api_write_node leaf_a, leaf_b, root;
    prc_api_write_node *kids[2];
    uint8_t *buf = NULL;
    size_t buf_size = 0;
    FILE *fid;
    prc_data *data = NULL;
    uint32_t num_parts = 0, num_products = 0, num_markups = 0;

    printf("  sub-case: a tessellation shared by two items names no style\n");

    fill_tess(&tess[0]);

    memset(items_a, 0, sizeof(items_a));
    items_a[0].kind = PRC_API_WRITE_RI_SURFACE;
    items_a[0].biased_tessellation_index = 1;
    items_a[0].has_material = 1;
    items_a[0].material_color[0] = 1.0;
    items_a[0].material_alpha = 1.0;

    memset(items_b, 0, sizeof(items_b));
    items_b[0].kind = PRC_API_WRITE_RI_SURFACE;
    items_b[0].biased_tessellation_index = 1;   /* the SAME tessellation */
    items_b[0].has_material = 1;
    items_b[0].material_color[2] = 1.0;
    items_b[0].material_alpha = 1.0;

    memset(&leaf_a, 0, sizeof(leaf_a));
    leaf_a.rep_items = items_a;
    leaf_a.num_rep_items = 1;
    leaf_a.bbox_max[0] = leaf_a.bbox_max[1] = 1.0;

    memset(&leaf_b, 0, sizeof(leaf_b));
    leaf_b.rep_items = items_b;
    leaf_b.num_rep_items = 1;
    leaf_b.bbox_max[0] = leaf_b.bbox_max[1] = 1.0;

    kids[0] = &leaf_a;
    kids[1] = &leaf_b;
    memset(&root, 0, sizeof(root));
    root.children = kids;
    root.num_children = 2;
    root.bbox_max[0] = root.bbox_max[1] = 1.0;

    PRC_ASSERT_EQ(prc_api_write_prc_buffer(ctx, "shared_tess_model", &root,
        tess, 1, &buf, &buf_size), 0);
    PRC_ASSERT_NOT_NULL(buf);

    fid = fopen(TEST_MATERIALS_FILENAME, "wb");
    PRC_ASSERT_NOT_NULL(fid);
    PRC_ASSERT_EQ(fwrite(buf, 1, buf_size, fid), buf_size);
    fclose(fid);
    prc_api_write_prc_buffer_free(ctx, buf);

    data = prc_api_open_contents(ctx, TEST_MATERIALS_FILENAME);
    PRC_ASSERT_NOT_NULL(data);
    PRC_ASSERT_EQ(prc_api_prep_model_tree(ctx, data, &num_parts, &num_products,
        &num_markups), 0);

    {
        const prc_tess_3d *t3 = data->file_struct[0].tessellation->tess[0].tess_3d;

        PRC_ASSERT_NOT_NULL(t3);
        PRC_ASSERT(t3->number_of_face_tessellation > 0);
        PRC_ASSERT_EQ(t3->face_tessellation_data[0].size_of_line_attributes, 0);
    }

    prc_api_release_data(ctx, data, NULL, 0, NULL, 0, NULL, 0, NULL);
    remove(TEST_MATERIALS_FILENAME);
}

/* Writes one leaf per item, each over its own copy of the one-triangle
   tessellation with texture coordinates, to TEST_MATERIALS_FILENAME. Returns
   the writer's result; the file is written only on success. */
static int
write_textured_items(prc_context *ctx, const char *name, prc_api_write_rep_item *items, uint32_t n)
{
    static const double uvs[3 * 2] = { 0.0, 0.0,  1.0, 0.0,  0.0, 1.0 };
    prc_api_write_tessellation tess[3];
    prc_api_write_node leaves[3], root;
    prc_api_write_node *kids[3];
    uint8_t *buf = NULL;
    size_t buf_size = 0;
    FILE *fid;
    uint32_t k;
    int rc;

    PRC_ASSERT(n >= 1 && n <= 3);
    for (k = 0; k < n; k++)
    {
        fill_tess(&tess[k]);
        tess[k].tex_coords = uvs;
        tess[k].num_tex_coords = 3;
        tess[k].tex_indices = tri_indices;
        items[k].kind = PRC_API_WRITE_RI_SURFACE;
        items[k].biased_tessellation_index = k + 1;

        memset(&leaves[k], 0, sizeof(leaves[k]));
        leaves[k].rep_items = &items[k];
        leaves[k].num_rep_items = 1;
        leaves[k].bbox_max[0] = leaves[k].bbox_max[1] = 1.0;
        kids[k] = &leaves[k];
    }
    memset(&root, 0, sizeof(root));
    root.name = "root";
    root.children = kids;
    root.num_children = n;
    root.bbox_max[0] = root.bbox_max[1] = 1.0;

    rc = prc_api_write_prc_buffer(ctx, name, &root, tess, n, &buf, &buf_size);
    if (rc != 0)
        return rc;
    fid = fopen(TEST_MATERIALS_FILENAME, "wb");
    PRC_ASSERT_NOT_NULL(fid);
    PRC_ASSERT_EQ(fwrite(buf, 1, buf_size, fid), buf_size);
    fclose(fid);
    prc_api_write_prc_buffer_free(ctx, buf);
    return 0;
}

/* One texture layer as the file holds it, followed from an application. */
typedef struct
{
    uint32_t attributes;
    uint32_t num_intensities;
    double   intensity;
    uint32_t num_components;
    uint32_t components;
    uint8_t  first_byte;   /* of the stored image, to tell the images apart */
    uint32_t wrap_s;
    uint32_t material;     /* the application's biased_material_generic_index */
} layer_seen;

/* Follows one item from its style through every texture application in the
   chain, and returns how many layers it found (at most max). */
static uint32_t
follow_layers(const prc_data *data, const prc_ri *ri, layer_seen *out, uint32_t max)
{
    const prc_file_struct_internal_global_data *gd = &data->file_struct[0].globals->global_data;
    const prc_graph_style *st;
    uint32_t si, mi, n = 0;

    si = ri->ri_poly_brep_model->item_content.base.graphics_content.biased_index_of_line_style;
    PRC_ASSERT(si >= 1 && si <= gd->style_count);
    st = &gd->styles[si - 1];
    PRC_ASSERT(st->is_material);
    mi = st->biased_color_index;
    while (mi != 0)
    {
        const prc_graph_material *app;
        const prc_graph_texture_definition *def;
        const prc_graph_picture *pic;
        const prc_uncomp_block *blk;

        PRC_ASSERT(n < max);
        PRC_ASSERT(mi <= gd->material_count);
        app = &gd->materials[mi - 1];
        PRC_ASSERT_EQ(app->tag, PRC_TYPE_GRAPH_TextureApplication);
        PRC_ASSERT_EQ(app->biased_uv_coordinates_index, 1);
        PRC_ASSERT(app->biased_texture_definition_index >= 1 &&
                   app->biased_texture_definition_index <= gd->texture_count);
        def = &gd->textures[app->biased_texture_definition_index - 1];
        PRC_ASSERT(def->biased_picture_index >= 1 && def->biased_picture_index <= gd->picture_count);
        pic = &gd->pictures[def->biased_picture_index - 1];
        PRC_ASSERT(pic->biased_uncompressed_file_index >= 1 &&
                   pic->biased_uncompressed_file_index <= data->file_struct[0].header->file_count);
        blk = &data->file_struct[0].header->files[pic->biased_uncompressed_file_index - 1];

        out[n].attributes = (uint32_t)def->texture_mapping_attributes;
        out[n].num_intensities = def->number_texture_mapping_attributes_intensities;
        out[n].intensity = out[n].num_intensities ? def->texture_mapping_attributes_intensities[0] : -1.0;
        out[n].num_components = def->number_of_texture_mapping_attributes_components;
        out[n].components = out[n].num_components ? def->texture_mapping_attributes_components[0] : 0xFFu;
        /* the image's top row, stored second of the two raw rows, inside the
           zlib stream the raw pixels are stored as */
        {
            uint8_t px[64];
            uLongf len = sizeof(px);
            PRC_ASSERT_EQ(uncompress(px, &len, blk->block, blk->block_size), Z_OK);
            out[n].first_byte = px[len / 2];
        }
        out[n].wrap_s = (uint32_t)def->texture_wrapping_mode_s;
        out[n].material = app->biased_material_generic_index;
        n++;
        mi = app->biased_next_texture_index;
    }
    return n;
}

/* Three items: a diffuse image followed by an opacity mask and a reflection,
   a reflection on its own, and a plain diffuse image. Each layer is checked
   for its kind, intensity, components and image, in order, and for sharing
   the item's material and wrap mode. The plain item must be written exactly
   as before layers existed: no attribute and empty arrays.

   The three images differ in their first byte, so a chain written in the
   wrong order or pointing at the wrong picture fails. The reflection layer's
   intensity is not 1, so a writer that dropped the intensity, or wrote the
   main texture's, fails too. */
static void
test_texture_layers(prc_context *ctx)
{
    static const uint8_t image_x[2 * 2 * 3] = {
        0x11, 0x00, 0x00,   0x00, 0xFF, 0x00,
        0x00, 0x00, 0xFF,   0xFF, 0xFF, 0x00
    };
    static const uint8_t image_m[2 * 2 * 3] = {
        0x22, 0x22, 0x22,   0x80, 0x80, 0x80,
        0xFF, 0xFF, 0xFF,   0x00, 0x00, 0x00
    };
    static const uint8_t image_e[2 * 2 * 3] = {
        0x33, 0x66, 0xCC,   0x33, 0x66, 0xCC,
        0x66, 0x44, 0x22,   0x66, 0x44, 0x22
    };
    prc_api_write_texture_layer layers[2];
    prc_api_write_rep_item items[3];
    prc_data *data;
    const prc_asm_file_structure_tree *tree;
    layer_seen seen[4];
    uint32_t found = 0, i, k, n;

    printf("  sub-case: texture layers chain in order with their kinds\n");

    memset(layers, 0, sizeof(layers));
    layers[0].kind = PRC_API_WRITE_TEXTURE_KIND_OPACITY;
    layers[0].image = image_m;
    layers[0].image_size = sizeof(image_m);
    layers[0].format = PRC_API_WRITE_TEXTURE_RGB;
    layers[0].width = layers[0].height = 2;
    layers[1].kind = PRC_API_WRITE_TEXTURE_KIND_SPHERICAL_REFLECTION;
    layers[1].image = image_e;
    layers[1].image_size = sizeof(image_e);
    layers[1].format = PRC_API_WRITE_TEXTURE_RGB;
    layers[1].width = layers[1].height = 2;
    layers[1].intensity = 0.25;

    memset(items, 0, sizeof(items));
    for (k = 0; k < 3; k++)
    {
        items[k].has_material = 1;
        items[k].material_color[0] = items[k].material_color[1] = items[k].material_color[2] = 1.0;
        items[k].material_alpha = 1.0;
        items[k].has_texture = 1;
        items[k].texture_format = PRC_API_WRITE_TEXTURE_RGB;
        items[k].texture_width = items[k].texture_height = 2;
        items[k].texture_image_size = sizeof(image_x);
    }
    items[0].texture_image = image_x;
    items[0].texture_wrap_u = PRC_API_WRITE_TEXTURE_WRAP_CLAMP_TO_BORDER;
    items[0].texture_layers = layers;
    items[0].num_texture_layers = 2;
    items[1].texture_image = image_e;
    items[1].texture_kind = PRC_API_WRITE_TEXTURE_KIND_SPHERICAL_REFLECTION;
    items[1].texture_intensity = 0.5;
    items[2].texture_image = image_x;

    PRC_ASSERT_EQ(write_textured_items(ctx, "texture_layers", items, 3), 0);
    data = prc_api_open_contents(ctx, TEST_MATERIALS_FILENAME);
    if (data == NULL)
        prc_api_print_error_stack(ctx);
    PRC_ASSERT_NOT_NULL(data);

    tree = data->file_struct[0].tree;
    PRC_ASSERT_NOT_NULL(tree);
    for (i = 0; i < tree->parts_count; i++)
    {
        const prc_asm_parts_definition *part = &tree->parts[i];
        uint32_t j;

        for (j = 0; j < part->num_rep_items; j++)
        {
            const prc_ri *ri = &part->rep_items[j];

            if (ri->ri_poly_brep_model == NULL)
                continue;
            n = follow_layers(data, ri, seen, 4);
            if (found == 0)
            {
                PRC_ASSERT_EQ(n, 3);
                /* diffuse, marked because layers follow */
                PRC_ASSERT_EQ(seen[0].attributes, 0x1u);
                PRC_ASSERT_EQ(seen[0].num_intensities, 1);
                PRC_ASSERT_NEAR(seen[0].intensity, 1.0, 1e-12);
                PRC_ASSERT_EQ(seen[0].components, 0x7u);
                PRC_ASSERT_EQ(seen[0].first_byte, 0x11);
                /* opacity mask, intensity 0 read as 1 */
                PRC_ASSERT_EQ(seen[1].attributes, 0x4u);
                PRC_ASSERT_EQ(seen[1].num_intensities, 1);
                PRC_ASSERT_NEAR(seen[1].intensity, 1.0, 1e-12);
                PRC_ASSERT_EQ(seen[1].num_components, 1);
                PRC_ASSERT_EQ(seen[1].components, 0x8u);
                PRC_ASSERT_EQ(seen[1].first_byte, 0x22);
                /* reflection */
                PRC_ASSERT_EQ(seen[2].attributes, 0x8u);
                PRC_ASSERT_NEAR(seen[2].intensity, 0.25, 1e-12);
                PRC_ASSERT_EQ(seen[2].components, 0x7u);
                PRC_ASSERT_EQ(seen[2].first_byte, 0x33);
                for (k = 0; k < 3; k++)
                {
                    PRC_ASSERT_EQ(seen[k].wrap_s, (uint32_t)PRC_texture_wrapping_clamp_to_border);
                    PRC_ASSERT_EQ(seen[k].material, seen[0].material);
                }
            }
            else if (found == 1)
            {
                PRC_ASSERT_EQ(n, 1);
                PRC_ASSERT_EQ(seen[0].attributes, 0x8u);
                PRC_ASSERT_NEAR(seen[0].intensity, 0.5, 1e-12);
                PRC_ASSERT_EQ(seen[0].components, 0x7u);
                PRC_ASSERT_EQ(seen[0].first_byte, 0x33);
            }
            else
            {
                PRC_ASSERT_EQ(n, 1);
                PRC_ASSERT_EQ(seen[0].attributes, 0u);
                PRC_ASSERT_EQ(seen[0].num_intensities, 0);
                PRC_ASSERT_EQ(seen[0].num_components, 0);
                PRC_ASSERT_EQ(seen[0].first_byte, 0x11);
                PRC_ASSERT_EQ(seen[0].wrap_s, (uint32_t)PRC_texture_wrapping_repeat);
            }
            found++;
        }
    }
    PRC_ASSERT_EQ(found, 3);
    prc_api_release_data(ctx, (prc_api_data)data, NULL, 0, NULL, 0, NULL, 0, NULL);
    remove(TEST_MATERIALS_FILENAME);

    printf("  sub-case: invalid texture layers are refused\n");
    items[0].has_texture = 0;
    PRC_ASSERT(write_textured_items(ctx, "layers_no_texture", items, 1) != 0);
    items[0].has_texture = 1;
    items[0].texture_layers = NULL;
    PRC_ASSERT(write_textured_items(ctx, "layers_null", items, 1) != 0);
    items[0].texture_layers = layers;
    layers[1].intensity = 1.5;
    PRC_ASSERT(write_textured_items(ctx, "layers_intensity", items, 1) != 0);
    layers[1].intensity = 0.25;
    layers[0].kind = 3;
    PRC_ASSERT(write_textured_items(ctx, "layers_kind", items, 1) != 0);
    layers[0].kind = PRC_API_WRITE_TEXTURE_KIND_OPACITY;
    layers[0].image = NULL;
    PRC_ASSERT(write_textured_items(ctx, "layers_no_image", items, 1) != 0);
    layers[0].image = image_m;
    items[0].texture_kind = 3;
    PRC_ASSERT(write_textured_items(ctx, "main_kind", items, 1) != 0);
    items[0].texture_kind = 0;
    PRC_ASSERT_EQ(write_textured_items(ctx, "layers_ok_again", items, 1), 0);
    remove(TEST_MATERIALS_FILENAME);
}

/* Four parts: two red items sharing one tessellation, then a green and a
   blue item. A tessellation used by two items names no style on its faces, so
   the red faces take their colour from the walk up the product tree, which is
   what this test is about. */
static void
write_herit_file(prc_context *ctx)
{
    static const double rgb[3][3] = { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
    static const uint32_t tess_of[4] = { 1, 1, 2, 3 };
    static const int colour_of[4] = { 0, 0, 1, 2 };
    prc_api_write_tessellation tess[3];
    prc_api_write_rep_item items[4];
    prc_api_write_node leaves[4], root;
    prc_api_write_node *kids[4];
    uint8_t *buf = NULL;
    size_t buf_size = 0;
    FILE *fid;
    uint32_t k, c;

    for (k = 0; k < 3; k++)
        fill_tess(&tess[k]);
    memset(items, 0, sizeof(items));
    for (k = 0; k < 4; k++)
    {
        items[k].kind = PRC_API_WRITE_RI_SURFACE;
        items[k].biased_tessellation_index = tess_of[k];
        items[k].has_material = 1;
        for (c = 0; c < 3; c++)
            items[k].material_color[c] = rgb[colour_of[k]][c];
        items[k].material_alpha = 1.0;
        memset(&leaves[k], 0, sizeof(leaves[k]));
        leaves[k].rep_items = &items[k];
        leaves[k].num_rep_items = 1;
        leaves[k].bbox_max[0] = leaves[k].bbox_max[1] = 1.0;
        kids[k] = &leaves[k];
    }
    memset(&root, 0, sizeof(root));
    root.name = "root";
    root.children = kids;
    root.num_children = 4;
    root.bbox_max[0] = root.bbox_max[1] = 1.0;

    PRC_ASSERT_EQ(prc_api_write_prc_buffer(ctx, "parent_herit", &root, tess, 3, &buf, &buf_size), 0);
    fid = fopen(TEST_MATERIALS_FILENAME, "wb");
    PRC_ASSERT_NOT_NULL(fid);
    PRC_ASSERT_EQ(fwrite(buf, 1, buf_size, fid), buf_size);
    fclose(fid);
    prc_api_write_prc_buffer_free(ctx, buf);
}

/* Reads the first red item's face colour after giving its ancestors styles
   in memory, as a file could give them:
     which 0: as written (every ancestor has the writer's default style, no flags);
     which 1: the root styled blue, with ParentHeritColor;
     which 2: the root blue and the node below it green, both with the flag;
     which 3: the root styled blue, without the flag.
   Also returns the green and blue items' colours, to show which is which. */
static void
herit_case(prc_context *ctx, int which, float red_out[3], float green_out[3], float blue_out[3])
{
    prc_api_data data;
    prc_api_product *model_tree;
    prc_api_tess *tesses;
    prc_api_object_style *chain[16];
    prc_api_object_style *node;
    uint32_t n = 0, k, green_style, blue_style;
    uint32_t num_parts = 0, num_products = 0, num_markups = 0;
    uint32_t num_tess = 0, num_line_tess = 0, num_extra = 0;
    uint8_t has_lines = 0;
    prc_api_material m;

    data = prc_api_open_contents(ctx, TEST_MATERIALS_FILENAME);
    PRC_ASSERT_NOT_NULL(data);
    model_tree = NULL;
    PRC_ASSERT(prc_api_prep_model_tree(ctx, data, &num_parts, &num_products, &num_markups) >= 0);
    PRC_ASSERT(prc_api_create_model_tree(ctx, data, &model_tree, num_parts, num_products, num_markups) >= 0);
    PRC_ASSERT(prc_api_get_number_tessellations(ctx, data, model_tree, &num_tess,
        &num_line_tess, &num_extra, 0) >= 0);
    PRC_ASSERT_EQ(num_tess, 4);
    tesses = (prc_api_tess *)calloc(num_tess, sizeof(prc_api_tess));
    PRC_ASSERT_NOT_NULL(tesses);
    for (k = 0; k < 4; k++)
    {
        tesses[k].num_faces = prc_api_get_number_faces(ctx, data, k);
        PRC_ASSERT(tesses[k].num_faces >= 1);
        tesses[k].tess_faces = (prc_api_face *)calloc(tesses[k].num_faces, sizeof(prc_api_face));
        PRC_ASSERT_NOT_NULL(tesses[k].tess_faces);
        PRC_ASSERT(prc_api_initialize_tessellation(ctx, data, model_tree, k, &tesses[k], NULL, &has_lines) >= 0);
        PRC_ASSERT_NOT_NULL(tesses[k].style_leaf);
    }
    green_style = ((prc_api_object_style *)tesses[2].style_leaf)->base_with_graphics->graphics_content.biased_index_of_line_style;
    blue_style = ((prc_api_object_style *)tesses[3].style_leaf)->base_with_graphics->graphics_content.biased_index_of_line_style;
    PRC_ASSERT(green_style > 0 && blue_style > 0 && green_style != blue_style);

    /* The red item's node and its ancestors, leaf first. The writer gives
       every node above the item its default style, without flags, so in case
       0 the ancestors are styled already and the item's own style must win. */
    for (node = (prc_api_object_style *)tesses[0].style_leaf; node != NULL; node = node->parent)
    {
        PRC_ASSERT(n < 16);
        chain[n++] = node;
    }
    PRC_ASSERT(n >= 3);
    PRC_ASSERT(chain[0]->base_with_graphics->graphics_content.biased_index_of_line_style > 0);
    for (k = 0; k < n; k++)
        PRC_ASSERT_EQ(chain[k]->base_with_graphics->graphics_content.behavior_bit_field1 &
                      PRC_GRAPHICS_ParentHeritColor, 0);

    if (which >= 1)
    {
        chain[n - 1]->base_with_graphics->graphics_content.biased_index_of_line_style = blue_style;
        if (which != 3)
            chain[n - 1]->base_with_graphics->graphics_content.behavior_bit_field1 |= PRC_GRAPHICS_ParentHeritColor;
    }
    if (which == 2)
    {
        chain[n - 2]->base_with_graphics->graphics_content.biased_index_of_line_style = green_style;
        chain[n - 2]->base_with_graphics->graphics_content.behavior_bit_field1 |= PRC_GRAPHICS_ParentHeritColor;
    }

    for (k = 0; k < 4; k++)
    {
        float *out = k == 0 ? red_out : k == 2 ? green_out : k == 3 ? blue_out : NULL;

        if (out == NULL)
            continue;
        PRC_ASSERT(prc_api_get_tessellation_vertices(ctx, data, k, 0, &tesses[k].tess_faces[0], &tesses[k]) >= 0);
        memset(&m, 0, sizeof(m));
        prc_api_get_face_material(ctx, &tesses[k], &m, 0);
        memcpy(out, m.diffuse, sizeof(float) * 3);
    }
    prc_api_release_data(ctx, data, tesses, 4, NULL, 0, NULL, 0, model_tree);
}

/* Colour inheritance down the product tree (ISO 14739-1, GraphicsContent
   behavior_bit_field):
   - no ancestor flagged: the item's own red;
   - the root styled blue with ParentHeritColor: blue;
   - the root blue and the node below it green, both flagged: blue, the
     highest flagged ancestor;
   - the root styled blue without the flag: the item's own red.
   The last case is what the rule before nanoPRC #143 got wrong (it took the
   topmost style); the second and third are what #143 alone gets wrong (it
   kept the lowest style found). */
static void
test_parent_herit_colour(prc_context *ctx)
{
    static const float red[3] = { 1.0f, 0.0f, 0.0f };
    static const float green[3] = { 0.0f, 1.0f, 0.0f };
    static const float blue[3] = { 0.0f, 0.0f, 1.0f };
    const float *expect[4];
    float r[3], g[3], b[3];
    int k, c;

    printf("  sub-case: an ancestor with ParentHeritColor overrides the item's colour\n");
    expect[0] = red;
    expect[1] = blue;
    expect[2] = blue;
    expect[3] = red;
    write_herit_file(ctx);
    for (k = 0; k < 4; k++)
    {
        herit_case(ctx, k, r, g, b);
        for (c = 0; c < 3; c++)
        {
            PRC_ASSERT_NEAR(g[c], green[c], 0.01);
            PRC_ASSERT_NEAR(b[c], blue[c], 0.01);
            PRC_ASSERT_NEAR(r[c], expect[k][c], 0.01);
        }
    }
    remove(TEST_MATERIALS_FILENAME);
}

/* Opens the file written by write_textured_items and reads the first face of
   tessellation tess_index. Returns the get_tessellation_vertices code. */
static int
read_first_face(prc_context *ctx, prc_api_data data, prc_api_product *model_tree,
    prc_api_tess *tesses, uint32_t tess_index)
{
    uint8_t has_lines = 0;

    tesses[tess_index].num_faces = prc_api_get_number_faces(ctx, data, tess_index);
    PRC_ASSERT(tesses[tess_index].num_faces >= 1);
    tesses[tess_index].tess_faces = (prc_api_face *)calloc(tesses[tess_index].num_faces, sizeof(prc_api_face));
    PRC_ASSERT_NOT_NULL(tesses[tess_index].tess_faces);
    PRC_ASSERT(prc_api_initialize_tessellation(ctx, data, model_tree, tess_index,
        &tesses[tess_index], NULL, &has_lines) >= 0);
    return prc_api_get_tessellation_vertices(ctx, data, tess_index, 0,
        &tesses[tess_index].tess_faces[0], &tesses[tess_index]);
}

static void
open_written_items(prc_context *ctx, prc_api_data *data, prc_api_product **model_tree,
    prc_api_tess **tesses, uint32_t want_tess)
{
    uint32_t num_parts = 0, num_products = 0, num_markups = 0;
    uint32_t num_tess = 0, num_line_tess = 0, num_extra = 0;

    *data = prc_api_open_contents(ctx, TEST_MATERIALS_FILENAME);
    PRC_ASSERT_NOT_NULL(*data);
    *model_tree = NULL;
    PRC_ASSERT(prc_api_prep_model_tree(ctx, *data, &num_parts, &num_products, &num_markups) >= 0);
    PRC_ASSERT(prc_api_create_model_tree(ctx, *data, model_tree, num_parts, num_products, num_markups) >= 0);
    PRC_ASSERT(prc_api_get_number_tessellations(ctx, *data, *model_tree, &num_tess,
        &num_line_tess, &num_extra, 0) >= 0);
    PRC_ASSERT_EQ(num_tess, want_tess);
    *tesses = (prc_api_tess *)calloc(num_tess, sizeof(prc_api_tess));
    PRC_ASSERT_NOT_NULL(*tesses);
}

/* The same three items as test_texture_layers, read back through the public
   API: each face's texture gives its kind, intensity and components, says
   how many layers follow, and prc_api_get_texture_layer returns them in
   order. Each image is told apart by its first byte, so a layer returned out
   of order, or the main texture returned again, fails.

   Then the written chain is damaged in memory, as a hostile file could: one
   application names itself as the next, and another names a material past
   the end of the table. Each face must be refused, not followed for ever or
   read out of bounds. */
static void
test_texture_layers_read(prc_context *ctx)
{
    static const uint8_t png_x[77] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
        0x08, 0x02, 0x00, 0x00, 0x00, 0xFD, 0xD4, 0x9A, 0x73, 0x00, 0x00, 0x00,
        0x14, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0x10, 0x64, 0x60, 0x60,
        0xF8, 0xCF, 0x00, 0x22, 0xFE, 0xFF, 0x67, 0x00, 0x00, 0x12, 0xD9, 0x04,
        0x0E, 0x95, 0x16, 0xD8, 0x58, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E,
        0x44, 0xAE, 0x42, 0x60, 0x82
    };
    static const uint8_t png_m[71] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
        0x08, 0x00, 0x00, 0x00, 0x00, 0x57, 0xDD, 0x52, 0xF8, 0x00, 0x00, 0x00,
        0x0E, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0x50, 0x6A, 0x60, 0xF8,
        0xCF, 0x00, 0x00, 0x04, 0xAE, 0x01, 0xA2, 0x13, 0xE9, 0x58, 0x16, 0x00,
        0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
    };
    static const uint8_t png_e[76] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
        0x08, 0x02, 0x00, 0x00, 0x00, 0xFD, 0xD4, 0x9A, 0x73, 0x00, 0x00, 0x00,
        0x13, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0x30, 0x4E, 0x3B, 0x03,
        0x44, 0x0C, 0x69, 0x2E, 0x4A, 0x40, 0x04, 0x00, 0x22, 0x41, 0x04, 0x63,
        0x8E, 0x0E, 0x74, 0x5A, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44,
        0xAE, 0x42, 0x60, 0x82
    };
    prc_api_write_texture_layer layers[2];
    prc_api_write_rep_item items[3];
    prc_api_data data;
    prc_api_product *model_tree;
    prc_api_tess *tesses;
    const prc_api_texture *t;
    prc_api_texture layer, after;
    prc_file_struct_internal_global_data *gd;
    uint32_t k, m, damaged;

    printf("  sub-case: texture layers read back with their kinds\n");

    memset(layers, 0, sizeof(layers));
    layers[0].kind = PRC_API_WRITE_TEXTURE_KIND_OPACITY;
    layers[0].image = png_m;
    layers[0].image_size = sizeof(png_m);
    layers[0].format = PRC_API_WRITE_TEXTURE_PNG;
    layers[1].kind = PRC_API_WRITE_TEXTURE_KIND_SPHERICAL_REFLECTION;
    layers[1].image = png_e;
    layers[1].image_size = sizeof(png_e);
    layers[1].format = PRC_API_WRITE_TEXTURE_PNG;
    layers[1].intensity = 0.25;

    memset(items, 0, sizeof(items));
    for (k = 0; k < 3; k++)
    {
        items[k].has_material = 1;
        items[k].material_color[0] = items[k].material_color[1] = items[k].material_color[2] = 1.0;
        items[k].material_alpha = 1.0;
        items[k].has_texture = 1;
        items[k].texture_format = PRC_API_WRITE_TEXTURE_PNG;
    }
    items[0].texture_image = png_x;
    items[0].texture_image_size = sizeof(png_x);
    items[0].texture_layers = layers;
    items[0].num_texture_layers = 2;
    items[1].texture_image = png_e;
    items[1].texture_image_size = sizeof(png_e);
    items[1].texture_kind = PRC_API_WRITE_TEXTURE_KIND_SPHERICAL_REFLECTION;
    items[1].texture_intensity = 0.5;
    items[2].texture_image = png_x;
    items[2].texture_image_size = sizeof(png_x);

    PRC_ASSERT_EQ(write_textured_items(ctx, "texture_layers_read", items, 3), 0);
    open_written_items(ctx, &data, &model_tree, &tesses, 3);
    for (k = 0; k < 3; k++)
    {
        PRC_ASSERT(read_first_face(ctx, data, model_tree, tesses, k) >= 0);
        PRC_ASSERT(tesses[k].tess_faces[0].is_texture);
    }

    /* A diffuse image, then an opacity mask, then a reflection at 0.25. */
    t = &tesses[0].tess_faces[0].texture;
    PRC_ASSERT_EQ(t->kind, (uint32_t)PRC_API_TEXTURE_KIND_DIFFUSE);
    PRC_ASSERT_EQ(t->mapping_attributes, 0x1u);
    PRC_ASSERT_NEAR(t->intensity, 1.0, 1e-12);
    PRC_ASSERT_EQ(t->components, 0x7);
    PRC_ASSERT_NOT_NULL(t->data);
    PRC_ASSERT_EQ(t->data[0], 0x11);
    PRC_ASSERT_EQ(t->num_layers, 2);

    PRC_ASSERT_EQ(prc_api_get_texture_layer(ctx, t, 0, &layer), 0);
    PRC_ASSERT_EQ(layer.kind, (uint32_t)PRC_API_TEXTURE_KIND_OPACITY);
    PRC_ASSERT_EQ(layer.mapping_attributes, 0x4u);
    PRC_ASSERT_NEAR(layer.intensity, 1.0, 1e-12);
    PRC_ASSERT_EQ(layer.components, 0x8);
    PRC_ASSERT_NOT_NULL(layer.data);
    PRC_ASSERT_EQ(layer.data[0], 0x22);
    PRC_ASSERT_EQ(layer.width, 2);
    PRC_ASSERT_EQ(layer.height, 2);
    PRC_ASSERT_EQ(layer.num_channels, 1);
    PRC_ASSERT_EQ(layer.num_layers, 1);

    /* The second layer, reached both from the face and from the first layer. */
    PRC_ASSERT_EQ(prc_api_get_texture_layer(ctx, t, 1, &layer), 0);
    PRC_ASSERT_EQ(prc_api_get_texture_layer(ctx, &layer, 0, &after), PRC_API_ERROR_PARAMETER);
    PRC_ASSERT_EQ(layer.kind, (uint32_t)PRC_API_TEXTURE_KIND_SPHERICAL_REFLECTION);
    PRC_ASSERT_EQ(layer.mapping_attributes, 0x8u);
    PRC_ASSERT_NEAR(layer.intensity, 0.25, 1e-12);
    PRC_ASSERT_EQ(layer.components, 0x7);
    PRC_ASSERT_EQ(layer.data[0], 0x33);
    PRC_ASSERT_EQ(layer.num_layers, 0);
    PRC_ASSERT_EQ(prc_api_get_texture_layer(ctx, t, 0, &layer), 0);
    PRC_ASSERT_EQ(prc_api_get_texture_layer(ctx, &layer, 0, &after), 0);
    PRC_ASSERT_EQ(after.data[0], 0x33);

    PRC_ASSERT_EQ(prc_api_get_texture_layer(ctx, t, 2, &layer), PRC_API_ERROR_PARAMETER);
    PRC_ASSERT_EQ(prc_api_get_texture_layer(ctx, NULL, 0, &layer), PRC_API_ERROR_PARAMETER);
    PRC_ASSERT_EQ(prc_api_get_texture_layer(ctx, t, 0, NULL), PRC_API_ERROR_PARAMETER);

    /* A reflection on its own. */
    t = &tesses[1].tess_faces[0].texture;
    PRC_ASSERT_EQ(t->kind, (uint32_t)PRC_API_TEXTURE_KIND_SPHERICAL_REFLECTION);
    PRC_ASSERT_NEAR(t->intensity, 0.5, 1e-12);
    PRC_ASSERT_EQ(t->components, 0x7);
    PRC_ASSERT_EQ(t->data[0], 0x33);
    PRC_ASSERT_EQ(t->num_layers, 0);
    PRC_ASSERT_EQ(prc_api_get_texture_layer(ctx, t, 0, &layer), PRC_API_ERROR_PARAMETER);

    /* A plain diffuse image states nothing, and reads with the defaults. */
    t = &tesses[2].tess_faces[0].texture;
    PRC_ASSERT_EQ(t->kind, (uint32_t)PRC_API_TEXTURE_KIND_DIFFUSE);
    PRC_ASSERT_EQ(t->mapping_attributes, 0u);
    PRC_ASSERT_NEAR(t->intensity, 1.0, 1e-12);
    PRC_ASSERT_EQ(t->components, 0xF);
    PRC_ASSERT_EQ(t->data[0], 0x11);
    PRC_ASSERT_EQ(t->num_layers, 0);

    prc_api_release_data(ctx, data, tesses, 3, NULL, 0, NULL, 0, model_tree);

    printf("  sub-case: a looping or out-of-range texture chain is refused\n");
    for (damaged = 0; damaged < 2; damaged++)
    {
        open_written_items(ctx, &data, &model_tree, &tesses, 3);
        gd = &((prc_data *)data)->file_struct[0].globals->global_data;
        /* The last application in the table ends item 0's chain. */
        for (m = gd->material_count; m > 0; m--)
        {
            if (gd->materials[m - 1].tag == PRC_TYPE_GRAPH_TextureApplication &&
                gd->materials[m - 1].biased_next_texture_index == 0)
                break;
        }
        PRC_ASSERT(m > 0);
        gd->materials[m - 1].biased_next_texture_index = damaged == 0 ? m : gd->material_count + 1;
        k = 0;
        if (read_first_face(ctx, data, model_tree, tesses, 0) < 0) k++;
        if (read_first_face(ctx, data, model_tree, tesses, 1) < 0) k++;
        if (read_first_face(ctx, data, model_tree, tesses, 2) < 0) k++;
        /* Only the damaged chain is refused. */
        PRC_ASSERT_EQ(k, 1);
        prc_api_release_data(ctx, data, tesses, 3, NULL, 0, NULL, 0, model_tree);
    }
    remove(TEST_MATERIALS_FILENAME);
}

/* Raw-pixel pictures read back. The writer stores RGB and RGBA pixels with
   their rows reversed, bottom row first, as the file holds them; the stored
   block is checked for that. Each must come back byte for byte, top row
   first, with its size and channel count. The specification calls these pixels flate-formatted, and some real
   files deflate them, so the helper is also given a deflated block, the same
   pixels stored as they are, and three blocks it must refuse. */
static void
test_raw_pictures_read(prc_context *ctx)
{
    static const uint8_t rgb[2 * 2 * 3] = {
        0x41, 0x00, 0x00,   0x00, 0xC0, 0x00,
        0x00, 0x00, 0xC0,   0xC0, 0xC0, 0x00
    };
    static const uint8_t rgba[2 * 2 * 4] = {
        0x51, 0x10, 0x20, 0xFF,   0x30, 0x40, 0x50, 0x80,
        0x60, 0x70, 0x80, 0x40,   0x90, 0xA0, 0xB0, 0x00
    };
    /* rgb deflated by zlib */
    static const uint8_t rgb_deflated[18] = {
        0x78, 0xDA, 0x73, 0x64, 0x60, 0x60, 0x38, 0x00, 0xC2, 0x07, 0x0E, 0x30,
        0x00, 0x00, 0x0F, 0xD8, 0x03, 0x42
    };
    /* rgb plus one more byte, deflated: inflates to 13 bytes, not 12 */
    static const uint8_t rgb13_deflated[19] = {
        0x78, 0xDA, 0x73, 0x64, 0x60, 0x60, 0x38, 0x00, 0xC2, 0x07, 0x0E, 0x30,
        0xB0, 0x03, 0x00, 0x13, 0x21, 0x03, 0x49
    };
    prc_api_write_rep_item items[2];
    prc_api_data data;
    prc_api_product *model_tree;
    prc_api_tess *tesses;
    const prc_api_texture *t;
    uint8_t *px;
    uint32_t k;

    printf("  sub-case: raw RGB and RGBA pictures read back\n");
    memset(items, 0, sizeof(items));
    for (k = 0; k < 2; k++)
    {
        items[k].has_material = 1;
        items[k].material_color[0] = items[k].material_color[1] = items[k].material_color[2] = 1.0;
        items[k].material_alpha = 1.0;
        items[k].has_texture = 1;
        items[k].texture_width = items[k].texture_height = 2;
    }
    items[0].texture_format = PRC_API_WRITE_TEXTURE_RGB;
    items[0].texture_image = rgb;
    items[0].texture_image_size = sizeof(rgb);
    items[1].texture_format = PRC_API_WRITE_TEXTURE_RGBA;
    items[1].texture_image = rgba;
    items[1].texture_image_size = sizeof(rgba);

    PRC_ASSERT_EQ(write_textured_items(ctx, "raw_pictures_read", items, 2), 0);
    open_written_items(ctx, &data, &model_tree, &tesses, 2);
    {
        /* The stored RGB block: its first row is the image's bottom row. */
        const prc_file_struct_internal_global_data *gd = &((prc_data *)data)->file_struct[0].globals->global_data;
        const prc_file_structure_header *hd = ((prc_data *)data)->file_struct[0].header;
        uint32_t p, found = 0;

        for (p = 0; p < gd->picture_count; p++)
        {
            const prc_uncomp_block *blk;

            if (gd->pictures[p].format != KEPRCPicture_BITMAP_RGB_BYTE)
                continue;
            PRC_ASSERT(gd->pictures[p].biased_uncompressed_file_index >= 1 &&
                       gd->pictures[p].biased_uncompressed_file_index <= hd->file_count);
            blk = &hd->files[gd->pictures[p].biased_uncompressed_file_index - 1];
            {
                uint8_t px[sizeof(rgb) + 8];
                uLongf len = sizeof(px);
                PRC_ASSERT_EQ(uncompress(px, &len, blk->block, blk->block_size), Z_OK);
                PRC_ASSERT_EQ((uint32_t)len, (uint32_t)sizeof(rgb));
                PRC_ASSERT(memcmp(px, rgb + 6, 6) == 0);
                PRC_ASSERT(memcmp(px + 6, rgb, 6) == 0);
            }
            found++;
        }
        PRC_ASSERT_EQ(found, 1);
    }
    for (k = 0; k < 2; k++)
    {
        PRC_ASSERT(read_first_face(ctx, data, model_tree, tesses, k) >= 0);
        t = &tesses[k].tess_faces[0].texture;
        PRC_ASSERT(tesses[k].tess_faces[0].is_texture);
        PRC_ASSERT_NOT_NULL(t->data);
        PRC_ASSERT_EQ(t->width, 2);
        PRC_ASSERT_EQ(t->height, 2);
        PRC_ASSERT_EQ(t->num_channels, k == 0 ? 3 : 4);
        PRC_ASSERT(memcmp(t->data, k == 0 ? rgb : rgba, k == 0 ? sizeof(rgb) : sizeof(rgba)) == 0);
    }
    prc_api_release_data(ctx, data, tesses, 2, NULL, 0, NULL, 0, model_tree);
    remove(TEST_MATERIALS_FILENAME);

    printf("  sub-case: raw pixels deflated, stored, or refused\n");
    px = prc_parse_raw_picture_pixels(ctx, rgb_deflated, sizeof(rgb_deflated), sizeof(rgb));
    PRC_ASSERT_NOT_NULL(px);
    PRC_ASSERT(memcmp(px, rgb, sizeof(rgb)) == 0);
    prc_free(ctx, px);
    px = prc_parse_raw_picture_pixels(ctx, rgb, sizeof(rgb), sizeof(rgb));
    PRC_ASSERT_NOT_NULL(px);
    PRC_ASSERT(memcmp(px, rgb, sizeof(rgb)) == 0);
    prc_free(ctx, px);
    /* one byte short, not deflated */
    PRC_ASSERT(prc_parse_raw_picture_pixels(ctx, rgb, sizeof(rgb) - 1, sizeof(rgb)) == NULL);
    /* deflated to the wrong size, and not the image size as it stands */
    PRC_ASSERT(prc_parse_raw_picture_pixels(ctx, rgb13_deflated, sizeof(rgb13_deflated), sizeof(rgb)) == NULL);
    /* an image far larger than any deflated block could hold */
    PRC_ASSERT(prc_parse_raw_picture_pixels(ctx, rgb_deflated, sizeof(rgb_deflated), (size_t)1 << 30) == NULL);
}

int
main(void)
{
    prc_context *ctx;

    PRC_TEST_BEGIN("test_write_materials");

    ctx = prc_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    test_two_materials(ctx);
    test_shared_pictures(ctx);
    test_material_slots(ctx);
    test_default_still_applies(ctx);
    test_face_names_its_style(ctx);
    test_shared_tessellation_names_no_style(ctx);
    test_texture_layers(ctx);
    test_parent_herit_colour(ctx);
    test_texture_layers_read(ctx);
    test_raw_pictures_read(ctx);

    prc_release_context(ctx);

    PRC_TEST_END;
}
