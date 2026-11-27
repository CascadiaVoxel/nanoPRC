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

/* The coedges of a loop meet end to start within LOOP_DEGENERACY_TEST. Ends that are a little
   apart, as CAD rounding leaves them, still chain; ends that are far apart do not, and neither
   does a loop that fails to close. */

#include <stdint.h>
#include "prc_test.h"
#include "prc_api.h"
#include "prc_tessellation_exact_geom.h"

/* A square of three coedges of two samples each, then a fourth back to the start. gap moves the
   start of the second coedge away from the end of the first */
static void
square(prc_coedge_samples *c, prc_vec3 *pts, double gap, double close_gap)
{
    uint32_t i;

    /* corners */
    pts[0].x = 0; pts[0].y = 0; pts[0].z = 0;
    pts[1].x = 1; pts[1].y = 0; pts[1].z = 0;
    pts[2].x = 1 + gap; pts[2].y = 0; pts[2].z = 0;
    pts[3].x = 1; pts[3].y = 1; pts[3].z = 0;
    pts[4].x = 1; pts[4].y = 1; pts[4].z = 0;
    pts[5].x = 0; pts[5].y = 1; pts[5].z = 0;
    pts[6].x = 0; pts[6].y = 1; pts[6].z = 0;
    pts[7].x = 0; pts[7].y = close_gap; pts[7].z = 0;
    for (i = 0; i < 4; i++)
    {
        c[i].num_samples = 2;
        c[i].samples = &pts[i * 2];
    }
}

int
main(void)
{
    prc_coedge_samples c[4];
    prc_vec3 pts[8];

    PRC_TEST_BEGIN("loop_degeneracy");

    /* the tolerance itself: well above 1e-6, which broke loops that were a rounding apart */
    PRC_ASSERT(LOOP_DEGENERACY_TEST >= 1e-4 && LOOP_DEGENERACY_TEST <= 1e-2);

    square(c, pts, 0.0, 0.0);
    PRC_ASSERT_EQ(prc_loop_coedges_chain(c, 4, LOOP_DEGENERACY_TEST), 1);

    /* ends 1e-5 apart (rounding) chain; both in the middle and at the closing */
    square(c, pts, 1e-5, 0.0);
    PRC_ASSERT_EQ(prc_loop_coedges_chain(c, 4, LOOP_DEGENERACY_TEST), 1);
    square(c, pts, 0.0, 1e-5);
    PRC_ASSERT_EQ(prc_loop_coedges_chain(c, 4, LOOP_DEGENERACY_TEST), 1);

    /* ends 0.1 apart do not */
    square(c, pts, 0.1, 0.0);
    PRC_ASSERT_EQ(prc_loop_coedges_chain(c, 4, LOOP_DEGENERACY_TEST), 0);
    /* nor does a loop that stops 0.5 short of where it began */
    square(c, pts, 0.0, 0.5);
    PRC_ASSERT_EQ(prc_loop_coedges_chain(c, 4, LOOP_DEGENERACY_TEST), 0);

    /* the same 1e-5 gap does break a chain tested at the old 1e-6 */
    square(c, pts, 1e-5, 0.0);
    PRC_ASSERT_EQ(prc_loop_coedges_chain(c, 4, 1e-6), 0);

    /* one coedge always chains */
    PRC_ASSERT_EQ(prc_loop_coedges_chain(c, 1, LOOP_DEGENERACY_TEST), 1);

    PRC_TEST_END;
}
