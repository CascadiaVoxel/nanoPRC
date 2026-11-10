# Third-Party Notices

This repository is distributed under the GNU Affero General Public License v3.0.
The components below retain their own upstream licenses and are used as third-party dependencies or embedded source copies.

## Parser Library

- **zlib**, used through the bundled third-party sources, is licensed under the zlib license.
- **stb_image** (`src/stb_image.h`) and **stb_image_write** (`src/stb_image_write.h`) are by Sean Barrett, available under the MIT License or the Unlicense (public domain), at the user's choice. See https://github.com/nothings/stb; the license text is at the end of each file.
- **PDFium** is used in `src/prc_pdf_decrypt.c` for adapted encryption-related methods and is licensed under the BSD 3-Clause License. See `thirdparty/PDFium_LICENSE.txt`.
- `src/prc_pdf_decrypt.c` contains code adapted from PDFium and carries a file-level attribution notice for that reason.

## Viewer Application

- **ImGui** is licensed under the MIT license.
- **MatrixUtil** is Copyright (c) Ethan Vrhel and licensed under the MIT License.
- **SDL** is licensed under the zlib license.
- **stb_image**, **stb_truetype** (`demos/viewer/src/stb_truetype.h`) and **stb_image_write** (`demos/viewer/src/stb_image_write.h`) are by Sean Barrett, available under the MIT License or the Unlicense (public domain), at the user's choice. See https://github.com/nothings/stb; the license text is at the end of each file.
- **GLAD OpenGL Loader** (`demos/viewer/glad/`) is Copyright (c) 2013-2026 David Herberth and licensed under the MIT License.
- **Khronos Platform Header** (`KHR/khrplatform.h`) is Copyright (c) 2008-2018 The Khronos Group Inc. and licensed under an MIT-style permissive license.
- **Roboto Font** (`demos/viewer/src/roboto_font.h`, Roboto version 3.009) is Copyright 2011 The Roboto Project Authors and licensed under the SIL Open Font License, Version 1.1. See `thirdparty/Roboto_OFL.txt`.

## Mobile/WASM Viewer
- **GLM** is licensed under the MIT license. Copyright (c) 2005 - G-Truc Creation

## OBJ Export Demo

- **stb_image_write** (`demos/obj_export/src/stb_image_write.h`) is by Sean Barrett, available under the MIT License or the Unlicense (public domain), at the user's choice. See https://github.com/nothings/stb; the license text is at the end of the file.

---

If you redistribute binaries or source bundles, include the relevant upstream license texts for these dependencies.
