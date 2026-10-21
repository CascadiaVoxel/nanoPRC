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

/* pdf_get_file_id and pdf_get_file_id_from_object: the trailer /ID array may
   hold hex strings <...> or literal strings (...). Both are valid PDF.

   The literal case is taken from a real file, a published standard whose
   trailer reads /ID [(\343\357\213E\200\3657...) (...)]. Before literal
   strings were accepted, that file failed to open with "Did not find < at
   start of hex string". */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "prc_test.h"
#include "prc_context.h"
#include "prc_data.h"
#include "prc_pdf.h"

static int
read_trailer_id(prc_context *ctx, const char *text, uint8_t *id, uint32_t *id_len)
{
    return pdf_get_file_id(ctx, (uint8_t *)text, (uint32_t)strlen(text), id, id_len);
}

/* The literal /ID from the motivating file. \3657 is the octal byte \365
   followed by the character 7: an octal escape takes at most three digits. */
static void
test_literal_id_in_trailer(prc_context *ctx)
{
    static const char text[] =
        "trailer\n"
        "<</Size 10/Root 1 0 R/Info 2 0 R"
        "/ID [(\\343\\357\\213E\\200\\3657\\031x;\\257\\253\\203f\\317\\274)"
        " (\\323 rm\\360\\320MI\\213\\255\\216j<%*\\273)]>>\n"
        "startxref\n"
        "116\n"
        "%%EOF\n";
    static const uint8_t expected[16] = {
        0xe3, 0xef, 0x8b, 0x45, 0x80, 0xf5, 0x37, 0x19,
        0x78, 0x3b, 0xaf, 0xab, 0x83, 0x66, 0xcf, 0xbc
    };
    uint8_t id[PDF_MAX_DICT_VALUE];
    uint32_t id_len = 0;

    PRC_ASSERT_EQ(read_trailer_id(ctx, text, id, &id_len), 1);
    PRC_ASSERT_EQ(id_len, 16);
    PRC_ASSERT(memcmp(id, expected, 16) == 0);
}

/* The hex form still reads as before */
static void
test_hex_id_in_trailer(prc_context *ctx)
{
    static const char text[] =
        "trailer\n"
        "<</Size 10/Root 1 0 R"
        "/ID [<E3EF8B4580F53719783BAFAB8366CFBC> <d3206d6df0d04d498badd8e6a3c252ab>]>>\n"
        "startxref\n"
        "116\n"
        "%%EOF\n";
    static const uint8_t expected[16] = {
        0xe3, 0xef, 0x8b, 0x45, 0x80, 0xf5, 0x37, 0x19,
        0x78, 0x3b, 0xaf, 0xab, 0x83, 0x66, 0xcf, 0xbc
    };
    uint8_t id[PDF_MAX_DICT_VALUE];
    uint32_t id_len = 0;

    PRC_ASSERT_EQ(read_trailer_id(ctx, text, id, &id_len), 1);
    PRC_ASSERT_EQ(id_len, 16);
    PRC_ASSERT(memcmp(id, expected, 16) == 0);
}

/* Every kind of escape, balanced parentheses, a backslash before CR LF (the
   lines join, no byte), and a bare CR LF (one line feed). */
static void
test_literal_id_escapes(prc_context *ctx)
{
    static const char text[] =
        "trailer\n"
        "<</ID [(a\\(b\\)(c)\\\\\\n\\r\\t\\b\\f\\101\\0657\\q\\\r\nz\r\nw) (x)]>>\n"
        "startxref\n"
        "116\n"
        "%%EOF\n";
    static const uint8_t expected[] = {
        'a', '(', 'b', ')', '(', 'c', ')', '\\',
        0x0A, 0x0D, 0x09, 0x08, 0x0C, 'A', '5', '7', 'q',
        'z', 0x0A, 'w'
    };
    uint8_t id[PDF_MAX_DICT_VALUE];
    uint32_t id_len = 0;

    PRC_ASSERT_EQ(read_trailer_id(ctx, text, id, &id_len), 1);
    PRC_ASSERT_EQ(id_len, sizeof(expected));
    PRC_ASSERT(memcmp(id, expected, sizeof(expected)) == 0);
}

/* A trailer with no /ID still reports none */
static void
test_no_id_in_trailer(prc_context *ctx)
{
    static const char text[] =
        "trailer\n"
        "<</Size 10/Root 1 0 R>>\n"
        "startxref\n"
        "116\n"
        "%%EOF\n";
    uint8_t id[PDF_MAX_DICT_VALUE];
    uint32_t id_len = 0;

    PRC_ASSERT_EQ(read_trailer_id(ctx, text, id, &id_len), 0);
    PRC_ASSERT_EQ(id_len, 0);
}

/* A cross-reference stream carries the /ID in its own dictionary */
static void
test_literal_id_in_xref_stream(prc_context *ctx)
{
    static const char text[] =
        "12 0 obj\n"
        "<</Type/XRef/Size 13/ID [(\\001\\002ab) (cd)]>>\n"
        "stream\n"
        "endstream\n"
        "endobj\n";
    static const uint8_t expected[] = { 0x01, 0x02, 'a', 'b' };
    uint8_t id[PDF_MAX_DICT_VALUE];
    uint32_t id_len = 0;

    PRC_ASSERT_EQ(pdf_get_file_id_from_object(ctx, (uint8_t *)text,
                  (uint8_t *)text + strlen(text), id, &id_len), 1);
    PRC_ASSERT_EQ(id_len, sizeof(expected));
    PRC_ASSERT(memcmp(id, expected, sizeof(expected)) == 0);
}

int
main(void)
{
    prc_context *ctx;

    PRC_TEST_BEGIN("test_pdf_file_id");

    ctx = prc_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    test_literal_id_in_trailer(ctx);
    test_hex_id_in_trailer(ctx);
    test_literal_id_escapes(ctx);
    test_no_id_in_trailer(ctx);
    test_literal_id_in_xref_stream(ctx);

    prc_release_context(ctx);

    PRC_TEST_END;
}
