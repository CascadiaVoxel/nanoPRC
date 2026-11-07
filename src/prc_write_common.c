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

#include <string.h>
#include "prc_write_common.h"
#include "prc_app_id.h"

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
prc_write_le_app_unique_id(uint8_t *p)
{
    p = prc_write_le_uint32(p, PRC_NANOPRC_APP_UID0);
    p = prc_write_le_uint32(p, PRC_NANOPRC_APP_UID1);
    p = prc_write_le_uint32(p, PRC_NANOPRC_APP_UID2);
    p = prc_write_le_uint32(p, PRC_NANOPRC_APP_UID3);
    return p;
}

/* Floor is 1e-7 mm (0.1 nm), NOT FLT_MIN: coordinates get divided by the
   tolerance to form int32 grid indices, and dividing any physical coordinate
   by FLT_MIN (~1.17e-38) would produce ~8.5e38 and overflow the grid index.
   1e-7 keeps even a 10 km model within int32 range with margin. */
#define PRC_WRITE_TOL_FLOOR_MM 1e-7

double
prc_write_tol_resolve(prc_context *ctx, prc_write_tolerance tol, double bbox_diagonal)
{
    double tolerance_mm;

    if (tol.mode == PRC_WRITE_TOL_RELATIVE)
    {
        if (bbox_diagonal <= 0.0)
        {
            prc_error(ctx, PRC_ERROR_INTERNAL, "prc_write_tol_resolve: relative tolerance requires a positive bbox diagonal\n");
            return PRC_WRITE_TOL_FLOOR_MM;
        }
        tolerance_mm = tol.value * bbox_diagonal;
    }
    else
    {
        tolerance_mm = tol.value;
    }

    if (tolerance_mm < PRC_WRITE_TOL_FLOOR_MM)
        tolerance_mm = PRC_WRITE_TOL_FLOOR_MM;
    return tolerance_mm;
}
