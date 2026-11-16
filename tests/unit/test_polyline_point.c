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

/* A PolyLinePoint (ISO 14739-1, Table 282) is a Vector3D when the curve is
   3D and a Vector2D, two doubles, when it is 2D. The reader used to take a
   2D point as one double, which left every later field of the curve one
   double out of step. Each case writes a point followed by a marker, reads
   the point back, and checks both coordinates and that the marker is what
   comes next. */

#include <stdlib.h>
#include <stdint.h>
#include "prc_test.h"
#include "prc_context.h"
#include "prc_data.h"
#include "prc_bit.h"
#include "prc_parse_extra_geometry.h"

#define MARKER 0x5A5A1234u

static void
test_point_2d(prc_context *ctx)
{
    prc_bit_write_state w;
    prc_bit_state r;
    prc_polyline_point point;

    PRC_ASSERT_EQ(prc_bitwrite_init(ctx, &w, 64), 0);
    PRC_ASSERT_EQ(prc_bitwrite_double(ctx, &w, 1.25), 0);
    PRC_ASSERT_EQ(prc_bitwrite_double(ctx, &w, -7.5), 0);
    PRC_ASSERT_EQ(prc_bitwrite_uint32(ctx, &w, MARKER), 0);
    PRC_ASSERT_EQ(prc_bitwrite_flush(ctx, &w), 0);

    prc_init_bit_state(ctx, &r, w.buf, w.byte_pos);
    prc_parse_polyline_point(ctx, &r, 0, &point);
    PRC_ASSERT_EQ(point.point_2d.x, 1.25);
    PRC_ASSERT_EQ(point.point_2d.y, -7.5);
    PRC_ASSERT_EQ(prc_bitread_uint32(ctx, &r), MARKER);

    prc_bitwrite_release(ctx, &w);
}

static void
test_point_3d(prc_context *ctx)
{
    prc_bit_write_state w;
    prc_bit_state r;
    prc_polyline_point point;

    PRC_ASSERT_EQ(prc_bitwrite_init(ctx, &w, 64), 0);
    PRC_ASSERT_EQ(prc_bitwrite_double(ctx, &w, 2.0), 0);
    PRC_ASSERT_EQ(prc_bitwrite_double(ctx, &w, -3.5), 0);
    PRC_ASSERT_EQ(prc_bitwrite_double(ctx, &w, 4.75), 0);
    PRC_ASSERT_EQ(prc_bitwrite_uint32(ctx, &w, MARKER), 0);
    PRC_ASSERT_EQ(prc_bitwrite_flush(ctx, &w), 0);

    prc_init_bit_state(ctx, &r, w.buf, w.byte_pos);
    prc_parse_polyline_point(ctx, &r, 1, &point);
    PRC_ASSERT_EQ(point.point_3d.x, 2.0);
    PRC_ASSERT_EQ(point.point_3d.y, -3.5);
    PRC_ASSERT_EQ(point.point_3d.z, 4.75);
    PRC_ASSERT_EQ(prc_bitread_uint32(ctx, &r), MARKER);

    prc_bitwrite_release(ctx, &w);
}

int
main(void)
{
    prc_context *ctx;

    PRC_TEST_BEGIN("test_polyline_point");

    ctx = prc_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    test_point_2d(ctx);
    test_point_3d(ctx);

    prc_release_context(ctx);

    PRC_TEST_END;
}
