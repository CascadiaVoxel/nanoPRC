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

/* The length of is_point_color_on_face in a compressed tessellation
   (Table 175) depends on the stored has_faces: one entry per face when it is
   TRUE, a single entry when it is FALSE.

   Both fixtures are the same unit cube: 12 triangles in 6 faces, normals
   recalculated, one flat colour per face. pcof_hasfacesT_6bit stores
   has_faces TRUE and six bits; pcof_hasfacesF_1bit stores has_faces FALSE and
   one bit. Adobe Acrobat and PDF-XChange Editor draw both with the right
   colours (November 2026), and refuse or lose the colours of the same cube
   written with the bit counts swapped. Each must read here with every face
   coloured and every corner carrying its own face's colour. */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "prc_test.h"
#include "prc_context.h"
#include "prc_data.h"
#include "prc_api.h"

#ifndef PRC_TEST_FIXTURES_DIR
#define PRC_TEST_FIXTURES_DIR "tests/fixtures"
#endif

/* Face order as written: -z, +z, -y, +y, -x, +x */
static const float FACE_RGB[6][3] = {
    { 1, 1, 0 }, { 0, 0, 1 }, { 1, 0, 1 },
    { 0, 1, 0 }, { 0, 1, 1 }, { 1, 0, 0 }
};

static void
check_cube(prc_context *ctx, const char *path, int has_faces)
{
    prc_data *pd;
    prc_tess_3d_compressed *t;
    uint32_t d, c, k;

    pd = (prc_data *)prc_api_open_contents(ctx, path);
    if (pd == NULL)
        prc_api_print_error_stack(ctx);
    PRC_ASSERT_NOT_NULL(pd);
    PRC_ASSERT_EQ(pd->file_struct[0].tessellation->tess_count, 1);
    t = pd->file_struct[0].tessellation->tess[0].tess_3d_compressed;
    PRC_ASSERT_NOT_NULL(t);

    PRC_ASSERT_EQ(t->has_faces, has_faces);
    PRC_ASSERT_EQ(t->face_number, 6);
    PRC_ASSERT_EQ(t->triangle_face_array_size, 12);
    PRC_ASSERT_EQ(t->is_point_color, 1);
    for (k = 0; k < t->face_number; k++)
        PRC_ASSERT_EQ(t->is_point_color_on_face[k], 1);
    PRC_ASSERT_EQ(t->point_color_array_size, 12 * 3 * 5);
    PRC_ASSERT_NOT_NULL(t->point_colors_prc_compressed_3d);

    for (d = 0; d < t->triangle_face_array_size; d++)
    {
        uint32_t f = t->triangle_face_array[d];

        PRC_ASSERT(f < 6);
        for (c = 0; c < 3; c++)
        {
            const float *got = &t->point_colors_prc_compressed_3d[(size_t)(d * 3 + c) * 4];

            PRC_ASSERT_NEAR(got[0], FACE_RGB[f][0], 1e-6);
            PRC_ASSERT_NEAR(got[1], FACE_RGB[f][1], 1e-6);
            PRC_ASSERT_NEAR(got[2], FACE_RGB[f][2], 1e-6);
            PRC_ASSERT_NEAR(got[3], 1.0, 1e-6);
        }
    }

    prc_api_release_data(ctx, (prc_api_data)pd, NULL, 0, NULL, 0, NULL, 0, NULL);
}

int
main(void)
{
    prc_context *ctx;

    PRC_TEST_BEGIN("is_point_color_on_face length");

    ctx = prc_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    check_cube(ctx, PRC_TEST_FIXTURES_DIR "/pcof_hasfacesT_6bit.prc", 1);
    check_cube(ctx, PRC_TEST_FIXTURES_DIR "/pcof_hasfacesF_1bit.prc", 0);

    prc_release_context(ctx);

    PRC_TEST_END;
}
