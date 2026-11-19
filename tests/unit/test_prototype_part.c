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

/* A product occurrence that has child occurrences of its own and no part
   definition, but a prototype that has one, takes its prototype's part.

   The model tree walk follows a prototype only when the occurrence has no
   children of its own. An occurrence with children stopped there, so a part
   that only its prototype held was never built. In GS19090-011_3D.stream-70
   (prc-db) 32 sub-assemblies hold their DATUM sets (planes, axes, centre
   point) this way, and Adobe Acrobat lists all of them in its model tree.

   The writer has no prototypes, so the case is made by writing a small
   model and then, in the parsed data, pointing two occurrences at a third as
   their prototype, before the model tree is built:

       root
         SUB_A  part removed, one child (LEAF_A)
           LEAF_A geometry
         SUB_B  its own empty part, one child (LEAF_B)
           LEAF_B geometry
         PROTO  geometry

   SUB_A must take PROTO's part, with its one item and tessellation, and keep
   its own child. SUB_B must keep its own empty part, not take PROTO's. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "prc_test.h"
#include "prc_api.h"
#include "prc_context.h"
#include "prc_data.h"

static const char *TEST_FILENAME = "test_prototype_part.prc";

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

static uint32_t
product_index(prc_asm_file_structure_tree *tree, const char *name)
{
    uint32_t i;

    for (i = 0; i < tree->product_count; i++)
    {
        const char *s = (const char *)tree->products[i].base.base.name.name.string;

        if (s != NULL && strcmp(s, name) == 0)
            return i;
    }
    return UINT32_MAX;
}

static void
init_sub(prc_api_write_node *sub, const char *name, prc_api_write_node *leaf, const char *leaf_name,
    prc_api_write_node **children, prc_api_write_rep_item *ri)
{
    memset(leaf, 0, sizeof(*leaf));
    leaf->name = leaf_name;
    leaf->part_name = "leafpart";
    leaf->rep_items = ri;
    leaf->num_rep_items = 1;

    memset(sub, 0, sizeof(*sub));
    sub->name = name;
    sub->has_empty_part = 1;
    children[0] = leaf;
    sub->children = children;
    sub->num_children = 1;
}

int
main(void)
{
    static const double positions[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
    static const uint32_t tri_indices[3] = { 0, 1, 2 };
    prc_context *ctx;
    prc_api_write_tessellation tess;
    prc_api_write_rep_item ri;
    prc_api_write_node root, sub_a, leaf_a, sub_b, leaf_b, proto;
    prc_api_write_node *root_children[3];
    prc_api_write_node *sub_a_children[1], *sub_b_children[1];
    uint32_t face_count = 1;
    uint8_t *buf = NULL;
    size_t buf_size = 0;
    FILE *fid;
    prc_api_data data;
    prc_data *pd;
    prc_asm_file_structure_tree *tree;
    uint32_t sub_a_i, sub_b_i, proto_i;
    prc_api_product *model_tree = NULL;
    const prc_api_product *sub_a_node, *sub_b_node, *proto_node;
    uint32_t num_parts = 0, num_products = 0, num_markups = 0;

    PRC_TEST_BEGIN("test_prototype_part");

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

    init_sub(&sub_a, "SUB_A", &leaf_a, "LEAF_A", sub_a_children, &ri);
    init_sub(&sub_b, "SUB_B", &leaf_b, "LEAF_B", sub_b_children, &ri);

    memset(&proto, 0, sizeof(proto));
    proto.name = "PROTO";
    proto.part_name = "protopart";
    proto.rep_items = &ri;
    proto.num_rep_items = 1;

    memset(&root, 0, sizeof(root));
    root.name = "ROOT";
    root.has_empty_part = 1;
    root_children[0] = &sub_a;
    root_children[1] = &sub_b;
    root_children[2] = &proto;
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

    /* Make PROTO the prototype of SUB_A and SUB_B, in the same file
       structure, and remove SUB_A's own part */
    pd = (prc_data *)data;
    PRC_ASSERT_EQ(pd->file_structure_count, 1);
    tree = pd->file_struct[0].tree;
    PRC_ASSERT_NOT_NULL(tree);
    sub_a_i = product_index(tree, "SUB_A");
    sub_b_i = product_index(tree, "SUB_B");
    proto_i = product_index(tree, "PROTO");
    PRC_ASSERT(sub_a_i != UINT32_MAX && sub_b_i != UINT32_MAX && proto_i != UINT32_MAX);
    PRC_ASSERT_EQ(tree->products[sub_a_i].references_product_occurrence.number_of_child_product_occurrences, 1);
    PRC_ASSERT(tree->products[sub_a_i].references_product_occurrence.biased_index_part != 0);
    PRC_ASSERT(tree->products[sub_b_i].references_product_occurrence.biased_index_part != 0);
    PRC_ASSERT(tree->products[proto_i].references_product_occurrence.biased_index_part != 0);
    tree->products[sub_a_i].references_product_occurrence.biased_index_part = 0;
    tree->products[sub_a_i].references_product_occurrence.biased_index_prototype = proto_i + 1;
    tree->products[sub_a_i].references_product_occurrence.prototype_in_same_file_structure.flag = 1;
    tree->products[sub_b_i].references_product_occurrence.biased_index_prototype = proto_i + 1;
    tree->products[sub_b_i].references_product_occurrence.prototype_in_same_file_structure.flag = 1;

    PRC_ASSERT_EQ(prc_api_prep_model_tree(ctx, data, &num_parts, &num_products, &num_markups), 0);
    PRC_ASSERT_EQ(prc_api_create_model_tree(ctx, data, &model_tree, num_parts, num_products, num_markups), 0);
    PRC_ASSERT_NOT_NULL(model_tree);

    sub_a_node = find(model_tree, "SUB_A");
    sub_b_node = find(model_tree, "SUB_B");
    proto_node = find(model_tree, "PROTO");
    PRC_ASSERT_NOT_NULL(sub_a_node);
    PRC_ASSERT_NOT_NULL(sub_b_node);
    PRC_ASSERT_NOT_NULL(proto_node);

    /* SUB_A takes PROTO's part, with its item and tessellation */
    PRC_ASSERT_NOT_NULL(sub_a_node->part);
    PRC_ASSERT_NOT_NULL(proto_node->part);
    printf("  SUB_A part: %u items (PROTO's part: %u)\n", (unsigned)sub_a_node->part->num_rep_items,
        (unsigned)proto_node->part->num_rep_items);
    PRC_ASSERT_EQ(sub_a_node->part->num_rep_items, proto_node->part->num_rep_items);
    PRC_ASSERT_EQ(sub_a_node->part->num_rep_items, 1);
    PRC_ASSERT(sub_a_node->part->rep_items[0].biased_tess_index != 0);
    PRC_ASSERT_EQ(sub_a_node->part->rep_items[0].biased_tess_index, proto_node->part->rep_items[0].biased_tess_index);

    /* and keeps its own child */
    PRC_ASSERT_EQ(sub_a_node->num_children, 1);
    PRC_ASSERT_NOT_NULL(sub_a_node->children[0].name);
    PRC_ASSERT(strcmp(sub_a_node->children[0].name, "LEAF_A") == 0);

    /* SUB_B keeps its own empty part */
    PRC_ASSERT_NOT_NULL(sub_b_node->part);
    printf("  SUB_B part: %u items\n", (unsigned)sub_b_node->part->num_rep_items);
    PRC_ASSERT_EQ(sub_b_node->part->num_rep_items, 0);
    PRC_ASSERT_EQ(sub_b_node->num_children, 1);

    prc_api_release_data(ctx, data, NULL, 0, NULL, 0, NULL, 0, model_tree);
    PRC_ASSERT_EQ(prc_api_release_context(ctx), 0);
    remove(TEST_FILENAME);
    PRC_TEST_END;
}
