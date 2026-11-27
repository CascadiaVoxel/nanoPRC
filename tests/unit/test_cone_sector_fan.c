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

/* A cone trimmed to a sector (half of it, say) has the apex as a point loop and, as its other
   loop, an arc of the base circle that does not close: the two edges to the apex were taken
   out as the point loop. prc_tessellate_cone_apex_fan_face with open_arc set fans the arc from
   the apex, one triangle per segment of the arc, and stops at its last sample; without
   open_arc it treats the loop as going all the way round and closes the fan. */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "prc_test.h"
#include "prc_api.h"
#include "prc_tessellation_exact_geom.h"

#define PI 3.14159265358979323846
#define SEGMENTS 16

static prc_vec3
tri_cross(prc_vec3 a, prc_vec3 b, prc_vec3 c)
{
    prc_vec3 u, v, w;

    u.x = b.x - a.x; u.y = b.y - a.y; u.z = b.z - a.z;
    v.x = c.x - a.x; v.y = c.y - a.y; v.z = c.z - a.z;
    w.x = u.y * v.z - u.z * v.y;
    w.y = u.z * v.x - u.x * v.z;
    w.z = u.x * v.y - u.y * v.x;
    return w;
}

/* Fans a loop of `n` samples (n - 1 segments) round the cone at v = 0 from u = 0 to sweep */
static int
fan(prc_context *ctx, prc_surf_cone *cone, double apex_v, double sweep, int open_arc,
    prc_exact_geom_tess_data **tess_out, prc_exact_geom_tess **part_out)
{
    prc_data data;
    prc_exact_geom_tess *part;
    prc_exact_geom_shell *shell;
    prc_exact_geom_face *face;
    prc_surface_params params;
    prc_surface_sampling_info info;
    prc_loop_samples point_loop, arc;
    prc_vec2 point_uv[2], arc_uv[SEGMENTS + 1];
    uint32_t i;
    int code;

    memset(&data, 0, sizeof data);
    part = (prc_exact_geom_tess *)calloc(1, sizeof *part);
    shell = (prc_exact_geom_shell *)calloc(1, sizeof *shell);
    face = (prc_exact_geom_face *)calloc(1, sizeof *face);
    part->shells = shell;
    part->number_of_shells = 1;
    shell->faces = face;
    shell->number_of_faces = 1;
    data.exact_geom_tess_part = part;

    memset(&params, 0, sizeof params);
    params.surface_params = cone;
    memset(&info, 0, sizeof info);
    info.precision_u = 0.01;
    info.precision_v = 0.01;
    info.end_u = 2.0 * PI;
    info.end_v = 1.0;

    memset(&point_loop, 0, sizeof point_loop);
    point_uv[0].x = 0.0; point_uv[0].y = apex_v;
    point_uv[1] = point_uv[0];
    point_loop.num_samples = 2;
    point_loop.uv_samples = point_uv;
    point_loop.is_point_loop = 1;

    memset(&arc, 0, sizeof arc);
    for (i = 0; i <= SEGMENTS; i++)
    {
        arc_uv[i].x = sweep * (double)i / (double)SEGMENTS;
        arc_uv[i].y = 0.0;
    }
    arc.num_samples = SEGMENTS + 1;
    arc.uv_samples = arc_uv;

    code = prc_tessellate_cone_apex_fan_face(ctx, &data, 0, 0, 0, 1, prc_evaluate_surf_cone,
        &params, &info, &point_loop, &arc, open_arc);
    *tess_out = face->tess_data;
    *part_out = part;
    free(shell);
    free(face);
    return code;
}

int
main(void)
{
    prc_context *ctx;
    prc_surf_cone cone;
    prc_exact_geom_tess_data *tess = NULL;
    prc_exact_geom_tess *part = NULL;
    double apex_v, area = 0.0, expect;
    uint32_t t, i;
    int all_use_apex = 1;

    PRC_TEST_BEGIN("cone_sector_fan");

    ctx = prc_api_new_context(NULL);
    PRC_ASSERT(ctx != NULL);

    /* A cone of base radius 1 at v = 0, rising at half a unit of radius per unit of v, so
       its apex is at v = -2 and a slant length of sqrt(5) from the base circle */
    memset(&cone, 0, sizeof cone);
    cone.radius = 1.0;
    cone.semi_angle = atan(0.5);
    cone.exact_geom_transform.is_identity = 1;
    apex_v = -2.0;

    /* Half of the cone: an arc from u = 0 to pi, fanned from the apex, one triangle per segment */
    PRC_ASSERT_EQ(fan(ctx, &cone, apex_v, PI, 1, &tess, &part), 0);
    PRC_ASSERT(tess != NULL);
    PRC_ASSERT_EQ(tess->number_of_triangles, SEGMENTS);
    for (t = 0; t < tess->number_of_triangles; t++)
    {
        prc_vec3 p[3], c;
        int uses_apex = 0;

        for (i = 0; i < 3; i++)
        {
            uint32_t idx = tess->triangles[t * 3 + i];

            PRC_ASSERT(idx < tess->number_of_vertices);
            p[i].x = tess->vertices[idx].position[0];
            p[i].y = tess->vertices[idx].position[1];
            p[i].z = tess->vertices[idx].position[2];
            if (fabs(p[i].x) < 1e-6 && fabs(p[i].y) < 1e-6 && fabs(p[i].z - apex_v) < 1e-6)
                uses_apex = 1;
        }
        if (!uses_apex)
            all_use_apex = 0;
        c = tri_cross(p[0], p[1], p[2]);
        area += 0.5 * sqrt(c.x * c.x + c.y * c.y + c.z * c.z);
    }
    PRC_ASSERT(all_use_apex);
    /* Half the lateral area of the cone, pi * 1 * sqrt(5) / 2, less what the chords of the arc
       leave out (about 0.3 per cent for 16 segments) */
    expect = 0.5 * PI * 1.0 * sqrt(5.0);
    PRC_ASSERT(area < expect && area > expect * 0.99);
    prc_free(ctx, tess->vertices);
    prc_free(ctx, tess->triangles);
    prc_free(ctx, tess);
    free(part);

    /* A loop that goes all the way round closes the fan: one triangle per segment, the last
       back to the first sample */
    PRC_ASSERT_EQ(fan(ctx, &cone, apex_v, 2.0 * PI, 0, &tess, &part), 0);
    PRC_ASSERT_EQ(tess->number_of_triangles, SEGMENTS);
    prc_free(ctx, tess->vertices);
    prc_free(ctx, tess->triangles);
    prc_free(ctx, tess);
    free(part);

    PRC_ASSERT_EQ(prc_api_release_context(ctx), 0);
    PRC_TEST_END;
}
