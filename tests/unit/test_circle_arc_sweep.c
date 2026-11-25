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

/* The sweep of a circle arc of a compressed B-rep (PRC_HCG_Circle). The sweep
   was zero for every arc whose radius was 0.1 model units or less, whatever
   the units, and for every half circle given as a start, an end and the
   middle of the arc. The circle evaluator returns the origin for a sweep of
   zero. The same arcs are drawn here at radii of half a millimetre and ten
   millimetres written in metres, at one unit and at a hundred thousand. */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "prc_test.h"
#include "prc_tessellation_exact_geom.h"

#define PI 3.14159265358979323846

static void
set_point(prc_compressed_point *p, double x, double y, double z)
{
    memset(p, 0, sizeof(*p));
    p->point.x = x;
    p->point.y = y;
    p->point.z = z;
}

/* A general circle: center, start and end, and the bit that says the arc is
   longer than a half turn */
static int
general_arc(prc_hcg_circle_information *info, const double *center, double r,
    double end_x, double end_y, uint8_t longer_than_pi)
{
    prc_hcg_circle circle;

    memset(&circle, 0, sizeof(circle));
    circle.is_particular_circle = 0;
    circle.general_circle.compressed_iso_spline = 0;
    circle.general_circle.circle_angle = longer_than_pi;
    set_point(&circle.general_circle.center, center[0], center[1], center[2]);
    set_point(&circle.general_circle.start_end_data.start_point, center[0] + r, center[1], center[2]);
    set_point(&circle.general_circle.start_end_data.end_point, center[0] + end_x * r,
              center[1] + end_y * r, center[2]);
    return prc_get_hcg_circle_data(NULL, &circle, info, NULL);
}

/* A particular circle that is an arc: start, end and the middle of the arc */
static int
particular_arc(prc_hcg_circle_information *info, const double *center, double r,
    double mid_y_sign)
{
    prc_hcg_circle circle;

    memset(&circle, 0, sizeof(circle));
    circle.is_particular_circle = 1;
    circle.particular_circle.full_circle = 0;
    circle.particular_circle.compressed_iso_spline = 0;
    set_point(&circle.particular_circle.start_end_data.start_point, center[0] + r, center[1], center[2]);
    set_point(&circle.particular_circle.start_end_data.end_point, center[0] - r, center[1], center[2]);
    set_point(&circle.particular_circle.middle_of_arc, center[0], center[1] + mid_y_sign * r, center[2]);
    return prc_get_hcg_circle_data(NULL, &circle, info, NULL);
}

int
main(void)
{
    const double radii[4] = { 5.0e-4, 1.0e-2, 1.0, 1.0e5 };
    const double center[3] = { 10.0, 20.0, 30.0 };
    int i;

    PRC_TEST_BEGIN("circle arcs: sweep at any size, half circles from their middle");

    for (i = 0; i < 4; i++)
    {
        double r = radii[i];
        prc_hcg_circle_information info;

        /* A quarter turn, counter-clockwise about +z */
        PRC_ASSERT_EQ(general_arc(&info, center, r, 0.0, 1.0, 0), 0);
        PRC_ASSERT_NEAR(info.theta, PI / 2.0, 1e-6);

        /* The same end point with the bit set: the long way round, three
           quarters of a turn, clockwise */
        PRC_ASSERT_EQ(general_arc(&info, center, r, 0.0, 1.0, 1), 0);
        PRC_ASSERT_NEAR(info.theta, 3.0 * PI / 2.0, 1e-6);

        /* A half circle, its middle above the diameter: a sweep of pi about +z */
        PRC_ASSERT_EQ(particular_arc(&info, center, r, 1.0), 0);
        PRC_ASSERT_NEAR(info.theta, PI, 1e-9);
        PRC_ASSERT(info.has_normal);
        PRC_ASSERT(info.normal.z > 0.0);
        PRC_ASSERT_NEAR(info.center.x, center[0], 1e-6 * r + 1e-9);

        /* Its middle below the diameter: the other way round, about -z */
        PRC_ASSERT_EQ(particular_arc(&info, center, r, -1.0), 0);
        PRC_ASSERT_NEAR(info.theta, PI, 1e-9);
        PRC_ASSERT(info.normal.z < 0.0);

        /* An arc with its start at its center has no radius: no sweep */
        PRC_ASSERT_EQ(general_arc(&info, center, 0.0, 0.0, 1.0, 0), 0);
        PRC_ASSERT_NEAR(info.theta, 0.0, 1e-12);
    }

    PRC_TEST_END;
}
