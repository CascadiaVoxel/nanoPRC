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

/* A compressed tessellation whose per-face arrays are read out of step must
   be refused, not read past the end of line_attribute_array.

   The fixture is malformed on purpose: two unit cubes, each a compressed
   tessellation of 6 faces that stores has_faces TRUE but only one
   is_multiple_line_attribute_on_face entry, where has_faces TRUE calls for
   six. A reader takes six, so it reads the rest of the first cube out of
   step, and a later triangle asks for more line_attribute_array entries than
   the entity stores. Opening the file must fail with an error, and must not
   crash. */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "prc_test.h"
#include "prc_context.h"
#include "prc_data.h"
#include "prc_api.h"

#ifndef PRC_TEST_FIXTURES_DIR
#define PRC_TEST_FIXTURES_DIR "tests/fixtures"
#endif

int
main(void)
{
    prc_context *ctx;
    prc_api_data data;

    PRC_TEST_BEGIN("line_attribute_array bounds");

    ctx = prc_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    data = prc_api_open_contents(ctx, PRC_TEST_FIXTURES_DIR "/mla_hasfacesT_1bit.prc");
    PRC_ASSERT(data == NULL);

    prc_release_context(ctx);

    PRC_TEST_END;
}
