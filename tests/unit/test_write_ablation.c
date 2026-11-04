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

/* The write pipeline's ablation switches (src/prc_write_ablate.h). One
   compressed and one TRIANGLES mesh, each with normals, a colour per corner,
   texture coordinates and a textured material, are written through the
   public API and read back. tests/CMakeLists.txt runs this once with no
   switch set and, in a build with PRC_ENABLE_DIAG_ENV, again under several
   sets of them; each run checks what its switches must change and what they
   must leave alone, and that the file still reads. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "prc_test.h"
#include "prc_context.h"
#include "prc_data.h"
#include "prc_api.h"
#include "prc_write_ablate.h"
#include "prc_bit.h"

static const char *PRC_PATH = "test_write_ablation_output.prc";

/* A cube: 8 corners, 12 triangles, one normal per face. */
static const double POS[8 * 3] = {
    0, 0, 0,  1, 0, 0,  1, 1, 0,  0, 1, 0,
    0, 0, 1,  1, 0, 1,  1, 1, 1,  0, 1, 1
};
static const uint32_t TRI[12 * 3] = {
    0, 2, 1,  0, 3, 2,      4, 5, 6,  4, 6, 7,
    0, 1, 5,  0, 5, 4,      3, 7, 6,  3, 6, 2,
    0, 4, 7,  0, 7, 3,      1, 2, 6,  1, 6, 5
};
static const double NRM[6 * 3] = {
    0, 0, -1,  0, 0, 1,  0, -1, 0,  0, 1, 0,  -1, 0, 0,  1, 0, 0
};
static uint32_t NRM_IDX[12 * 3];
static const double UV[4 * 2] = { 0, 0,  1, 0,  1, 1,  0, 1 };
static uint32_t UV_IDX[12 * 3];
static const uint32_t UV_PATTERN[6] = { 0, 1, 2, 0, 2, 3 };
static uint8_t COLORS[12 * 3 * 3];

/* 32 x 32 RGB, every pixel different from its neighbours. */
#define IMAGE_SIZE 32u
static uint8_t IMAGE[IMAGE_SIZE * IMAGE_SIZE * 3];

static int
env(const char *name)
{
    return getenv(name) != NULL;
}

static void
fill_inputs(void)
{
    uint32_t k;

    for (k = 0; k < 12 * 3; k++)
    {
        NRM_IDX[k] = (k / 3) / 2;          /* two triangles per cube face */
        UV_IDX[k] = UV_PATTERN[k % 6];     /* each quad: (0,1,2) and (0,2,3) */
        COLORS[k * 3 + 0] = (uint8_t)(k * 7);
        COLORS[k * 3 + 1] = (uint8_t)(255 - k * 5);
        COLORS[k * 3 + 2] = (uint8_t)(k * 11);
    }
    for (k = 0; k < sizeof(IMAGE); k++)
        IMAGE[k] = (uint8_t)(k * 37 + (k >> 5));
}

static void
set_mesh(prc_api_write_tessellation *t, prc_api_write_tess_kind_t kind, const uint32_t *face_count)
{
    memset(t, 0, sizeof(*t));
    t->kind = kind;
    t->positions = POS;
    t->num_positions = 8;
    t->normals = NRM;
    t->num_normals = 6;
    t->tri_indices = TRI;
    t->norm_indices = NRM_IDX;
    t->num_triangles = 12;
    t->face_tri_counts = face_count;
    t->num_faces = 1;
    t->tex_coords = UV;
    t->num_tex_coords = 4;
    t->tex_indices = UV_IDX;
    t->vertex_colors = COLORS;
    t->num_vertex_colors = 12 * 3;
}

static void
set_item(prc_api_write_rep_item *ri, uint32_t biased_tess)
{
    memset(ri, 0, sizeof(*ri));
    ri->kind = PRC_API_WRITE_RI_SURFACE;
    ri->biased_tessellation_index = biased_tess;
    ri->has_material = 1;
    ri->material_color[0] = ri->material_color[1] = ri->material_color[2] = 1.0;
    ri->material_alpha = 1.0;
    ri->has_texture = 1;
    ri->texture_image = IMAGE;
    ri->texture_image_size = sizeof(IMAGE);
    ri->texture_format = PRC_API_WRITE_TEXTURE_RGB;
    ri->texture_width = IMAGE_SIZE;
    ri->texture_height = IMAGE_SIZE;
}

static long
file_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;

    if (f == NULL)
        return -1;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fclose(f);
    return n;
}

/* A bit-length table whose compression the writer may choose
   (has_comp_bit set), with a negative step: references 0, 9 and 14 need 2, 6
   and 5 bits, so the deltas after the first are +4 and -1. By default the
   table is written plain, one 8-bit character per value, and a negative delta
   must then be a whole signed char (-1 = 0xFF), as in the short reference
   arrays (test_bitwrite's test_short_indice_array_signed_char): Adobe Acrobat
   reads the 6-bit pattern (-1 = 0x3F) as +63 and fails on the file. With
   PRC_DIAG_HUFFMAN_WHERE_OPTIONAL, and without PRC_DIAG_ABLATE_HUFFMAN, the
   table is Huffman-coded. Either way it must read back. */
static void
test_indice_array_plain_sign(prc_context *ctx, int huffman_off)
{
    static const int32_t data[3] = { 0, 9, 14 };
    prc_bit_write_state w;
    prc_bit_state r;
    int32_t *out;
    uint32_t out_size = 0, i;

    printf("  sub-case: a flagged bit-length table with a negative step is %s\n",
        huffman_off ? "written plain, -1 as 0xFF" : "Huffman-coded");
    PRC_ASSERT_EQ(prc_bitwrite_init(ctx, &w, 16), 0);
    PRC_ASSERT_EQ(prc_bitwrite_compressed_indice_array(ctx, &w, data, 3, 1, 0), 0);
    PRC_ASSERT_EQ(w.error, 0);
    PRC_ASSERT_EQ(prc_bitwrite_flush(ctx, &w), 0);

    prc_init_bit_state(ctx, &r, w.buf, w.byte_pos);
    PRC_ASSERT_EQ(prc_bitread_bit(ctx, &r), huffman_off ? 0 : 1);
    if (huffman_off)
    {
        PRC_ASSERT_EQ(prc_bitread_uint32(ctx, &r), 3);
        PRC_ASSERT_EQ(prc_bitread_uint8(ctx, &r), 2);
        PRC_ASSERT_EQ(prc_bitread_uint8(ctx, &r), 4);
        PRC_ASSERT_EQ(prc_bitread_uint8(ctx, &r), 0xFF);
    }

    prc_init_bit_state(ctx, &r, w.buf, w.byte_pos);
    out = prc_bitread_compressed_indice_array(ctx, &r, &out_size, 1, 0);
    PRC_ASSERT_NOT_NULL(out);
    PRC_ASSERT_EQ(out_size, 3);
    for (i = 0; i < 3; i++)
        PRC_ASSERT_EQ(out[i], data[i]);
    prc_free(ctx, out);
    prc_bitwrite_release(ctx, &w);
}

int
main(void)
{
    static const uint32_t face_count = 12;
    prc_api_write_tessellation tess[2];
    prc_api_write_rep_item items[2];
    prc_api_write_node root;
    prc_context *ctx;
    prc_data *pd;
    prc_tess_3d_compressed *c;
    prc_tess_3d *t;
    const prc_file_struct_internal_global_data *gd;
    int normals_off = env("PRC_DIAG_ABLATE_NORMALS");
    int colors_off = env("PRC_DIAG_ABLATE_COLORS");
    int textures_off = env("PRC_DIAG_ABLATE_TEXTURES");
    int tiny = env("PRC_DIAG_ABLATE_TINY_TEXTURES");
    int deflate_off = env("PRC_DIAG_ABLATE_DEFLATE");
    const char *dump_dir = getenv("PRC_DIAG_ABLATE_DUMP_DIR");
    uint32_t k;

    PRC_TEST_BEGIN("test_write_ablation");
    printf("  switches:%s%s%s%s%s%s%s%s\n", normals_off ? " normals" : "", colors_off ? " colors" : "",
        textures_off ? " textures" : "", tiny ? " tiny-textures" : "",
        env("PRC_DIAG_ABLATE_HUFFMAN") ? " huffman" : "",
        env("PRC_DIAG_HUFFMAN_WHERE_OPTIONAL") ? " huffman-where-optional" : "", deflate_off ? " deflate" : "",
        dump_dir != NULL ? " dump" : "");

    fill_inputs();
    set_mesh(&tess[0], PRC_API_WRITE_TESS_KIND_COMPRESSED, &face_count);
    set_mesh(&tess[1], PRC_API_WRITE_TESS_KIND_TRIANGLES, &face_count);
    set_item(&items[0], 1);
    set_item(&items[1], 2);
    memset(&root, 0, sizeof(root));
    root.rep_items = items;
    root.num_rep_items = 2;
    root.bbox_max[0] = root.bbox_max[1] = root.bbox_max[2] = 1.0;

    ctx = prc_api_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);
    PRC_ASSERT_EQ(prc_api_write_prc_file(ctx, PRC_PATH, "ablation", &root, tess, 2), 0);

    printf("  sub-case: the caller's arrays are untouched\n");
    PRC_ASSERT(tess[0].normals == NRM && tess[1].tex_coords == UV && tess[0].vertex_colors == COLORS);

    printf("  sub-case: the file reads back, with every triangle\n");
    pd = (prc_data *)prc_api_open_contents(ctx, PRC_PATH);
    if (pd == NULL)
        prc_print_error_stack(ctx);
    PRC_ASSERT_NOT_NULL(pd);
    PRC_ASSERT_EQ(pd->file_struct[0].tessellation->tess_count, 2);
    c = pd->file_struct[0].tessellation->tess[0].tess_3d_compressed;
    t = pd->file_struct[0].tessellation->tess[1].tess_3d;
    PRC_ASSERT_NOT_NULL(c);
    PRC_ASSERT_NOT_NULL(t);
    PRC_ASSERT_EQ(c->triangle_face_array_size, 12);
    PRC_ASSERT_EQ(t->number_of_triangulated_indicies > 0, 1);

    printf("  sub-case: normals %s\n", normals_off ? "recalculated" : "as supplied");
    PRC_ASSERT_EQ(t->must_calculate_normals, normals_off ? 1 : 0);
    PRC_ASSERT_EQ(t->number_of_normal_coordinates == 0, normals_off ? 1 : 0);
    PRC_ASSERT_EQ(c->must_recalculate_normals, normals_off ? 1 : 0);

    printf("  sub-case: vertex colours %s\n", colors_off ? "gone" : "kept");
    PRC_ASSERT_EQ(c->is_point_color, colors_off ? 0 : 1);

    printf("  sub-case: texture coordinates %s\n", textures_off ? "gone" : "kept");
    PRC_ASSERT_EQ(c->no_texture, textures_off ? 1 : 0);
    PRC_ASSERT_EQ(t->number_of_texture_coordinates == 0, textures_off ? 1 : 0);

    gd = &pd->file_struct[0].globals->global_data;
    if (textures_off)
    {
        printf("  sub-case: no texture or picture is written\n");
        PRC_ASSERT_EQ(gd->texture_count, 0);
        PRC_ASSERT_EQ(gd->picture_count, 0);
    }
    else
    {
        uint32_t want = tiny ? PRC_WRITE_ABLATION_TINY_SIZE : IMAGE_SIZE;

        printf("  sub-case: every picture is %u x %u\n", want, want);
        PRC_ASSERT(gd->picture_count >= 1);
        for (k = 0; k < gd->picture_count; k++)
        {
            PRC_ASSERT_EQ(gd->pictures[k].pixel_width, want);
            PRC_ASSERT_EQ(gd->pictures[k].pixel_height, want);
        }
    }
    prc_api_release_data(ctx, (prc_api_data)pd, NULL, 0, NULL, 0, NULL, 0, NULL);

    if (dump_dir != NULL)
    {
        static const char *names[6] = { "schema_globals", "tree", "tessellation", "geometry",
            "extra_geometry", "model" };
        char path[1024];
        long sum = 0;

        printf("  sub-case: all six sections are dumped\n");
        for (k = 0; k < 6; k++)
        {
            long n;

            snprintf(path, sizeof(path), "%s/prc_001_%u_%s.bin", dump_dir, k + 1, names[k]);
            n = file_size(path);
            PRC_ASSERT(n > 0);
            sum += n;
            remove(path);
        }
        if (deflate_off)
        {
            printf("  sub-case: stored deflate makes the file larger than its sections\n");
            PRC_ASSERT(file_size(PRC_PATH) > sum);
        }
    }

    test_indice_array_plain_sign(ctx,
        !env("PRC_DIAG_HUFFMAN_WHERE_OPTIONAL") || env("PRC_DIAG_ABLATE_HUFFMAN"));

    remove(PRC_PATH);
    PRC_ASSERT_EQ(prc_api_release_context(ctx), 0);
    PRC_TEST_END;
}
