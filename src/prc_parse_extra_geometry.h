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

#ifndef PRC_PARSE_EXTRA_GEOMETRY_H
#define PRC_PARSE_EXTRA_GEOMETRY_H

#include "prc_data.h"
#include "prc_bit.h"

int prc_parse_extra_geometry(prc_context *ctx, prc_bit_state *bit_state, prc_extra_geometry *data);
int prc_parse_exact_geometry(prc_context *ctx, prc_bit_state *bit_state, prc_file_structure_exact_geometry *data);

/* How compressed B-rep vertices and curves are numbered, and whether a
   reference's number has been stored yet. Internal to the parser; declared
   here only so tests/unit/test_ref_numbering.c can reach them. */
int prc_next_ref_number(prc_context *ctx, const prc_nano_brep_compressed_data *compressed_data,
    prc_unsigned_int number_refs, uint32_t *iso_count, uint32_t *ana_count, uint32_t *number);
int prc_ref_number_is_stored(prc_unsigned_int number_refs, uint32_t iso_count, uint32_t ana_count,
    uint32_t number);

#endif
