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

/* A 3D stream's /VA is looked for in its dictionary only.

   /VA, the 3D stream's views, is an entry of the 3D stream dictionary
   (ISO 32000-1, 13.6.3 "3D Streams", Table 300 "Entries in a 3D stream
   dictionary"). The reader
   used to search from the start of the stream object to the end of the
   file, so when the dictionary had none it went on into the stream's
   compressed PRC bytes. Where those happened to hold "/VA" followed by
   anything other than an object reference, the file was refused ("Did not
   get view array"): a plain PDF wrapping the iPhone model in a public
   corpus could not be opened, while Adobe Acrobat opened it.

   Each PDF here is built by hand around a PRC file from the writer, with
   "/VA" placed in the stream after the PRC data:

     no /VA in the dictionary     opens, no views
     /VA [6 0 R] in the dictionary   opens, the one view is found */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "prc_test.h"
#include "prc_api.h"

#define TEST_VA_FILENAME "test_pdf_view_array_scope_tmp.pdf"

static const double tri_pos[9] = { 0, 0, 0,  1, 0, 0,  0, 1, 0 };
static const uint32_t tri_idx[3] = { 0, 1, 2 };
static const uint32_t tri_counts[1] = { 1 };

/* Bytes in the stream after the PRC data: "/VA" and then not an object
   reference, as in the compressed bytes that tripped the search. */
static const char stray[] = "/VA (not a reference)";

static void
write_prc(prc_context *ctx, uint8_t **buf, size_t *size)
{
    prc_api_write_tessellation t;
    prc_api_write_rep_item ri;
    prc_api_write_node leaf, root, *kids[1];

    memset(&t, 0, sizeof(t));
    t.kind = PRC_API_WRITE_TESS_KIND_TRIANGLES;
    t.positions = tri_pos;
    t.num_positions = 3;
    t.tri_indices = tri_idx;
    t.num_triangles = 1;
    t.face_tri_counts = tri_counts;
    t.num_faces = 1;
    t.must_calculate_normals = 1;

    memset(&ri, 0, sizeof(ri));
    ri.kind = PRC_API_WRITE_RI_SURFACE;
    ri.biased_tessellation_index = 1;

    memset(&leaf, 0, sizeof(leaf));
    leaf.rep_items = &ri;
    leaf.num_rep_items = 1;
    leaf.bbox_max[0] = leaf.bbox_max[1] = 1.0;
    kids[0] = &leaf;
    memset(&root, 0, sizeof(root));
    root.name = "root";
    root.children = kids;
    root.num_children = 1;
    root.bbox_max[0] = root.bbox_max[1] = 1.0;

    PRC_ASSERT_EQ(prc_api_write_prc_buffer(ctx, "view_array_scope", &root, &t, 1, buf, size), 0);
    PRC_ASSERT_NOT_NULL(*buf);
}

/* A one-page PDF: 1 catalog, 2 pages, 3 page, 4 3D annotation, 5 3D stream
   holding prc and then stray, and with_view adds /VA [6 0 R] to the stream
   dictionary and object 6, one view. */
static void
write_pdf(const uint8_t *prc, size_t prc_size, int with_view)
{
    FILE *f;
    long offsets[7];
    long xref;
    int n = with_view ? 6 : 5;
    int k;
    size_t stream_len = prc_size + strlen(stray);

    f = fopen(TEST_VA_FILENAME, "wb");
    PRC_ASSERT_NOT_NULL(f);
    fputs("%PDF-1.7\n", f);
    offsets[1] = ftell(f);
    fputs("1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n", f);
    offsets[2] = ftell(f);
    fputs("2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n", f);
    offsets[3] = ftell(f);
    fputs("3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 612] /Annots [4 0 R] >>\nendobj\n", f);
    offsets[4] = ftell(f);
    fputs("4 0 obj\n<< /Type /Annot /Subtype /3D /Rect [0 0 612 612] /3DD 5 0 R >>\nendobj\n", f);
    offsets[5] = ftell(f);
    fprintf(f, "5 0 obj\n<< /Type /3D /Subtype /PRC %s/Length %u >>\nstream\n",
        with_view ? "/VA [6 0 R] " : "", (unsigned)stream_len);
    PRC_ASSERT_EQ(fwrite(prc, 1, prc_size, f), prc_size);
    fputs(stray, f);
    fputs("\nendstream\nendobj\n", f);
    if (with_view)
    {
        offsets[6] = ftell(f);
        fputs("6 0 obj\n<< /Type /3DView /XN (Front) /MS /M "
              "/C2W [1 0 0 0 0 -1 0 1 0 0.5 -10 0.5] /CO 10 >>\nendobj\n", f);
    }
    xref = ftell(f);
    fprintf(f, "xref\n0 %d\n0000000000 65535 f \n", n + 1);
    for (k = 1; k <= n; k++)
        fprintf(f, "%010ld 00000 n \n", offsets[k]);
    fprintf(f, "trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n", n + 1, xref);
    fclose(f);
}

static void
run_case(prc_context *ctx, const uint8_t *prc, size_t prc_size, int with_view)
{
    prc_api_data data;

    printf("  sub-case: %s\n", with_view ? "/VA in the dictionary and in the stream"
                                         : "/VA in the stream only");
    write_pdf(prc, prc_size, with_view);
    data = prc_api_open_contents(ctx, TEST_VA_FILENAME);
    if (data == NULL)
        prc_api_print_error_stack(ctx);
    PRC_ASSERT_NOT_NULL(data);
    if (with_view)
    {
        char *name = NULL;
        double *m = NULL;
        double co = 0.0;

        PRC_ASSERT_EQ(prc_api_get_number_of_view(ctx, data), 1);
        PRC_ASSERT_EQ(prc_api_get_view(ctx, data, 0, &name, &m, &co), 0);
        PRC_ASSERT_NOT_NULL(name);
        PRC_ASSERT(strcmp(name, "Front") == 0);
    }
    else
    {
        PRC_ASSERT_EQ(prc_api_get_number_of_view(ctx, data), 0);
    }
    prc_api_release_data(ctx, data, NULL, 0, NULL, 0, NULL, 0, NULL);
    remove(TEST_VA_FILENAME);
}

int
main(void)
{
    prc_context *ctx;
    uint8_t *prc = NULL;
    size_t prc_size = 0;

    PRC_TEST_BEGIN("test_pdf_view_array_scope");

    ctx = prc_api_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    write_prc(ctx, &prc, &prc_size);
    run_case(ctx, prc, prc_size, 0);
    run_case(ctx, prc, prc_size, 1);
    prc_api_write_prc_buffer_free(ctx, prc);

    prc_api_release_context(ctx);

    PRC_TEST_END;
}
