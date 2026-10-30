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

/* prc_api_write_bounds and prc_api_pdf_view_from_bounds.

   The box must be the box of the placed model. Geometry is often stored
   around its own origin and placed by the transforms of the nodes above it;
   a real file in a public corpus has its parts placed about 2,000 units
   from the origin, and a camera aimed at the stored coordinates showed
   nothing in Adobe Acrobat. */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "prc_test.h"
#include "prc_api.h"

#define EPS 1e-9

/* One triangle in the unit square of the xy plane, z = 0. */
static const double tri_pos[9] = { 0, 0, 0,  1, 0, 0,  0, 1, 0 };
static const uint32_t tri_idx[3] = { 0, 1, 2 };
static const uint32_t tri_counts[1] = { 1 };

static void
make_triangle(prc_api_write_tessellation *t)
{
    memset(t, 0, sizeof(*t));
    t->kind = PRC_API_WRITE_TESS_KIND_TRIANGLES;
    t->positions = tri_pos;
    t->num_positions = 3;
    t->tri_indices = tri_idx;
    t->num_triangles = 1;
    t->face_tri_counts = tri_counts;
    t->num_faces = 1;
    t->must_calculate_normals = 1;
}

static void
make_item(prc_api_write_rep_item *ri, prc_api_write_ri_kind_t kind)
{
    memset(ri, 0, sizeof(*ri));
    ri->kind = kind;
    ri->biased_tessellation_index = 1;
}

/* Column-major translation. */
static void
set_translation(prc_api_write_node *n, double x, double y, double z)
{
    memset(n->transform, 0, sizeof(n->transform));
    n->transform[0] = n->transform[5] = n->transform[10] = n->transform[15] = 1.0;
    n->transform[12] = x;
    n->transform[13] = y;
    n->transform[14] = z;
    n->has_transform = 1;
    n->is_identity = 0;
}

static void
assert_box(const double lo[3], const double hi[3],
    double x0, double y0, double z0, double x1, double y1, double z1)
{
    PRC_ASSERT_NEAR(lo[0], x0, EPS);
    PRC_ASSERT_NEAR(lo[1], y0, EPS);
    PRC_ASSERT_NEAR(lo[2], z0, EPS);
    PRC_ASSERT_NEAR(hi[0], x1, EPS);
    PRC_ASSERT_NEAR(hi[1], y1, EPS);
    PRC_ASSERT_NEAR(hi[2], z1, EPS);
}

/* The triangle under a node moved by (1500, 1490, 10): the box is moved
   with it, not the stored (0,0,0)-(1,1,0). */
static void
test_translated_part(prc_context *ctx)
{
    prc_api_write_tessellation t;
    prc_api_write_rep_item ri;
    prc_api_write_node leaf, root, *kids[1];
    double lo[3], hi[3];
    uint8_t has = 0;

    printf("  sub-case: a translated part's box is moved with it\n");
    make_triangle(&t);
    make_item(&ri, PRC_API_WRITE_RI_SURFACE);
    memset(&leaf, 0, sizeof(leaf));
    leaf.rep_items = &ri;
    leaf.num_rep_items = 1;
    set_translation(&leaf, 1500.0, 1490.0, 10.0);
    kids[0] = &leaf;
    memset(&root, 0, sizeof(root));
    root.children = kids;
    root.num_children = 1;

    PRC_ASSERT_EQ(prc_api_write_bounds(ctx, &root, &t, 1, lo, hi, &has), 0);
    PRC_ASSERT_EQ(has, 1);
    assert_box(lo, hi, 1500, 1490, 10, 1501, 1491, 10);
}

/* Parent moved by (10, 0, 0), child turned 90 degrees about z. The child's
   turn applies first: (1,0,0) goes to (0,1,0) and (0,1,0) to (-1,0,0), then
   the parent's move adds 10 to x. */
static void
test_two_levels(prc_context *ctx)
{
    prc_api_write_tessellation t;
    prc_api_write_rep_item ri;
    prc_api_write_node leaf, mid, root, *mid_kids[1], *root_kids[1];
    double lo[3], hi[3];
    uint8_t has = 0;

    printf("  sub-case: transforms compose from the root down\n");
    make_triangle(&t);
    make_item(&ri, PRC_API_WRITE_RI_SURFACE);
    memset(&leaf, 0, sizeof(leaf));
    leaf.rep_items = &ri;
    leaf.num_rep_items = 1;
    memset(leaf.transform, 0, sizeof(leaf.transform));
    leaf.transform[1] = 1.0;    /* x axis -> +y */
    leaf.transform[4] = -1.0;   /* y axis -> -x */
    leaf.transform[10] = 1.0;
    leaf.transform[15] = 1.0;
    leaf.has_transform = 1;
    mid_kids[0] = &leaf;
    memset(&mid, 0, sizeof(mid));
    mid.children = mid_kids;
    mid.num_children = 1;
    set_translation(&mid, 10.0, 0.0, 0.0);
    root_kids[0] = &mid;
    memset(&root, 0, sizeof(root));
    root.children = root_kids;
    root.num_children = 1;

    PRC_ASSERT_EQ(prc_api_write_bounds(ctx, &root, &t, 1, lo, hi, &has), 0);
    PRC_ASSERT_EQ(has, 1);
    assert_box(lo, hi, 9, 0, 0, 10, 1, 0);
}

/* One tessellation drawn by two nodes in different places. */
static void
test_two_instances(prc_context *ctx)
{
    prc_api_write_tessellation t;
    prc_api_write_rep_item ri;
    prc_api_write_node a, b, root, *kids[2];
    double lo[3], hi[3];
    uint8_t has = 0;

    printf("  sub-case: one tessellation drawn twice covers both places\n");
    make_triangle(&t);
    make_item(&ri, PRC_API_WRITE_RI_SURFACE);
    memset(&a, 0, sizeof(a));
    a.rep_items = &ri;
    a.num_rep_items = 1;
    set_translation(&a, -5.0, 0.0, 0.0);
    memset(&b, 0, sizeof(b));
    b.rep_items = &ri;
    b.num_rep_items = 1;
    set_translation(&b, 5.0, 0.0, 2.0);
    kids[0] = &a;
    kids[1] = &b;
    memset(&root, 0, sizeof(root));
    root.children = kids;
    root.num_children = 2;

    PRC_ASSERT_EQ(prc_api_write_bounds(ctx, &root, &t, 1, lo, hi, &has), 0);
    PRC_ASSERT_EQ(has, 1);
    assert_box(lo, hi, -5, 0, 0, 6, 1, 2);
}

static void
test_wire_only(prc_context *ctx)
{
    static const float wpos[6] = { -1.0f, 2.0f, 3.0f,  4.0f, -5.0f, 6.0f };
    prc_api_write_wire_element w;
    prc_api_write_tessellation t;
    prc_api_write_rep_item ri;
    prc_api_write_node root;
    double lo[3], hi[3];
    uint8_t has = 0;

    printf("  sub-case: a wire-only model has a box\n");
    memset(&w, 0, sizeof(w));
    w.positions = wpos;
    w.num_vertices = 2;
    memset(&t, 0, sizeof(t));
    t.kind = PRC_API_WRITE_TESS_KIND_WIRE;
    t.wire_elements = &w;
    t.num_wire_elements = 1;
    make_item(&ri, PRC_API_WRITE_RI_WIRE);
    memset(&root, 0, sizeof(root));
    root.rep_items = &ri;
    root.num_rep_items = 1;

    PRC_ASSERT_EQ(prc_api_write_bounds(ctx, &root, &t, 1, lo, hi, &has), 0);
    PRC_ASSERT_EQ(has, 1);
    assert_box(lo, hi, -1, -5, 3, 4, 2, 6);
}

static void
test_nothing_drawn(prc_context *ctx)
{
    prc_api_write_node root;
    double lo[3] = { 7, 7, 7 }, hi[3] = { 8, 8, 8 };
    uint8_t has = 1;

    printf("  sub-case: a tree that draws nothing has no box\n");
    memset(&root, 0, sizeof(root));
    root.has_empty_part = 1;
    PRC_ASSERT_EQ(prc_api_write_bounds(ctx, &root, NULL, 0, lo, hi, &has), 0);
    PRC_ASSERT_EQ(has, 0);
    assert_box(lo, hi, 7, 7, 7, 8, 8, 8);
}

static void
test_bad_index_refused(prc_context *ctx)
{
    prc_api_write_tessellation t;
    prc_api_write_rep_item ri;
    prc_api_write_node root;
    double lo[3], hi[3];
    uint8_t has = 1;

    printf("  sub-case: an index the writer refuses is refused here too\n");
    make_triangle(&t);
    make_item(&ri, PRC_API_WRITE_RI_SURFACE);
    ri.biased_tessellation_index = 2;
    memset(&root, 0, sizeof(root));
    root.rep_items = &ri;
    root.num_rep_items = 1;
    PRC_ASSERT(prc_api_write_bounds(ctx, &root, &t, 1, lo, hi, &has) < 0);
    PRC_ASSERT_EQ(has, 0);
}

static void
test_view_from_bounds(void)
{
    static const double lo[3] = { 1, 2, 3 }, hi[3] = { 3, 6, 7 };
    static const double point[3] = { 4, 5, 6 };
    prc_pdf_view_spec v;
    double diag = 6.0;   /* sqrt(2*2 + 4*4 + 4*4) */

    printf("  sub-case: the view looks at the centre from +x -y +z, z up\n");
    prc_api_pdf_view_from_bounds(lo, hi, &v);
    PRC_ASSERT(strcmp(v.name, "Default") == 0);
    PRC_ASSERT_EQ(v.is_default, 1);
    PRC_ASSERT_NEAR(v.target[0], 2.0, EPS);
    PRC_ASSERT_NEAR(v.target[1], 4.0, EPS);
    PRC_ASSERT_NEAR(v.target[2], 5.0, EPS);
    PRC_ASSERT_NEAR(v.eye[0], 2.0 + diag * 1.0, EPS);
    PRC_ASSERT_NEAR(v.eye[1], 4.0 - diag * 1.3, EPS);
    PRC_ASSERT_NEAR(v.eye[2], 5.0 + diag * 0.7, EPS);
    PRC_ASSERT_NEAR(v.up[0], 0.0, EPS);
    PRC_ASSERT_NEAR(v.up[1], 0.0, EPS);
    PRC_ASSERT_NEAR(v.up[2], 1.0, EPS);

    printf("  sub-case: a box of one point still puts the camera away from it\n");
    prc_api_pdf_view_from_bounds(point, point, &v);
    PRC_ASSERT_NEAR(v.target[0], 4.0, EPS);
    PRC_ASSERT(v.eye[0] != v.target[0]);
}

/* Write the translated part, embed it with the view, read the view back:
   the camera sits where the view put it and looks at the moved centre. */
static void
test_end_to_end(prc_context *ctx)
{
    prc_api_write_tessellation t;
    prc_api_write_rep_item ri;
    prc_api_write_node leaf, root, *kids[1];
    prc_pdf_view_spec view;
    prc_pdf_write_options opts;
    double lo[3], hi[3];
    uint8_t has = 0;
    uint8_t *buf = NULL;
    size_t size = 0;
    prc_api_data data;
    char *name = NULL;
    double *m = NULL;
    double co = 0.0;
    int a;

    printf("  sub-case: the embedded view looks at the placed model\n");
    make_triangle(&t);
    make_item(&ri, PRC_API_WRITE_RI_SURFACE);
    memset(&leaf, 0, sizeof(leaf));
    leaf.rep_items = &ri;
    leaf.num_rep_items = 1;
    leaf.bbox_max[0] = leaf.bbox_max[1] = 1.0;
    set_translation(&leaf, 1500.0, 1490.0, 10.0);
    kids[0] = &leaf;
    memset(&root, 0, sizeof(root));
    root.name = "root";
    root.children = kids;
    root.num_children = 1;

    PRC_ASSERT_EQ(prc_api_write_bounds(ctx, &root, &t, 1, lo, hi, &has), 0);
    PRC_ASSERT_EQ(has, 1);
    prc_api_pdf_view_from_bounds(lo, hi, &view);
    PRC_ASSERT_EQ(prc_api_write_prc_buffer(ctx, "default_view", &root, &t, 1, &buf, &size), 0);
    memset(&opts, 0, sizeof(opts));
    opts.views = &view;
    opts.num_views = 1;
    PRC_ASSERT_EQ(prc_api_pdf_embed_prc(ctx, "test_pdf_default_view.pdf", buf, size, &opts), 0);
    prc_api_write_prc_buffer_free(ctx, buf);

    data = prc_api_open_contents(ctx, "test_pdf_default_view.pdf");
    PRC_ASSERT_NOT_NULL(data);
    PRC_ASSERT_EQ(prc_api_get_number_of_view(ctx, data), 1);
    PRC_ASSERT_EQ(prc_api_get_view(ctx, data, 0, &name, &m, &co), 0);
    PRC_ASSERT_NOT_NULL(name);
    PRC_ASSERT(strcmp(name, "Default") == 0);
    PRC_ASSERT_NOT_NULL(m);
    /* /C2W holds the axes and then the camera position, written to 6
       decimals; the third axis points from the camera to what it looks at,
       /CO away. */
    for (a = 0; a < 3; a++)
    {
        PRC_ASSERT_NEAR(m[9 + a], view.eye[a], 1e-5);
        PRC_ASSERT_NEAR(m[9 + a] + co * m[6 + a], view.target[a], 1e-4);
    }
    PRC_ASSERT_NEAR(view.target[0], 1500.5, EPS);
    PRC_ASSERT_NEAR(view.target[1], 1490.5, EPS);
    PRC_ASSERT_NEAR(view.target[2], 10.0, EPS);
    prc_api_release_data(ctx, data, NULL, 0, NULL, 0, NULL, 0, NULL);
    remove("test_pdf_default_view.pdf");
}

int
main(void)
{
    prc_context *ctx;

    PRC_TEST_BEGIN("test_pdf_default_view");

    ctx = prc_api_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    test_translated_part(ctx);
    test_two_levels(ctx);
    test_two_instances(ctx);
    test_wire_only(ctx);
    test_nothing_drawn(ctx);
    test_bad_index_refused(ctx);
    test_view_from_bounds();
    test_end_to_end(ctx);

    PRC_ASSERT_EQ(prc_api_release_context(ctx), 0);

    PRC_TEST_END;
}
