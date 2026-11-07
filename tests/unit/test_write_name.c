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

/* prc_write_name against the current name of clause 4.4: same_name TRUE is
   written only for the current name itself, so that a NULL name written
   after a named entity reads back as NULL, not as that entity's name; and a
   tree written through the public API keeps an unnamed node unnamed. */

#include <stdlib.h>
#include <string.h>
#include "prc_test.h"
#include "prc_api.h"
#include "prc_context.h"
#include "prc_data.h"
#include "prc_bit.h"
#include "prc_parse_common.h"
#include "prc_write_common.h"

static void
assert_name(const prc_name *n, const char *text)
{
    if (text == NULL)
    {
        PRC_ASSERT(n->name.string == NULL);
        return;
    }
    PRC_ASSERT_NOT_NULL(n->name.string);
    PRC_ASSERT_EQ(n->name.size, (prc_unsigned_int)strlen(text));
    PRC_ASSERT(memcmp(n->name.string, text, n->name.size) == 0);
}

/* Names written one after another in one section, read back. */
static void
test_sequence(prc_context *ctx)
{
    static const char *const written[8] = { NULL, "a", NULL, "a", "a", "", NULL, "b" };
    /* same_name is TRUE only where the name is the current one. */
    static const uint8_t same[8] = { 1, 0, 0, 0, 1, 0, 0, 0 };
    prc_bit_write_state w;
    prc_bit_state r;
    prc_name names[8];
    int k;

    PRC_ASSERT_EQ(prc_bitwrite_init(ctx, &w, 64), 0);
    for (k = 0; k < 8; k++)
        PRC_ASSERT_EQ(prc_write_name(ctx, &w, written[k]), 0);
    PRC_ASSERT_EQ(prc_bitwrite_flush(ctx, &w), 0);

    prc_init_bit_state(ctx, &r, w.buf, w.byte_pos);
    memset(names, 0, sizeof(names));
    for (k = 0; k < 8; k++)
    {
        PRC_ASSERT_EQ(prc_parse_name(ctx, &r, &names[k]), 0);
        assert_name(&names[k], written[k]);
        PRC_ASSERT_EQ(names[k].same, same[k]);
    }
    for (k = 0; k < 8; k++)
        if (names[k].name.string != NULL)
            prc_free(ctx, names[k].name.string);
    prc_bitwrite_release(ctx, &w);
}

/* The unnamed node: a product, not its parent's first child, whose part
   draws nothing. */
static const prc_api_product *
find_unnamed_sibling(const prc_api_product *p)
{
    size_t c;

    for (c = 0; c < p->num_children; c++)
    {
        const prc_api_product *found;

        if (p->children[c].part != NULL && p->children[c].part->num_rep_items == 0
            && p->children[c].part->biased_tess_index == 0 && c > 0)
            return &p->children[c];
        found = find_unnamed_sibling(&p->children[c]);
        if (found != NULL)
            return found;
    }
    return NULL;
}

/* Through the public API: a named part with geometry, then an unnamed node
   with an empty part. The unnamed node and its part come back unnamed. */
static void
test_tree(void)
{
    static const double pos[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
    static const uint32_t tri[3] = { 0, 1, 2 };
    static const uint32_t count = 1;
    prc_api_write_tessellation t;
    prc_api_write_rep_item ri;
    prc_api_write_node root, named, unnamed;
    prc_api_write_node *kids[2];
    prc_context *ctx = prc_api_new_context(NULL);
    prc_api_data data;
    prc_api_product *tree = NULL;
    const prc_api_product *u;
    uint32_t np, nprod, nm;

    PRC_ASSERT_NOT_NULL(ctx);
    memset(&t, 0, sizeof(t));
    t.kind = PRC_API_WRITE_TESS_KIND_TRIANGLES;
    t.positions = pos;
    t.num_positions = 3;
    t.tri_indices = tri;
    t.num_triangles = 1;
    t.face_tri_counts = &count;
    t.num_faces = 1;
    memset(&ri, 0, sizeof(ri));
    ri.kind = PRC_API_WRITE_RI_SURFACE;
    ri.biased_tessellation_index = 1;

    memset(&named, 0, sizeof(named));
    named.name = "named";
    named.part_name = "named part";
    named.rep_items = &ri;
    named.num_rep_items = 1;
    memset(&unnamed, 0, sizeof(unnamed));
    unnamed.has_empty_part = 1;
    memset(&root, 0, sizeof(root));
    root.name = "root";
    root.has_empty_part = 1;
    kids[0] = &named;
    kids[1] = &unnamed;
    root.children = kids;
    root.num_children = 2;
    PRC_ASSERT_EQ(prc_api_write_prc_file(ctx, "test_write_name.prc", "model", &root, &t, 1), 0);

    data = prc_api_open_contents(ctx, "test_write_name.prc");
    PRC_ASSERT_NOT_NULL(data);
    PRC_ASSERT_EQ(prc_api_prep_model_tree(ctx, data, &np, &nprod, &nm), 0);
    PRC_ASSERT_EQ(prc_api_create_model_tree(ctx, data, &tree, np, nprod, nm), 0);
    u = find_unnamed_sibling(tree);
    PRC_ASSERT_NOT_NULL(u);
    if (u != NULL)
    {
        /* The reader's own label for a product with no name, not "named". */
        PRC_ASSERT_NOT_NULL(u->name);
        PRC_ASSERT(strcmp(u->name, "Unnamed Product") == 0);
        PRC_ASSERT(u->part->name == NULL || strcmp(u->part->name, "named part") != 0);
    }
    prc_api_release_data(ctx, data, NULL, 0, NULL, 0, NULL, 0, tree);
    PRC_ASSERT_EQ(prc_api_release_context(ctx), 0);
    remove("test_write_name.prc");
}

int
main(void)
{
    prc_context *ctx;

    PRC_TEST_BEGIN("test_write_name");
    ctx = prc_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);
    test_sequence(ctx);
    prc_release_context(ctx);
    test_tree();
    PRC_TEST_END;
}
