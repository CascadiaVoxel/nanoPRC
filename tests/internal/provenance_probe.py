#!/usr/bin/env python3
# Copyright (C) 2023-2026 CascadiaVoxel LLC
#
# nanoPRC is free software: you can redistribute it and/or modify it under
# the terms of the GNU Affero General Public License as published by the
# Free Software Foundation, either version 3 of the License, or (at your
# option) any later version. See <https://www.gnu.org/licenses/>.

"""Report nanoPRC's provenance mark in a .prc file, or a .pdf whose 3D stream
is stored uncompressed (as nanoPRC's own PDF writer leaves it).

nanoPRC writes a documented mark so a file it produced can be recognised even
if the "nanoPRC" producer text has been edited away (see src/prc_app_id.h):

  - a tag, 0x9E3779B1, in word 0 of the main header's unique_id_file, and
    repeated in the file-structure identity across the stream;
  - a checksum of the section layout in word 1 of that same field.

The main header is uncompressed and begins with the bytes "PRC", so the mark
is at a fixed offset after each signature. This reads sizes and offsets only;
it never looks at geometry or any user content.

A one-line equivalent for the tag alone, for a quick scan of many files:

    grep -l -F -e $'\\xb1\\x79\\x37\\x9e' *.prc

usage: provenance_probe.py <file> [<file> ...]   (exit 0 if any file marked)
"""

import struct
import sys

TAG = 0x9E3779B1
SIG = b"PRC"
# "PRC" (3) + min_version (4) + authoring_version (4), then unique_id_file.
FILE_ID_OFFSET = 3 + 4 + 4
# Within the main header, after unique_id_file (16), unique_id_application
# (16), filestructure_count (4), file_info[0].unique_id (16) and a 4-byte
# reserved field, comes section_count, then the section-offset table (see
# prc_write_main_header_compute_layout in src/prc_write_model.c).
SECTION_COUNT_OFFSET = FILE_ID_OFFSET + 16 + 16 + 4 + 16 + 4


def fnv1a(words):
    """FNV-1a over each 32-bit word, low byte first -- matches
    prc_nanoprc_file_mark_checksum in src/prc_write_common.c."""
    h = 2166136261
    for v in words:
        for b in range(4):
            h ^= (v >> (b * 8)) & 0xFF
            h = (h * 16777619) & 0xFFFFFFFF
    return h


def recompute_checksum(data, sig):
    """The section-layout checksum, recomputed from the main header at `sig`,
    or None if the header runs past the data."""
    at = sig + SECTION_COUNT_OFFSET
    if at + 4 > len(data):
        return None
    count = struct.unpack_from("<I", data, at)[0]
    table = at + 4
    # section_offset[count], then start_offset and end_offset.
    if table + (count + 2) * 4 > len(data) or count > (len(data) // 4):
        return None
    offsets = struct.unpack_from("<%dI" % count, data, table) if count else ()
    start_offset = struct.unpack_from("<I", data, table + count * 4)[0]
    end_offset = struct.unpack_from("<I", data, table + count * 4 + 4)[0]
    return fnv1a((count,) + offsets + (start_offset, end_offset))


def scan(path):
    data = open(path, "rb").read()
    tag_le = struct.pack("<I", TAG)
    raw_hits = data.count(tag_le)

    marked = False
    stored = None
    computed = None
    start = 0
    while True:
        s = data.find(SIG, start)
        if s < 0:
            break
        start = s + 1
        at = s + FILE_ID_OFFSET
        if at + 8 > len(data):
            continue
        if struct.unpack_from("<I", data, at)[0] == TAG:
            marked = True
            stored = struct.unpack_from("<I", data, at + 4)[0]
            computed = recompute_checksum(data, s)
            break

    if marked:
        if computed is None:
            verdict = "checksum 0x%08X (layout not parsed)" % stored
        elif computed == stored:
            verdict = "checksum 0x%08X verified" % stored
        else:
            verdict = ("checksum 0x%08X does NOT match the section layout "
                       "(recomputed 0x%08X) -- edited after writing" % (stored, computed))
        print("%s: nanoPRC (tag 0x%08X, %s, %d tag occurrences in all)"
              % (path, TAG, verdict, raw_hits))
    elif raw_hits:
        print("%s: tag bytes present (%d) but not at a main-header offset -- "
              "inspect" % (path, raw_hits))
    else:
        print("%s: no nanoPRC provenance mark" % path)
    return marked


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    any_marked = False
    for path in argv[1:]:
        try:
            if scan(path):
                any_marked = True
        except OSError as e:
            print("%s: %s" % (path, e))
    return 0 if any_marked else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
