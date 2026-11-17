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

/* A product occurrence that has child occurrences of its own and no
   location, but a prototype that has one, is placed by its prototype's
   location.

   The model tree walk follows a prototype only when the occurrence has no
   children of its own (the children then come from the prototype). An
   occurrence with children stopped there, so a location that only its
   prototype stated was never applied. In 3D-PDF-Sample-Flashlight (prc-db)
   LL_LED is such an occurrence: its prototype scales by 1/25.4 to cancel the
   25.4 of the assembly above it, and without it the LED assembly drew 25.4
   times too large and hid the case. Adobe Acrobat places it by its
   prototype.

   The writer has no prototypes, so the case is made by writing a small
   model and then pointing one occurrence at another as its prototype in the
   parsed data, before the model tree is built:

       root
         SUB    no location, one child (LEAF)
           LEAF   geometry
         PROTO  location: scale 2, moved by (5, 6, 7); geometry

   With SUB's prototype set to PROTO, SUB must carry PROTO's location, keep
   its own child LEAF (its children are not replaced), and PROTO keeps its
   own location. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "prc_test.h"
#include "prc_api.h"
#include "prc_context.h"
#include "prc_data.h"

static const char *TEST_FILENAME = "test_prototype_chain_location.prc";

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

int
main(void)
{
    static const double positions[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
    static const uint32_t tri_indices[3] = { 0, 1, 2 };
    static const double proto_matrix[16] = {
        2, 0, 0, 0,
        0, 2, 0, 0,
        0, 0, 2, 0,
        5, 6, 7, 1
    };
    prc_context *ctx;
    prc_api_write_tessellation tess;
    prc_api_write_rep_item ri;
    prc_api_write_node root, sub, leaf, proto;
    prc_api_write_node *root_children[2];
    prc_api_write_node *sub_children[1];
    uint32_t face_count = 1;
    uint8_t *buf = NULL;
    size_t buf_size = 0;
    FILE *fid;
    prc_api_data data;
    prc_data *pd;
    prc_asm_file_structure_tree *tree;
    uint32_t sub_i, proto_i;
    prc_api_product *model_tree = NULL;
    const prc_api_product *sub_node, *proto_node;
    uint32_t num_parts = 0, num_products = 0, num_markups = 0;
    int k;

    PRC_TEST_BEGIN("test_prototype_chain_location");

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

    memset(&leaf, 0, sizeof(leaf));
    leaf.name = "LEAF";
    leaf.part_name = "leafpart";
    leaf.rep_items = &ri;
    leaf.num_rep_items = 1;

    memset(&sub, 0, sizeof(sub));
    sub.name = "SUB";
    sub.has_empty_part = 1;
    sub_children[0] = &leaf;
    sub.children = sub_children;
    sub.num_children = 1;

    memset(&proto, 0, sizeof(proto));
    proto.name = "PROTO";
    proto.part_name = "protopart";
    proto.rep_items = &ri;
    proto.num_rep_items = 1;
    proto.has_transform = 1;
    memcpy(proto.transform, proto_matrix, sizeof(proto_matrix));

    memset(&root, 0, sizeof(root));
    root.name = "ROOT";
    root.has_empty_part = 1;
    root_children[0] = &sub;
    root_children[1] = &proto;
    root.children = root_children;
    root.num_children = 2;

    PRC_ASSERT_EQ(prc_api_write_prc_buffer(ctx, "model", &root, &tess, 1, &buf, &buf_size), 0);
    fid = fopen(TEST_FILENAME, "wb");
    PRC_ASSERT_NOT_NULL(fid);
    PRC_ASSERT_EQ(fwrite(buf, 1, buf_size, fid), buf_size);
    fclose(fid);
    prc_api_write_prc_buffer_free(ctx, buf);

    data = prc_api_open_contents(ctx, TEST_FILENAME);
    PRC_ASSERT_NOT_NULL(data);

    /* Make PROTO the prototype of SUB, in the same file structure */
    pd = (prc_data *)data;
    PRC_ASSERT_EQ(pd->file_structure_count, 1);
    tree = pd->file_struct[0].tree;
    PRC_ASSERT_NOT_NULL(tree);
    sub_i = product_index(tree, "SUB");
    proto_i = product_index(tree, "PROTO");
    PRC_ASSERT(sub_i != UINT32_MAX && proto_i != UINT32_MAX);
    PRC_ASSERT_EQ(tree->products[sub_i].has_transform, 0);
    PRC_ASSERT_EQ(tree->products[sub_i].references_product_occurrence.number_of_child_product_occurrences, 1);
    PRC_ASSERT_EQ(tree->products[proto_i].has_transform, 1);
    tree->products[sub_i].references_product_occurrence.biased_index_prototype = proto_i + 1;
    tree->products[sub_i].references_product_occurrence.prototype_in_same_file_structure.flag = 1;

    PRC_ASSERT_EQ(prc_api_prep_model_tree(ctx, data, &num_parts, &num_products, &num_markups), 0);
    PRC_ASSERT_EQ(prc_api_create_model_tree(ctx, data, &model_tree, num_parts, num_products, num_markups), 0);
    PRC_ASSERT_NOT_NULL(model_tree);

    sub_node = find(model_tree, "SUB");
    proto_node = find(model_tree, "PROTO");
    PRC_ASSERT_NOT_NULL(sub_node);
    PRC_ASSERT_NOT_NULL(proto_node);

    /* SUB is placed by its prototype's location */
    PRC_ASSERT_EQ(sub_node->location_set, 1);
    for (k = 0; k < 16; k++)
        PRC_ASSERT(sub_node->location.matrix[k] == proto_matrix[k]);

    /* and keeps its own child */
    PRC_ASSERT_EQ(sub_node->num_children, 1);
    PRC_ASSERT_NOT_NULL(sub_node->children[0].name);
    PRC_ASSERT(strcmp(sub_node->children[0].name, "LEAF") == 0);

    /* PROTO keeps its own location */
    for (k = 0; k < 16; k++)
        PRC_ASSERT(proto_node->location.matrix[k] == proto_matrix[k]);

    prc_api_release_data(ctx, data, NULL, 0, NULL, 0, NULL, 0, model_tree);
    PRC_ASSERT_EQ(prc_api_release_context(ctx), 0);
    remove(TEST_FILENAME);
    PRC_TEST_END;
}
