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

/* A GeneralTransformation has no tag, so a schema the file declares for it is
   found by entity type and its extension sits right after the 16 matrix
   doubles. The reader runs a versioned block only when its own version is
   below the block's. Each case writes the 16 doubles, one extra double (1.5)
   and a marker; the extra double is consumed exactly when the block runs. */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "prc_test.h"
#include "prc_context.h"
#include "prc_data.h"
#include "prc_bit.h"
#include "prc_schema.h"
#include "prc_parse_common.h"

#define MARKER 0x5A5A1234u

/* Returns 1 when the extra double was consumed (the marker is next), 0 when it
   was left in the stream (the extra double is next). */
static int
consumed(prc_context *ctx, int with_schema, uint32_t block_version, uint32_t reader_version)
{
    prc_bit_write_state w;
    prc_bit_state r;
    prc_misc_general_trans t;
    prc_unsigned_int tokens[4];
    prc_entity_schema entity;
    prc_schema schema;
    int k, code, result;

    PRC_ASSERT_EQ(prc_bitwrite_init(ctx, &w, 256), 0);
    for (k = 0; k < 16; k++)
        PRC_ASSERT_EQ(prc_bitwrite_double(ctx, &w, (k % 5 == 0) ? 1.0 : 0.0), 0);
    PRC_ASSERT_EQ(prc_bitwrite_double(ctx, &w, 1.5), 0);
    PRC_ASSERT_EQ(prc_bitwrite_uint32(ctx, &w, MARKER), 0);
    PRC_ASSERT_EQ(prc_bitwrite_flush(ctx, &w), 0);

    tokens[0] = EPRCSchema_Block_Version;
    tokens[1] = block_version;
    tokens[2] = EPRCSchema_Data_Double;
    tokens[3] = EPRCSchema_Block_End;
    entity.entity_type = PRC_TYPE_MISC_GeneralTransformation;
    entity.token_count = 4;
    entity.schema_tokens = tokens;
    schema.schema_count = 1;
    schema.entity_schema = &entity;
    ctx->internal.schema = with_schema ? &schema : NULL;
    ctx->internal.reader_version = reader_version;

    memset(&t, 0, sizeof(t));
    prc_init_bit_state(ctx, &r, w.buf, w.byte_pos);
    code = prc_parse_general_transform(ctx, &r, &t);
    PRC_ASSERT_EQ(code, 0);
    PRC_ASSERT_EQ(t.general_transform[0], 1.0);
    PRC_ASSERT_EQ(t.general_transform[5], 1.0);
    PRC_ASSERT_EQ(t.general_transform[15], 1.0);

    if (prc_bitread_uint32(ctx, &r) == MARKER)
        result = 1;
    else
        result = 0;
    ctx->internal.schema = NULL;
    prc_bitwrite_release(ctx, &w);
    return result;
}

int
main(void)
{
    prc_context *ctx;

    PRC_TEST_BEGIN("schema after a general transformation");
    ctx = prc_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    /* No schema for the type: nothing extra is consumed. */
    PRC_ASSERT_EQ(consumed(ctx, 0, 15083, 10001), 0);
    /* Block above the reader's version: runs, the extra double is consumed. */
    PRC_ASSERT_EQ(consumed(ctx, 1, 15083, 10001), 1);
    /* Block inside the file's window, reader below it: runs. */
    PRC_ASSERT_EQ(consumed(ctx, 1, 9149, 8137), 1);
    /* Block at or below the reader's version is native data: not run. */
    PRC_ASSERT_EQ(consumed(ctx, 1, 9149, 10001), 0);

    prc_release_context(ctx);
    PRC_TEST_END;
}
