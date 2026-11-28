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

/* A band of a torus is bounded by two circles that each go once round the torus (here round
   the axis, so they wind once in u), one traversed the opposite way to the other. They sit at
   two angles round the tube, here -0.968 radians and 0. The two loops are mapped into uv one by
   one, each with its tube angle reduced on its own, so the lower can land below 0 and the upper
   at 0; the band is then not a stretch of the grid's domain [0, 2 * pi) and no grid point lies
   inside it. prc_map_loops_to_surface must hand back the pair as one stretch: the loop wound the
   positive way (the lower edge of the band) inside [0, 2 * pi), and the other not below it, so
   the band is 0.968 radians wide. */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "prc_test.h"
#include "prc_api.h"
#include "prc_tessellation_exact_geom.h"

#define PI 3.14159265358979323846
#define SAMPLES 32

/* A circle round the axis of a torus at tube angle v; going_up traverses it with u increasing */
static void
make_ring(prc_loop_samples *loop, prc_vec3 *pts, double major, double minor, double v, int going_up)
{
    uint32_t i;

    for (i = 0; i <= SAMPLES; i++)
    {
        double f = (double)i / (double)SAMPLES;
        double u = 2.0 * PI * (going_up ? f : 1.0 - f);
        double r = major + minor * cos(v);

        pts[i].x = r * cos(u);
        pts[i].y = r * sin(u);
        pts[i].z = minor * sin(v);
    }
    memset(loop, 0, sizeof *loop);
    loop->num_samples = SAMPLES + 1;
    loop->samples = pts;
}

int
main(void)
{
    prc_context *ctx;
    prc_surf_torus torus;
    prc_type_surf surface;
    prc_loop_samples loops[2];
    prc_vec3 pts_a[SAMPLES + 1], pts_b[SAMPLES + 1];
    double va, vb;

    PRC_TEST_BEGIN("torus_band_loops");

    ctx = prc_api_new_context(NULL);
    PRC_ASSERT(ctx != NULL);

    memset(&torus, 0, sizeof torus);
    memset(&surface, 0, sizeof surface);
    torus.major_radius = 3.0;
    torus.minor_radius = 1.0;
    torus.exact_geom_transform.is_identity = 1;
    surface.surface_type = PRC_TYPE_SURF_Torus;
    surface.surf_torus = &torus;

    /* Loop 0 is the lower edge of the band (u increasing, wound the positive way), at tube
       angle -0.968; loop 1 the upper edge (u decreasing) at tube angle 0 */
    make_ring(&loops[0], pts_a, 3.0, 1.0, -0.968, 1);
    make_ring(&loops[1], pts_b, 3.0, 1.0, 0.0, 0);

    PRC_ASSERT_EQ(prc_map_loops_to_surface(ctx, NULL, 0, &surface, 2, loops), 0);
    PRC_ASSERT_EQ(loops[0].wind_u, 1);
    PRC_ASSERT_EQ(loops[0].wind_v, 0);
    PRC_ASSERT_EQ(loops[1].wind_u, -1);
    PRC_ASSERT_EQ(loops[1].wind_v, 0);

    va = loops[0].uv_samples[0].y;
    vb = loops[1].uv_samples[0].y;
    PRC_ASSERT(va >= 0.0 && va < 2.0 * PI);
    PRC_ASSERT(vb >= va);
    PRC_ASSERT_NEAR(vb - va, 0.968, 1e-6);

    prc_free(ctx, loops[0].uv_samples);
    prc_free(ctx, loops[1].uv_samples);

    PRC_ASSERT_EQ(prc_api_release_context(ctx), 0);
    PRC_TEST_END;
}
