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

/* Segment of exact tessellation work that provides methods for dealing with
   curve and vertex refs */

#include "prc_tessellation_exact_geom.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

static int
prc_get_curve_by_id(prc_context *ctx, prc_nano_brep_compressed_data *compressed_data,
    uint32_t index_compressed_curve, prc_compressed_curve **curve)
{
    if (index_compressed_curve >= compressed_data->current_curve_index)
    {
        prc_error(ctx, PRC_ERROR_PARSE, "Invalid compressed curve index: %u\n", index_compressed_curve);
        return PRC_ERROR_PARSE;
    }
    /* Slots are allocated on demand, so an in-range index can still be null if
       the curve it names was never reached -- a truncated or malformed file.
       Returning null here would push the check onto every caller. */
    if (compressed_data->curves[index_compressed_curve] == NULL)
    {
        prc_error(ctx, PRC_ERROR_PARSE, "Compressed curve %u was never parsed\n",
            index_compressed_curve);
        return PRC_ERROR_PARSE;
    }
    *curve = compressed_data->curves[index_compressed_curve];
    return 0;
}

static int
prc_get_vertex_by_id(prc_context *ctx, prc_nano_brep_compressed_data *compressed_data,
    uint32_t index_compressed_vertex, prc_compressed_vertex **vertex)
{
    if (compressed_data == NULL ||
        index_compressed_vertex >= compressed_data->current_vertex_index)
    {
        prc_error(ctx, PRC_ERROR_PARSE, "Vertex ref struct NULL or invalid compressed vertex index: %u\n", index_compressed_vertex);
        return PRC_ERROR_PARSE;
    }
    /* Vertices are stored at their own numbers, so the numbers between the
       iso and ana ranges hold nothing. */
    if (!compressed_data->vertices[index_compressed_vertex].not_already_stored)
    {
        prc_error(ctx, PRC_ERROR_PARSE, "Compressed vertex %u was never stored\n", index_compressed_vertex);
        return PRC_ERROR_PARSE;
    }
    *vertex = &compressed_data->vertices[index_compressed_vertex];
    return 0;
}

int
prc_get_compressed_curve(prc_context *ctx, prc_ref_or_compressed_curve *ref_or_comp_curve,
    prc_compressed_curve **comp_curve, prc_nano_brep_compressed_data *compressed_data)
{
    int code = 0;

    if (ref_or_comp_curve->curve_is_not_already_stored ||
        ref_or_comp_curve->is_deduced_curve)
    {
        *comp_curve = ref_or_comp_curve->compressed_curve;
    }
    else
    {
        code = prc_get_curve_by_id(ctx, compressed_data,
            ref_or_comp_curve->index_compressed_curve, comp_curve);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed to get curve by id in prc_get_compressed_curve\n");
            return code;
        }
    }
    return 0;
}

int
prc_get_compressed_vertex(prc_context *ctx, prc_compressed_vertex *comp_vertex,
    prc_vec3 *vertex, prc_nano_brep_compressed_data *compressed_data)
{
    int code = 0;

    if (comp_vertex->not_already_stored)
    {
        *vertex = comp_vertex->point_data.point;
        return 0;
    }
    else
    {
        if (compressed_data == NULL)
        {
            prc_error(ctx, PRC_ERROR_PARSE, "Failed to get common vertex by id in prc_get_compressed_point\n");
            return PRC_ERROR_PARSE;
        }
        prc_compressed_vertex *vertex_store = NULL;
        if (compressed_data->use_alt_indexing)
        {
            code = prc_get_vertex_by_id(ctx, compressed_data, comp_vertex->point_index_alt,
                &vertex_store);
        }
        else
        {
            code = prc_get_vertex_by_id(ctx, compressed_data, comp_vertex->point_index,
                &vertex_store);
        }
        if (code < 0)
        {
            prc_error(ctx, code, "Failed to get common vertex by id in prc_get_compressed_point\n");
            return code;
        }
        *vertex = vertex_store->point_data.point;
    }
    return 0;
}

void
prc_get_compressed_point(prc_context *ctx, prc_compressed_point *comp_point,
    prc_vec3 *vertex)
{
    *vertex = comp_point->point;
}

int
prc_get_ptr_vertex(prc_context *ctx, prc_nano_brep_ref_data *brep_ref_data,
    prc_ptr_topology *ptr_topo_in, prc_vec3 *vertex)
{
    prc_topo *topo;

    if (!ptr_topo_in->is_stored)
    {
        topo = ptr_topo_in->topo;
    }
    else
    {
        if (ptr_topo_in->topo_identifier > brep_ref_data->number_of_topo_refs ||
            ptr_topo_in->topo_identifier == 0)
        {
            prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid topo identifier in prc_get_ptr_vertex\n");
            return PRC_ERROR_INTERNAL;
        }
        /* These are biased I believe */
        topo = brep_ref_data->topo_refs[ptr_topo_in->topo_identifier - 1];
    }

    /* I think this could be PRC_TYPE_TOPO_MultipleVertex also */
    if (topo->tag != PRC_TYPE_TOPO_UniqueVertex)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid topo identifier in prc_get_ptr_vertex\n");
        return PRC_ERROR_INTERNAL;
    }

    *vertex = topo->topo_unique_vertex->vertex;
    return 0;
}

int
prc_get_start_end_data(prc_context *ctx, prc_start_end_data *data,
    prc_vec3 *start, prc_vec3 *end, prc_nano_brep_compressed_data *compressed_data)
{
    int code = 0;

    if (data->is_vertex)
    {
        code = prc_get_compressed_vertex(ctx, &data->start_vertex, start, compressed_data);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed to get start vertex by id in prc_get_start_end_data\n");
            return code;
        }
        code = prc_get_compressed_vertex(ctx, &data->end_vertex, end, compressed_data);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed to get end vertex by id in prc_get_start_end_data\n");
            return code;
        }
    }
    else
    {
        prc_get_compressed_point(ctx, &data->start_point, start);
        prc_get_compressed_point(ctx, &data->end_point, end);
    }
    return 0;
}
