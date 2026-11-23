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

/* A markup text entity (Table 150, "Text": two doubles, the width and height, and one inner code, the
   index of a string) must decode to a text primitive. It never did: the branch that makes the
   primitive was the else of a test for "the entity has doubles", which a text entity always has, so
   no markup text was ever decoded and the viewer drew the lead lines of a callout without its label.
   On the 39 files of the public prc-db collection that have markup, that was 0 text primitives from
   9,233 stored strings. */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "prc_test.h"
#include "prc_context.h"
#include "prc_data.h"
#include "prc_decode_markup_tess.h"

#define EXTRA(type, inner) (PRC_MARKUP_IsExtraData | ((uint32_t)(type) << 21) | (uint32_t)(inner))

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
    m->decode_primitives = NULL;
    m->decode_vertices = NULL;
    m->decode_number_primitives = 0;
}

/* A markup with the given codes, doubles and strings. The caller releases it with release_markup. */
static void
build_markup(prc_tess_markup *m, const uint32_t *codes, uint32_t num_codes, const double *coords,
    uint32_t num_coords, const char *const *strings, uint32_t num_strings)
{
    uint32_t k;

    memset(m, 0, sizeof(*m));
    m->number_of_codes = num_codes;
    m->code_numbers = (prc_unsigned_int *)malloc(sizeof(prc_unsigned_int) * (num_codes ? num_codes : 1));
    memcpy(m->code_numbers, codes, sizeof(uint32_t) * num_codes);
    m->tessellation_coordinates.number_of_coordinates = num_coords;
    m->tessellation_coordinates.coordinates = (double *)malloc(sizeof(double) * (num_coords ? num_coords : 1));
    if (num_coords)
        memcpy(m->tessellation_coordinates.coordinates, coords, sizeof(double) * num_coords);
    m->number_of_text_strings = num_strings;
    if (num_strings)
    {
        m->text_strings = (prc_string *)calloc(num_strings, sizeof(prc_string));
        for (k = 0; k < num_strings; k++)
        {
            size_t n = strlen(strings[k]);
            m->text_strings[k].size = (prc_unsigned_int)n;
            m->text_strings[k].string = (unsigned char *)malloc(n + 1);
            memcpy(m->text_strings[k].string, strings[k], n + 1);
        }
    }
}

static void
release_markup(prc_context *ctx, prc_tess_markup *m)
{
    uint32_t k;
    release_decoded(ctx, m);
    for (k = 0; k < m->number_of_text_strings; k++)
        free(m->text_strings[k].string);
    free(m->text_strings);
    free(m->code_numbers);
    free(m->tessellation_coordinates.coordinates);
}

static void
test_text_alone(prc_context *ctx)
{
    const uint32_t codes[] = { EXTRA(PRC_TEXT_ENTITY, 1), 2, 0 };
    const double coords[] = { 3.5, 7.25 };
    const char *const strings[] = { "Check shaft runout" };
    prc_tess_markup m;

    printf("  sub-case: a text entity decodes to a text primitive with its string, width and height\n");
    build_markup(&m, codes, 3, coords, 2, strings, 1);
    PRC_ASSERT_EQ(prc_decode_markup_tess(ctx, &m), 0);
    PRC_ASSERT_EQ(m.decode_number_primitives, 1);
    PRC_ASSERT_EQ(m.decode_primitives[0].primitive_type, MARKUP_TEXT);
    PRC_ASSERT_NOT_NULL(m.decode_primitives[0].text);
    PRC_ASSERT(strcmp(m.decode_primitives[0].text, "Check shaft runout") == 0);
    PRC_ASSERT(m.decode_primitives[0].text_width == 3.5);
    PRC_ASSERT(m.decode_primitives[0].text_height == 7.25);
    release_markup(ctx, &m);
}

static void
test_text_after_lines_and_colour(prc_context *ctx)
{
    /* A colour, a two-point polyline, then the label: a callout's lead line and its text. */
    const uint32_t codes[] = { EXTRA(PRC_COLOR_ENTITY, 1), 0, 6, 0, 6, EXTRA(PRC_TEXT_ENTITY, 1), 2, 1 };
    const double coords[] = { 0, 0, 0, 1, 1, 1, 9.0, 8.0 };
    const char *const strings[] = { "first", "second" };
    prc_tess_markup m;

    printf("  sub-case: a label after a lead line keeps the line, and takes the colour and the string it names\n");
    build_markup(&m, codes, 8, coords, 8, strings, 2);
    PRC_ASSERT_EQ(prc_decode_markup_tess(ctx, &m), 0);
    PRC_ASSERT_EQ(m.decode_number_primitives, 2);
    PRC_ASSERT_EQ(m.decode_primitives[0].primitive_type, MARKUP_LINE_STRIP);
    PRC_ASSERT_EQ(m.decode_primitives[0].number_indices, 2);
    PRC_ASSERT_EQ(m.decode_primitives[1].primitive_type, MARKUP_TEXT);
    PRC_ASSERT(strcmp(m.decode_primitives[1].text, "second") == 0);
    PRC_ASSERT_EQ(m.decode_primitives[1].biased_color_index, 7);
    release_markup(ctx, &m);
}

static void
test_text_in_frame_draw(prc_context *ctx)
{
    /* A Frame Draw block (its three doubles are the 3D origin), a label inside it, the block's end. */
    const uint32_t codes[] = { EXTRA(PRC_FRAME_DRAW_MODEL, 1), 3, EXTRA(PRC_TEXT_ENTITY, 1), 2, 0,
                               EXTRA(PRC_FRAME_DRAW_MODEL, 0), 0 };
    const double coords[] = { 10.0, 20.0, 30.0, 6.625, 8.0 };
    const char *const strings[] = { "Push/Pull" };
    prc_tess_markup m;

    printf("  sub-case: a label in a Frame Draw block carries the block's origin and mode\n");
    build_markup(&m, codes, 7, coords, 5, strings, 1);
    PRC_ASSERT_EQ(prc_decode_markup_tess(ctx, &m), 0);
    PRC_ASSERT_EQ(m.decode_number_primitives, 1);
    PRC_ASSERT_EQ(m.decode_primitives[0].primitive_type, MARKUP_TEXT);
    PRC_ASSERT_EQ(m.decode_primitives[0].block_mode, PRC_FRAME_DRAW_MODE);
    PRC_ASSERT(m.decode_primitives[0].face_frame_draw_origin.x == 10.0);
    PRC_ASSERT(m.decode_primitives[0].face_frame_draw_origin.y == 20.0);
    PRC_ASSERT(m.decode_primitives[0].face_frame_draw_origin.z == 30.0);
    PRC_ASSERT(m.decode_primitives[0].text_height == 8.0);
    release_markup(ctx, &m);
}

static void
test_origin_of_fixed_size_block(prc_context *ctx)
{
    /* The shape of a callout in a real file: a Fixed Size block (three doubles of origin, then a two-point
       line), then a Frame Draw block holding the label. The Fixed Size origin was never read, so the line
       took the origin's doubles and the Frame Draw block took the line's: the label sat at the wrong place. */
    const uint32_t codes[] = { EXTRA(PRC_FIXED_SIZE_MODEL, 1), 9, 0, 6, EXTRA(PRC_FIXED_SIZE_MODEL, 0), 0,
                               EXTRA(PRC_FRAME_DRAW_MODEL, 1), 3, EXTRA(PRC_TEXT_ENTITY, 1), 2, 0,
                               EXTRA(PRC_FRAME_DRAW_MODEL, 0), 0 };
    const double coords[] = { 100, 200, 300, 1, 2, 3, 4, 5, 6, 10.0, 20.0, 30.0, 6.5, 8.0 };
    const char *const strings[] = { "Inlet" };
    prc_tess_markup m;

    printf("  sub-case: a Fixed Size block's origin is read, so the label's Frame Draw origin is the right doubles\n");
    build_markup(&m, codes, 13, coords, 14, strings, 1);
    PRC_ASSERT_EQ(prc_decode_markup_tess(ctx, &m), 0);
    PRC_ASSERT_EQ(m.decode_number_primitives, 2);
    PRC_ASSERT_EQ(m.decode_primitives[0].primitive_type, MARKUP_LINE_STRIP);
    PRC_ASSERT_EQ(m.decode_primitives[0].block_mode, PRC_FIXED_SIZE_MODE);
    PRC_ASSERT(m.decode_primitives[0].face_frame_draw_origin.x == 100.0);
    PRC_ASSERT(m.decode_vertices[0].x == 1.0 && m.decode_vertices[0].z == 3.0);
    PRC_ASSERT(m.decode_vertices[1].x == 4.0 && m.decode_vertices[1].z == 6.0);
    PRC_ASSERT_EQ(m.decode_primitives[1].primitive_type, MARKUP_TEXT);
    PRC_ASSERT_EQ(m.decode_primitives[1].block_mode, PRC_FRAME_DRAW_MODE);
    PRC_ASSERT(m.decode_primitives[1].face_frame_draw_origin.x == 10.0);
    PRC_ASSERT(m.decode_primitives[1].face_frame_draw_origin.y == 20.0);
    PRC_ASSERT(m.decode_primitives[1].face_frame_draw_origin.z == 30.0);
    PRC_ASSERT(m.decode_primitives[1].text_width == 6.5);
    release_markup(ctx, &m);
}

static void
test_origin_of_face_view_block(prc_context *ctx)
{
    const uint32_t codes[] = { EXTRA(PRC_FACE_VIEW_MODEL, 1), 5, EXTRA(PRC_TEXT_ENTITY, 1), 2, 0,
                               EXTRA(PRC_FACE_VIEW_MODEL, 0), 0 };
    const double coords[] = { 5.0, 6.0, 7.0, 2.5, 3.5 };
    const char *const strings[] = { "Vent" };
    prc_tess_markup m;

    printf("  sub-case: a label in a Face View block carries the block's origin and mode\n");
    build_markup(&m, codes, 7, coords, 5, strings, 1);
    PRC_ASSERT_EQ(prc_decode_markup_tess(ctx, &m), 0);
    PRC_ASSERT_EQ(m.decode_number_primitives, 1);
    PRC_ASSERT_EQ(m.decode_primitives[0].block_mode, PRC_FACE_VIEW_MODE);
    PRC_ASSERT(m.decode_primitives[0].face_frame_draw_origin.x == 5.0);
    PRC_ASSERT(m.decode_primitives[0].face_frame_draw_origin.y == 6.0);
    PRC_ASSERT(m.decode_primitives[0].face_frame_draw_origin.z == 7.0);
    PRC_ASSERT(m.decode_primitives[0].text_width == 2.5);
    PRC_ASSERT(m.decode_primitives[0].text_height == 3.5);
    release_markup(ctx, &m);
}

static void
test_bad_string_index_refused(prc_context *ctx)
{
    /* The text entity names string 5 and there is one. A markup like that is refused, not drawn
       with a string it does not have. */
    const uint32_t codes[] = { EXTRA(PRC_TEXT_ENTITY, 1), 2, 4 };
    const double coords[] = { 1.0, 1.0 };
    const char *const strings[] = { "only" };
    prc_tess_markup m;

    printf("  sub-case: a text entity naming a string that is not there is refused\n");
    build_markup(&m, codes, 3, coords, 2, strings, 1);
    PRC_ASSERT(prc_decode_markup_tess(ctx, &m) != 0);
    release_markup(ctx, &m);
}

int
main(void)
{
    prc_context *ctx;

    PRC_TEST_BEGIN("test_markup_text_decode");
    ctx = prc_api_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    test_text_alone(ctx);
    test_text_after_lines_and_colour(ctx);
    test_text_in_frame_draw(ctx);
    test_origin_of_fixed_size_block(ctx);
    test_origin_of_face_view_block(ctx);
    test_bad_string_index_refused(ctx);

    PRC_ASSERT_EQ(prc_api_release_context(ctx), 0);
    PRC_TEST_END;
}
