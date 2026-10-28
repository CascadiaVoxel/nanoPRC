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

/* VM for dealing with schema */
#include "prc_schema.h"
#include "prc_bit.h"
#include "prc_parse_common.h"

void prc_release_string(prc_context *ctx, prc_string *data);

static int
add_schema_dict(prc_context *ctx, schema_dict **variable_dict, uint32_t key, double value)
{
    schema_dict *new_entry;

    new_entry = (schema_dict*) prc_calloc(ctx, 1, sizeof(schema_dict));
    if (new_entry == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Allocation error add_schema_dict\n");
        return PRC_ERROR_MEMORY;
    }

    new_entry->key = key;
    new_entry->value = value;

    if (*variable_dict == NULL)
    {
        new_entry->next = NULL;
        *variable_dict = new_entry;
    }
    else
    {
        /* Add it to the top */
        new_entry->next = *variable_dict;
        *variable_dict = new_entry;
    }
    return 0;
}

/* Get the dictionary entry for that key */
static schema_dict*
find_dict_entry(prc_context *ctx, uint32_t key, schema_dict *dict)
{
    schema_dict *curr_entry = dict;

    while (curr_entry != NULL)
    {
        if (curr_entry->key == key)
            return curr_entry;
        curr_entry = curr_entry->next;
    }
    return SCHEMA_DICT_ENTRY_NOTFOUND;
}

#define PRC_SKIP_TOKENS_RECURSION_MAX 4096

/* Used to skip over sections in the conditional clauses. This is tricky
   and care must be taken */
static int
skip_tokens(prc_context * ctx, uint32_t *codes, int count, int offset, int depth)
{
    if (depth > PRC_SKIP_TOKENS_RECURSION_MAX)
    {
        prc_error(ctx, PRC_SCHEMA_ERROR, "skip_tokens recursion depth exceeded\n");
        return PRC_SCHEMA_ERROR;
    }
    /* The token array comes from the file: a block left open would otherwise walk off its end. */
    if (offset < 0 || offset >= count)
    {
        prc_error(ctx, PRC_SCHEMA_ERROR, "skip_tokens ran past the end of the schema\n");
        return PRC_SCHEMA_ERROR;
    }

    /* If we are pointing at a block or block version skip
       including the end.  Other wise just skip the next command */
    if (codes[offset] == EPRCSchema_Block_Version)
    {
        offset = offset + 2; /* Skip the version number too */
        while (offset < count && codes[offset] != EPRCSchema_Block_End)
        {
            offset = skip_tokens(ctx, codes, count, offset, depth + 1);
            if (offset < 0)
            {
                prc_error(ctx, PRC_SCHEMA_ERROR, "Error with skip_tokens\n");
                return offset;
            }
        }
        if (offset >= count)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Schema block not closed\n");
            return PRC_SCHEMA_ERROR;
        }
        /* Skip block end */
        return offset + 1;
    }
    else if (codes[offset] == EPRCSchema_Block_Start)
    {
        offset = offset + 1;
        while (offset < count && codes[offset] != EPRCSchema_Block_End)
        {
            offset = skip_tokens(ctx, codes, count, offset, depth + 1);
            if (offset < 0)
            {
                prc_error(ctx, PRC_SCHEMA_ERROR, "Error with skip_tokens\n");
                return offset;
            }
        }
        if (offset >= count)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Schema block not closed\n");
            return PRC_SCHEMA_ERROR;
        }
        /* Skip block end */
        return offset + 1;
    }
    else
    {
        switch (codes[offset])
        {
        case EPRCSchema_Data_Boolean:
        case EPRCSchema_Data_Double:
        case EPRCSchema_Data_Character:
        case EPRCSchema_Data_Unsigned_Integer:
        case EPRCSchema_Data_Integer:
        case EPRCSchema_Data_String:
        case EPRCSchema_Vector_2D:
        case EPRCSchema_Vector_3D:
        case EPRCSchema_Extent_1D:
        case EPRCSchema_Extent_2D:
        case EPRCSchema_Extent_3D:
        case EPRCSchema_Ptr_Surface:
        case EPRCSchema_Ptr_Curve:
        case EPRCSchema_Value_CurveIs3D:
        case EPRCSchema_Value_For:
        case EPRCSchema_Operator_IGNORE1:
        case EPRCSchema_Operator_IGNORE2:
            return offset + 1;

        case EPRCSchema_If:
            offset = skip_tokens(ctx, codes, count, offset + 1, depth + 1);
            if (offset < 0)
            {
                prc_error(ctx, PRC_SCHEMA_ERROR, "Error with skip_tokens\n");
                return offset;
            }

            /* Now skip the value that the if statement would have executed */
            offset = skip_tokens(ctx, codes, count, offset, depth + 1);
            if (offset < 0)
            {
                prc_error(ctx, PRC_SCHEMA_ERROR, "Error with skip_tokens\n");
                return offset;
            }

            /* Check for presence of else */
            if (offset >= 0 && offset < count && codes[offset] == EPRCSchema_Else)
            {
                offset = skip_tokens(ctx, codes, count, offset + 1, depth + 1);
                return offset;
            }
            else
                return offset;

        case EPRCSchema_Else:
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with skip_tokens (EPRCSchema_Else) \n");
            return PRC_SCHEMA_ERROR;

        case EPRCSchema_Ptr_Type:
        case EPRCSchema_Parent_Type:
        case EPRCSchema_Value_Constant:
        case EPRCSchema_Value:
        case EPRCSchema_Value_Declare:
            return offset + 2;

        case EPRCSchema_Value_DeclareAndSet:
        case EPRCSchema_Value_Set:
            offset = skip_tokens(ctx, codes, count, offset + 2, depth + 1);
            if (offset < 0)
            {
                prc_error(ctx, PRC_SCHEMA_ERROR, "Error with skip_tokens\n");
                return offset;
            }
            return offset;

        case EPRCSchema_SimpleFor:
            offset = skip_tokens(ctx, codes, count, offset + 1, depth + 1);
            if (offset < 0)
            {
                prc_error(ctx, PRC_SCHEMA_ERROR, "Error with skip_tokens\n");
                return offset;
            }
            return offset;

        case EPRCSchema_For:
        case EPRCSchema_Operator_LT:
        case EPRCSchema_Operator_LE:
        case EPRCSchema_Operator_GT:
        case EPRCSchema_Operator_GE:
        case EPRCSchema_Operator_EQ:
        case EPRCSchema_Operator_NEQ:
        case EPRCSchema_Operator_SUB:
        case EPRCSchema_Operator_ADD:
        case EPRCSchema_Operator_DIV:
        case EPRCSchema_Operator_MULT:
            offset = skip_tokens(ctx, codes, count, offset + 1, depth + 1);
            if (offset < 0)
            {
                prc_error(ctx, PRC_SCHEMA_ERROR, "Error with skip_tokens\n");
                return offset;
            }
            offset = skip_tokens(ctx, codes, count, offset, depth + 1);
            if (offset < 0)
            {
                prc_error(ctx, PRC_SCHEMA_ERROR, "Error with skip_tokens\n");
                return offset;
            }
            return offset;

        default:
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with skip_tokens\n");
            return PRC_SCHEMA_ERROR;
        }
    }
}

static int
get_double(prc_context *ctx, prc_schema_read *data_read, double *value)
{
    switch (data_read->data_type)
    {
    case EPRCSchema_Data_Boolean:
        *value = (double) data_read->bool_val;
        break;

    case EPRCSchema_Data_Double:
        *value = data_read->double_val;
        break;

    case EPRCSchema_Data_Character:
        *value = (double) data_read->char_val;
        break;

    case EPRCSchema_Data_Unsigned_Integer:
        *value = (double) data_read->uint32_val;
        break;

    case EPRCSchema_Data_Integer:
        *value = (double) data_read->int32_val;
        break;

    default:
        prc_error(ctx, PRC_SCHEMA_ERROR, "Error with get_double\n");
        return PRC_SCHEMA_ERROR;
    }
    return 0;
}

static int
get_integer(prc_context *ctx, prc_schema_read *data_read, int32_t *value)
{
    switch (data_read->data_type)
    {
    case EPRCSchema_Data_Boolean:
        *value = (int32_t)data_read->bool_val;
        break;

    case EPRCSchema_Data_Double:
        *value = (int32_t)data_read->double_val;
        break;

    case EPRCSchema_Data_Character:
        *value = (int32_t)data_read->char_val;
        break;

    case EPRCSchema_Data_Unsigned_Integer:
        *value = (int32_t)data_read->uint32_val;
        break;

    case EPRCSchema_Data_Integer:
        *value = (int32_t)data_read->int32_val;
        break;

    default:
        prc_error(ctx, PRC_SCHEMA_ERROR, "Error with get_integer\n");
        return PRC_SCHEMA_ERROR;
    }
    return 0;
}

#define PRC_SCHEMA_INSTRUCTION_RECURSION_MAX 4096

/* Execute the token */
static int
prc_execute_schema_instruction(prc_context *ctx, prc_bit_state *bit_state,
    uint32_t *codes, int count, int offset, prc_schema_read *data_read,
    schema_dict **variable_dict, prc_schema_read *parent_read, int depth)
{
    int code;
    uint32_t key;
    schema_dict *dict_entry;

    /* This is a recursive-descent evaluator over the schema token stream;
       nested expressions/blocks are attacker-controlled via token_count, so
       cap the depth instead of letting it grow unbounded. Kept as recursion
       (rather than a full heap-based rewrite) since this is a many-case
       expression evaluator where only the depth, not the mechanism, needed
       fixing -- same tradeoff as prc_parse_ri/prc_parse_topo elsewhere in
       this codebase. */
    if (depth > PRC_SCHEMA_INSTRUCTION_RECURSION_MAX)
    {
        prc_error(ctx, PRC_SCHEMA_ERROR, "prc_execute_schema_instruction recursion depth exceeded\n");
        return PRC_SCHEMA_ERROR;
    }
    double val1, val2;
    int32_t intval;
    uint32_t vers;
    int k;
    int curr_offset;

    if (offset < 0 || offset >= count)
    {
        prc_error(ctx, PRC_SCHEMA_ERROR, "Schema instruction past the end of the schema\n");
        return PRC_SCHEMA_ERROR;
    }

    uint8_t instruction = codes[offset];
    switch (instruction)
    {
    case EPRCSchema_Data_Boolean:
        data_read->bool_val = prc_bitread_bit(ctx, bit_state);
        data_read->data_type = EPRCSchema_Data_Boolean;
        return offset + 1;

    case EPRCSchema_Data_Double:
        data_read->double_val = prc_bitread_double(ctx, bit_state);
        data_read->data_type = EPRCSchema_Data_Double;
        return offset + 1;

    case EPRCSchema_Data_Character:
        data_read->char_val = prc_bitread_uint8(ctx, bit_state);
        data_read->data_type = EPRCSchema_Data_Character;
        return offset + 1;

    case EPRCSchema_Data_Unsigned_Integer:
        data_read->uint32_val = prc_bitread_uint32(ctx, bit_state);
        data_read->data_type = EPRCSchema_Data_Unsigned_Integer;
        return offset + 1;

    case EPRCSchema_Data_Integer:
        data_read->int32_val = prc_bitread_int32(ctx, bit_state);
        data_read->data_type = EPRCSchema_Data_Integer;
        return offset + 1;

    case EPRCSchema_Data_String:
        code = prc_bitread_string(ctx, bit_state, &data_read->string_val);
        if (code < 0)
        {
            prc_error(ctx, PRC_ERROR_MEMORY, "Allocation error prc_bitread_string\n");
            return PRC_ERROR_MEMORY;
        }
        /* Lets release this string at this time... Schema madness here */
        if (data_read->string_val.string != NULL)
        {
            prc_release_string(ctx, &data_read->string_val);
        }
        data_read->data_type = EPRCSchema_Data_String;
        return offset + 1;

    case EPRCSchema_Parent_Type:
        /* Next token in the token aris the parent type.  This is a tricky one.
           This parent could have a schema of its own.  We need to hand back the
           type so that we can run the appropriate schema */
        data_read->data_type = EPRCSchema_PARENT_TYPE;
        if (offset + 1 >= count)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Schema operand past the end of the schema\n");
            return PRC_SCHEMA_ERROR;
        }
        data_read->parent_type = codes[offset + 1];
        return offset + 2;

    case EPRCSchema_Vector_2D:
        data_read->data_type = EPRCSchema_Vector_2D;
        data_read->vec2_val = prc_parse_2d_vector(ctx, bit_state);
        return offset + 1;

    case EPRCSchema_Vector_3D:
        data_read->data_type = EPRCSchema_Vector_3D;
        data_read->vec3_val = prc_parse_3d_vector(ctx, bit_state);
        return offset + 1;

    case EPRCSchema_Extent_1D:
        data_read->data_type = EPRCSchema_Extent_1D;
        data_read->interval_val = prc_parse_interval(ctx, bit_state);
        return offset + 1;

    case EPRCSchema_Extent_2D:
        data_read->data_type = EPRCSchema_Extent_2D;
        data_read->domain_val = prc_parse_domain(ctx, bit_state);
        return offset + 1;

    case EPRCSchema_Extent_3D:
        data_read->data_type = EPRCSchema_Extent_3D;
        code = prc_parse_bound_box(ctx, bit_state, &data_read->bbox_val);
        if (code < 0)
        {
            prc_error(ctx, PRC_ERROR_MEMORY, "Allocation error prc_parse_bound_box\n");
            return PRC_ERROR_MEMORY;
        }
        return offset + 1;

    case EPRCSchema_Ptr_Type:
        /* The next token indicates the data type that we need to read next from
           the file */
        if (offset + 1 >= count)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Schema operand past the end of the schema\n");
            return PRC_SCHEMA_ERROR;
        }
        key = codes[offset + 1];
        switch (key)
        {
        case PRC_TYPE_SURF:
            break;

        case PRC_TYPE_CRV:
            break;

        case PRC_TYPE_TOPO:
            break;

        case PRC_TYPE_MKP_Markup:
            break;

        case PRC_TYPE_RI_RepresentationalItem:
            break;

        case PRC_TYPE_RI_BrepModel:
            break;

        case PRC_TYPE_RI_Curve:
            break;

        case PRC_TYPE_RI_Direction:
            break;

        case PRC_TYPE_RI_Plane:
            break;

        case PRC_TYPE_RI_PointSet:
            break;

        case PRC_TYPE_RI_PolyBrepModel:
            break;

        case PRC_TYPE_RI_Set:
            break;

        case PRC_TYPE_RI_CoordinateSystem:
            break;

        default:
            /* TODO The math functions. (PRC_TYPE_MATH_FCT_1D) PRC_TYPE_MATH_FCT_3D)
               Also note that we could have a special type that is defined by a schema */
            break;
        };
        return offset + 2;

    case EPRCSchema_Ptr_Surface:
        /* TODO READ IN A SURFACE FROM THE FILE */
        return offset + 1;

    case EPRCSchema_Ptr_Curve:
        /* TODO READ IN A CURVE FROM THE FILE */
        return offset + 1;

    case EPRCSchema_For:
        /* First get the number of times to execute the loop. */
        offset = prc_execute_schema_instruction(ctx, bit_state, codes, count, offset + 1,
                                                data_read, variable_dict, parent_read, depth + 1);
        if (offset < 0)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_For\n");
            return offset;
        }
        code = get_integer(ctx, data_read, &intval);

        /* Now the loop.  We have to reset the offset each iteration so that we
           execute the same bit of code */
        curr_offset = offset;
        for (k = 0; k < intval; k++)
        {
            data_read->in_for_loop = 1;
            data_read->loop_value = k;
            offset = curr_offset;
            offset = prc_execute_schema_instruction(ctx, bit_state, codes, count, offset,
                                                    data_read, variable_dict, parent_read, depth + 1);
            if (offset < 0)
            {
                prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_For\n");
                return offset;
            }
            /* The loop count comes from the file; stop once the section has been read past its end. */
            if (bit_state->overrun)
            {
                prc_error(ctx, PRC_SCHEMA_ERROR, "EPRCSchema_For read past the end of the section\n");
                return PRC_SCHEMA_ERROR;
            }
        }
        return offset;

    case EPRCSchema_SimpleFor:
        {
            /* Read the loop integer */
            uint32_t uint32_val = prc_bitread_uint32(ctx, bit_state);
            offset = offset + 1;
            /* Now the loop.  We have to reset the offset each iteration so that
              we execute the same bit of code */
            curr_offset = offset;
            for (uint32_t kk = 0; kk < uint32_val; kk++)
            {
                data_read->in_for_loop = 1;
                data_read->loop_value = kk;
                offset = curr_offset;
                offset = prc_execute_schema_instruction(ctx, bit_state, codes, count,
                                  offset, data_read, variable_dict, parent_read, depth + 1);
                if (offset < 0)
                {
                    prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_SimpleFor\n");
                    return offset;
                }
                if (bit_state->overrun)
                {
                    prc_error(ctx, PRC_SCHEMA_ERROR, "EPRCSchema_SimpleFor read past the end of the section\n");
                    return PRC_SCHEMA_ERROR;
                }
            }

            return offset;
        }

    case EPRCSchema_If:
        /* Execute the next operator which should return a boolean */
        offset = prc_execute_schema_instruction(ctx, bit_state, codes, count, offset + 1,
                                            data_read, variable_dict, parent_read, depth + 1);
        if (offset < 0)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_If\n");
            return offset;
        }
        if (data_read->data_type != EPRCSchema_Data_Boolean)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_If\n");
            return PRC_SCHEMA_ERROR;
        }
        if (data_read->bool_val)
        {
            /* Execute the if statement */
            offset = prc_execute_schema_instruction(ctx, bit_state, codes, count, offset,
                                          data_read, variable_dict, parent_read, depth + 1);
            if (offset < 0)
            {
                prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_If\n");
                return offset;
            }

            /* Check if there is an else that we need to skip */
            if (offset >= 0 && offset < count && codes[offset] == EPRCSchema_Else)
            {
                /* Skip the else and what the else executes */
                offset = skip_tokens(ctx, codes, count, offset + 1, 0);
            }
        }
        else
        {
            /* We need to check if there is an else statement. But first we
               need to skip any instructions that are remaining in the if clause */
            /* Skip the if tokens */
            offset = skip_tokens(ctx, codes, count, offset, 0);

            /* Check for else */
            if (offset >= 0 && offset < count && codes[offset] == EPRCSchema_Else)
            {
                /* Execute the else statement */
                offset = prc_execute_schema_instruction(ctx, bit_state, codes, count,
                             offset + 1, data_read, variable_dict, parent_read, depth + 1);
                if (offset < 0)
                {
                    prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_If\n");
                    return offset;
                }
            }
        }
        return offset;

    case EPRCSchema_Else:
        /* We should never be here.  This always occurs in combo with an if */
        prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Else\n");
        return PRC_SCHEMA_ERROR;

    case EPRCSchema_Block_Start:
        /* Execute until EPRCSchema_Block_End */
        offset = offset + 1;
        while (offset < count && codes[offset] != EPRCSchema_Block_End)
        {
            offset = prc_execute_schema_instruction(ctx, bit_state, codes, count, offset,
                                            data_read, variable_dict, parent_read, depth + 1);
            if (offset < 0)
            {
                prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Block_Start\n");
                return offset;
            }
        }

        if (offset >= count)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Schema block not closed\n");
            return PRC_SCHEMA_ERROR;
        }
        /* Skip the block end */
        return offset + 1;

    case EPRCSchema_Block_Version:
        /* Versioned block.  If the version number is less than or equal to the
           current version we ignore this block */
        if (offset + 1 >= count)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Schema operand past the end of the schema\n");
            return PRC_SCHEMA_ERROR;
        }
        vers = codes[offset + 1];
        if (ctx->internal.reader_version < vers)
        {
            offset = offset + 2;
            while (offset < count && codes[offset] != EPRCSchema_Block_End)
            {
                offset = prc_execute_schema_instruction(ctx, bit_state, codes, count,
                                  offset, data_read, variable_dict, parent_read, depth + 1);
                if (offset < 0)
                {
                    prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Block_Version\n");
                    return offset;
                }
                /* Check for presence of parent type. In that case, we may need
                   to execute another schema */
                if (data_read->data_type == EPRCSchema_PARENT_TYPE)
                {
                    parent_read->data_type = EPRCSchema_PARENT_TYPE;
                    parent_read->parent_type = data_read->parent_type;
                }
            }
            if (offset >= count)
            {
                prc_error(ctx, PRC_SCHEMA_ERROR, "Schema block not closed\n");
                return PRC_SCHEMA_ERROR;
            }
            /* skip block end */
            return offset + 1;
        }
        else
        {
            offset = skip_tokens(ctx, codes, count, offset, 0);

            /* skip_tokens will skip any block end command */
            return offset;
        }

    case EPRCSchema_Block_End:
        /* We should never be here.  This always occurs in combo with
           EPRCSchema_Block_Start or EPRCSchema_Block_Version */
        prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Block_End\n");
        return PRC_SCHEMA_ERROR;

    case EPRCSchema_Value_Declare:
        /* Get the next value which is the index */
        if (offset + 1 >= count)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Schema operand past the end of the schema\n");
            return PRC_SCHEMA_ERROR;
        }
        key = codes[offset + 1];

        /* Get the dictionary entry for that key */
        dict_entry = find_dict_entry(ctx, key, *variable_dict);
        if (dict_entry == NULL)
        {
            code = add_schema_dict(ctx, variable_dict, key, 0);
            if (code < 0)
            {
                prc_error(ctx, code, "Error add_schema_dict\n");
                return code;
            }
        }
        else
        {
            dict_entry->value = 0;
        }
        return offset + 2;

    case EPRCSchema_Value_Set:
        /* Value must be declared before it can be set. So it should be in the dict. */
        /* Get the dictionary entry for that key */
        /* Get the next value which is the index */
        if (offset + 1 >= count)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Schema operand past the end of the schema\n");
            return PRC_SCHEMA_ERROR;
        }
        key = codes[offset + 1];

        dict_entry = find_dict_entry(ctx, key, *variable_dict);
        if (dict_entry == NULL)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Value_Set\n");
            return PRC_SCHEMA_ERROR;
        }
        else
        {
            /* Now execute the next code to get the value */
            offset = prc_execute_schema_instruction(ctx, bit_state, codes, count, offset + 2,
                                          data_read, variable_dict, parent_read, depth + 1);
            if (offset < 0)
            {
                prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Value_Set\n");
                return offset;
            }

            switch (data_read->data_type)
            {
                case EPRCSchema_Data_Boolean:
                    dict_entry->value = data_read->bool_val;
                    break;

                 case EPRCSchema_Data_Double:
                     dict_entry->value = data_read->double_val;
                     break;

                case EPRCSchema_Data_Character:
                    dict_entry->value = data_read->char_val;
                    break;

                case EPRCSchema_Data_Unsigned_Integer:
                    dict_entry->value = data_read->uint32_val;
                    break;

                case EPRCSchema_Data_Integer:
                    dict_entry->value = data_read->int32_val;
                    break;

                default:
                    prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Value_Set\n");
                    return PRC_SCHEMA_ERROR;
            }
        }
        return offset;

    case EPRCSchema_Value_DeclareAndSet:

        if (offset + 1 >= count)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Schema operand past the end of the schema\n");
            return PRC_SCHEMA_ERROR;
        }
        key = codes[offset + 1];
        dict_entry = find_dict_entry(ctx, key, *variable_dict);
        if (dict_entry == NULL)
        {
            code = add_schema_dict(ctx, variable_dict, key, 0);
            if (code < 0)
            {
                prc_error(ctx, code, "Error add_schema_dict\n");
                return code;
            }
            dict_entry = *variable_dict;
        }

        /* Now execute the next code to get the value */
        offset = prc_execute_schema_instruction(ctx, bit_state, codes, count, offset + 2,
                                           data_read, variable_dict, parent_read, depth + 1);
        if (offset < 0)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Value_DeclareAndSet\n");
            return offset;
        }

        switch (data_read->data_type)
        {
            case EPRCSchema_Data_Boolean:
                dict_entry->value = data_read->bool_val;
                break;

            case EPRCSchema_Data_Double:
                dict_entry->value = data_read->double_val;
                break;

            case EPRCSchema_Data_Character:
                dict_entry->value = data_read->char_val;
                break;

            case EPRCSchema_Data_Unsigned_Integer:
                dict_entry->value = data_read->uint32_val;
                break;

            case EPRCSchema_Data_Integer:
                dict_entry->value = data_read->int32_val;
                break;

            default:
                prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Value_Set\n");
                return PRC_SCHEMA_ERROR;
        }
        return offset;

    case EPRCSchema_Value:
        /* Key must be in dict already. Retrun this in the ReadValue as a double  */
        if (offset + 1 >= count)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Schema operand past the end of the schema\n");
            return PRC_SCHEMA_ERROR;
        }
        key = codes[offset + 1];

        dict_entry = find_dict_entry(ctx, key, *variable_dict);
        if (dict_entry == NULL)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Value_Set\n");
            return PRC_SCHEMA_ERROR;
        }

        data_read->double_val = dict_entry->value;
        data_read->data_type = EPRCSchema_Data_Double;

        return offset + 2;

    case EPRCSchema_Value_Constant:
        if (offset + 1 >= count)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Schema operand past the end of the schema\n");
            return PRC_SCHEMA_ERROR;
        }
        data_read->double_val = codes[offset + 1];
        data_read->data_type = EPRCSchema_Data_Double;
        return offset + 2;

    case EPRCSchema_Value_For:
        if (data_read->in_for_loop == 0)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Value_For\n");
            return PRC_SCHEMA_ERROR;
        }
        data_read->uint32_val = data_read->loop_value;
        data_read->data_type = EPRCSchema_Data_Unsigned_Integer;
        return offset + 1;

    case EPRCSchema_Value_CurveIs3D:
        /* TODO: Not clear what they mean by current object */
        return offset + 1;

    case EPRCSchema_Operator_MULT:
        offset = prc_execute_schema_instruction(ctx, bit_state, codes, count, offset + 1,
                                           data_read, variable_dict, parent_read, depth + 1);
        if (offset < 0)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Operator_MULT\n");
            return offset;
        }
        get_double(ctx, data_read, &val1);
        offset = prc_execute_schema_instruction(ctx, bit_state, codes, count, offset,
                                          data_read, variable_dict, parent_read, depth + 1);
        if (offset < 0)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Operator_MULT\n");
            return offset;
        }
        get_double(ctx, data_read, &val2);
        data_read->double_val = val1 * val2;
        data_read->data_type = EPRCSchema_Data_Double;
        return offset;

    case EPRCSchema_Operator_DIV:
        offset = prc_execute_schema_instruction(ctx, bit_state, codes, count, offset + 1,
                                            data_read, variable_dict, parent_read, depth + 1);
        if (offset < 0)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Operator_DIV\n");
            return offset;
        }
        get_double(ctx, data_read, &val1);
        offset = prc_execute_schema_instruction(ctx, bit_state, codes, count, offset,
                                           data_read, variable_dict, parent_read, depth + 1);
        if (offset < 0)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Operator_DIV\n");
            return offset;
        }
        get_double(ctx, data_read, &val2);
        if (val2 == 0)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Operator_DIV\n");
            return PRC_SCHEMA_ERROR;
        }
        data_read->double_val = val1 / val2;
        data_read->data_type = EPRCSchema_Data_Double;
        return offset;

    case EPRCSchema_Operator_ADD:
        offset = prc_execute_schema_instruction(ctx, bit_state, codes, count, offset + 1,
                                            data_read, variable_dict, parent_read, depth + 1);
        if (offset < 0)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Operator_ADD\n");
            return offset;
        }
        get_double(ctx, data_read, &val1);
        offset = prc_execute_schema_instruction(ctx, bit_state, codes, count, offset,
                                            data_read, variable_dict, parent_read, depth + 1);
        if (offset < 0)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Operator_ADD\n");
            return offset;
        }
        get_double(ctx, data_read, &val2);
        data_read->double_val = val1 + val2;
        data_read->data_type = EPRCSchema_Data_Double;
        return offset;

    case EPRCSchema_Operator_SUB:
        offset = prc_execute_schema_instruction(ctx, bit_state, codes, count, offset + 1,
                                            data_read, variable_dict, parent_read, depth + 1);
        if (offset < 0)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Operator_SUB\n");
            return offset;
        }
        get_double(ctx, data_read, &val1);
        offset = prc_execute_schema_instruction(ctx, bit_state, codes, count, offset,
                                            data_read, variable_dict, parent_read, depth + 1);
        if (offset < 0)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Operator_SUB\n");
            return offset;
        }
        get_double(ctx, data_read, &val2);
        data_read->double_val = val1 - val2;
        data_read->data_type = EPRCSchema_Data_Double;
        return offset;

    case EPRCSchema_Operator_LT:
        offset = prc_execute_schema_instruction(ctx, bit_state, codes, count, offset + 1,
                                             data_read, variable_dict, parent_read, depth + 1);
        if (offset < 0)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Operator_LT\n");
            return offset;
        }
        get_double(ctx, data_read, &val1);
        offset = prc_execute_schema_instruction(ctx, bit_state, codes, count, offset,
                                            data_read, variable_dict, parent_read, depth + 1);
        if (offset < 0)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Operator_LT\n");
            return offset;
        }
        get_double(ctx, data_read, &val2);
        data_read->bool_val = ((val1 < val2) ? 1 : 0);
        data_read->data_type = EPRCSchema_Data_Boolean;
        return offset;

    case EPRCSchema_Operator_LE:
        offset = prc_execute_schema_instruction(ctx, bit_state, codes, count, offset + 1,
                                            data_read, variable_dict, parent_read, depth + 1);
        if (offset < 0)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Operator_LE\n");
            return offset;
        }
        get_double(ctx, data_read, &val1);
        offset = prc_execute_schema_instruction(ctx, bit_state, codes, count, offset,
                                          data_read, variable_dict, parent_read, depth + 1);
        if (offset < 0)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Operator_LE\n");
            return offset;
        }
        get_double(ctx, data_read, &val2);
        data_read->bool_val = ((val1 <= val2) ? 1 : 0);
        data_read->data_type = EPRCSchema_Data_Boolean;
        return offset;

    case EPRCSchema_Operator_GT:
        offset = prc_execute_schema_instruction(ctx, bit_state, codes, count, offset + 1,
                                            data_read, variable_dict, parent_read, depth + 1);
        if (offset < 0)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Operator_GT\n");
            return offset;
        }
        get_double(ctx, data_read, &val1);
        offset = prc_execute_schema_instruction(ctx, bit_state, codes, count, offset,
                                          data_read, variable_dict, parent_read, depth + 1);
        if (offset < 0)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Operator_GT\n");
            return offset;
        }
        get_double(ctx, data_read, &val2);
        data_read->bool_val = ((val1 > val2) ? 1 : 0);
        data_read->data_type = EPRCSchema_Data_Boolean;
        return offset;

    case EPRCSchema_Operator_GE:
        offset = prc_execute_schema_instruction(ctx, bit_state, codes, count, offset + 1,
                                            data_read, variable_dict, parent_read, depth + 1);
        if (offset < 0)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Operator_GE\n");
            return offset;
        }
        get_double(ctx, data_read, &val1);
        offset = prc_execute_schema_instruction(ctx, bit_state, codes, count, offset,
                                           data_read, variable_dict, parent_read, depth + 1);
        if (offset < 0)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Operator_GE\n");
            return offset;
        }
        get_double(ctx, data_read, &val2);
        data_read->bool_val = ((val1 >= val2) ? 1 : 0);
        data_read->data_type = EPRCSchema_Data_Boolean;
        return offset;

    case EPRCSchema_Operator_EQ:
        offset = prc_execute_schema_instruction(ctx, bit_state, codes, count, offset + 1,
                                             data_read, variable_dict, parent_read, depth + 1);
        if (offset < 0)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Operator_EQ\n");
            return offset;
        }
        get_double(ctx, data_read, &val1);
        offset = prc_execute_schema_instruction(ctx, bit_state, codes, count, offset,
                                           data_read, variable_dict, parent_read, depth + 1);
        if (offset < 0)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Operator_EQ\n");
            return offset;
        }
        get_double(ctx, data_read, &val2);
        data_read->bool_val = ((val1 == val2) ? 1 : 0);
        data_read->data_type = EPRCSchema_Data_Boolean;
        return offset;

    case EPRCSchema_Operator_NEQ:
        offset = prc_execute_schema_instruction(ctx, bit_state, codes, count, offset + 1,
                                            data_read, variable_dict, parent_read, depth + 1);
        if (offset < 0)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Operator_NEQ\n");
            return offset;
        }
        get_double(ctx, data_read, &val1);
        offset = prc_execute_schema_instruction(ctx, bit_state, codes, count, offset,
                                          data_read, variable_dict, parent_read, depth + 1);
        if (offset < 0)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with EPRCSchema_Operator_NEQ\n");
            return offset;
        }
        get_double(ctx, data_read, &val2);
        data_read->bool_val = ((val1 != val2) ? 1 : 0);
        data_read->data_type = EPRCSchema_Data_Boolean;
        return offset;

    case EPRCSchema_Operator_IGNORE1:
        /* There apparently is a data field that needs to be ignored. */
        return offset + 2;

    case EPRCSchema_Operator_IGNORE2:
        /* There apparently is a data field that needs to be ignored. */
        return offset + 2;

    default:
        return offset + 1;
    }
}

prc_entity_schema*
prc_get_schema(prc_context *ctx, uint32_t number)
{
    prc_schema *schema = ctx->internal.schema;
    if (schema == NULL || number >= schema->schema_count)
        return NULL;
    return &schema->entity_schema[number];
}

int
prc_execute_schema(prc_context *ctx, prc_bit_state *bit_state,
                   prc_entity_schema *schema, int *recursion)
{
    int offset;
    prc_schema_read data_read, parent_read;
    schema_dict *variable_dict = NULL;
    int parent_code;
    int code;

    if (schema == NULL)
    {
        prc_error(ctx, PRC_SCHEMA_ERROR, "NULL schema passed to prc_execute_schema\n");
        return PRC_SCHEMA_ERROR;
    }

    data_read.in_for_loop = 0;
    data_read.data_type = EPRCSchema_Operator_IGNORE1;

    parent_read.data_type = EPRCSchema_Operator_IGNORE1;
    parent_read.in_for_loop = 0;

    for (offset = 0; offset < schema->token_count;)
    {
        offset = prc_execute_schema_instruction(ctx, bit_state, schema->schema_tokens,
                                      (int)schema->token_count, offset, &data_read, &variable_dict,
                                      &parent_read, 0);
        if (offset < 0)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error with prc_execute_schema_instruction\n");
            return offset;
        }
    }

    if (variable_dict != NULL)
    {
        while (variable_dict != NULL)
        {
            schema_dict *next = variable_dict->next;
            prc_free(ctx, variable_dict);
            variable_dict = next;
        }
    }

    if (parent_read.data_type == EPRCSchema_PARENT_TYPE)
    {
        prc_entity_schema *parent_schema;

        *recursion = *recursion + 1;
        if (*recursion > SCHEMA_RECURSION_MAX)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Error in prc_execute_schema recursion\n");
            return PRC_SCHEMA_ERROR;
        }

        parent_code = prc_check_for_schema(ctx, parent_read.parent_type);
        if (parent_code <= 0)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Unknown parent schema type in prc_execute_schema\n");
            return PRC_SCHEMA_ERROR;
        }

        parent_schema = prc_get_schema(ctx, (uint32_t)(parent_code - 1));
        if (parent_schema == NULL)
        {
            prc_error(ctx, PRC_SCHEMA_ERROR, "Invalid parent schema index in prc_execute_schema\n");
            return PRC_SCHEMA_ERROR;
        }

        code = prc_execute_schema(ctx, bit_state, parent_schema, recursion);
        if (code < 0)
        {
            prc_error(ctx, code, "Error in prc_execute_schema recursion\n");
            return code;
        }
    }

    return 0;
}
