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

/* Provenance mark: a second, documented way to tell that nanoPRC wrote a
   file, meant to survive an edit that removes the "nanoPRC" text above.

   The application identifier and the producer string both read as the
   ASCII text "nanoPRC", so anyone changing one to hide nanoPRC's authorship
   is likely to change the other in the same pass. The mark below is a plain
   32-bit number, not text, and it lives in a different field (the header's
   unique_id_file, which the format provides for a producer's own file
   identity and which viewers do not render), so a search-and-replace on the
   "nanoPRC" text leaves it in place. It is provenance evidence for the AGPL,
   not a protection measure: nanoPRC is free software, this is documented in
   the open, and anyone who reads this header can still remove the mark.

   The tag is 2^32 divided by the golden ratio, a well-known constant, so a
   file carrying it by chance is very unlikely and it is plainly a chosen
   marker rather than noise. The writer puts it in word 0 of:
     - the main header's unique_id_file (with the checksum below in word 1),
     - the file-structure identity (PRC_WRITE_FILE_STRUCT_UID0), which the
       main header, every file-structure header and the model section all
       repeat, so the tag appears several times across the stream.
   It does not depend on the geometry, so every file -- tessellation, wire or
   exact geometry -- carries it the same way.

   The checksum (main header unique_id_file word 1) is an FNV-1a hash of the
   file's section count, section offsets and start/end offsets -- sizes and
   positions only, never geometry or any user content. It corroborates the
   tag: a file copied whole still matches, while unrelated bytes that happen
   to equal the tag almost certainly will not. prc_nanoprc_file_mark_checksum
   in prc_write_common.c computes it; tests/internal/provenance_probe.py
   reads the mark back. */
#define PRC_NANOPRC_FILE_MARK_TAG 0x9E3779B1u

#endif
