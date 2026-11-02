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

/* Segment of exact tessellation work that evaluates surfaces */

#include "prc_tessellation_exact_geom.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

prc_vec3
prc_evaluate_surf_torus(prc_context *ctx, void *params, double u, double v)
{
    prc_vec3 output;
    prc_surface_params *surf_params = (prc_surface_params *)params;
    prc_surf_torus *torus = (prc_surf_torus *)surf_params->surface_params;
    double major_radius = torus->major_radius;
    double minor_radius = torus->minor_radius;
    double radius;
    uint32_t num_loops = surf_params->num_loops;
    prc_loop_samples *loops = surf_params->loop_samples;

    radius = major_radius + minor_radius * cos(v);

    output.x = radius * cos(u);
    output.y = radius * sin(u);
    output.z = minor_radius * sin(v);

    if (torus->has_transform && !torus->exact_geom_transform.is_identity)
    {
        output = prc_exact_geom_apply_transform(ctx, &torus->exact_geom_transform, output);
    }

    return output;
}

prc_vec3
prc_evaluate_surf_fromcurves(prc_context *ctx, void *params, double u, double v)
{
    prc_vec3 output;
    prc_surface_params *surf_params = (prc_surface_params *)params;
    prc_surf_fromcurves *surf = (prc_surf_fromcurves *)surf_params->surface_params;
    curve_func curve1 = NULL;
    void *curve1_params = NULL;
    double curve1_max_u = 0.0;
    double curve1_min_u = 0.0;
    curve_func curve2 = NULL;
    void *curve2_params = NULL;
    double curve2_max_u = 0.0;
    double curve2_min_u = 0.0;
    int code;
    prc_vec3 origin = surf->origin;
    prc_vec3 curve1_point, curve2_point, temp;
    uint32_t num_loops = surf_params->num_loops;
    prc_loop_samples *loops = surf_params->loop_samples;

    /* We need to take into account the curve parameterization */
    /* Lets get the base evaluation surface function.  We probably should
       do a 1-D curve sample here to get a good approximation of the curve. ToDo. */
    code = prc_get_curve_eval_func(ctx, &surf->first_curve,
        &curve1, &curve1_params, &curve1_min_u, &curve1_max_u);
    if (code < 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid base base curve type in prc_get_curve_eval_func\n");
        output.x = 0;
        output.y = 0;
        output.z = 0;
        return output;
    }

    code = prc_get_curve_eval_func(ctx, &surf->second_curve,
        &curve2, &curve2_params, &curve2_min_u, &curve2_max_u);
    if (code < 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid base base curve type in prc_get_curve_eval_func\n");
        output.x = 0;
        output.y = 0;
        output.z = 0;
        return output;
    }

    /* We need to apply any parametrization to the base_curve_params */
    if (u < curve1_min_u)
        u = curve1_min_u;
    if (u > curve1_max_u)
        u = curve1_max_u;
    curve1_point = curve1(ctx, curve1_params, u);

    if (v < curve2_min_u)
        v = curve2_min_u;
    if (v > curve2_max_u)
        v = curve2_max_u;
    curve2_point = curve2(ctx, curve2_params, v);

    prc_vec_add(curve1_point, curve2_point, &temp);
    prc_vec_sub(temp, origin, &output);

    if (surf->has_transform && !surf->exact_geom_transform.is_identity)
    {
        output = prc_exact_geom_apply_transform(ctx, &surf->exact_geom_transform, output);
    }
    return output;
}

prc_vec3
prc_evaluate_surf_cone(prc_context *ctx, void *params, double u, double v)
{
    prc_vec3 output;
    prc_surface_params *surf_params = (prc_surface_params *)params;
    prc_surf_cone *cone = (prc_surf_cone *)surf_params->surface_params;
    double bottom_radius = cone->radius;
    double semi_angle = cone->semi_angle;
    double radius;
    uint32_t num_loops = surf_params->num_loops;
    prc_loop_samples *loops = surf_params->loop_samples;

    radius = bottom_radius + v * tan(semi_angle);
    output.x = radius * cos(u);
    output.y = radius * sin(u);
    output.z = v;

    if (cone->has_transform && !cone->exact_geom_transform.is_identity)
    {
        output = prc_exact_geom_apply_transform(ctx, &cone->exact_geom_transform, output);
    }

    return output;
}

prc_vec3
prc_evaluate_surf_sphere(prc_context *ctx, void *params, double u, double v)
{
    prc_vec3 output;
    prc_surface_params *surf_params = (prc_surface_params *)params;
    prc_surf_sphere *sphere = (prc_surf_sphere *)surf_params->surface_params;
    double radius = sphere->radius;
    uint32_t num_loops = surf_params->num_loops;
    prc_loop_samples *loops = surf_params->loop_samples;

    output.x = radius * cos(v) * cos(u);
    output.y = radius * cos(v) * sin(u);
    output.z = radius * sin(v);

    if (sphere->has_transform && !sphere->exact_geom_transform.is_identity)
    {
        output = prc_exact_geom_apply_transform(ctx, &sphere->exact_geom_transform, output);
    }

    return output;
}

prc_vec3
prc_evaluate_surf_cylinder(prc_context *ctx, void *params, double u, double v)
{
    prc_vec3 output;
    prc_surface_params *surf_params = (prc_surface_params *)params;
    prc_surf_cylinder *cylinder = (prc_surf_cylinder *)surf_params->surface_params;
    double radius = cylinder->radius;
    uint32_t num_loops = surf_params->num_loops;
    prc_loop_samples *loops = surf_params->loop_samples;

    output.x = radius * cos(u);
    output.y = radius * sin(u);
    output.z = v;

    if (cylinder->has_transform && !cylinder->exact_geom_transform.is_identity)
    {
        output = prc_exact_geom_apply_transform(ctx, &cylinder->exact_geom_transform, output);
    }

    return output;
}

prc_vec3
prc_evaluate_surf_plane(prc_context *ctx, void *params, double u, double v)
{
    prc_vec3 output;
    prc_surface_params *surf_params = (prc_surface_params *)params;
    prc_surf_plane *plane = (prc_surf_plane *)surf_params->surface_params;
    double u_parameter_coeff_a = plane->u_parameter_coeff_a;
    double v_parameter_coeff_a = plane->v_parameter_coeff_a;
    double u_parameter_coeff_b = plane->u_parameter_coeff_b;
    double v_parameter_coeff_b = plane->v_parameter_coeff_b;
    uint32_t num_loops = surf_params->num_loops;
    prc_loop_samples *loops = surf_params->loop_samples;

    output.x = u * u_parameter_coeff_a + u_parameter_coeff_b;
    output.y = v * v_parameter_coeff_a + v_parameter_coeff_b;
    output.z = 0;

    if (!plane->exact_geom_transform.is_identity)
    {
        output = prc_exact_geom_apply_transform(ctx, &plane->exact_geom_transform, output);
    }
    return output;
}

prc_vec3
prc_evaluate_surf_nurbs(prc_context *ctx, void *params, double u, double v)
{
    prc_surface_params *surf_params = (prc_surface_params *)params;
    prc_surf_nurbs *nurbs = (prc_surf_nurbs *)surf_params->surface_params;
    prc_vec3 output = { 0.0, 0.0, 0.0 };
    double Nu[PRC_BSPLINE_MAX_DEGREE + 1];
    double Nv[PRC_BSPLINE_MAX_DEGREE + 1];
    uint32_t num_ctrl_u = nurbs->highest_index_of_control_points_u + 1;
    uint32_t num_ctrl_v = nurbs->highest_index_of_control_points_v + 1;
    uint32_t span_u, span_v, i, j;
    double x = 0.0, y = 0.0, z = 0.0, weight_sum = 0.0;
    uint32_t num_loops = surf_params->num_loops;
    prc_loop_samples *loops = surf_params->loop_samples;

    if (nurbs->du > PRC_BSPLINE_MAX_DEGREE || nurbs->dv > PRC_BSPLINE_MAX_DEGREE)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "NURBS surface degree exceeds supported maximum\n");
        return output;
    }

    span_u = prc_bspline_find_span(nurbs->du, num_ctrl_u, u, nurbs->knot_vector_u);
    span_v = prc_bspline_find_span(nurbs->dv, num_ctrl_v, v, nurbs->knot_vector_v);
    prc_bspline_basis_funs(span_u, u, nurbs->du, nurbs->knot_vector_u, Nu);
    prc_bspline_basis_funs(span_v, v, nurbs->dv, nurbs->knot_vector_v, Nv);

    /* Control points are stored u-major: index = ctrl_u * num_ctrl_v + ctrl_v */
    for (i = 0; i <= nurbs->du; i++)
    {
        uint32_t ctrl_u = span_u - nurbs->du + i;

        for (j = 0; j <= nurbs->dv; j++)
        {
            uint32_t ctrl_v = span_v - nurbs->dv + j;
            prc_control_points_nurbs_surf *cp = &nurbs->p[ctrl_u * num_ctrl_v + ctrl_v];
            double weight = nurbs->is_rational ? cp->w : 1.0;
            double basis = Nu[i] * Nv[j];  /* Weight is already applied to these */

            x += basis * cp->x;
            y += basis * cp->y;
            z += basis * cp->z;
            weight_sum += basis * weight; /* Denominator does need the weight */
        }
    }

    if (weight_sum != 0.0)
    {
        output.x = x / weight_sum;
        output.y = y / weight_sum;
        output.z = z / weight_sum;
    }

    return output;
}

/* This one uses a base curve */
prc_vec3
prc_evaluate_surf_extrusion(prc_context *ctx, void *params, double u, double v)
{
    prc_vec3 output, base_point, temp;
    prc_surface_params *surf_params = (prc_surface_params *)params;
    prc_surf_extrusion *extrusion = (prc_surf_extrusion *)surf_params->surface_params;
    curve_func base_curve_func = NULL;
    void *base_curve_params = NULL;
    prc_vec3 sweep_vector = extrusion->sweep_vector;
    double curve_max_u = 0.0;
    double curve_min_u = 0.0;
    int code;
    uint32_t num_loops = surf_params->num_loops;
    prc_loop_samples *loops = surf_params->loop_samples;

    /* We need to take into account the curve parameterization */
    /* Lets get the base evaluation surface function.  We probably should
       do a 1-D curve sample here to get a good approximation of the curve. ToDo. */
    code = prc_get_curve_eval_func(ctx, &extrusion->base_curve,
        &base_curve_func, &base_curve_params, &curve_min_u, &curve_max_u);
    if (code < 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid base base curve type in prc_get_curve_eval_func\n");
        output.x = 0;
        output.y = 0;
        output.z = 0;
        return output;
    }

    /* We need to apply any parametrization to the base_curve_params */
    if (u < curve_min_u)
        u = curve_min_u;
    if (u > curve_max_u)
        u = curve_max_u;
    base_point = base_curve_func(ctx, base_curve_params, u);
    prc_vec_scale(v, &sweep_vector);
    prc_vec_add(base_point, sweep_vector, &output);

    if (extrusion->has_transform && !extrusion->exact_geom_transform.is_identity)
    {
        output = prc_exact_geom_apply_transform(ctx, &extrusion->exact_geom_transform, output);
    }

    return output;
}

/* This one also uses a base curve */
prc_vec3
prc_evaluate_surf_revolution(prc_context *ctx, void *params, double u, double v)
{
    prc_vec3 output, base_point;
    prc_surface_params *surf_params = (prc_surface_params *)params;
    prc_surf_revolution *revolution = (prc_surf_revolution *)surf_params->surface_params;
    curve_func base_curve_func = NULL;
    void *base_curve_params = NULL;
    double curve_max_u = 0.0;
    double curve_min_u = 0.0;
    int code;
    prc_vec3 origin = revolution->origin;
    prc_vec3 x_axis = revolution->x_axis;
    prc_vec3 y_axis = revolution->y_axis;
    prc_vec3 axis_of_revolution;
    prc_vec3 point_on_axis;
    prc_vec3 temp_axis_x, temp_axis_y;
    prc_vec3 temp1;
    uint32_t num_loops = surf_params->num_loops;
    prc_loop_samples *loops = surf_params->loop_samples;

    /* We need to take into account the curve parameterization */
    /* Lets get the base evaluation surface function.  We probably should
       do a 1-D curve sample here to get a good approximation of the curve. ToDo. */
    code = prc_get_curve_eval_func(ctx, &revolution->base_curve,
        &base_curve_func, &base_curve_params, &curve_min_u, &curve_max_u);
    if (code < 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid base base curve type in prc_get_curve_eval_func\n");
        output.x = 0;
        output.y = 0;
        output.z = 0;
        return output;
    }

    /* We need to apply any parametrization to the base_curve_params */
    if (v < curve_min_u)
        v = curve_min_u;
    if (v > curve_max_u)
        v = curve_max_u;
    base_point = base_curve_func(ctx, base_curve_params, v);

    /* axis_of_revolution is found from the origin and cross product of x_axis and y_axis */
    prc_vec_cross(x_axis, y_axis, &axis_of_revolution);

    point_on_axis = prc_project_point_onto_line(ctx, base_point, origin, axis_of_revolution);
    prc_vec_sub(base_point, point_on_axis, &temp_axis_x);
    prc_vec_cross(axis_of_revolution, temp_axis_x, &temp_axis_y);
    prc_vec_scale(cos(u), &temp_axis_x);
    prc_vec_scale(sin(u), &temp_axis_y);
    prc_vec_add(temp_axis_x, temp_axis_y, &temp1);
    prc_vec_add(point_on_axis, temp1, &output);

    if (revolution->has_transform && !revolution->exact_geom_transform.is_identity)
    {
        output = prc_exact_geom_apply_transform(ctx, &revolution->exact_geom_transform, output);
    }

    return output;
}

/* Keep this one at the bottom as it may call the other evaluate methods above */
/* Maps a base/bound prc_type_surf to its evaluation function and params. Only
   the surface kinds that already have an evaluate function implemented are supported. */
int
prc_get_surface_eval_func(prc_context *ctx, prc_type_surf *surface,
    surface_func *eval_func, void **params)
{
    switch (surface->surface_type)
    {
    case PRC_TYPE_SURF_Cylinder:
        *eval_func = prc_evaluate_surf_cylinder;
        *params = (void *)surface->surf_cylinder;
        break;
    case PRC_TYPE_SURF_Cone:
        *eval_func = prc_evaluate_surf_cone;
        *params = (void *)surface->surf_cone;
        break;
    case PRC_TYPE_SURF_Sphere:
        *eval_func = prc_evaluate_surf_sphere;
        *params = (void *)surface->surf_sphere;
        break;
    case PRC_TYPE_SURF_Torus:
        *eval_func = prc_evaluate_surf_torus;
        *params = (void *)surface->surf_torus;
        break;
    case PRC_TYPE_SURF_Extrusion:
        *eval_func = prc_evaluate_surf_extrusion;
        *params = (void *)surface->surf_extrusion;
        break;
    case PRC_TYPE_SURF_Revolution:
        *eval_func = prc_evaluate_surf_revolution;
        *params = (void *)surface->surf_revolution;
        break;
    case PRC_TYPE_SURF_FromCurves:
        *eval_func = prc_evaluate_surf_fromcurves;
        *params = (void *)surface->surf_fromcurves;
        break;
    case PRC_TYPE_SURF_Plane:
        *eval_func = prc_evaluate_surf_plane;
        *params = (void *)surface->surf_plane;
        break;
    case PRC_TYPE_SURF_NURBS:
        *eval_func = prc_evaluate_surf_nurbs;
        *params = (void *)surface->surf_nurbs;
        break;
    default:
        return PRC_ERROR_INTERNAL;
    }
    return 0;
}


/* Projects center onto whichever of bound_surface/bound_curve is present (exactly one
   shall be non-NULL per the spec). Referenced (shared) entities are not yet supported. */
static int
prc_project_onto_blend_bound(prc_context *ctx, prc_ptr_surface *bound_surface,
    prc_ptr_curve *bound_curve, prc_vec3 center, prc_vec3 *out)
{
    int code;

    if (!bound_surface->is_referenced && bound_surface->surface.surface_type != PRC_TYPE_ROOT)
    {
        surface_func eval_func = NULL;
        void *eval_params = NULL;
        prc_surface_sampling_info sampling_info = { 0 };
        prc_surface_params surf_params = { 0 };

        code = prc_get_surface_eval_func(ctx, &bound_surface->surface, &eval_func, &eval_params);
        if (code < 0)
            return code;

        code = prc_get_surface_data(ctx, &bound_surface->surface, &sampling_info);
        if (code < 0)
            return code;

        surf_params.surface_params = eval_params;
        *out = prc_project_point_onto_surface(ctx, eval_func, &surf_params,
            sampling_info.start_u, sampling_info.end_u,
            sampling_info.start_v, sampling_info.end_v, center, NULL, NULL);
        return 0;
    }

    if (!bound_curve->is_referenced && bound_curve->curve_type != PRC_TYPE_ROOT)
    {
        curve_func eval_func = NULL;
        void *eval_params = NULL;
        double min_u, max_u;

        code = prc_get_curve_eval_func(ctx, bound_curve, &eval_func, &eval_params, &min_u, &max_u);
        if (code < 0)
            return code;

        *out = prc_project_point_onto_curve(ctx, eval_func, eval_params, min_u, max_u, center);
        return 0;
    }

    return PRC_ERROR_INTERNAL;
}

/* --- Local Helper: Find Knot Interval ---
 * Locates index i such that parameters[i] <= u < parameters[i+1]
 */
static int
prc_blend03_find_knot_interval(double u, prc_surf_blend03 *blend)
{
    uint32_t n = blend->number_of_elements;
    if (u <= blend->parameters[0]) return 0;
    if (u >= blend->parameters[n - 1]) return (int)n - 2;

    int low = 0, high = (int)n - 1;
    while (high - low > 1)
    {
        int mid = (low + high) / 2;
        if (blend->parameters[mid] <= u) low = mid;
        else high = mid;
    }
    return low;
}

/* Quintic Hermite Segment Math
 * Evaluates a single 1D quintic Hermite spline segment using 6 boundary conditions.
 */
static double
prc_blend03_evaluate_quintic_segment(double t, double dt, double p0, double p1,
    double t0, double t1, double a0, double a1)
{
    double s = t / dt;
    double s2 = s * s;
    double s3 = s2 * s;
    double s4 = s3 * s;
    double s5 = s4 * s;

    /* Rescale derivatives to match normalized local domain space [0, 1] */
    double v0 = t0 * dt;
    double v1 = t1 * dt;
    double acc0 = a0 * dt * dt;
    double acc1 = a1 * dt * dt;

    /* Quintic Hermite Basis Functions */
    double h0 = -6.0 * s5 + 15.0 * s4 - 10.0 * s3 + 1.0;
    double h1 = -3.0 * s5 + 8.0 * s4 - 6.0 * s3 + s;
    double h2 = -0.5 * s5 + 1.5 * s4 - 1.5 * s3 + 0.5 * s2;
    double h3 = 0.5 * s5 - 1.0 * s4 + 0.5 * s3;
    double h4 = -3.0 * s5 + 7.0 * s4 - 4.0 * s3;
    double h5 = 6.0 * s5 - 15.0 * s4 + 10.0 * s3;

    return (h0 * p0) + (h1 * v0) + (h2 * acc0) + (h3 * acc1) + (h4 * v1) + (h5 * p1);
}

/* Vector Curve Evaluation
 * Evaluates the 3D curves (center, rail1, or rail2) based on interleaved stride indices.
 */
static prc_vec3
prc_blend03_eval_spline_vec3(double u, prc_surf_blend03 *blend, int curve_offset)
{
    int i = prc_blend03_find_knot_interval(u, blend);
    double u0 = blend->parameters[i];
    double u1 = blend->parameters[i + 1];
    double dt = u1 - u0;

    if (dt < 1e-12)
        dt = 1e-12;

    /* Compute sequential structural offsets (i * 3 + curve_offset) */
    int idx0 = i * 3 + curve_offset;
    int idx1 = (i + 1) * 3 + curve_offset;

    prc_vec3 p0 = blend->points[idx0];
    prc_vec3 p1 = blend->points[idx1];
    prc_vec3 t0 = blend->tangents[idx0];
    prc_vec3 t1 = blend->tangents[idx1];
    prc_vec3 a0 = blend->second_derivatives[idx0];
    prc_vec3 a1 = blend->second_derivatives[idx1];

    prc_vec3 res;
    double local_u = u - u0;
    res.x = prc_blend03_evaluate_quintic_segment(local_u, dt, p0.x, p1.x, t0.x, t1.x, a0.x, a1.x);
    res.y = prc_blend03_evaluate_quintic_segment(local_u, dt, p0.y, p1.y, t0.y, t1.y, a0.y, a1.y);
    res.z = prc_blend03_evaluate_quintic_segment(local_u, dt, p0.z, p1.z, t0.z, t1.z, a0.z, a1.z);
    return res;
}

/* Scalar Curve Evaluation
 * Evaluates the 1D Rail 2 Angle Curve.
 */
static double
prc_blend03_eval_spline_double(double u, prc_surf_blend03 *blend)
{
    int i = prc_blend03_find_knot_interval(u, blend);
    double u0 = blend->parameters[i];
    double u1 = blend->parameters[i + 1];
    double dt = u1 - u0;
    if (dt < 1e-12)
        dt = 1e-12;

    double local_u = u - u0;
    return prc_blend03_evaluate_quintic_segment(local_u, dt,
        blend->rail_2_angles_v[i],
        blend->rail_2_angles_v[i + 1],
        blend->rail_2_derivatives_v[i],
        blend->rail_2_derivatives_v[i + 1],
        blend->rail_2_second_derivatives[i],
        blend->rail_2_second_derivatives[i + 1]);
}

prc_vec3
prc_evaluate_surf_ruled(prc_context *ctx, void *params, double u, double v)
{
    prc_surface_params *surf_params = (prc_surface_params *)params;
    prc_surf_ruled *ruled = (prc_surf_ruled *)surf_params->surface_params;
    prc_vec3 output = { 0.0, 0.0, 0.0 };
    int code;
    curve_func curve1 = NULL, curve2 = NULL;
    void *curve1_params = NULL, *curve2_params = NULL;
    double curve1_min_u, curve1_max_u; /* Curves are defined over the same interval */
    double curve2_min_u, curve2_max_u; /* Curves are defined over the same interval */
    prc_vec3 base_point1, base_point2;

    code = prc_get_curve_eval_func(ctx, &ruled->first_curve,
        &curve1, &curve1_params, &curve1_min_u, &curve1_max_u);
    if (code < 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid curve1 type in prc_get_curve_eval_func\n");
        output.x = 0;
        output.y = 0;
        output.z = 0;
        return output;
    }

    code = prc_get_curve_eval_func(ctx, &ruled->second_curve,
        &curve2, &curve2_params, &curve2_min_u, &curve2_max_u);
    if (code < 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid curve2 type in prc_get_curve_eval_func\n");
        output.x = 0;
        output.y = 0;
        output.z = 0;
        return output;
    }

    base_point1 = curve1(ctx, curve1_params, v);
    base_point2 = curve1(ctx, curve2_params, v);

    output.x = (1.0 - u) * base_point1.x + u * base_point2.x;
    output.y = (1.0 - u) * base_point1.y + u * base_point2.y;
    output.z = (1.0 - u) * base_point1.z + u * base_point2.z;

    return output;
}

prc_vec3
prc_evaluate_surf_transform(prc_context *ctx, void *params, double u, double v)
{
    prc_surface_params *surf_params = (prc_surface_params *)params;
    prc_surf_transform *transform = (prc_surf_transform *)surf_params->surface_params;
    prc_vec3 output = { 0.0, 0.0, 0.0 };
    prc_vec3 base_point;
    prc_surface_params surf_params_base = { 0 };

    /* Evaluate the base surface at (u, v) */
    surface_func base_eval_func = NULL;
    void *base_eval_params = NULL;
    int code = prc_get_surface_eval_func(ctx, &transform->base_surface.surface, &base_eval_func, &base_eval_params);
    if (code < 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Failed to get base surface eval function in prc_evaluate_surf_transform\n");
        return output;
    }

    surf_params_base.surface_params = base_eval_params;
    base_point = base_eval_func(ctx, &surf_params_base, u, v);
    code = prc_apply_math_transform(ctx, &transform->math_transform, base_point, &output);
    if (code < 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Failed to apply math transform in prc_evaluate_surf_transform\n");
    }

    return output;
}

prc_vec3
prc_evaluate_surf_blend03(prc_context *ctx, void *params, double u, double v)
{
    prc_surface_params *surf_params = (prc_surface_params *)params;
    prc_surf_blend03 *blend = (prc_surf_blend03 *)surf_params->surface_params;
    prc_vec3 output = { 0.0, 0.0, 0.0 };
    prc_vec3 center_pt, rail1_pt, rail2_pt;
    prc_vec3 x_dir, y_dir, cross_xy, y2_dir;
    double angle_curve_val, x_len, radius, y2_len, angle_a, implicit_v;

    if (!blend || blend->number_of_elements < 2 || !blend->parameters)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid spline data arrays in prc_evaluate_surf_blend03\n");
        return output;
    }

    /* 1. Evaluate underlying splines at parameter 'u' */
    /* Initial spec was wrong on the ordering, so we have a detector pick
       it for files that used the spec and for those that use the updated spec */
    rail1_pt = prc_blend03_eval_spline_vec3(u, blend, blend->layout_offsets.rail1_offset);
    rail2_pt = prc_blend03_eval_spline_vec3(u, blend, blend->layout_offsets.rail2_offset);
    center_pt = prc_blend03_eval_spline_vec3(u, blend, blend->layout_offsets.center_offset);
    angle_curve_val = prc_blend03_eval_spline_double(u, blend);

    /* 2. Compute Local Frame Orthogonal Vector Base */
    /* X(u) = (rail_curve_1(u) - center_curve(u)) unitized */
    x_dir.x = rail1_pt.x - center_pt.x;
    x_dir.y = rail1_pt.y - center_pt.y;
    x_dir.z = rail1_pt.z - center_pt.z;
    x_len = sqrt(x_dir.x * x_dir.x + x_dir.y * x_dir.y + x_dir.z * x_dir.z);
    if (x_len > 1e-12)
    {
        x_dir.x /= x_len; x_dir.y /= x_len; x_dir.z /= x_len;
    }

    /* Y(u) = rail_curve_2(u) - center_curve(u) */
    y_dir.x = rail2_pt.x - center_pt.x;
    y_dir.y = rail2_pt.y - center_pt.y;
    y_dir.z = rail2_pt.z - center_pt.z;

    /* Radius(u) = ||rail_curve_2(u) - center_curve(u)|| */
    radius = sqrt(y_dir.x * y_dir.x + y_dir.y * y_dir.y + y_dir.z * y_dir.z);

    /* Y_2(u) = [[X(u) ∧ Y(u)] ∧ X(u)] unitized */
    prc_vec_cross(x_dir, y_dir, &cross_xy);
    prc_vec_cross(cross_xy, x_dir, &y2_dir);
    y2_len = sqrt(y2_dir.x * y2_dir.x + y2_dir.y * y2_dir.y + y2_dir.z * y2_dir.z);
    if (y2_len > 1e-12)
    {
        y2_dir.x /= y2_len; y2_dir.y /= y2_len; y2_dir.z /= y2_len;
    }

    /* 3. Evaluate Scaled Circular Angular Sweep Profile Space */
    /* A(u) = rail2_anglesV_curve(u) / Rail2ParameterV */
    angle_a = 0.0;
    if (fabs(blend->rail_2_parameter_v) > 1e-12)
    {
        angle_a = angle_curve_val / blend->rail_2_parameter_v;
    }

    implicit_v = angle_a * v;

    /* 4. Construct Final 3D Geometric Coordinate Surface Point Mapping */
    output.x = center_pt.x + radius * (cos(implicit_v) * x_dir.x + sin(implicit_v) * y2_dir.x);
    output.y = center_pt.y + radius * (cos(implicit_v) * x_dir.y + sin(implicit_v) * y2_dir.y);
    output.z = center_pt.z + radius * (cos(implicit_v) * x_dir.z + sin(implicit_v) * y2_dir.z);

    return output;
}

prc_vec3
prc_evaluate_surf_blend01(prc_context *ctx, void *params, double u, double v)
{
    prc_surface_params *surf_params = (prc_surface_params *)params;
    prc_surf_blend01 *blend = (prc_surf_blend01 *)surf_params->surface_params;
    prc_vec3 output = { 0.0, 0.0, 0.0 };
    curve_func center_eval_func = NULL, origin_eval_func = NULL, tangent_eval_func = NULL;
    void *center_params = NULL, *origin_params = NULL, *tangent_params = NULL;
    double center_min_u, center_max_u, origin_min_u, origin_max_u, tangent_min_u, tangent_max_u;
    prc_vec3 center_pt, origin_pt, r_vec, tangent_vec, cross_vec;
    double tangent_len;
    int code;

    code = prc_get_curve_eval_func(ctx, &blend->center_curve, &center_eval_func,
        &center_params, &center_min_u, &center_max_u);
    if (code < 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid center curve type in prc_evaluate_surf_blend01\n");
        return output;
    }
    code = prc_get_curve_eval_func(ctx, &blend->origin_curve, &origin_eval_func,
        &origin_params, &origin_min_u, &origin_max_u);
    if (code < 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid origin curve type in prc_evaluate_surf_blend01\n");
        return output;
    }

    center_pt = center_eval_func(ctx, center_params, v);
    origin_pt = origin_eval_func(ctx, origin_params, v);

    r_vec.x = origin_pt.x - center_pt.x;
    r_vec.y = origin_pt.y - center_pt.y;
    r_vec.z = origin_pt.z - center_pt.z;

    if (!blend->tangent_curve.is_referenced && blend->tangent_curve.curve_type != PRC_TYPE_ROOT)
    {
        code = prc_get_curve_eval_func(ctx, &blend->tangent_curve, &tangent_eval_func,
            &tangent_params, &tangent_min_u, &tangent_max_u);
        if (code < 0)
        {
            prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid tangent curve type in prc_evaluate_surf_blend01\n");
            return output;
        }
        tangent_vec = tangent_eval_func(ctx, tangent_params, v);
    }
    else
    {
        /* No tangent curve: fall back to the unitized first derivative of the origin curve at v */
        double h = (origin_max_u - origin_min_u) * 1e-5;
        double v_plus, v_minus;
        prc_vec3 p_plus, p_minus;

        if (h < 1e-9) h = 1e-9;
        v_plus = v + h;
        v_minus = v - h;
        if (v_plus > origin_max_u) v_plus = origin_max_u;
        if (v_minus < origin_min_u) v_minus = origin_min_u;

        p_plus = origin_eval_func(ctx, origin_params, v_plus);
        p_minus = origin_eval_func(ctx, origin_params, v_minus);

        tangent_vec.x = (p_plus.x - p_minus.x) / (v_plus - v_minus);
        tangent_vec.y = (p_plus.y - p_minus.y) / (v_plus - v_minus);
        tangent_vec.z = (p_plus.z - p_minus.z) / (v_plus - v_minus);

        // ONLY unitize the derivative fallback
        tangent_len = sqrt(tangent_vec.x * tangent_vec.x + tangent_vec.y * tangent_vec.y + tangent_vec.z * tangent_vec.z);
        if (tangent_len > 1e-12)
        {
            tangent_vec.x /= tangent_len;
            tangent_vec.y /= tangent_len;
            tangent_vec.z /= tangent_len;
        }
    }
    prc_vec_cross(tangent_vec, r_vec, &cross_vec);

    /*
     * If cross_vec length is shrinking or your pipe looks oval,
     * ensure cross_vec has the exact same scalar magnitude as r_vec:
     */
    double r_len = sqrt(r_vec.x * r_vec.x + r_vec.y * r_vec.y + r_vec.z * r_vec.z);
    double c_len = sqrt(cross_vec.x * cross_vec.x + cross_vec.y * cross_vec.y + cross_vec.z * cross_vec.z);
    if (c_len > 1e-12) {
        cross_vec.x *= (r_len / c_len);
        cross_vec.y *= (r_len / c_len);
        cross_vec.z *= (r_len / c_len);
    }

    output.x = center_pt.x + cos(u) * r_vec.x + sin(u) * cross_vec.x;
    output.y = center_pt.y + cos(u) * r_vec.y + sin(u) * cross_vec.y;
    output.z = center_pt.z + cos(u) * r_vec.z + sin(u) * cross_vec.z;

    if (blend->has_transform && !blend->exact_geom_transform.is_identity)
    {
        output = prc_exact_geom_apply_transform(ctx, &blend->exact_geom_transform, output);
    }

    return output;
}

prc_vec3
prc_evaluate_surf_blend02(prc_context *ctx, void *params, double u, double v)
{
    prc_surface_params *surf_params = (prc_surface_params *)params;
    prc_surf_blend02 *blend = (prc_surf_blend02 *)surf_params->surface_params;
    prc_vec3 output = { 0.0, 0.0, 0.0 };
    curve_func center_eval_func = NULL;
    void *center_params = NULL;
    double center_min_u = 0.0, center_max_u = 0.0;
    double radius, implicit_v, cos_a, angle_a, x_len, y_len, y2_len;
    prc_vec3 center, p1, p2, x_dir, y_dir, cross_xy, y2_dir;
    int code;

    code = prc_get_curve_eval_func(ctx, &blend->center_curve, &center_eval_func,
        &center_params, &center_min_u, &center_max_u);
    if (code < 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid center curve type in prc_evaluate_surf_blend02\n");
        return output;
    }
    center = center_eval_func(ctx, center_params, u);

    code = prc_project_onto_blend_bound(ctx, &blend->bound_surface0, &blend->bound_curve0, center, &p1);
    if (code < 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid bound geometry 0 in prc_evaluate_surf_blend02\n");
        return output;
    }
    code = prc_project_onto_blend_bound(ctx, &blend->bound_surface1, &blend->bound_curve1, center, &p2);
    if (code < 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid bound geometry 1 in prc_evaluate_surf_blend02\n");
        return output;
    }

    x_dir.x = p1.x - center.x; x_dir.y = p1.y - center.y; x_dir.z = p1.z - center.z;
    x_len = sqrt(x_dir.x * x_dir.x + x_dir.y * x_dir.y + x_dir.z * x_dir.z);
    if (x_len > 1e-12)
    {
        x_dir.x /= x_len; x_dir.y /= x_len; x_dir.z /= x_len;
    }

    y_dir.x = p2.x - center.x; y_dir.y = p2.y - center.y; y_dir.z = p2.z - center.z;
    y_len = sqrt(y_dir.x * y_dir.x + y_dir.y * y_dir.y + y_dir.z * y_dir.z);
    if (y_len > 1e-12)
    {
        y_dir.x /= y_len; y_dir.y /= y_len; y_dir.z /= y_len;
    }

    cos_a = x_dir.x * y_dir.x + x_dir.y * y_dir.y + x_dir.z * y_dir.z;
    if (cos_a > 1.0) cos_a = 1.0;
    if (cos_a < -1.0) cos_a = -1.0;
    angle_a = acos(cos_a);

    prc_vec_cross(x_dir, y_dir, &cross_xy);
    prc_vec_cross(cross_xy, x_dir, &y2_dir);
    y2_len = sqrt(y2_dir.x * y2_dir.x + y2_dir.y * y2_dir.y + y2_dir.z * y2_dir.z);
    if (y2_len > 1e-12)
    {
        y2_dir.x /= y2_len; y2_dir.y /= y2_len; y2_dir.z /= y2_len;
    }

    radius = fabs(blend->radius0);
    implicit_v = (blend->parameterization_type == 0) ? angle_a * v : v;

    output.x = center.x + radius * (cos(implicit_v) * x_dir.x + sin(implicit_v) * y2_dir.x);
    output.y = center.y + radius * (cos(implicit_v) * x_dir.y + sin(implicit_v) * y2_dir.y);
    output.z = center.z + radius * (cos(implicit_v) * x_dir.z + sin(implicit_v) * y2_dir.z);

    if (blend->has_transform && !blend->exact_geom_transform.is_identity)
    {
        output = prc_exact_geom_apply_transform(ctx, &blend->exact_geom_transform, output);
    }

    return output;
}

prc_vec3
prc_evaluate_surf_cylindrical(prc_context *ctx, void *params, double u, double v)
{
    prc_vec3 output, base_point;
    prc_surface_params *surf_params = (prc_surface_params *)params;
    prc_surf_cylindrical *cylindrical = (prc_surf_cylindrical *)surf_params->surface_params;
    surface_func base_eval_func = NULL;
    void *base_params = NULL;
    prc_surface_params surf_params_base = { 0 };
    int code;
    prc_ptr_surface base_surf = cylindrical->base_surface;
    double base_start_u;
    double base_end_u;
    double base_start_v;
    double base_end_v;
    prc_surface_sampling_info sampling_info = { 0 };

    if (base_surf.is_referenced)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid base surface case in prc_evaluate_surf_cylindrical\n");
        output.x = 0;
        output.y = 0;
        output.z = 0;
        return output;
    }

    code = prc_get_surface_data(ctx, &base_surf.surface, &sampling_info);
    if (code < 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Error in prc_get_surface_data\n");
        output.x = 0;
        output.y = 0;
        output.z = 0;
        return output;
    }

    /* Lets get the base evaluation surface function */
    code = prc_get_surface_eval_func(ctx, &cylindrical->base_surface.surface, &base_eval_func, &base_params);
    if (code < 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid base surface type in prc_evaluate_surf_cylindrical\n");
        output.x = 0;
        output.y = 0;
        output.z = 0;
        return output;
    }

    /* Per spec: the implicit (u, v) passed to the base surface is derived
       from this surface's own given (u, v) via its UVParameterization,
       which is what "enables the surface to be reparameterized" -- u, v
       here are NOT already the base surface's native parameter */
    {
        double u_coeff_a = (cylindrical->parameterization.u_param_coeff_a != 0.0) ? cylindrical->parameterization.u_param_coeff_a : 1.0;
        double v_coeff_a = (cylindrical->parameterization.v_param_coeff_a != 0.0) ? cylindrical->parameterization.v_param_coeff_a : 1.0;
        double implicit_u = u * u_coeff_a + cylindrical->parameterization.u_param_coeff_b;
        double implicit_v = v * v_coeff_a + cylindrical->parameterization.v_param_coeff_b;

        surf_params_base.surface_params = base_params;
        base_point = base_eval_func(ctx, &surf_params_base, implicit_u, implicit_v);
    }
    output.x = base_point.x * cos(base_point.y);
    output.y = base_point.x * sin(base_point.y);
    output.z = base_point.z;

    if (cylindrical->has_transform && !cylindrical->exact_geom_transform.is_identity)
    {
        output = prc_exact_geom_apply_transform(ctx, &cylindrical->exact_geom_transform, output);
    }

    return output;
}


static double
prc_clamp_surface_param(double value, double min_value, double max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

static double
prc_wrap_surface_param(double value, double min_value, double max_value, double period)
{
    double span;

    if (period <= 0.0)
        return prc_clamp_surface_param(value, min_value, max_value);

    span = max_value - min_value;
    if (span == 0.0)
        return min_value;

    while (value < min_value)
        value += period;
    while (value > max_value)
        value -= period;

    return value;
}

int
prc_compute_surface_normal(prc_context *ctx, surface_func surface_eval_func,
    void *surface_params, double u, double v, double du, double dv,
    double start_u, double end_u, double start_v, double end_v,
    const prc_surface_sampling_info *sampling_info,
    uint8_t orientation, prc_vec3 *normal)
{
    prc_vec3 pu0, pu1, pv0, pv1;
    prc_vec3 tangent_u, tangent_v;
    double u0;
    double u1;
    double v0;
    double v1;
    int code;

    if (sampling_info->u_periodic)
    {
        u0 = prc_wrap_surface_param(u - du, start_u, end_u, sampling_info->u_period);
        u1 = prc_wrap_surface_param(u + du, start_u, end_u, sampling_info->u_period);
    }
    else
    {
        u0 = prc_clamp_surface_param(u - du, start_u, end_u);
        u1 = prc_clamp_surface_param(u + du, start_u, end_u);
    }

    if (sampling_info->v_periodic)
    {
        v0 = prc_wrap_surface_param(v - dv, start_v, end_v, sampling_info->v_period);
        v1 = prc_wrap_surface_param(v + dv, start_v, end_v, sampling_info->v_period);
    }
    else
    {
        v0 = prc_clamp_surface_param(v - dv, start_v, end_v);
        v1 = prc_clamp_surface_param(v + dv, start_v, end_v);
    }

    pu0 = surface_eval_func(ctx, surface_params, u0, v);
    pu1 = surface_eval_func(ctx, surface_params, u1, v);
    pv0 = surface_eval_func(ctx, surface_params, u, v0);
    pv1 = surface_eval_func(ctx, surface_params, u, v1);

    prc_vec_sub(pu1, pu0, &tangent_u);
    prc_vec_sub(pv1, pv0, &tangent_v);
    prc_vec_cross(tangent_u, tangent_v, normal);
    code = prc_vec_normalize(normal);
    if (code < 0)
    {
        normal->x = 0.0;
        normal->y = 0.0;
        normal->z = 1.0;
    }

    if (orientation == 0)
    {
        prc_vec_negate(normal);
    }

    return 0;
}

prc_vec3
prc_evaluate_surf_offset(prc_context *ctx, void *params, double u, double v)
{
    prc_surface_params *surf_params = (prc_surface_params *)params;
    prc_surf_offset *offset = (prc_surf_offset *)surf_params->surface_params;
    prc_surface_params base_surf_params;
    prc_vec3 output = { 0.0, 0.0, 0.0 };
    surface_func base_eval_func = NULL;
    void *base_params = NULL;
    prc_surface_sampling_info sampling_info = { 0 };
    prc_vec3 base_point, base_normal;
    double du, dv;
    int code;

    if (offset->base_surface.is_referenced)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid base surface case in prc_evaluate_surf_offset\n");
        return output;
    }

    code = prc_get_surface_eval_func(ctx, &offset->base_surface.surface, &base_eval_func, &base_params);
    if (code < 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid base surface type in prc_evaluate_surf_offset\n");
        return output;
    }

    code = prc_get_surface_data(ctx, &offset->base_surface.surface, &sampling_info);
    if (code < 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Error in prc_get_surface_data in prc_evaluate_surf_offset\n");
        return output;
    }

    du = fabs(sampling_info.end_u - sampling_info.start_u) * 1e-5;
    dv = fabs(sampling_info.end_v - sampling_info.start_v) * 1e-5;
    if (du < 1e-9)
        du = 1e-9;
    if (dv < 1e-9)
        dv = 1e-9;

    base_surf_params.surface_params = base_params;
    base_point = base_eval_func(ctx, &base_surf_params, u, v);

    /* Orientation 1 (no negation): the offset direction follows the base surface's natural normal */
    code = prc_compute_surface_normal(ctx, base_eval_func, &base_surf_params, u, v, du, dv,
        sampling_info.start_u, sampling_info.end_u, sampling_info.start_v, sampling_info.end_v,
        &sampling_info, 1, &base_normal);
    if (code < 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Error computing base normal in prc_evaluate_surf_offset\n");
        return output;
    }

    output.x = base_point.x + offset->offset_distance * base_normal.x;
    output.y = base_point.y + offset->offset_distance * base_normal.y;
    output.z = base_point.z + offset->offset_distance * base_normal.z;

    if (offset->has_transform && !offset->exact_geom_transform.is_identity)
    {
        output = prc_exact_geom_apply_transform(ctx, &offset->exact_geom_transform, output);
    }

    return output;
}
