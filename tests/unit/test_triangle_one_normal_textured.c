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

/* Uncompressed one-normal textured triangles are read, and so are faces that
   hold more than one kind of triangle.

   A PRC_TYPE_TESS_3D face can store triangles as
   PRC_FACETESSDATA_TriangleOneNormalTextured: one normal per triangle, then
   a texture index and a point index per corner, (normal, texture, point,
   texture, point, texture, point) (ISO 14739-1 7.8.5.2, Table 139). The
   reader used to refuse it with PRC_ERROR_NOT_IMPLEMENTED, so a file using
   it -- the CFD streamline tubes in a public corpus, whose end caps are
   stored this way -- was not drawn at all.

   A face's triangulateddata holds one count per kind of entity it uses, in
   Table 139 order (7.8.6.1). The reader took the triangle count from the
   first entry whatever the kind, and set the face's index total instead of
   adding to it, so the count of a later triangle kind was wrong.

   The writer produces neither, so the test writes ordinary uncompressed
   triangles and rewrites the parsed face in memory before reading it:

     A  three one-normal textured triangles
     B  two textured triangles, then one one-normal textured triangle
     C  two plain triangles, then one one-normal triangle (no texture)

   Each must read as three triangles, with the corners where they were
   written, and a one-normal triangle's three corners all taking its one
   normal. The original data is put back before it is released. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "prc_test.h"
#include "prc_context.h"
#include "prc_data.h"
#include "prc_api.h"

#define TEST_ONTX_FILENAME "test_triangle_one_normal_textured_tmp.prc"

/* A fan of three triangles in z = 0 */
static const double positions[5 * 3] = {
    0, 0, 0,   1, 0, 0,   1, 1, 0,   0, 1, 0,   -1, 1, 0
};
static const uint32_t tri_indices[3 * 3] = { 0, 1, 2,   0, 2, 3,   0, 3, 4 };
static const double normals[3 * 3] = { 0, 0, 1,   0, 0.6, 0.8,   0.6, 0, 0.8 };
/* Triangle k's corners use normal k, except the third triangle's, which use
   three different normals: as a one-normal triangle only its first, normal
   2, applies to all three corners. */
static const uint32_t norm_indices[3 * 3] = { 0, 0, 0,   1, 1, 1,   2, 0, 1 };
static const double tex_coords[5 * 2] = { 0, 0,   1, 0,   1, 1,   0, 1,   0.5, 0.5 };
static const uint32_t tex_indices[3 * 3] = { 0, 1, 2,   0, 2, 3,   0, 3, 4 };
static const uint32_t face_counts[1] = { 3 };
/* The normal each corner reads back with: the first two triangles' corners
   share one normal anyway, and the third triangle, one-normal in every case,
   takes its first. */
static const uint32_t want_normal[9] = { 0, 0, 0,   1, 1, 1,   2, 2, 2 };

static void
write_file(prc_context *ctx, int textured)
{
    prc_api_write_tessellation t;
    prc_api_write_rep_item ri;
    prc_api_write_node leaf, root;
    prc_api_write_node *kids[1];
    uint8_t *buf = NULL;
    size_t size = 0;
    FILE *f;

    memset(&t, 0, sizeof(t));
    t.kind = PRC_API_WRITE_TESS_KIND_TRIANGLES;
    t.positions = positions;
    t.num_positions = 5;
    t.normals = normals;
    t.num_normals = 3;
    t.norm_indices = norm_indices;
    t.tri_indices = tri_indices;
    t.num_triangles = 3;
    t.face_tri_counts = face_counts;
    t.num_faces = 1;
    if (textured)
    {
        t.tex_coords = tex_coords;
        t.num_tex_coords = 5;
        t.tex_indices = tex_indices;
    }
    memset(&ri, 0, sizeof(ri));
    ri.kind = PRC_API_WRITE_RI_SURFACE;
    ri.biased_tessellation_index = 1;
    memset(&leaf, 0, sizeof(leaf));
    leaf.rep_items = &ri;
    leaf.num_rep_items = 1;
    leaf.name = "fan";
    leaf.bbox_min[0] = -1.0;
    leaf.bbox_max[0] = leaf.bbox_max[1] = 1.0;
    kids[0] = &leaf;
    memset(&root, 0, sizeof(root));
    root.name = "root";
    root.children = kids;
    root.num_children = 1;
    root.bbox_min[0] = -1.0;
    root.bbox_max[0] = root.bbox_max[1] = 1.0;

    PRC_ASSERT_EQ(prc_api_write_prc_buffer(ctx, "one_normal", &root, &t, 1, &buf, &size), 0);
    f = fopen(TEST_ONTX_FILENAME, "wb");
    PRC_ASSERT_NOT_NULL(f);
    PRC_ASSERT_EQ(fwrite(buf, 1, size, f), size);
    fclose(f);
    prc_api_write_prc_buffer_free(ctx, buf);
}

/* Rewrite one triangle of the parsed multi-normal data at `src` (corner
   words: normal, [texture,] point) as a one-normal triangle at `dst`,
   dropping the second and third corners' normals. Returns the words
   written. */
static uint32_t
to_one_normal(const prc_unsigned_int *src, prc_unsigned_int *dst, uint32_t corner_words)
{
    uint32_t c, w, n = 0;

    for (c = 0; c < 3; c++)
        for (w = (c == 0) ? 0 : 1; w < corner_words; w++)
            dst[n++] = src[c * corner_words + w];
    return n;
}

static void
check_case(prc_context *ctx, const char *label, int textured, uint32_t num_one_normal,
    uint32_t want_flag, const uint32_t *expect_normal)
{
    prc_api_data data;
    prc_data *pd;
    prc_tess_3d *t3;
    prc_tess_face *face;
    prc_unsigned_int *saved_array, saved_count, saved_flag, saved_tdsize, *saved_td;
    prc_unsigned_int new_array[3 * 9], new_td[2];
    uint32_t corner_words = textured ? 3u : 2u;
    uint32_t num_multi = 3 - num_one_normal;
    uint32_t n = 0, k;
    prc_api_product *tree = NULL;
    prc_api_tess *tesses = NULL;
    prc_api_tess *t;
    uint32_t np = 0, npr = 0, nm = 0, nt = 0, nl = 0, ne = 0, nf;
    uint8_t has_lines = 0;
    const prc_api_tess_vertex_buffer *vb;
    prc_api_graphic_primitive prim;
    uint32_t corners = 0;
    size_t p;
    int code;

    printf("  sub-case %s\n", label);
    data = prc_api_open_contents(ctx, TEST_ONTX_FILENAME);
    if (data == NULL)
        prc_api_print_error_stack(ctx);
    PRC_ASSERT_NOT_NULL(data);

    /* The face as written: one kind of triangle, every corner its own normal */
    pd = (prc_data *)data;
    PRC_ASSERT(pd->file_struct[0].tessellation->tess_count >= 1);
    PRC_ASSERT_EQ(pd->file_struct[0].tessellation->tess[0].tess_type, PRC_TYPE_TESS_3D);
    t3 = pd->file_struct[0].tessellation->tess[0].tess_3d;
    PRC_ASSERT_EQ(t3->number_of_face_tessellation, 1u);
    face = &t3->face_tessellation_data[0];
    PRC_ASSERT_EQ(face->used_entities_flag,
        textured ? (prc_unsigned_int)PRC_FACETESSDATA_TriangleTextured : (prc_unsigned_int)PRC_FACETESSDATA_Triangle);
    PRC_ASSERT_EQ(face->size_of_triangulateddata, 1u);
    PRC_ASSERT_EQ(face->triangulateddata[0], 3u);
    PRC_ASSERT_EQ(face->start_triangulated, 0u);
    PRC_ASSERT_EQ(face->number_of_textured_coordinate_indexes, textured ? 1u : 0u);
    PRC_ASSERT_EQ(t3->number_of_triangulated_indicies, 3 * 3 * corner_words);

    /* Keep the first num_multi triangles as they are, and rewrite the rest
       as one-normal triangles */
    for (k = 0; k < num_multi; k++)
    {
        memcpy(&new_array[n], &t3->triangulated_index_array[k * 3 * corner_words],
            3 * corner_words * sizeof(prc_unsigned_int));
        n += 3 * corner_words;
    }
    for (k = num_multi; k < 3; k++)
        n += to_one_normal(&t3->triangulated_index_array[k * 3 * corner_words], &new_array[n], corner_words);
    new_td[0] = num_multi;
    new_td[1] = num_one_normal;

    saved_array = t3->triangulated_index_array;
    saved_count = t3->number_of_triangulated_indicies;
    saved_flag = face->used_entities_flag;
    saved_tdsize = face->size_of_triangulateddata;
    saved_td = face->triangulateddata;
    t3->triangulated_index_array = new_array;
    t3->number_of_triangulated_indicies = n;
    face->used_entities_flag = want_flag;
    if (num_multi > 0)
    {
        face->triangulateddata = new_td;
        face->size_of_triangulateddata = 2;
    }
    else
    {
        face->triangulateddata = &new_td[1];
        face->size_of_triangulateddata = 1;
    }

    PRC_ASSERT_EQ(prc_api_prep_model_tree(ctx, data, &np, &npr, &nm), 0);
    PRC_ASSERT_EQ(prc_api_create_model_tree(ctx, data, &tree, np, npr, nm), 0);
    PRC_ASSERT_EQ(prc_api_get_number_tessellations(ctx, data, tree, &nt, &nl, &ne, 0), 0);
    PRC_ASSERT_EQ(nt, 1u);
    tesses = (prc_api_tess *)calloc(1, sizeof(prc_api_tess));
    PRC_ASSERT_NOT_NULL(tesses);
    t = &tesses[0];
    nf = prc_api_get_number_faces(ctx, data, 0);
    PRC_ASSERT_EQ(nf, 1u);
    t->num_faces = nf;
    t->tess_faces = (prc_api_face *)calloc(nf, sizeof(prc_api_face));
    PRC_ASSERT_NOT_NULL(t->tess_faces);
    PRC_ASSERT_EQ(prc_api_initialize_tessellation(ctx, data, tree, 0, t, NULL, &has_lines), 0);
    PRC_ASSERT_EQ(t->type, PRC_API_TESS_3D);
    code = prc_api_get_tessellation_vertices(ctx, data, 0, 0, &t->tess_faces[0], t);
    if (code < 0)
        prc_api_print_error_stack(ctx);
    PRC_ASSERT_EQ(code, 0);
    vb = &t->tess_faces[0].face_vertices;

    /* Three triangles, in the order written, each corner at its position and
       with the normal it should take */
    for (p = 0; p < t->tess_faces[0].num_graphic_primitives; p++)
    {
        PRC_ASSERT_EQ(prc_api_get_graphics_primitive(ctx, data, t, 0, p, &prim), 0);
        PRC_ASSERT_EQ(prim.type, PRC_API_TRIANGLES);
        for (k = 0; k < (uint32_t)prim.num_indices; k++)
        {
            const prc_api_vertex *v;
            const double *wp, *wn;

            PRC_ASSERT(corners < 9);
            PRC_ASSERT(prim.indices[k] < vb->num_vertices);
            v = &vb->vertices[prim.indices[k]];
            wp = &positions[tri_indices[corners] * 3];
            wn = &normals[expect_normal[corners] * 3];
            PRC_ASSERT_NEAR(v->position[0], wp[0], 1e-6);
            PRC_ASSERT_NEAR(v->position[1], wp[1], 1e-6);
            PRC_ASSERT_NEAR(v->position[2], wp[2], 1e-6);
            PRC_ASSERT_NEAR(v->normal[0], wn[0], 1e-6);
            PRC_ASSERT_NEAR(v->normal[1], wn[1], 1e-6);
            PRC_ASSERT_NEAR(v->normal[2], wn[2], 1e-6);
            corners++;
        }
    }
    printf("    %u corners read, as written\n", corners);
    PRC_ASSERT_EQ(corners, 9u);

    /* Put the parsed data back, so that releasing it frees what was parsed */
    t3->triangulated_index_array = saved_array;
    t3->number_of_triangulated_indicies = saved_count;
    face->used_entities_flag = saved_flag;
    face->size_of_triangulateddata = saved_tdsize;
    face->triangulateddata = saved_td;

    prc_api_release_data(ctx, data, tesses, 1, NULL, 0, NULL, 0, tree);
    free(t->tess_faces);
    free(tesses);
}

int
main(void)
{
    prc_context *ctx;

    PRC_TEST_BEGIN("test_triangle_one_normal_textured");

    ctx = prc_api_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    write_file(ctx, 1);
    check_case(ctx, "A: three one-normal textured triangles", 1, 3,
        PRC_FACETESSDATA_TriangleOneNormalTextured, want_normal);
    check_case(ctx, "B: two textured triangles, then one one-normal textured", 1, 1,
        PRC_FACETESSDATA_TriangleTextured | PRC_FACETESSDATA_TriangleOneNormalTextured,
        want_normal);

    write_file(ctx, 0);
    check_case(ctx, "C: two triangles, then one one-normal triangle (no texture)", 0, 1,
        PRC_FACETESSDATA_Triangle | PRC_FACETESSDATA_TriangleOneNormal, want_normal);

    PRC_ASSERT_EQ(prc_api_release_context(ctx), 0);
    remove(TEST_ONTX_FILENAME);

    PRC_TEST_END;
}
