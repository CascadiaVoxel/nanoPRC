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

#ifndef PRC_APP_ID_H
#define PRC_APP_ID_H

/* nanoPRC's application identifier: the unique_id_application the writer
   puts in the main PRC header and in every file-structure header, where the
   format records which application wrote the file.

   Its 16 bytes, as stored, are the ASCII text "nanoPRC" followed by nine
   zero bytes. Both headers are stored uncompressed, so a search for the text
   "nanoPRC" finds it in a .prc file, and in a PDF whose 3D stream is not
   compressed at the PDF level, as nanoPRC's own PDF writer leaves it.

   A unique identifier is four 32-bit words stored little-endian, so the
   same bytes read as words are:
     "nano" -> 0x6F6E616E,  "PRC\0" -> 0x00435250,  then 0 and 0.

   The value is never all zero: at least one reader rejects an all-zero
   identifier (see the identifiers in prc_write_common.h). */
#define PRC_NANOPRC_APP_UID0 0x6F6E616Eu
#define PRC_NANOPRC_APP_UID1 0x00435250u
#define PRC_NANOPRC_APP_UID2 0u
#define PRC_NANOPRC_APP_UID3 0u

#endif
