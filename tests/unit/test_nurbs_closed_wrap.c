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

/* A NURBS surface that closes on itself along u (here a square tube: four
   unit edges round, degree 1, u from 0 to 4 with the last control point equal to
   the first) has loops that go round it or cross the seam where u = 0 and
   u = 4 meet. prc_map_loops_to_surface must report a loop that goes once round as
   winding once in u, with u running on from its start to its start plus the
   period, and a loop that only crosses the seam as winding 0 with u continuous
   across it (not jumping by the period). A surface that is not closed is left
   alone. */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "prc_test.h"
#include "prc_api.h"
#include "prc_tessellation_exact_geom.h"

#define PER_EDGE 8

static prc_vec3
tube_point(double s, double z)
{
    static const double cx[5] = { 0.0, 1.0, 1.0, 0.0, 0.0 };
    static const double cy[5] = { 0.0, 0.0, 1.0, 1.0, 0.0 };
    prc_vec3 p;
    int e;
    double f;

    s = fmod(s, 4.0);
    if (s < 0.0)
        s += 4.0;
    e = (int)floor(s);
    f = s - (double)e;
    p.x = cx[e] * (1.0 - f) + cx[e + 1] * f;
    p.y = cy[e] * (1.0 - f) + cy[e + 1] * f;
    p.z = z;
    return p;
}

int
main(void)
{
    prc_context *ctx;
    prc_surf_nurbs nurbs;
    prc_type_surf surface;
    prc_control_points_nurbs_surf cp[10];
    double knots_u[7] = { 0.0, 0.0, 1.0, 2.0, 3.0, 4.0, 4.0 };
    double knots_v[4] = { 0.0, 0.0, 1.0, 1.0 };
    prc_loop_samples ring, patch;
    prc_vec3 ring_pts[33], patch_pts[33];
    uint32_t i, n;
    double lo, hi;
    int monotone = 1;

    PRC_TEST_BEGIN("nurbs_closed_wrap");

    ctx = prc_api_new_context(NULL);
    PRC_ASSERT(ctx != NULL);

    memset(&nurbs, 0, sizeof nurbs);
    memset(&surface, 0, sizeof surface);
    for (i = 0; i < 5; i++)
    {
        prc_vec3 a = tube_point((double)i, 0.0);

        cp[i * 2].x = a.x;     cp[i * 2].y = a.y;     cp[i * 2].z = 0.0; cp[i * 2].w = 1.0;
        cp[i * 2 + 1].x = a.x; cp[i * 2 + 1].y = a.y; cp[i * 2 + 1].z = 1.0; cp[i * 2 + 1].w = 1.0;
    }
    nurbs.du = 1;
    nurbs.dv = 1;
    nurbs.highest_index_of_control_points_u = 4;
    nurbs.highest_index_of_control_points_v = 1;
    nurbs.highest_index_of_knots_u = 6;
    nurbs.highest_index_of_knots_v = 3;
    nurbs.p = cp;
    nurbs.knot_vector_u = knots_u;
    nurbs.knot_vector_v = knots_v;
    surface.surface_type = PRC_TYPE_SURF_NURBS;
    surface.surf_nurbs = &nurbs;

    /* Closure is found by evaluation for a NURBS that does not say */
    PRC_ASSERT_EQ(prc_nurbs_surface_is_closed(ctx, &nurbs, 0), 1);
    PRC_ASSERT_EQ(prc_nurbs_surface_is_closed(ctx, &nurbs, 1), 0);

    /* A loop once round the tube at z = 0.5, starting half way along the first edge */
    n = 4 * PER_EDGE;
    for (i = 0; i <= n; i++)
        ring_pts[i] = tube_point(0.5 + 4.0 * (double)i / (double)n, 0.5);
    memset(&ring, 0, sizeof ring);
    ring.num_samples = n + 1;
    ring.samples = ring_pts;
    PRC_ASSERT_EQ(prc_map_loops_to_surface(ctx, NULL, 0, &surface, 1, &ring), 0);
    PRC_ASSERT_EQ(ring.wind_u, 1);
    PRC_ASSERT_EQ(ring.wind_v, 0);
    for (i = 1; i <= n; i++)
        if (ring.uv_samples[i].x < ring.uv_samples[i - 1].x)
            monotone = 0;
    PRC_ASSERT(monotone);
    PRC_ASSERT_NEAR(ring.uv_samples[n].x - ring.uv_samples[0].x, 4.0, 1e-6);
    prc_free(ctx, ring.uv_samples);

    /* A small loop that crosses the seam (u = 3.9 to 0.1) and does not go round it */
    {
        static const double ds[4] = { -0.1, 0.1, 0.1, -0.1 };
        static const double dz[4] = { 0.3, 0.3, 0.7, 0.7 };
        uint32_t k, c = 0;

        for (k = 0; k < 4; k++)
        {
            uint32_t m;
            double s0 = ds[k], z0 = dz[k];
            double s1 = ds[(k + 1) % 4], z1 = dz[(k + 1) % 4];

            for (m = 0; m < 8; m++)
            {
                double t = (double)m / 8.0;
                patch_pts[c++] = tube_point(s0 + (s1 - s0) * t, z0 + (z1 - z0) * t);
            }
        }
        patch_pts[c++] = patch_pts[0];
        memset(&patch, 0, sizeof patch);
        patch.num_samples = c;
        patch.samples = patch_pts;
        PRC_ASSERT_EQ(prc_map_loops_to_surface(ctx, NULL, 0, &surface, 1, &patch), 0);
        PRC_ASSERT_EQ(patch.wind_u, 0);
        lo = hi = patch.uv_samples[0].x;
        for (i = 1; i < c; i++)
        {
            if (patch.uv_samples[i].x < lo) lo = patch.uv_samples[i].x;
            if (patch.uv_samples[i].x > hi) hi = patch.uv_samples[i].x;
        }
        PRC_ASSERT_NEAR(hi - lo, 0.2, 1e-6);
        prc_free(ctx, patch.uv_samples);
    }

    PRC_ASSERT_EQ(prc_api_release_context(ctx), 0);
    PRC_TEST_END;
}
