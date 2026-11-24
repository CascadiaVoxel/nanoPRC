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

/* prc_api_markup.visible: a markup's graphics behaviour resolved along its
   path from the root, by the rule prc_api_product.visible and
   prc_api_part.visible follow (see test_product_visibility.c). A markup is shown
   by its own Show bit where no ChildHeritShow or ParentHeritShow along the path
   decides.

   3D_PDF_Pump_Maintenance_Manual.stream-359 holds 68 markups: 22 with Show set
   and 46, the part-name labels, with only ParentHeritShow set. Adobe Acrobat
   draws the 22 and none of the 46; the API gave no markup a visibility, so a
   viewer drew all 68 and the labels' leader lines ran off into space away from
   the model.

   The writer writes no markups, so the cases are made by writing a small model
   and then adding markups to three of its occurrences in the parsed data, before
   the model tree is built:

       P1  0x0001 (Show)                    { 0x0001, 0x0004, starting value }
                                              shown, hidden, shown
       P2  0x0002 (ChildHeritShow, no Show) { 0x0001 }
                                              hidden: the lowest ChildHeritShow decides
       P3  0x0003 (ChildHeritShow, Show)    { 0x0004 }
                                              shown, whatever the markup's own bits */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "prc_test.h"
#include "prc_api.h"
#include "prc_context.h"
#include "prc_data.h"

static const char *TEST_FILENAME = "test_markup_visibility.prc";

#define STARTING_VALUE 0xffffffffu

static const prc_api_product *
find(const prc_api_product *p, const char *name)
{
    size_t i;

    if (p->name != NULL && strcmp(p->name, name) == 0)
        return p;
    for (i = 0; i < p->num_children; i++)
    {
        const prc_api_product *q = find(&p->children[i], name);

        if (q != NULL)
            return q;
    }
    return NULL;
}

/* The node under the named product that holds its markups */
static const prc_api_product *
markup_node(const prc_api_product *model_tree, const char *product_name)
{
    const prc_api_product *p = find(model_tree, product_name);
    size_t i;

    PRC_ASSERT_NOT_NULL(p);
    for (i = 0; i < p->num_children; i++)
    {
        if (p->children[i].type == PRC_API_NODE_MARKUP)
            return &p->children[i];
    }
    PRC_ASSERT(0 && "no markup node");
    return NULL;
}

static prc_asm_product_occurrence *
find_occurrence(prc_asm_file_structure_tree *tree, const char *name)
{
    uint32_t i;

    for (i = 0; i < tree->product_count; i++)
    {
        const char *s = (const char *)tree->products[i].base.base.name.name.string;

        if (s != NULL && strcmp(s, name) == 0)
            return &tree->products[i];
    }
    PRC_ASSERT(0 && "product not found");
    return NULL;
}

static void
set_graphics(prc_graphics_content *g, uint32_t bits)
{
    if (bits == STARTING_VALUE)
    {
        g->behavior_bit_field1 = 0;
        g->behavior_bit_field2 = 0;
        g->is_set = 0;
    }
    else
    {
        g->behavior_bit_field1 = (uint8_t)(bits & 0xff);
        g->behavior_bit_field2 = (uint8_t)(bits >> 8);
        g->is_set = 1;
    }
}

/* Gives the named occurrence its own behaviour and n markups with the
   behaviours listed */
static void
add_markups(prc_context *ctx, prc_asm_file_structure_tree *tree, const char *name,
    uint32_t product_bits, const uint32_t *markup_bits, uint32_t n)
{
    prc_asm_product_occurrence *p = find_occurrence(tree, name);
    uint32_t k;

    set_graphics(&p->base.graphics_content, product_bits);
    p->markups.markups = (prc_mkp_markup *)prc_calloc(ctx, n, sizeof(prc_mkp_markup));
    PRC_ASSERT_NOT_NULL(p->markups.markups);
    p->markups.number_of_markups = n;
    for (k = 0; k < n; k++)
        set_graphics(&p->markups.markups[k].base.graphics_content, markup_bits[k]);
}

int
main(void)
{
    static const double positions[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
    static const uint32_t tri_indices[3] = { 0, 1, 2 };
    static const uint32_t p1_bits[3] = { 0x0001, 0x0004, STARTING_VALUE };
    static const uint32_t p2_bits[1] = { 0x0001 };
    static const uint32_t p3_bits[1] = { 0x0004 };
    prc_context *ctx;
    prc_api_write_tessellation tess;
    prc_api_write_rep_item ri;
    prc_api_write_node root, p1, p2, p3;
    prc_api_write_node *root_children[3];
    uint32_t face_count = 1;
    uint8_t *buf = NULL;
    size_t buf_size = 0;
    FILE *fid;
    prc_api_data data;
    prc_data *pd;
    prc_api_product *model_tree = NULL;
    const prc_api_product *m1, *m2, *m3;
    uint32_t num_parts = 0, num_products = 0, num_markups = 0;

    PRC_TEST_BEGIN("test_markup_visibility");

    ctx = prc_api_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    memset(&tess, 0, sizeof(tess));
    tess.kind = PRC_API_WRITE_TESS_KIND_COMPRESSED;
    tess.positions = positions;
    tess.num_positions = 3;
    tess.tri_indices = tri_indices;
    tess.num_triangles = 1;
    tess.face_tri_counts = &face_count;
    tess.num_faces = 1;

    memset(&ri, 0, sizeof(ri));
    ri.kind = PRC_API_WRITE_RI_SURFACE;
    ri.biased_tessellation_index = 1;

    memset(&p1, 0, sizeof(p1));
    p1.name = "P1";
    p1.part_name = "p1part";
    p1.rep_items = &ri;
    p1.num_rep_items = 1;
    p2 = p1;
    p2.name = "P2";
    p2.part_name = "p2part";
    p3 = p1;
    p3.name = "P3";
    p3.part_name = "p3part";

    memset(&root, 0, sizeof(root));
    root.name = "ROOT";
    root.has_empty_part = 1;
    root_children[0] = &p1;
    root_children[1] = &p2;
    root_children[2] = &p3;
    root.children = root_children;
    root.num_children = 3;

    PRC_ASSERT_EQ(prc_api_write_prc_buffer(ctx, "model", &root, &tess, 1, &buf, &buf_size), 0);
    fid = fopen(TEST_FILENAME, "wb");
    PRC_ASSERT_NOT_NULL(fid);
    PRC_ASSERT_EQ(fwrite(buf, 1, buf_size, fid), buf_size);
    fclose(fid);
    prc_api_write_prc_buffer_free(ctx, buf);

    data = prc_api_open_contents(ctx, TEST_FILENAME);
    PRC_ASSERT_NOT_NULL(data);

    pd = (prc_data *)data;
    PRC_ASSERT_EQ(pd->file_structure_count, 1);
    PRC_ASSERT_NOT_NULL(pd->file_struct[0].tree);

    add_markups(ctx, pd->file_struct[0].tree, "P1", 0x0001, p1_bits, 3);
    add_markups(ctx, pd->file_struct[0].tree, "P2", 0x0002, p2_bits, 1);
    add_markups(ctx, pd->file_struct[0].tree, "P3", 0x0003, p3_bits, 1);

    PRC_ASSERT_EQ(prc_api_prep_model_tree(ctx, data, &num_parts, &num_products, &num_markups), 0);
    PRC_ASSERT_EQ(num_markups, 5);
    PRC_ASSERT_EQ(prc_api_create_model_tree(ctx, data, &model_tree, num_parts, num_products, num_markups), 0);
    PRC_ASSERT_NOT_NULL(model_tree);

    m1 = markup_node(model_tree, "P1");
    m2 = markup_node(model_tree, "P2");
    m3 = markup_node(model_tree, "P3");
    PRC_ASSERT_EQ(m1->num_markups, 3);
    PRC_ASSERT_EQ(m2->num_markups, 1);
    PRC_ASSERT_EQ(m3->num_markups, 1);

    /* P1: its own Show bit gives a markup's visibility where no flag decides */
    printf("  P1 markups: %d %d %d\n", (int)m1->markup[0].visible, (int)m1->markup[1].visible,
        (int)m1->markup[2].visible);
    PRC_ASSERT_EQ(m1->markup[0].visible, 1);
    PRC_ASSERT_EQ(m1->markup[1].visible, 0); /* ParentHeritShow with Show clear, and nothing above to say otherwise */
    PRC_ASSERT_EQ(m1->markup[2].visible, 1); /* still the section's starting value: Show */

    /* the behaviour is kept as stored */
    PRC_ASSERT_EQ(m1->markup[0].behavior_bit_field1, 0x01);
    PRC_ASSERT_EQ(m1->markup[0].behavior_is_set, 1);
    PRC_ASSERT_EQ(m1->markup[1].behavior_bit_field1, 0x04);
    PRC_ASSERT_EQ(m1->markup[2].behavior_is_set, 0);

    /* P2: the occurrence's ChildHeritShow, with Show clear, hides what is below it */
    PRC_ASSERT_EQ(m2->markup[0].visible, 0);

    /* P3: its ChildHeritShow, with Show set, shows what is below it */
    PRC_ASSERT_EQ(m3->markup[0].visible, 1);

    prc_api_release_data(ctx, data, NULL, 0, NULL, 0, NULL, 0, model_tree);
    PRC_ASSERT_EQ(prc_api_release_context(ctx), 0);
    remove(TEST_FILENAME);
    PRC_TEST_END;
}
