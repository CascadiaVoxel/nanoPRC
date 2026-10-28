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

#ifndef PRC_PARSE_GLOBAL_H
#define PRC_PARSE_GLOBAL_H

#include "prc_data.h"
#include "prc_bit.h"

int prc_parse_global_data(prc_context *ctx, prc_bit_state *bit_state, prc_file_struct_internal_global_data *data, prc_file_structure_header *header);
int prc_parse_serialize_help(prc_context *ctx, prc_bit_state *bit_state, prc_markup_serialization_helper *data);
uint8_t *prc_parse_raw_picture_pixels(prc_context *ctx, const uint8_t *block, size_t block_size, size_t image_size);
#endif
