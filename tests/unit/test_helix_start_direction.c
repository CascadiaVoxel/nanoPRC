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

/* PRC_TYPE_CRV_Helix01, sub-type 0, start on the axis (pdf-issues #853).
   The curve is evaluated through the sampling information the exact-geometry
   tessellator uses. With the start on the axis the frame's x direction has
   no defined value; Adobe Acrobat takes it as unit(axis x (1, 0, 0)) for a
   tilted axis, and as world +y for an axis parallel to x, whichever way the
   axis points (read in Acrobat on the three files of 16 November 2026). Also
   checked: the radius has no floor (it is the start's distance from the axis
   plus parameter * radius evolution, so it goes negative) and the pitch
   advances along the unit axis. */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "prc_test.h"
#include "prc_tessellation_exact_geom.h"

#define PITCH 0.5
#define RADIUS_EVOLUTION 2.1

static prc_vec3
eval_helix(double ax, double ay, double az, double t)
{
    prc_crv_helix01 helix;
    prc_ptr_curve curve;
    prc_curve_sampling_info info;

    memset(&helix, 0, sizeof(helix));
    memset(&curve, 0, sizeof(curve));
    memset(&info, 0, sizeof(info));
    helix.type = 0;
    helix.orientation = 1;
    helix.type0_helix.direction_0 = ax;
    helix.type0_helix.direction_1 = ay;
    helix.type0_helix.direction_2 = az;
    helix.type0_helix.pitch = PITCH;
    helix.type0_helix.radius = RADIUS_EVOLUTION;
    helix.parameterization.interval.min_value = -10.0;
    helix.parameterization.interval.max_value = 12.5664;
    curve.curve_type = PRC_TYPE_CRV_Helix01;
    curve.crv_helix01 = &helix;
    PRC_ASSERT_EQ(prc_get_curve_sample_info(NULL, &curve, &info), 0);
    PRC_ASSERT_NOT_NULL(info.curve_eval_func);
    return info.curve_eval_func(NULL, info.curve_params, t);
}

/* Unit vector from the axis to the curve at parameter t, the axis being the
   unit vector (ux, uy, uz) through the origin */
static prc_vec3
radial_direction(double ux, double uy, double uz, double t, double axis_x, double axis_y, double axis_z)
{
    prc_vec3 p = eval_helix(axis_x, axis_y, axis_z, t);
    double along = p.x * ux + p.y * uy + p.z * uz;
    double len;

    p.x -= along * ux;
    p.y -= along * uy;
    p.z -= along * uz;
    len = sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    PRC_ASSERT(len > 0.0);
    p.x /= len;
    p.y /= len;
    p.z /= len;
    return p;
}

int
main(void)
{
    prc_vec3 d;
    prc_vec3 p;
    double n;

    PRC_TEST_BEGIN("helix type 0, start on the axis: start direction, radius, pitch");

    /* Axis along +x and along -x: the curve leaves the axis along +y in both */
    d = radial_direction(1.0, 0.0, 0.0, 0.001, 1.0, 0.0, 0.0);
    PRC_ASSERT_NEAR(d.x, 0.0, 1e-9);
    PRC_ASSERT_NEAR(d.y, 1.0, 5e-3);
    PRC_ASSERT_NEAR(d.z, 0.0, 5e-3);
    d = radial_direction(-1.0, 0.0, 0.0, 0.001, -1.0, 0.0, 0.0);
    PRC_ASSERT_NEAR(d.x, 0.0, 1e-9);
    PRC_ASSERT_NEAR(d.y, 1.0, 5e-3);
    PRC_ASSERT_NEAR(d.z, 0.0, 5e-3);

    /* Tilted axis (1, 2, 3): unit(axis x (1, 0, 0)) = unit(0, 3, -2) */
    n = sqrt(14.0);
    d = radial_direction(1.0 / n, 2.0 / n, 3.0 / n, 0.001, 1.0, 2.0, 3.0);
    PRC_ASSERT_NEAR(d.x, 0.0, 5e-3);
    PRC_ASSERT_NEAR(d.y, 3.0 / sqrt(13.0), 5e-3);
    PRC_ASSERT_NEAR(d.z, -2.0 / sqrt(13.0), 5e-3);

    /* No floor on the radius: before the parameter 0 it is negative, so the
       curve leaves the axis the other way, and is 2.1 * |t| from it */
    d = radial_direction(1.0, 0.0, 0.0, -0.001, 1.0, 0.0, 0.0);
    PRC_ASSERT_NEAR(d.y, -1.0, 5e-3);
    p = eval_helix(1.0, 0.0, 0.0, -1.0);
    PRC_ASSERT_NEAR(sqrt(p.y * p.y + p.z * p.z), RADIUS_EVOLUTION, 1e-9);

    /* The pitch advances along the unit axis, per unit of parameter */
    p = eval_helix(1.0, 2.0, 3.0, 5.0);
    PRC_ASSERT_NEAR((p.x + 2.0 * p.y + 3.0 * p.z) / n, PITCH * 5.0, 1e-9);

    PRC_TEST_END;
}
