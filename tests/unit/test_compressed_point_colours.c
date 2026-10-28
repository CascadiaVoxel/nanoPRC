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

/* Point colours of a compressed tessellation (Table 175, point_color_array)
   must land on the corners they belong to.

   The array holds one colour per corner, in the order the traversal produces
   the corners. The decoder stores a triangle with its last two corners
   swapped when its normal is not reversed, so the colours have to be swapped
   with them. Measured on prc-db (October 2026): with colours in traversal
   order, corners that share a vertex agree on 100% of 72,593 in a VRMesh
   file and 99.95% of 2,481,427 in a CloudCompare file; read in the decoder's
   stored order, about a quarter agree.

   The fixture is a unit cube written by nanoPRC's own encoder with
   recalculated normals, which gives triangles of both kinds. The encoder
   reports, for each triangle in traversal order, the decoder point each
   corner decodes into (triangle_point_indices). The colours are set from
   that, one per corner, as a function of the point alone, and the tessellation
   is decoded again. Every decoded corner must then carry its own point's
   colour, whichever way its triangle was stored. */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "prc_test.h"
#include "prc_context.h"
#include "prc_write_compress_tess.h"
#include "prc_parse_tess.h"
#include "prc_parse_common.h"
#include "prc_decode_compressed_tess.h"

/* A colour no two of the cube's 8 points share. */
static void
point_colour(uint32_t p, float rgba[4])
{
    rgba[0] = (float)(p + 1) / 32.0f;
    rgba[1] = (float)(p % 4) / 4.0f;
    rgba[2] = (float)(p / 4) / 4.0f;
    rgba[3] = 1.0f;
}

#define FREE_FIELD(ctx, f) do { if ((f) != NULL) prc_free((ctx), (f)); (f) = NULL; } while (0)

/* Frees what prc_decode_compressed_tess and edge extraction allocated, so
   the decoder can run again. */
static void
free_decoded(prc_context *ctx, prc_tess_3d_compressed *d)
{
    FREE_FIELD(ctx, d->triangle_indices_prc_compressed_3d);
    FREE_FIELD(ctx, d->normal_indices_prc_compressed_3d);
    FREE_FIELD(ctx, d->uv_indices_prc_compressed_3d);
    FREE_FIELD(ctx, d->point_colors_prc_compressed_3d);
    FREE_FIELD(ctx, d->vertices_prc_compressed_3d);
    FREE_FIELD(ctx, d->normals_prc_compressed_3d);
    FREE_FIELD(ctx, d->triangle_styles);
    FREE_FIELD(ctx, d->edge_vertices);
    FREE_FIELD(ctx, d->edge_indices);
}

/* Frees everything prc_parse_tess_3d_compressed allocated for this fixture. */
static void
free_parsed(prc_context *ctx, prc_tess_3d_compressed *d)
{
    free_decoded(ctx, d);
    FREE_FIELD(ctx, d->point_array);
    FREE_FIELD(ctx, d->edge_status_array);
    FREE_FIELD(ctx, d->triangle_face_array);
    FREE_FIELD(ctx, d->points_is_reference_array);
    FREE_FIELD(ctx, d->point_reference_array);
    FREE_FIELD(ctx, d->normal_is_reversed);
    FREE_FIELD(ctx, d->normal_binary_data);
    FREE_FIELD(ctx, d->normal_angle_array);
    FREE_FIELD(ctx, d->normal_angle_array_scaled);
    FREE_FIELD(ctx, d->is_face_planar);
    FREE_FIELD(ctx, d->is_point_color_on_face);
    FREE_FIELD(ctx, d->point_color_array);
    FREE_FIELD(ctx, d->decoded_point_color_array);
    FREE_FIELD(ctx, d->is_multiple_line_attribute_on_face);
    FREE_FIELD(ctx, d->line_attribute_array);
    FREE_FIELD(ctx, d->face_has_texture);
    FREE_FIELD(ctx, d->uv_coordinates_3d);
    prc_free(ctx, d);
}

/* Unit cube, corner i at (i & 1, (i >> 1) & 1, (i >> 2) & 1), each face
   counter-clockwise seen from outside except the first, which is wound the
   other way. On a consistently wound mesh nanoPRC's encoder gives every
   triangle the same reversed bit; the odd face gives the decoder triangles
   of both kinds. */
static const uint32_t CUBE_QUADS[6][4] = {
    { 0, 1, 3, 2 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 },
    { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 }
};

static void
test_cube_colours(prc_context *ctx)
{
    double positions[8 * 3];
    uint32_t tris[12 * 3];
    double corner_normals[12 * 9];
    prc_encode_mesh mesh;
    prc_encode_traversal_result res;
    prc_tess_3d_compressed *parsed = NULL;
    uint8_t *rev = NULL;
    prc_bit_write_state w;
    prc_bit_state r;
    uint32_t x, y, n = 0, t, c, num_tris;
    uint32_t stored_swapped = 0, stored_straight = 0;
    int code;

    printf("  sub-case: cube, colours follow their corners through the swap\n");
    for (x = 0; x < 8; x++)
    {
        positions[x * 3 + 0] = (double)(x & 1);
        positions[x * 3 + 1] = (double)((x >> 1) & 1);
        positions[x * 3 + 2] = (double)((x >> 2) & 1);
    }
    for (y = 0; y < 6; y++)
    {
        const uint32_t *q = CUBE_QUADS[y];
        tris[n * 3 + 0] = q[0]; tris[n * 3 + 1] = q[1]; tris[n * 3 + 2] = q[2]; n++;
        tris[n * 3 + 0] = q[0]; tris[n * 3 + 1] = q[2]; tris[n * 3 + 2] = q[3]; n++;
    }

    PRC_ASSERT_EQ(prc_encode_preprocess(ctx, positions, 8, tris, n,
        prc_write_tol_absolute(1e-4), &mesh), 0);

    /* Each corner gets its face's outward normal, and the traversal decides
       every triangle's reversed bit from it geometrically. The first face's
       triangles are wound against their normal, so their bit differs. */
    for (t = 0; t < mesh.num_triangles; t++)
    {
        double centre[3] = { 0, 0, 0 }, outward[3];
        uint32_t axis, best = 0;

        for (c = 0; c < 3; c++)
            for (x = 0; x < 3; x++)
                centre[x] += mesh.positions[(size_t)mesh.tri_indices[t * 3 + c] * 3 + x] / 3.0;
        /* The face's axis is the one where the centre sits on 0 or 1. */
        for (axis = 0; axis < 3; axis++)
            if (fabs(centre[axis] - 0.5) > fabs(centre[best] - 0.5))
                best = axis;
        outward[0] = outward[1] = outward[2] = 0.0;
        outward[best] = centre[best] > 0.5 ? 1.0 : -1.0;
        for (c = 0; c < 3; c++)
            memcpy(&corner_normals[(t * 3 + c) * 3], outward, sizeof(outward));
    }
    PRC_ASSERT_EQ(prc_encode_traversal(ctx, &mesh, NULL, mesh.tolerance_mm, &res, NULL, NULL,
        corner_normals, 0), 0);
    rev = res.triangle_reversed;

    PRC_ASSERT_EQ(prc_bitwrite_init(ctx, &w, 256), 0);
    PRC_ASSERT_EQ(prc_write_compress_tess_to_stream(ctx, &w, &res, mesh.tolerance_mm,
        rev, 30.0, NULL, 0, NULL, 0, 1, NULL), 0);
    PRC_ASSERT_EQ(prc_bitwrite_flush(ctx, &w), 0);

    prc_init_bit_state(ctx, &r, w.buf, w.byte_pos);
    code = prc_parse_tess_3d_compressed(ctx, &r, &parsed, 0);
    if (code < 0)
        prc_print_error_stack(ctx);
    PRC_ASSERT_EQ(code, 0);
    PRC_ASSERT_NOT_NULL(parsed);
    num_tris = parsed->triangle_face_array_size;
    PRC_ASSERT_EQ(num_tris, n);
    PRC_ASSERT_EQ(res.triangle_face_array_size, n);

    /* The fixture must have triangles stored both ways, or it tests nothing. */
    for (t = 0; t < num_tris; t++)
    {
        if (parsed->normal_is_reversed[t])
            stored_straight++;
        else
            stored_swapped++;
    }
    printf("    %u triangles stored swapped, %u as read\n", stored_swapped, stored_straight);
    PRC_ASSERT(stored_swapped > 0);
    PRC_ASSERT(stored_straight > 0);

    /* One colour per corner, in traversal order, as the file would hold them. */
    parsed->decoded_point_color_array_size = num_tris * 12;
    parsed->decoded_point_color_array = (float *)prc_calloc(ctx, (size_t)num_tris * 12, sizeof(float));
    PRC_ASSERT_NOT_NULL(parsed->decoded_point_color_array);
    for (t = 0; t < num_tris; t++)
        for (c = 0; c < 3; c++)
            point_colour((uint32_t)res.triangle_point_indices[t * 3 + c],
                &parsed->decoded_point_color_array[(t * 3 + c) * 4]);

    free_decoded(ctx, parsed);
    code = prc_decode_compressed_tess(ctx, parsed, 0);
    if (code < 0)
        prc_print_error_stack(ctx);
    PRC_ASSERT_EQ(code, 0);
    PRC_ASSERT_NOT_NULL(parsed->point_colors_prc_compressed_3d);
    PRC_ASSERT_EQ(parsed->num_triangle_indices_prc_compressed_3d, num_tris * 3);

    for (t = 0; t < num_tris * 3; t++)
    {
        float want[4];
        const float *got = &parsed->point_colors_prc_compressed_3d[t * 4];

        point_colour(parsed->triangle_indices_prc_compressed_3d[t], want);
        for (c = 0; c < 4; c++)
        {
            if (fabs(got[c] - want[c]) > 1e-6)
                printf("    corner %u (triangle %u, stored %s): colour of the wrong point\n",
                    t, t / 3, parsed->normal_is_reversed[t / 3] ? "as read" : "swapped");
            PRC_ASSERT(fabs(got[c] - want[c]) <= 1e-6);
        }
    }

    /* Edge extraction compares the colours either side of every edge; with a
       colour per point, none of them differ. */
    PRC_ASSERT_EQ(prc_compute_edges_compressed_tess(ctx, parsed), 0);

    free_parsed(ctx, parsed);

    prc_bitwrite_release(ctx, &w);
    prc_encode_traversal_free(ctx, &res);
    prc_encode_preprocess_free(ctx, &mesh);
}

int
main(void)
{
    prc_context *ctx;

    PRC_TEST_BEGIN("compressed tessellation point colours");

    ctx = prc_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    test_cube_colours(ctx);

    prc_release_context(ctx);

    PRC_TEST_END;
}
