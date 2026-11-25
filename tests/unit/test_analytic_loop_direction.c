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

/* Which way each curve of an analytic face's loop is walked. A curve that the
   loop names by index runs the other way round by default; in the files of
   some writers (Bracket with PMI, the Landing Gear and pmi_sample of prc-db)
   that leaves the loop open, and walking each curve in whichever direction
   meets the end the loop has reached closes it. prc_choose_loop_curve_directions
   keeps the default when it closes every joint, and replaces it only by a walk
   that closes every joint. The loop is a unit square in the first test cases
   (A = (0,0,0), B = (1,0,0), C = (1,1,0), D = (0,1,0)); the last scales it. */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "prc_test.h"
#include "prc_tessellation_exact_geom.h"

static prc_vec3
pt(double x, double y)
{
    prc_vec3 v;

    v.x = x;
    v.y = y;
    v.z = 0.0;
    return v;
}

static int
same(const uint8_t *got, const uint8_t *want, uint32_t n)
{
    return memcmp(got, want, n) == 0;
}

int
main(void)
{
    prc_vec3 a = pt(0, 0), b = pt(1, 0), c = pt(1, 1), d = pt(0, 1);
    prc_vec3 start[4], end[4];
    uint8_t def[4], out[4];
    uint8_t want[4];
    uint32_t k;
    double scales[3] = { 1.0e-4, 1.0, 1.0e5 };
    int s;

    PRC_TEST_BEGIN("analytic face loop: direction of each curve");

    /* All four curves stored in the direction of the loop, default all
       forward: closes, so it is kept and nothing changes */
    start[0] = a; end[0] = b; start[1] = b; end[1] = c;
    start[2] = c; end[2] = d; start[3] = d; end[3] = a;
    memset(def, 0, 4);
    PRC_ASSERT_EQ(prc_choose_loop_curve_directions(start, end, def, 4, out), 0);
    PRC_ASSERT(same(out, def, 4));

    /* Curves 1 and 3 stored against the loop, and named by index: the default
       reverses exactly those two, which closes the loop, so it is kept */
    start[1] = c; end[1] = b; start[3] = a; end[3] = d;
    def[0] = 0; def[1] = 1; def[2] = 0; def[3] = 1;
    PRC_ASSERT_EQ(prc_choose_loop_curve_directions(start, end, def, 4, out), 0);
    PRC_ASSERT(same(out, def, 4));

    /* The same loop, but curve 1 is named by index and stored in the direction
       of the loop. The default reverses it and the loop is open; walking each
       curve in the direction that meets the end reached reverses only curve 3 */
    start[1] = b; end[1] = c;
    PRC_ASSERT_EQ(prc_choose_loop_curve_directions(start, end, def, 4, out), 1);
    want[0] = 0; want[1] = 0; want[2] = 0; want[3] = 1;
    PRC_ASSERT(same(out, want, 4));

    /* Every curve stored against the loop and the default all forward: the
       walk that starts with the first curve reversed closes it */
    start[0] = b; end[0] = a; start[1] = c; end[1] = b;
    start[2] = d; end[2] = c; start[3] = a; end[3] = d;
    memset(def, 0, 4);
    PRC_ASSERT_EQ(prc_choose_loop_curve_directions(start, end, def, 4, out), 1);
    want[0] = 1; want[1] = 1; want[2] = 1; want[3] = 1;
    PRC_ASSERT(same(out, want, 4));

    /* Two curves from A to B, as two halves of a ring: both ways round close
       the loop and each differs from the default in one curve; the first
       is taken */
    start[0] = a; end[0] = b; start[1] = a; end[1] = b;
    def[0] = 0; def[1] = 0;
    PRC_ASSERT_EQ(prc_choose_loop_curve_directions(start, end, def, 2, out), 1);
    want[0] = 0; want[1] = 1;
    PRC_ASSERT(same(out, want, 2));

    /* A gap that no direction closes: the default is kept */
    start[0] = a; end[0] = b; start[1] = b; end[1] = c;
    start[2] = c; end[2] = d; start[3] = pt(0, 0.5); end[3] = pt(0, 0.25);
    memset(def, 0, 4);
    PRC_ASSERT_EQ(prc_choose_loop_curve_directions(start, end, def, 4, out), 0);
    PRC_ASSERT(same(out, def, 4));

    /* One curve, or no extent: nothing to choose */
    PRC_ASSERT_EQ(prc_choose_loop_curve_directions(start, end, def, 1, out), 0);
    PRC_ASSERT_EQ(out[0], def[0]);
    start[0] = a; end[0] = a; start[1] = a; end[1] = a;
    PRC_ASSERT_EQ(prc_choose_loop_curve_directions(start, end, def, 2, out), 0);

    /* The tolerance goes with the size of the loop: the open loop of the third
       case at a ten thousandth and at a hundred thousand times the size */
    for (s = 0; s < 3; s++)
    {
        double f = scales[s];

        start[0] = pt(0, 0); end[0] = pt(f, 0); start[1] = pt(f, 0); end[1] = pt(f, f);
        start[2] = pt(f, f); end[2] = pt(0, f); start[3] = pt(0, 0); end[3] = pt(0, f);
        def[0] = 0; def[1] = 1; def[2] = 0; def[3] = 1;
        PRC_ASSERT_EQ(prc_choose_loop_curve_directions(start, end, def, 4, out), 1);
        want[0] = 0; want[1] = 0; want[2] = 0; want[3] = 1;
        PRC_ASSERT(same(out, want, 4));
    }

    /* Whatever the loop, the result never closes fewer joints than the default
       would have: a loop closed by the default is never changed */
    for (k = 0; k < 16; k++)
    {
        uint32_t j;

        start[0] = a; end[0] = b; start[1] = b; end[1] = c;
        start[2] = c; end[2] = d; start[3] = d; end[3] = a;
        for (j = 0; j < 4; j++)
        {
            def[j] = (uint8_t)((k >> j) & 1);
            if (def[j])
            {
                prc_vec3 t = start[j];

                start[j] = end[j];
                end[j] = t;
            }
        }
        /* start and end now hold the curve as stored, reversed where the
           default reverses it, so the default closes the loop */
        PRC_ASSERT_EQ(prc_choose_loop_curve_directions(start, end, def, 4, out), 0);
        PRC_ASSERT(same(out, def, 4));
    }

    PRC_TEST_END;
}
