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

/* The write pipeline's ablation switches; see prc_write_ablate.h. */

#include <stdio.h>
#include <string.h>
#include "prc_write_ablate.h"
#include "prc_diag_env.h"

#define PRC_WRITE_ABLATION_CREASE_DEGREES 30.0

void
prc_write_ablation_get(prc_write_ablation *a)
{
    memset(a, 0, sizeof(*a));
    a->normals = prc_diag_getenv("PRC_DIAG_ABLATE_NORMALS") != NULL;
    a->colors = prc_diag_getenv("PRC_DIAG_ABLATE_COLORS") != NULL;
    a->textures = prc_diag_getenv("PRC_DIAG_ABLATE_TEXTURES") != NULL;
    a->tiny_textures = prc_diag_getenv("PRC_DIAG_ABLATE_TINY_TEXTURES") != NULL;
    a->huffman = prc_diag_getenv("PRC_DIAG_ABLATE_HUFFMAN") != NULL;
    a->huffman_where_optional = prc_diag_getenv("PRC_DIAG_HUFFMAN_WHERE_OPTIONAL") != NULL;
    a->deflate = prc_diag_getenv("PRC_DIAG_ABLATE_DEFLATE") != NULL;
    a->dump_dir = prc_diag_getenv("PRC_DIAG_ABLATE_DUMP_DIR");
    if (a->dump_dir != NULL && a->dump_dir[0] == '\0')
        a->dump_dir = NULL;
}

int
prc_write_ablation_any(const prc_write_ablation *a)
{
    return a->normals || a->colors || a->textures || a->tiny_textures ||
        a->huffman || a->huffman_where_optional || a->deflate || a->dump_dir != NULL;
}

void
prc_write_ablation_report(const prc_write_ablation *a)
{
    fprintf(stderr, "PRC_DIAG_ABLATE: writing with%s%s%s%s%s%s%s%s%s\n",
        a->normals ? " normals-off" : "",
        a->colors ? " colors-off" : "",
        a->textures ? " textures-off" : "",
        a->tiny_textures ? " tiny-textures" : "",
        a->huffman ? " huffman-flat" : "",
        a->huffman_where_optional ? " huffman-where-optional" : "",
        a->deflate ? " deflate-stored" : "",
        a->dump_dir != NULL ? " sections-dumped-to " : "",
        a->dump_dir != NULL ? a->dump_dir : "");
}

/* Does any switch change this entry? */
static int
prc_write_ablation_touches(const prc_write_ablation *a, const prc_api_write_tessellation *t)
{
    int surface = (t->kind != PRC_API_WRITE_TESS_KIND_WIRE);

    if (a->colors && t->vertex_colors != NULL)
        return 1;
    if (surface && a->textures && t->tex_coords != NULL)
        return 1;
    if (surface && a->normals && t->normals != NULL &&
        !(t->kind == PRC_API_WRITE_TESS_KIND_TRIANGLES && t->face_groups != NULL))
        return 1;
    return 0;
}

int
prc_write_ablation_apply_entries(prc_context *ctx, const prc_write_ablation *a,
    const prc_api_write_tessellation *entries, uint32_t num_entries,
    prc_write_ablated_entries *copy, const prc_api_write_tessellation **out)
{
    uint64_t num_face_groups = 0, num_groups = 0;
    uint32_t e, f, g, next_fg = 0, next_g = 0;
    int any = 0, kept_normals = 0;

    memset(copy, 0, sizeof(*copy));
    *out = entries;
    for (e = 0; e < num_entries; e++)
    {
        const prc_api_write_tessellation *t = &entries[e];

        if (a->normals && t->normals != NULL && t->kind == PRC_API_WRITE_TESS_KIND_TRIANGLES &&
            t->face_groups != NULL)
            kept_normals++;
        if (!prc_write_ablation_touches(a, t))
            continue;
        any = 1;
        /* Fans and strips carry their own texture indices, so dropping
           textures needs a copy of them too. */
        if (a->textures && t->tex_coords != NULL && t->face_groups != NULL)
        {
            num_face_groups += t->num_faces;
            for (f = 0; f < t->num_faces; f++)
                num_groups += (uint64_t)t->face_groups[f].num_fans + t->face_groups[f].num_strips;
        }
    }
    if (kept_normals > 0)
        fprintf(stderr, "PRC_DIAG_ABLATE: %d TRIANGLES entries with fans or strips keep their normals "
            "(must_calculate_normals is triangles-only)\n", kept_normals);
    if (!any || num_entries == 0)
        return 0;
    if (num_face_groups > 0xFFFFFFFFu || num_groups > 0xFFFFFFFFu)
        return PRC_ERROR_MEMORY;

    copy->entries = (prc_api_write_tessellation *)prc_malloc(ctx, (size_t)num_entries * sizeof(*copy->entries));
    if (num_face_groups > 0)
        copy->face_groups = (prc_api_write_face_groups *)prc_malloc(ctx,
            (size_t)num_face_groups * sizeof(*copy->face_groups));
    if (num_groups > 0)
        copy->groups = (prc_api_write_tri_group *)prc_malloc(ctx, (size_t)num_groups * sizeof(*copy->groups));
    if (copy->entries == NULL || (num_face_groups > 0 && copy->face_groups == NULL) ||
        (num_groups > 0 && copy->groups == NULL))
    {
        prc_write_ablation_free_entries(ctx, copy);
        prc_error(ctx, PRC_ERROR_MEMORY, "Allocation error in prc_write_ablation_apply_entries\n");
        return PRC_ERROR_MEMORY;
    }
    memcpy(copy->entries, entries, (size_t)num_entries * sizeof(*copy->entries));

    for (e = 0; e < num_entries; e++)
    {
        prc_api_write_tessellation *t = &copy->entries[e];
        int surface = (t->kind != PRC_API_WRITE_TESS_KIND_WIRE);

        if (a->colors && t->vertex_colors != NULL)
        {
            t->vertex_colors = NULL;
            t->num_vertex_colors = 0;
            t->vertex_colors_have_alpha = 0;
            t->vertex_colors_per_segment = 0;
        }

        if (surface && a->textures && t->tex_coords != NULL)
        {
            if (t->face_groups != NULL)
            {
                prc_api_write_face_groups *fg = &copy->face_groups[next_fg];

                for (f = 0; f < t->num_faces; f++)
                {
                    const prc_api_write_face_groups *src = &t->face_groups[f];
                    prc_api_write_tri_group *fans = &copy->groups[next_g];
                    prc_api_write_tri_group *strips;

                    for (g = 0; g < src->num_fans; g++)
                    {
                        copy->groups[next_g] = src->fans[g];
                        copy->groups[next_g].tex_indices = NULL;
                        next_g++;
                    }
                    strips = &copy->groups[next_g];
                    for (g = 0; g < src->num_strips; g++)
                    {
                        copy->groups[next_g] = src->strips[g];
                        copy->groups[next_g].tex_indices = NULL;
                        next_g++;
                    }
                    fg[f] = *src;
                    fg[f].fans = src->num_fans > 0 ? fans : NULL;
                    fg[f].strips = src->num_strips > 0 ? strips : NULL;
                }
                t->face_groups = fg;
                next_fg += t->num_faces;
            }
            t->tex_coords = NULL;
            t->num_tex_coords = 0;
            t->tex_indices = NULL;
        }

        if (surface && a->normals && t->normals != NULL)
        {
            if (!(t->kind == PRC_API_WRITE_TESS_KIND_TRIANGLES && t->face_groups != NULL))
            {
                t->normals = NULL;
                t->num_normals = 0;
                t->norm_indices = NULL;
                if (t->kind == PRC_API_WRITE_TESS_KIND_TRIANGLES)
                {
                    t->must_calculate_normals = 1;
                    if (t->crease_angle_degrees <= 0.0)
                        t->crease_angle_degrees = PRC_WRITE_ABLATION_CREASE_DEGREES;
                }
            }
        }
    }
    *out = copy->entries;
    return 0;
}

void
prc_write_ablation_free_entries(prc_context *ctx, prc_write_ablated_entries *copy)
{
    prc_free(ctx, copy->entries);
    prc_free(ctx, copy->face_groups);
    prc_free(ctx, copy->groups);
    memset(copy, 0, sizeof(*copy));
}

/* Red and green over blue and white: the three primaries, plus white for the
   fourth pixel, so a stand-in texture is plainly visible in a viewer. */
const uint8_t prc_write_ablation_tiny_rgb[PRC_WRITE_ABLATION_TINY_SIZE * PRC_WRITE_ABLATION_TINY_SIZE * 3] =
{
    0xFF, 0x00, 0x00,   0x00, 0xFF, 0x00,
    0x00, 0x00, 0xFF,   0xFF, 0xFF, 0xFF
};

void
prc_write_ablation_dump_section(const prc_write_ablation *a, uint32_t call, uint32_t index,
    const char *name, const uint8_t *buf, size_t len)
{
    char path[1024];
    FILE *f;
    int n;

    if (a->dump_dir == NULL)
        return;
    n = snprintf(path, sizeof(path), "%s/prc_%03u_%u_%s.bin", a->dump_dir, call, index, name);
    if (n < 0 || (size_t)n >= sizeof(path))
    {
        fprintf(stderr, "PRC_DIAG_ABLATE: dump path too long for %s\n", name);
        return;
    }
    f = fopen(path, "wb");
    if (f == NULL)
    {
        fprintf(stderr, "PRC_DIAG_ABLATE: cannot write %s\n", path);
        return;
    }
    if (len > 0 && fwrite(buf, 1, len, f) != len)
        fprintf(stderr, "PRC_DIAG_ABLATE: short write to %s\n", path);
    fclose(f);
}

uint32_t
prc_write_ablation_next_call(void)
{
    /* Not thread-safe; nor is the rest of this diagnostic. */
    static uint32_t calls = 0;

    return ++calls;
}
