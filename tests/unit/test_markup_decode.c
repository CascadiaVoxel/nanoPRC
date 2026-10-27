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

/* The markup (PMI) tessellation decoder takes its codes and coordinates from the file. A code array cut
   short, or entities that declare more doubles than are stored, used to be read past the end of the
   arrays, and a polyline declaring more points than were stored was also written past the end of the
   decoded vertex buffer. Each such markup must now be refused, and a well-formed one still decoded. */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "prc_test.h"
#include "prc_context.h"
#include "prc_data.h"
#include "prc_decode_markup_tess.h"

#define EXTRA(type, inner) (PRC_MARKUP_IsExtraData | ((uint32_t)(type) << 21) | (uint32_t)(inner))

/* The decoder allocates its outputs; release them the way prc_release.c does. */
static void
release_decoded(prc_context *ctx, prc_tess_markup *m)
{
    uint32_t k;
    if (m->decode_primitives != NULL)
    {
        for (k = 0; k < m->decode_number_primitives; k++)
        {
            prc_free(ctx, m->decode_primitives[k].indices);
            prc_free(ctx, m->decode_primitives[k].text);
        }
        prc_free(ctx, m->decode_primitives);
    }
    prc_free(ctx, m->decode_vertices);
    memset(m, 0, sizeof(*m));
}

/* Decode a markup built from the given codes and doubles, copied into allocations of exactly their
   own size so that a read past either end is a read past an allocation. Returns the decoder's code. */
static int
decode(prc_context *ctx, const uint32_t *codes, uint32_t num_codes, const double *coords,
       uint32_t num_coords, uint32_t *out_primitives, uint32_t *out_vertices)
{
    prc_tess_markup m;
    int code;

    memset(&m, 0, sizeof(m));
    m.number_of_codes = num_codes;
    m.code_numbers = (prc_unsigned_int *)malloc(sizeof(prc_unsigned_int) * (num_codes ? num_codes : 1));
    if (num_codes)
        memcpy(m.code_numbers, codes, sizeof(uint32_t) * num_codes);
    m.tessellation_coordinates.number_of_coordinates = num_coords;
    m.tessellation_coordinates.coordinates = (double *)malloc(sizeof(double) * (num_coords ? num_coords : 1));
    if (num_coords)
        memcpy(m.tessellation_coordinates.coordinates, coords, sizeof(double) * num_coords);

    code = prc_decode_markup_tess(ctx, &m);
    if (out_primitives) *out_primitives = m.decode_number_primitives;
    if (out_vertices) *out_vertices = m.decode_num_vertices;

    free(m.code_numbers);
    free(m.tessellation_coordinates.coordinates);
    release_decoded(ctx, &m);
    return code;
}

static void
test_wellformed(prc_context *ctx)
{
    /* A two-point polyline, then a matrix block around a three-point polyline. */
    const uint32_t codes[] = { 0, 6, PRC_MARKUP_IsMatrix | 1, 16, 0, 9, PRC_MARKUP_IsMatrix, 0 };
    double coords[6 + 16 + 9];
    uint32_t prims = 0, verts = 0, i;

    printf("  sub-case: a well-formed markup decodes\n");
    for (i = 0; i < 6; i++) coords[i] = (double)i;
    memset(coords + 6, 0, sizeof(double) * 16);
    coords[6] = coords[11] = coords[16] = coords[21] = 1.0;     /* identity */
    for (i = 0; i < 9; i++) coords[22 + i] = (double)(10 + i);
    PRC_ASSERT_EQ(decode(ctx, codes, 8, coords, 31, &prims, &verts), 0);
    PRC_ASSERT_EQ(prims, 2);
    PRC_ASSERT_EQ(verts, 5);
}

static void
test_damaged_refused(prc_context *ctx)
{
    double coords[32];
    memset(coords, 0, sizeof(coords));

    printf("  sub-case: damaged markups are refused\n");
    {   /* an odd code count: the last entity has no doubles count */
        const uint32_t codes[] = { 0, 3, 0 };
        PRC_ASSERT(decode(ctx, codes, 3, coords, 3, NULL, NULL) != 0);
    }
    {   /* a polyline declaring 30 doubles with 6 stored: read past the coordinates and, before the
           fix, written past the 3-vertex decode buffer */
        const uint32_t codes[] = { 0, 30 };
        PRC_ASSERT(decode(ctx, codes, 2, coords, 6, NULL, NULL) != 0);
    }
    {   /* a matrix with only 4 doubles stored */
        const uint32_t codes[] = { PRC_MARKUP_IsMatrix | 1, 16, PRC_MARKUP_IsMatrix, 0 };
        PRC_ASSERT(decode(ctx, codes, 4, coords, 4, NULL, NULL) != 0);
    }
    {   /* a colour entity whose inner code is missing */
        const uint32_t codes[] = { EXTRA(PRC_COLOR_ENTITY, 1), 0 };
        PRC_ASSERT(decode(ctx, codes, 2, coords, 0, NULL, NULL) != 0);
    }
    {   /* a frame-draw block whose 3-double origin is missing */
        const uint32_t codes[] = { EXTRA(PRC_FRAME_DRAW_MODEL, 1), 3, EXTRA(PRC_FRAME_DRAW_MODEL, 0), 0 };
        PRC_ASSERT(decode(ctx, codes, 4, coords, 1, NULL, NULL) != 0);
    }
    {   /* a text entity whose width and height are missing */
        const uint32_t codes[] = { EXTRA(PRC_TEXT_ENTITY, 1), 2, 0 };
        PRC_ASSERT(decode(ctx, codes, 3, coords, 1, NULL, NULL) != 0);
    }
    {   /* a valid polyline, then an unclosed matrix block: the error comes after a primitive has been
           decoded, which must still be released */
        const uint32_t codes[] = { 0, 6, PRC_MARKUP_IsMatrix | 1, 16 };
        PRC_ASSERT(decode(ctx, codes, 4, coords, 22, NULL, NULL) != 0);
    }
}

int
main(void)
{
    prc_context *ctx;

    PRC_TEST_BEGIN("markup decoder refuses codes and doubles the file does not hold");
    ctx = prc_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    test_wellformed(ctx);
    test_damaged_refused(ctx);

    prc_release_context(ctx);
    PRC_TEST_END;
}
