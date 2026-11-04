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

/* The current name of clause 4.4: a Name whose same_name bit is TRUE takes the
   last name read in the same section, including one read inside a nested
   entity, and the current name starts as NULL in every section. */

#include <stdlib.h>
#include <string.h>
#include "prc_test.h"
#include "prc_context.h"
#include "prc_data.h"
#include "prc_bit.h"
#include "prc_parse_common.h"

/* Writes one Name: same_name, then the string when same_name is FALSE. A NULL
   text with same_name FALSE writes a null string. */
static void
write_name(prc_context *ctx, prc_bit_write_state *w, uint8_t same, const char *text)
{
    prc_string s;

    PRC_ASSERT_EQ(prc_bitwrite_bit(ctx, w, same), 0);
    if (same)
        return;
    s.null_flag = 0;
    s.size = text != NULL ? (prc_unsigned_int)strlen(text) : 0;
    s.string = (unsigned char *)text;
    PRC_ASSERT_EQ(prc_bitwrite_string(ctx, w, &s), 0);
}

static void
assert_name(const prc_name *n, const char *text)
{
    if (text == NULL)
    {
        PRC_ASSERT(n->name.string == NULL);
        return;
    }
    PRC_ASSERT_NOT_NULL(n->name.string);
    PRC_ASSERT_EQ(n->name.size, (prc_unsigned_int)strlen(text));
    PRC_ASSERT(memcmp(n->name.string, text, n->name.size) == 0);
}

int
main(void)
{
    prc_context *ctx;
    prc_bit_write_state w;
    prc_bit_state r;
    prc_name names[8];
    int k;

    PRC_TEST_BEGIN("test_current_name");

    ctx = prc_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    PRC_ASSERT_EQ(prc_bitwrite_init(ctx, &w, 64), 0);
    write_name(ctx, &w, 1, NULL);       /* 0: same, before any name: NULL */
    write_name(ctx, &w, 0, "view 4");   /* 1: a view's own name */
    write_name(ctx, &w, 1, NULL);       /* 2: same as "view 4" */
    write_name(ctx, &w, 0, "");         /* 3: a nested entity's empty name */
    write_name(ctx, &w, 1, NULL);       /* 4: same as the nested name, "" */
    write_name(ctx, &w, 0, NULL);       /* 5: a null name */
    write_name(ctx, &w, 1, NULL);       /* 6: same as the null name: NULL */
    PRC_ASSERT_EQ(prc_bitwrite_flush(ctx, &w), 0);

    prc_init_bit_state(ctx, &r, w.buf, w.byte_pos);
    memset(names, 0, sizeof(names));
    for (k = 0; k < 7; k++)
        PRC_ASSERT_EQ(prc_parse_name(ctx, &r, &names[k]), 0);

    assert_name(&names[0], NULL);
    assert_name(&names[1], "view 4");
    assert_name(&names[2], "view 4");
    PRC_ASSERT(names[2].name.string != names[1].name.string); /* its own copy */
    assert_name(&names[3], "");
    assert_name(&names[4], "");
    assert_name(&names[5], NULL);
    assert_name(&names[6], NULL);
    PRC_ASSERT_EQ(names[2].same, 1);
    PRC_ASSERT_EQ(names[1].same, 0);

    /* A new section starts with no current name */
    prc_init_bit_state(ctx, &r, w.buf, w.byte_pos);
    PRC_ASSERT_EQ(prc_parse_name(ctx, &r, &names[7]), 0);
    assert_name(&names[7], NULL);

    for (k = 0; k < 8; k++)
        if (names[k].name.string != NULL)
            prc_free(ctx, names[k].name.string);
    prc_bitwrite_release(ctx, &w);
    prc_release_context(ctx);

    PRC_TEST_END;
}
