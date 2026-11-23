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

/* A product occurrence with no part and no prototype that names external data
   is an empty stand-in for the product the reference names, and takes its part.

   The model tree walk used a product's external-data reference only to choose
   which file structure to look in, and followed prototype references only.
   3D_PDF_Pump_Maintenance_Manual.stream-359 is an assembly of 81 such
   stand-ins, each naming the product that holds its part in another file
   structure: none of the 81 parts was reached, and the model loaded as markup
   alone (0 parts, 68 markup tessellations), where Adobe Acrobat draws the pump.

   The writer writes no external data, so the case is made by writing a small
   model and then, in the parsed data, pointing two occurrences at a third as
   their external data, before the model tree is built:

       root
         STAND   part removed, external data -> SOURCE        takes SOURCE's part
         KEEPER  its own empty part, external data -> SOURCE  keeps its own part
         SOURCE  geometry

   The reference names a product in the same file structure here; the walk
   treats a reference to another file structure the same way (it already finds
   that structure for its external data, as it does for a prototype). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "prc_test.h"
#include "prc_api.h"
#include "prc_context.h"
#include "prc_data.h"

static const char *TEST_FILENAME = "test_external_data_part.prc";

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
    prc_context *ctx;
    prc_api_write_tessellation tess;
    prc_api_write_rep_item ri;
    prc_api_write_node root, stand, keeper, source;
    prc_api_write_node *root_children[3];
    uint32_t face_count = 1;
    uint8_t *buf = NULL;
    size_t buf_size = 0;
    FILE *fid;
    prc_api_data data;
    prc_data *pd;
    prc_asm_file_structure_tree *tree;
    uint32_t stand_i, keeper_i, source_i;
    prc_api_product *model_tree = NULL;
    const prc_api_product *stand_node, *keeper_node, *source_node;
    uint32_t num_parts = 0, num_products = 0, num_markups = 0;

    PRC_TEST_BEGIN("test_external_data_part");

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

    memset(&stand, 0, sizeof(stand));
    stand.name = "STAND";
    stand.has_empty_part = 1;

    memset(&keeper, 0, sizeof(keeper));
    keeper.name = "KEEPER";
    keeper.has_empty_part = 1;

    memset(&source, 0, sizeof(source));
    source.name = "SOURCE";
    source.part_name = "sourcepart";
    source.rep_items = &ri;
    source.num_rep_items = 1;

    memset(&root, 0, sizeof(root));
    root.name = "ROOT";
    root.has_empty_part = 1;
    root_children[0] = &stand;
    root_children[1] = &keeper;
    root_children[2] = &source;
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
    tree = pd->file_struct[0].tree;
    PRC_ASSERT_NOT_NULL(tree);
    stand_i = product_index(tree, "STAND");
    keeper_i = product_index(tree, "KEEPER");
    source_i = product_index(tree, "SOURCE");
    PRC_ASSERT(stand_i != UINT32_MAX && keeper_i != UINT32_MAX && source_i != UINT32_MAX);
    PRC_ASSERT(tree->products[stand_i].references_product_occurrence.biased_index_part != 0);
    PRC_ASSERT(tree->products[keeper_i].references_product_occurrence.biased_index_part != 0);
    PRC_ASSERT(tree->products[source_i].references_product_occurrence.biased_index_part != 0);

    /* STAND has neither part nor prototype, and names SOURCE as its external
       data; KEEPER names SOURCE too but keeps a part of its own */
    tree->products[stand_i].references_product_occurrence.biased_index_part = 0;
    tree->products[stand_i].references_product_occurrence.biased_index_prototype = 0;
    tree->products[stand_i].references_product_occurrence.biased_index_external_data = source_i + 1;
    tree->products[stand_i].references_product_occurrence.external_data_in_same_file_structure.flag = 1;
    tree->products[keeper_i].references_product_occurrence.biased_index_external_data = source_i + 1;
    tree->products[keeper_i].references_product_occurrence.external_data_in_same_file_structure.flag = 1;

    PRC_ASSERT_EQ(prc_api_prep_model_tree(ctx, data, &num_parts, &num_products, &num_markups), 0);
    PRC_ASSERT_EQ(prc_api_create_model_tree(ctx, data, &model_tree, num_parts, num_products, num_markups), 0);
    PRC_ASSERT_NOT_NULL(model_tree);

    stand_node = find(model_tree, "STAND");
    keeper_node = find(model_tree, "KEEPER");
    source_node = find(model_tree, "SOURCE");
    PRC_ASSERT_NOT_NULL(stand_node);
    PRC_ASSERT_NOT_NULL(keeper_node);
    PRC_ASSERT_NOT_NULL(source_node);

    /* STAND takes SOURCE's part, with its item and tessellation */
    PRC_ASSERT_NOT_NULL(source_node->part);
    PRC_ASSERT_NOT_NULL(stand_node->part);
    printf("  STAND part: %u items (SOURCE's part: %u)\n", (unsigned)stand_node->part->num_rep_items,
        (unsigned)source_node->part->num_rep_items);
    PRC_ASSERT_EQ(stand_node->part->num_rep_items, 1);
    PRC_ASSERT_EQ(stand_node->part->num_rep_items, source_node->part->num_rep_items);
    PRC_ASSERT(stand_node->part->rep_items[0].biased_tess_index != 0);
    PRC_ASSERT_EQ(stand_node->part->rep_items[0].biased_tess_index, source_node->part->rep_items[0].biased_tess_index);

    /* KEEPER keeps its own empty part */
    PRC_ASSERT_NOT_NULL(keeper_node->part);
    printf("  KEEPER part: %u items\n", (unsigned)keeper_node->part->num_rep_items);
    PRC_ASSERT_EQ(keeper_node->part->num_rep_items, 0);

    prc_api_release_data(ctx, data, NULL, 0, NULL, 0, NULL, 0, model_tree);
    PRC_ASSERT_EQ(prc_api_release_context(ctx), 0);
    remove(TEST_FILENAME);
    PRC_TEST_END;
}
