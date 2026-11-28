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

/* Planar regions from the loops of a face.

   1. A planar face can have several loops with none inside another: a writer that merges the
      coplanar end caps of several cylinders into one face stores one loop per cap. Such a face
      has no single outer loop, so prc_assign_outer_loop_by_nesting with allow_regions marks each
      loop that nothing contains as outer, and prc_triangulate_planar_regions triangulates each
      outer loop with the loops inside it as its holes. Without allow_regions the old result
      stands: no outer loop is found and the call fails.

   2. Loops nested several deep (the outline a trimmed grid leaves round a sphere's band, the
      outline of the grid's own hole, and a trimming circle inside that) make rings between
      consecutive levels: prc_mark_loops_by_nesting_depth marks the even levels outer and the
      odd ones holes, and each hole goes to the innermost outer loop round it.

   3. A boundary with repeated points, points on a straight run, or a sliver that doubles back
      along an edge must still triangulate, and cover the polygon's area. */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "prc_test.h"
#include "prc_api.h"
#include "prc_tessellation_exact_geom.h"

#define SIDES 32

/* A closed polygon of SIDES sides (the last sample repeats the first), as a loop in uv */
static void
make_circle(prc_loop_samples *loop, double cx, double cy, double r)
{
    uint32_t i;

    memset(loop, 0, sizeof *loop);
    loop->num_samples = SIDES + 1;
    loop->uv_samples = (prc_vec2 *)calloc(SIDES + 1, sizeof(prc_vec2));
    for (i = 0; i < SIDES; i++)
    {
        double a = 2.0 * 3.14159265358979323846 * (double)i / (double)SIDES;

        loop->uv_samples[i].x = cx + r * cos(a);
        loop->uv_samples[i].y = cy + r * sin(a);
    }
    loop->uv_samples[SIDES] = loop->uv_samples[0];
}

static int
all_indices_valid(const uint32_t *tris, uint32_t num_tris, uint32_t num_verts)
{
    uint32_t i;

    for (i = 0; i < num_tris * 3; i++)
        if (tris[i] >= num_verts)
            return 0;
    return 1;
}

static double
triangles_area(const prc_vec2 *verts, const uint32_t *tris, uint32_t num_tris)
{
    uint32_t t;
    double sum = 0.0;

    for (t = 0; t < num_tris; t++)
    {
        prc_vec2 a = verts[tris[t * 3]], b = verts[tris[t * 3 + 1]], c = verts[tris[t * 3 + 2]];

        sum += 0.5 * fabs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y));
    }
    return sum;
}

/* Triangulates one polygon given as x, y pairs; returns the result code, and the area the
   triangles cover through *area_out */
static int
triangulate_polygon(prc_context *ctx, const double *xy, int n, double *area_out)
{
    prc_loop_samples loop;
    prc_vec2 *verts = NULL;
    uint32_t *tris = NULL;
    uint32_t nv = 0, nt = 0;
    int i, rc;

    memset(&loop, 0, sizeof loop);
    loop.num_samples = (uint32_t)n + 1;
    loop.uv_samples = (prc_vec2 *)calloc((size_t)n + 1, sizeof(prc_vec2));
    for (i = 0; i < n; i++)
    {
        loop.uv_samples[i].x = xy[2 * i];
        loop.uv_samples[i].y = xy[2 * i + 1];
    }
    loop.uv_samples[n] = loop.uv_samples[0];
    loop.is_outer_loop = 1;
    rc = prc_triangulate_planar_regions(ctx, 1, &loop, &verts, &nv, &tris, &nt);
    *area_out = 0.0;
    if (rc >= 0)
    {
        PRC_ASSERT(all_indices_valid(tris, nt, nv));
        *area_out = triangles_area(verts, tris, nt);
        prc_free(ctx, verts);
        prc_free(ctx, tris);
    }
    free(loop.uv_samples);
    return rc;
}

int
main(void)
{
    prc_context *ctx;
    prc_loop_samples loops[5];
    prc_vec2 *verts = NULL;
    uint32_t *tris = NULL;
    uint32_t nv = 0, nt = 0, i, outers;

    PRC_TEST_BEGIN("planar_face_regions");

    ctx = prc_api_new_context(NULL);
    PRC_ASSERT(ctx != NULL);

    /* Five separate discs, as on the plane face of mower_carb: a large one and four small
       ones round it, none containing another */
    make_circle(&loops[0], 0.0, 0.0, 3.0);
    make_circle(&loops[1], 5.0, 0.0, 0.3);
    make_circle(&loops[2], -5.0, 0.0, 0.3);
    make_circle(&loops[3], 0.0, 5.0, 0.3);
    make_circle(&loops[4], 0.0, -5.0, 0.3);

    PRC_ASSERT(prc_assign_outer_loop_by_nesting(ctx, 5, loops, 0) != 0);
    PRC_ASSERT_EQ(prc_assign_outer_loop_by_nesting(ctx, 5, loops, 1), 0);
    for (i = 0, outers = 0; i < 5; i++)
        outers += loops[i].is_outer_loop ? 1 : 0;
    PRC_ASSERT_EQ(outers, 5);

    PRC_ASSERT_EQ(prc_triangulate_planar_regions(ctx, 5, loops, &verts, &nv, &tris, &nt), 0);
    PRC_ASSERT_EQ(nt, 5 * (SIDES - 2));
    PRC_ASSERT_EQ(nv, 5 * SIDES);
    PRC_ASSERT(all_indices_valid(tris, nt, nv));
    prc_free(ctx, verts);
    prc_free(ctx, tris);
    verts = NULL;
    tris = NULL;
    for (i = 0; i < 5; i++)
        free(loops[i].uv_samples);

    /* A disc with a hole, and a separate disc: the hole goes with the disc that holds it */
    make_circle(&loops[0], 0.0, 0.0, 3.0);
    make_circle(&loops[1], 0.5, 0.0, 1.0);
    make_circle(&loops[2], 8.0, 0.0, 1.0);

    PRC_ASSERT_EQ(prc_assign_outer_loop_by_nesting(ctx, 3, loops, 1), 0);
    PRC_ASSERT_EQ(loops[0].is_outer_loop, 1);
    PRC_ASSERT_EQ(loops[1].is_outer_loop, 0);
    PRC_ASSERT_EQ(loops[2].is_outer_loop, 1);

    PRC_ASSERT_EQ(prc_triangulate_planar_regions(ctx, 3, loops, &verts, &nv, &tris, &nt), 0);
    /* The disc with its hole is a ring of 2 * SIDES vertices plus 2 for the bridge, so
       2 * SIDES triangles; the other disc takes SIDES - 2 */
    PRC_ASSERT_EQ(nt, 2 * SIDES + (SIDES - 2));
    PRC_ASSERT(all_indices_valid(tris, nt, nv));
    prc_free(ctx, verts);
    prc_free(ctx, tris);
    for (i = 0; i < 3; i++)
        free(loops[i].uv_samples);

    /* The usual face, one loop holding the rest, is unchanged with or without allow_regions */
    make_circle(&loops[0], 0.0, 0.0, 3.0);
    make_circle(&loops[1], 0.5, 0.0, 1.0);
    PRC_ASSERT_EQ(prc_assign_outer_loop_by_nesting(ctx, 2, loops, 1), 0);
    PRC_ASSERT_EQ(loops[0].is_outer_loop, 1);
    PRC_ASSERT_EQ(loops[1].is_outer_loop, 0);
    free(loops[0].uv_samples);
    free(loops[1].uv_samples);

    /* Four circles, one inside the next, as a sphere's band gives with its two trimming
       circles and the two outlines of its kept grid: depths 0 and 2 are outer, 1 and 3 holes.
       The regions are the ring between the first two and the ring between the last two, each
       2 * SIDES triangles with its bridge */
    make_circle(&loops[0], 0.0, 0.0, 4.0);
    make_circle(&loops[1], 0.0, 0.0, 3.0);
    make_circle(&loops[2], 0.0, 0.0, 2.0);
    make_circle(&loops[3], 0.0, 0.0, 1.0);
    prc_mark_loops_by_nesting_depth(4, loops);
    PRC_ASSERT_EQ(loops[0].is_outer_loop, 1);
    PRC_ASSERT_EQ(loops[1].is_outer_loop, 0);
    PRC_ASSERT_EQ(loops[2].is_outer_loop, 1);
    PRC_ASSERT_EQ(loops[3].is_outer_loop, 0);

    PRC_ASSERT_EQ(prc_triangulate_planar_regions(ctx, 4, loops, &verts, &nv, &tris, &nt), 0);
    PRC_ASSERT_EQ(nt, 4 * SIDES);
    PRC_ASSERT(all_indices_valid(tris, nt, nv));
    {
        /* Regular SIDES-gons: the area is SIDES / 2 * r * r * sin(2 * pi / SIDES) */
        double k = 0.5 * SIDES * sin(2.0 * 3.14159265358979323846 / SIDES);
        double expect = k * ((16.0 - 9.0) + (4.0 - 1.0));

        PRC_ASSERT_NEAR(triangles_area(verts, tris, nt), expect, 1e-9);
    }
    prc_free(ctx, verts);
    prc_free(ctx, tris);
    for (i = 0; i < 4; i++)
        free(loops[i].uv_samples);

    /* Boundaries that ear clipping used to be left stuck on. Each must triangulate and
       cover the polygon (area 2, 6 and about 10) */
    {
        /* a repeated vertex and points on straight runs */
        static const double A[] = { 0,0, 1,0, 1,0, 2,0, 2,1, 1,1, 0,1, 0,0.5 };
        /* a flat back-track along the bottom edge */
        static const double C[] = { 0,0, 2,0, 1,0, 3,0, 3,2, 0,2 };
        /* a sliver that doubles back along the top edge, with a repeated point */
        static const double E[] = { 0,0, 10,0, 10,1, 7.46,1, 8.246,1, 8.256,0.9996, 6.676,1, 6.676,1, 0,1 };
        double area;

        PRC_ASSERT_EQ(triangulate_polygon(ctx, A, 8, &area), 0);
        PRC_ASSERT_NEAR(area, 2.0, 1e-9);
        PRC_ASSERT_EQ(triangulate_polygon(ctx, C, 6, &area), 0);
        PRC_ASSERT_NEAR(area, 6.0, 1e-9);
        PRC_ASSERT_EQ(triangulate_polygon(ctx, E, 9, &area), 0);
        PRC_ASSERT_NEAR(area, 10.0, 1e-2);
    }

    PRC_ASSERT_EQ(prc_api_release_context(ctx), 0);
    PRC_TEST_END;
}
