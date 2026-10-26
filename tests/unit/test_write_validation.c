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

/* The public write API is given inconsistent or hostile structures: a NULL
   array behind a count, an index past its array, face counts that do not add
   up, counts whose products overflow 32 bits, a NULL child, a tessellation
   index outside the table, views given as NULL with a count. Each used to
   crash the writer or write past a buffer; each must now be refused. Two
   legal inputs that used to crash must now be written: a face made only of a
   fan with normals left to the writer, and a tree 100,000 nodes deep. */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "prc_test.h"
#include "prc_api.h"

static double cube_pos[8 * 3] = {
    0, 0, 0,  1, 0, 0,  1, 1, 0,  0, 1, 0,  0, 0, 1,  1, 0, 1,  1, 1, 1,  0, 1, 1 };
static uint32_t two_tris[6] = { 0, 1, 2,  0, 2, 3 };
static uint32_t one_face[1] = { 2 };

static void
base_item(prc_api_write_rep_item *ri, prc_api_write_ri_kind_t kind)
{
    memset(ri, 0, sizeof(*ri));
    ri->kind = kind;
    ri->biased_tessellation_index = 1;
}

static void
base_tess(prc_api_write_tessellation *t, prc_api_write_tess_kind_t kind)
{
    memset(t, 0, sizeof(*t));
    t->kind = kind;
    t->positions = cube_pos;
    t->num_positions = 8;
    t->tri_indices = two_tris;
    t->num_triangles = 2;
    t->face_tri_counts = one_face;
    t->num_faces = 1;
}

/* Writes one node holding one item over num_tess entries; returns the code. */
static int
write_one(prc_context *ctx, const prc_api_write_tessellation *t, uint32_t num_tess,
    const prc_api_write_rep_item *ri)
{
    prc_api_write_node root;
    uint8_t *buf = NULL;
    size_t size = 0;
    int code;

    memset(&root, 0, sizeof(root));
    root.rep_items = ri;
    root.num_rep_items = 1;
    root.bbox_max[0] = root.bbox_max[1] = root.bbox_max[2] = 1.0;
    code = prc_api_write_prc_buffer(ctx, "m", &root, t, num_tess, &buf, &size);
    if (code == 0)
        PRC_ASSERT(buf != NULL && size > 0);
    else
        PRC_ASSERT(buf == NULL);
    if (buf != NULL)
        prc_api_write_prc_buffer_free(ctx, buf);
    return code;
}

static void
test_legal_inputs_are_written(prc_context *ctx)
{
    prc_api_write_tessellation t;
    prc_api_write_rep_item ri;
    uint32_t no_tris[1] = { 0 };
    uint32_t no_face_tris[1] = { 0 };
    uint32_t fan_v[4] = { 0, 1, 2, 3 };
    prc_api_write_tri_group fan;
    prc_api_write_face_groups fg;

    base_item(&ri, PRC_API_WRITE_RI_SURFACE);
    base_tess(&t, PRC_API_WRITE_TESS_KIND_TRIANGLES);
    PRC_ASSERT_EQ(write_one(ctx, &t, 1, &ri), 0);

    /* A face of one fan and no triangles, normals left to the writer: the
       writer took the face normal from a triangle the face does not have. */
    memset(&fan, 0, sizeof(fan));
    fan.vertex_indices = fan_v;
    fan.num_vertices = 4;
    memset(&fg, 0, sizeof(fg));
    fg.fans = &fan;
    fg.num_fans = 1;
    t.tri_indices = no_tris;
    t.num_triangles = 0;
    t.face_tri_counts = no_face_tris;
    t.face_groups = &fg;
    PRC_ASSERT_EQ(write_one(ctx, &t, 1, &ri), 0);
}

static void
test_bad_meshes_are_refused(prc_context *ctx)
{
    prc_api_write_tessellation t;
    prc_api_write_rep_item ri;
    double normal[3] = { 0, 0, 1 };

    base_item(&ri, PRC_API_WRITE_RI_SURFACE);

    {   /* face_tri_counts adds up to num_triangles only modulo 2^32 */
        uint32_t ftc[2] = { 0xFFFFFFFFu, 2u };
        base_tess(&t, PRC_API_WRITE_TESS_KIND_TRIANGLES);
        t.num_triangles = 1;
        t.face_tri_counts = ftc;
        t.num_faces = 2;
        PRC_ASSERT(write_one(ctx, &t, 1, &ri) != 0);
    }
    {   /* a triangle index past num_positions */
        uint32_t tri[3] = { 0, 1, 100000 };
        uint32_t ftc[1] = { 1 };
        base_tess(&t, PRC_API_WRITE_TESS_KIND_TRIANGLES);
        t.tri_indices = tri;
        t.num_triangles = 1;
        t.face_tri_counts = ftc;
        PRC_ASSERT(write_one(ctx, &t, 1, &ri) != 0);
    }
    {   /* norm_indices with no normals */
        uint32_t ni[6] = { 0 };
        base_tess(&t, PRC_API_WRITE_TESS_KIND_TRIANGLES);
        t.norm_indices = ni;
        t.num_normals = 1;
        PRC_ASSERT(write_one(ctx, &t, 1, &ri) != 0);
    }
    {   /* a fan long enough that 3 words per vertex wraps the 32-bit index
           buffer size; refused from the counts, before its indices are read */
        uint32_t no_tris[1] = { 0 }, no_face_tris[1] = { 0 }, tri_n[1] = { 0 };
        uint32_t fan_v[8] = { 0 }, fan_n[8] = { 0 };
        prc_api_write_tri_group fan;
        prc_api_write_face_groups fg;
        memset(&fan, 0, sizeof(fan));
        fan.vertex_indices = fan_v;
        fan.normal_indices = fan_n;
        fan.num_vertices = 0x55555556u;
        memset(&fg, 0, sizeof(fg));
        fg.fans = &fan;
        fg.num_fans = 1;
        base_tess(&t, PRC_API_WRITE_TESS_KIND_TRIANGLES);
        t.normals = normal;
        t.num_normals = 1;
        t.tri_indices = no_tris;
        t.norm_indices = tri_n;
        t.num_triangles = 0;
        t.face_tri_counts = no_face_tris;
        t.face_groups = &fg;
        PRC_ASSERT(write_one(ctx, &t, 1, &ri) != 0);
    }
    {   /* a face that declares fans but supplies none */
        uint32_t no_face_tris[1] = { 0 };
        prc_api_write_face_groups fg;
        memset(&fg, 0, sizeof(fg));
        fg.num_fans = 3;
        base_tess(&t, PRC_API_WRITE_TESS_KIND_TRIANGLES);
        t.num_triangles = 0;
        t.face_tri_counts = no_face_tris;
        t.face_groups = &fg;
        PRC_ASSERT(write_one(ctx, &t, 1, &ri) != 0);
    }
    {   /* compressed: face_tri_counts adds up to more than num_triangles */
        uint32_t ftc[1] = { 4000 };
        base_tess(&t, PRC_API_WRITE_TESS_KIND_COMPRESSED);
        t.face_tri_counts = ftc;
        PRC_ASSERT(write_one(ctx, &t, 1, &ri) != 0);
    }
    {   /* compressed: normals with no norm_indices */
        base_tess(&t, PRC_API_WRITE_TESS_KIND_COMPRESSED);
        t.face_tri_counts = NULL;
        t.num_faces = 0;
        t.normals = normal;
        t.num_normals = 1;
        PRC_ASSERT(write_one(ctx, &t, 1, &ri) != 0);
    }
    {   /* compressed: a normal index past num_normals */
        uint32_t ni[6] = { 0, 0, 0, 0, 0, 5000000 };
        base_tess(&t, PRC_API_WRITE_TESS_KIND_COMPRESSED);
        t.normals = normal;
        t.num_normals = 1;
        t.norm_indices = ni;
        PRC_ASSERT(write_one(ctx, &t, 1, &ri) != 0);
    }
    {   /* positions NULL behind a count */
        base_tess(&t, PRC_API_WRITE_TESS_KIND_TRIANGLES);
        t.positions = NULL;
        PRC_ASSERT(write_one(ctx, &t, 1, &ri) != 0);
    }
    {   /* wire: two elements whose vertex counts add up past 32 bits */
        float pts[6] = { 0, 0, 0, 1, 1, 1 };
        prc_api_write_wire_element el[2];
        prc_api_write_rep_item wire_ri;
        memset(el, 0, sizeof(el));
        el[0].positions = pts;
        el[0].num_vertices = 0x80000000u;
        el[1].positions = pts;
        el[1].num_vertices = 0x80000001u;
        memset(&t, 0, sizeof(t));
        t.kind = PRC_API_WRITE_TESS_KIND_WIRE;
        t.wire_elements = el;
        t.num_wire_elements = 2;
        base_item(&wire_ri, PRC_API_WRITE_RI_WIRE);
        PRC_ASSERT(write_one(ctx, &t, 1, &wire_ri) != 0);
    }
    {   /* tess_entries NULL behind a count */
        PRC_ASSERT(write_one(ctx, NULL, 1, &ri) != 0);
    }
}

static void
test_bad_trees_are_refused(prc_context *ctx)
{
    prc_api_write_tessellation t;
    prc_api_write_rep_item ri;
    prc_api_write_node root;
    prc_api_write_node *kids[1] = { NULL };
    uint8_t *buf = NULL;
    size_t size = 0;

    base_tess(&t, PRC_API_WRITE_TESS_KIND_TRIANGLES);
    base_item(&ri, PRC_API_WRITE_RI_SURFACE);

    ri.biased_tessellation_index = 0;
    PRC_ASSERT(write_one(ctx, &t, 1, &ri) != 0);
    ri.biased_tessellation_index = 2;   /* one past the table */
    PRC_ASSERT(write_one(ctx, &t, 1, &ri) != 0);

    memset(&root, 0, sizeof(root));
    root.children = (prc_api_write_node * const *)kids;
    root.num_children = 1;
    PRC_ASSERT(prc_api_write_prc_buffer(ctx, "m", &root, NULL, 0, &buf, &size) != 0);
    PRC_ASSERT(buf == NULL);

    memset(&root, 0, sizeof(root));
    root.num_rep_items = 3;
    PRC_ASSERT(prc_api_write_prc_buffer(ctx, "m", &root, NULL, 0, &buf, &size) != 0);
    PRC_ASSERT(buf == NULL);

    memset(&root, 0, sizeof(root));
    root.num_children = 2;
    PRC_ASSERT(prc_api_write_prc_buffer(ctx, "m", &root, NULL, 0, &buf, &size) != 0);
    PRC_ASSERT(buf == NULL);
}

/* A chain of nodes, each the only child of the one above, with the mesh on
   the last. Two of the writer's tree walks recursed, so 20,000 levels
   overflowed the stack. */
static void
test_deep_tree_is_written(prc_context *ctx)
{
    const uint32_t depth = 100000;
    prc_api_write_node *nodes;
    prc_api_write_node **kids;
    prc_api_write_tessellation t;
    prc_api_write_rep_item ri;
    uint8_t *buf = NULL;
    size_t size = 0;
    uint32_t i;

    nodes = (prc_api_write_node *)calloc(depth, sizeof(*nodes));
    kids = (prc_api_write_node **)calloc(depth, sizeof(*kids));
    PRC_ASSERT_NOT_NULL(nodes);
    PRC_ASSERT_NOT_NULL(kids);
    base_tess(&t, PRC_API_WRITE_TESS_KIND_TRIANGLES);
    base_item(&ri, PRC_API_WRITE_RI_SURFACE);
    ri.has_material = 1;
    ri.material_color[0] = 1.0;
    ri.material_alpha = 1.0;
    for (i = 0; i + 1 < depth; i++)
    {
        kids[i] = &nodes[i + 1];
        nodes[i].children = (prc_api_write_node * const *)&kids[i];
        nodes[i].num_children = 1;
    }
    nodes[depth - 1].rep_items = &ri;
    nodes[depth - 1].num_rep_items = 1;
    PRC_ASSERT_EQ(prc_api_write_prc_buffer(ctx, "m", &nodes[0], &t, 1, &buf, &size), 0);
    PRC_ASSERT(buf != NULL && size > 0);
    if (buf != NULL)
        prc_api_write_prc_buffer_free(ctx, buf);
    free(kids);
    free(nodes);
}

static void
test_null_views_are_refused(prc_context *ctx)
{
    static const char *pdf = "test_write_validation_views.pdf";
    prc_pdf_write_options o;
    uint8_t prc[16] = { 'P', 'R', 'C' };
    FILE *f;

    memset(&o, 0, sizeof(o));
    o.views = NULL;
    o.num_views = 2;
    remove(pdf);
    PRC_ASSERT(prc_api_pdf_embed_prc(ctx, pdf, prc, sizeof(prc), &o) != 0);
    f = fopen(pdf, "rb");
    PRC_ASSERT_NULL(f);
    if (f != NULL)
        fclose(f);
    remove(pdf);
}

int
main(void)
{
    prc_context *ctx;

    PRC_TEST_BEGIN("write API refuses inconsistent input");

    ctx = prc_api_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    test_legal_inputs_are_written(ctx);
    test_bad_meshes_are_refused(ctx);
    test_bad_trees_are_refused(ctx);
    test_deep_tree_is_written(ctx);
    test_null_views_are_refused(ctx);

    prc_api_release_context(ctx);

    PRC_TEST_END;
}
