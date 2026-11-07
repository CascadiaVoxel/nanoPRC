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

/* Methods to create wire and tessellation structures from exact geometry */

#include "prc_tessellation_exact_geom.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

int
prc_get_surface_data(prc_context *ctx, prc_type_surf *surface,
                     prc_surface_sampling_info *sampling_info)
{
    prc_uv_parameterization params;
    prc_domain domain;
    uint8_t has_transform = 0;
    prc_trans_3d *prc_trans = NULL;
    prc_exact_geom_transform *exact_geom_trans = NULL;
    int code;

    /* Initialize parameters to quiet compiler */
    memset(&params, 0, sizeof(prc_uv_parameterization));

    switch (surface->surface_type)
    {
        case PRC_TYPE_SURF_Cone:
        {
            prc_surf_cone *cone = surface->surf_cone;
            params = cone->parameterization;
            has_transform = cone->has_transform;
            prc_trans = &cone->transform;
            exact_geom_trans = &cone->exact_geom_transform;

            sampling_info->num_samples_u = SURFACE_SAMPLES;
            sampling_info->num_samples_v = 2;

            sampling_info->u_periodic = params.swap_uv ? 0 : 1;
            sampling_info->v_periodic = params.swap_uv ? 1 : 0;
            sampling_info->u_linear = 0;
            sampling_info->v_linear = params.swap_uv ? 0 : 1;
            sampling_info->u_period = params.swap_uv ? 0.0 : 2.0 * PRC_PI;
            sampling_info->v_period = params.swap_uv ? 2.0 * PRC_PI : 0.0;
            sampling_info->precision_u = CONE_SURFACE_PRECISION;
            sampling_info->precision_v = SURFACE_PRECISION;
            sampling_info->max_samples_u = CONE_MAX_SAMPLES;
            sampling_info->max_samples_v = 2;
            if (params.swap_uv)
            {
                sampling_info->u_linear = 1;
                sampling_info->v_linear = 0;
                sampling_info->num_samples_u = 2;
                sampling_info->num_samples_v = SURFACE_SAMPLES;
                sampling_info->precision_u = SURFACE_PRECISION;
                sampling_info->precision_v = CONE_SURFACE_PRECISION;
                sampling_info->max_samples_u = 2;
                sampling_info->max_samples_v = CONE_MAX_SAMPLES;
            }
            break;
        }
        case PRC_TYPE_SURF_Cylinder:
        {
            prc_surf_cylinder *cylinder = surface->surf_cylinder;
            params = cylinder->parameterization;
            has_transform = cylinder->has_transform;
            prc_trans = &cylinder->transform;
            exact_geom_trans = &cylinder->exact_geom_transform;

            sampling_info->num_samples_u = SURFACE_SAMPLES;
            sampling_info->num_samples_v = 2;
            sampling_info->u_periodic = params.swap_uv ? 0 : 1;
            sampling_info->v_periodic = params.swap_uv ? 1 : 0;
            sampling_info->u_linear = 0;
            sampling_info->v_linear = params.swap_uv ? 0 : 1;
            sampling_info->u_period = params.swap_uv ? 0.0 : 2.0 * PRC_PI;
            sampling_info->v_period = params.swap_uv ? 2.0 * PRC_PI : 0.0;
            sampling_info->precision_u = CYLINDER_SURFACE_PRECISION;
            sampling_info->precision_v = SURFACE_PRECISION;
            sampling_info->max_samples_u = CYLINDER_MAX_SAMPLES;
            sampling_info->max_samples_v = 2;
            if (params.swap_uv)
            {
                sampling_info->u_linear = 1;
                sampling_info->v_linear = 0;
                sampling_info->num_samples_u = 2;
                sampling_info->num_samples_v = SURFACE_SAMPLES;
                sampling_info->precision_u = SURFACE_PRECISION;
                sampling_info->precision_v = CYLINDER_SURFACE_PRECISION;
                sampling_info->max_samples_u = 2;
                sampling_info->max_samples_v = CYLINDER_MAX_SAMPLES;
            }
            break;
        }
        case PRC_TYPE_SURF_Blend01:
        {
            prc_surf_blend01 *blend = surface->surf_blend01;
            curve_func center_eval_func = NULL;
            void *center_params = NULL;
            double center_min_u = 0.0, center_max_u = 0.0;

            has_transform = blend->has_transform;
            prc_trans = &blend->transform;
            exact_geom_trans = &blend->exact_geom_transform;

            /* u is the angle around the pipe; v is the center curve parameter */
            code = prc_get_curve_eval_func(ctx, &blend->center_curve, &center_eval_func,
                &center_params, &center_min_u, &center_max_u);
            if (code < 0)
            {
                prc_error(ctx, code, "Invalid center curve type in prc_get_surface_data (Blend01)\n");
                return code;
            }

            domain.min_uv.x = 0.0;
            domain.max_uv.x = 2.0 * PRC_PI;
            domain.min_uv.y = center_min_u;
            domain.max_uv.y = center_max_u;

            sampling_info->num_samples_u = SURFACE_SAMPLES;
            sampling_info->num_samples_v = SURFACE_SAMPLES;
            sampling_info->u_periodic = 1;
            sampling_info->v_periodic = 0;
            sampling_info->u_linear = 0;
            sampling_info->v_linear = 0;
            sampling_info->u_period = 2.0 * PRC_PI;
            sampling_info->v_period = 0;
            sampling_info->precision_u = SURFACE_PRECISION;
            sampling_info->precision_v = SURFACE_PRECISION;
            sampling_info->max_samples_u = SURFACE_MAX_SAMPLES;
            sampling_info->max_samples_v = SURFACE_MAX_SAMPLES;
            break;
        }
        case PRC_TYPE_SURF_Blend02:
        {
            prc_surf_blend02 *blend = surface->surf_blend02;
            curve_func center_eval_func = NULL;
            void *center_params = NULL;
            double center_min_u = 0.0, center_max_u = 0.0;

            has_transform = blend->has_transform;
            prc_trans = &blend->transform;
            exact_geom_trans = &blend->exact_geom_transform;

            /* u is the center curve parameter; v's range depends on parameterization_type */
            code = prc_get_curve_eval_func(ctx, &blend->center_curve, &center_eval_func,
                &center_params, &center_min_u, &center_max_u);
            if (code < 0)
            {
                prc_error(ctx, code, "Invalid center curve type in prc_get_surface_data (Blend02)\n");
                return code;
            }

            domain.min_uv.x = center_min_u;
            domain.max_uv.x = center_max_u;
            domain.min_uv.y = 0.0;
            domain.max_uv.y = (blend->parameterization_type == 0) ? 1.0 : 2.0 * PRC_PI;

            sampling_info->num_samples_u = SURFACE_SAMPLES;
            sampling_info->num_samples_v = SURFACE_SAMPLES;
            sampling_info->u_periodic = 0;
            sampling_info->v_periodic = 0;
            sampling_info->u_linear = 0;
            sampling_info->v_linear = 0;
            sampling_info->u_period = 0;
            sampling_info->v_period = 0;
            sampling_info->precision_u = SURFACE_PRECISION;
            sampling_info->precision_v = SURFACE_PRECISION;
            sampling_info->max_samples_u = SURFACE_MAX_SAMPLES;
            sampling_info->max_samples_v = SURFACE_MAX_SAMPLES;
            break;
        }
        case PRC_TYPE_SURF_Blend03:
        {   
            prc_surf_blend03 *blend = surface->surf_blend03;
            params = blend->parameterization;
            has_transform = blend->has_transform;
            prc_trans = &blend->transform;
            exact_geom_trans = &blend->exact_geom_transform;

            domain.min_uv.x = blend->parameters[0];
            domain.max_uv.x = blend->parameters[blend->number_of_elements - 1];
            domain.min_uv.y = blend->trim_v_min;
            domain.max_uv.y = blend->trim_v_max;

            if (domain.min_uv.y >= domain.max_uv.y)
            {
                domain.min_uv.y = 0;
                domain.max_uv.y = 1;
            }

            sampling_info->num_samples_u = SURFACE_SAMPLES;
            sampling_info->num_samples_v = SURFACE_SAMPLES;
            sampling_info->u_periodic = 0;
            sampling_info->v_periodic = 0;
            sampling_info->u_linear = 0;
            sampling_info->v_linear = 0;
            sampling_info->u_period = 0;
            sampling_info->v_period = 0;
            sampling_info->precision_u = SURFACE_PRECISION;
            sampling_info->precision_v = SURFACE_PRECISION;
            sampling_info->max_samples_u = SURFACE_MAX_SAMPLES;
            sampling_info->max_samples_v = SURFACE_MAX_SAMPLES;

            break;
        }
        case PRC_TYPE_SURF_NURBS:
        {
            prc_surf_nurbs *nurbs = surface->surf_nurbs;

            /* Valid parameter range excludes the clamped end knot multiplicities */
            domain.min_uv.x = nurbs->knot_vector_u[nurbs->du];
            domain.max_uv.x = nurbs->knot_vector_u[nurbs->highest_index_of_knots_u - nurbs->du];
            domain.min_uv.y = nurbs->knot_vector_v[nurbs->dv];
            domain.max_uv.y = nurbs->knot_vector_v[nurbs->highest_index_of_knots_v - nurbs->dv];

            sampling_info->num_samples_u = SURFACE_SAMPLES;
            sampling_info->num_samples_v = SURFACE_SAMPLES;
            sampling_info->u_periodic = 0;
            sampling_info->v_periodic = 0;
            sampling_info->u_linear = (nurbs->du <= 1) ? 1 : 0;
            sampling_info->v_linear = (nurbs->dv <= 1) ? 1 : 0;
            sampling_info->u_period = 0;
            sampling_info->v_period = 0;
            sampling_info->precision_u = NURBS_SURFACE_PRECISION;
            sampling_info->precision_v = NURBS_SURFACE_PRECISION;
            sampling_info->max_samples_u = NURBS_MAX_SAMPLES;
            sampling_info->max_samples_v = NURBS_MAX_SAMPLES;
            break;
        }

        case PRC_TYPE_SURF_Transform:
        {
            prc_surf_transform *transform = surface->surf_transform;
            prc_surface_sampling_info base_sampling_info = { 0 };
            params = transform->parameterization;

            has_transform = transform->has_transform;
            prc_trans = &transform->transform;
            exact_geom_trans = &transform->exact_geom_transform;

            /* The implicit parameterization matches the base surface's UV domain */
            if (transform->base_surface.is_referenced)
            {
                prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid base surface case in prc_get_surface_data (Transform)\n");
                return PRC_ERROR_INTERNAL;
            }

            code = prc_get_surface_data(ctx, &transform->base_surface.surface, &base_sampling_info);
            if (code < 0)
            {
                prc_error(ctx, code, "Error in prc_get_surface_data for Transform base surface\n");
                return code;
            }

            *sampling_info = base_sampling_info;
            sampling_info->start_u = params.surface_domain.min_uv.x;
            sampling_info->start_v = params.surface_domain.min_uv.y;
            sampling_info->end_u = params.surface_domain.max_uv.x;
            sampling_info->end_v = params.surface_domain.max_uv.y;
            break;
        }

        case PRC_TYPE_SURF_Cylindrical:
        {
            prc_surf_cylindrical *cylindrical = surface->surf_cylindrical;
            params = cylindrical->parameterization;
            has_transform = cylindrical->has_transform;
            prc_trans = &cylindrical->transform;
            exact_geom_trans = &cylindrical->exact_geom_transform;

            sampling_info->num_samples_u = CYLINDRICAL_MAX_SAMPLES;
            sampling_info->num_samples_v = CYLINDRICAL_MAX_SAMPLES;
            sampling_info->u_periodic = 0;
            sampling_info->v_periodic = 0;
            sampling_info->u_linear = 1;
            sampling_info->v_linear = 1;
            sampling_info->u_period = 0;
            sampling_info->v_period = 0;
            sampling_info->precision_u = CYLINDRICAL_SURFACE_PRECISION;
            sampling_info->precision_v = CYLINDRICAL_SURFACE_PRECISION;
            break;
        }
        case PRC_TYPE_SURF_Offset:
        {
            prc_surf_offset *offset = surface->surf_offset;
            prc_surface_sampling_info base_sampling_info = { 0 };
            params = offset->parameterization;

            has_transform = offset->has_transform;
            prc_trans = &offset->transform;
            exact_geom_trans = &offset->exact_geom_transform;

            /* The implicit parameterization matches the base surface's UV domain */
            if (offset->base_surface.is_referenced)
            {
                prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid base surface case in prc_get_surface_data (Offset)\n");
                return PRC_ERROR_INTERNAL;
            }

            code = prc_get_surface_data(ctx, &offset->base_surface.surface, &base_sampling_info);
            if (code < 0)
            {
                prc_error(ctx, code, "Error in prc_get_surface_data for Offset base surface\n");
                return code;
            }

            *sampling_info = base_sampling_info;
            sampling_info->start_u = params.surface_domain.min_uv.x;
            sampling_info->start_v = params.surface_domain.min_uv.y;
            sampling_info->end_u = params.surface_domain.max_uv.x;
            sampling_info->end_v = params.surface_domain.max_uv.y;
            break;
        }
        case PRC_TYPE_SURF_Pipe:
        {
            /* Currenntly not supported */
            prc_surf_pipe *pipe = surface->surf_pipe;
            params = pipe->parameterization;
            has_transform = pipe->has_transform;
            prc_trans = &pipe->transform;
            exact_geom_trans = &pipe->exact_geom_transform;
            break;
        }
        case PRC_TYPE_SURF_Plane:
        {
            /* This one does not have the has_transform bit*/
            prc_surf_plane *plane = surface->surf_plane;
            domain = plane->domain;
            has_transform = 1; /* Always has a transform */
            prc_trans = &plane->transform;
            exact_geom_trans = &plane->exact_geom_transform;
            sampling_info->num_samples_u = PLANE_MAX_SAMPLES;
            sampling_info->num_samples_v = PLANE_MAX_SAMPLES;
            sampling_info->u_periodic = 0;
            sampling_info->v_periodic = 0;
            sampling_info->u_linear = 1;
            sampling_info->v_linear = 1;
            sampling_info->u_period = 0;
            sampling_info->v_period = 0;
            sampling_info->precision_u = PLANE_SURFACE_PRECISION;
            sampling_info->precision_v = PLANE_SURFACE_PRECISION;
            break;
        }
        case PRC_TYPE_SURF_Ruled:
        {
            prc_surf_ruled *ruled = surface->surf_ruled;
            params = ruled->parameterization;
            has_transform = ruled->has_transform;
            prc_trans = &ruled->transform;
            exact_geom_trans = &ruled->exact_geom_transform;
            prc_curve_sampling_info curve_sampling_info;

            code = prc_get_curve_sample_info(ctx, &ruled->first_curve, &curve_sampling_info);
            if (code < 0)
            {
                prc_error(ctx, code, "Error in prc_get_curve_sample_info for Extrusion base curve\n");
                return code;
            }

            sampling_info->start_u = 0;
            sampling_info->start_v = curve_sampling_info.start;
            sampling_info->end_u = 1;
            sampling_info->end_v = curve_sampling_info.end;
            break;
        }

        case PRC_TYPE_SURF_Sphere:
        {
            prc_surf_sphere *sphere = surface->surf_sphere;
            params = sphere->parameterization;
            has_transform = sphere->has_transform;
            prc_trans = &sphere->transform;
            exact_geom_trans = &sphere->exact_geom_transform;
            sampling_info->num_samples_u = SPHERE_MAX_SAMPLES;
            sampling_info->num_samples_v = SPHERE_MAX_SAMPLES;
            sampling_info->u_periodic = 1;
            sampling_info->v_periodic = 1;
            sampling_info->u_linear = 0;
            sampling_info->v_linear = 0;
            sampling_info->u_period = 2.0 * PRC_PI;
            sampling_info->v_period = 2.0 * PRC_PI;
            sampling_info->precision_u = SPHERE_SURFACE_PRECISION;
            sampling_info->precision_v = SPHERE_SURFACE_PRECISION;
            break;
        }
        case PRC_TYPE_SURF_Revolution:
        {
            prc_surf_revolution *revolution = surface->surf_revolution;
            uint8_t curve_periodic = 0;
            double curve_period = 0.0;
            prc_curve_sampling_info curve_sampling_info;

            params = revolution->parameterization;
            has_transform = revolution->has_transform;
            prc_trans = &revolution->transform;
            exact_geom_trans = &revolution->exact_geom_transform;

            code = prc_get_curve_sample_info(ctx, &revolution->base_curve, &curve_sampling_info);
            if (code < 0)
            {
                prc_error(ctx, code, "Error in prc_get_curve_sample_info for Extrusion base curve\n");
                return code;
            }

            /* Goes with v unless swapped */
            prc_get_curve_periodicity(ctx, &revolution->base_curve,
                &curve_periodic, &curve_period);

            /* u is 0 to 2pi  v is base curve */
            domain.min_uv.x = 0;
            domain.max_uv.x = 2 * PRC_PI;
            domain.min_uv.y = curve_sampling_info.start;
            domain.max_uv.y = curve_sampling_info.end;

            /* TODO Finish the period here */
            sampling_info->num_samples_u = REVOLUTION_MAX_SAMPLES;
            sampling_info->num_samples_v = REVOLUTION_MAX_SAMPLES;

            /* This is a tricky one in terms of the period */
            sampling_info->u_periodic = params.swap_uv ? curve_periodic : 1;
            sampling_info->v_periodic = params.swap_uv ? 1 : curve_periodic;
            sampling_info->u_linear = params.swap_uv ? 0 : 1;
            sampling_info->v_linear = params.swap_uv ? 1 : 0;
            sampling_info->u_period = params.swap_uv ? 2.0 * PRC_PI : curve_period;
            sampling_info->v_period = params.swap_uv ? curve_period : 2.0 * PRC_PI;
            sampling_info->precision_u = REVOLUTION_SURFACE_PRECISION;
            sampling_info->precision_v = REVOLUTION_SURFACE_PRECISION;
            break;
        }
        case PRC_TYPE_SURF_Extrusion:
        {
            prc_surf_extrusion *extrusion = surface->surf_extrusion;
            params = extrusion->parameterization;
            has_transform = extrusion->has_transform;
            uint8_t curve_periodic = 0;
            double curve_period = 0.0;
            prc_curve_sampling_info curve_sampling_info;

            prc_trans = &extrusion->transform;
            exact_geom_trans = &extrusion->exact_geom_transform;

            prc_get_curve_periodicity(ctx, &extrusion->base_curve,
                &curve_periodic, &curve_period);

            code = prc_get_curve_sample_info(ctx, &extrusion->base_curve, &curve_sampling_info);
            if (code < 0)
            {
                prc_error(ctx, code, "Error in prc_get_curve_sample_info for Extrusion base curve\n");
                return code;
            }

            domain.min_uv.x = curve_sampling_info.start;
            domain.max_uv.x = curve_sampling_info.end;
            domain.min_uv.y = extrusion->parameterization.surface_domain.min_uv.y;
            domain.max_uv.y = extrusion->parameterization.surface_domain.max_uv.y;

            sampling_info->num_samples_u = EXTRUSION_MAX_SAMPLES;
            sampling_info->num_samples_v = EXTRUSION_MAX_SAMPLES;

            sampling_info->u_periodic = params.swap_uv ? 0 : curve_periodic;
            sampling_info->v_periodic = params.swap_uv ? curve_periodic : 0;
            sampling_info->u_linear = 0;
            sampling_info->v_linear = params.swap_uv ? 0 : 1;
            sampling_info->u_period = params.swap_uv ? 0.0 : curve_period;
            sampling_info->v_period = params.swap_uv ? curve_period : 0.0;

            sampling_info->precision_u = EXTRUSION_SURFACE_PRECISION;
            sampling_info->precision_v = EXTRUSION_SURFACE_PRECISION;
            break;
        }
        case PRC_TYPE_SURF_FromCurves:
        {
            prc_surf_fromcurves *from_curves = surface->surf_fromcurves;
            uint8_t curve1_periodic, curve2_periodic;
            double curve1_period, curve2_period;
            prc_curve_sampling_info curve1_sampling_info;
            prc_curve_sampling_info curve2_sampling_info;

            params = from_curves->parameterization;
            has_transform = from_curves->has_transform;
            prc_trans = &from_curves->transform;
            exact_geom_trans = &from_curves->exact_geom_transform;

            prc_get_curve_periodicity(ctx, &from_curves->first_curve,
                &curve1_periodic, &curve1_period);
            prc_get_curve_periodicity(ctx, &from_curves->second_curve,
                &curve2_periodic, &curve2_period);

            prc_get_curve_sample_info(ctx, &from_curves->first_curve, &curve1_sampling_info);
            prc_get_curve_sample_info(ctx, &from_curves->second_curve, &curve2_sampling_info);

            domain.min_uv.x = curve1_sampling_info.start;
            domain.max_uv.x = curve1_sampling_info.end;
            domain.min_uv.y = curve2_sampling_info.start;
            domain.max_uv.y = curve2_sampling_info.end;

            sampling_info->num_samples_u = SURFACE_SAMPLES;
            sampling_info->num_samples_v = SURFACE_SAMPLES;
            sampling_info->u_periodic = params.swap_uv ? curve2_periodic : curve1_periodic;
            sampling_info->v_periodic = params.swap_uv ? curve1_periodic : curve2_periodic;
            sampling_info->u_linear = 0;
            sampling_info->v_linear = 0;
            sampling_info->u_period = params.swap_uv ? curve2_period : curve1_period;
            sampling_info->v_period = params.swap_uv ? curve1_period : curve2_period;
            sampling_info->precision_u = SURFACE_PRECISION;
            sampling_info->precision_v = SURFACE_PRECISION;
            sampling_info->max_samples_u = SURFACE_MAX_SAMPLES;
            sampling_info->max_samples_v = SURFACE_MAX_SAMPLES;
            break;
        }
        case PRC_TYPE_SURF_Torus:
        {
            prc_surf_torus *torus = surface->surf_torus;
            params = torus->parameterization;
            has_transform = torus->has_transform;
            prc_trans = &torus->transform;
            exact_geom_trans = &torus->exact_geom_transform;

            sampling_info->num_samples_u = TORUS_MAX_SAMPLES;
            sampling_info->num_samples_v = TORUS_MAX_SAMPLES;
            sampling_info->u_periodic = 1;
            sampling_info->v_periodic = 1;
            sampling_info->u_linear = 0;
            sampling_info->v_linear = 0;
            sampling_info->u_period = 2.0 * PRC_PI;
            sampling_info->v_period = 2.0 * PRC_PI;
            sampling_info->precision_u = TORUS_SURFACE_PRECISION;
            sampling_info->precision_v = TORUS_SURFACE_PRECISION;
            sampling_info->max_samples_u = TORUS_MAX_SAMPLES;
            sampling_info->max_samples_v = TORUS_MAX_SAMPLES;
            break;
        }
        case PRC_TYPE_SURF_Blend04:
        {
            /* UNDEFINED */
            break;
        }
        default:
            return PRC_ERROR_INTERNAL;
    }

    if (has_transform && prc_trans != NULL && exact_geom_trans != NULL)
    {
        code = prc_exact_geom_set_transform(ctx, exact_geom_trans, prc_trans);
        if (code < 0)
        {
            return code;
        }
    }
    else if (exact_geom_trans != NULL)
    {
        exact_geom_trans->is_identity = 1;
    }

    if (surface->surface_type == PRC_TYPE_SURF_Plane || surface->surface_type == PRC_TYPE_SURF_NURBS ||
        surface->surface_type == PRC_TYPE_SURF_Blend01 || surface->surface_type == PRC_TYPE_SURF_Blend02 ||
        surface->surface_type == PRC_TYPE_SURF_FromCurves)
    {
        sampling_info->start_u = domain.min_uv.x;
        sampling_info->start_v = domain.min_uv.y;
        sampling_info->end_u = domain.max_uv.x;
        sampling_info->end_v = domain.max_uv.y;
    }
    else
    {
        sampling_info->start_u = params.surface_domain.min_uv.x;
        sampling_info->start_v = params.surface_domain.min_uv.y;
        sampling_info->end_u = params.surface_domain.max_uv.x;
        sampling_info->end_v = params.surface_domain.max_uv.y;

        if (params.swap_uv)
        {
            double temp = sampling_info->start_u;
            sampling_info->start_u = sampling_info->start_v;
            sampling_info->start_v = temp;
            temp = sampling_info->end_u;
            sampling_info->end_u = sampling_info->end_v;
            sampling_info->end_v = temp;
        }
    }
    return 0;
}

static uint32_t
prc_count_faces_in_shell(prc_context *ctx, prc_topo_shell *shell)
{
    if (shell->tag == PRC_TYPE_TOPO_Shell)
    {
        return shell->number_of_faces;
    }
    return 0;
}

static void
prc_count_shells_faces_in_topo(prc_context *ctx, prc_topo *topo,
    uint32_t *num_shells, uint32_t *num_faces)
{
    *num_shells = 0;
    *num_faces = 0;

    if (topo->tag == PRC_TYPE_TOPO_BrepData)
    {
        prc_topo_brep_data *brep_data = topo->topo_brep_data;
        if (brep_data->number_of_connex > 0)
        {
            /* Do we need to worry about multiple connex here? */
            prc_topo_connex *connex = brep_data->connex[0].topo->topo_connex;
            *num_shells = connex->number_of_shells;
            for (uint32_t i = 0; i < connex->number_of_shells; i++)
            {
                prc_topo_shell *shell = connex->shells[i].topo->topo_shell;
                *num_faces += prc_count_faces_in_shell(ctx, shell);
            }
        }
    }
    else if (topo->tag == PRC_TYPE_TOPO_BrepDataCompress)
    {
        /* The number of faces in the compressed brep is calculated as the
           number of faces in all of the shells in all of the connex entities.*/
        prc_topo_brep_data_compress *brep_data_comp = topo->topo_brep_data_compress;

        /* We will handle just the single_connex compressed at this time. */
        if (!brep_data_comp->single_connex_test)
        {
            prc_error(ctx, PRC_ERROR_INTERNAL, "Multi-connex in compressed brep not yet supported");
            return;
        }
        /* In this case we just have a single shell and how every many faces that shell has */
        *num_shells = 1;
        *num_faces = brep_data_comp->number_of_faces;
    }
}

static uint32_t
prc_count_wires_in_topo(prc_context *ctx, prc_topo *topo)
{   
    if (topo->tag == PRC_TYPE_TOPO_SingleWireBody)
    {
        prc_topo_single_wire_body *body = topo->topo_single_wire_body;
        /* This one could be referenced... */
        if (body->wire_body.is_stored == 0)
        {
            if (body->wire_body.topo->tag == PRC_TYPE_TOPO_WireEdge)
            {
                return 1;
            }
        }
    }
    else if (topo->tag == PRC_TYPE_TOPO_SingleWireBodyCompress)
    {
        /* I *think* this is never referenced */
        return 1;
    }
    return 0;
}

/* Main entry point for tessellation of exact geometry objects */
int
prc_approximate_objects_exact_geom(prc_context *ctx, prc_api_data data_in, uint32_t *num_tessellations)
{
    prc_data *data = (prc_data *)data_in;
    uint32_t geom_count = data->exact_geom_tess_part_count;
    uint32_t file_index = data->exact_geom_tess_part[geom_count].file_index;
    uint32_t topo_index = data->exact_geom_tess_part[geom_count].topo_context_index;
    uint32_t body_index = data->exact_geom_tess_part[geom_count].body_index;
    int code;
    uint32_t num_shells, num_faces, num_wires;
    uint32_t i, j;

    /* Lets figure out if we are doing a curve or a triangle tessellation */
    prc_topo *topo = &data->file_struct[file_index].geometry->exact_geometry.topo_contexts[topo_index].bodies[body_index];

    /* Find out how many shells and how many faces we are dealing with here */
    prc_count_shells_faces_in_topo(ctx, topo, &num_shells, &num_faces);
    //num_faces = 3; /* For debug testing */
    num_wires = prc_count_wires_in_topo(ctx, topo);

    /* I *think* we do not have wires AND faces */
    if (num_wires > 0 && num_faces > 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Error: Found both wires and faces in the same topo. This is not supported.\n");
        return PRC_ERROR_INTERNAL;
    }

    /* If we have faces and shells allocate accordingly. If we have a wire also 
       allocate */
    if (num_wires == 1)
    {
        num_faces = 1;
        num_shells = 1;
    }

    /* Allocate shells and faces */
    data->exact_geom_tess_part[geom_count].shells = (prc_exact_geom_shell *)prc_calloc(ctx, num_shells, sizeof(prc_exact_geom_shell));
    if (data->exact_geom_tess_part[geom_count].shells == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Allocation failure of shells in prc_approximate_objects_exact_geom\n");
        return PRC_ERROR_MEMORY;
    }
    data->exact_geom_tess_part[geom_count].number_of_shells = num_shells;
    for (i = 0; i < num_shells; i++)
    {
        uint32_t num_faces_in_shell = 0;

        if (topo->tag == PRC_TYPE_TOPO_BrepData)
        {
            num_faces_in_shell = (num_wires == 1) ? 1 : prc_count_faces_in_shell(ctx, topo->topo_brep_data->connex[0].topo->topo_connex->shells[i].topo->topo_shell);
        }
        else if (topo->tag == PRC_TYPE_TOPO_BrepDataCompress)
        {
            /* Note we only handle the single connex case for now.. */
            num_faces_in_shell = topo->topo_brep_data_compress->single_connex.number_of_faces;
        }
        else if (topo->tag == PRC_TYPE_TOPO_SingleWireBodyCompress || topo->tag == PRC_TYPE_TOPO_SingleWireBody)
        {
            num_faces_in_shell = 1;
        }
        data->exact_geom_tess_part[geom_count].shells[i].faces = (prc_exact_geom_face *)prc_calloc(ctx, num_faces, sizeof(prc_exact_geom_face));
        if (data->exact_geom_tess_part[geom_count].shells[i].faces == NULL)
        {
            prc_error(ctx, PRC_ERROR_MEMORY, "Allocation failure of faces in prc_approximate_objects_exact_geom\n");
            return PRC_ERROR_MEMORY;
        }
        //num_faces_in_shell = num_faces; /* For debug testing */
        data->exact_geom_tess_part[geom_count].shells[i].number_of_faces = num_faces_in_shell;
    }

    /* Now loop on the shells and the faces */
    for (i = 0; i < num_shells; i++)
    {
        for (j = 0; j < data->exact_geom_tess_part[geom_count].shells[i].number_of_faces; j++)
        {
            switch (topo->tag)
            {
            case PRC_TYPE_TOPO_SingleWireBodyCompress:
            {
                /* Single wire should not be storing vertex data I don't believe hence
                   the NULL in the call to prc_sample_compressed_curve_store_in_body */
                data->exact_geom_tess_part[geom_count].shells[i].faces[j].type = PRC_EXACT_GEOM_WIRE;
                prc_topo_single_wire_compress *body = topo->topo_single_wire_compress;
                code = prc_sample_compressed_curve_store_in_body(ctx, data, i, j,
                                    &body->compressed_curve, body->curve_tolerance, NULL);
                if (code < 0)
                {
                    prc_error(ctx, code, "Failed in prc_sample_compressed_curve\n");
                    return code;
                }
                (*num_tessellations)++;
                break;
            }
            case PRC_TYPE_TOPO_SingleWireBody:
            {
                data->exact_geom_tess_part[geom_count].shells[i].faces[j].type = PRC_EXACT_GEOM_WIRE;

                prc_topo_single_wire_body *body = topo->topo_single_wire_body;
                if (body->wire_body.is_stored == 1)
                {
                    /* We have to find this one. For now we skip this case */
                    data->exact_geom_tess_part[geom_count].shells[i].faces[j].type = PRC_EXACT_GEOM_UNKNOWN;
                    return 0;
                }

                /* Get the type of topo that this is. For now we just do wire edge.. */
                switch (body->wire_body.topo->tag)
                {
                case PRC_TYPE_TOPO_WireEdge:
                {
                    prc_topo_wire_edge *wire_edge = body->wire_body.topo->topo_wire_edge;
                    prc_exact_geom_wire_data *wire_data =
                        (prc_exact_geom_wire_data *)prc_calloc(ctx, 1, sizeof(prc_exact_geom_wire_data));
                    if (wire_data == NULL)
                    {
                        prc_error(ctx, PRC_ERROR_MEMORY, "Allocation failure of wire_data\n");
                        return PRC_ERROR_MEMORY;
                    }
                    data->exact_geom_tess_part[geom_count].shells[i].faces[j].wire_data = wire_data;
                    code = prc_sample_curve(ctx, &wire_edge->curve, wire_data);
                    if (code < 0)
                    {
                        /* Clean up wire data */
                        if (wire_data->points != NULL)
                        {
                            prc_free(ctx, wire_data->points);
                            prc_free(ctx, wire_data);
                            data->exact_geom_tess_part[geom_count].shells[i].faces[j].wire_data = NULL;
                        }
                        prc_error(ctx, code, "Failed in prc_sample_curve\n");
                        return code;
                    }

                    (*num_tessellations)++;
                    break;
                }
                default:
                    data->exact_geom_tess_part[geom_count].shells[i].faces[j].type = PRC_EXACT_GEOM_UNKNOWN;
                    return 0;
                }
                break;
            }
            case PRC_TYPE_TOPO_BrepData:
            {
                /* Lets get all the way to the surface geometry that we need to tessellate */
                /* But we need to handle multiple faces */
                data->exact_geom_tess_part[geom_count].shells[i].faces[j].type = PRC_EXACT_GEOM_3D;
                prc_topo_brep_data *brep_data = topo->topo_brep_data;
                uint8_t orientation;

                /* Skip a number of cases as we learn to walk before running */
                if (brep_data->number_of_connex > 1 ||
                    brep_data->number_of_connex == 0)
                {
                    data->exact_geom_tess_part[geom_count].shells[i].faces[j].type = PRC_EXACT_GEOM_UNKNOWN;
                    return 0;
                }
                if (brep_data->connex[0].is_stored == 1)
                {
                    data->exact_geom_tess_part[geom_count].shells[i].faces[j].type = PRC_EXACT_GEOM_UNKNOWN;
                    return 0;
                }
                if (brep_data->connex[0].topo->topo_connex->shells[i].is_stored == 1)
                {
                    data->exact_geom_tess_part[geom_count].shells[i].faces[j].type = PRC_EXACT_GEOM_UNKNOWN;
                    return 0;
                }
                if (brep_data->connex[0].topo->topo_connex->shells[i].topo->topo_shell->faces[j].face.is_stored == 1)
                {
                    data->exact_geom_tess_part[geom_count].shells[i].faces[j].type = PRC_EXACT_GEOM_UNKNOWN;
                    return 0;
                }
                orientation = brep_data->connex[0].topo->topo_connex->shells[i].topo->topo_shell->faces[j].orientation;
                data->exact_geom_tess_part[geom_count].shells[i].faces[j].orientation = orientation;
                prc_topo_face *topo_face = brep_data->connex[0].topo->topo_connex->shells[i].topo->topo_shell->faces[j].face.topo->topo_face;
                prc_topo_context *topo_context = brep_data->connex[0].topo->topo_context; /* Not sure which topo context I should use for loop information */
                code = prc_tessellate_surface(ctx, data, i, j, topo_face, orientation, topo->brep_ref_data, topo_context);
                if (code < 0)
                {
                    prc_error(ctx, code, "Failed in prc_sample_curve\n");
                    return code;
                }
                (*num_tessellations)++;
                break;
            }
            case PRC_TYPE_TOPO_WireEdge:
            case PRC_TYPE_TOPO_Edge:
            case PRC_TYPE_TOPO_CoEdge:
            case PRC_TYPE_TOPO_Loop:
            case PRC_TYPE_TOPO_WireBody:
                data->exact_geom_tess_part[geom_count].shells[i].faces[j].type = PRC_EXACT_GEOM_UNKNOWN;
                break;

            case PRC_TYPE_TOPO_BrepDataCompress:
            {
                data->exact_geom_tess_part[geom_count].shells[i].faces[j].type = PRC_EXACT_GEOM_3D;
                prc_topo_brep_data_compress *brep_data_comp = topo->topo_brep_data_compress;
                uint8_t orientation = 0;
                prc_compressed_face *compressed_face;

                /* Skip a number of cases as we learn to walk before running */
                if (!brep_data_comp->single_connex_test)
                {
                    /* We don't handle multi-connex here yet */
                    data->exact_geom_tess_part[geom_count].shells[i].faces[j].type = PRC_EXACT_GEOM_UNKNOWN;
                    return 0;
                }

                data->exact_geom_tess_part[geom_count].shells[i].faces[j].orientation = orientation;
                compressed_face = &brep_data_comp->single_connex.faces[j];
                code = prc_tessellate_compressed_face(ctx, data, i, j,
                                    compressed_face, brep_data_comp->ref_data);
                if (code < 0)
                {
                    prc_error(ctx, code, "Failed in prc_sample_curve\n");
                    return code;
                }
                (*num_tessellations)++;
                break;
            }
            case PRC_TYPE_TOPO_Body:
            case PRC_TYPE_TOPO_Face:
                data->exact_geom_tess_part[geom_count].shells[i].faces[j].type = PRC_EXACT_GEOM_UNKNOWN;
                break;

            default:
                data->exact_geom_tess_part[geom_count].shells[i].faces[j].type = PRC_EXACT_GEOM_UNKNOWN;
            }
        }
    }

    return 0;
}