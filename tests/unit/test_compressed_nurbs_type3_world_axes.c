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

/* An interior control point of type 3 stores its offset from the plane
   prediction in the world axes, not in the local frame (U, N x U, N) that
   types 1 and 2 use. This is what the mower_carb file of prc-db shows: its
   type-3 triples equal the world-space offsets of its helical thread, and with
   them the thread surface closes (seam gap 4.5e-3 before, 1.9e-5 after, one
   net unit being 2e-5). A 2 x 2 net checked by hand, in which the local frame
   and the world axes differ, so a decode in the local frame gives another point. */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "prc_test.h"
#include "prc_tessellation_exact_geom.h"

/* P00 = 0, V = (6,0,0), U = (0,0,8): the plane prediction is (6,0,8) and the
   local frame is x = (0,0,1), y = (1,0,0), z = (0,1,0). The tolerance 0.01
   makes the net unit 100 x 0.01 = 1. */
static void
decode(int type, int x, int y, int z, prc_control_points_nurbs_surf *out)
{
    prc_compressed_control_points cp;
    prc_interior_compressed_control_points interior;
    prc_vec3 in_v, in_u;
    int code;

    memset(&cp, 0, sizeof cp);
    memset(&interior, 0, sizeof interior);
    in_v.x = 6.0; in_v.y = 0.0; in_v.z = 0.0;
    in_u.x = 0.0; in_u.y = 0.0; in_u.z = 8.0;
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

    PRC_TEST_BEGIN("compressed_nurbs_type3_world_axes");

    /* Type 3: (3, -4, 5) added to (6, 0, 8) in the world axes: (9, -4, 13).
       Read in the local frame it would be (2, 5, 11). */
    decode(3, 3, -4, 5, out);
    PRC_ASSERT_NEAR(out[3].x, 9.0, 1e-12);
    PRC_ASSERT_NEAR(out[3].y, -4.0, 1e-12);
    PRC_ASSERT_NEAR(out[3].z, 13.0, 1e-12);

    /* Type 2 stays in the local frame: x along U, y along N x U:
       (6,0,8) + 3 (0,0,1) - 4 (1,0,0) = (2, 0, 11). */
    decode(2, 3, -4, 0, out);
    PRC_ASSERT_NEAR(out[3].x, 2.0, 1e-12);
    PRC_ASSERT_NEAR(out[3].y, 0.0, 1e-12);
    PRC_ASSERT_NEAR(out[3].z, 11.0, 1e-12);

    /* Type 1 stays in the local frame: z along N = (0,1,0): (6, 5, 8). */
    decode(1, 0, 0, 5, out);
    PRC_ASSERT_NEAR(out[3].x, 6.0, 1e-12);
    PRC_ASSERT_NEAR(out[3].y, 5.0, 1e-12);
    PRC_ASSERT_NEAR(out[3].z, 8.0, 1e-12);

    PRC_TEST_END;
}
