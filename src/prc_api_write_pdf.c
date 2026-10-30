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

/* Implementation of the public entry point declared in include/prc_api.h.
   The only embedding path is the standard (ISO 32000) 3D annotation --
   see prc_write_pdf_3d.c for the object graph. */

#include <math.h>
#include <string.h>
#include "../include/prc_api.h"
#include "prc_write_pdf_3d.h"

int
prc_api_pdf_embed_prc(prc_context *ctx, const char *pdf_path,
    const uint8_t *prc_data, size_t prc_size, const prc_pdf_write_options *options)
{
    if (ctx == NULL || pdf_path == NULL || prc_data == NULL || prc_size == 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_api_pdf_embed_prc: invalid arguments\n");
        return PRC_ERROR_INTERNAL;
    }

    return prc_write_pdf_3d_annotation(ctx, pdf_path, prc_data, prc_size, options);
}

void
prc_api_pdf_view_from_bounds(const double min[3], const double max[3], prc_pdf_view_spec *view)
{
    double center[3], d[3], diagonal;
    int a;

    if (min == NULL || max == NULL || view == NULL)
        return;

    for (a = 0; a < 3; a++)
    {
        center[a] = 0.5 * (min[a] + max[a]);
        d[a] = max[a] - min[a];
    }
    diagonal = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    /* A single point still needs the camera some distance away. */
    if (!(diagonal > 0.0))
        diagonal = 1.0;

    /* demos/stl_import's direction and distance, which Adobe Acrobat frames
       correctly; also checked on a wrapped PRC whose parts are placed about
       2,000 units from the origin. */
    memset(view, 0, sizeof(*view));
    view->name = "Default";
    view->eye[0] = center[0] + diagonal * 1.0;
    view->eye[1] = center[1] - diagonal * 1.3;
    view->eye[2] = center[2] + diagonal * 0.7;
    for (a = 0; a < 3; a++)
        view->target[a] = center[a];
    view->up[2] = 1.0;
    view->is_default = 1;
}
