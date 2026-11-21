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

#include <stdlib.h>
#include <string.h>
#include "prc_write_common.h"
#include "prc_app_id.h"
#include "prc_diag_env.h"

/* A Name: same_name TRUE means "the current name" of clause 4.4, the last
   name written in this section (NULL at its start), not "no name". So TRUE
   is written only when name is the current name, both NULL or both the same
   text. Otherwise the name follows, a NULL name as a null string, and
   becomes the current name. Writing TRUE for every NULL name, as this did,
   gave an unnamed entity the name of whatever was named before it. */
int
prc_write_name(prc_context *ctx, prc_bit_write_state *s, const char *name)
{
    prc_string str;

    if ((name == NULL && s->curr_name == NULL)
        || (name != NULL && s->curr_name != NULL && strcmp(name, s->curr_name) == 0))
        return prc_bitwrite_bit(ctx, s, 1); /* same_name: the current name */

    if (prc_bitwrite_bit(ctx, s, 0) != 0) return -1; /* same_name = 0: a name follows */
    memset(&str, 0, sizeof(str));
    if (name != NULL)
    {
        /* prc_bitwrite_string only reads through this pointer. */
        str.string = (unsigned char *)name;
        str.size = (prc_unsigned_int)strlen(name);
    }
    if (prc_bitwrite_string(ctx, s, &str) != 0) return -1;

    /* The current name is now this one; keep a copy. */
    if (s->curr_name != NULL)
        prc_free(ctx, s->curr_name);
    s->curr_name = NULL;
    if (name != NULL)
    {
        size_t n = strlen(name);

        s->curr_name = (char *)prc_malloc(ctx, n + 1);
        if (s->curr_name == NULL)
        {
            prc_error(ctx, PRC_ERROR_MEMORY, "Allocation error in prc_write_name\n");
            s->error = 1;
            return -1;
        }
        memcpy(s->curr_name, name, n + 1);
    }
    return 0;
}

uint8_t *
prc_write_le_uint32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)((v >> 24) & 0xffu);
    return p + 4;
}

uint8_t *
prc_write_le_unique_id(uint8_t *p, uint32_t word0)
{
    p = prc_write_le_uint32(p, word0);
    p = prc_write_le_uint32(p, 0);
    p = prc_write_le_uint32(p, 0);
    p = prc_write_le_uint32(p, 0);
    return p;
}

uint8_t *
prc_write_le_unique_id2(uint8_t *p, uint32_t word0, uint32_t word1)
{
    p = prc_write_le_uint32(p, word0);
    p = prc_write_le_uint32(p, word1);
    p = prc_write_le_uint32(p, 0);
    p = prc_write_le_uint32(p, 0);
    return p;
}

/* FNV-1a over four bytes, low byte first. */
static uint32_t
prc_fnv1a_u32(uint32_t h, uint32_t v)
{
    int b;
    for (b = 0; b < 4; b++)
    {
        h ^= (v >> (b * 8)) & 0xffu;
        h *= 16777619u;
    }
    return h;
}

uint32_t
prc_nanoprc_file_mark_checksum(uint32_t section_count,
    const uint32_t *section_offsets, uint32_t start_offset, uint32_t end_offset)
{
    uint32_t h = 2166136261u; /* FNV-1a offset basis */
    uint32_t i;

    h = prc_fnv1a_u32(h, section_count);
    for (i = 0; i < section_count; i++)
        h = prc_fnv1a_u32(h, section_offsets != NULL ? section_offsets[i] : 0);
    h = prc_fnv1a_u32(h, start_offset);
    h = prc_fnv1a_u32(h, end_offset);
    return h;
}

uint8_t *
prc_write_le_app_unique_id(uint8_t *p)
{
    p = prc_write_le_uint32(p, PRC_NANOPRC_APP_UID0);
    p = prc_write_le_uint32(p, PRC_NANOPRC_APP_UID1);
    p = prc_write_le_uint32(p, PRC_NANOPRC_APP_UID2);
    p = prc_write_le_uint32(p, PRC_NANOPRC_APP_UID3);
    return p;
}

/* The compressed encoder stores each position as a whole number of
   tolerance steps from the chain origin (the bounding box's corner) or from
   a predicted position, in an int32, and refuses a delta outside that range
   (prc_encode_quantize). Those deltas stay within about twice the bounding
   box diagonal, so a tolerance of at least the diagonal times 2^-28 keeps
   them within 2^29 steps, a quarter of the int32 range. That is below what
   float32 input can resolve (about 6e-8 of a coordinate), so any tolerance
   a caller can mean is used as asked.

   The floor used to be 1e-7 whatever the model's size. A scan measured in
   metres, 0.0265 across, asked for 1e-6 of its diagonal, 2.65e-8, and was
   quantized at 1e-7 instead, almost four times as coarse; scans about 0.25
   across were coarsened below about 4e-7 of the diagonal. */
#define PRC_WRITE_TOL_FLOOR_FRACTION (1.0 / 268435456.0) /* 2^-28 */
/* Only for a mesh with no extent, where any positive step will do */
#define PRC_WRITE_TOL_MIN_MM 1e-30

double
prc_write_tol_resolve(prc_context *ctx, prc_write_tolerance tol, double bbox_diagonal)
{
    double tolerance_mm, floor_mm;

    floor_mm = (bbox_diagonal > 0.0) ? bbox_diagonal * PRC_WRITE_TOL_FLOOR_FRACTION : PRC_WRITE_TOL_MIN_MM;
    if (floor_mm < PRC_WRITE_TOL_MIN_MM)
        floor_mm = PRC_WRITE_TOL_MIN_MM;

    if (tol.mode == PRC_WRITE_TOL_RELATIVE)
    {
        if (bbox_diagonal <= 0.0)
        {
            prc_error(ctx, PRC_ERROR_INTERNAL, "prc_write_tol_resolve: relative tolerance requires a positive bbox diagonal\n");
            return floor_mm;
        }
        tolerance_mm = tol.value * bbox_diagonal;
    }
    else
    {
        tolerance_mm = tol.value;
    }

    if (!(tolerance_mm >= floor_mm))
        tolerance_mm = floor_mm;
    return tolerance_mm;
}

/* See prc_write_common.h. Always 0 unless the build enables the diagnostic
   environment hooks and PRC_DIAG_TEST_SCHEMA_VERSION is set to a number. */
uint32_t
prc_write_diag_test_schema_version(void)
{
    const char *ov = prc_diag_getenv("PRC_DIAG_TEST_SCHEMA_VERSION");
    return ov != NULL ? (uint32_t)strtoul(ov, NULL, 10) : 0u;
}

int
prc_write_diag_test_schema_on_transform(void)
{
    const char *ov = prc_diag_getenv("PRC_DIAG_TEST_SCHEMA_ENTITY");
    return ov != NULL && strcmp(ov, "transform") == 0;
}
