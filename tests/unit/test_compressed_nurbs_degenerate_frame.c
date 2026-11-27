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

/* A compressed NURBS net with a collapsed edge (every point of its first row
   the same) has no local frame at its interior points: V, and so the normal,
   have zero length. The decode divided by that length and produced NaN control
   points (8 of the 32 patches of the teapot in prc-db). With no frame, a point
   of type 0 is the plane prediction and a stored point (type 3) is plain
   x, y, z in units of 100 x NURBS_tolerance. A 2 x 2 net checked by hand. */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "prc_test.h"
#include "prc_tessellation_exact_geom.h"

static void
decode(int type, int x, int y, int z, prc_control_points_nurbs_surf *out)
{
    prc_compressed_control_points cp;
    prc_interior_compressed_control_points interior;
    prc_vec3 in_v, in_u;
    int code;

    memset(&cp, 0, sizeof cp);
    memset(&interior, 0, sizeof interior);
    cp.p00.x = 0.0; cp.p00.y = 0.0; cp.p00.z = 0.0;
    in_v.x = 0.0; in_v.y = 0.0; in_v.z = 0.0;   /* collapsed first row: V = 0 */
    in_u.x = 0.0; in_u.y = 7.0; in_u.z = 0.0;   /* 7 in model units: the tolerance below makes the unit 1 */
    interior.type = (uint32_t)type;
    interior.Pijx = x;
    interior.Pijy = y;
    interior.Pijz = z;
    cp.ccpt_in_v = &in_v;
    cp.ccpt_in_u = &in_u;
    cp.ccpt_interior = &interior;

    memset(out, 0, 4 * sizeof(prc_control_points_nurbs_surf));
    code = prc_decompress_control_points(NULL, 2, 2, &cp, 0, NULL, 0.01, out);
    PRC_ASSERT_EQ(code, 0);
}

int
main(void)
{
    prc_control_points_nurbs_surf out[4];
    int k;

    PRC_TEST_BEGIN("compressed_nurbs_degenerate_frame");

    /* Type 0: the plane prediction, P00 + V + U = (0, 7, 0). */
    decode(0, 0, 0, 0, out);
    for (k = 0; k < 4; k++)
    {
        PRC_ASSERT(isfinite(out[k].x));
        PRC_ASSERT(isfinite(out[k].y));
        PRC_ASSERT(isfinite(out[k].z));
    }
    PRC_ASSERT_NEAR(out[3].x, 0.0, 1e-12);
    PRC_ASSERT_NEAR(out[3].y, 7.0, 1e-12);
    PRC_ASSERT_NEAR(out[3].z, 0.0, 1e-12);

    /* Type 3: plain x, y, z added to the plane prediction: (3, 7 - 4, 5). */
    decode(3, 3, -4, 5, out);
    for (k = 0; k < 4; k++)
    {
        PRC_ASSERT(isfinite(out[k].x));
        PRC_ASSERT(isfinite(out[k].y));
        PRC_ASSERT(isfinite(out[k].z));
    }
    PRC_ASSERT_NEAR(out[3].x, 3.0, 1e-12);
    PRC_ASSERT_NEAR(out[3].y, 3.0, 1e-12);
    PRC_ASSERT_NEAR(out[3].z, 5.0, 1e-12);

    PRC_TEST_END;
}
