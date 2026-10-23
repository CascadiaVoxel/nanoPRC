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

/* Pins how a compressed B-rep numbers the vertices and curves it stores.
 *
 * A body declares number_of_vertex_refs (and number_of_edge_refs): how many
 * are stored while iso faces are read. Those take 0 .. count-1; everything
 * else takes count, count+1 ... A reference is looked up at its own number.
 *
 * One writer declares 0 on every body, iso faces or not. At a threshold of 0
 * the two ranges are one, so every vertex is numbered in the order stored. The
 * reader used to refuse such a body at its first iso vertex. A non-zero count
 * that is then exceeded is still refused, because past it the ranges overlap.
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "prc_test.h"
#include "prc_api.h"
#include "prc_context.h"
#include "prc_data.h"
#include "prc_parse_extra_geometry.h"

/* Store one vertex (or curve) on an iso face or elsewhere; return the parser's
   result code and the number it was given. */
static int
store(prc_context *ctx, prc_nano_brep_compressed_data *cd, prc_unsigned_int declared,
    uint8_t iso, uint32_t *iso_count, uint32_t *ana_count, uint32_t *number)
{
    cd->is_iso_type = iso;
    *number = UINT32_MAX;
    return prc_next_ref_number(ctx, cd, declared, iso_count, ana_count, number);
}

int
main(void)
{
    prc_context *ctx;
    prc_nano_brep_compressed_data cd;
    uint32_t iso_count, ana_count, number;

    PRC_TEST_BEGIN("compressed B-rep reference numbering");

    ctx = prc_api_new_context(NULL);
    PRC_ASSERT(ctx != NULL);

    /* Declared 0, iso and analytic vertices interleaved: one sequence in the
       order stored, and each is then found at its own number. */
    memset(&cd, 0, sizeof(cd));
    iso_count = ana_count = 0;
    PRC_ASSERT_EQ(store(ctx, &cd, 0, 1, &iso_count, &ana_count, &number), 0);
    PRC_ASSERT_EQ(number, 0u);
    PRC_ASSERT_EQ(store(ctx, &cd, 0, 0, &iso_count, &ana_count, &number), 0);
    PRC_ASSERT_EQ(number, 1u);
    PRC_ASSERT_EQ(store(ctx, &cd, 0, 1, &iso_count, &ana_count, &number), 0);
    PRC_ASSERT_EQ(number, 2u);
    PRC_ASSERT_EQ(store(ctx, &cd, 0, 1, &iso_count, &ana_count, &number), 0);
    PRC_ASSERT_EQ(number, 3u);
    PRC_ASSERT(prc_ref_number_is_stored(0, iso_count, ana_count, 0));
    PRC_ASSERT(prc_ref_number_is_stored(0, iso_count, ana_count, 3));
    PRC_ASSERT(!prc_ref_number_is_stored(0, iso_count, ana_count, 4));

    /* Declared 2: iso vertices take 0 and 1, analytic ones start at 2, and a
       third iso vertex is refused. */
    memset(&cd, 0, sizeof(cd));
    iso_count = ana_count = 0;
    PRC_ASSERT_EQ(store(ctx, &cd, 2, 0, &iso_count, &ana_count, &number), 0);
    PRC_ASSERT_EQ(number, 2u);
    PRC_ASSERT_EQ(store(ctx, &cd, 2, 1, &iso_count, &ana_count, &number), 0);
    PRC_ASSERT_EQ(number, 0u);
    PRC_ASSERT_EQ(store(ctx, &cd, 2, 1, &iso_count, &ana_count, &number), 0);
    PRC_ASSERT_EQ(number, 1u);
    PRC_ASSERT_EQ(store(ctx, &cd, 2, 0, &iso_count, &ana_count, &number), 0);
    PRC_ASSERT_EQ(number, 3u);
    PRC_ASSERT(prc_ref_number_is_stored(2, iso_count, ana_count, 1));
    PRC_ASSERT(prc_ref_number_is_stored(2, iso_count, ana_count, 3));
    PRC_ASSERT(!prc_ref_number_is_stored(2, iso_count, ana_count, 4));
    PRC_ASSERT(store(ctx, &cd, 2, 1, &iso_count, &ana_count, &number) < 0);

    /* A wire body never uses the iso range, whatever the face flag says. */
    memset(&cd, 0, sizeof(cd));
    cd.is_a_SingleWireBodyCompress = 1;
    iso_count = ana_count = 0;
    PRC_ASSERT_EQ(store(ctx, &cd, 0, 1, &iso_count, &ana_count, &number), 0);
    PRC_ASSERT_EQ(number, 0u);
    PRC_ASSERT_EQ(store(ctx, &cd, 0, 1, &iso_count, &ana_count, &number), 0);
    PRC_ASSERT_EQ(number, 1u);
    PRC_ASSERT_EQ(iso_count, 0u);

    /* Curves go through the same function with their own counters and their
       own declared count: declared 0 numbers them in order too. */
    memset(&cd, 0, sizeof(cd));
    PRC_ASSERT_EQ(store(ctx, &cd, 0, 1, &cd.iso_curve_count, &cd.ana_curve_count, &number), 0);
    PRC_ASSERT_EQ(number, 0u);
    PRC_ASSERT_EQ(store(ctx, &cd, 0, 0, &cd.iso_curve_count, &cd.ana_curve_count, &number), 0);
    PRC_ASSERT_EQ(number, 1u);
    PRC_ASSERT(prc_ref_number_is_stored(0, cd.iso_curve_count, cd.ana_curve_count, 1));

    prc_api_release_context(ctx);

    PRC_TEST_END;
}
