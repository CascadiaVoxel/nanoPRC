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

/**
@file obj_export.c
@brief Optimized utility program to extract 3D polygon tessellations and textures from PRC files,
exporting to Wavefront OBJ + MTL + PNG maps with automatic texture deduplication.

The obj_export program is a high-performance, cross-platform command-line utility designed to extract 3D polygon tessellations,
material definitions, and embedded surface textures from Product Representation Compact (PRC) files or PDF containers,
converting them into industry-standard Wavefront OBJ asset bundles.

Program Features
- Automated Asset Unbundling: Processes a single PRC file and automatically outputs a matched set of resources:
a geometry file (.obj), a material library file (.mtl), and a collection of extracted texture maps (.png).

- Embedded Texture Extraction: Automatically detects embedded raster imagery maps mapped to individual faces,
extracts the uncompressed raw pixel streams, and saves them locally as portable PNG images.

- Smart Asset Linking: Dynamically generates localized, distinct material identifiers within the .mtl library,
binding surface color coefficients (ambient, diffuse, specular, emissive, and transparency)
and linking extracted texture maps via the standard map_Kd syntax.

- Full Vertex Attributes Export: Formats and writes absolute geometric coordinates, including spatial positions (v),
explicit surface normal vectors (vn), and normalized texture mapping coordinates (vt).

- Multi-Topology Primitive Triangulation: Decodes and unwinds complex GPU primitives - including independent triangle listings,
high-density triangle strips, and triangle fans - translating them into uniform 3-element face records (f).

- Platform-Agnostic Path Resolving: Automatically parses input and output paths to isolate file stems and base names,
ensuring correct material library declarations (mtllib) and relative texture paths regardless of whether it is running on Linux or Windows.

Command-Line Usage
  The tool operates via a simple, non-interactive command-line interface requiring an explicit input file pathway and a target destination.

Syntax
    obj_export <input_file.prc> <output_file.obj>
Example Execution
    ./obj_export engine_assembly.prc output/engine.obj

Generated Output Artifacts
- Executing the program generates a clean suite of asset files in the designated target directory:

- Main Geometry File (<filename>.obj): Contains the indexed arrangement of all geometric points,
texture coordinates, and surface normals grouped sequentially by internal part names.
It references the companion material file at the header via mtllib.

- Material Library File (<filename>.mtl): Declares distinct material definitions for each
individual mesh component or face configuration. This file tracks transparency metrics (d),
shininess indices (Ns), and color spaces.

- Extracted Texture Maps (<filename>_tex_<tessellation_id>_<face_id>.png):
Generated only if embedded textures are present. These files use a structured numerical suffix
corresponding to the precise internal structural indices of the source PRC model to avoid asset collisions.

Limitations & Behavioral Constraints
- 3D Surface Tessellation Focus: The tool is explicitly optimized to capture solid surface envelopes
(PRC_API_TESS_3D and PRC_API_TESS_3D_Compressed). It ignores standalone wireframe curves (PRC_API_TESS_3D_Wire),
structural bounding outlines, and floating 2D markup sketches.

- Fixed Texture Export Format: Textures are extracted and uncompressed directly into PNG format
to ensure cross-platform compatibility and lossless fidelity. Alternative image encodings (such as JPEG or BMP) are not supported.

- Local Workspace Permissions: Because the utility isolates filename stems to dump companion
texture maps alongside the .obj file, it requires active write permissions within the targeted destination
folder or execution will fail during asset serialization. Existing files are overwritten without warning.

 */

#include <prc_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

/* Embed STB Image Writer for portable texture image file generation */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

/**
 * @brief Structure to track unique texture assets to optimize disk operations
 * and avoid writing duplicate image files.
 */
typedef struct {
    const unsigned char *data; /* Shallow pointer to the context's internal texture pixel buffer */
    uint32_t width;
    uint32_t height;
    uint32_t num_channels;
    char filename[256];        /* Local filename string to reuse across matching MTL references */
} TrackedTexture;

/**
 * @brief Centralized macro to check API return status codes, log clean diagnostics,
 * and redirect control flow to the cleanup label on failure.
 */
#define PRC_CHECK_RC(expr, msg) \
    do { \
        int rc_ = (expr); \
        if (rc_ < 0) { \
            fprintf(stderr, "Error: %s (code: %d)\n", (msg), rc_); \
            ret = 1; \
            goto cleanup; \
        } \
    } while (0)

/**
 * @brief 1 if a texture has an alpha channel (2 or 4 channels) that is not
 * opaque everywhere. Labels are often drawn this way: black glyphs on an
 * otherwise clear image, which drawn opaque cover what lies behind them.
 */
static int texture_has_alpha(const prc_api_texture *tex)
{
    size_t n, k;
    uint32_t ch = tex->num_channels;

    if (!tex->data || (ch != 2 && ch != 4))
        return 0;
    n = (size_t)tex->width * tex->height;
    for (k = 0; k < n; k++)
    {
        if (tex->data[k * ch + ch - 1] != 255)
            return 1;
    }
    return 0;
}

/**
 * @brief Helper to write a material entry into the companion .mtl file descriptor,
 * supporting optional diffuse texture maps via map_Kd. When the texture is
 * partly clear (alpha), the same image also goes out as map_d, the alpha
 * map, which importers read from the image's alpha channel.
 */
static void write_mtl_material(FILE *mtl_out, const char *mat_name, const prc_api_material *mat,
    const char *texture_filename, int texture_alpha)
{
    fprintf(mtl_out, "newmtl %s\n", mat_name);
    fprintf(mtl_out, "  Kd %f %f %f\n", mat->diffuse[0], mat->diffuse[1], mat->diffuse[2]);
    fprintf(mtl_out, "  Ks %f %f %f\n", mat->specular[0], mat->specular[1], mat->specular[2]);
    fprintf(mtl_out, "  Ka %f %f %f\n", mat->ambient[0], mat->ambient[1], mat->ambient[2]);
    fprintf(mtl_out, "  Ke %f %f %f\n", mat->emissive[0], mat->emissive[1], mat->emissive[2]);
    fprintf(mtl_out, "  Ns %f\n", mat->shininess);
    fprintf(mtl_out, "  d %f\n", mat->diffuse_alpha);

    if (texture_filename && strlen(texture_filename) > 0)
    {
        fprintf(mtl_out, "  map_Kd %s\n", texture_filename);
        if (texture_alpha)
        {
            fprintf(mtl_out, "  map_d %s\n", texture_filename);
        }
    }
    fprintf(mtl_out, "\n");
}

/* -------------------------------------------------------------------- */
/* Where each tessellation is drawn                                      */
/* -------------------------------------------------------------------- */

/* PRC_GRAPHICS_Removed (0x2000), as it appears in the high byte of a
   behaviour field, behavior_bit_field2: a product or item that has been
   removed and no longer appears (see prc_api_product). */
#define PRC_REMOVED_BYTE2 0x20

/* tess_drawn values: a part draws the tessellation, or only removed ones do */
#define TESS_DRAWN 1
#define TESS_REMOVED_ONLY 2

/* One place a tessellation is drawn: its index and the transform from the
   root of the model tree down to the part or item that draws it. Geometry is
   often stored around its own origin and placed by these transforms; written
   without them, the parts of an assembly pile up at the origin. */
typedef struct
{
    uint32_t tess;
    prc_api_transform xform;
} tess_instance;

typedef struct
{
    tess_instance *items;
    size_t count, cap;
} instance_list;

static int add_instance(instance_list *l, uint32_t tess, const prc_api_transform *xform)
{
    if (l->count == l->cap)
    {
        size_t cap = l->cap ? l->cap * 2 : 64;
        tess_instance *g = (tess_instance *)realloc(l->items, cap * sizeof(*g));

        if (!g)
        {
            return -1;
        }
        l->items = g;
        l->cap = cap;
    }
    l->items[l->count].tess = tess;
    l->items[l->count].xform = *xform;
    l->count++;
    return 0;
}

/* A representation item waiting to be visited, with the transform it is
   placed by (its product's, then the local coordinate systems of the sets
   that hold it), and whether it lies under something removed. */
typedef struct
{
    const prc_api_part *item;
    prc_api_transform xform;
    int removed;
} item_visit;

/**
 * @brief Walks the model tree as demos/stl_export does, composing each
 * product's own placement (prc_api_product::location) after its parents'
 * with prc_api_update_transform, and lists every tessellation each part
 * draws with that transform.
 *
 * A part's geometry is on the part itself or on its representation items,
 * which can nest in sets to a depth the file chooses, so those are visited
 * from an explicit stack. An item with a local coordinate system
 * (biased_local_coordinate_index) is placed by it within its parent's frame,
 * as nanoPRC returns its vertices in that system's frame (see
 * prc_api_get_local_coordinate_system). Tessellations are looked up by
 * part_detail_index, the API's numbering, which differs from the file's
 * wherever parts share a file tessellation.
 *
 * A product or item marked removed is not written, nor anything under it:
 * Adobe Acrobat does not draw it. Its tessellations are marked
 * TESS_REMOVED_ONLY unless a part that is not removed draws them too.
 */
static int collect_instances(prc_context *ctx, prc_api_data data, const prc_api_product *node,
    prc_api_transform parent, int removed, uint32_t total_tessellations, instance_list *out,
    uint8_t *tess_drawn)
{
    prc_api_transform world = parent;
    size_t k;

    if (node->behavior_bit_field2 & PRC_REMOVED_BYTE2)
    {
        removed = 1;
    }
    prc_api_update_transform(ctx, &world, (prc_api_transform *)&node->location);

    if (node->part)
    {
        item_visit *stack;
        size_t depth = 0, capacity = 16;

        stack = (item_visit *)malloc(capacity * sizeof(*stack));
        if (!stack)
        {
            return -1;
        }
        stack[depth].item = node->part;
        stack[depth].xform = world;
        stack[depth++].removed = removed;
        while (depth > 0)
        {
            item_visit v = stack[--depth];
            const prc_api_part *item = v.item;
            size_t ri;

            if (item->behavior_bit_field2 & PRC_REMOVED_BYTE2)
            {
                v.removed = 1;
            }
            if (item->biased_local_coordinate_index != 0)
            {
                prc_api_transform own;

                if (prc_api_get_local_coordinate_system(ctx, data, item->tess_file_index,
                        item->biased_local_coordinate_index, &own) == 0)
                {
                    prc_api_update_transform(ctx, &v.xform, &own);
                }
            }
            if (item->biased_tess_index > 0 && item->part_detail_index < total_tessellations)
            {
                if (v.removed)
                {
                    if (tess_drawn[item->part_detail_index] == 0)
                    {
                        tess_drawn[item->part_detail_index] = TESS_REMOVED_ONLY;
                    }
                }
                else
                {
                    if (add_instance(out, item->part_detail_index, &v.xform) != 0)
                    {
                        free(stack);
                        return -1;
                    }
                    tess_drawn[item->part_detail_index] = TESS_DRAWN;
                }
            }
            for (ri = item->num_rep_items; ri > 0; ri--)
            {
                if (depth == capacity)
                {
                    item_visit *grown = (item_visit *)realloc(stack, capacity * 2 * sizeof(*stack));

                    if (!grown)
                    {
                        free(stack);
                        return -1;
                    }
                    stack = grown;
                    capacity *= 2;
                }
                stack[depth].item = &item->rep_items[ri - 1];
                stack[depth].xform = v.xform;
                stack[depth++].removed = v.removed;
            }
        }
        free(stack);
    }

    for (k = 0; k < node->num_children; k++)
    {
        if (collect_instances(ctx, data, &node->children[k], world, removed, total_tessellations, out,
                tess_drawn) != 0)
        {
            return -1;
        }
    }
    return 0;
}

/**
 * @brief Moves a normal into place by a transform and renormalises. A normal
 * takes the cofactor matrix of the transform's linear (3x3) part: its inverse
 * transpose times its determinant, so with the determinant's sign it points
 * the same way and stays perpendicular to the surface under non-uniform
 * scale and shear too (local coordinate systems do scale unevenly).
 */
static void transform_normal(const prc_api_transform *xform, const float in[3], float out[3])
{
    const double *m;
    double c[9], det;
    float len;
    int i;

    if (xform->is_identity)
    {
        out[0] = in[0]; out[1] = in[1]; out[2] = in[2];
        return;
    }
    m = xform->matrix;
    /* c is column-major, as m is */
    c[0] = m[5] * m[10] - m[6] * m[9];
    c[1] = m[6] * m[8] - m[4] * m[10];
    c[2] = m[4] * m[9] - m[5] * m[8];
    c[3] = m[9] * m[2] - m[10] * m[1];
    c[4] = m[10] * m[0] - m[8] * m[2];
    c[5] = m[8] * m[1] - m[9] * m[0];
    c[6] = m[1] * m[6] - m[2] * m[5];
    c[7] = m[2] * m[4] - m[0] * m[6];
    c[8] = m[0] * m[5] - m[1] * m[4];
    det = m[0] * c[0] + m[4] * c[3] + m[8] * c[6];
    if (det < 0.0)
    {
        for (i = 0; i < 9; i++)
        {
            c[i] = -c[i];
        }
    }
    out[0] = (float)(c[0] * in[0] + c[3] * in[1] + c[6] * in[2]);
    out[1] = (float)(c[1] * in[0] + c[4] * in[1] + c[7] * in[2]);
    out[2] = (float)(c[2] * in[0] + c[5] * in[1] + c[8] * in[2]);
    len = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    if (len > 1e-7f)
    {
        out[0] /= len; out[1] /= len; out[2] /= len;
    }
    else
    {
        out[0] = in[0]; out[1] = in[1]; out[2] = in[2];
    }
}

/**
 * @brief 1 if the transform mirrors (its linear part has a negative
 * determinant). A mirrored triangle's corners then run the other way round,
 * so they are swapped back to keep the face pointing outward.
 */
static int transform_mirrors(const prc_api_transform *xform)
{
    const double *m = xform->matrix;
    double det;

    if (xform->is_identity)
    {
        return 0;
    }
    det = m[0] * (m[5] * m[10] - m[9] * m[6])
        - m[4] * (m[1] * m[10] - m[9] * m[2])
        + m[8] * (m[1] * m[6] - m[5] * m[2]);
    return det < 0.0;
}

/**
 * @brief Formats and writes a single triangle face record into the Wavefront OBJ standard stream,
 * utilizing independent global counters to prevent cross-attribute index drift. The triangle is
 * moved into place by xform; mirror swaps its last two corners (see transform_mirrors).
 */
static void process_and_write_obj_triangle(FILE* obj_out, prc_api_vertex v1, prc_api_vertex v2, prc_api_vertex v3,
    const prc_api_transform *xform, int mirror, uint32_t* v_count, uint32_t* vt_count, uint32_t* vn_count)
{
    if (mirror)
    {
        prc_api_vertex t = v2;
        v2 = v3;
        v3 = t;
    }

    /* Into place: positions and normals both, so the normals computed below
       for corners without one are already in place too. */
    if (!xform->is_identity)
    {
        prc_api_vertex *vs[3] = { &v1, &v2, &v3 };
        for (int c = 0; c < 3; c++)
        {
            double in[3] = { vs[c]->position[0], vs[c]->position[1], vs[c]->position[2] };
            double p[3];
            float n[3];

            prc_api_transform_point(xform, in, p);
            vs[c]->position[0] = (float)p[0];
            vs[c]->position[1] = (float)p[1];
            vs[c]->position[2] = (float)p[2];
            if (vs[c]->normal_set)
            {
                transform_normal(xform, vs[c]->normal, n);
                memcpy(vs[c]->normal, n, sizeof(n));
            }
        }
    }

    uint8_t has_uv = (v1.uv_set && v2.uv_set && v3.uv_set);

    /* Capture current 1-based start offsets for this specific triangle */
    uint32_t v_idx = *v_count;
    uint32_t vt_idx = *vt_count;
    uint32_t vn_idx = *vn_count;

    /* 1. Write vertex spatial coordinates */
    fprintf(obj_out, "v %f %f %f\n", v1.position[0], v1.position[1], v1.position[2]);
    fprintf(obj_out, "v %f %f %f\n", v2.position[0], v2.position[1], v2.position[2]);
    fprintf(obj_out, "v %f %f %f\n", v3.position[0], v3.position[1], v3.position[2]);
    *v_count += 3;

    /* 2. Write texture coordinates only if explicitly available */
    if (has_uv)
    {
        fprintf(obj_out, "vt %f %f\n", v1.uv[0], v1.uv[1]);
        fprintf(obj_out, "vt %f %f\n", v2.uv[0], v2.uv[1]);
        fprintf(obj_out, "vt %f %f\n", v3.uv[0], v3.uv[1]);
        *vt_count += 3;
    }

    /* 3. Determine and write normals */
    float n1[3], n2[3], n3[3];
    if (v1.normal_set && v2.normal_set && v3.normal_set)
    {
        memcpy(n1, v1.normal, sizeof(float) * 3);
        memcpy(n2, v2.normal, sizeof(float) * 3);
        memcpy(n3, v3.normal, sizeof(float) * 3);
    }
    else
    {
        float u[3] = { v2.position[0] - v1.position[0], v2.position[1] - v1.position[1], v2.position[2] - v1.position[2] };
        float v[3] = { v3.position[0] - v1.position[0], v3.position[1] - v1.position[1], v3.position[2] - v1.position[2] };
        float nx = u[1] * v[2] - u[2] * v[1];
        float ny = u[2] * v[0] - u[0] * v[2];
        float nz = u[0] * v[1] - u[1] * v[0];
        float len = sqrtf(nx * nx + ny * ny + nz * nz);
        if (len > 1e-7f)
        {
            nx /= len; ny /= len; nz /= len;
        }
        n1[0] = nx; n1[1] = ny; n1[2] = nz;
        memcpy(n2, n1, sizeof(float) * 3);
        memcpy(n3, n1, sizeof(float) * 3);
    }

    fprintf(obj_out, "vn %f %f %f\n", n1[0], n1[1], n1[2]);
    fprintf(obj_out, "vn %f %f %f\n", n2[0], n2[1], n2[2]);
    fprintf(obj_out, "vn %f %f %f\n", n3[0], n3[1], n3[2]);
    *vn_count += 3;

    /* 4. Emit Wavefront OBJ element face record using the decoupled index handles */
    if (has_uv)
    {
        /* Format: vertex/texture/normal */
        fprintf(obj_out, "f %u/%u/%u %u/%u/%u %u/%u/%u\n",
            v_idx, vt_idx, vn_idx,
            v_idx + 1, vt_idx + 1, vn_idx + 1,
            v_idx + 2, vt_idx + 2, vn_idx + 2);
    }
    else
    {
        /* Format: vertex//normal (safely skipping texture coordinates) */
        fprintf(obj_out, "f %u//%u %u//%u %u//%u\n",
            v_idx, vn_idx,
            v_idx + 1, vn_idx + 1,
            v_idx + 2, vn_idx + 2);
    }
}

/**
 * @brief Resolves base paths and filenames to safely manage platform-agnostic file writing.
 */
static void extract_stem_properties(const char *obj_path, char *out_stem_path, char *out_stem_filename, size_t max_len)
{
    strncpy(out_stem_path, obj_path, max_len - 1);
    out_stem_path[max_len - 1] = '\0';

    char *dot = strrchr(out_stem_path, '.');
    if (dot && strcmp(dot, ".obj") == 0)
    {
        *dot = '\0';
    }

    const char *slash1 = strrchr(out_stem_path, '/');
    const char *slash2 = strrchr(out_stem_path, '\\');
    const char *slash = (slash1 > slash2) ? slash1 : slash2;

    if (slash)
    {
        strncpy(out_stem_filename, slash + 1, max_len - 1);
    }
    else
    {
        strncpy(out_stem_filename, out_stem_path, max_len - 1);
    }
    out_stem_filename[max_len - 1] = '\0';
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: obj_export <input_file.prc> <output_file.obj>\n");
        return 1;
    }

    const char *input_path = argv[1];
    const char *output_path = argv[2];

    prc_context *ctx = NULL;
    prc_api_data data = NULL;
    prc_api_product *model_tree = NULL;
    prc_api_tess *tesses = NULL;
    FILE *obj_file = NULL;
    FILE *mtl_file = NULL;
    int ret = 0;

    /* Where each tessellation is drawn, and which have had their materials
       written: a tessellation drawn by several parts is written at each
       place, but its materials and textures once. */
    instance_list instances = { NULL, 0, 0 };
    uint8_t *tess_drawn = NULL;
    uint8_t *materials_written = NULL;

    /* Deduplication Tracking Registry */
    TrackedTexture *tracked_textures = NULL;
    size_t tracked_count = 0;
    size_t tracked_capacity = 0;

    uint32_t num_parts = 0, num_products = 0, num_markups = 0;
    uint32_t total_tessellations = 0, total_line_tessellations = 0;
    uint32_t num_extra_geom_tess = 0;

    uint32_t global_v_counter = 1;
    uint32_t global_vt_counter = 1;
    uint32_t global_vn_counter = 1;

    char stem_path[512];
    char stem_filename[512];
    extract_stem_properties(output_path, stem_path, stem_filename, sizeof(stem_path));

    char mtl_full_path[512];
    char mtl_local_filename[512];
    snprintf(mtl_full_path, sizeof(mtl_full_path), "%.*s.mtl",
             (int)(sizeof(mtl_full_path) - 5), stem_path);
    snprintf(mtl_local_filename, sizeof(mtl_local_filename), "%.*s.mtl",
             (int)(sizeof(mtl_local_filename) - 5), stem_filename);

    ctx = prc_api_new_context(NULL);
    if (!ctx)
    {
        fprintf(stderr, "Error: Failed to instantiate prc_api_new_context frame.\n");
        return 1;
    }

    data = prc_api_open_contents(ctx, input_path);
    if (!data)
    {
        fprintf(stderr, "Error: Failed to process or open input content pathway: %s\n", input_path);
        prc_api_release_context(ctx);
        return 1;
    }

    PRC_CHECK_RC(prc_api_prep_model_tree(ctx, data, &num_parts, &num_products, &num_markups),
                 "prc_api_prep_model_tree compilation failed");

    PRC_CHECK_RC(prc_api_create_model_tree(ctx, data, &model_tree, num_parts, num_products, num_markups),
                 "prc_api_create_model_tree node creation failed");

    PRC_CHECK_RC(prc_api_get_number_tessellations(ctx, data, model_tree, &total_tessellations, &total_line_tessellations, &num_extra_geom_tess, 0),
                 "prc_api_get_number_tessellations count retrieval failed");

    if (total_tessellations > 0)
    {
        tesses = calloc(total_tessellations, sizeof(prc_api_tess));
        if (!tesses)
        {
            fprintf(stderr, "Error: Calloc memory allocation space failure for tessellation elements registry.\n");
            ret = 1;
            goto cleanup;
        }
    }

    for (uint32_t k = 0; k < total_tessellations; k++)
    {
        prc_api_tess *tess = &tesses[k];
        uint32_t num_faces = prc_api_get_number_faces(ctx, data, k);

        tess->num_faces = num_faces;
        tess->tess_faces = calloc(num_faces, sizeof(prc_api_face));
        if (!tess->tess_faces)
        {
            fprintf(stderr, "Error: Face array cache segment allocation failure.\n");
            ret = 1;
            goto cleanup;
        }

        uint8_t has_lines = 0;
        PRC_CHECK_RC(prc_api_initialize_tessellation(ctx, data, model_tree, k, tess, NULL, &has_lines),
                     "prc_api_initialize_tessellation block setup failure");

        if (tess->type == PRC_API_TESS_UNKNOWN || tess->type == PRC_API_TESS_3D_Wire || tess->type == PRC_API_TESS_MarkUp)
        {
            continue;
        }

        for (uint32_t j = 0; j < tess->num_faces; j++)
        {
            PRC_CHECK_RC(prc_api_get_tessellation_vertices(ctx, data, k, j, tess->tess_faces + j, tess),
                         "prc_api_get_tessellation_vertices mapping lookup failure");
        }
    }

    if (total_tessellations > 0)
    {
        tess_drawn = calloc(total_tessellations, 1);
        materials_written = calloc(total_tessellations, 1);
        if (!tess_drawn || !materials_written)
        {
            fprintf(stderr, "Error: out of memory\n");
            ret = 1;
            goto cleanup;
        }
    }
    if (model_tree)
    {
        prc_api_transform root_transform;
        prc_api_set_transform_identity(ctx, &root_transform);
        if (collect_instances(ctx, data, model_tree, root_transform, 0, total_tessellations, &instances,
                tess_drawn) != 0)
        {
            fprintf(stderr, "Error: out of memory\n");
            ret = 1;
            goto cleanup;
        }
    }

    /* A surface tessellation that no part draws is reported, not written.
       Adobe Acrobat does not draw one either. Nor one only removed products
       or items draw: those are counted. */
    {
        uint32_t removed_only = 0;

        for (uint32_t i = 0; i < total_tessellations; i++)
        {
            if (tess_drawn[i] == TESS_DRAWN ||
                (tesses[i].type != PRC_API_TESS_3D && tesses[i].type != PRC_API_TESS_3D_Compressed))
            {
                continue;
            }
            if (tess_drawn[i] == TESS_REMOVED_ONLY)
            {
                removed_only++;
            }
            else
            {
                fprintf(stderr, "Warning: tessellation %u is not drawn by any part; not exported.\n", i);
            }
        }
        if (removed_only > 0)
        {
            printf("%u tessellation(s) left out: only products or items marked removed draw them.\n",
                (unsigned)removed_only);
        }
    }

    obj_file = fopen(output_path, "w");
    if (!obj_file)
    {
        fprintf(stderr, "Error: Could not open file destination OBJ stream layout path: %s\n", output_path);
        ret = 1;
        goto cleanup;
    }

    mtl_file = fopen(mtl_full_path, "w");
    if (!mtl_file)
    {
        fprintf(stderr, "Error: Could not open material destination MTL stream layout path: %s\n", mtl_full_path);
        ret = 1;
        goto cleanup;
    }

    fprintf(obj_file, "# Generated by nanoPRC - AGPLv3 PRC parsing library (c) CascadiaVoxel LLC.\n");
    fprintf(obj_file, "mtllib %s\n\n", mtl_local_filename);
    fprintf(mtl_file, "# Generated by nanoPRC - AGPLv3 PRC parsing library (c) CascadiaVoxel LLC.\n\n");

    for (size_t n = 0; n < instances.count; n++)
    {
        uint32_t i = instances.items[n].tess;
        const prc_api_transform *xform = &instances.items[n].xform;
        int mirror = transform_mirrors(xform);
        int write_materials = !materials_written[i];
        prc_api_tess *tess = &tesses[i];
        if (tess->type != PRC_API_TESS_3D && tess->type != PRC_API_TESS_3D_Compressed)
        {
            continue;
        }
        materials_written[i] = 1;

        const char *part_boundary_name = (tess->name && strlen(tess->name) > 0) ? tess->name : "Unnamed_PRC_Component_Part";
        fprintf(obj_file, "\n# Part: %s\n", part_boundary_name);

        prc_api_tess_vertex_buffer *vertex_buf = (tess->type == PRC_API_TESS_3D_Compressed) ? &tess->tess_vertices : NULL;

        /* COMPRESSED tessellations don't partition geometry by face: every
           face_index's primitive returns the SAME complete mesh (see
           prc_api_get_tessellation_vertices's own comment on this, in
           src/prc_tri_primitives_api.c) -- face_index only carries meaning
           for per-face STYLE lookup there, never for geometry. Walking every
           face and emitting its "primitive" here therefore wrote the whole
           mesh once per face instead of once total: a 14-triangle, 7-face
           test mesh came out as 98 triangles, exactly 7x. Same defect and
           same fix as demos/stl_export. */
        uint32_t num_faces_to_walk = (tess->type == PRC_API_TESS_3D_Compressed) ? 1 : tess->num_faces;

        for (size_t f = 0; f < num_faces_to_walk; f++)
        {
            prc_api_face *face = &tess->tess_faces[f];
            if (tess->type == PRC_API_TESS_3D)
            {
                vertex_buf = &face->face_vertices;
            }

            if (!vertex_buf || !vertex_buf->vertices || vertex_buf->num_vertices == 0)
            {
                continue;
            }

            char dynamic_mat_id[128];
            char texture_local_name[256] = {0};
            snprintf(dynamic_mat_id, sizeof(dynamic_mat_id), "mat_tess_%u_face_%zu", i, f);

            /* Texture Optimization: Deduplicate embedded raster streams. Only the
               first place a tessellation is drawn writes its materials; later
               places reuse them by name. */
            if (write_materials && face->is_texture && face->texture.data && face->texture.width > 0 && face->texture.height > 0)
            {
                int found_duplicate = 0;
                size_t current_texture_bytes = (size_t)face->texture.width * face->texture.height * face->texture.num_channels;

                /* Check registry for an identical byte layout */
                for (size_t t = 0; t < tracked_count; t++)
                {
                    if (tracked_textures[t].width == face->texture.width &&
                        tracked_textures[t].height == face->texture.height &&
                        tracked_textures[t].num_channels == face->texture.num_channels)
                    {
                        if (memcmp(tracked_textures[t].data, face->texture.data, current_texture_bytes) == 0)
                        {
                            /* Match found! Share the file link and skip the redundant write sequence */
                                strncpy(texture_local_name, tracked_textures[t].filename,
                                    sizeof(texture_local_name) - 1);
                                texture_local_name[sizeof(texture_local_name) - 1] = '\0';
                            found_duplicate = 1;
                            break;
                        }
                    }
                }

                if (!found_duplicate)
                {
                    char texture_full_path[512];
                    /* Reserve worst-case tail: "_tex_" + uint32 + '_' + size_t + ".png" + NUL = 41 bytes. */
                    snprintf(texture_full_path, sizeof(texture_full_path),
                             "%.*s_tex_%u_%zu.png",
                             (int)(sizeof(texture_full_path) - 41), stem_path, i, f);
                    snprintf(texture_local_name, sizeof(texture_local_name),
                             "%.*s_tex_%u_%zu.png",
                             (int)(sizeof(texture_local_name) - 41), stem_filename, i, f);

                    int write_ok = stbi_write_png(texture_full_path,
                                                 (int)face->texture.width,
                                                 (int)face->texture.height,
                                                 (int)face->texture.num_channels,
                                                 face->texture.data,
                                                 (int)(face->texture.width * face->texture.num_channels));
                    if (write_ok)
                    {
                        /* Dynamically grow optimization array capacity if needed */
                        if (tracked_count >= tracked_capacity)
                        {
                            tracked_capacity = tracked_capacity == 0 ? 16 : tracked_capacity * 2;
                            TrackedTexture *new_arr = realloc(tracked_textures, tracked_capacity * sizeof(TrackedTexture));
                            if (!new_arr)
                            {
                                fprintf(stderr, "Error: Out of memory growing texture tracking array.\n");
                                ret = 1;
                                goto cleanup;
                            }
                            tracked_textures = new_arr;
                        }

                        /* Store metadata for subsequent comparison checks */
                        tracked_textures[tracked_count].data = face->texture.data;
                        tracked_textures[tracked_count].width = face->texture.width;
                        tracked_textures[tracked_count].height = face->texture.height;
                        tracked_textures[tracked_count].num_channels = face->texture.num_channels;
                        strncpy(tracked_textures[tracked_count].filename, texture_local_name, sizeof(tracked_textures[tracked_count].filename) - 1);
                        tracked_textures[tracked_count].filename[sizeof(tracked_textures[tracked_count].filename) - 1] = '\0';
                        tracked_count++;
                    }
                    else
                    {
                        fprintf(stderr, "Warning: Failed to write texture file: %s\n", texture_full_path);
                        texture_local_name[0] = '\0';
                    }
                }
            }

            /* Resolve material records */
            int texture_alpha = texture_local_name[0] != '\0' && texture_has_alpha(&face->texture);

            if (!write_materials)
            {
                /* written at the first place this tessellation is drawn */
            }
            else if (face->is_material)
            {
                write_mtl_material(mtl_file, dynamic_mat_id, &face->material, texture_local_name, texture_alpha);
            }
            else if (tess->is_material)
            {
                write_mtl_material(mtl_file, dynamic_mat_id, &tess->tess_material, texture_local_name, texture_alpha);
            }
            else
            {
                prc_api_material fallback_mat = {
                    .diffuse = { 0.80f, 0.80f, 0.80f },
                    .specular = { 0.25f, 0.25f, 0.25f },
                    .ambient = { 0.20f, 0.20f, 0.20f },
                    .emissive = { 0.00f, 0.00f, 0.00f },
                    .shininess = 32.0f,
                    .diffuse_alpha = 1.0f
                };
                write_mtl_material(mtl_file, dynamic_mat_id, &fallback_mat, texture_local_name, texture_alpha);
            }

            fprintf(obj_file, "usemtl %s\n", dynamic_mat_id);

            /* Traverse graphic primitives and build faces */
            for (size_t p = 0; p < face->num_graphic_primitives; p++)
            {
                prc_api_graphic_primitive primitive;
                if (prc_api_get_graphics_primitive(ctx, data, tess, (uint32_t)f, p, &primitive) < 0)
                {
                    continue;
                }

                if (primitive.type == PRC_API_TRIANGLES)
                {
                    for (size_t idx = 0; idx + 2 < primitive.num_indices; idx += 3)
                    {
                        uint32_t i0 = primitive.indices[idx];
                        uint32_t i1 = primitive.indices[idx + 1];
                        uint32_t i2 = primitive.indices[idx + 2];

                        if (i0 < vertex_buf->num_vertices && i1 < vertex_buf->num_vertices && i2 < vertex_buf->num_vertices)
                        {
                            process_and_write_obj_triangle(obj_file,
                                vertex_buf->vertices[i0],
                                vertex_buf->vertices[i1],
                                vertex_buf->vertices[i2],
                                xform, mirror,
                                &global_v_counter,
                                &global_vt_counter,
                                &global_vn_counter);
                        }
                    }
                }
                else if (primitive.type == PRC_API_STRIP)
                {
                    for (size_t m = 2; m < primitive.num_indices; m++)
                    {
                        uint32_t i0, i1, i2;
                        if (m % 2 == 0)
                        {
                            i0 = primitive.indices[m - 2];
                            i1 = primitive.indices[m - 1];
                            i2 = primitive.indices[m];
                        }
                        else
                        {
                            i0 = primitive.indices[m - 1];
                            i1 = primitive.indices[m - 2];
                            i2 = primitive.indices[m];
                        }

                        if (i0 < vertex_buf->num_vertices && i1 < vertex_buf->num_vertices && i2 < vertex_buf->num_vertices)
                        {
                            process_and_write_obj_triangle(obj_file,
                                vertex_buf->vertices[i0],
                                vertex_buf->vertices[i1],
                                vertex_buf->vertices[i2],
                                xform, mirror,
                                &global_v_counter,
                                &global_vt_counter,
                                &global_vn_counter);
                        }
                    }
                }
                else if (primitive.type == PRC_API_FAN)
                {
                    for (size_t m = 2; m < primitive.num_indices; m++)
                    {
                        uint32_t i0 = primitive.indices[0];
                        uint32_t i1 = primitive.indices[m - 1];
                        uint32_t i2 = primitive.indices[m];

                        if (i0 < vertex_buf->num_vertices && i1 < vertex_buf->num_vertices && i2 < vertex_buf->num_vertices)
                        {
                            process_and_write_obj_triangle(obj_file,
                                vertex_buf->vertices[i0],
                                vertex_buf->vertices[i1],
                                vertex_buf->vertices[i2],
                                xform, mirror,
                                &global_v_counter,
                                &global_vt_counter,
                                &global_vn_counter);
                        }
                    }
                }
            }
        }
    }

    printf("OBJ export completed successfully. Unique textures written: %zu. Total vertices: %u\n",
           tracked_count, global_v_counter - 1);

cleanup:
    free(instances.items);
    free(tess_drawn);
    free(materials_written);
    if (tracked_textures)
    {
        free(tracked_textures);
    }
    if (obj_file)
    {
        fclose(obj_file);
    }
    if (mtl_file)
    {
        fclose(mtl_file);
    }
    if (ctx)
    {
        if (data)
        {
            prc_api_release_data(ctx, data, tesses, total_tessellations, NULL, 0, NULL, 0, model_tree);
        }
        prc_api_release_context(ctx);
    }
    /* The face arrays and the tessellation array were allocated here, not
       by the library, so prc_api_release_data leaves them to us. */
    if (tesses)
    {
        for (uint32_t k = 0; k < total_tessellations; k++)
        {
            free(tesses[k].tess_faces);
        }
        free(tesses);
    }

    return ret;
}
