/* Copyright (C) 2026 Ian Curington, Thames 3D Research

    prc_obj_rw is free software: you can redistribute it and/or modify it
    under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or (at
    your option) any later version.

    prc_obj_rw is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
    General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with prc_obj_rw. If not, see <https://www.gnu.org/licenses/>.
*/

/* ====================================================================
 * obj_import: read a Wavefront OBJ (and its MTL) and write it as a .prc
 * file and a minimal 3D-annotated .pdf, through nanoPRC's public write
 * API only (include/prc_api.h).
 *
 * PROTOTYPE. Parsing is done by fast_obj. What this file adds:
 *
 *   - polygons of more than three vertices are triangulated by ear
 *     clipping, which is correct for non-convex polygons where a fan is not;
 *   - the mesh is split into parts, one per OBJ object (or group, when the
 *     file has no objects), and each part into one tessellation per
 *     material, since a PRC representation item carries one material;
 *   - each MTL material becomes the item's colour, opacity and shininess,
 *     and a map_Kd image in PNG or JPEG becomes its diffuse texture.
 *
 * Geometry goes out as compressed tessellation by default. Two kinds of
 * part are written as plain triangles instead: a textured part, because the
 * compressed encoder has no texture path, and a part with a vertex shared
 * by disconnected triangle fans, which some Acrobat builds misread when
 * compressed. --triangles writes everything that way.
 *
 * Each part is also moved by its own tiny offset (--jitter, default 1e-4 of
 * the model diagonal): some layouts of separate parts, such as parts along
 * one axis, otherwise give an empty model tree in Acrobat.
 *
 * Usage:
 *     nano_prc_obj_import <input.obj> [output.prc] [output.pdf]
 *                         [--triangles] [--no-normals] [--no-materials]
 *                         [--jitter F]
 *
 * Output paths default to the input's name with .obj replaced.
 *
 * Not yet done: instancing of repeated parts, texture de-duplication,
 * units, map_d and emissive colour.
 * ==================================================================== */

#include <prc_api.h>
#include <fast_obj.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#define OBJ_NO_INDEX 0xFFFFFFFFu

/* -------------------------------------------------------------------- */
/* Small growable array of uint32_t                                      */
/* -------------------------------------------------------------------- */

typedef struct
{
    uint32_t *data;
    uint32_t count;
    uint32_t capacity;
} u32_array;

static int
u32_push(u32_array *a, uint32_t v)
{
    if (a->count == a->capacity)
    {
        uint32_t cap = a->capacity ? a->capacity * 2 : 64;
        uint32_t *grown = (uint32_t *)realloc(a->data, (size_t)cap * sizeof(uint32_t));

        if (grown == NULL)
            return -1;
        a->data = grown;
        a->capacity = cap;
    }
    a->data[a->count++] = v;
    return 0;
}

/* -------------------------------------------------------------------- */
/* Triangulation                                                         */
/* -------------------------------------------------------------------- */

/* Twice the signed area of the 2D triangle (a, b, c). */
static double
cross2(const double *a, const double *b, const double *c)
{
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
}

/* Is p inside or on triangle (a, b, c), which has orientation sign `s`? */
static int
point_in_triangle(const double *p, const double *a, const double *b,
    const double *c, double s)
{
    return cross2(a, b, p) * s >= 0.0 &&
           cross2(b, c, p) * s >= 0.0 &&
           cross2(c, a, p) * s >= 0.0;
}

/* Triangulate one polygon of `n` corners. `corner` holds the corners'
   positions in polygon order. Triangles are appended to `out` as triples of
   corner numbers 0..n-1, in the polygon's own winding.

   Ear clipping in the plane the polygon most nearly lies in, chosen from
   its Newell normal. If no ear can be found -- a degenerate or
   self-intersecting polygon -- the remaining corners are fanned, so every
   polygon still produces n-2 triangles. */
static int
triangulate_polygon(const double (*corner)[3], uint32_t n, u32_array *out,
    double (*proj)[2], uint32_t *ring)
{
    double nx = 0.0, ny = 0.0, nz = 0.0, s;
    int ax, ay;
    uint32_t i, m;

    if (n == 3)
        return u32_push(out, 0) || u32_push(out, 1) || u32_push(out, 2);

    for (i = 0; i < n; i++)
    {
        const double *p = corner[i];
        const double *q = corner[(i + 1) % n];

        nx += (p[1] - q[1]) * (p[2] + q[2]);
        ny += (p[2] - q[2]) * (p[0] + q[0]);
        nz += (p[0] - q[0]) * (p[1] + q[1]);
    }

    /* Drop the axis the normal is largest along. */
    if (fabs(nx) >= fabs(ny) && fabs(nx) >= fabs(nz))
    {
        ax = 1; ay = 2; s = nx;
    }
    else if (fabs(ny) >= fabs(nz))
    {
        ax = 2; ay = 0; s = ny;
    }
    else
    {
        ax = 0; ay = 1; s = nz;
    }
    s = (s >= 0.0) ? 1.0 : -1.0;

    for (i = 0; i < n; i++)
    {
        proj[i][0] = corner[i][ax];
        proj[i][1] = corner[i][ay];
        ring[i] = i;
    }

    m = n;
    while (m > 3)
    {
        uint32_t k, found = 0;

        for (k = 0; k < m && !found; k++)
        {
            uint32_t ia = ring[(k + m - 1) % m];
            uint32_t ib = ring[k];
            uint32_t ic = ring[(k + 1) % m];
            uint32_t j;
            int blocked = 0;

            /* Convex corner in the polygon's own orientation. */
            if (cross2(proj[ia], proj[ib], proj[ic]) * s <= 0.0)
                continue;

            for (j = 0; j < m && !blocked; j++)
            {
                uint32_t iv = ring[j];

                if (iv == ia || iv == ib || iv == ic)
                    continue;
                if (point_in_triangle(proj[iv], proj[ia], proj[ib], proj[ic], s))
                    blocked = 1;
            }
            if (blocked)
                continue;

            if (u32_push(out, ia) || u32_push(out, ib) || u32_push(out, ic))
                return -1;
            memmove(&ring[k], &ring[k + 1], (size_t)(m - k - 1) * sizeof(uint32_t));
            m--;
            found = 1;
        }

        if (!found)
        {
            /* No ear: fan what remains rather than drop it. */
            for (k = 1; k + 1 < m; k++)
            {
                if (u32_push(out, ring[0]) || u32_push(out, ring[k]) ||
                    u32_push(out, ring[k + 1]))
                    return -1;
            }
            return 0;
        }
    }
    return u32_push(out, ring[0]) || u32_push(out, ring[1]) || u32_push(out, ring[2]);
}

/* -------------------------------------------------------------------- */
/* One tessellation entry: the faces of one part that use one material   */
/* -------------------------------------------------------------------- */

typedef struct
{
    /* fast_obj indices of each triangle corner, 3 per triangle */
    u32_array p, t, n;
    uint32_t material;          /* fast_obj material index, OBJ_NO_INDEX for none */

    /* Built for the writer */
    double   *positions;
    uint32_t  num_positions;
    double   *normals;
    uint32_t  num_normals;
    double   *tex_coords;
    uint32_t  num_tex_coords;
    uint32_t *tri_indices;
    uint32_t *norm_indices;
    uint32_t *tex_indices;
    uint32_t  face_tri_count;
    double    bbox_min[3], bbox_max[3];
    /* From prc_api_mesh_weld_and_split, owned by the write context */
    double   *split_positions;
    uint32_t *split_indices;
    uint8_t  *texture_bytes;
    size_t    texture_size;
    uint32_t  texture_format;
} submesh;

static void
submesh_free(submesh *s)
{
    free(s->p.data); free(s->t.data); free(s->n.data);
    free(s->positions); free(s->normals); free(s->tex_coords);
    free(s->tri_indices); free(s->norm_indices); free(s->tex_indices);
    free(s->texture_bytes);
}

/* Compact one fast_obj attribute stream: every index used by the submesh is
   given a new dense index, in first-use order, and the values are copied
   out as doubles. `dim` is 3 for positions and normals, 2 for UVs. */
static int
compact_stream(const u32_array *idx, const float *src, uint32_t src_count,
    uint32_t dim, double **values_out, uint32_t *count_out, uint32_t **indices_out)
{
    uint32_t *remap = (uint32_t *)malloc((size_t)src_count * sizeof(uint32_t));
    double *values = (double *)malloc((size_t)idx->count * dim * sizeof(double));
    uint32_t *indices = (uint32_t *)malloc((size_t)idx->count * sizeof(uint32_t));
    uint32_t i, used = 0;

    if (remap == NULL || values == NULL || indices == NULL)
    {
        free(remap); free(values); free(indices);
        return -1;
    }
    for (i = 0; i < src_count; i++)
        remap[i] = OBJ_NO_INDEX;

    for (i = 0; i < idx->count; i++)
    {
        uint32_t v = idx->data[i];

        if (remap[v] == OBJ_NO_INDEX)
        {
            uint32_t d;

            remap[v] = used;
            for (d = 0; d < dim; d++)
                values[used * dim + d] = (double)src[v * dim + d];
            used++;
        }
        indices[i] = remap[v];
    }
    free(remap);
    *values_out = values;
    *count_out = used;
    *indices_out = indices;
    return 0;
}

static int
read_file(const char *path, uint8_t **bytes, size_t *size)
{
    FILE *f = fopen(path, "rb");
    long len;
    uint8_t *buf;

    if (f == NULL)
        return -1;
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET) != 0)
    {
        fclose(f);
        return -1;
    }
    buf = (uint8_t *)malloc((size_t)len);
    if (buf == NULL || fread(buf, 1, (size_t)len, f) != (size_t)len)
    {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    *bytes = buf;
    *size = (size_t)len;
    return 0;
}

/* PNG or JPEG, by signature rather than by file extension. */
static int
image_format(const uint8_t *b, size_t n, uint32_t *format, uint32_t *w, uint32_t *h)
{
    static const uint8_t png_sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

    *w = *h = 0;
    if (n >= 24 && memcmp(b, png_sig, 8) == 0)
    {
        *format = PRC_API_WRITE_TEXTURE_PNG;
        *w = ((uint32_t)b[16] << 24) | ((uint32_t)b[17] << 16) | ((uint32_t)b[18] << 8) | b[19];
        *h = ((uint32_t)b[20] << 24) | ((uint32_t)b[21] << 16) | ((uint32_t)b[22] << 8) | b[23];
        return 0;
    }
    if (n >= 3 && b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF)
    {
        *format = PRC_API_WRITE_TEXTURE_JPEG;
        return 0;
    }
    return -1;
}

/* Turn the collected corner indices into the arrays the writer takes, and
   work out which optional channels the submesh can carry. */
static int
build_submesh(const fastObjMesh *mesh, submesh *s, int want_texture,
    uint32_t *has_normals, uint32_t *has_uvs)
{
    uint32_t i, d;

    *has_normals = 1;
    *has_uvs = want_texture ? 1 : 0;
    for (i = 0; i < s->p.count; i++)
    {
        if (s->n.data[i] == 0)
            *has_normals = 0;
        if (s->t.data[i] == 0)
            *has_uvs = 0;
    }

    if (compact_stream(&s->p, mesh->positions, mesh->position_count, 3,
                       &s->positions, &s->num_positions, &s->tri_indices) != 0)
        return -1;
    if (*has_normals &&
        compact_stream(&s->n, mesh->normals, mesh->normal_count, 3,
                       &s->normals, &s->num_normals, &s->norm_indices) != 0)
        return -1;
    if (*has_uvs &&
        compact_stream(&s->t, mesh->texcoords, mesh->texcoord_count, 2,
                       &s->tex_coords, &s->num_tex_coords, &s->tex_indices) != 0)
        return -1;

    s->face_tri_count = s->p.count / 3;
    if (s->num_positions == 0 || s->positions == NULL)
        return -1;

    for (d = 0; d < 3; d++)
    {
        s->bbox_min[d] = s->positions[d];
        s->bbox_max[d] = s->positions[d];
    }
    for (i = 1; i < s->num_positions; i++)
    {
        for (d = 0; d < 3; d++)
        {
            double v = s->positions[i * 3 + d];

            if (v < s->bbox_min[d]) s->bbox_min[d] = v;
            if (v > s->bbox_max[d]) s->bbox_max[d] = v;
        }
    }
    return 0;
}

/* -------------------------------------------------------------------- */
/* Main                                                                  */
/* -------------------------------------------------------------------- */

static char *
replace_extension(const char *path, const char *ext)
{
    const char *dot = strrchr(path, '.');
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    size_t stem;
    char *out;

    if (bslash > slash)
        slash = bslash;
    stem = (dot != NULL && (slash == NULL || dot > slash)) ? (size_t)(dot - path) : strlen(path);
    out = (char *)malloc(stem + strlen(ext) + 1);
    if (out == NULL)
        return NULL;
    memcpy(out, path, stem);
    strcpy(out + stem, ext);
    return out;
}

static void
usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s <input.obj> [output.prc] [output.pdf] [options]\n"
        "  Writes the OBJ (and its MTL materials and PNG/JPEG diffuse maps) as PRC.\n"
        "  --triangles     write uncompressed triangles for every part (default:\n"
        "                  compressed, except textured parts and parts with a\n"
        "                  vertex shared by disconnected fans, which are\n"
        "                  written as triangles).\n"
        "  --no-normals    ignore the file's vn normals (the reader computes them)\n"
        "  --no-materials  ignore the MTL file (no colours, no textures)\n"
        "  --jitter F      move each part by its own offset of up to F times the\n"
        "                  model diagonal, against an Acrobat defect where some\n"
        "                  layouts of parts give an empty model tree. Default 1e-4;\n"
        "                  0 turns it off; some layouts in a line need 5e-4.\n", prog);
}

int
main(int argc, char *argv[])
{
    const char *in_path = NULL;
    const char *prc_path = NULL;
    const char *pdf_path = NULL;
    char *prc_default = NULL, *pdf_default = NULL;
    int force_triangles = 0, ignore_normals = 0, ignore_materials = 0;
    int a, rc = 1;
    prc_write_tolerance tol = prc_write_tol_relative(1e-6);
    uint32_t nonmanifold_parts = 0;
    /* On by default: measured on 16 October 2026, three files whose parts lie
       along one axis gave an empty model tree in Acrobat, and all rendered
       once each part was moved by up to 1e-4 of the model diagonal. Datalogics
       found the same, and that some layouts in a line need up to 5e-4. */
    double jitter = 1e-4;

    fastObjMesh *mesh = NULL;
    prc_context *ctx = NULL;

    submesh *subs = NULL;
    uint32_t num_subs = 0;
    uint32_t *part_first = NULL;     /* first submesh of each part */
    uint32_t *part_count = NULL;     /* submeshes in each part */
    const fastObjGroup *parts = NULL;
    uint32_t num_parts = 0;

    prc_api_write_tessellation *tess = NULL;
    prc_api_write_rep_item *items = NULL;
    prc_api_write_node *nodes = NULL;
    prc_api_write_node **children = NULL;
    prc_api_write_node root;
    uint8_t *prc_buf = NULL;
    size_t prc_size = 0;

    double (*corner)[3] = NULL;
    double (*proj)[2] = NULL;
    uint32_t *ring = NULL;
    uint32_t max_face_vertices = 0;
    u32_array tri = { NULL, 0, 0 };
    uint32_t total_tris = 0, polygons_split = 0, degenerate_skipped = 0;
    uint32_t i, k;

    for (a = 1; a < argc; a++)
    {
        if (strcmp(argv[a], "--triangles") == 0)
            force_triangles = 1;
        else if (strcmp(argv[a], "--no-normals") == 0)
            ignore_normals = 1;
        else if (strcmp(argv[a], "--no-materials") == 0)
            ignore_materials = 1;
        else if (strcmp(argv[a], "--jitter") == 0 && a + 1 < argc)
            jitter = atof(argv[++a]);
        else if (argv[a][0] == '-' && argv[a][1] == '-')
        {
            usage(argv[0]);
            return 1;
        }
        else if (in_path == NULL)
            in_path = argv[a];
        else if (prc_path == NULL)
            prc_path = argv[a];
        else if (pdf_path == NULL)
            pdf_path = argv[a];
        else
        {
            usage(argv[0]);
            return 1;
        }
    }
    if (in_path == NULL)
    {
        usage(argv[0]);
        return 1;
    }
    if (prc_path == NULL)
        prc_path = prc_default = replace_extension(in_path, ".prc");
    if (pdf_path == NULL)
        pdf_path = pdf_default = replace_extension(in_path, ".pdf");
    if (prc_path == NULL || pdf_path == NULL)
        goto done;

    mesh = fast_obj_read(in_path);
    if (mesh == NULL)
    {
        fprintf(stderr, "Could not read %s\n", in_path);
        goto done;
    }
    if (mesh->face_count == 0 || mesh->position_count <= 1)
    {
        fprintf(stderr, "%s contains no faces\n", in_path);
        goto done;
    }
    printf("%s: %u positions, %u normals, %u texcoords, %u faces, %u materials, "
           "%u objects, %u groups\n", in_path,
           mesh->position_count - 1, mesh->normal_count - 1, mesh->texcoord_count - 1,
           mesh->face_count, mesh->material_count, mesh->object_count, mesh->group_count);

    /* Parts: objects if the file names any, else groups, else the whole file. */
    if (mesh->object_count > 0)
    {
        parts = mesh->objects;
        num_parts = mesh->object_count;
    }
    else if (mesh->group_count > 0)
    {
        parts = mesh->groups;
        num_parts = mesh->group_count;
    }

    for (i = 0; i < mesh->face_count; i++)
        if (mesh->face_vertices[i] > max_face_vertices)
            max_face_vertices = mesh->face_vertices[i];
    if (max_face_vertices < 3)
        max_face_vertices = 3;
    corner = (double (*)[3])malloc((size_t)max_face_vertices * sizeof(*corner));
    proj = (double (*)[2])malloc((size_t)max_face_vertices * sizeof(*proj));
    ring = (uint32_t *)malloc((size_t)max_face_vertices * sizeof(uint32_t));
    part_first = (uint32_t *)calloc(num_parts ? num_parts : 1, sizeof(uint32_t));
    part_count = (uint32_t *)calloc(num_parts ? num_parts : 1, sizeof(uint32_t));
    /* At most one submesh per (part, material) pair, plus "no material". */
    subs = (submesh *)calloc((size_t)(num_parts ? num_parts : 1) * (mesh->material_count + 1),
                             sizeof(submesh));
    if (corner == NULL || proj == NULL || ring == NULL || part_first == NULL ||
        part_count == NULL || subs == NULL)
    {
        fprintf(stderr, "Out of memory\n");
        goto done;
    }

    {
        uint32_t p, nparts = num_parts ? num_parts : 1;

        for (p = 0; p < nparts; p++)
        {
            uint32_t face_begin = num_parts ? parts[p].face_offset : 0;
            uint32_t face_end = num_parts ? face_begin + parts[p].face_count : mesh->face_count;
            uint32_t index_pos = num_parts ? parts[p].index_offset : 0;
            uint32_t f;

            part_first[p] = num_subs;

            for (f = face_begin; f < face_end; f++)
            {
                uint32_t nv = mesh->face_vertices[f];
                uint32_t mat = (mesh->material_count > 0 && mesh->face_materials != NULL)
                    ? mesh->face_materials[f] : OBJ_NO_INDEX;
                const fastObjIndex *fi = &mesh->indices[index_pos];
                submesh *s = NULL;
                uint32_t c;

                index_pos += nv;

                /* fast_obj v1.3 ignores OBJ line records, so every face here
                   is a polygon; only a face of fewer than 3 corners is skipped. */
                if (nv < 3)
                {
                    degenerate_skipped++;
                    continue;
                }

                for (c = part_first[p]; c < num_subs; c++)
                {
                    if (subs[c].material == mat)
                    {
                        s = &subs[c];
                        break;
                    }
                }
                if (s == NULL)
                {
                    s = &subs[num_subs++];
                    s->material = mat;
                }

                for (c = 0; c < nv; c++)
                {
                    const float *v = &mesh->positions[fi[c].p * 3];

                    corner[c][0] = v[0];
                    corner[c][1] = v[1];
                    corner[c][2] = v[2];
                }
                tri.count = 0;
                if (triangulate_polygon((const double (*)[3])corner, nv, &tri, proj, ring) != 0)
                {
                    fprintf(stderr, "Out of memory\n");
                    goto done;
                }
                if (nv > 3)
                    polygons_split++;

                for (c = 0; c < tri.count; c++)
                {
                    const fastObjIndex *x = &fi[tri.data[c]];

                    if (u32_push(&s->p, x->p) || u32_push(&s->t, x->t) || u32_push(&s->n, x->n))
                    {
                        fprintf(stderr, "Out of memory\n");
                        goto done;
                    }
                }
            }
            part_count[p] = num_subs - part_first[p];
        }
        num_parts = nparts;
    }

    if (num_subs == 0)
    {
        fprintf(stderr, "%s contains no polygons\n", in_path);
        goto done;
    }

    tess = (prc_api_write_tessellation *)calloc(num_subs, sizeof(*tess));
    items = (prc_api_write_rep_item *)calloc(num_subs, sizeof(*items));
    nodes = (prc_api_write_node *)calloc(num_parts, sizeof(*nodes));
    children = (prc_api_write_node **)calloc(num_parts, sizeof(*children));
    if (tess == NULL || items == NULL || nodes == NULL || children == NULL)
    {
        fprintf(stderr, "Out of memory\n");
        goto done;
    }

    ctx = prc_api_new_context(NULL);
    if (ctx == NULL)
    {
        fprintf(stderr, "prc_api_new_context failed\n");
        goto done;
    }

    for (i = 0; i < num_subs; i++)
    {
        submesh *s = &subs[i];
        const fastObjMaterial *m = (s->material != OBJ_NO_INDEX && !ignore_materials)
            ? &mesh->materials[s->material] : NULL;
        int want_texture = 0;
        uint32_t has_normals, has_uvs, tw = 0, th = 0;
        prc_api_write_tessellation *t = &tess[i];
        prc_api_write_rep_item *ri = &items[i];

        /* A texture is attached only if the image exists and is PNG or JPEG. */
        if (m != NULL && m->map_Kd != 0 && mesh->textures[m->map_Kd].path != NULL)
        {
            const char *img = mesh->textures[m->map_Kd].path;

            if (read_file(img, &s->texture_bytes, &s->texture_size) != 0)
                fprintf(stderr, "warning: texture %s not found, material left untextured\n", img);
            else if (image_format(s->texture_bytes, s->texture_size, &s->texture_format, &tw, &th) != 0)
                fprintf(stderr, "warning: texture %s is not PNG or JPEG, left untextured\n", img);
            else
            {
                want_texture = 1;
                if (s->texture_format == PRC_API_WRITE_TEXTURE_PNG && tw >= 256 && th >= 256)
                    fprintf(stderr, "warning: %s is a PNG of %ux%u; a PNG of 256 or more on both "
                            "axes currently gives an empty model tree in Acrobat. JPEG does not.\n",
                            img, tw, th);
            }
        }

        if (build_submesh(mesh, s, want_texture, &has_normals, &has_uvs) != 0)
        {
            fprintf(stderr, "Out of memory\n");
            goto done;
        }
        if (want_texture && !has_uvs)
        {
            fprintf(stderr, "warning: material %s has a texture but its faces have no UVs; "
                    "left untextured\n", m->name ? m->name : "(unnamed)");
            want_texture = 0;
        }

        if (ignore_normals)
            has_normals = 0;

        t->kind = (force_triangles || want_texture)
            ? PRC_API_WRITE_TESS_KIND_TRIANGLES : PRC_API_WRITE_TESS_KIND_COMPRESSED;
        t->positions = s->positions;
        t->num_positions = s->num_positions;
        t->tri_indices = s->tri_indices;
        t->num_triangles = s->face_tri_count;

        /* A vertex shared by disconnected triangle fans is decoded correctly
           by our reader but not by some Acrobat builds when written
           compressed. The write API's own guidance is to write such a mesh
           as triangles instead, repaired by prc_api_mesh_weld_and_split. */
        if (t->kind == PRC_API_WRITE_TESS_KIND_COMPRESSED)
        {
            int nm = prc_api_mesh_has_nonmanifold_fans(ctx, s->positions, s->num_positions,
                s->tri_indices, s->face_tri_count, tol);

            if (nm < 0)
            {
                prc_api_print_error_stack(ctx);
                goto done;
            }
            if (nm == 1)
            {
                t->kind = PRC_API_WRITE_TESS_KIND_TRIANGLES;
                nonmanifold_parts++;
                if (!has_normals)
                {
                    if (prc_api_mesh_weld_and_split(ctx, s->positions, s->num_positions,
                            s->tri_indices, s->face_tri_count, tol,
                            &s->split_positions, &t->num_positions,
                            &s->split_indices, &s->face_tri_count) != 0)
                    {
                        prc_api_print_error_stack(ctx);
                        goto done;
                    }
                    t->positions = s->split_positions;
                    t->tri_indices = s->split_indices;
                    t->num_triangles = s->face_tri_count;
                }
            }
        }
        t->face_tri_counts = &s->face_tri_count;
        t->num_faces = 1;
        if (has_normals)
        {
            t->normals = s->normals;
            t->num_normals = s->num_normals;
            t->norm_indices = s->norm_indices;
        }
        else if (t->kind == PRC_API_WRITE_TESS_KIND_TRIANGLES)
        {
            /* Without supplied normals TRIANGLES would give the whole face
               one flat normal; have the reader compute per-vertex ones. */
            t->must_calculate_normals = 1;
        }
        if (want_texture)
        {
            t->tex_coords = s->tex_coords;
            t->num_tex_coords = s->num_tex_coords;
            t->tex_indices = s->tex_indices;
        }

        ri->kind = PRC_API_WRITE_RI_SURFACE;
        ri->biased_tessellation_index = i + 1;
        if (m != NULL)
        {
            ri->has_material = 1;
            ri->material_color[0] = m->Kd[0];
            ri->material_color[1] = m->Kd[1];
            ri->material_color[2] = m->Kd[2];
            ri->material_alpha = (m->d > 0.0f && m->d <= 1.0f) ? m->d : 1.0;
            /* MTL Ns runs 0..1000; the writer takes 0..1. */
            ri->material_shininess = m->Ns <= 0.0f ? 0.0 : (m->Ns >= 1000.0f ? 1.0 : m->Ns / 1000.0);
            if (want_texture)
            {
                ri->has_texture = 1;
                ri->texture_image = s->texture_bytes;
                ri->texture_image_size = s->texture_size;
                ri->texture_format = s->texture_format;
                ri->texture_width = tw;
                ri->texture_height = th;
            }
        }
        total_tris += s->face_tri_count;
    }

    /* Optional jitter: move each tessellation rigidly by its own tiny offset.
       Some layouts of separate parts -- in a line, or on a lattice -- give an
       empty model tree in Acrobat, with no rule known for which; displacing
       every part slightly and differently avoids it (Datalogics, 17 August
       2026). Offsets come from frac((i+1) * a) with an irrational a per axis,
       so no two parts move by the same vector. */
    if (jitter > 0.0)
    {
        static const double alpha[3] = { 0.6180339887498949, 0.4142135623730951, 0.7320508075688772 };
        double lo[3], hi[3], diag;
        uint32_t d, v;

        for (d = 0; d < 3; d++)
        {
            lo[d] = subs[0].bbox_min[d];
            hi[d] = subs[0].bbox_max[d];
        }
        for (i = 1; i < num_subs; i++)
            for (d = 0; d < 3; d++)
            {
                if (subs[i].bbox_min[d] < lo[d]) lo[d] = subs[i].bbox_min[d];
                if (subs[i].bbox_max[d] > hi[d]) hi[d] = subs[i].bbox_max[d];
            }
        diag = sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) +
                    (hi[2] - lo[2]) * (hi[2] - lo[2]));

        for (i = 0; i < num_subs; i++)
        {
            double *pos = subs[i].split_positions ? subs[i].split_positions : subs[i].positions;
            double off[3];

            for (d = 0; d < 3; d++)
            {
                double f = (double)(i + 1) * alpha[d];

                off[d] = jitter * diag * 2.0 * ((f - floor(f)) - 0.5);
                subs[i].bbox_min[d] += off[d];
                subs[i].bbox_max[d] += off[d];
            }
            for (v = 0; v < tess[i].num_positions; v++)
                for (d = 0; d < 3; d++)
                    pos[v * 3 + d] += off[d];
        }
    }

    /* Tree: one root, one child node per part, each holding its submeshes. */
    memset(&root, 0, sizeof(root));
    for (k = 0; k < num_parts; k++)
    {
        prc_api_write_node *node = &nodes[k];
        uint32_t d;

        if (part_count[k] == 0)
            continue;
        node->rep_items = &items[part_first[k]];
        node->num_rep_items = part_count[k];
        node->name = (parts != NULL && parts[k].name != NULL) ? parts[k].name : NULL;
        node->part_name = node->name;
        for (d = 0; d < 3; d++)
        {
            node->bbox_min[d] = subs[part_first[k]].bbox_min[d];
            node->bbox_max[d] = subs[part_first[k]].bbox_max[d];
        }
        for (i = part_first[k] + 1; i < part_first[k] + part_count[k]; i++)
        {
            for (d = 0; d < 3; d++)
            {
                if (subs[i].bbox_min[d] < node->bbox_min[d]) node->bbox_min[d] = subs[i].bbox_min[d];
                if (subs[i].bbox_max[d] > node->bbox_max[d]) node->bbox_max[d] = subs[i].bbox_max[d];
            }
        }
        if (root.num_children == 0)
        {
            memcpy(root.bbox_min, node->bbox_min, sizeof(root.bbox_min));
            memcpy(root.bbox_max, node->bbox_max, sizeof(root.bbox_max));
        }
        else
        {
            for (d = 0; d < 3; d++)
            {
                if (node->bbox_min[d] < root.bbox_min[d]) root.bbox_min[d] = node->bbox_min[d];
                if (node->bbox_max[d] > root.bbox_max[d]) root.bbox_max[d] = node->bbox_max[d];
            }
        }
        children[root.num_children++] = node;
    }
    /* The root is an empty part; its box must be real, not zero, or Acrobat
       blanks the tree beneath it. */
    root.has_empty_part = 1;
    root.children = children;

    if (prc_api_write_prc_buffer(ctx, NULL, &root, tess, num_subs, &prc_buf, &prc_size) != 0)
    {
        fprintf(stderr, "Writing PRC failed\n");
        prc_api_print_error_stack(ctx);
        goto done;
    }
    {
        FILE *out = fopen(prc_path, "wb");

        if (out == NULL || fwrite(prc_buf, 1, prc_size, out) != prc_size)
        {
            fprintf(stderr, "Could not write %s\n", prc_path);
            if (out != NULL)
                fclose(out);
            goto done;
        }
        fclose(out);
    }
    if (prc_api_pdf_embed_prc(ctx, pdf_path, prc_buf, prc_size, NULL) != 0)
    {
        fprintf(stderr, "Writing PDF failed\n");
        prc_api_print_error_stack(ctx);
        goto done;
    }

    {
        uint32_t textured = 0, compressed = 0;

        for (i = 0; i < num_subs; i++)
        {
            if (items[i].has_texture) textured++;
            if (tess[i].kind == PRC_API_WRITE_TESS_KIND_COMPRESSED) compressed++;
        }
        printf("Wrote %s (%lu bytes) and %s: %u parts, %u tessellations "
               "(%u compressed, %u textured, %u written as triangles for non-manifold "
               "fans), %u triangles; %u polygons triangulated, %u degenerate faces skipped\n",
               prc_path, (unsigned long)prc_size, pdf_path, root.num_children, num_subs,
               compressed, textured, nonmanifold_parts, total_tris, polygons_split,
               degenerate_skipped);
    }
    rc = 0;

done:
    if (ctx != NULL)
    {
        if (prc_buf != NULL)
            prc_api_write_prc_buffer_free(ctx, prc_buf);
        if (subs != NULL)
            for (i = 0; i < num_subs; i++)
                prc_api_mesh_weld_and_split_free(ctx, subs[i].split_positions,
                                                 subs[i].split_indices);
        prc_api_release_context(ctx);
    }
    if (subs != NULL)
        for (i = 0; i < num_subs; i++)
            submesh_free(&subs[i]);
    free(subs); free(part_first); free(part_count);
    free(tess); free(items); free(nodes); free(children);
    free(corner); free(proj); free(ring); free(tri.data);
    free(prc_default); free(pdf_default);
    if (mesh != NULL)
        fast_obj_destroy(mesh);
    return rc;
}
