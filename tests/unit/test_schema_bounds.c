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

/* A file's schema is a token program that the reader runs. Its tokens come from the file, and the interpreter
   read them with no bound: a block left open, or an instruction missing its operand, read past the end of the
   token array. Each must now be refused, and a well-formed program still run. */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "prc_test.h"
#include "prc_context.h"
#include "prc_data.h"
#include "prc_bit.h"
#include "prc_schema.h"

/* Run a token program over a few zero bytes. The tokens are copied into an allocation of exactly their own size,
   so a read past the last token is a read past an allocation. */
static int
run(prc_context *ctx, const uint32_t *tokens, uint32_t count)
{
    uint8_t bits[16];
    prc_bit_state state;
    prc_entity_schema schema;
    int recursion = 0;
    int code;

    memset(bits, 0, sizeof(bits));
    prc_init_bit_state(ctx, &state, bits, sizeof(bits));
    memset(&schema, 0, sizeof(schema));
    schema.token_count = count;
    schema.schema_tokens = (prc_unsigned_int *)malloc(sizeof(prc_unsigned_int) * count);
    memcpy(schema.schema_tokens, tokens, sizeof(uint32_t) * count);
    code = prc_execute_schema(ctx, &state, &schema, &recursion);
    free(schema.schema_tokens);
    return code;
}

int
main(void)
{
    prc_context *ctx;

    PRC_TEST_BEGIN("schema interpreter stays inside the token array");
    ctx = prc_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    printf("  sub-case: a well-formed program runs\n");
    {
        const uint32_t ok[] = { EPRCSchema_Block_Start, EPRCSchema_Data_Boolean, EPRCSchema_Data_Double,
                                EPRCSchema_Block_End, EPRCSchema_Data_Boolean };
        PRC_ASSERT_EQ(run(ctx, ok, 5), 0);
    }

    printf("  sub-case: programs that end where an instruction expects more are refused\n");
    {
        const uint32_t open_block[] = { EPRCSchema_Block_Start, EPRCSchema_Data_Boolean };
        const uint32_t no_operand[] = { EPRCSchema_Value_Declare };
        const uint32_t no_version[] = { EPRCSchema_Block_Version };
        const uint32_t if_alone[] = { EPRCSchema_If, EPRCSchema_Data_Boolean };
        PRC_ASSERT(run(ctx, open_block, 2) != 0);
        PRC_ASSERT(run(ctx, no_operand, 1) != 0);
        PRC_ASSERT(run(ctx, no_version, 1) != 0);
        PRC_ASSERT(run(ctx, if_alone, 2) != 0);
    }

    prc_release_context(ctx);
    PRC_TEST_END;
}
