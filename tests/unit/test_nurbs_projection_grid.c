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

/* A point on the surface is projected back to the parameters it came from. The
   surface is a thread: a flat strip (2 control points across, u) wound 24 times
   round an axis (8 control points per turn along v, degree 1). A 32 x 32 grid has
   fewer than 2 samples per turn in v, so its nearest sample can be on the wrong
   turn; a grid with several samples per control point finds the right one. */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "prc_test.h"
#include "prc_tessellation_exact_geom.h"

#define TURNS 24
#define PER_TURN 8
#define NUM_V (TURNS * PER_TURN + 1)

int
main(void)
{
    prc_surf_nurbs nurbs;
    prc_surface_params params;
    static prc_control_points_nurbs_surf cp[2 * NUM_V];
    static double knots_u[4] = { 0.0, 0.0, 1.0, 1.0 };
    static double knots_v[NUM_V + 2];
    const double pi = 3.14159265358979323846;
    const double pitch = 0.5; /* rise per turn */
    double target_u = 0.25, target_v = 20.0 * PER_TURN + 3.5; /* turn 20, mid-segment */
    double v0, v1, a0, a1;
    prc_vec3 target, found;
    double u_out = -1.0, v_out = -1.0;
    uint32_t i, j;
    uint32_t seg;

    PRC_TEST_BEGIN("nurbs_projection_grid");

    memset(&nurbs, 0, sizeof nurbs);
    memset(&params, 0, sizeof params);
    for (j = 0; j < NUM_V; j++)
    {
        double a = 2.0 * pi * (double)j / (double)PER_TURN;
        double z = pitch * (double)j / (double)PER_TURN;

        for (i = 0; i < 2; i++)
        {
            double r = 1.0 + (double)i; /* the strip spans radius 1 to 2 */
            cp[i * NUM_V + j].x = r * cos(a);
            cp[i * NUM_V + j].y = r * sin(a);
            cp[i * NUM_V + j].z = z;
            cp[i * NUM_V + j].w = 1.0;
        }
    }
    knots_v[0] = 0.0;
    for (j = 1; j <= NUM_V; j++)
        knots_v[j] = (double)(j - 1);
    knots_v[NUM_V + 1] = (double)(NUM_V - 1);

    nurbs.du = 1;
    nurbs.dv = 1;
    nurbs.highest_index_of_control_points_u = 1;
    nurbs.highest_index_of_control_points_v = NUM_V - 1;
    nurbs.highest_index_of_knots_u = 3;
    nurbs.highest_index_of_knots_v = NUM_V + 1;
    nurbs.p = cp;
    nurbs.knot_vector_u = knots_u;
    nurbs.knot_vector_v = knots_v;
    params.surface_params = &nurbs;

    /* The target, by hand: bilinear within segment seg. */
    seg = (uint32_t)target_v;
    v0 = (double)seg; v1 = v0 + 1.0;
    a0 = 2.0 * pi * v0 / PER_TURN; a1 = 2.0 * pi * v1 / PER_TURN;
    {
        double tv = target_v - v0;
        double r = 1.0 + target_u;
        /* radius varies with u only; the strip is flat between the two edges
           of each segment, so interpolate the two edge points in u then in v */
        double x0 = (1.0 - target_u) * cos(a0) + target_u * 2.0 * cos(a0);
        double y0 = (1.0 - target_u) * sin(a0) + target_u * 2.0 * sin(a0);
        double x1 = (1.0 - target_u) * cos(a1) + target_u * 2.0 * cos(a1);
        double y1 = (1.0 - target_u) * sin(a1) + target_u * 2.0 * sin(a1);
        (void)r;
        target.x = (1.0 - tv) * x0 + tv * x1;
        target.y = (1.0 - tv) * y0 + tv * y1;
        target.z = pitch * (v0 + tv) / PER_TURN;
    }

    found = prc_project_point_onto_surface_grid(NULL, prc_evaluate_surf_nurbs, &params,
        0.0, 1.0, 0.0, (double)(NUM_V - 1), target, 32, 4 * NUM_V, &u_out, &v_out);
    PRC_ASSERT_NEAR(found.x, target.x, 1e-6);
    PRC_ASSERT_NEAR(found.y, target.y, 1e-6);
    PRC_ASSERT_NEAR(found.z, target.z, 1e-6);
    PRC_ASSERT_NEAR(v_out, target_v, 1e-6);
    PRC_ASSERT_NEAR(u_out, target_u, 1e-6);

    PRC_TEST_END;
}
