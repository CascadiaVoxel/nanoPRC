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

/* The sampling data of a ruled surface. prc_get_surface_data set only the
   domain for it and left the sample counts, the linear flags and the precision
   at zero, so the grid builder asked for a 0 x 0 allocation and the face (and
   with it the whole file, as for the "teapot" of prc-db) failed with an
   allocation error. A ruled surface between two straight lines: u runs along
   the ruling and is linear, v runs along the first curve. */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "prc_test.h"
#include "prc_tessellation_exact_geom.h"

int
main(void)
{
    prc_crv_line line1, line2;
    prc_surf_ruled ruled;
    prc_type_surf surface;
    prc_surface_sampling_info info;
    int code;

    PRC_TEST_BEGIN("ruled_surface_sampling");

    memset(&line1, 0, sizeof line1);
    memset(&line2, 0, sizeof line2);
    memset(&ruled, 0, sizeof ruled);
    memset(&surface, 0, sizeof surface);
    memset(&info, 0, sizeof info);

    line1.parameterization.interval.min_value = 0.0;
    line1.parameterization.interval.max_value = 5.0;
    line1.parameterization.coeff_a = 1.0;
    line2 = line1;

    ruled.first_curve.is_referenced = 0;
    ruled.first_curve.curve_type = PRC_TYPE_CRV_Line;
    ruled.first_curve.crv_line = &line1;
    ruled.second_curve.is_referenced = 0;
    ruled.second_curve.curve_type = PRC_TYPE_CRV_Line;
    ruled.second_curve.crv_line = &line2;
    ruled.parameterization.swap_uv = 0;
    ruled.parameterization.surface_domain.min_uv.x = 0.0;
    ruled.parameterization.surface_domain.min_uv.y = 0.0;
    ruled.parameterization.surface_domain.max_uv.x = 1.0;
    ruled.parameterization.surface_domain.max_uv.y = 5.0;

    surface.surface_type = PRC_TYPE_SURF_Ruled;
    surface.surf_ruled = &ruled;

    code = prc_get_surface_data(NULL, &surface, &info);
    PRC_ASSERT_EQ(code, 0);

    /* The sample counts and the precision are set (they were zero). */
    PRC_ASSERT(info.num_samples_u >= 2);
    PRC_ASSERT(info.num_samples_v >= 2);
    PRC_ASSERT(info.precision_u > 0.0);
    PRC_ASSERT(info.precision_v > 0.0);

    /* u is the straight ruling; a line is not periodic. */
    PRC_ASSERT_EQ(info.u_linear, 1);
    PRC_ASSERT_EQ(info.v_linear, 0);
    PRC_ASSERT_EQ(info.u_periodic, 0);
    PRC_ASSERT_EQ(info.v_periodic, 0);

    /* The domain: u over the ruling 0..1, v over the curve. */
    PRC_ASSERT_NEAR(info.start_u, 0.0, 1e-12);
    PRC_ASSERT_NEAR(info.end_u, 1.0, 1e-12);
    PRC_ASSERT_NEAR(info.start_v, 0.0, 1e-12);
    PRC_ASSERT_NEAR(info.end_v, 5.0, 1e-12);

    PRC_TEST_END;
}
