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

/* The two version numbers the writer declares (pdf-association/pdf-issues
   #805): minimal_version_for_read 8137, what a reader needs and the highest a
   deployed reader accepts, and authoring_version 10001, the edition of the
   document the writer worked to. Both go into the main header and into the
   file-structure header, and a file that carries them reads back. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "prc_test.h"
#include "prc_api.h"
#include "prc_context.h"
#include "prc_data.h"

#define MIN_VERSION_FOR_READ 8137u
#define AUTHORING_VERSION 10001u

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
    return prc_api_write_prc_buffer(ctx, "versions", &root, &t, 1, buf, size);
}

static void
put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

/* Every place "PRC", the minimum version and the authoring version occur in
   that order: the main header and the file-structure header. */
static uint32_t
count_version_pair(const uint8_t *buf, size_t size)
{
    uint8_t pattern[11];
    uint32_t n = 0;
    size_t i;

    memcpy(pattern, "PRC", 3);
    put_le32(pattern + 3, MIN_VERSION_FOR_READ);
    put_le32(pattern + 7, AUTHORING_VERSION);
    for (i = 0; i + sizeof(pattern) <= size; i++)
        if (memcmp(buf + i, pattern, sizeof(pattern)) == 0)
            n++;
    return n;
}

int
main(void)
{
    static const char *PRC_PATH = "test_written_versions.prc";
    prc_context *ctx;
    prc_api_data data;
    const prc_data *d;
    uint8_t *buf = NULL;
    size_t size = 0;
    FILE *f;

    PRC_TEST_BEGIN("test_written_versions");

    ctx = prc_api_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);
    PRC_ASSERT_EQ(write_triangle(ctx, &buf, &size), 0);

    printf("  sub-case: the main header and the file-structure header both declare 8137 and 10001\n");
    PRC_ASSERT(size > 11);
    PRC_ASSERT(memcmp(buf, "PRC", 3) == 0);
    PRC_ASSERT_EQ(count_version_pair(buf, size), 2);

    f = fopen(PRC_PATH, "wb");
    PRC_ASSERT_NOT_NULL(f);
    PRC_ASSERT_EQ(fwrite(buf, 1, size, f), size);
    fclose(f);
    prc_api_write_prc_buffer_free(ctx, buf);

    printf("  sub-case: the file reads back, with the same two numbers in both headers\n");
    data = prc_api_open_contents(ctx, PRC_PATH);
    PRC_ASSERT_NOT_NULL(data);
    d = (const prc_data *)data;
    PRC_ASSERT_NOT_NULL(d->header);
    PRC_ASSERT_EQ(d->header->min_vers_for_read, MIN_VERSION_FOR_READ);
    PRC_ASSERT_EQ(d->header->auth_vers, AUTHORING_VERSION);
    PRC_ASSERT_EQ(d->header->filestructure_count, 1);
    PRC_ASSERT_NOT_NULL(d->file_struct);
    PRC_ASSERT_NOT_NULL(d->file_struct[0].header);
    PRC_ASSERT_EQ(d->file_struct[0].header->min_vers_for_read, MIN_VERSION_FOR_READ);
    PRC_ASSERT_EQ(d->file_struct[0].header->auth_vers, AUTHORING_VERSION);

    prc_api_release_data(ctx, data, NULL, 0, NULL, 0, NULL, 0, NULL);

    remove(PRC_PATH);
    PRC_ASSERT_EQ(prc_api_release_context(ctx), 0);

    PRC_TEST_END;
}
