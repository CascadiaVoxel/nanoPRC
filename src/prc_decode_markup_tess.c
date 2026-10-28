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

#include "prc_decode_markup_tess.h"
#include <string.h>

static int
prc_initialize_markup_state(prc_context *ctx, prc_markup_state **state)
{
    *state = (prc_markup_state *)prc_malloc(ctx, sizeof(prc_markup_state));
    if (*state == NULL)
        return PRC_ERROR_MEMORY;
    (*state)->line_width = 1.0;

    memset((*state)->matrix, 0, sizeof(double) * 16);
    (*state)->matrix[0] = 1.0;
    (*state)->matrix[5] = 1.0;
    (*state)->matrix[10] = 1.0;
    (*state)->matrix[15] = 1.0;
    (*state)->is_identity = 1;

    (*state)->next = NULL;

    return 0;
}

static void
prc_apply_matrix(prc_context *ctx, const prc_markup_state *state, prc_vec3 *point)
{
    double x = point->x;
    double y = point->y;
    double z = point->z;

    if (state->is_identity)
        return;

    point->x = state->matrix[0] * x + state->matrix[4] * y + state->matrix[8] * z + state->matrix[12];
    point->y = state->matrix[1] * x + state->matrix[5] * y + state->matrix[9] * z + state->matrix[13];
    point->z = state->matrix[2] * x + state->matrix[6] * y + state->matrix[10] * z + state->matrix[14];
}

static void
prc_matrix_multiply(const double *matrix1, const double *matrix2, double *result)
{
    double temp[16];
    int i, j, k;
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            temp[i * 4 + j] = 0.0;
            for (k = 0; k < 4; k++)
            {
                temp[i * 4 + j] += matrix1[i * 4 + k] * matrix2[k * 4 + j];
            }
        }
    }
    memcpy(result, temp, sizeof(double) * 16);
}

/* Allocate a new state on the state stack and set the matrix */
static int
prc_set_matrix(prc_context *ctx, prc_markup_state **state, const double *matrix)
{
    prc_markup_state *temp = (prc_markup_state *)prc_malloc(ctx, sizeof(prc_markup_state));
    if (temp == NULL)
        return PRC_ERROR_MEMORY;

    memcpy(temp->matrix, matrix, sizeof(double) * 16);
    temp->is_identity = 0;

    temp->next = *state;
    *state = temp;

    return 0;
}

/* Pop a state from the state stack */
static int
prc_restore_matrix(prc_context *ctx, prc_markup_state **state)
{
    prc_markup_state *temp = *state;
    if (temp->next == NULL)
        return PRC_ERROR_MEMORY;

    *state = temp->next;
    prc_free(ctx, temp);

    return 0;
}

static int
prc_initialize_mode_stack(prc_context *ctx, prc_markup_mode_stack **stack)
{
    *stack = (prc_markup_mode_stack *)prc_malloc(ctx, sizeof(prc_markup_mode_stack));
    if (*stack == NULL)
        return PRC_ERROR_MEMORY;

    (*stack)->mode = PRC_NO_MODE;
    (*stack)->next = NULL;

    return 0;
}

/* Push a mode on the LIFO stack */
static int
prc_push_mode_stack(prc_context *ctx, prc_markup_mode_stack **stack, prc_markup_block_mode_t mode)
{
    prc_markup_mode_stack *temp = (prc_markup_mode_stack *)prc_malloc(ctx, sizeof(prc_markup_mode_stack));
    if (temp == NULL)
        return PRC_ERROR_MEMORY;

    temp->mode = mode;
    temp->next = *stack;

    *stack = temp;

    return 0;
}

/* Pop a mode from the LIFO stack */
static int
prc_pop_mode_stack(prc_context *ctx, prc_markup_mode_stack **stack)
{
    prc_markup_mode_stack *temp = *stack;

    if (temp->next == NULL)
        return PRC_ERROR_MEMORY;

    *stack = temp->next;
    prc_free(ctx, temp);

    return 0;
}

/* Every count and index below comes from the file. Before a code or a double is read, check that the
   array holds it; a file that declares more than it stores is refused rather than read past its end.
   These two need k, num_codes, doubles_count, num_coords, code and the fail label in scope. */
#define MKP_NEED_CODES(n) \
    do { if ((uint64_t)k + (uint64_t)(n) > (uint64_t)num_codes) { code = PRC_ERROR_FILE; goto fail; } } while (0)
#define MKP_NEED_DOUBLES(n) \
    do { if ((uint64_t)doubles_count + (uint64_t)(n) > (uint64_t)num_coords) { code = PRC_ERROR_FILE; goto fail; } } while (0)

int
prc_decode_markup_tess(prc_context *ctx, prc_tess_markup *data)
{
    uint32_t k = 0;
    uint32_t code_value;
    uint32_t num_doubles;
    uint32_t doubles_count = 0;  /* For debug */
    uint32_t num_codes = data->number_of_codes;
    uint32_t num_coords = data->tessellation_coordinates.number_of_coordinates;
    uint32_t num_inner_codes;
    prc_markup_entity_t extra_data_type;
    prc_markup_type_t markup_type = PRC_MARKUPTYPE_IS_UNKNOWN;
    prc_markup_state *markup_state = NULL;
    prc_markup_mode_stack *mode_stack = NULL;
    int code;
    uint32_t num_entites_in_block = 0;
    uint32_t max_num_primitive, max_num_vertices;
    uint32_t num_primitives = 0;
    uint32_t num_vertices = 0;
    uint32_t j;
    uint32_t biased_color_index = 0;
    uint32_t biased_pattern_index = 0;
    uint32_t biased_font_index = 0;
    uint32_t biased_string_index = 0;
    double text_height = 0;
    double text_width = 0;
    prc_vec3 frame_draw_origin;

    frame_draw_origin.x = 0.0;
    frame_draw_origin.y = 0.0;
    frame_draw_origin.z = 0.0;

    max_num_primitive = data->number_of_codes / 2;

    data->decode_primitives = (prc_markup_primitive_decode *)prc_calloc(ctx,
                            sizeof(prc_markup_primitive_decode), max_num_primitive);
    if (data->decode_primitives == NULL)
        return PRC_ERROR_MEMORY;

    max_num_vertices = data->tessellation_coordinates.number_of_coordinates / 3 + 1;
    data->decode_vertices = (prc_vec3 *)prc_calloc(ctx,
                                                sizeof(prc_vec3), max_num_vertices);
    if (data->decode_vertices == NULL)
        return PRC_ERROR_MEMORY;

    code = prc_initialize_mode_stack(ctx, &mode_stack);
    if (code < 0)
        return code;

    code = prc_initialize_markup_state(ctx, &markup_state);
    if (code < 0)
        goto fail;

    /* In the code array, the first code contains the entity type AND the number of
       specific inner codes. The second code is the number of doubles (coordinates)
       for this entity.  These doubles are located in the coordinates array */

    /* Lets step through the code array */
    while (k < num_codes)
    {
        MKP_NEED_CODES(2);
        code_value = data->code_numbers[k];
        num_inner_codes = code_value & PRC_MARKUP_IntegerMask;

        if (code_value & PRC_MARKUP_IsMatrix)
        {
            markup_type = PRC_MARKUPTYPE_IS_MATRIX;
            num_doubles = data->code_numbers[k + 1]; /* Number of doubles in the block */
            if (!markup_state->is_identity)
            {
                code = prc_restore_matrix(ctx, &markup_state);
                if (code < 0)
                    goto fail;
            }
            else
            {
                /* Set the state stack with the new matrix */
                MKP_NEED_DOUBLES(16);
                code = prc_set_matrix(ctx, &markup_state,
                    &data->tessellation_coordinates.coordinates[doubles_count]);
                if (code < 0)
                    goto fail;
                doubles_count += 16; /* We consumed 16 doubles for the matrix */
                num_entites_in_block = num_inner_codes; /* Only needed if we wanted to skip */
            }
            if (code < 0)
                goto fail;
        }
        else if (code_value & PRC_MARKUP_IsExtraData)
        {
            markup_type = PRC_MARKUPTYPE_IS_EXTRA_DATA;

            /* These are defined in Table of Entities under the Extra Data Type column e.g. FaceView */
            extra_data_type = (code_value & PRC_MARKUP_ExtraDataType) >> 21;

            switch (extra_data_type)
            {
            case PRC_PATTERN_ENTITY:
                num_doubles = data->code_numbers[k + 1]; /* Points in loop * 3 */
                doubles_count += num_doubles;
                //return PRC_ERROR_NOT_IMPLEMENTED;
                break;

            case PRC_PICTURE_ENTITY:
                num_doubles = data->code_numbers[k + 1];
                if (num_doubles != 0)
                    { code = PRC_ERROR_FILE; goto fail; }
                //return PRC_ERROR_NOT_IMPLEMENTED;
                break;

            case PRC_TRIANGLES_ENTITY:
                num_doubles = data->code_numbers[k + 1]; /* Number of triangles * 9 */
                /* double_count is updated when we read the vertices */
                break;

            case PRC_QUADS_ENTITY:
                num_doubles = data->code_numbers[k + 1]; /* Number of quads * 12 */
                doubles_count += num_doubles;
                //return PRC_ERROR_NOT_IMPLEMENTED;
                break;

            case PRC_FACE_VIEW_MODEL:
                /* If current mode on stack is PRC_FACE_VIEW_MODE then pop else push */
                num_doubles = data->code_numbers[k + 1]; /* Number of doubles in the block */
                if (mode_stack->mode == PRC_FACE_VIEW_MODE)
                {
                    code = prc_pop_mode_stack(ctx, &mode_stack);
                }
                else
                {
                    code = prc_push_mode_stack(ctx, &mode_stack, PRC_FACE_VIEW_MODE);
                    num_entites_in_block = num_inner_codes;
                }
                if (code < 0)
                    goto fail;
               // return PRC_ERROR_NOT_IMPLEMENTED;
                break;

            case PRC_FRAME_DRAW_MODEL:
                /* If current mode on stack is PRC_FRAME_DRAW_MODE then pop else push */
                num_doubles = data->code_numbers[k + 1]; /* Number of doubles in the block */
                if (mode_stack->mode == PRC_FRAME_DRAW_MODE)
                {
                    code = prc_pop_mode_stack(ctx, &mode_stack);
                    /* The origin belongs to the block that has just ended */
                    frame_draw_origin.x = 0.0;
                    frame_draw_origin.y = 0.0;
                    frame_draw_origin.z = 0.0;
                }
                else
                {
                    code = prc_push_mode_stack(ctx, &mode_stack, PRC_FRAME_DRAW_MODE);
                    num_entites_in_block = num_inner_codes;

                    /* Frame draw mode puts a 3D point in the doubles array. This
                       point corresponds to a 3D point projected onto the screen
                       providing the origin of the 2-D coordinate system in which
                       to draw */
                       /* This is *very* unclear in the specification */
                    MKP_NEED_DOUBLES(3);
                    frame_draw_origin.x =
                        data->tessellation_coordinates.coordinates[doubles_count];
                    frame_draw_origin.y =
                        data->tessellation_coordinates.coordinates[doubles_count + 1];
                    frame_draw_origin.z =
                        data->tessellation_coordinates.coordinates[doubles_count + 2];
                    doubles_count += 3;
                }
                if (code < 0)
                    goto fail;
                break;

            case PRC_FIXED_SIZE_MODEL:
                /* If current mode on stack is PRC_FIXED_SIZE_MODE then pop else push */
                num_doubles = data->code_numbers[k + 1]; /* Number of doubles in the block */
                if (mode_stack->mode == PRC_FIXED_SIZE_MODE)
                    code = prc_pop_mode_stack(ctx, &mode_stack);
                else
                {
                    code = prc_push_mode_stack(ctx, &mode_stack, PRC_FIXED_SIZE_MODE);
                    num_entites_in_block = num_inner_codes;
                }
                if (code < 0)
                    goto fail;
                //return PRC_ERROR_NOT_IMPLEMENTED;
                break;

            case PRC_SYMBOL_ENTITY:
                num_doubles = data->code_numbers[k + 1]; /* Must be 3 */
                doubles_count += num_doubles;
                if (num_doubles != 3)
                    { code = PRC_ERROR_FILE; goto fail; }
                if (num_inner_codes != 1)
                    { code = PRC_ERROR_FILE; goto fail; }
                /* Inner code is pattern identifier index into the picture array
                   stored in FileStructureInternalGlobalData */
                /* TODO actually draw this. For now, just consume the data */
                MKP_NEED_CODES(3);
                biased_pattern_index = data->code_numbers[k + 2] + 1;
                k = k + 1;
                break;

            case PRC_CYLINDER_ENTITY:
                num_doubles = data->code_numbers[k + 1]; /* Must be 3 */
                doubles_count += num_doubles;
                if (num_doubles != 3)
                    { code = PRC_ERROR_FILE; goto fail; }
                //return PRC_ERROR_NOT_IMPLEMENTED;
                break;

            case PRC_COLOR_ENTITY:
                num_doubles = data->code_numbers[k + 1]; /* Must be 0 */
                if (num_doubles != 0)
                    { code = PRC_ERROR_FILE; goto fail; }
                if (num_inner_codes != 1)
                    { code = PRC_ERROR_FILE; goto fail; }
                /* The inner code is an index into the color array in the
                   global data. I assume it is a multiple of 3 like the other
                   indices into that array */
                MKP_NEED_CODES(3);
                biased_color_index = data->code_numbers[k + 2] + 1; /* This stays in effect until reset */
                k = k + 1;
                break;

            case PRC_LINE_STIPPLE_ENTITY:
                num_doubles = data->code_numbers[k + 1]; /* Must be 10 */
                doubles_count += num_doubles;
                if (num_doubles != 10)
                    { code = PRC_ERROR_FILE; goto fail; }
                //return PRC_ERROR_NOT_IMPLEMENTED;
                break;

            case PRC_FONT_ENTITY:
                num_doubles = data->code_numbers[k + 1]; /* Must be 0 */
                if (num_doubles != 0)
                    { code = PRC_ERROR_FILE; goto fail; }
                if (num_inner_codes != 1)
                    { code = PRC_ERROR_FILE; goto fail; }
                MKP_NEED_CODES(3);
                /* Files store this index 1-based already, so it is kept as read. This stays in effect until reset */
                biased_font_index = data->code_numbers[k + 2];
                k = k + 1;
                break;

            case PRC_TEXT_ENTITY:
                num_doubles = data->code_numbers[k + 1]; /* Must be 2 */
                MKP_NEED_DOUBLES(2);
                text_width = data->tessellation_coordinates.coordinates[doubles_count];
                text_height = data->tessellation_coordinates.coordinates[doubles_count + 1];
                doubles_count += 2;
                if (num_doubles != 2)
                    { code = PRC_ERROR_FILE; goto fail; }
                if (num_inner_codes != 1)
                    { code = PRC_ERROR_FILE; goto fail; }
                MKP_NEED_CODES(3);
                biased_string_index = data->code_numbers[k + 2] + 1; /* This stays in effect until reset */
                k = k + 1;
                break;

            case PRC_POINTS_ENTITY:
                num_doubles = data->code_numbers[k + 1]; /* Number of points * 3 */
                doubles_count += num_doubles;
               // return PRC_ERROR_NOT_IMPLEMENTED;
                break;

            case PRC_POLYGON_ENTITY:
                /* These are typically a triangle that must be filled */
                num_doubles = data->code_numbers[k + 1]; /* Number of points * 3 */
                break;

            case PRC_LINEWIDTH_ENTITY:
                num_doubles = data->code_numbers[k + 1]; /* Must be 1 or 0 */
                doubles_count += num_doubles;
                if (num_doubles != 1 && num_doubles != 0)
                    { code = PRC_ERROR_FILE; goto fail; }
                //return PRC_ERROR_NOT_IMPLEMENTED;
                break;

            default:
                { code = PRC_ERROR_FILE; goto fail; }
            }
        }
        else
        {
            markup_type = PRC_MARKUPTYPE_IS_POLYLINE;/* Default case is polyline */
            extra_data_type = PRC_POLYLINE_ENTITY;   /* Default case is polyline */
            num_doubles = data->code_numbers[k + 1]; /* Number of points times 3 */
        }

        if (markup_type != PRC_MARKUPTYPE_IS_MATRIX)
        {
            /* Oddly there are files out that have polylines with no points */
            if (num_doubles > 0)
            {
                /* Triangles we fill, polylines we do not. If we have three points for a polygon,
                   then we treat this as a triangle that we fill.  If we have more than three points
                   for a polygon, then we treat this as a line loop that we do not fill --
                   we need to fix this and do a tessellation of that polygon. */
                if (extra_data_type == PRC_POLYLINE_ENTITY ||
                    extra_data_type == PRC_POLYGON_ENTITY ||
                    extra_data_type == PRC_TRIANGLES_ENTITY)
                {
                    /* Add a primitive */
                    MKP_NEED_DOUBLES((uint64_t)(num_doubles / 3) * 3);
                    if ((uint64_t)num_vertices + num_doubles / 3 > (uint64_t)max_num_vertices)
                    { code = PRC_ERROR_FILE; goto fail; }
                    data->decode_primitives[num_primitives].biased_color_index = biased_color_index;
                    data->decode_primitives[num_primitives].biased_font_index = biased_font_index;
                    data->decode_primitives[num_primitives].text = NULL;
                    data->decode_primitives[num_primitives].text_height = text_height;
                    data->decode_primitives[num_primitives].text_width = text_width;
                    data->decode_primitives[num_primitives].face_frame_draw_origin = frame_draw_origin;
                    data->decode_primitives[num_primitives].line_width = markup_state->line_width;
                    data->decode_primitives[num_primitives].number_indices = num_doubles / 3;
                    data->decode_primitives[num_primitives].indices = (uint32_t *)prc_calloc(ctx,
                        sizeof(uint32_t), data->decode_primitives[num_primitives].number_indices);

                    if (data->decode_primitives[num_primitives].indices == NULL)
                        { code = PRC_ERROR_MEMORY; goto fail; }

                    for (j = 0; j < data->decode_primitives[num_primitives].number_indices; j++)
                    {
                        data->decode_primitives[num_primitives].indices[j] = num_vertices;
                        data->decode_vertices[num_vertices].x = data->tessellation_coordinates.coordinates[doubles_count];
                        data->decode_vertices[num_vertices].y = data->tessellation_coordinates.coordinates[doubles_count + 1];
                        data->decode_vertices[num_vertices].z = data->tessellation_coordinates.coordinates[doubles_count + 2];
                        prc_apply_matrix(ctx, markup_state, &data->decode_vertices[num_vertices]);
                        num_vertices++;
                        doubles_count += 3;
                    }

                    if (extra_data_type == PRC_POLYLINE_ENTITY)
                        /* No filling */
                        data->decode_primitives[num_primitives].primitive_type = MARKUP_LINE_STRIP;
                    else if (extra_data_type == PRC_TRIANGLES_ENTITY)
                    {
                        data->decode_primitives[num_primitives].primitive_type = MARKUP_TRIANGLE;
                    }
                    else
                    {
                        /* If we have three points for PRC_POLYGON_ENTITY,
                           then treat this as a triangle that we fill. */
                        if (data->decode_primitives[num_primitives].number_indices == 3)
                        {
                            data->decode_primitives[num_primitives].primitive_type = MARKUP_TRIANGLE;
                        }
                        else
                        {
                            /* If we have more than three points for PRC_POLYGON_ENTITY,
                               then treat this as a line loop that we do not fill --
                               we need to fix this and do a tessellation of that polygon. */
                            data->decode_primitives[num_primitives].primitive_type = MARKUP_LINE_LOOP;
                        }
                    }

                    data->decode_primitives[num_primitives].block_mode = mode_stack->mode;
                    num_primitives++;
                }
            }
            else if (extra_data_type == PRC_TEXT_ENTITY)
            {
                /* Add a primitive */
                uint32_t string_length;
                if (biased_string_index == 0 || biased_string_index > data->number_of_text_strings ||
                    data->text_strings == NULL || data->text_strings[biased_string_index - 1].string == NULL)
                { code = PRC_ERROR_FILE; goto fail; }
                string_length = (uint32_t)strlen((const char*) data->text_strings[biased_string_index - 1].string);
                data->decode_primitives[num_primitives].text = (char *)prc_calloc(ctx, string_length + 1, 1);
                if (data->decode_primitives[num_primitives].text == NULL)
                    { code = PRC_ERROR_MEMORY; goto fail; }
                strcpy(data->decode_primitives[num_primitives].text,
                      (const char*) data->text_strings[biased_string_index - 1].string);

                data->decode_primitives[num_primitives].biased_color_index = biased_color_index;
                data->decode_primitives[num_primitives].biased_font_index = biased_font_index;
                data->decode_primitives[num_primitives].text_height = text_height;
                data->decode_primitives[num_primitives].text_width = text_width;
                data->decode_primitives[num_primitives].face_frame_draw_origin = frame_draw_origin;
                prc_apply_matrix(ctx, markup_state, &data->decode_primitives[num_primitives].face_frame_draw_origin);
                data->decode_primitives[num_primitives].primitive_type = MARKUP_TEXT;
                data->decode_primitives[num_primitives].block_mode = mode_stack->mode;
                num_primitives++;
            }
        }

        k += 2;
    }

    data->decode_number_primitives = num_primitives;
    data->decode_num_vertices = num_vertices;

    /* Verify that the stack only has one item, which is the root */
    if (mode_stack->next != NULL)
    { code = PRC_ERROR_FILE; goto fail; }

    /* The turbine part fails this test */
    /* Verify that the number of doubles in the block matches the number of doubles */
   // if (doubles_count != data->tessellation_coordinates.number_of_coordinates)
   //     return PRC_ERROR_FILE;

    /* Verify that the state has only 1 item */
    if (markup_state->next != NULL)
    { code = PRC_ERROR_FILE; goto fail; }

    prc_free(ctx, markup_state);
    prc_free(ctx, mode_stack);

    return 0;

fail:
    /* Keep what was decoded so teardown frees it, and free both stacks whole. */
    data->decode_number_primitives = num_primitives;
    data->decode_num_vertices = num_vertices;
    while (markup_state != NULL)
    {
        prc_markup_state *next = markup_state->next;
        prc_free(ctx, markup_state);
        markup_state = next;
    }
    while (mode_stack != NULL)
    {
        prc_markup_mode_stack *next = mode_stack->next;
        prc_free(ctx, mode_stack);
        mode_stack = next;
    }
    return code < 0 ? code : PRC_ERROR_FILE;
}

#undef MKP_NEED_CODES
#undef MKP_NEED_DOUBLES
