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

#ifndef PRC_WRITE_ABLATE_H
#define PRC_WRITE_ABLATE_H

/* Switches that turn off parts of the write pipeline, for measuring how much
   each part contributes to the size of a written file. They are environment
   variables read through prc_diag_getenv, so they exist only in a build
   configured with PRC_ENABLE_DIAG_ENV=ON (see include/prc_diag_env.h); in
   any other build they are all off and none of their names is in the
   binary. Every file written with one of them set is still a valid PRC file.

     PRC_DIAG_ABLATE_NORMALS   Drop supplied normals. A TRIANGLES entry is
                               written with must_calculate_normals (crease
                               angle 30 degrees unless one is given); a
                               COMPRESSED entry takes its recalculated-normals
                               path. A TRIANGLES entry with fans or strips
                               keeps its normals, since must_calculate_normals
                               is triangles-only.
     PRC_DIAG_ABLATE_COLORS    Drop vertex colours, from every kind of entry.
     PRC_DIAG_ABLATE_TEXTURES  Drop texture coordinates and every item's
                               texture and texture layers; the item keeps its
                               material.
     PRC_DIAG_ABLATE_TINY_TEXTURES
                               Replace every texture image, the main one and
                               each layer's, with the same 2 x 2 RGB image,
                               so the images' own size drops out of the file.
                               Texture coordinates are kept.
     PRC_DIAG_ABLATE_HUFFMAN   Huffman coding ignores how often each value
                               occurs: every value present is given the same
                               weight, so the codes come out the same length
                               to within one bit. Where the format leaves the
                               choice to the writer, the array is written
                               without Huffman coding at all, which is also
                               the default (see the next switch). Where it
                               does not (a character array of more than three
                               known values), a Huffman block has to be
                               present, and the equal-length codes are the
                               nearest thing to none.
     PRC_DIAG_HUFFMAN_WHERE_OPTIONAL
                               Huffman-code the arrays whose compression the
                               format leaves to the writer, as the writer did
                               before plain became the default. For comparing
                               the two forms; PRC_DIAG_ABLATE_HUFFMAN
                               overrides it.
     PRC_DIAG_ABLATE_DEFLATE   Store every deflated stream (each file
                               section, and raw pictures) at zlib level 0:
                               still zlib format, so still readable, but not
                               compressed.
     PRC_DIAG_ABLATE_DUMP_DIR  A directory: each section's bytes are also
                               written there, before deflate, one file per
                               section, named prc_<call>_<n>_<section>.bin,
                               where <call> counts the files written by this
                               process. For measuring entropy coders outside
                               nanoPRC.

   Each of the others is on when its variable is set to anything. When any
   is on, the writer says so on stderr once per file, so a measurement cannot
   be taken by mistake from an ablated file. */

#include <stdint.h>
#include <stddef.h>
#include "../include/prc_api.h"

typedef struct prc_write_ablation_s
{
    uint8_t normals;
    uint8_t colors;
    uint8_t textures;
    uint8_t tiny_textures;
    uint8_t huffman;
    uint8_t huffman_where_optional;
    uint8_t deflate;
    const char *dump_dir;       /* NULL: no dump */
} prc_write_ablation;

/* The switches as set now. All off in a build without PRC_ENABLE_DIAG_ENV. */
void prc_write_ablation_get(prc_write_ablation *a);

/* 1 if any switch is on. */
int prc_write_ablation_any(const prc_write_ablation *a);

/* Prints the switches that are on, on one stderr line. */
void prc_write_ablation_report(const prc_write_ablation *a);

/* The caller's tessellation entries with the switched-off channels removed,
   as a copy: the caller's own arrays are never changed. *out is entries
   itself when nothing applies to them. Free with
   prc_write_ablation_free_entries. */
typedef struct prc_write_ablated_entries_s
{
    prc_api_write_tessellation *entries;
    prc_api_write_face_groups *face_groups;
    prc_api_write_tri_group *groups;
} prc_write_ablated_entries;

int prc_write_ablation_apply_entries(prc_context *ctx, const prc_write_ablation *a,
    const prc_api_write_tessellation *entries, uint32_t num_entries,
    prc_write_ablated_entries *copy, const prc_api_write_tessellation **out);
void prc_write_ablation_free_entries(prc_context *ctx, prc_write_ablated_entries *copy);

/* The 2 x 2 image PRC_DIAG_ABLATE_TINY_TEXTURES puts in place of every
   texture: raw RGB, red, green, blue and white. */
#define PRC_WRITE_ABLATION_TINY_SIZE 2u
extern const uint8_t prc_write_ablation_tiny_rgb[PRC_WRITE_ABLATION_TINY_SIZE * PRC_WRITE_ABLATION_TINY_SIZE * 3];

/* Writes one section's bytes to the dump directory. `call` numbers the file
   being written; `index` and `name` the section. A file that cannot be
   written is reported on stderr and does not fail the write. */
void prc_write_ablation_dump_section(const prc_write_ablation *a, uint32_t call, uint32_t index,
    const char *name, const uint8_t *buf, size_t len);

/* Numbers the files written by this process, from 1, for the dump names. */
uint32_t prc_write_ablation_next_call(void);

#endif
