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

/* Segment of exact tessellation work that converts ana and iso surfaces
   to non-compressed forms */

#include "prc_tessellation_exact_geom.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

static double
prc_get_hcg_circle_radius(const prc_hcg_circle_information *info)
{
    prc_vec3 center_to_point;
    double radius = 0.0;

    if (!info->has_center)
    {
        return 0.0;
    }

    if (info->has_start_end_points)
    {
        prc_vec_sub(info->start_point, info->center, &center_to_point);
        radius = prc_vec_length_sqrt(center_to_point);
        if (radius > 0.0)
        {
            return radius;
        }
        prc_vec_sub(info->end_point, info->center, &center_to_point);
        radius = prc_vec_length_sqrt(center_to_point);
        if (radius > 0.0)
        {
            return radius;
        }
    }

    if (info->has_middle_of_arc_point)
    {
        prc_vec_sub(info->middle_of_arc_point, info->center, &center_to_point);
        radius = prc_vec_length_sqrt(center_to_point);
        if (radius > 0.0)
        {
            return radius;
        }
    }

    return 0.0;
}

static int
prc_build_iso_cylinder_from_circle_data(prc_context *ctx,
    const prc_hcg_circle_information *circle_info,
    const prc_vec3 *line_start,
    const prc_vec3 *line_end,
    const prc_vec3 *common_vertex,
    prc_surf_cylinder *cylinder)
{
    prc_vec3 axis;
    prc_vec3 center;
    prc_vec3 radial_vec;
    prc_vec3 x_axis;
    prc_vec3 y_axis;
    prc_vec3 ref_axis;
    prc_vec3 line_dir;
    double radius = 0.0;
    int code;

    if (circle_info == NULL || cylinder == NULL)
    {
        prc_error(ctx, PRC_ERROR_PARSE, "Invalid cylinder circle data in prc_build_iso_cylinder_from_circle_data\n");
        return PRC_ERROR_PARSE;
    }
    if (!circle_info->has_center)
    {
        prc_error(ctx, PRC_ERROR_PARSE,
            "Missing cylinder circle center in prc_build_iso_cylinder_from_circle_data\n");
        return PRC_ERROR_PARSE;
    }

    memset(cylinder, 0, sizeof(*cylinder));
    center = circle_info->center;

    if (circle_info->has_normal)
    {
        axis = circle_info->normal;
    }
    else if (line_start != NULL && line_end != NULL)
    {
        prc_vec_sub(*line_end, *line_start, &line_dir);
        axis = line_dir;
    }
    else
    {
        axis.x = 0.0;
        axis.y = 0.0;
        axis.z = 1.0;
    }
    code = prc_vec_normalize(&axis);
    if (code < 0)
    {
        prc_error(ctx, code, "Degenerate cylinder axis in prc_build_iso_cylinder_from_circle_data\n");
        return code;
    }

    if (common_vertex != NULL)
    {
        prc_vec_sub(*common_vertex, center, &radial_vec);
    }
    else if (line_start != NULL && line_end != NULL)
    {
        prc_vec3 line_mid;
        prc_vec_avg(*line_start, *line_end, &line_mid);
        prc_vec_sub(line_mid, center, &radial_vec);
    }
    else if (circle_info->has_start_end_points)
    {
        prc_vec_sub(circle_info->start_point, center, &radial_vec);
    }
    else
    {
        radial_vec.x = 1.0;
        radial_vec.y = 0.0;
        radial_vec.z = 0.0;
    }

    if (prc_vec_length_sqrt(radial_vec) > 0.0)
    {
        double axial_component = prc_vec_dot_product(radial_vec, axis);
        prc_vec3 axial_vec = axis;
        prc_vec_scale(axial_component, &axial_vec);
        prc_vec_sub(radial_vec, axial_vec, &radial_vec);
        radius = prc_vec_length_sqrt(radial_vec);
    }
    if (radius <= 0.0 && circle_info->has_start_end_points)
    {
        prc_vec_sub(circle_info->start_point, center, &radial_vec);
        if (prc_vec_length_sqrt(radial_vec) > 0.0)
        {
            double axial_component = prc_vec_dot_product(radial_vec, axis);
            prc_vec3 axial_vec = axis;
            prc_vec_scale(axial_component, &axial_vec);
            prc_vec_sub(radial_vec, axial_vec, &radial_vec);
            radius = prc_vec_length_sqrt(radial_vec);
        }
    }
    if (radius <= 0.0 && circle_info->has_middle_of_arc_point)
    {
        prc_vec_sub(circle_info->middle_of_arc_point, center, &radial_vec);
        if (prc_vec_length_sqrt(radial_vec) > 0.0)
        {
            double axial_component = prc_vec_dot_product(radial_vec, axis);
            prc_vec3 axial_vec = axis;
            prc_vec_scale(axial_component, &axial_vec);
            prc_vec_sub(radial_vec, axial_vec, &radial_vec);
            radius = prc_vec_length_sqrt(radial_vec);
        }
    }
    if (radius <= 0.0)
    {
        prc_error(ctx, PRC_ERROR_PARSE, "Invalid cylinder radius in prc_build_iso_cylinder_from_circle_data\n");
        return PRC_ERROR_PARSE;
    }

    if (prc_vec_length_sqrt(radial_vec) > 0.0)
    {
        prc_vec_scale(1.0 / prc_vec_length_sqrt(radial_vec), &radial_vec);
        x_axis = radial_vec;
    }
    else
    {
        if (fabs(axis.z) > 0.9)
        {
            ref_axis.x = 1.0;
            ref_axis.y = 0.0;
            ref_axis.z = 0.0;
        }
        else
        {
            ref_axis.x = 0.0;
            ref_axis.y = 0.0;
            ref_axis.z = 1.0;
        }
        prc_vec_cross(ref_axis, axis, &x_axis);
        code = prc_vec_normalize(&x_axis);
        if (code < 0)
        {
            x_axis.x = 1.0;
            x_axis.y = 0.0;
            x_axis.z = 0.0;
        }
    }

    prc_vec_cross(axis, x_axis, &y_axis);
    code = prc_vec_normalize(&y_axis);
    if (code < 0)
    {
        y_axis.x = 0.0;
        y_axis.y = 1.0;
        y_axis.z = 0.0;
    }

    cylinder->tag = PRC_TYPE_SURF_Cylinder;
    cylinder->has_transform = 1;
    cylinder->transform.behavior = PRC_TRANSFORMATION_Translate | PRC_TRANSFORMATION_Rotate;
    cylinder->transform.translation = center;
    cylinder->transform.rotation[0] = x_axis;
    cylinder->transform.rotation[1] = y_axis;
    cylinder->transform.scale = 1.0;
    cylinder->radius = radius;
    cylinder->parameterization.swap_uv = 0;
    cylinder->parameterization.surface_domain.min_uv.x = 0.0;
    cylinder->parameterization.surface_domain.min_uv.y = 0.0;
    cylinder->parameterization.surface_domain.max_uv.x = 2.0 * PRC_PI;
    cylinder->parameterization.surface_domain.max_uv.y = 1.0;

    if (prc_exact_geom_set_transform(ctx, &cylinder->exact_geom_transform, &cylinder->transform) < 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Failed to initialize cylinder transform in prc_build_iso_cylinder_from_circle_data\n");
        return PRC_ERROR_INTERNAL;
    }

    return 0;
}

static void
prc_compute_circle_angle_basis(const prc_vec3 *plane_normal, prc_vec3 *u_axis, prc_vec3 *v_axis)
{
    prc_vec3 ref_axis;
    prc_vec3 normal = *plane_normal;

    if (fabs(normal.z) > 0.9)
    {
        ref_axis.x = 1.0;
        ref_axis.y = 0.0;
        ref_axis.z = 0.0;
    }
    else
    {
        ref_axis.x = 0.0;
        ref_axis.y = 0.0;
        ref_axis.z = 1.0;
    }

    prc_vec_cross(ref_axis, normal, u_axis);
    prc_vec_normalize(u_axis);
    prc_vec_cross(normal, *u_axis, v_axis);
    prc_vec_normalize(v_axis);
}

static double
prc_compute_circle_angle(const prc_vec3 *center, const prc_vec3 *plane_normal,
    const prc_vec3 *point)
{
    prc_vec3 u_axis;
    prc_vec3 v_axis;
    prc_vec3 rel;
    double x;
    double y;

    prc_compute_circle_angle_basis(plane_normal, &u_axis, &v_axis);
    prc_vec_sub(*point, *center, &rel);
    x = prc_vec_dot_product(rel, u_axis);
    y = prc_vec_dot_product(rel, v_axis);
    return atan2(y, x);
}

static void
prc_normalize_angle_range(double *min_angle, double *max_angle)
{
    double span = *max_angle - *min_angle;

    if (span < 0.0)
    {
        span += 2.0 * PRC_PI;
        *max_angle = *min_angle + span;
    }
    if (span <= 0.0)
    {
        *max_angle = *min_angle + 2.0 * PRC_PI;
    }
}

static int
prc_build_iso_torus_from_circle_data(prc_context *ctx,
    const prc_hcg_circle_information *major_radius_circle_info,
    const prc_hcg_circle_information *minor_radius_circle_info,
    const prc_vec3 *common_vertex, prc_surf_torus *torus)
{
    prc_vec3 torus_center;
    prc_vec3 torus_axis;
    prc_vec3 minor_center_to_major_center;
    prc_vec3 major_center_to_common;
    prc_vec3 torus_reference;
    prc_vec3 x_axis;
    prc_vec3 y_axis;
    double major_radius;
    double minor_radius;
    double u_min = 0.0;
    double u_max = 2.0 * PRC_PI;
    double v_min = 0.0;
    double v_max = 2.0 * PRC_PI;
    int code;

    if (major_radius_circle_info == NULL || minor_radius_circle_info == NULL || torus == NULL)
    {
        prc_error(ctx, PRC_ERROR_PARSE, "Invalid torus circle data in prc_build_iso_torus_from_circle_data\n");
        return PRC_ERROR_PARSE;
    }
    if (!major_radius_circle_info->has_center || !minor_radius_circle_info->has_center)
    {
        prc_error(ctx, PRC_ERROR_PARSE,
            "Missing torus circle center: a zero/PI/2PI compressed circle cannot define a torus without a resolvable center\n");
        return PRC_ERROR_PARSE;
    }

    memset(torus, 0, sizeof(*torus));
    torus_center = major_radius_circle_info->center;
    torus->tag = PRC_TYPE_SURF_Torus;
    torus->has_transform = 1;
    torus->transform.behavior = PRC_TRANSFORMATION_Translate | PRC_TRANSFORMATION_Rotate;
    torus->transform.translation = torus_center;
    torus->transform.scale = 1.0;

    if (major_radius_circle_info->has_normal)
    {
        torus_axis = major_radius_circle_info->normal;
    }
    else if (common_vertex != NULL)
    {
        prc_vec_sub(minor_radius_circle_info->center, major_radius_circle_info->center, &minor_center_to_major_center);
        prc_vec_sub(*common_vertex, major_radius_circle_info->center, &major_center_to_common);
        prc_vec_cross(minor_center_to_major_center, major_center_to_common, &torus_axis);
    }
    else
    {
        torus_axis.x = 0.0;
        torus_axis.y = 0.0;
        torus_axis.z = 1.0;
    }
    code = prc_vec_normalize(&torus_axis);
    if (code < 0)
    {
        prc_error(ctx, code, "Degenerate torus axis in prc_build_iso_torus_from_circle_data\n");
        return code;
    }

    major_radius = prc_get_hcg_circle_radius(major_radius_circle_info);
    minor_radius = prc_get_hcg_circle_radius(minor_radius_circle_info);
    if (major_radius <= 0.0 || minor_radius <= 0.0)
    {
        if (common_vertex != NULL)
        {
            prc_vec_sub(*common_vertex, torus_center, &major_center_to_common);
            if (prc_vec_length_sqrt(major_center_to_common) > 0.0)
            {
                major_radius = prc_vec_length_sqrt(major_center_to_common);
            }
        }
        if (major_radius <= 0.0 || minor_radius <= 0.0)
        {
            prc_error(ctx, PRC_ERROR_PARSE, "Invalid torus radii in prc_build_iso_torus_from_circle_data\n");
            return PRC_ERROR_PARSE;
        }
    }
    torus->major_radius = major_radius;
    torus->minor_radius = minor_radius;

    if (common_vertex != NULL)
    {
        prc_vec_sub(minor_radius_circle_info->center, torus_center, &minor_center_to_major_center);
        if (prc_vec_length_sqrt(minor_center_to_major_center) > 0.0)
        {
            prc_vec_scale(1.0 / prc_vec_length_sqrt(minor_center_to_major_center), &minor_center_to_major_center);
            x_axis = minor_center_to_major_center;
        }
        else
        {
            prc_vec_sub(*common_vertex, torus_center, &major_center_to_common);
            if (prc_vec_length_sqrt(major_center_to_common) > 0.0)
            {
                prc_vec_scale(1.0 / prc_vec_length_sqrt(major_center_to_common), &major_center_to_common);
                x_axis = major_center_to_common;
            }
            else
            {
                x_axis.x = 1.0;
                x_axis.y = 0.0;
                x_axis.z = 0.0;
            }
        }
    }
    else
    {
        x_axis.x = 1.0;
        x_axis.y = 0.0;
        x_axis.z = 0.0;
    }

    if (fabs(torus_axis.z) > 0.9)
    {
        torus_reference.x = 1.0;
        torus_reference.y = 0.0;
        torus_reference.z = 0.0;
    }
    else
    {
        torus_reference.x = 0.0;
        torus_reference.y = 0.0;
        torus_reference.z = 1.0;
    }

    prc_vec_cross(torus_reference, torus_axis, &x_axis);
    code = prc_vec_normalize(&x_axis);
    if (code < 0)
    {
        x_axis.x = 1.0;
        x_axis.y = 0.0;
        x_axis.z = 0.0;
        code = prc_vec_normalize(&x_axis);
        if (code < 0)
        {
            return code;
        }
    }
    prc_vec_cross(torus_axis, x_axis, &y_axis);
    code = prc_vec_normalize(&y_axis);
    if (code < 0)
    {
        y_axis.x = 0.0;
        y_axis.y = 1.0;
        y_axis.z = 0.0;
    }

    torus->transform.rotation[0] = x_axis;
    torus->transform.rotation[1] = y_axis;
    torus->parameterization.swap_uv = 0;

    if (major_radius_circle_info->has_start_end_points && major_radius_circle_info->has_center)
    {
        double theta_start = prc_compute_circle_angle(&major_radius_circle_info->center, &torus_axis,
            &major_radius_circle_info->start_point);
        double theta_end = prc_compute_circle_angle(&major_radius_circle_info->center, &torus_axis,
            &major_radius_circle_info->end_point);
        u_min = theta_start;
        u_max = theta_end;
        prc_normalize_angle_range(&u_min, &u_max);
    }
    if (minor_radius_circle_info->has_start_end_points && minor_radius_circle_info->has_center)
    {
        double theta_start = prc_compute_circle_angle(&minor_radius_circle_info->center, &torus_axis,
            &minor_radius_circle_info->start_point);
        double theta_end = prc_compute_circle_angle(&minor_radius_circle_info->center, &torus_axis,
            &minor_radius_circle_info->end_point);
        v_min = theta_start;
        v_max = theta_end;
        prc_normalize_angle_range(&v_min, &v_max);
    }
    if (major_radius_circle_info->is_full_circle)
    {
        u_min = 0.0;
        u_max = 2.0 * PRC_PI;
    }
    if (minor_radius_circle_info->is_full_circle)
    {
        v_min = 0.0;
        v_max = 2.0 * PRC_PI;
    }

    torus->parameterization.surface_domain.min_uv.x = u_min;
    torus->parameterization.surface_domain.min_uv.y = v_min;
    torus->parameterization.surface_domain.max_uv.x = u_max;
    torus->parameterization.surface_domain.max_uv.y = v_max;

    if (prc_exact_geom_set_transform(ctx, &torus->exact_geom_transform, &torus->transform) < 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Failed to initialize torus transform in prc_build_iso_torus_from_circle_data\n");
        return PRC_ERROR_INTERNAL;
    }

    return 0;
}

/* In the ISO surface case add the compressed curve as a coedge */
static int
prc_convert_comp_curve_to_coedge(prc_context *ctx,
    prc_compressed_curve *comp_curve, prc_coedge_in_loop *coedge,
    uint8_t coedge_orientation, uint8_t curve_deduced,
    prc_nano_brep_compressed_data *compressed_data)
{
    int code;
    prc_topo_coedge *topo_coedge = NULL;
    prc_topo_single_wire_compress *topo_single_wire_compress = NULL;

    coedge->next_coedge.is_stored = 0;
    coedge->next_coedge.topo = (prc_topo *)prc_calloc(ctx, 1, sizeof(prc_topo));
    if (coedge->next_coedge.topo == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate memory for coedge->next_coedge.topo\n");
        return PRC_ERROR_MEMORY;
    }
    coedge->next_coedge.topo->tag = PRC_TYPE_TOPO_CoEdge;
    coedge->next_coedge.topo->topo_coedge = (prc_topo_coedge *)prc_calloc(ctx, 1, sizeof(prc_topo_coedge));
    if (coedge->next_coedge.topo->topo_coedge == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate memory for coedge->next_coedge.topo->topo_coedge\n");
        return PRC_ERROR_MEMORY;
    }

    topo_coedge = coedge->next_coedge.topo->topo_coedge;
    topo_coedge->tag = PRC_TYPE_TOPO_CoEdge;
    topo_coedge->coedge_orientation = coedge_orientation; /* 1 means it is correct. 0 is reversed */
    topo_coedge->ptr_topology.is_stored = 0;
    topo_coedge->ptr_topology.topo = (prc_topo *)prc_calloc(ctx, 1, sizeof(prc_topo));
    if (topo_coedge == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate memory for topo_coedge->ptr_topology.topo\n");
        return PRC_ERROR_MEMORY;
    }

    /* Set this one to the compressed wire type. This should be enough. No need
       to convert this to a non-compressed form as we already support the compressed
       curve sampling */
    topo_coedge->ptr_topology.topo->tag = PRC_TYPE_TOPO_SingleWireBodyCompress;

    topo_coedge->ptr_topology.topo->topo_single_wire_compress =
        (prc_topo_single_wire_compress *)prc_calloc(ctx, 1, sizeof(prc_topo_single_wire_compress));
    if (topo_coedge->ptr_topology.topo->topo_single_wire_compress == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate memory for topo_single_wire_compress\n");
        return PRC_ERROR_MEMORY;
    }
    topo_single_wire_compress = topo_coedge->ptr_topology.topo->topo_single_wire_compress;
    topo_single_wire_compress->tag = PRC_TYPE_TOPO_SingleWireBodyCompress;
    topo_single_wire_compress->ref_or_compressed_curve.curve_is_not_already_stored = 1;

    if (curve_deduced)
    {
        /* In this case the incoming curve was on the stack and we need to allocate
           copy and then free later */
        prc_compressed_curve *new_curve = (prc_compressed_curve *)prc_calloc(ctx, 1, sizeof(prc_compressed_curve));
        if (new_curve == NULL)
        {
            prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate memory for prc_compressed_curve\n");
            return PRC_ERROR_MEMORY;
        }
        memcpy(new_curve, comp_curve, sizeof(prc_compressed_curve));
        topo_single_wire_compress->ref_or_compressed_curve.compressed_curve = new_curve;
        topo_single_wire_compress->ref_or_compressed_curve.is_deduced_curve = 1;
    }
    else
    {
        topo_single_wire_compress->ref_or_compressed_curve.compressed_curve = comp_curve;
        topo_single_wire_compress->ref_or_compressed_curve.is_deduced_curve = 0;
    }
    topo_single_wire_compress->ref_data = compressed_data;

    return 0;
}


/* In the ANA surface case add the compressed curve as a coedge */
static int
prc_convert_compref_curve_to_coedge(prc_context *ctx,
    prc_ref_or_compressed_curve *comp_curve, prc_coedge_in_loop *coedge,
    prc_nano_brep_compressed_data *compressed_data, prc_exact_geom_wire_data *wire_data)
{
    int code;
    prc_compressed_curve *curve = NULL;
    prc_topo_coedge *topo_coedge = NULL;
    prc_topo_single_wire_compress *topo_single_wire_compress = NULL;

    coedge->next_coedge.is_stored = 0;
    coedge->next_coedge.topo = (prc_topo *)prc_calloc(ctx, 1, sizeof(prc_topo));
    if (coedge->next_coedge.topo == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate memory for coedge->next_coedge.topo\n");
        return PRC_ERROR_MEMORY;
    }
    coedge->next_coedge.topo->tag = PRC_TYPE_TOPO_CoEdge;
    coedge->next_coedge.topo->topo_coedge = (prc_topo_coedge *)prc_calloc(ctx, 1, sizeof(prc_topo_coedge));
    if (coedge->next_coedge.topo->topo_coedge == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate memory for coedge->next_coedge.topo->topo_coedge\n");
        return PRC_ERROR_MEMORY;
    }

    topo_coedge = coedge->next_coedge.topo->topo_coedge;
    topo_coedge->tag = PRC_TYPE_TOPO_CoEdge;
    topo_coedge->coedge_orientation = 1;
    topo_coedge->ptr_topology.is_stored = 0;
    topo_coedge->ptr_topology.topo = (prc_topo *)prc_calloc(ctx, 1, sizeof(prc_topo));
    if (topo_coedge == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate memory for topo_coedge->ptr_topology.topo\n");
        return PRC_ERROR_MEMORY;
    }

    /* Set this one to the compressed wire type. This should be enough. No need
       to convert this to a non-compressed form as we already support the compressed
       curve sampling */
    topo_coedge->ptr_topology.topo->tag = PRC_TYPE_TOPO_SingleWireBodyCompress;

    topo_coedge->ptr_topology.topo->topo_single_wire_compress =
        (prc_topo_single_wire_compress *)prc_calloc(ctx, 1, sizeof(prc_topo_single_wire_compress));
    if (topo_coedge->ptr_topology.topo->topo_single_wire_compress == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate memory for topo_single_wire_compress\n");
        return PRC_ERROR_MEMORY;
    }
    topo_single_wire_compress = topo_coedge->ptr_topology.topo->topo_single_wire_compress;
    topo_single_wire_compress->tag = PRC_TYPE_TOPO_SingleWireBodyCompress;
    topo_single_wire_compress->ref_or_compressed_curve = *comp_curve;
    topo_single_wire_compress->ref_data = compressed_data;

    if (wire_data != NULL)
    {
        code = prc_get_compressed_curve(ctx, comp_curve, &curve, compressed_data);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed to get compressed curve in prc_convert_comp_curve_to_coedge\n");
            return code;
        }
        code = prc_sample_compressed_curve(ctx, NULL, curve, 0, wire_data, compressed_data);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed to sample compressed curve in prc_convert_comp_curve_to_coedge\n");
            return code;
        }
    }

    return 0;
}

/* In the ISO surface case, add the four curves to a loop */
static int
prc_convert_iso_curves_to_std_loop(prc_context *ctx, prc_compressed_curve **curves,
    prc_ptr_topology *synthetic_loop, uint8_t *coedge_orientation, uint8_t *curve_deduced,
    prc_nano_brep_compressed_data *compressed_data)
{
    int code;
    uint32_t curve_count = 4;
    uint32_t j;

    synthetic_loop->is_stored = 0;
    synthetic_loop->topo = (prc_topo *)prc_calloc(ctx, 1, sizeof(prc_topo));
    if (synthetic_loop->topo == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate memory for synthetic_loop->topo\n");
        return PRC_ERROR_MEMORY;
    }

    synthetic_loop->topo->tag = PRC_TYPE_TOPO_Loop;
    synthetic_loop->topo->topo_loop = (prc_topo_loop *)prc_calloc(ctx, 1, sizeof(prc_topo_loop));
    if (synthetic_loop->topo->topo_loop == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate memory for synthetic_loop->topo->topo_loop\n");
        return PRC_ERROR_MEMORY;
    }

    synthetic_loop->topo->topo_loop->number_of_coedges = curve_count;
    synthetic_loop->topo->topo_loop->coedge = (prc_coedge_in_loop *)prc_calloc(ctx, curve_count, sizeof(prc_coedge_in_loop));
    if (synthetic_loop->topo->topo_loop->coedge == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate memory for synthetic_loop->topo->topo_loop->coedge\n");
        return PRC_ERROR_MEMORY;
    }

    for (j = 0; j < curve_count; ++j)
    {
        prc_coedge_in_loop *coedge = &synthetic_loop->topo->topo_loop->coedge[j];
        code = prc_convert_comp_curve_to_coedge(ctx, curves[j], coedge,
            coedge_orientation[j], curve_deduced[j], compressed_data);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed to convert ana loop to std loop in prc_convert_comp_curve_to_coedge\n");
            return code;
        }
    }
    return 0;
}

static int
prc_convert_compressed_loop_to_std_loop(prc_context *ctx, prc_ana_face_trim_loop *ana_loop,
    prc_ptr_topology *synthetic_loop,
    prc_nano_brep_compressed_data *compressed_data, prc_exact_geom_wire_data *wire_data)
{
    int code;
    uint32_t j;
    uint32_t curve_count = ana_loop->curve_count;

    synthetic_loop->is_stored = 0;
    synthetic_loop->topo = (prc_topo *)prc_calloc(ctx, 1, sizeof(prc_topo));
    if (synthetic_loop->topo == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate memory for synthetic_loop->topo\n");
        return PRC_ERROR_MEMORY;
    }

    synthetic_loop->topo->tag = PRC_TYPE_TOPO_Loop;
    synthetic_loop->topo->topo_loop = (prc_topo_loop *)prc_calloc(ctx, 1, sizeof(prc_topo_loop));
    if (synthetic_loop->topo->topo_loop == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate memory for synthetic_loop->topo->topo_loop\n");
        return PRC_ERROR_MEMORY;
    }

    synthetic_loop->topo->topo_loop->number_of_coedges = curve_count;
    synthetic_loop->topo->topo_loop->coedge = (prc_coedge_in_loop *)prc_calloc(ctx, curve_count, sizeof(prc_coedge_in_loop));
    if (synthetic_loop->topo->topo_loop->coedge == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate memory for synthetic_loop->topo->topo_loop->coedge\n");
        return PRC_ERROR_MEMORY;
    }

    for (j = 0; j < curve_count; ++j)
    {
        prc_coedge_in_loop *coedge = &synthetic_loop->topo->topo_loop->coedge[j];
        prc_ref_or_compressed_curve *comp_curve = &ana_loop->trim_curves[j];
        code = prc_convert_compref_curve_to_coedge(ctx, comp_curve, coedge,
            compressed_data, wire_data);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed to convert ana loop to std loop in prc_convert_compref_curve_to_coedge\n");
            return code;
        }
    }
    return 0;
}

static int
prc_convert_ana_loops_to_std_loops(prc_context *ctx, prc_topo_face *synthetic_face,
    prc_content_compressed_ana_face *ana_face,
    prc_nano_brep_compressed_data *compressed_data, prc_exact_geom_wire_data *wire_data)
{
    int code;
    uint32_t j;
    uint32_t num_loops = ana_face->trim_loop_count;

    synthetic_face->loops = (prc_ptr_topology *)prc_calloc(ctx, ana_face->trim_loop_count, sizeof(prc_ptr_topology));
    if (synthetic_face->loops == NULL)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Failed to allocate memory for synthetic_face.loops\n");
        return PRC_ERROR_INTERNAL;
    }

    if (wire_data == NULL)
    {
        for (j = 0; j < num_loops; ++j)
        {
            code = prc_convert_compressed_loop_to_std_loop(ctx, &ana_face->trim_loop[j],
                &synthetic_face->loops[j], compressed_data, NULL);
            if (code < 0)
            {
                prc_error(ctx, code, "Failed to convert ana loop to std loop in prc_convert_compressed_loop_to_std_loop\n");
                return code;
            }
        }
    }
    else
    {
        for (j = 0; j < num_loops; ++j)
        {
            code = prc_convert_compressed_loop_to_std_loop(ctx, &ana_face->trim_loop[j],
                &synthetic_face->loops[j], compressed_data, &wire_data[j]);
            if (code < 0)
            {
                prc_error(ctx, code, "Failed to convert ana loop to std loop in prc_convert_compressed_loop_to_std_loop\n");
                return code;
            }
        }
    }
    return 0;
}

/* Decompress the compressed NURBs control points */
static int
prc_decompress_control_points(prc_context *ctx, uint32_t num_ctrl_u,
    uint32_t num_ctrl_v, prc_compressed_control_points *comp_cp,
    uint8_t is_rational, prc_compressed_weights *comp_weights,
    double nurbs_tolerance, prc_control_points_nurbs_surf *output_p)
{
    uint32_t i, j;

    /* Step 1: Initialize baseline starting pointer corner cell matrix index */
    output_p[0].x = comp_cp->p00.x;
    output_p[0].y = comp_cp->p00.y;
    output_p[0].z = comp_cp->p00.z;

    /* Step 2: Accumulate the first V row data points (i = 0, j > 0) */
    for (j = 1; j < num_ctrl_v; j++)
    {
        output_p[j].x = output_p[j - 1].x + comp_cp->ccpt_in_v[j - 1].x;
        output_p[j].y = output_p[j - 1].y + comp_cp->ccpt_in_v[j - 1].y;
        output_p[j].z = output_p[j - 1].z + comp_cp->ccpt_in_v[j - 1].z;
    }

    /* Step 3: Accumulate the first U column data points (i > 0, j = 0) */
    for (i = 1; i < num_ctrl_u; i++)
    {
        uint32_t curr_idx = i * num_ctrl_v;
        uint32_t prev_idx = (i - 1) * num_ctrl_v;
        output_p[curr_idx].x = output_p[prev_idx].x + comp_cp->ccpt_in_u[i - 1].x;
        output_p[curr_idx].y = output_p[prev_idx].y + comp_cp->ccpt_in_u[i - 1].y;
        output_p[curr_idx].z = output_p[prev_idx].z + comp_cp->ccpt_in_u[i - 1].z;
    }

    /* Step 4: Core Matrix Loop - Evaluate types on localized tangent surfaces */
    uint32_t interior_idx = 0;
    for (i = 1; i < num_ctrl_u; i++)
    {
        for (j = 1; j < num_ctrl_v; j++)
        {
            uint32_t idx_ij = i * num_ctrl_v + j;
            uint32_t idx_im1j = (i - 1) * num_ctrl_v + j;
            uint32_t idx_ijm1 = i * num_ctrl_v + (j - 1);
            uint32_t idx_prev = (i - 1) * num_ctrl_v + (j - 1);

            /* Local coordinate calculation vectors mapping */
            prc_vec3 V, U, N, PlaneOrigin;
            V.x = output_p[idx_im1j].x - output_p[idx_prev].x;
            V.y = output_p[idx_im1j].y - output_p[idx_prev].y;
            V.z = output_p[idx_im1j].z - output_p[idx_prev].z;

            U.x = output_p[idx_ijm1].x - output_p[idx_prev].x;
            U.y = output_p[idx_ijm1].y - output_p[idx_prev].y;
            U.z = output_p[idx_ijm1].z - output_p[idx_prev].z;

            PlaneOrigin.x = output_p[idx_prev].x + V.x + U.x;
            PlaneOrigin.y = output_p[idx_prev].y + V.y + U.y;
            PlaneOrigin.z = output_p[idx_prev].z + V.z + U.z;

            prc_vec_cross(U, V, &N);

            double len_u = prc_vec_length_sqrt(U);
            double len_v = prc_vec_length_sqrt(V);
            double len_n = prc_vec_length_sqrt(N);

            prc_interior_compressed_control_points *cp_int = &comp_cp->ccpt_interior[interior_idx++];
            uint32_t type = cp_int->type;

            /* Force fallback to basic absolute vector evaluations if edge constraints collapse */
            if (len_u < 1e-12 || len_v < 1e-12 || len_n < 1e-12)
            {
                type = 3;
            }

            if (type == 0)
            {
                /* Type 0: Explicit node point matches baseline plane prediction directly */
                output_p[idx_ij].x = PlaneOrigin.x;
                output_p[idx_ij].y = PlaneOrigin.y;
                output_p[idx_ij].z = PlaneOrigin.z;
            }
            else
            {
                /* Transform plane orientation system metrics to locate target */
                prc_vec3 unit_u = { U.x / len_u, U.y / len_u, U.z / len_u };
                prc_vec3 unit_n = { N.x / len_n, N.y / len_n, N.z / len_n };
                prc_vec3 cross_nu;
                prc_vec_cross(unit_n, unit_u, &cross_nu);

                double local_x = 0.0, local_y = 0.0, local_z = 0.0;

                if (type == 1)
                {
                    /* Type 1: Planar deviation matches plane, unpack only vertical Z vector */
                    local_z = (double)cp_int->Pijz;
                }
                else if (type == 2)
                {
                    /* Type 2: Unpack explicit localized X and Y shifts */
                    local_x = (double)cp_int->Pijx;
                    local_y = (double)cp_int->Pijy;
                }
                else if (type == 3)
                {
                    /* Type 3: Read full 3D deviations vector explicitly */
                    local_x = (double)cp_int->Pijx;
                    local_y = (double)cp_int->Pijy;
                    local_z = (double)cp_int->Pijz;
                }

                /* Project local coordinate coordinates back into world 3D space configuration mapping */
                output_p[idx_ij].x = PlaneOrigin.x + (local_x * unit_u.x) + (local_y * cross_nu.x) + (local_z * unit_n.x);
                output_p[idx_ij].y = PlaneOrigin.y + (local_x * unit_u.y) + (local_y * cross_nu.y) + (local_z * unit_n.y);
                output_p[idx_ij].z = PlaneOrigin.z + (local_x * unit_u.z) + (local_y * cross_nu.z) + (local_z * unit_n.z);
            }
        }
    }

    /* Step 5: Unpack Rational Weights Sequence Configuration Matrix */
    uint32_t total_points = num_ctrl_u * num_ctrl_v;
    for (i = 0; i < total_points; i++)
    {
        if (is_rational && comp_weights) {
            /* Decode using uncompressed array or unpack with weight variable metrics tolerances */
            if (comp_weights->number_bit_weight > 30)
            {
                output_p[i].w = comp_weights->weights[i];
            }
            else
            {
                output_p[i].w = comp_weights->compressed_weights[i];
            }
        }
        else
        {
            output_p[i].w = 1.0; /* Non-rational implicit fallback state boundary */
        }
    }
    return 0;
}


/* Decompress NURBS knot vectors */
static int
prc_decompress_knot_vector(prc_context *ctx, uint32_t degree, uint32_t num_control_points,
    uint32_t number_stored_knots, prc_compressed_multiplicities *mult_array,
    prc_compressed_knot_vector *comp_knot_vector, double *output_knot_vector)
{
    uint32_t total_knots = degree + num_control_points + 1;
    uint32_t i, j;

    if (comp_knot_vector->is_uniform)
    {
        /* Clamped uniform knot vector construction (0.0 at start, 1.0 at end) */
        /* First (degree + 1) entries are 0.0 */
        for (i = 0; i <= degree; i++)
        {
            output_knot_vector[i] = 0.0;
        }

        /* Inner uniform steps */
        uint32_t num_inner_steps = num_control_points - degree;
        for (i = 1; i < num_inner_steps; i++)
        {
            double value = (double)i / (double)num_inner_steps;
            output_knot_vector[degree + i] = value;
        }

        /* Last (degree + 1) entries are 1.0 */
        for (i = num_control_points; i < total_knots; i++)
        {
            output_knot_vector[i] = 1.0;
        }
        return 0;
    }

    /* Non-Uniform / Pseudo-Uniform Knot Vector Reconstruction */
    uint32_t knot_idx = 0;

    if (number_stored_knots < 2)
    {
        prc_error(ctx, PRC_ERROR_PARSE, "Invalid number of stored knots in decompression\n");
        return PRC_ERROR_PARSE;
    }

    /* Ian's feedback: Reverting pseudo-uniform configurations to unreadable until
       the specification's step-calculation interpretation is officially finalized. */
    if (comp_knot_vector->knots.is_pseudo_uniform)
    {
        prc_error(ctx, PRC_ERROR_PARSE, "Pseudo-uniform compressed knots are currently unsupported in tessellation\n");
        return PRC_ERROR_PARSE;
    }

    double current_knot_value = 0.0;

    /* General Non-Uniform Form Loop */
    for (i = 0; i < number_stored_knots; i++)
    {
        /* 1. Determine the knot's value */
        if (i == 0)
        {
            /* First knot is implicitly 0.0 */
            current_knot_value = 0.0;
        }
        else if (i == number_stored_knots - 1)
        {
            /* Last knot is implicitly 1.0 */
            current_knot_value = 1.0;
        }
        else
        {
            /* Interior knots accumulate the increments stored in the array.
               Index shifted by -1 because array only holds the (number_stored_knots - 2) interior elements */
            double compressed_delta = comp_knot_vector->knots.compressed_knots[i - 1].knot;
            current_knot_value += compressed_delta;
        }

        /* 2. Retrieve the multiplicity directly */
        uint32_t multiplicity = mult_array[i].multiplicity;

        /* 3. Write the value 'multiplicity' times to the output vector */
        for (j = 0; j < multiplicity; j++)
        {
            if (knot_idx < total_knots)
            {
                output_knot_vector[knot_idx++] = current_knot_value;
            }
            else
            {
                break;
            }
        }
    }

    /* Safety fallback padding if the total count wasn't met */
    while (knot_idx < total_knots)
    {
        output_knot_vector[knot_idx++] = 1.0;
    }

    return 0;
}

static int
prc_create_nurbs_from_compressed_nurbs(prc_context *ctx,
    prc_compressed_nurbs *compressed_nurbs, prc_surf_nurbs *nurbs, double tolerance)
{
    int code = 0;
    uint32_t i;

    /* Get all the data from the compressed nurbs */
    uint32_t degree_in_u = compressed_nurbs->degree_in_u;
    uint32_t degree_in_v = compressed_nurbs->degree_in_v;
    uint32_t number_stored_knots_in_u = compressed_nurbs->number_stored_knots_in_u;
    uint32_t number_stored_knots_in_v = compressed_nurbs->number_stored_knots_in_v;
    uint8_t is_closed_in_u = compressed_nurbs->is_closed_in_u;
    uint8_t is_closed_in_v = compressed_nurbs->is_closed_in_v;
    uint8_t is_rational = compressed_nurbs->is_rational;
    prc_compressed_multiplicities *mult_v = compressed_nurbs->mult_v;
    prc_compressed_multiplicities *mult_u = compressed_nurbs->mult_u;
    prc_compressed_control_points comp_control_points = compressed_nurbs->compressed_control_points;
    prc_compressed_knot_vector comp_knot_vector_u = compressed_nurbs->knot_vector_u;
    prc_compressed_knot_vector comp_knot_vector_v = compressed_nurbs->knot_vector_v;
    prc_compressed_weights comp_weights = compressed_nurbs->compressed_weights;

    /* Make sure all the control points, knot vectors, multiplicities, and weights are uncompressed */
    nurbs->tag = PRC_TYPE_SURF_NURBS;
    nurbs->is_rational = is_rational;
    nurbs->surface_form = KEPRCBsplineSurfaceFormUnspecified;

    /* COMPUTE THESE FROM THE COMPRESSED NURBS DATA */
    nurbs->du = degree_in_u; /* Restricted to range 1 <= degree <= 25 */
    nurbs->dv = degree_in_v; /* Restricted to range 1 <= degree <= 25 */

    /* 1. Calculate explicit total knot vector counts by summing the multiplicity arrays */
    uint32_t total_knots_u = 0;
    for (i = 0; i < number_stored_knots_in_u; i++)
    {
        total_knots_u += mult_u[i].multiplicity;
    }

    uint32_t total_knots_v = 0;
    for (i = 0; i < number_stored_knots_in_v; i++)
    {
        total_knots_v += mult_v[i].multiplicity;
    }

    /* Safety validation check for underflow or sizing anomalies */
    if (total_knots_u <= degree_in_u + 1 || total_knots_v <= degree_in_v + 1)
    {
        prc_error(ctx, PRC_ERROR_PARSE, "Invalid accumulated knot count vs surface degree configuration.\n");
        return PRC_ERROR_PARSE;
    }

    nurbs->highest_index_of_knots_u = total_knots_u - 1;
    nurbs->highest_index_of_knots_v = total_knots_v - 1;

    /* 2. Solve for control point counts using the standard NURBS structural constraint equation */
    uint32_t num_ctrl_u = total_knots_u - degree_in_u - 1;
    uint32_t num_ctrl_v = total_knots_v - degree_in_v - 1;

    nurbs->highest_index_of_control_points_u = num_ctrl_u - 1;
    nurbs->highest_index_of_control_points_v = num_ctrl_v - 1;

    /* 3. Allocate Uncompressed NURBS Arrays */
    nurbs->p = (prc_control_points_nurbs_surf *)prc_calloc(ctx, num_ctrl_u * num_ctrl_v, sizeof(prc_control_points_nurbs_surf));
    nurbs->knot_vector_u = (double *)prc_calloc(ctx, total_knots_u, sizeof(double));
    nurbs->knot_vector_v = (double *)prc_calloc(ctx, total_knots_v, sizeof(double));
    nurbs->knot_type = KEPRCKnotTypeUnspecified;

    if (!nurbs->p || !nurbs->knot_vector_u || !nurbs->knot_vector_v)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Memory allocation failed for uncompressed NURBS arrays.\n");
        goto free_nurbs_error;
    }

    /* 4. Decompress U and V Knot Vectors */
    code = prc_decompress_knot_vector(ctx, degree_in_u, num_ctrl_u,
        number_stored_knots_in_u, mult_u, &comp_knot_vector_u, nurbs->knot_vector_u);
    if (code < 0)
    {
        prc_error(ctx, code, "Error in prc_decompress_knot_vector.\n");
        goto free_nurbs_error;
    }

    code = prc_decompress_knot_vector(ctx, degree_in_v, num_ctrl_v,
        number_stored_knots_in_v, mult_v, &comp_knot_vector_v, nurbs->knot_vector_v);
    if (code < 0)
    {
        prc_error(ctx, code, "Error in prc_decompress_knot_vector.\n");
        goto free_nurbs_error;
    }

    /* 5. Decompress Control Points Matrix & Optional Weights */
    code = prc_decompress_control_points(ctx, num_ctrl_u, num_ctrl_v,
        &comp_control_points, is_rational, &comp_weights, tolerance, nurbs->p);
    if (code < 0)
    {
        prc_error(ctx, code, "Error in prc_decompress_control_points.\n");
        goto free_nurbs_error;
    }

    return 0;

free_nurbs_error:
    if (nurbs->p != NULL)
    {
        prc_free(ctx, nurbs->p);
        nurbs->p = NULL;
    }
    if (nurbs->knot_vector_u != NULL)
    {
        prc_free(ctx, nurbs->knot_vector_u);
        nurbs->knot_vector_u = NULL;
    }
    if (nurbs->knot_vector_v != NULL)
    {
        prc_free(ctx, nurbs->knot_vector_v);
        nurbs->knot_vector_v = NULL;
    }
    return code;
}

/* We can use this method in every case even when we are dealing with
   the curve that moves along the minor radius of the torus. For spheres,
   cylinders, cones the circles will be co-planar and this allows us
   to compute curve3 from curve1 and curve4 from curve2. For the minor
   torus circle we need information about the major axis, hence we have
   the struct is_torus_minor_axis which either saves or uses torus_major_center
   and torus_major_normal */
static int
prc_create_deduced_curve(prc_context *ctx, prc_compressed_curve *src_curve,
    uint8_t src_reversed, prc_iso_torus_circle *torus_data,
    prc_compressed_curve *des_curve, prc_vec3 des_start,
    prc_vec3 des_end, prc_nano_brep_compressed_data *compressed_data)
{
    int code = 0;

    /* Set des_curve to same type as src_curve */
    des_curve->curve_type = src_curve->curve_type;

    /* Now special handling for each type */
    switch (des_curve->curve_type)
    {
    case PRC_HCG_Line:
        /* Line is easy.  We just add the points */
        des_curve->hcg_line.type = PRC_HCG_Line;
        des_curve->hcg_line.start_end_data.is_vertex = 0;
        des_curve->hcg_line.start_end_data.start_point.point = des_start;
        des_curve->hcg_line.start_end_data.end_point.point = des_end;
        break;

    case PRC_HCG_Circle:
    {
        prc_hcg_circle_information info;
        code = prc_get_hcg_circle_data(ctx, &src_curve->hcg_circle, &info, compressed_data);
        if (code < 0)
        {
            return code;
        }

        des_curve->hcg_circle.type = PRC_HCG_Circle;
        des_curve->hcg_circle.is_particular_circle = src_curve->hcg_circle.is_particular_circle;

        /* Calculate source circle radius */
        prc_vec3 src_rad_vec;
        prc_vec_sub(info.start_point, info.center, &src_rad_vec);
        double radius = prc_vec_length_sqrt(src_rad_vec);

        /* Establish the Base Normal Vector */
        prc_vec3 des_normal;

        if (torus_data->type == PRC_MINOR_ISO_TORUS_CIRCLE)
        {
            /* [TORUS MINOR AXIS ONLY]: Normal rotates around the donut */
            prc_vec3 radial_vec;
            prc_vec_sub(des_start, torus_data->center, &radial_vec);
            prc_vec_cross(torus_data->normal, radial_vec, &des_normal);
        }
        else
        {
            /* For Cylinders, Cones, Spheres, and Torus Major rings.
               The normal plane is parallel to the source curve plane. */
            if (info.has_normal)
            {
                des_normal = info.normal;
            }
            else
            {
                prc_vec3 v1, v2;
                prc_vec_sub(info.start_point, info.center, &v1);
                prc_vec_sub(info.end_point, info.center, &v2);
                prc_vec_cross(v1, v2, &des_normal);
            }
        }

        prc_vec_normalize(&des_normal);

        /* Save original raw normal direction for geometric reference */
        prc_vec3 raw_normal = des_normal;
        if (!src_reversed)
        {
            prc_vec_negate(&des_normal);
        }

        /* Compute fundamental chord geometry first to check tracking bounds */
        prc_vec3 chord, chord_mid;
        prc_vec_sub(des_end, des_start, &chord);
        prc_vec_avg(des_start, des_end, &chord_mid);
        double half_chord_len = prc_vec_length_sqrt(chord) / 2.0;

        prc_vec3 des_center;
        int use_negative_offset = 0;

        /* Determine if this is a Coaxial Varying-Radius surface (Cone, Sphere Latitudes, Torus Major) */
        int is_coaxial_varying = 0;
        if (torus_data->type == PRC_MAJOR_ISO_TORUS_CIRCLE)
        {
            is_coaxial_varying = 1;
        }
        else if (torus_data->type != PRC_MINOR_ISO_TORUS_CIRCLE)
        {
            /* Check 1: Is the new chord too wide to fit the old radius? (Expanding Cone/Sphere) */
            if (radius <= half_chord_len)
            {
                is_coaxial_varying = 1;
            }
            else
            {
                /* Check 2: Has the plane shifted along the axis normal? (Shrinking Cone/Sphere)
                   If the vector from the old center to the new chord midpoint points along
                   the normal axis, the circles are parallel but non-coplanar. */
                prc_vec3 center_to_mid;
                prc_vec_sub(chord_mid, info.center, &center_to_mid);
                double axial_shift = fabs(prc_vec_dot_product(center_to_mid, raw_normal));

                /* If the shift along the axis is non-trivial, it's a cone or sphere latitude */
                if (axial_shift > 1e-5)
                {
                    is_coaxial_varying = 1;
                }
            }
        }

        if (is_coaxial_varying)
        {
            /* --- IAN'S AXIAL PROJECTION RULE ---
               Find the foot of the perpendicular from des_start onto the source axis line.
               The axis line passes through info.center along the direction of raw_normal. */
            prc_vec3 v_to_start;
            prc_vec_sub(des_start, info.center, &v_to_start);

            double t = prc_vec_dot_product(v_to_start, raw_normal);

            prc_vec3 axis_offset = raw_normal;
            prc_vec_scale(t, &axis_offset);
            prc_vec_add(info.center, axis_offset, &des_center);

            /* Recalculate true radius for this height/level */
            prc_vec3 new_rad_vec;
            prc_vec_sub(des_start, des_center, &new_rad_vec);
            radius = prc_vec_length_sqrt(new_rad_vec);
        }
        else
        {
            /* --- STANDARD CHORD BISECTOR RULE --- (Cylinders, Sphere Meridians, Torus Minors) */
            prc_vec3 bisector_dir;
            prc_vec_cross(chord, des_normal, &bisector_dir);
            prc_vec_normalize(&bisector_dir);

            double dist_to_center = 0.0;
            if (radius > half_chord_len)
            {
                dist_to_center = sqrt((radius * radius) - (half_chord_len * half_chord_len));
            }

            prc_vec3 offset_vec = bisector_dir;
            prc_vec_scale(dist_to_center, &offset_vec);

            /* Generate both prospective center points */
            prc_vec3 center_pos, center_neg;
            prc_vec_add(chord_mid, offset_vec, &center_pos);
            prc_vec_sub(chord_mid, offset_vec, &center_neg);

            /* Dot-product loop sign selection logic */
            prc_vec3 src_chord, src_chord_mid, src_to_center;
            prc_vec_sub(info.end_point, info.start_point, &src_chord);
            prc_vec_avg(info.start_point, info.end_point, &src_chord_mid);
            prc_vec_sub(info.center, src_chord_mid, &src_to_center);

            if (prc_vec_dot_product(bisector_dir, src_to_center) < 0.0) {
                use_negative_offset = 1;
            }

            if (use_negative_offset)
            {
                des_center = center_neg;
            }
            else
            {
                des_center = center_pos;
            }
        }
#if 0
        printf("\n---[ DEDUCE CURVE DEBUG ]---\n");
        printf("src_reversed state:    %d\n", src_reversed);
        printf("Source Circle Radius:  %.6f\n", radius);
        printf("Source Center:         (%.6f, %.6f, %.6f)\n", info.center.x, info.center.y, info.center.z);
        printf("Source Normal (raw):   (%.6f, %.6f, %.6f)\n", raw_normal.x, raw_normal.y, raw_normal.z);
        printf("Derived Normal (used): (%.6f, %.6f, %.6f)\n", des_normal.x, des_normal.y, des_normal.z);
        printf("Input des_start:       (%.6f, %.6f, %.6f)\n", des_start.x, des_start.y, des_start.z);
        printf("Input des_end:         (%.6f, %.6f, %.6f)\n", des_end.x, des_end.y, des_end.z);
        printf("Chord Midpoint:        (%.6f, %.6f, %.6f)\n", chord_mid.x, chord_mid.y, chord_mid.z);
        printf("Half Chord Length:     %.6f\n", half_chord_len);
        printf("Bisector Direction:    (%.6f, %.6f, %.6f)\n", bisector_dir.x, bisector_dir.y, bisector_dir.z);
        printf("Distance to Center:    %.6f\n", dist_to_center);
        printf("Calculated Center (+): (%.6f, %.6f, %.6f)\n", center_pos.x, center_pos.y, center_pos.z);
        printf("Alternative Center(-): (%.6f, %.6f, %.6f)\n", center_neg.x, center_neg.y, center_neg.z);
        printf("Chosen Offset Type:    %s\n", use_negative_offset ? "NEGATIVE (-)" : "POSITIVE (+)");
        printf("Final Assigned Center: (%.6f, %.6f, %.6f)\n", des_center.x, des_center.y, des_center.z);
        printf("----------------------------\n\n");
#endif
        /* Assign results back to destination structures */
        if (src_curve->hcg_circle.is_particular_circle)
        {
            prc_particular_circle *p_circle_des = &des_curve->hcg_circle.particular_circle;
            prc_particular_circle *p_circle_src = &src_curve->hcg_circle.particular_circle;

            p_circle_des->full_circle = p_circle_src->full_circle;
            p_circle_des->start_end_data.is_vertex = 0;
            p_circle_des->start_end_data.start_point.point = des_start;
            p_circle_des->start_end_data.end_point.point = des_end;

            if (p_circle_des->full_circle)
            {
                p_circle_des->center.point = des_center;
                p_circle_des->normal_plane.point = des_normal;
            }
            else
            {
                prc_vec3 mid_arc_offset;
                if (is_coaxial_varying)
                {
                    /* For axial projection, compute mid-arc directly outward from center to chord midpoint */
                    prc_vec_sub(chord_mid, des_center, &mid_arc_offset);
                    prc_vec_normalize(&mid_arc_offset);
                }
                else
                {
                    mid_arc_offset = raw_normal; /* base reference fallback */
                }
                prc_vec_scale(radius, &mid_arc_offset);
                prc_vec_add(des_center, mid_arc_offset, &p_circle_des->middle_of_arc.point);
            }
        }
        else
        {
            /* General circle case */
            prc_general_circle *g_circle_des = &des_curve->hcg_circle.general_circle;
            prc_general_circle *g_circle_src = &src_curve->hcg_circle.general_circle;

            g_circle_des->start_end_data.is_vertex = 0;
            g_circle_des->start_end_data.start_point.point = des_start;
            g_circle_des->start_end_data.end_point.point = des_end;

            g_circle_des->center.point = des_center;
            g_circle_des->circle_angle = g_circle_src->circle_angle;
        }

        /* Get the major torus axis information so we have it when we do the minor */
        if (torus_data->type == PRC_MAJOR_ISO_TORUS_CIRCLE)
        {
            torus_data->normal = raw_normal;
            torus_data->center = des_center;
        }
        break;
    }

    case  PRC_HCG_BsplineHermiteCurve:
        prc_error(ctx, PRC_ERROR_PARSE, "Unsupported curve type in prc_get_compressed_curve_start_end\n");
        return PRC_ERROR_PARSE;
        break;
    case PRC_HCG_CompositeCurve:
        prc_error(ctx, PRC_ERROR_PARSE, "Unsupported curve type in prc_get_compressed_curve_start_end\n");
        return PRC_ERROR_PARSE;
        break;
    default:
        prc_error(ctx, PRC_ERROR_PARSE, "Unsupported curve type in prc_get_compressed_curve_start_end\n");
        return PRC_ERROR_PARSE;
    }
    return 0;
}

/* Helper for ISO surfaces as we contruct the loop */
static uint8_t
prc_compressed_curve_is_ref(prc_context *ctx, prc_ref_or_compressed_curve *ref_or_comp_curve)
{
    if (ref_or_comp_curve->curve_is_not_already_stored ||
        ref_or_comp_curve->is_deduced_curve)
    {
        return 0;
    }
    return 1;
}

/* Helper for ISO surfaces as we contruct the loop. */
static int
prc_get_compressed_curve_start_end(prc_context *ctx, prc_compressed_curve *comp_curve,
    prc_vec3 *start, prc_vec3 *end, prc_nano_brep_compressed_data *compressed_data)
{
    int code = 0;

    switch (comp_curve->curve_type)
    {
    case PRC_HCG_Line:
        code = prc_get_start_end_data(ctx, &comp_curve->hcg_line.start_end_data,
            start, end, compressed_data);
        break;
    case PRC_HCG_Circle:
    {
        prc_hcg_circle_information info;
        code = prc_get_hcg_circle_data(ctx, &comp_curve->hcg_circle, &info, compressed_data);
        *start = info.start_point;
        *end = info.end_point;
        break;
    }
    case  PRC_HCG_BsplineHermiteCurve:
        code = prc_get_start_end_data(ctx, &comp_curve->hcg_bspline_hermite_curve.start_end_data,
            start, end, compressed_data);
        break;
    case PRC_HCG_CompositeCurve:
        code = prc_get_start_end_data(ctx, &comp_curve->hcg_composite_curve.start_end_data,
            start, end, compressed_data);
        break;
    default:
        prc_error(ctx, PRC_ERROR_PARSE, "Unsupported curve type in prc_get_compressed_curve_start_end\n");
        return PRC_ERROR_PARSE;
    }

    return code;
}

/* In the case of the ISO surfaces we have 4 curves, two of which are given
   and two that are given by ref or deduced from the first two and a vertex.
   Here we assemble these into a single loop making sure to add any information
   about the direction of the curves as we add them. For example referenced
   curves will always be reversed.  We label the curves, 0, 1, 2, 3 here  */
static int
prc_construct_loop_from_iso_curves(prc_context *ctx, prc_content_compressed_face *face,
    prc_topo_face *synthetic_face, uint8_t is_torus, uint8_t first_curve_is_torus_major,
    prc_nano_brep_compressed_data *compressed_data)
{
    int code;
    prc_compressed_curve *curve[4] = { 0 };
    uint8_t coedge_orientation[4];
    prc_compressed_curve curve3_deduced = { 0 };
    prc_compressed_curve curve4_deduced = { 0 };
    prc_vec3 start_curve0, end_curve0, start_curve1, end_curve1;
    prc_vec3 start_curve2, end_curve2, start_curve3, end_curve3;
    prc_vec3 common_vertex;
    uint8_t reverse_curve0 = 0;
    uint8_t reverse_curve1 = 0;
    uint8_t reverse_curve2 = 0;
    uint8_t reverse_curve3 = 0;
    uint8_t curve_is_torus_minor = 0;
    prc_vec3 torus_major_center = { 0 };
    prc_vec3 torus_major_normal = { 0 };
    prc_iso_torus_circle torus_data = { 0 };
    uint8_t curve_deduced[4] = { 0 };

    code = prc_get_compressed_curve(ctx, &face->iso_face.first_trim_curve,
        &curve[0], compressed_data);
    if (code < 0)
    {
        prc_error(ctx, code, "Failed to get first trim curve by id in prc_construct_loop_from_iso_curves\n");
        return code;
    }
    reverse_curve0 = prc_compressed_curve_is_ref(ctx, &face->iso_face.first_trim_curve);
    code = prc_get_compressed_curve_start_end(ctx, curve[0], &start_curve0, &end_curve0, compressed_data);
    if (code < 0)
    {
        prc_error(ctx, code, "Failed to get start and end points of first trim curve in prc_construct_loop_from_iso_curves\n");
        return code;
    }

    code = prc_get_compressed_curve(ctx, &face->iso_face.second_trim_curve, &curve[1], compressed_data);
    if (code < 0)
    {
        prc_error(ctx, code, "Failed to get second trim curve by id in prc_construct_loop_from_iso_curves\n");
        return code;
    }
    reverse_curve1 = prc_compressed_curve_is_ref(ctx, &face->iso_face.second_trim_curve);
    code = prc_get_compressed_curve_start_end(ctx, curve[1], &start_curve1, &end_curve1, compressed_data);
    if (code < 0)
    {
        prc_error(ctx, code, "Failed to get start and end points of second trim curve in prc_construct_loop_from_iso_curves\n");
        return code;
    }

    /* Per discussion with Patrick, the curves ARE ordered to form a loop,
       however, if they are referenced due to being from another face,
       then we have to flip them around. We can denote this in the
       construction of the loop. At this point, lets see if we are
       deducing the third and fourth curves or if we are getting them
       as references. If they are references, then they are reversed.
       The deduced ones we will have in the proper direction */
    if (face->iso_face.fourth_trim_curve_is_not_yet_saved &&
        face->iso_face.third_trim_curve_is_not_yet_saved)
    {
        /* In this case, we will be deducing the third and fourth curves
           using the first and second curve AND the vertex. First get the vertex */
        code = prc_get_compressed_vertex(ctx, &face->iso_face.common_third_fourth_vertex,
            &common_vertex, compressed_data);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed to get common vertex in prc_construct_loop_from_iso_curves\n");
            return code;
        }

        /* Curve 3 is created using curve 1 and the vertex. It *is* possible
           that curve 1 was referenced and so is reversed. In any event first
           lets get the start and end point of curve 3. These come from curve 2
           and from the vertex */
        if (reverse_curve1)
        {
            start_curve2 = start_curve1;
        }
        else
        {
            start_curve2 = end_curve1;
        }
        end_curve2 = common_vertex;


        /* And curve 4 which is based upon curve 2 */
        if (reverse_curve0)
        {
            end_curve3 = end_curve0;
        }
        else
        {
            end_curve3 = start_curve0;
        }
        start_curve3 = common_vertex;

        /* Now create a curve 3 equivalent to curve 1 but using different
           start and end points AND perhaps reversed from curve 1. If we
           are in a torus situation and !first_curve_is_torus_major, then
           we actually have some work to do as we should go ahead and do the
           major curve first as we need the normal and center of that circle
           so we know how to locate where the minor is */
        if (is_torus)
        {
            /* Just do this separately to keep things clear */
            if (first_curve_is_torus_major)
            {
                /* Major first */
                curve[2] = &curve3_deduced;
                torus_data.type = PRC_MAJOR_ISO_TORUS_CIRCLE;
                code = prc_create_deduced_curve(ctx, curve[0], reverse_curve0,
                    &torus_data, curve[2], start_curve2, end_curve2, compressed_data);
                if (code < 0)
                {
                    prc_error(ctx, code, "Failed to create deduced curve in prc_construct_loop_from_iso_curves\n");
                    return code;
                }

                /* Now the minor axis. We already should have the data from
                   the major axis */
                curve[3] = &curve4_deduced;
                torus_data.type = PRC_MINOR_ISO_TORUS_CIRCLE;
                code = prc_create_deduced_curve(ctx, curve[1], reverse_curve1,
                    &torus_data, curve[3], start_curve3, end_curve3, compressed_data);
                if (code < 0)
                {
                    prc_error(ctx, code, "Failed to create deduced curve in prc_construct_loop_from_iso_curves\n");
                    return code;
                }
            }
            else
            {
                /* In this case the curve1 is the minor torus axis. So we do curve 4 (major) first */
                curve[3] = &curve4_deduced;
                torus_data.type = PRC_MAJOR_ISO_TORUS_CIRCLE;
                code = prc_create_deduced_curve(ctx, curve[1], reverse_curve1,
                    &torus_data, curve[3], start_curve3, end_curve3, compressed_data);
                if (code < 0)
                {
                    prc_error(ctx, code, "Failed to create deduced curve in prc_construct_loop_from_iso_curves\n");
                    return code;
                }

                /* And now curve 3 (minor) */
                curve[2] = &curve3_deduced;
                torus_data.type = PRC_MINOR_ISO_TORUS_CIRCLE;
                code = prc_create_deduced_curve(ctx, curve[0], reverse_curve0,
                    &torus_data, curve[2], start_curve2, end_curve2, compressed_data);
                if (code < 0)
                {
                    prc_error(ctx, code, "Failed to create deduced curve in prc_construct_loop_from_iso_curves\n");
                    return code;
                }
            }
        }
        else
        {
            curve[2] = &curve3_deduced;
            code = prc_create_deduced_curve(ctx, curve[0], reverse_curve0,
                &torus_data, curve[2], start_curve2, end_curve2, compressed_data);
            if (code < 0)
            {
                prc_error(ctx, code, "Failed to create deduced curve in prc_construct_loop_from_iso_curves\n");
                return code;
            }

            /* Now create a curve 4 equivalent to curve 2 but using different
               start and end points AND perhaps reversed from curve 2 */
            curve[3] = &curve4_deduced;
            code = prc_create_deduced_curve(ctx, curve[1], reverse_curve1,
                &torus_data, curve[3], start_curve3, end_curve3, compressed_data);
            if (code < 0)
            {
                prc_error(ctx, code, "Failed to create deduced curve in prc_construct_loop_from_iso_curves\n");
                return code;
            }
        }
        coedge_orientation[2] = 1; /* These should be in the proper direction */
        coedge_orientation[3] = 1;

        /* Mark curve3 and curve4 as deduced. It is from this that we will know
           that we need to make an allocation, copy, and then free */
        curve_deduced[2] = 1;
        curve_deduced[3] = 1;
    }
    else
    {
        /* In this case we get curve three and four from the reference
           and they should be reversed. No need to do any creation of curves */
        code = prc_get_compressed_curve(ctx, &face->iso_face.third_trim_curve, &curve[2], compressed_data);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed to third curve in prc_construct_loop_from_iso_curves\n");
            return code;
        }
        code = prc_get_compressed_curve(ctx, &face->iso_face.fourth_trim_curve, &curve[3], compressed_data);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed to fourth curve in prc_construct_loop_from_iso_curves\n");
            return code;
        }

        /* Lets check that they are indeed references though */
        if (!prc_compressed_curve_is_ref(ctx, &face->iso_face.fourth_trim_curve) ||
            !prc_compressed_curve_is_ref(ctx, &face->iso_face.third_trim_curve))
        {
            prc_error(ctx, code, "Fourth and third trim curves should be references\n");
            return code;
        }
        coedge_orientation[2] = 0; /* These should be reversed since they are from another face */
        coedge_orientation[3] = 0;
    }

    synthetic_face->loops = (prc_ptr_topology *)prc_calloc(ctx, 1, sizeof(prc_ptr_topology));
    if (synthetic_face->loops == NULL)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Failed to allocate memory for synthetic_face.loops\n");
        return PRC_ERROR_INTERNAL;
    }

    /* Now add the four curves to the loop. */
    coedge_orientation[0] = !reverse_curve0;
    coedge_orientation[1] = !reverse_curve1;
    code = prc_convert_iso_curves_to_std_loop(ctx, curve, synthetic_face->loops,
        coedge_orientation, curve_deduced, compressed_data);
    if (code < 0)
    {
        prc_error(ctx, code, "Failed to convert iso curves to std loop in prc_construct_loop_from_iso_curves\n");
        return code;
    }
    return 0;
}

static prc_compressed_curve*
prc_get_compressed_curve_from_iso_loop(prc_context *ctx, prc_ptr_topology *loop,
    uint8_t index, uint8_t *is_reversed)
{
    prc_coedge_in_loop *coedge = &loop->topo->topo_loop->coedge[index];
    prc_topo_coedge *topo_coedge = coedge->next_coedge.topo->topo_coedge;
    prc_topo *topo = topo_coedge->ptr_topology.topo;
    *is_reversed = !topo_coedge->coedge_orientation;
    return topo->topo_single_wire_compress->ref_or_compressed_curve.compressed_curve;
}

/* From the iso curve data figure out the cylinder radius and the transform
   so that we can represent the ISO surface with the uncompressed one */
static int
prc_get_cylinder_data_from_iso_curves(prc_context *ctx, prc_ptr_topology *loop,
    double *radius, prc_trans_3d *transform, uint8_t *has_transform,
    prc_nano_brep_compressed_data *compressed_data)
{
    int code = 0;
    prc_compressed_curve *curve1, *curve2;
    uint8_t is_reversed1, is_reversed2;
    prc_compressed_curve *line, *circle;
    uint8_t is_line_reversed, is_circle_reversed;
    uint8_t line_is_curve1 = 0;
    prc_vec3 line_start, line_end, line_dir, temp_vec;
    prc_hcg_circle_information circle_info;

    /* Lets dig in and get curve1 and curve2. One should be circle the other
       a line. */
    curve1 = prc_get_compressed_curve_from_iso_loop(ctx, loop, 0, &is_reversed1);
    curve2 = prc_get_compressed_curve_from_iso_loop(ctx, loop, 1, &is_reversed2);

    /* One of these is a line and one is a circle */
    if (curve1->curve_type == PRC_HCG_Circle)
    {
        circle = curve1;
        line = curve2;
        is_circle_reversed = is_reversed1;
        is_line_reversed = is_reversed2;
        line_is_curve1 = 0;
    }
    else
    {
        circle = curve2;
        line = curve1;
        is_circle_reversed = is_reversed2;
        is_line_reversed = is_reversed1;
    }

    /* Get the normal of the cylinder */
    prc_get_compressed_curve_start_end(ctx, line, &line_start, &line_end, NULL);
    prc_vec_sub(line_end, line_start, &line_dir);
    if (is_line_reversed)
    {
        prc_vec_negate(&line_dir);
    }
    code = prc_vec_normalize_sqrt(&line_dir);
    if (code < 0)
    {
        prc_error(ctx, code, "Failed to normalize line direction in prc_get_cylinder_data_from_iso_curves\n");
        return code;
    }

    /* Now get the center point of the circle as this will define the
       center of the cylinder and get the radius of the circle */
    code = prc_get_hcg_circle_data(ctx, &circle->hcg_circle, &circle_info, compressed_data);
    if (code < 0)
    {
        prc_error(ctx, code, "Failed to get HCG circle data in prc_get_cylinder_data_from_iso_curves\n");
        return code;
    }

    prc_vec_sub(circle_info.start_point, circle_info.center, &temp_vec);
    *radius = prc_vec_length_sqrt(temp_vec);

    /* Now we have the radius and the direction of the cylinder. We need to
       create a transform that will take the cylinder from its default position
       to the position defined by the line and circle. The default cylinder is
       along the Z axis with center at origin. So we need to rotate it so that
       Z axis aligns with line_dir and then translate it to circle_info.center. */
    *transform = (prc_trans_3d){ 0 };

    /* If direction is 0 0 1 and center is at 0 0 X then there is no transform */
    if (line_dir.x == 0 && line_dir.y == 0 && line_dir.z == 1 &&
        circle_info.center.x == 0 && circle_info.center.y == 0)
    {
        *has_transform = 0;
    }
    else
    {
        *has_transform = 1;
        transform->behavior = PRC_TRANSFORMATION_Translate | PRC_TRANSFORMATION_Rotate;

        /*  Set the translation directly to the circle's center */
        transform->translation = circle_info.center;

        /* Build the rotation frame where the new Z-axis is line_dir */
        /* We need an arbitrary, non-parallel vector to seed the cross product.
           If line_dir is mostly aligned with Z, we use X; otherwise, we use Z. */
        prc_vec3 temp_axis = { 0.0, 0.0, 1.0 };
        if (fabs(line_dir.x) < 0.1 && fabs(line_dir.y) < 0.1)
        {
            temp_axis.x = 1.0;
            temp_axis.z = 0.0;
        }

        /* Generate the new X-axis (rotation[0]) perpendicular to line_dir */
        prc_vec_cross(temp_axis, line_dir, &transform->rotation[0]);
        code = prc_vec_normalize_sqrt(&transform->rotation[0]);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed in prc_vec_normalize_sqrt\n");
            return code;
        }

        /* Generate the new Y-axis (rotation[1]) perpendicular to both X and Z */
        prc_vec_cross(line_dir, transform->rotation[0], &transform->rotation[1]);
        code = prc_vec_normalize_sqrt(&transform->rotation[1]);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed in prc_vec_normalize_sqrt\n");
            return code;
        }
    }
    return 0;
}

/* From the iso curve data figure out the cone radius, semi_angle, and the transform
   so that we can represent the ISO surface with the uncompressed one. It is
   easier to make use of all the curves in this method */
static int
prc_get_cone_data_from_iso_curves(prc_context *ctx, prc_ptr_topology *loop,
    double *radius, double *semi_angle, prc_trans_3d *transform, uint8_t *has_transform,
    prc_nano_brep_compressed_data *compressed_data)
{
    int code = 0;
    prc_compressed_curve *curve1, *curve2, *curve3, *curve4;
    uint8_t is_reversed1, is_reversed2, is_reversed3, is_reversed4;
    prc_compressed_curve *line1, *circle1, *line2, *circle2;
    uint8_t is_line_reversed, is_circle_reversed;
    prc_vec3 line_start, line_end, line_dir, temp_vec;
    prc_hcg_circle_information circle_info1, circle_info2;
    double radius1, radius2;

    /* Lets dig in and get curve1 and curve2. One should be circle the other a line. */
    curve1 = prc_get_compressed_curve_from_iso_loop(ctx, loop, 0, &is_reversed1);
    curve2 = prc_get_compressed_curve_from_iso_loop(ctx, loop, 1, &is_reversed2);
    curve3 = prc_get_compressed_curve_from_iso_loop(ctx, loop, 2, &is_reversed3);
    curve4 = prc_get_compressed_curve_from_iso_loop(ctx, loop, 3, &is_reversed4);

    /* One of these is a line and one is a circle */
    if (curve1->curve_type == PRC_HCG_Circle)
    {
        circle1 = curve1;
        line1 = curve2;
        circle2 = curve3;
        line2 = curve4;
    }
    else
    {
        circle1 = curve2;
        line1 = curve1;
        circle2 = curve4;
        line2 = curve3;
    }

    /* Figure out which of the circles is the smaller radius. Note that one
       of these could even be the apex (zero radius) */
    code = prc_get_hcg_circle_data(ctx, &circle1->hcg_circle, &circle_info1, compressed_data);
    if (code < 0)
    {
        prc_error(ctx, code, "Failed to get HCG circle data in prc_get_cone_data_from_iso_curves\n");
        return code;
    }
    code = prc_get_hcg_circle_data(ctx, &circle2->hcg_circle, &circle_info2, compressed_data);
    if (code < 0)
    {
        prc_error(ctx, code, "Failed to get HCG circle data in prc_get_cone_data_from_iso_curves\n");
        return code;
    }

    prc_vec_sub(circle_info1.start_point, circle_info1.center, &temp_vec);
    radius1 = prc_vec_length_sqrt(temp_vec);
    prc_vec_sub(circle_info2.start_point, circle_info2.center, &temp_vec);
    radius2 = prc_vec_length_sqrt(temp_vec);

    /* The base cone has its radius defined at z = 0 and then goes up from
       there towards its apex. Lets create a cone that is like this and
       then we will transform via translation and rotation to what we have
       defined with these curves.  Use the larger radius as the z = 0 base */

    /* Figure out which circle is the base (larger radius) and which is the top (smaller radius) */
    prc_hcg_circle_information *base_circle, *top_circle;
    double r_base, r_top;

    if (radius1 >= radius2)
    {
        base_circle = &circle_info1;
        top_circle = &circle_info2;
        r_base = radius1;
        r_top = radius2;
    }
    else
    {
        base_circle = &circle_info2;
        top_circle = &circle_info1;
        r_base = radius2;
        r_top = radius1;
    }

    /* 1. Calculate the central axis direction vector (Z-axis of the cone)
          It points from the base circle center toward the top circle center */
    prc_vec_sub(top_circle->center, base_circle->center, &line_dir);
    double delta_v = prc_vec_length_sqrt(line_dir);

    if (delta_v > 1e-12)
    {
        code = prc_vec_normalize_sqrt(&line_dir);
        if (code < 0) return code;

        /* 2. Compute the semi-angle
              As v increases by delta_v, the radius shrinks from r_base to r_top */
        *semi_angle = atan((r_top - r_base) / delta_v);
    }
    else
    {
        *semi_angle = 0.0; /* Degenerate case */
    }

    /* 3. Assign the bottom radius at z = 0 */
    *radius = r_base;

    /* 4. Construct the 3D local coordinate transform matrix */
    *transform = (prc_trans_3d){ 0 };
    transform->behavior = PRC_TRANSFORMATION_Translate | PRC_TRANSFORMATION_Rotate;
    transform->translation = base_circle->center;

    /* X-Axis (rotation[0]) must line up with the u = 0 starting point of the boundary.
       We project the base circle's start point onto the circle's plane to get the true X-axis. */
    prc_vec3 radial_vec;
    prc_vec_sub(base_circle->start_point, base_circle->center, &radial_vec);

    /* Remove any component along line_dir to ensure it is perfectly orthogonal */
    double dot_proj = prc_vec_dot_product(radial_vec, line_dir);
    transform->rotation[0].x = radial_vec.x - (dot_proj * line_dir.x);
    transform->rotation[0].y = radial_vec.y - (dot_proj * line_dir.y);
    transform->rotation[0].z = radial_vec.z - (dot_proj * line_dir.z);

    code = prc_vec_normalize_sqrt(&transform->rotation[0]);
    if (code < 0)
    {
        prc_error(ctx, code, "Failed to get normalize vector in prc_get_cone_data_from_iso_curves\n");
        return code;
    }

    /* Y-Axis (rotation[1]) is determined using the cross product of the Z and X axes */
    prc_vec_cross(line_dir, transform->rotation[0], &transform->rotation[1]);
    code = prc_vec_normalize_sqrt(&transform->rotation[1]);
    if (code < 0)
    {
        prc_error(ctx, code, "Failed to get normalize vector in prc_get_cone_data_from_iso_curves\n");
        return code;
    }

    const double eps = 1e-12;
    if (fabs(base_circle->center.x) < eps &&
        fabs(base_circle->center.y) < eps &&
        fabs(base_circle->center.z) < eps &&
        fabs(line_dir.x) < eps &&
        fabs(line_dir.y) < eps &&
        fabs(line_dir.z - 1.0) < eps &&
        fabs(transform->rotation[0].x - 1.0) < eps &&
        fabs(transform->rotation[0].y) < eps &&
        fabs(transform->rotation[0].z) < eps)
    {
            /* Complete geometry matches canonical alignment exactly; no transform needed */
            *transform = (prc_trans_3d){ 0 };
            *has_transform = 0;
    }
    else
    {
        *has_transform = 1;
    }
    return 0;
}

/* From the iso curve data figure out the torus major/minor radius and the transform
   so that we can represent the ISO surface with the uncompressed one */
static int
prc_get_torus_data_from_iso_curves(prc_context *ctx, prc_ptr_topology *loop,
    uint8_t first_curve_is_major, double *major_radius, double *minor_radius,
    prc_trans_3d *transform, uint8_t *has_transform,
    prc_nano_brep_compressed_data *compressed_data)
{
    int code = 0;
    prc_compressed_curve *curve1, *curve2, *curve3, *curve4;
    uint8_t is_reversed1, is_reversed2, is_reversed3, is_reversed4;
    prc_compressed_curve *c_maj1, *c_maj2, *c_min1, *c_min2;
    prc_hcg_circle_information maj_info1, maj_info2, min_info1, min_info2;
    prc_vec3 axis_dir, mid_minor, w_vec, torus_center, radial_vec;
    double t_param, r_min1, r_min2;

    /* 1. Extract the 4 compressed curves from the ISO loop */
    curve1 = prc_get_compressed_curve_from_iso_loop(ctx, loop, 0, &is_reversed1);
    curve2 = prc_get_compressed_curve_from_iso_loop(ctx, loop, 1, &is_reversed2);
    curve3 = prc_get_compressed_curve_from_iso_loop(ctx, loop, 2, &is_reversed3);
    curve4 = prc_get_compressed_curve_from_iso_loop(ctx, loop, 3, &is_reversed4);

    /* Categorize into major and minor pairs depending on first_curve_is_major flag */
    if (first_curve_is_major)
    {
        c_maj1 = curve1;
        c_min1 = curve2;
        c_maj2 = curve3;
        c_min2 = curve4;
    }
    else
    {
        c_min1 = curve1;
        c_maj1 = curve2;
        c_min2 = curve3;
        c_maj2 = curve4;
    }

    /* 2. Unpack geometry info for all 4 circles */
    code = prc_get_hcg_circle_data(ctx, &c_maj1->hcg_circle, &maj_info1, compressed_data);
    if (code < 0)
    {
        prc_error(ctx, code, "Failed in prc_get_hcg_circle_data\n");
        return code;
    }
    code = prc_get_hcg_circle_data(ctx, &c_maj2->hcg_circle, &maj_info2, compressed_data);
    if (code < 0)
    {
        prc_error(ctx, code, "Failed in prc_get_hcg_circle_data\n");
        return code;
    }
    code = prc_get_hcg_circle_data(ctx, &c_min1->hcg_circle, &min_info1, compressed_data);
    if (code < 0)
    {
        prc_error(ctx, code, "Failed in prc_get_hcg_circle_data\n");
        return code;
    }
    code = prc_get_hcg_circle_data(ctx, &c_min2->hcg_circle, &min_info2, compressed_data);
    if (code < 0)
    {
        prc_error(ctx, code, "Failed in prc_get_hcg_circle_data\n");
        return code;
    }

    /* 3. Determine the central axis direction vector (Z-axis of the torus) */
    prc_vec_sub(maj_info2.center, maj_info1.center, &axis_dir);
    code = prc_vec_normalize_sqrt(&axis_dir);
    if (code < 0)
    {
        prc_error(ctx, code, "Failed to normalize torus axis direction\n");
        return code;
    }

    /* 4. Find Torus Center using Line-Plane intersection
          The line passes through maj_info1.center with direction axis_dir.
          The plane passes through the midpoint of the minor circle centers. */
    mid_minor.x = 0.5 * (min_info1.center.x + min_info2.center.x);
    mid_minor.y = 0.5 * (min_info1.center.y + min_info2.center.y);
    mid_minor.z = 0.5 * (min_info1.center.z + min_info2.center.z);

    prc_vec_sub(mid_minor, maj_info1.center, &w_vec);
    t_param = prc_vec_dot_product(w_vec, axis_dir);

    torus_center.x = maj_info1.center.x + (t_param * axis_dir.x);
    torus_center.y = maj_info1.center.y + (t_param * axis_dir.x);
    torus_center.z = maj_info1.center.z + (t_param * axis_dir.z);

    // Correcting component extraction for safety:
    torus_center.y = maj_info1.center.y + (t_param * axis_dir.y);

    /* 5. Compute Radii
          Minor radius is the radius of the minor circles */
    prc_vec_sub(min_info1.start_point, min_info1.center, &w_vec);
    r_min1 = prc_vec_length_sqrt(w_vec);
    prc_vec_sub(min_info2.start_point, min_info2.center, &w_vec);
    r_min2 = prc_vec_length_sqrt(w_vec);
    *minor_radius = 0.5 * (r_min1 + r_min2);

    /* Major radius is the distance from the torus center to a minor circle center */
    prc_vec_sub(min_info1.center, torus_center, &w_vec);
    *major_radius = prc_vec_length_sqrt(w_vec);

    /* 6. Construct the 3D Coordinate Transform Matrix */
    *transform = (prc_trans_3d){ 0 };
    transform->behavior = PRC_TRANSFORMATION_Translate | PRC_TRANSFORMATION_Rotate;
    transform->translation = torus_center;

    /* X-Axis: We can define the local X-axis by projecting the vector from
       the torus center to the first minor circle center onto the equatorial plane */
    prc_vec_sub(min_info1.center, torus_center, &radial_vec);
    double dot_proj = prc_vec_dot_product(radial_vec, axis_dir);
    transform->rotation[0].x = radial_vec.x - (dot_proj * axis_dir.x);
    transform->rotation[0].y = radial_vec.y - (dot_proj * axis_dir.y);
    transform->rotation[0].z = radial_vec.z - (dot_proj * axis_dir.z);

    code = prc_vec_normalize_sqrt(&transform->rotation[0]);
    if (code < 0)
    {
        prc_error(ctx, code, "Failed to normalize local X-axis for torus transform\n");
        return code;
    }

    /* Y-Axis: Computed via the cross product of the Z-axis (axis_dir) and local X-axis */
    prc_vec_cross(axis_dir, transform->rotation[0], &transform->rotation[1]);
    code = prc_vec_normalize_sqrt(&transform->rotation[1]);
    if (code < 0)
    {
        prc_error(ctx, code, "Failed to normalize local Y-axis for torus transform\n");
        return code;
    }

    /* Check if the canonical geometry aligns exactly with standard global orientation */
    const double eps = 1e-12;
    if (fabs(torus_center.x) < eps &&
        fabs(torus_center.y) < eps &&
        fabs(torus_center.z) < eps &&
        fabs(axis_dir.x) < eps &&
        fabs(axis_dir.y) < eps &&
        fabs(axis_dir.z - 1.0) < eps &&
        fabs(transform->rotation[0].x - 1.0) < eps &&
        fabs(transform->rotation[0].y) < eps &&
        fabs(transform->rotation[0].z) < eps)
    {
        *transform = (prc_trans_3d){ 0 };
        *has_transform = 0;
    }
    else
    {
        *has_transform = 1;
    }

    return 0;
}

/* From the iso curve data figure out the sphere radius and the transform
   so that we can represent the ISO surface with the uncompressed one */
static int
prc_get_sphere_data_from_iso_curves(prc_context *ctx, prc_ptr_topology *loop,
    double *radius, prc_trans_3d *transform, uint8_t *has_transform,
    prc_nano_brep_compressed_data *compressed_data)
{
    int code = 0;
    prc_compressed_curve *curve1, *curve2;
    uint8_t is_reversed1, is_reversed2;
    prc_hcg_circle_information circle1_info, circle2_info;
    prc_vec3 sphere_center = { 0 };

    /* Lets dig in and get curve1 and curve2. They should both be circles */
    curve1 = prc_get_compressed_curve_from_iso_loop(ctx, loop, 0, &is_reversed1);
    curve2 = prc_get_compressed_curve_from_iso_loop(ctx, loop, 1, &is_reversed2);

    /* Now get the circle information for each of the curves */
    code = prc_get_hcg_circle_data(ctx, &curve1->hcg_circle, &circle1_info, compressed_data);
    if (code < 0)
    {
        prc_error(ctx, code, "Failed to get HCG circle data in prc_get_sphere_data_from_iso_curves\n");
        return code;
    }
    code = prc_get_hcg_circle_data(ctx, &curve2->hcg_circle, &circle2_info, compressed_data);
    if (code < 0)
    {
        prc_error(ctx, code, "Failed to get HCG circle data in prc_get_sphere_data_from_iso_curves\n");
        return code;
    }

    /* From the above circle information, we can determine the sphere center
       and its radius */
    /* Every iso curve on a sphere is a circle of latitude or longitude. The
      center of the sphere is the intersection of the normals of these circles.
      here curve1 and curve2 are never the parallel case since they must intersect */

     /* 1. Calculate the sphere center by finding the intersection of the two normal axes:
           L1 = C1 + t*N1  and  L2 = C2 + s*N2 */
    prc_vec3 cross_normals;
    prc_vec_cross(circle1_info.normal, circle2_info.normal, &cross_normals);

    double cross_len_sq = (cross_normals.x * cross_normals.x) +
        (cross_normals.y * cross_normals.y) +
        (cross_normals.z * cross_normals.z);

    prc_vec3 c2_minus_c1;
    prc_vec_sub(circle2_info.center, circle1_info.center, &c2_minus_c1);

    prc_vec3 c2_minus_c1_cross_n2;
    prc_vec_cross(c2_minus_c1, circle2_info.normal, &c2_minus_c1_cross_n2);

    /* Dot product of (C2-C1)xN2 and N1xN2 determines the scale factor 't' for Line 1 */
    double dot_numerator;
    dot_numerator = prc_vec_dot_product(c2_minus_c1_cross_n2, cross_normals);
    double t = dot_numerator / cross_len_sq;

    /* Sphere center = C1 + t * N1 */
    sphere_center.x = circle1_info.center.x + (t * circle1_info.normal.x);
    sphere_center.y = circle1_info.center.y + (t * circle1_info.normal.y);
    sphere_center.z = circle1_info.center.z + (t * circle1_info.normal.z);

    /* 2. Compute the sphere radius.
          The radius is the direct 3D distance from the sphere center to the circle's perimeter. */
    prc_vec3 radius_vec;
    prc_vec_sub(circle1_info.start_point, sphere_center, &radius_vec);
    *radius = prc_vec_length_sqrt(radius_vec);

    /* Now that we have the center of the sphere. We need to
       create a transform that will take the sphere from its default position
       to the position defined by the sphere center */
    *transform = (prc_trans_3d){ 0 };

    if (sphere_center.x == 0 && sphere_center.y == 0 && sphere_center.z == 0)
    {
        *has_transform = 0;
    }
    else
    {
        *has_transform = 1;
        transform->behavior = PRC_TRANSFORMATION_Translate;

        /*  Set the translation directly to the circle's center */
        transform->translation = sphere_center;
    }
    return 0;
}

int
prc_tessellate_compressed_face(prc_context *ctx, prc_data *data, uint32_t shell_index, uint32_t face_index,
    prc_compressed_face *topo_face, prc_nano_brep_compressed_data *compressed_data)
{
    int code = 0;
    uint8_t free_loops = 0;
    prc_topo_face synthetic_face = { 0 };
    prc_type_surf synthetic_surface = { 0 };
    prc_exact_geom_wire_data *wire_data;

    synthetic_face.is_synthetic = 1;

    switch (topo_face->tag)
    {
    case PRC_HCG_IsoPlane:
    {
        prc_hcg_iso_plane hcg_iso_plane = topo_face->hcg_iso_plane;
        break;
    }

    case PRC_HCG_IsoCylinder:
    {
        prc_hcg_iso_cylinder *hcg_iso_cylinder = &topo_face->hcg_iso_cylinder;
        prc_content_compressed_face *face = &hcg_iso_cylinder->face;
        prc_surf_cylinder cylinder = { 0 };
        prc_topo_face synthetic_face = { 0 };
        prc_type_surf synthetic_surface = { 0 };
        uint8_t is_an_iso_face = face->is_an_iso_face;

        free_loops = 1;
        if (!is_an_iso_face)
        {
            prc_error(ctx, PRC_ERROR_PARSE, "ISO face is an Analytical face in prc_tessellate_compressed_face\n");
            return PRC_ERROR_PARSE;
        }

        synthetic_surface.surface_type = PRC_TYPE_SURF_Cylinder;
        cylinder.tag = PRC_TYPE_SURF_Cylinder;
        synthetic_surface.surf_cylinder = &cylinder;
        synthetic_face.has_tolerance = 0;
        synthetic_face.surface_geometry.is_referenced = 0;
        synthetic_face.surface_geometry.surface = synthetic_surface;
        synthetic_face.is_trimmed = 0;
        synthetic_face.tag = PRC_TYPE_TOPO_Face;
        synthetic_face.index_of_outer_loop = -1;
        synthetic_face.number_of_loops = 1;
        cylinder.parameterization.u_param_coeff_a = 1;
        cylinder.parameterization.v_param_coeff_a = 1;
        cylinder.parameterization.u_param_coeff_b = 0;
        cylinder.parameterization.v_param_coeff_b = 0;
        cylinder.parameterization.swap_uv = 0;
        cylinder.parameterization.surface_domain.max_uv.x = 2 * PRC_PI;
        cylinder.parameterization.surface_domain.max_uv.y = 2 * PRC_PI;
        cylinder.parameterization.surface_domain.min_uv.x = 0;
        cylinder.parameterization.surface_domain.min_uv.y = 0;

        code = prc_construct_loop_from_iso_curves(ctx, face, &synthetic_face,
            0, 0, compressed_data);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed to construct loop in prc_tessellate_compressed_face\n");
            goto free_loops;
        }

        /* From the loop we need to get the parameters of the cylinder so
           that we can determine the transformation as well the radius */
        code = prc_get_cylinder_data_from_iso_curves(ctx, synthetic_face.loops,
            &cylinder.radius, &cylinder.transform, &cylinder.has_transform, compressed_data);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed in prc_get_cylinder_data_from_iso_curves for compressed ISO cylinder\n");
            goto free_loops;
        }

        code = prc_tessellate_surface(ctx, data, shell_index, face_index, &synthetic_face,
            face->orientation_surface_with_shell, NULL, NULL);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed in prc_tessellate_surface for compressed ISO cylinder\n");
            goto free_loops;
        }

        break;
    }

    case PRC_HCG_IsoTorus:
    {
        /* is_major_radius TRUE indicates if the first serialized circle defines
           the major radius. */
        prc_hcg_iso_torus *hcg_iso_torus = &topo_face->hcg_iso_torus;
        prc_content_compressed_face *face = &hcg_iso_torus->face;
        prc_surf_torus torus = { 0 };
        prc_topo_face synthetic_face = { 0 };
        prc_type_surf synthetic_surface = { 0 };
        uint8_t is_an_iso_face = face->is_an_iso_face;

        free_loops = 1;
        if (!is_an_iso_face)
        {
            prc_error(ctx, PRC_ERROR_PARSE, "ISO face is an Analytical face in prc_tessellate_compressed_face\n");
            return PRC_ERROR_PARSE;
        }

        synthetic_surface.surface_type = PRC_TYPE_SURF_Torus;
        torus.tag = PRC_TYPE_SURF_Torus;
        synthetic_surface.surf_torus = &torus;
        synthetic_face.has_tolerance = 0;
        synthetic_face.surface_geometry.is_referenced = 0;
        synthetic_face.surface_geometry.surface = synthetic_surface;
        synthetic_face.is_trimmed = 0;
        synthetic_face.tag = PRC_TYPE_TOPO_Face;
        synthetic_face.index_of_outer_loop = -1;
        synthetic_face.number_of_loops = 1;
        torus.parameterization.u_param_coeff_a = 1;
        torus.parameterization.v_param_coeff_a = 1;
        torus.parameterization.u_param_coeff_b = 0;
        torus.parameterization.v_param_coeff_b = 0;
        torus.parameterization.swap_uv = 0;
        torus.parameterization.surface_domain.max_uv.x = 2 * PRC_PI;
        torus.parameterization.surface_domain.max_uv.y = 2 * PRC_PI;
        torus.parameterization.surface_domain.min_uv.x = 0;
        torus.parameterization.surface_domain.min_uv.y = 0;

        /* hcg_iso_torus->is_major_radius is true if first curve is the major
           radius */
        code = prc_construct_loop_from_iso_curves(ctx, face, &synthetic_face,
                                                  1, hcg_iso_torus->is_major_radius,
                                                  compressed_data);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed to construct loop in prc_tessellate_compressed_face\n");
            goto free_loops;
        }

        /* From the loop we need to get the parameters of the torus so
            that we can determine the transformation as well the major and minor
            radius */
        code = prc_get_torus_data_from_iso_curves(ctx, synthetic_face.loops,
            hcg_iso_torus->is_major_radius, &torus.major_radius,
            &torus.minor_radius, &torus.transform, &torus.has_transform,
            compressed_data);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed in prc_get_torus_data_from_iso_curves for compressed ISO torus\n");
            goto free_loops;
        }

        code = prc_tessellate_surface(ctx, data, shell_index, face_index, &synthetic_face,
            face->orientation_surface_with_shell, NULL, NULL);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed in prc_tessellate_surface for compressed ISO torus\n");
            goto free_loops;
        }
        break;
    }

    case PRC_HCG_IsoSphere:
    {
        prc_hcg_iso_sphere *hcg_iso_sphere = &topo_face->hcg_iso_sphere;
        prc_content_compressed_face *face = &hcg_iso_sphere->face;
        prc_surf_sphere sphere = { 0 };
        prc_topo_face synthetic_face = { 0 };
        prc_type_surf synthetic_surface = { 0 };
        uint8_t is_an_iso_face = face->is_an_iso_face;

        free_loops = 1;
        if (!is_an_iso_face)
        {
            prc_error(ctx, PRC_ERROR_PARSE, "ISO face is an Analytical face in prc_tessellate_compressed_face\n");
            return PRC_ERROR_PARSE;
        }

        synthetic_surface.surface_type = PRC_TYPE_SURF_Sphere;
        sphere.tag = PRC_TYPE_SURF_Sphere;
        synthetic_surface.surf_sphere = &sphere;
        synthetic_face.has_tolerance = 0;
        synthetic_face.surface_geometry.is_referenced = 0;
        synthetic_face.surface_geometry.surface = synthetic_surface;
        synthetic_face.is_trimmed = 0;
        synthetic_face.tag = PRC_TYPE_TOPO_Face;
        synthetic_face.index_of_outer_loop = -1;
        synthetic_face.number_of_loops = 1;
        sphere.parameterization.u_param_coeff_a = 1;
        sphere.parameterization.v_param_coeff_a = 1;
        sphere.parameterization.u_param_coeff_b = 0;
        sphere.parameterization.v_param_coeff_b = 0;
        sphere.parameterization.swap_uv = 0;
        sphere.parameterization.surface_domain.max_uv.x = 2 * PRC_PI;
        sphere.parameterization.surface_domain.max_uv.y = PRC_PI / 2;
        sphere.parameterization.surface_domain.min_uv.x = 0;
        sphere.parameterization.surface_domain.min_uv.y = -PRC_PI / 2;

        code = prc_construct_loop_from_iso_curves(ctx, face, &synthetic_face,
            0, 0, compressed_data);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed to construct loop in prc_tessellate_compressed_face\n");
            goto free_loops;
        }

        /* From the loop we need to get the parameters of the sphere so
            that we can determine the transformation as well the radius */
        code = prc_get_sphere_data_from_iso_curves(ctx, synthetic_face.loops,
            &sphere.radius, &sphere.transform, &sphere.has_transform, compressed_data);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed in prc_get_sphere_data_from_iso_curves for compressed ISO sphere\n");
            goto free_loops;
        }

        code = prc_tessellate_surface(ctx, data, shell_index, face_index, &synthetic_face,
            face->orientation_surface_with_shell, NULL, NULL);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed in prc_tessellate_surface for compressed ISO sphere\n");
            goto free_loops;
        }

        break;
    }

    case PRC_HCG_IsoCone:
    {
        prc_hcg_iso_cone *hcg_iso_cone = &topo_face->hcg_iso_cone;
        prc_content_compressed_face *face = &hcg_iso_cone->face;
        prc_surf_cone cone = { 0 };
        prc_topo_face synthetic_face = { 0 };
        prc_type_surf synthetic_surface = { 0 };
        uint8_t is_an_iso_face = face->is_an_iso_face;

        free_loops = 1;
        if (!is_an_iso_face)
        {
            prc_error(ctx, PRC_ERROR_PARSE, "ISO face is an Analytical face in prc_tessellate_compressed_face\n");
            return PRC_ERROR_PARSE;
        }

        synthetic_surface.surface_type = PRC_TYPE_SURF_Cone;
        cone.tag = PRC_TYPE_SURF_Cone;
        synthetic_surface.surf_cone = &cone;
        synthetic_face.has_tolerance = 0;
        synthetic_face.surface_geometry.is_referenced = 0;
        synthetic_face.surface_geometry.surface = synthetic_surface;
        synthetic_face.is_trimmed = 0;
        synthetic_face.tag = PRC_TYPE_TOPO_Face;
        synthetic_face.index_of_outer_loop = -1;
        synthetic_face.number_of_loops = 1;
        cone.parameterization.u_param_coeff_a = 1;
        cone.parameterization.v_param_coeff_a = 1;
        cone.parameterization.u_param_coeff_b = 0;
        cone.parameterization.v_param_coeff_b = 0;
        cone.parameterization.swap_uv = 0;
        cone.parameterization.surface_domain.max_uv.x = 2 * PRC_PI;
        cone.parameterization.surface_domain.max_uv.y = 12345;
        cone.parameterization.surface_domain.min_uv.x = 0;
        cone.parameterization.surface_domain.min_uv.y = -12345;

        code = prc_construct_loop_from_iso_curves(ctx, face, &synthetic_face,
            0, 0, compressed_data);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed to construct loop in prc_tessellate_compressed_face\n");
            goto free_loops;
        }

        /* From the loop we need to get the parameters of the cone so
            that we can determine the transformation as well the radius and semi-angle */
        code = prc_get_cone_data_from_iso_curves(ctx, synthetic_face.loops,
            &cone.radius, &cone.semi_angle, &cone.transform, &cone.has_transform,
            compressed_data);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed in prc_get_cone_data_from_iso_curves for compressed ISO cone\n");
            goto free_loops;
        }

        code = prc_tessellate_surface(ctx, data, shell_index, face_index, &synthetic_face,
            face->orientation_surface_with_shell, NULL, NULL);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed in prc_tessellate_surface for compressed ISO cone\n");
            goto free_loops;
        }
        break;
    }

    case PRC_HCG_AnaPlane:
    {
        prc_hcg_ana_plane hcg_ana_plane = topo_face->hcg_ana_plane;
        double x_unit_plane_normal = hcg_ana_plane.x;
        double y_unit_plane_normal = hcg_ana_plane.y;
        uint8_t positive_z = hcg_ana_plane.positive_z; /* True if z normal is > 0.0 */
        prc_content_compressed_face *face = &hcg_ana_plane.face;
        uint8_t orientation_surface_with_shell = face->orientation_surface_with_shell;
        uint8_t is_an_iso_face = face->is_an_iso_face;
        prc_surf_plane plane = { 0 };

        free_loops = 1;
        if (is_an_iso_face)
        {
            prc_error(ctx, PRC_ERROR_PARSE, "Analytic plane face is an ISO face in prc_tessellate_compressed_face\n");
            return PRC_ERROR_PARSE;
        }

        prc_content_compressed_ana_face *ana_face = &face->ana_face;

        /* Now we need to build a synthetic uncompressed plane with loops */
        uint32_t num_loop = 0;

        synthetic_surface.surface_type = PRC_TYPE_SURF_Plane;
        synthetic_surface.surf_plane = &plane;

        plane.domain.max_uv.x = 1245;
        plane.domain.max_uv.y = 1245;
        plane.domain.min_uv.x = -1245;
        plane.domain.min_uv.y = -1245;

        plane.tag = PRC_TYPE_SURF_Plane;

        plane.u_parameter_coeff_a = 1.0;
        plane.u_parameter_coeff_b = 0.0;
        plane.v_parameter_coeff_a = 1.0;
        plane.v_parameter_coeff_b = 0.0;

        synthetic_face.has_tolerance = 0;
        synthetic_face.surface_geometry.is_referenced = 0;
        synthetic_face.surface_geometry.surface = synthetic_surface;
        synthetic_face.is_trimmed = 0;
        synthetic_face.tag = PRC_TYPE_TOPO_Face;
        synthetic_face.index_of_outer_loop = -1;
        synthetic_face.number_of_loops = ana_face->trim_loop_count;

        /* The plane is stored as its unit normal alone; its origin is the
           first vertex of the loop (ISO 14739-1, PRC_HCG_AnaPlane), so the
           loops are read first. Without this the plane lay through the
           origin with normal +z whatever the file said: a face at z = -5
           was drawn at z = 0, and a tilted face flat in the xy plane. */
        if (!ana_face->is_trimmed || ana_face->trim_loop_count == 0)
        {
            prc_error(ctx, PRC_ERROR_PARSE, "Analytic plane face has no loop to place it in prc_tessellate_compressed_face\n");
            return PRC_ERROR_PARSE;
        }
        wire_data = (prc_exact_geom_wire_data *)prc_calloc(ctx,
            ana_face->trim_loop_count, sizeof(prc_exact_geom_wire_data));
        if (wire_data == NULL)
        {
            prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate memory for wire_data in prc_tessellate_compressed_face\n");
            return PRC_ERROR_MEMORY;
        }
        code = prc_convert_ana_loops_to_std_loops(ctx, &synthetic_face,
            ana_face, compressed_data, wire_data);
        if (code < 0 || wire_data[0].number_of_points == 0)
        {
            if (code >= 0)
            {
                code = PRC_ERROR_PARSE;
                prc_error(ctx, code, "Analytic plane face loop has no vertex in prc_tessellate_compressed_face\n");
            }
            else
                prc_error(ctx, code, "Failed in prc_convert_ana_loops_to_std_loops\n");
            for (uint32_t i = 0; i < ana_face->trim_loop_count; ++i)
                prc_free(ctx, wire_data[i].points);
            prc_free(ctx, wire_data);
            goto free_loops;
        }
        {
            prc_vec3 origin = wire_data[0].points[0];
            prc_vec3 z_basis, x_basis, y_basis;
            prc_vec3 arb_vec = { 1.0, 0.0, 0.0 };
            double zz = 1.0 - x_unit_plane_normal * x_unit_plane_normal - y_unit_plane_normal * y_unit_plane_normal;
            double len;

            z_basis.x = x_unit_plane_normal;
            z_basis.y = y_unit_plane_normal;
            z_basis.z = zz > 0.0 ? sqrt(zz) : 0.0;
            if (!positive_z)
                z_basis.z = -z_basis.z;
            len = sqrt(z_basis.x * z_basis.x + z_basis.y * z_basis.y + z_basis.z * z_basis.z);
            if (len > 1e-9)
            {
                z_basis.x /= len;
                z_basis.y /= len;
                z_basis.z /= len;
            }
            else
            {
                z_basis.x = 0.0;
                z_basis.y = 0.0;
                z_basis.z = 1.0;
            }

            /* Any x and y that complete a right-handed frame: the plane's
               own parameterization has no preferred direction in the file. */
            if (fabs(z_basis.x) > 0.9)
            {
                arb_vec.x = 0.0;
                arb_vec.y = 1.0;
            }
            prc_vec_cross(arb_vec, z_basis, &x_basis);
            len = sqrt(x_basis.x * x_basis.x + x_basis.y * x_basis.y + x_basis.z * x_basis.z);
            x_basis.x /= len;
            x_basis.y /= len;
            x_basis.z /= len;
            prc_vec_cross(z_basis, x_basis, &y_basis);

            plane.transform.behavior = PRC_TRANSFORMATION_Translate | PRC_TRANSFORMATION_Rotate;
            plane.transform.translation = origin;
            plane.transform.rotation[0] = x_basis;
            plane.transform.rotation[1] = y_basis;
            plane.transform.scale = 1.0;
        }
        for (uint32_t i = 0; i < ana_face->trim_loop_count; ++i)
            prc_free(ctx, wire_data[i].points);
        prc_free(ctx, wire_data);
        wire_data = NULL;

        code = prc_tessellate_surface(ctx, data, shell_index, face_index, &synthetic_face,
            face->orientation_surface_with_shell, NULL, NULL);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed in prc_tessellate_surface for PRC_HCG_AnaPlane\n");
            goto free_loops;
        }
        break;
    }

    case PRC_HCG_AnaCylinder:
    {
        prc_hcg_ana_cylinder hcg_ana_cylinder = topo_face->hcg_ana_cylinder;
        prc_vec3 point_on_axis, cylinder_axis_direction;
        prc_content_compressed_face *face = &hcg_ana_cylinder.face;
        uint8_t orientation_surface_with_shell = face->orientation_surface_with_shell;
        uint8_t is_an_iso_face = face->is_an_iso_face;
        prc_surf_cylinder cylinder = { 0 };
        prc_vec3 curve_points[ANA_CURVE_SAMPLE];

        free_loops = 1;
        if (is_an_iso_face)
        {
            prc_error(ctx, PRC_ERROR_PARSE, "Analytic plane face is an ISO face in prc_tessellate_compressed_face\n");
            return PRC_ERROR_PARSE;
        }

        prc_content_compressed_ana_face *ana_face = &face->ana_face;

        /* Now we need to build a synthetic uncompressed torus with loops */
        uint32_t num_loop = 0;

        synthetic_surface.surface_type = PRC_TYPE_SURF_Cylinder;
        cylinder.tag = PRC_TYPE_SURF_Cylinder;
        synthetic_surface.surf_cylinder = &cylinder;
        prc_get_compressed_point(ctx, &hcg_ana_cylinder.point, &point_on_axis);
        prc_get_compressed_point(ctx, &hcg_ana_cylinder.direction, &cylinder_axis_direction);

        /* The radius of the cylinder is found by looking at the curves
           and obtain an average radius when projected onto the axis */
        cylinder.parameterization.u_param_coeff_a = 1;
        cylinder.parameterization.v_param_coeff_a = 1;
        cylinder.parameterization.u_param_coeff_b = 0;
        cylinder.parameterization.v_param_coeff_b = 0;
        cylinder.parameterization.swap_uv = 0;
        cylinder.parameterization.surface_domain.max_uv.x = 2 * PRC_PI;
        cylinder.parameterization.surface_domain.max_uv.y = 2 * PRC_PI;
        cylinder.parameterization.surface_domain.min_uv.x = 0;
        cylinder.parameterization.surface_domain.min_uv.y = 0;

        synthetic_face.has_tolerance = 0;
        synthetic_face.surface_geometry.is_referenced = 0;
        synthetic_face.surface_geometry.surface = synthetic_surface;
        synthetic_face.is_trimmed = 0;
        synthetic_face.tag = PRC_TYPE_TOPO_Face;
        synthetic_face.index_of_outer_loop = -1;
        synthetic_face.number_of_loops = ana_face->trim_loop_count;

        /* Compute translation and rotation data to get map from a z-axis
           to the point_on_axis, cylinder_axis_direction form (this will be
           encoded).  Also compute the inverse of this, so that we can apply
           this to the curve data points to figure out the radius. We only sample
           one curve at 4 points to get an idea of the radius. */
        prc_vec3 z_basis = cylinder_axis_direction;

        // Normalize z_basis
        double len_z = sqrt(z_basis.x * z_basis.x + z_basis.y * z_basis.y + z_basis.z * z_basis.z);
        if (len_z > 1e-9)
        {
            z_basis.x /= len_z;
            z_basis.y /= len_z;
            z_basis.z /= len_z;
        }
        else
        {
            z_basis.x = 0.0;
            z_basis.y = 0.0;
            z_basis.z = 1.0;
        }

        // Find an arbitrary non-collinear vector to cross with z_basis to find x_basis
        prc_vec3 arb_vec = { 1.0, 0.0, 0.0 };
        if (fabs(z_basis.x) > 0.9)
        {
            arb_vec.x = 0.0; arb_vec.y = 1.0; arb_vec.z = 0.0;
        }

        prc_vec3 x_basis, y_basis;
        // x_basis = arb_vec X z_basis
        prc_vec_cross(arb_vec, z_basis, &x_basis);
        double len_x = sqrt(x_basis.x * x_basis.x + x_basis.y * x_basis.y + x_basis.z * x_basis.z);
        x_basis.x /= len_x;
        x_basis.y /= len_x;
        x_basis.z /= len_x;

        // y_basis = z_basis X x_basis
        prc_vec_cross(z_basis, x_basis, &y_basis);

        // Populate the standard geometry transformation matrix (Canonical -> Model Space)
        // matching the matrix format expected by prc_exact_geom_set_transform
        cylinder.has_transform = 1;
        cylinder.transform.behavior = PRC_TRANSFORMATION_Translate | PRC_TRANSFORMATION_Rotate;
        cylinder.transform.translation = point_on_axis;

        // Populate the 2-vector rotation matrix array (X and Y basis vectors)
        cylinder.transform.rotation[0] = x_basis;
        cylinder.transform.rotation[1] = y_basis;
        cylinder.transform.scale = 1.0;

        /* =================================================================
           POPULATE THE INVERSE TRANSFORMATION (Model Space -> Canonical Z-Axis)
           ================================================================= */
        prc_trans_3d inv_transform = { 0 };
        inv_transform.behavior = PRC_TRANSFORMATION_Translate | PRC_TRANSFORMATION_Rotate;
        inv_transform.scale = 1.0;

        // 1. Inverse Rotation: Transpose of the forward rotation matrix
        // Row 0 of R^T is Column 0 of R (which is x_basis)
        inv_transform.rotation[0].x = x_basis.x;
        inv_transform.rotation[0].y = y_basis.x;
        inv_transform.rotation[0].z = z_basis.x;

        // Row 1 of R^T is Column 1 of R (which is y_basis)
        inv_transform.rotation[1].x = x_basis.y;
        inv_transform.rotation[1].y = y_basis.y;
        inv_transform.rotation[1].z = z_basis.y;

        // 2. Inverse Translation: T_inv = -R^T * point_on_axis
        inv_transform.translation.x = -(point_on_axis.x * x_basis.x + point_on_axis.y * x_basis.y + point_on_axis.z * x_basis.z);
        inv_transform.translation.y = -(point_on_axis.x * y_basis.x + point_on_axis.y * y_basis.y + point_on_axis.z * y_basis.z);
        inv_transform.translation.z = -(point_on_axis.x * z_basis.x + point_on_axis.y * z_basis.y + point_on_axis.z * z_basis.z);

        // 3. Generate the 4x4 inverse matrix
        prc_exact_geom_transform inv_geom_matrix;
        prc_exact_geom_set_transform(ctx, &inv_geom_matrix, &inv_transform);

        /* Now the trimming data */
        if (ana_face->is_trimmed)
        {
            wire_data = (prc_exact_geom_wire_data *)prc_calloc(ctx,
                ana_face->trim_loop_count, sizeof(prc_exact_geom_wire_data));
            if (wire_data == NULL)
            {
                prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate memory for wire_data in prc_tessellate_compressed_face\n");
                return PRC_ERROR_MEMORY;
            }
            code = prc_convert_ana_loops_to_std_loops(ctx, &synthetic_face,
                ana_face, compressed_data, wire_data);
            if (code < 0)
            {
                prc_error(ctx, code, "Failed in prc_convert_ana_loops_to_std_loops\n");
                goto free_loops;
            }
            /* Take the data in wire_data, apply the transform to get us to
               our cylinder at the z-axis and then compute the mean of the
               sample distance from the z axis to get the radius just use
               loop 0 */
            uint32_t num_samples = wire_data[0].number_of_points;
            double radius_sum = 0.0;
            for (uint32_t i = 0; i < num_samples; ++i)
            {
                prc_vec3 transformed_point;
                prc_vec3 point = wire_data[0].points[i];
                /* TODO Apply the inverse transform to get to the z-axis cylinder */

                // Apply inverse rotation
                transformed_point = prc_exact_geom_apply_transform(ctx, &inv_geom_matrix, point);

                /* The radius is the distance from the z-axis (x=0,y=0) */
                double radius = sqrt(transformed_point.x * transformed_point.x + transformed_point.y * transformed_point.y);
                radius_sum += radius;
            }
            cylinder.radius = radius_sum / num_samples;
            for (uint32_t i = 0; i < ana_face->trim_loop_count; ++i)
            {
                prc_free(ctx, wire_data[i].points);
            }
            prc_free(ctx, wire_data);
        }
        else
        {
            /* If the face is not trimmed, this is an error because the cylinder
              must have trimming data to define its extent. */
            prc_error(ctx, PRC_ERROR_PARSE, "Analytic cylinder face is not trimmed in prc_tessellate_compressed_face\n");
            return PRC_ERROR_PARSE;
        }

        code = prc_tessellate_surface(ctx, data, shell_index, face_index, &synthetic_face,
            face->orientation_surface_with_shell, NULL, NULL);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed in prc_tessellate_surface for PRC_HCG_AnaPlane\n");
            goto free_loops;
        }
        break;
    }

    case PRC_HCG_AnaTorus:
    {
        prc_hcg_ana_torus hcg_ana_torus = topo_face->hcg_ana_torus;
        prc_vec3 torus_center, x_axis, y_axis;
        prc_content_compressed_face *face = &hcg_ana_torus.face;
        uint8_t orientation_surface_with_shell = face->orientation_surface_with_shell;
        uint8_t is_an_iso_face = face->is_an_iso_face;
        prc_surf_torus torus = { 0 };

        free_loops = 1;
        if (is_an_iso_face)
        {
            prc_error(ctx, PRC_ERROR_PARSE, "Analytic plane face is an ISO face in prc_tessellate_compressed_face\n");
            return PRC_ERROR_PARSE;
        }

        prc_content_compressed_ana_face *ana_face = &face->ana_face;

        /* Now we need to build a synthetic uncompressed torus with loops */
        uint32_t num_loop = 0;

        synthetic_surface.surface_type = PRC_TYPE_SURF_Torus;
        torus.tag = PRC_TYPE_SURF_Torus;
        synthetic_surface.surf_torus = &torus;
        prc_get_compressed_point(ctx, &hcg_ana_torus.center, &torus_center);
        prc_get_compressed_point(ctx, &hcg_ana_torus.x_axis, &x_axis);
        y_axis = hcg_ana_torus.y_axis;

        torus.major_radius = prc_vec_length_sqrt(x_axis);
        torus.minor_radius = prc_vec_length_sqrt(y_axis);

        /* The non-compressed one is centered at the origin */
        if (prc_vec_length_sqrt(torus_center) == 0)
        {
            torus.transform.behavior = 0;
            torus.exact_geom_transform.is_identity = 1;
        }
        else
        {
            /* Need to translate the torus */
            torus.has_transform = 1;
            torus.transform.behavior = PRC_TRANSFORMATION_Translate;
            torus.transform.translation = torus_center;
        }

        torus.parameterization.u_param_coeff_a = 1;
        torus.parameterization.v_param_coeff_a = 1;
        torus.parameterization.u_param_coeff_b = 0;
        torus.parameterization.v_param_coeff_b = 0;
        torus.parameterization.swap_uv = 0;
        torus.parameterization.surface_domain.max_uv.x = 2 * PRC_PI;
        torus.parameterization.surface_domain.max_uv.y = 2 * PRC_PI;
        torus.parameterization.surface_domain.min_uv.x = 0;
        torus.parameterization.surface_domain.min_uv.y = 0;

        synthetic_face.has_tolerance = 0;
        synthetic_face.surface_geometry.is_referenced = 0;
        synthetic_face.surface_geometry.surface = synthetic_surface;
        synthetic_face.is_trimmed = 0;
        synthetic_face.tag = PRC_TYPE_TOPO_Face;
        synthetic_face.index_of_outer_loop = -1;
        synthetic_face.number_of_loops = ana_face->trim_loop_count;

        /* Now the trimming data */
        if (ana_face->is_trimmed)
        {
            code = prc_convert_ana_loops_to_std_loops(ctx, &synthetic_face,
                ana_face, compressed_data, NULL);
            if (code < 0)
            {
                prc_error(ctx, code, "Failed in prc_convert_ana_loops_to_std_loops\n");
                goto free_loops;
            }
        }

        code = prc_tessellate_surface(ctx, data, shell_index, face_index, &synthetic_face,
            face->orientation_surface_with_shell, NULL, NULL);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed in prc_tessellate_surface for PRC_HCG_AnaPlane\n");
            goto free_loops;
        }
        break;
    }

    case PRC_HCG_AnaSphere:
    {
        prc_hcg_ana_sphere hcg_ana_sphere = topo_face->hcg_ana_sphere;
        prc_vec3 sphere_center;
        prc_content_compressed_face *face = &hcg_ana_sphere.face;
        uint8_t orientation_surface_with_shell = face->orientation_surface_with_shell;
        uint8_t is_an_iso_face = face->is_an_iso_face;
        prc_surf_sphere sphere = { 0 };
        prc_vec3 curve_points[ANA_CURVE_SAMPLE];

        free_loops = 1;
        if (is_an_iso_face)
        {
            prc_error(ctx, PRC_ERROR_PARSE, "Analytic face is an ISO face in prc_tessellate_compressed_face\n");
            return PRC_ERROR_PARSE;
        }

        prc_content_compressed_ana_face *ana_face = &face->ana_face;

        /* Now we need to build a synthetic uncompressed torus with loops */
        uint32_t num_loop = 0;

        synthetic_surface.surface_type = PRC_TYPE_SURF_Sphere;
        sphere.tag = PRC_TYPE_SURF_Sphere;
        synthetic_surface.surf_sphere = &sphere;
        prc_get_compressed_point(ctx, &hcg_ana_sphere.sphere_center, &sphere_center);

        /* The radius of the sphere is found by looking at the first point of
           the curve and getting its distance from the center */
        sphere.parameterization.u_param_coeff_a = 1;
        sphere.parameterization.v_param_coeff_a = 1;
        sphere.parameterization.u_param_coeff_b = 0;
        sphere.parameterization.v_param_coeff_b = 0;
        sphere.parameterization.swap_uv = 0;
        sphere.parameterization.surface_domain.max_uv.x = 2 * PRC_PI;
        sphere.parameterization.surface_domain.max_uv.y = PRC_HALF_PI;
        sphere.parameterization.surface_domain.min_uv.x = 0;
        sphere.parameterization.surface_domain.min_uv.y = -PRC_HALF_PI;

        synthetic_face.has_tolerance = 0;
        synthetic_face.surface_geometry.is_referenced = 0;
        synthetic_face.surface_geometry.surface = synthetic_surface;
        synthetic_face.is_trimmed = 0;
        synthetic_face.tag = PRC_TYPE_TOPO_Face;
        synthetic_face.index_of_outer_loop = -1;
        synthetic_face.number_of_loops = ana_face->trim_loop_count;

        /* If the center is anything other than [0, 0, 0] then we need
           to add a translation here */
        if (prc_vec_length_sqrt(sphere_center) > 1e-9)
        {
            sphere.has_transform = 1;
            sphere.transform.behavior = PRC_TRANSFORMATION_Translate;
            sphere.transform.translation = sphere_center;
        }
        else
        {
            sphere.has_transform = 0;
        }

        /* Now the trimming data */
        if (ana_face->is_trimmed)
        {
            wire_data = (prc_exact_geom_wire_data *)prc_calloc(ctx,
                ana_face->trim_loop_count, sizeof(prc_exact_geom_wire_data));
            if (wire_data == NULL)
            {
                prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate memory for wire_data in prc_tessellate_compressed_face\n");
                return PRC_ERROR_MEMORY;
            }
            code = prc_convert_ana_loops_to_std_loops(ctx, &synthetic_face,
                ana_face, compressed_data, wire_data);
            if (code < 0)
            {
                for (uint32_t i = 0; i < ana_face->trim_loop_count; ++i)
                {
                    prc_free(ctx, wire_data[i].points);
                }
                prc_free(ctx, wire_data);
                prc_error(ctx, code, "Failed in prc_convert_ana_loops_to_std_loops\n");
                goto free_loops;
            }

            /* Just the first point to compute the radius */
            prc_vec3 diff;
            prc_vec_sub(wire_data[0].points[0], sphere_center, &diff);
            sphere.radius = prc_vec_length_sqrt(diff);

            for (uint32_t i = 0; i < ana_face->trim_loop_count; ++i)
            {
                prc_free(ctx, wire_data[i].points);
            }
            prc_free(ctx, wire_data);
        }
        else
        {
            /* If the face is not trimmed, this is an error because the cylinder
              must have trimming data to define its extent. */
            prc_error(ctx, PRC_ERROR_PARSE, "Analytic cylinder face is not trimmed in prc_tessellate_compressed_face\n");
            return PRC_ERROR_PARSE;
        }

        code = prc_tessellate_surface(ctx, data, shell_index, face_index, &synthetic_face,
            face->orientation_surface_with_shell, NULL, NULL);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed in prc_tessellate_surface for PRC_HCG_AnaPlane\n");
            goto free_loops;
        }
        break;
    }

    case PRC_HCG_AnaCone:
    {
        prc_hcg_ana_cone hcg_ana_cone = topo_face->hcg_ana_cone;
        prc_vec3 axis_point, apex_point;
        prc_content_compressed_face *face = &hcg_ana_cone.face;
        uint8_t orientation_surface_with_shell = face->orientation_surface_with_shell;
        uint8_t is_an_iso_face = face->is_an_iso_face;
        prc_surf_cone cone = { 0 };
        prc_vec3 curve_points[ANA_CURVE_SAMPLE];

        free_loops = 1;
        if (is_an_iso_face)
        {
            prc_error(ctx, PRC_ERROR_PARSE, "Analytic face is an ISO face in prc_tessellate_compressed_face\n");
            return PRC_ERROR_PARSE;
        }

        prc_content_compressed_ana_face *ana_face = &face->ana_face;

        synthetic_surface.surface_type = PRC_TYPE_SURF_Cone;
        cone.tag = PRC_TYPE_SURF_Cone;
        synthetic_surface.surf_cone = &cone;
        prc_get_compressed_point(ctx, &hcg_ana_cone.axis_point, &axis_point);
        prc_get_compressed_point(ctx, &hcg_ana_cone.apex_point, &apex_point);

        cone.parameterization.u_param_coeff_a = 1;
        cone.parameterization.v_param_coeff_a = 1;
        cone.parameterization.u_param_coeff_b = 0;
        cone.parameterization.v_param_coeff_b = 0;
        cone.parameterization.swap_uv = 0;
        cone.parameterization.surface_domain.max_uv.x = 2 * PRC_PI;
        cone.parameterization.surface_domain.max_uv.y = 1245;
        cone.parameterization.surface_domain.min_uv.x = 0;
        cone.parameterization.surface_domain.min_uv.y = -1245;

        synthetic_face.has_tolerance = 0;
        synthetic_face.surface_geometry.is_referenced = 0;
        synthetic_face.surface_geometry.surface = synthetic_surface;
        synthetic_face.is_trimmed = 0;
        synthetic_face.tag = PRC_TYPE_TOPO_Face;
        synthetic_face.index_of_outer_loop = -1;
        synthetic_face.number_of_loops = ana_face->trim_loop_count;

        /* =================================================================
           1. COMPUTE FORWARD TRANSFORMATION BASIS VECTORS
           ================================================================= */
        prc_vec3 z_basis;
        prc_vec_sub(apex_point, axis_point, &z_basis);
        double len_z = sqrt(z_basis.x * z_basis.x + z_basis.y * z_basis.y + z_basis.z * z_basis.z);
        if (len_z > 1e-9)
        {
            z_basis.x /= len_z;
            z_basis.y /= len_z;
            z_basis.z /= len_z;
        }
        else
        {
            z_basis.x = 0.0; z_basis.y = 0.0; z_basis.z = 1.0;
        }

        prc_vec3 arb_vec = { 1.0, 0.0, 0.0 };
        if (fabs(z_basis.x) > 0.9)
        {
            arb_vec.x = 0.0; arb_vec.y = 1.0; arb_vec.z = 0.0;
        }

        prc_vec3 x_basis, y_basis;
        prc_vec_cross(arb_vec, z_basis, &x_basis);
        double len_x = sqrt(x_basis.x * x_basis.x + x_basis.y * x_basis.y + x_basis.z * x_basis.z);
        x_basis.x /= len_x; x_basis.y /= len_x; x_basis.z /= len_x;

        prc_vec_cross(z_basis, x_basis, &y_basis);

        /* Forward transform mapping Canonical space -> Model space */
        cone.has_transform = 1;
        cone.transform.behavior = PRC_TRANSFORMATION_Translate | PRC_TRANSFORMATION_Rotate;
        cone.transform.translation = axis_point;
        cone.transform.rotation[0] = x_basis;
        cone.transform.rotation[1] = y_basis;
        cone.transform.scale = 1.0;

        if (prc_exact_geom_set_transform(ctx, &cone.exact_geom_transform, &cone.transform) < 0)
        {
            prc_error(ctx, PRC_ERROR_INTERNAL, "Failed forward transform generation for cone\n");
            return PRC_ERROR_INTERNAL;
        }

        /* =================================================================
           2. BUILD INVERSE MATRIX SYSTEM (Model space -> Canonical space)
           ================================================================= */
        prc_trans_3d inv_transform = { 0 };
        inv_transform.behavior = PRC_TRANSFORMATION_Translate | PRC_TRANSFORMATION_Rotate;
        inv_transform.scale = 1.0;

        inv_transform.rotation[0].x = x_basis.x;
        inv_transform.rotation[0].y = y_basis.x;
        inv_transform.rotation[0].z = z_basis.x;

        inv_transform.rotation[1].x = x_basis.y;
        inv_transform.rotation[1].y = y_basis.y;
        inv_transform.rotation[1].z = z_basis.y;

        inv_transform.translation.x = -(axis_point.x * x_basis.x + axis_point.y * x_basis.y + axis_point.z * x_basis.z);
        inv_transform.translation.y = -(axis_point.x * y_basis.x + axis_point.y * y_basis.y + axis_point.z * y_basis.z);
        inv_transform.translation.z = -(axis_point.x * z_basis.x + axis_point.y * z_basis.y + axis_point.z * z_basis.z);

        prc_exact_geom_transform inv_geom_matrix;
        prc_exact_geom_set_transform(ctx, &inv_geom_matrix, &inv_transform);

        /* =================================================================
           3. RESOLVE CONE PARAMETERS USING THE STRIP LOOP AND APEX COORDS
           ================================================================= */
        if (ana_face->is_trimmed)
        {
            wire_data = (prc_exact_geom_wire_data *)prc_calloc(ctx,
                ana_face->trim_loop_count, sizeof(prc_exact_geom_wire_data));
            if (wire_data == NULL)
            {
                prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate memory for wire_data in prc_tessellate_compressed_face\n");
                return PRC_ERROR_MEMORY;
            }
            code = prc_convert_ana_loops_to_std_loops(ctx, &synthetic_face,
                ana_face, compressed_data, wire_data);
            if (code < 0)
            {
                prc_error(ctx, code, "Failed in prc_convert_ana_loops_to_std_loops\n");
                goto free_loops;
            }

            uint32_t loop_index = 0;
            uint32_t num_samples = wire_data[0].number_of_points;
            for (uint32_t i = 1; i < ana_face->trim_loop_count; i++)
            {
                if (wire_data[i].number_of_points > num_samples)
                {
                    loop_index = i;
                    num_samples = wire_data[i].number_of_points;
                }
            }

            if (num_samples >= 2)
            {
                /* A. Project the global model space apex point into canonical space */
                prc_vec3 canonical_apex = prc_exact_geom_apply_transform(ctx,
                    &inv_geom_matrix, apex_point);
                double v_apex = canonical_apex.z;

                /* B. Compute average radius and height from the un-collapsed boundary circle */
                double r_sum = 0.0;
                double v_sum = 0.0;

                for (uint32_t i = 0; i < num_samples; ++i)
                {
                    prc_vec3 canonical_pt = prc_exact_geom_apply_transform(ctx,
                        &inv_geom_matrix, wire_data[loop_index].points[i]);
                    r_sum += sqrt(canonical_pt.x * canonical_pt.x + canonical_pt.y * canonical_pt.y);
                    v_sum += canonical_pt.z;
                }
                double r_sample = r_sum / (double)num_samples;
                double v_sample = v_sum / (double)num_samples;

                /* C. Calculate properties cleanly using the apex height offset distance */
                double dv = v_sample - v_apex;
                if (fabs(dv) > 1e-9)
                {
                    double tan_alpha = r_sample / dv;
                    cone.semi_angle = atan(tan_alpha);
                    cone.radius = r_sample - (v_sample * tan_alpha);
                }
                else
                {
                    /* Fallback to cylinder geometry if loops flatten entirely */
                    cone.radius = r_sample;
                    cone.semi_angle = 0.0;
                }
            }
            else
            {
                prc_error(ctx, PRC_ERROR_PARSE, "Insufficient trimming loop samples to resolve cone coefficients\n");
                for (uint32_t i = 0; i < ana_face->trim_loop_count; ++i)
                {
                    prc_free(ctx, wire_data[i].points);
                }
                prc_free(ctx, wire_data);
                return PRC_ERROR_PARSE;
            }

            for (uint32_t i = 0; i < ana_face->trim_loop_count; ++i)
            {
                prc_free(ctx, wire_data[i].points);
            }
            prc_free(ctx, wire_data);
        }
        else
        {
            prc_error(ctx, PRC_ERROR_PARSE, "Analytic cone face is not trimmed in prc_tessellate_compressed_face\n");
            return PRC_ERROR_PARSE;
        }

        code = prc_tessellate_surface(ctx, data, shell_index, face_index,
            &synthetic_face, face->orientation_surface_with_shell, NULL, NULL);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed in prc_tessellate_surface for PRC_HCG_AnaCone\n");
            goto free_loops;
        }
        break;
    }

    case PRC_HCG_IsoNURBS:
    {
        prc_hcg_iso_nurbs hcg_iso_nurbs = topo_face->hcg_iso_nurbs;
        uint8_t orientation_surface_with_shell = hcg_iso_nurbs.orientation_surface_with_shell;
        uint8_t orientation_curve_with_surface = hcg_iso_nurbs.orientation_loop_with_surface;
        uint8_t *sense_array = hcg_iso_nurbs.sense_array;
        prc_compressed_nurbs *compressed_nurbs = &hcg_iso_nurbs.surface;
        prc_surf_nurbs nurbs = { 0 };
        double nurbs_tolerance = compressed_data->tolerance / 5.0;

        /* First just do the nurbs conversion then we have the curve insanity */
        code = prc_create_nurbs_from_compressed_nurbs(ctx, compressed_nurbs,
            &nurbs, nurbs_tolerance);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed in prc_create_nurbs_from_compressed_nurbs\n");
            return code;
        }

        synthetic_surface.surface_type = PRC_TYPE_SURF_NURBS;
        synthetic_surface.surf_nurbs = &nurbs;
        synthetic_face.has_tolerance = 0;
        synthetic_face.surface_geometry.is_referenced = 0;
        synthetic_face.surface_geometry.surface = synthetic_surface;
        synthetic_face.is_trimmed = 0;
        synthetic_face.tag = PRC_TYPE_TOPO_Face;
        synthetic_face.index_of_outer_loop = -1;

        /* Now the loops TODO WIP */
        //synthetic_face.number_of_loops = 

        break;
    }

    case PRC_HCG_AnaNURBS:
    {
        /* Convert to a non-compressed nurbs */
        prc_hcg_ana_nurbs hcg_ana_nurbs = topo_face->hcg_ana_nurbs;
        prc_content_compressed_face *face = &hcg_ana_nurbs.face;
        uint8_t orientation_surface_with_shell = face->orientation_surface_with_shell;
        uint8_t is_an_iso_face = face->is_an_iso_face;
        prc_surf_nurbs nurbs = { 0 };
        prc_vec3 curve_points[ANA_CURVE_SAMPLE];
        double nurbs_tolerance = compressed_data->tolerance / 5.0;

        free_loops = 1;
        if (is_an_iso_face)
        {
            prc_error(ctx, PRC_ERROR_PARSE, "Analytic face is an ISO face in prc_tessellate_compressed_face\n");
            return PRC_ERROR_PARSE;
        }

        prc_content_compressed_ana_face *ana_face = &face->ana_face;

        synthetic_surface.surface_type = PRC_TYPE_SURF_NURBS;
        synthetic_surface.surf_nurbs = &nurbs;

        code = prc_create_nurbs_from_compressed_nurbs(ctx,
            &hcg_ana_nurbs.compressed_surface, &nurbs, nurbs_tolerance);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed in prc_create_nurbs_from_compressed_nurbs\n");
            goto free_loops;
        }

        synthetic_face.has_tolerance = 0;
        synthetic_face.surface_geometry.is_referenced = 0;
        synthetic_face.surface_geometry.surface = synthetic_surface;
        synthetic_face.is_trimmed = 0;
        synthetic_face.tag = PRC_TYPE_TOPO_Face;
        synthetic_face.index_of_outer_loop = -1;
        synthetic_face.number_of_loops = ana_face->trim_loop_count;

        /* Now the trimming data */
        if (ana_face->is_trimmed)
        {
            code = prc_convert_ana_loops_to_std_loops(ctx, &synthetic_face,
                ana_face, compressed_data, NULL);
            if (code < 0)
            {
                prc_error(ctx, code, "Failed in prc_convert_ana_loops_to_std_loops\n");
                goto free_nurbs;
            }
        }

        code = prc_tessellate_surface(ctx, data, shell_index, face_index, &synthetic_face,
            face->orientation_surface_with_shell, NULL, NULL);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed in prc_tessellate_surface for PRC_HCG_AnaPlane\n");
            goto free_nurbs;
        }

    free_nurbs:
        if (nurbs.p != NULL)
        {
            prc_free(ctx, nurbs.p);
            nurbs.p = NULL;
        }
        if (nurbs.knot_vector_u != NULL)
        {
            prc_free(ctx, nurbs.knot_vector_u);
            nurbs.knot_vector_u = NULL;
        }
        if (nurbs.knot_vector_v != NULL)
        {
            prc_free(ctx, nurbs.knot_vector_v);
            nurbs.knot_vector_v = NULL;
        }

        break;
    }

    case PRC_HCG_AnaGenericFace:
    {
        prc_hcg_ana_generic_face hcg_ana_generic_face = topo_face->hcg_ana_generic_face;
        break;
    }
    default:
        /* topo_face->tag is what this switch dispatches on; `entity_type`
           named nothing in this scope and only compiled because the old
           prc_error macro discarded its arguments without evaluating them. */
        prc_error(ctx, PRC_ERROR_PARSE,
            "Unknown entity type in prc_tessellate_compressed_face: %u\n",
            (unsigned)topo_face->tag);
        return PRC_ERROR_PARSE;
    }

free_loops:
    if (free_loops)
    {
        if (synthetic_face.number_of_loops > 0 && synthetic_face.loops != NULL)
        {
            uint32_t j, k;

            for (j = 0; j < synthetic_face.number_of_loops; j++)
            {
                prc_ptr_topology *loop = &synthetic_face.loops[j];
                if (loop != NULL)
                {
                    if (loop->topo != NULL)
                    {
                        if (loop->topo->topo_loop != NULL)
                        {
                            uint32_t num_coedges = loop->topo->topo_loop->number_of_coedges;
                            for (k = 0; k < num_coedges; k++)
                            {
                                prc_ptr_topology *next_coedge = &loop->topo->topo_loop->coedge[k].next_coedge;
                                if (next_coedge->topo != NULL)
                                {
                                    if (next_coedge->topo->topo_coedge != NULL)
                                    {
                                        if (next_coedge->topo->topo_coedge->ptr_topology.topo != NULL)
                                        {
                                            if (next_coedge->topo->topo_coedge->ptr_topology.topo->topo_single_wire_compress != NULL)
                                            {
                                                /* Check for deduced curve case (iso curve 3 and 4 only) */
                                                if (next_coedge->topo->topo_coedge->ptr_topology.topo->topo_single_wire_compress->ref_or_compressed_curve.is_deduced_curve)
                                                {
                                                    prc_free(ctx, next_coedge->topo->topo_coedge->ptr_topology.topo->topo_single_wire_compress->ref_or_compressed_curve.compressed_curve);
                                                }
                                                prc_free(ctx, next_coedge->topo->topo_coedge->ptr_topology.topo->topo_single_wire_compress);
                                            }
                                            prc_free(ctx, next_coedge->topo->topo_coedge->ptr_topology.topo);
                                        }
                                        prc_free(ctx, next_coedge->topo->topo_coedge);
                                    }
                                    prc_free(ctx, next_coedge->topo);
                                }
                            }
                            prc_free(ctx, loop->topo->topo_loop->coedge);
                            prc_free(ctx, loop->topo->topo_loop);
                        }
                        prc_free(ctx, loop->topo);
                    }
                }
            }
            prc_free(ctx, synthetic_face.loops);
        }
    }
    return code;
}
