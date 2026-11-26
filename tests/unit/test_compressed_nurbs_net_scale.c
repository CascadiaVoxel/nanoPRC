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

/* The stored control-net numbers of a compressed NURBS surface are in units of
   100 x NURBS_tolerance, and a rational net is returned weighted. A 2 x 2 net
   with its corner p00, one step in v, one step in u and one interior point of
   type 1 (an offset along the normal only), checked by hand. */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "prc_test.h"
#include "prc_tessellation_exact_geom.h"

int
main(void)
{
    prc_compressed_control_points cp;
    prc_interior_compressed_control_points interior;
    prc_compressed_weights w;
    prc_vec3 in_v, in_u;
    prc_control_points_nurbs_surf out[4];
    double weights[4] = { 2.0, 2.0, 0.5, 4.0 };
    double tol = 0.002; /* net unit = 100 * tol = 0.2 */
    int code;

    PRC_TEST_BEGIN("compressed_nurbs_net_scale");

    memset(&cp, 0, sizeof cp);
    memset(&w, 0, sizeof w);
    memset(out, 0, sizeof out);
    cp.p00.x = 1.0; cp.p00.y = 2.0; cp.p00.z = 3.0;
    in_v.x = 10.0; in_v.y = 0.0; in_v.z = 0.0;  /* 2.0 in model units */
    in_u.x = 0.0; in_u.y = 20.0; in_u.z = 0.0;  /* 4.0 in model units */
    interior.type = 1;
    interior.Pijz = 5;                          /* 1.0 along the normal, which is -z here */
    interior.Pijx = 0;
    interior.Pijy = 0;
    cp.ccpt_in_v = &in_v;
    cp.ccpt_in_u = &in_u;
    cp.ccpt_interior = &interior;

    /* Non-rational: positions only, w = 1. */
    code = prc_decompress_control_points(NULL, 2, 2, &cp, 0, NULL, tol, out);
    PRC_ASSERT_EQ(code, 0);
    PRC_ASSERT_NEAR(out[0].x, 1.0, 1e-12);
    PRC_ASSERT_NEAR(out[1].x, 3.0, 1e-12);
    PRC_ASSERT_NEAR(out[1].y, 2.0, 1e-12);
    PRC_ASSERT_NEAR(out[2].x, 1.0, 1e-12);
    PRC_ASSERT_NEAR(out[2].y, 6.0, 1e-12);
    PRC_ASSERT_NEAR(out[3].x, 3.0, 1e-12);
    PRC_ASSERT_NEAR(out[3].y, 6.0, 1e-12);
    PRC_ASSERT_NEAR(out[3].z, 2.0, 1e-12);
    PRC_ASSERT_NEAR(out[3].w, 1.0, 1e-12);

    /* Rational: the same positions, each multiplied by its weight. */
    w.number_bit_weight = 31;
    w.weights = weights;
    code = prc_decompress_control_points(NULL, 2, 2, &cp, 1, &w, tol, out);
    PRC_ASSERT_EQ(code, 0);
    PRC_ASSERT_NEAR(out[0].x, 1.0 * 2.0, 1e-12);
    PRC_ASSERT_NEAR(out[1].x, 3.0 * 2.0, 1e-12);
    PRC_ASSERT_NEAR(out[2].y, 6.0 * 0.5, 1e-12);
    PRC_ASSERT_NEAR(out[3].y, 6.0 * 4.0, 1e-12);
    PRC_ASSERT_NEAR(out[3].z, 2.0 * 4.0, 1e-12);
    PRC_ASSERT_NEAR(out[3].w, 4.0, 1e-12);

    PRC_TEST_END;
}
