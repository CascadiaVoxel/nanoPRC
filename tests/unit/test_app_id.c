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

/* nanoPRC's application identifier: written by the writer into the main PRC
   header and the file-structure header, and recognised by
   prc_api_written_by_nanoprc, from a .prc file or a PDF. A file from another
   writer is not recognised. */

#include <stdio.h>
#include <string.h>
#include "prc_test.h"
#include "prc_api.h"

/* Set by tests/CMakeLists.txt; the default is for a build run from the
   repository root. */
#ifndef PRC_TEST_EXAMPLES_DIR
#define PRC_TEST_EXAMPLES_DIR "examples"
#endif

static const char *PRC_PATH = "test_app_id.prc";
static const char *PDF_PATH = "test_app_id.pdf";

/* The identifier as stored: "nanoPRC" and nine zero bytes. */
static const uint8_t APP_ID[16] = { 'n', 'a', 'n', 'o', 'P', 'R', 'C', 0, 0, 0, 0, 0, 0, 0, 0, 0 };

/* "PRC", min_version_for_read, authoring_version, unique_id_file, then
   unique_id_application. */
#define MAIN_HEADER_APP_ID_OFFSET (3 + 4 + 4 + 16)

static int
write_triangle(prc_context *ctx, uint8_t **buf, size_t *size)
{
    static const double pos[9] = { 0, 0, 0,  1, 0, 0,  0, 1, 0 };
    static const uint32_t tri[3] = { 0, 1, 2 };
    static const uint32_t counts[1] = { 1 };
    prc_api_write_tessellation t;
    prc_api_write_rep_item ri;
    prc_api_write_node root;

    memset(&t, 0, sizeof(t));
    t.kind = PRC_API_WRITE_TESS_KIND_COMPRESSED;
    t.positions = pos;
    t.num_positions = 3;
    t.tri_indices = tri;
    t.num_triangles = 1;
    t.face_tri_counts = counts;
    t.num_faces = 1;
    memset(&ri, 0, sizeof(ri));
    ri.kind = PRC_API_WRITE_RI_SURFACE;
    ri.biased_tessellation_index = 1;
    memset(&root, 0, sizeof(root));
    root.rep_items = &ri;
    root.num_rep_items = 1;
    root.bbox_max[0] = root.bbox_max[1] = 1.0;
    return prc_api_write_prc_buffer(ctx, "app_id", &root, &t, 1, buf, size);
}

/* Every place the 16 identifier bytes occur in the buffer. */
static uint32_t
count_app_id(const uint8_t *buf, size_t size)
{
    uint32_t n = 0;
    size_t i;

    for (i = 0; i + sizeof(APP_ID) <= size; i++)
        if (memcmp(buf + i, APP_ID, sizeof(APP_ID)) == 0)
            n++;
    return n;
}

static void
test_written(prc_context *ctx, const uint8_t *buf, size_t size)
{
    printf("  sub-case: the main header carries the identifier\n");
    PRC_ASSERT(size > MAIN_HEADER_APP_ID_OFFSET + sizeof(APP_ID));
    PRC_ASSERT(memcmp(buf, "PRC", 3) == 0);
    PRC_ASSERT(memcmp(buf + MAIN_HEADER_APP_ID_OFFSET, APP_ID, sizeof(APP_ID)) == 0);

    printf("  sub-case: so does the file-structure header, also uncompressed\n");
    PRC_ASSERT_EQ(count_app_id(buf, size), 2);
    (void)ctx;
}

static void
test_recognised(prc_context *ctx, const char *path)
{
    prc_api_data data;

    printf("  sub-case: %s is recognised as written by nanoPRC\n", path);
    data = prc_api_open_contents(ctx, path);
    PRC_ASSERT_NOT_NULL(data);
    PRC_ASSERT_EQ(prc_api_written_by_nanoprc(ctx, data), 1);
    prc_api_release_data(ctx, data, NULL, 0, NULL, 0, NULL, 0, NULL);
}

static void
test_not_recognised(prc_context *ctx)
{
    prc_api_data data;

    printf("  sub-case: examples/cube.pdf, from another writer, is not recognised\n");
    data = prc_api_open_contents(ctx, PRC_TEST_EXAMPLES_DIR "/cube.pdf");
    PRC_ASSERT_NOT_NULL(data);
    PRC_ASSERT_EQ(prc_api_written_by_nanoprc(ctx, data), 0);
    prc_api_release_data(ctx, data, NULL, 0, NULL, 0, NULL, 0, NULL);

    printf("  sub-case: no data is not recognised\n");
    PRC_ASSERT_EQ(prc_api_written_by_nanoprc(ctx, NULL), 0);
}

int
main(void)
{
    prc_context *ctx;
    uint8_t *buf = NULL;
    size_t size = 0;
    FILE *f;

    PRC_TEST_BEGIN("test_app_id");

    ctx = prc_api_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    PRC_ASSERT_EQ(write_triangle(ctx, &buf, &size), 0);
    test_written(ctx, buf, size);

    f = fopen(PRC_PATH, "wb");
    PRC_ASSERT_NOT_NULL(f);
    PRC_ASSERT_EQ(fwrite(buf, 1, size, f), size);
    fclose(f);
    PRC_ASSERT_EQ(prc_api_pdf_embed_prc(ctx, PDF_PATH, buf, size, NULL), 0);
    prc_api_write_prc_buffer_free(ctx, buf);

    test_recognised(ctx, PRC_PATH);
    test_recognised(ctx, PDF_PATH);
    test_not_recognised(ctx);

    remove(PRC_PATH);
    remove(PDF_PATH);
    PRC_ASSERT_EQ(prc_api_release_context(ctx), 0);

    PRC_TEST_END;
}
