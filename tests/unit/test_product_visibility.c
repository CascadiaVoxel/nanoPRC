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

/* prc_api_product.visible and prc_api_part.visible: the graphics behaviour
   resolved along the path from the root, as Adobe Acrobat resolves it.

   ISO 14739-1, 7.2.4.2 "GraphicsContent" and Table 34 "behavior_bit_field":
   the lowest node on the path with ChildHeritShow gives the value, else the
   highest with ParentHeritShow, and a node with ChildHeritShow ignores its
   own ParentHeritShow. Where no node sets either, Acrobat draws the node
   whatever its own Show bit. Removed hides a node and what it holds.

   Checked against Adobe Acrobat Reader, which reported every node it hides
   for four files (private 00006 and 00077, and the prc-db Capping Machine
   and Industrial Grinder samples): the hidden products and items it listed
   are the ones this gives.

   The writer sets Show on every node, so the cases are made by writing a
   small model and then setting the behaviour of its occurrences in the
   parsed data, before the model tree is built:

       A  0x0002 (ChildHeritShow, Show clear)      { A1 0x0001 }   both hidden
       B  0x0004 (ParentHeritShow, Show clear)     { B1 0x0001 }   both hidden
       C  0x0000 (no flags, Show clear)            { C1 0x0000 }   both shown
       D  0x0003 (ChildHeritShow, Show)            { D1 0x0004 }   both shown
       E  0x0001 (Show)                            { E1 Removed }  E shown, E1 hidden
       F  0x0005 (ParentHeritShow, Show)           { F1 0x0004 }   both shown: the highest wins
       G  0x0002 (ChildHeritShow, Show clear)      { G1 0x0003 }   G hidden, G1 shown: the lowest wins

   Each leaf's part follows its product. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "prc_test.h"
#include "prc_api.h"
#include "prc_context.h"
#include "prc_data.h"

static const char *TEST_FILENAME = "test_product_visibility.prc";

#define NUM_BRANCHES 7

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

static void
set_behaviour(prc_asm_file_structure_tree *tree, const char *name, uint32_t bits)
{
    uint32_t i;

    for (i = 0; i < tree->product_count; i++)
    {
        const char *s = (const char *)tree->products[i].base.base.name.name.string;

        if (s != NULL && strcmp(s, name) == 0)
        {
            tree->products[i].base.graphics_content.behavior_bit_field1 = (uint8_t)(bits & 0xff);
            tree->products[i].base.graphics_content.behavior_bit_field2 = (uint8_t)(bits >> 8);
            return;
        }
    }
    PRC_ASSERT(0 && "product not found");
}

static void
expect(const prc_api_product *tree, const char *name, int visible)
{
    const prc_api_product *p = find(tree, name);

    PRC_ASSERT_NOT_NULL(p);
    printf("  %s: visible %d (expected %d)\n", name, (int)p->visible, visible);
    PRC_ASSERT_EQ(p->visible, visible);
    if (p->part != NULL)
    {
        size_t r;

        PRC_ASSERT_EQ(p->part->visible, visible);
        for (r = 0; r < p->part->num_rep_items; r++)
            PRC_ASSERT_EQ(p->part->rep_items[r].visible, visible);
    }
}

int
main(void)
{
    static const double positions[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
    static const uint32_t tri_indices[3] = { 0, 1, 2 };
    static const char *branch_names[NUM_BRANCHES] = { "A", "B", "C", "D", "E", "F", "G" };
    static const char *leaf_names[NUM_BRANCHES] = { "A1", "B1", "C1", "D1", "E1", "F1", "G1" };
    static const uint32_t branch_bits[NUM_BRANCHES] = { 0x0002, 0x0004, 0x0000, 0x0003, 0x0001, 0x0005, 0x0002 };
    static const uint32_t leaf_bits[NUM_BRANCHES] = { 0x0001, 0x0001, 0x0000, 0x0004, 0x2001, 0x0004, 0x0003 };
    static const int branch_visible[NUM_BRANCHES] = { 0, 0, 1, 1, 1, 1, 0 };
    static const int leaf_visible[NUM_BRANCHES] = { 0, 0, 1, 1, 0, 1, 1 };
    prc_context *ctx;
    prc_api_write_tessellation tess;
    prc_api_write_rep_item ri;
    prc_api_write_node root, branches[NUM_BRANCHES], leaves[NUM_BRANCHES];
    prc_api_write_node *root_children[NUM_BRANCHES];
    prc_api_write_node *branch_children[NUM_BRANCHES][1];
    uint32_t face_count = 1;
    uint8_t *buf = NULL;
    size_t buf_size = 0;
    FILE *fid;
    prc_api_data data;
    prc_data *pd;
    prc_api_product *model_tree = NULL;
    uint32_t num_parts = 0, num_products = 0, num_markups = 0;
    int k;

    PRC_TEST_BEGIN("test_product_visibility");

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

    for (k = 0; k < NUM_BRANCHES; k++)
    {
        memset(&leaves[k], 0, sizeof(leaves[k]));
        leaves[k].name = leaf_names[k];
        leaves[k].part_name = "leafpart";
        leaves[k].rep_items = &ri;
        leaves[k].num_rep_items = 1;

        memset(&branches[k], 0, sizeof(branches[k]));
        branches[k].name = branch_names[k];
        branches[k].has_empty_part = 1;
        branch_children[k][0] = &leaves[k];
        branches[k].children = branch_children[k];
        branches[k].num_children = 1;
        root_children[k] = &branches[k];
    }
    memset(&root, 0, sizeof(root));
    root.name = "ROOT";
    root.has_empty_part = 1;
    root.children = root_children;
    root.num_children = NUM_BRANCHES;

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
    for (k = 0; k < NUM_BRANCHES; k++)
    {
        set_behaviour(pd->file_struct[0].tree, branch_names[k], branch_bits[k]);
        set_behaviour(pd->file_struct[0].tree, leaf_names[k], leaf_bits[k]);
    }

    PRC_ASSERT_EQ(prc_api_prep_model_tree(ctx, data, &num_parts, &num_products, &num_markups), 0);
    PRC_ASSERT_EQ(prc_api_create_model_tree(ctx, data, &model_tree, num_parts, num_products, num_markups), 0);
    PRC_ASSERT_NOT_NULL(model_tree);

    expect(model_tree, "ROOT", 1);
    for (k = 0; k < NUM_BRANCHES; k++)
    {
        expect(model_tree, branch_names[k], branch_visible[k]);
        expect(model_tree, leaf_names[k], leaf_visible[k]);
    }

    prc_api_release_data(ctx, data, NULL, 0, NULL, 0, NULL, 0, model_tree);
    PRC_ASSERT_EQ(prc_api_release_context(ctx), 0);
    remove(TEST_FILENAME);
    PRC_TEST_END;
}
