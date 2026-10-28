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

/* prc_api_get_text_primitive checked the index against num_text_primitives - 1. With no text primitives that
   wrapped round, so every index passed and the function copied from a NULL array. An index at or past the
   count must be refused, and one inside it returned. */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "prc_test.h"
#include "prc_api.h"

int
main(void)
{
    prc_context *ctx;
    prc_api_tess tess;
    prc_api_text_primitive texts[2], out;

    PRC_TEST_BEGIN("text primitive index is checked against the count");
    ctx = prc_api_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    printf("  sub-case: a markup tessellation with no text\n");
    memset(&tess, 0, sizeof(tess));
    tess.type = PRC_API_TESS_MarkUp;
    tess.num_text_primitives = 0;
    tess.text_primitives = NULL;
    PRC_ASSERT(prc_api_get_text_primitive(ctx, NULL, &tess, 0, &out) != 0);
    PRC_ASSERT(prc_api_get_text_primitive(ctx, NULL, &tess, 7, &out) != 0);

    printf("  sub-case: two text primitives\n");
    memset(texts, 0, sizeof(texts));
    texts[0].text_height = 1.5;
    texts[1].text_height = 2.5;
    tess.num_text_primitives = 2;
    tess.text_primitives = texts;
    memset(&out, 0, sizeof(out));
    PRC_ASSERT_EQ(prc_api_get_text_primitive(ctx, NULL, &tess, 1, &out), 0);
    PRC_ASSERT(out.text_height == 2.5);
    PRC_ASSERT(prc_api_get_text_primitive(ctx, NULL, &tess, 2, &out) != 0);

    printf("  sub-case: NULL arguments\n");
    PRC_ASSERT(prc_api_get_text_primitive(ctx, NULL, NULL, 0, &out) != 0);
    PRC_ASSERT(prc_api_get_text_primitive(ctx, NULL, &tess, 0, NULL) != 0);

    prc_api_release_context(ctx);
    PRC_TEST_END;
}
