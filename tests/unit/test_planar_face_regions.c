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

/* A planar face can have several loops with none inside another: a writer that merges the
   coplanar end caps of several cylinders into one face stores one loop per cap. Such a face
   has no single outer loop, so prc_assign_outer_loop_by_nesting with allow_regions marks each
   loop that nothing contains as outer, and prc_triangulate_planar_regions triangulates each
   outer loop with the loops inside it as its holes. Without allow_regions the old result
   stands: no outer loop is found and the call fails. */

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

    PRC_ASSERT_EQ(prc_api_release_context(ctx), 0);
    PRC_TEST_END;
}
