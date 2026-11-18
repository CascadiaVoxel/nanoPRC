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

/* A compressed tessellation whose triangles are styled separately, with only
   some of them textured: examples/cube.pdf. One side of the six carries a
   picture, repeated three times each way (as Adobe Acrobat draws it); the
   others are plain colours.

   Each of the cube's 8 positions is first reached from a plain side, so every
   corner of the pictured side becomes a split vertex, a second vertex at a
   position already used. A split vertex took its texture coordinates only
   when the whole tessellation's face was textured, which it is not when one
   side of six is, so the pictured side had none and drew as one flat colour.

   Read as a renderer reads it (prc_api_get_face_vertices): exactly four
   vertices have texture coordinates; they are the four corners of one side,
   at four different positions sharing one coordinate; together they span
   u -1..2 and v -0.886..1.886 (the file's texture transform scales the
   stored coordinates by 69.77, and the reader then turns v over); and no
   other vertex has coordinates. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "prc_test.h"
#include "prc_api.h"

/* Set by tests/CMakeLists.txt; the default is for a build run from the
   repository root. */
#ifndef PRC_TEST_EXAMPLES_DIR
#define PRC_TEST_EXAMPLES_DIR "examples"
#endif

#define NEAR(a, b) (fabs((double)(a) - (double)(b)) < 1e-3)

int
main(void)
{
    prc_context *ctx;
    prc_api_data data;
    prc_api_product *tree;
    prc_api_tess tess;
    prc_api_tess line;
    prc_api_vertex *v = NULL;
    uint32_t parts, products, markups, ntess, nline, nexact = 0, nv = 0, i;
    uint8_t has_line = 0;
    float textured[4][5];
    uint32_t ntextured = 0;
    float lo_u = 1e30f, hi_u = -1e30f, lo_v = 1e30f, hi_v = -1e30f;
    int axis, shared = 0;

    PRC_TEST_BEGIN("test_compressed_split_vertex_uv");

    ctx = prc_api_new_context(NULL);
    PRC_ASSERT(ctx != NULL);
    data = prc_api_open_contents(ctx, PRC_TEST_EXAMPLES_DIR "/cube.pdf");
    PRC_ASSERT(data != NULL);
    PRC_ASSERT(prc_api_prep_model_tree(ctx, data, &parts, &products, &markups) >= 0);
    PRC_ASSERT(prc_api_create_model_tree(ctx, data, &tree, parts, products, markups) >= 0);
    PRC_ASSERT(prc_api_get_number_tessellations(ctx, data, tree, &ntess, &nline, &nexact, 0) >= 0);
    PRC_ASSERT_EQ(ntess, 1);
    PRC_ASSERT(prc_api_get_tessellation_type(ctx, data, 0) == PRC_API_TESS_3D_Compressed);

    /* A compressed tessellation is read as one face, as the viewer reads it */
    memset(&tess, 0, sizeof(tess));
    memset(&line, 0, sizeof(line));
    tess.num_faces = 1;
    tess.tess_faces = (prc_api_face *)calloc(1, sizeof(prc_api_face));
    PRC_ASSERT(tess.tess_faces != NULL);
    PRC_ASSERT(prc_api_initialize_tessellation(ctx, data, tree, 0, &tess, &line, &has_line) >= 0);
    PRC_ASSERT(prc_api_get_tessellation_vertices(ctx, data, 0, 0, &tess.tess_faces[0], &tess) >= 0);

    /* The picture itself reaches the face */
    PRC_ASSERT(tess.tess_faces[0].texture.data != NULL);
    PRC_ASSERT_EQ(tess.tess_faces[0].texture.width, 1024);
    PRC_ASSERT_EQ(tess.tess_faces[0].texture.height, 1267);

    PRC_ASSERT(prc_api_get_face_vertices(ctx, &tess, 0, &nv, &v) >= 0);
    PRC_ASSERT_EQ(nv, 24);
    for (i = 0; i < nv; i++)
    {
        if (!v[i].uv_set)
        {
            /* No coordinates, and none left behind */
            PRC_ASSERT(v[i].uv[0] == 0.0f && v[i].uv[1] == 0.0f);
            continue;
        }
        PRC_ASSERT(ntextured < 4);
        memcpy(textured[ntextured], v[i].position, 3 * sizeof(float));
        textured[ntextured][3] = v[i].uv[0];
        textured[ntextured][4] = v[i].uv[1];
        ntextured++;
        if (v[i].uv[0] < lo_u)
            lo_u = v[i].uv[0];
        if (v[i].uv[0] > hi_u)
            hi_u = v[i].uv[0];
        if (v[i].uv[1] < lo_v)
            lo_v = v[i].uv[1];
        if (v[i].uv[1] > hi_v)
            hi_v = v[i].uv[1];
    }
    PRC_ASSERT_EQ(ntextured, 4);

    /* Three repeats each way */
    PRC_ASSERT(NEAR(lo_u, -1.0) && NEAR(hi_u, 2.0));
    PRC_ASSERT(NEAR(lo_v, -0.885616) && NEAR(hi_v, 1.885616));

    /* One side: the four share one coordinate, at four different positions.
       The positions are quantised, so the shared coordinate agrees to within
       0.05 (the cube is 48 across; the side is at x = 48.07 +- 0.006). */
    for (axis = 0; axis < 3; axis++)
    {
        float lo = textured[0][axis], hi = textured[0][axis];

        for (i = 1; i < 4; i++)
        {
            if (textured[i][axis] < lo)
                lo = textured[i][axis];
            if (textured[i][axis] > hi)
                hi = textured[i][axis];
        }
        if (hi - lo < 0.05f)
            shared++;
    }
    PRC_ASSERT_EQ(shared, 1);
    for (i = 0; i < 4; i++)
    {
        uint32_t j;

        for (j = i + 1; j < 4; j++)
            PRC_ASSERT(memcmp(textured[i], textured[j], 3 * sizeof(float)) != 0);
    }

    prc_api_release_data(ctx, data, &tess, 1, NULL, 0, NULL, 0, tree);
    /* prc_api_release_data leaves the face array its caller allocated */
    free(tess.tess_faces);
    PRC_ASSERT_EQ(prc_api_release_context(ctx), 0);
    PRC_TEST_END;
}
