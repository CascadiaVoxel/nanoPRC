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

/* Segment of exact tessellation work that provides tools and utility methods */

#include "prc_tessellation_exact_geom.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

static int
prc_apply_linear_math_transform(prc_context *ctx, prc_math_fct_3d_linear *linear,
    prc_vec3 base_point, prc_vec3 *output)
{
    if (linear == NULL)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Linear function pointer is NULL in prc_apply_linear_math_transform\n");
        return PRC_ERROR_INTERNAL;
    }
    double *matrix = linear->mat;
    double *vector = linear->vect;

    output->x = matrix[0] * base_point.x + matrix[3] * base_point.y + matrix[6] * base_point.z + vector[0];
    output->y = matrix[1] * base_point.x + matrix[4] * base_point.y + matrix[7] * base_point.z + vector[1];
    output->z = matrix[2] * base_point.x + matrix[5] * base_point.y + matrix[8] * base_point.z + vector[2];

    return 0;
}

static int
prc_apply_nonlinear_math_transform(prc_context *ctx, prc_math_fct_3d_nonlinear *nonlinear,
    prc_vec3 base_point, prc_vec3 *output)
{
    int code;
    prc_vec3 temp_point;

    if (nonlinear == NULL)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Nonlinear function pointer is NULL in prc_apply_nonlinear_math_transform\n");
        return PRC_ERROR_INTERNAL;
    }

    prc_math_fct_3d_linear *left = nonlinear->left_transformation.linear;
    prc_math_fct_3d_linear *right = nonlinear->right_transformation.linear;

    code = prc_apply_linear_math_transform(ctx, left, base_point, &temp_point);
    if (code != 0)
    {
        return code;
    }

    output->x = temp_point.x * cos(temp_point.y * nonlinear->d2);
    output->y = temp_point.x * sin(temp_point.y * nonlinear->d2);
    output->z = temp_point.z;

    code = prc_apply_linear_math_transform(ctx, right, *output, output);
    if (code != 0)
    {
        return code;
    }

    return 0;
}

/* Apply the 3D Math functions */
int
prc_apply_math_transform(prc_context *ctx, prc_math_fct_3d *function, prc_vec3 base_point, prc_vec3 *output)
{
    int code;

    if (function == NULL)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Function pointer is NULL in prc_exact_geom_apply_math_transform\n");
        return PRC_ERROR_INTERNAL;
    }

    if (function->tag == PRC_TYPE_MATH_FCT_3D_Linear)
    {
        code = prc_apply_linear_math_transform(ctx, function->linear, base_point, output);
        if (code != 0)
        {
            return code;
        }
        return 0;
    }
    else if (function->tag == PRC_TYPE_MATH_FCT_3D_nonLinear)
    {
        code = prc_apply_nonlinear_math_transform(ctx, function->nonlinear, base_point, output);
        if (code != 0)
        {
            return code;
        }
        return 0;
    }
    else
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Unknown function type in prc_exact_geom_apply_math_transform\n");
        return PRC_ERROR_INTERNAL;
    }
    return 0;
}

/* Invert the transform associated with the surface. This is applied to the loop
   samples */
int
prc_invert_exact_transform(prc_context *ctx, prc_exact_geom_transform *transform_in,
    prc_exact_geom_transform *inverse_transform_out)
{
    const double *m = transform_in->matrix;
    double *out;
    double a, b, c, d, e, f, g, h, i;
    double inv00, inv01, inv02, inv10, inv11, inv12, inv20, inv21, inv22;
    double det, inv_det;
    double tx, ty, tz;

    if (transform_in->is_identity)
    {
        memset(inverse_transform_out->matrix, 0, sizeof(double) * 16);
        inverse_transform_out->matrix[0] = 1.0;
        inverse_transform_out->matrix[5] = 1.0;
        inverse_transform_out->matrix[10] = 1.0;
        inverse_transform_out->matrix[15] = 1.0;
        inverse_transform_out->is_identity = 1;
        return 0;
    }

    /* Column-major 4x4: column c, row r lives at m[c * 4 + r] (see
       prc_api_transform_point). The upper-left 3x3 is the linear part. */
    a = m[0]; d = m[1]; g = m[2];
    b = m[4]; e = m[5]; h = m[6];
    c = m[8]; f = m[9]; i = m[10];

    det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (det == 0.0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Singular matrix in prc_invert_exact_transform\n");
        return PRC_ERROR_INTERNAL;
    }
    inv_det = 1.0 / det;

    inv00 = (e * i - f * h) * inv_det;
    inv01 = (c * h - b * i) * inv_det;
    inv02 = (b * f - c * e) * inv_det;
    inv10 = (f * g - d * i) * inv_det;
    inv11 = (a * i - c * g) * inv_det;
    inv12 = (c * d - a * f) * inv_det;
    inv20 = (d * h - e * g) * inv_det;
    inv21 = (b * g - a * h) * inv_det;
    inv22 = (a * e - b * d) * inv_det;

    tx = m[12]; ty = m[13]; tz = m[14];

    out = inverse_transform_out->matrix;
    memset(out, 0, sizeof(double) * 16);
    out[0] = inv00; out[1] = inv10; out[2] = inv20;
    out[4] = inv01; out[5] = inv11; out[6] = inv21;
    out[8] = inv02; out[9] = inv12; out[10] = inv22;
    /* Inverted translation is -(Inverse linear part) * original translation */
    out[12] = -(inv00 * tx + inv01 * ty + inv02 * tz);
    out[13] = -(inv10 * tx + inv11 * ty + inv12 * tz);
    out[14] = -(inv20 * tx + inv21 * ty + inv22 * tz);
    out[15] = 1.0;

    inverse_transform_out->is_identity = 0;

    return 0;
}

/* A version of the 3D transform that we use for exact geometry. This one is limited
   to Identity, Translate, Rotate and Scale */
int
prc_exact_geom_set_transform(prc_context *ctx, prc_exact_geom_transform *exact_geom_trans,
    const prc_trans_3d *prc_trans)
{
    if (exact_geom_trans == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "exact_geom_trans is NULL in prc_exact_geom_set_transform\n");
        return PRC_ERROR_MEMORY;
    }
    uint8_t other_flags_set = 0;
    double *matrix = exact_geom_trans->matrix;
    char behavior = prc_trans->behavior;

    memset(matrix, 0, sizeof(double) * 16);
    exact_geom_trans->is_identity = 0;

    /* Identity */
    matrix[0] = 1.0;
    matrix[5] = 1.0;
    matrix[10] = 1.0;
    matrix[15] = 1.0;

    if (behavior == 0)
    {
        exact_geom_trans->is_identity = 1;
        return 0;
    }

    if (behavior & PRC_TRANSFORMATION_Translate)
    {
        matrix[12] = prc_trans->translation.x;
        matrix[13] = prc_trans->translation.y;
        matrix[14] = prc_trans->translation.z;
    }

    if (behavior & PRC_TRANSFORMATION_Rotate)
    {
        prc_vec3 z_axis;

        if (behavior & PRC_TRANSFORMATION_Mirror)
        {
            /* Z is cross product of Y and X */
            prc_vec_cross(prc_trans->rotation[1],
                prc_trans->rotation[0], &z_axis);
        }
        else
        {
            prc_vec_cross(prc_trans->rotation[0],
                prc_trans->rotation[1], &z_axis);
        }
        /* Now load the matrix */
        matrix[0] = prc_trans->rotation[0].x;
        matrix[1] = prc_trans->rotation[0].y;
        matrix[2] = prc_trans->rotation[0].z;
        matrix[4] = prc_trans->rotation[1].x;
        matrix[5] = prc_trans->rotation[1].y;
        matrix[6] = prc_trans->rotation[1].z;
        matrix[8] = z_axis.x;
        matrix[9] = z_axis.y;
        matrix[10] = z_axis.z;

        other_flags_set = 1;
    }

    if (behavior & PRC_TRANSFORMATION_Scale)
    {
        for (int i = 0; i < 11; i++)
        {
            matrix[i] = matrix[i] * prc_trans->scale;
        }
    }

    /* Check if resulting matrix is still identity for performance */
    {
        static const double identity[16] = {
            1.0, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            0.0, 0.0, 0.0, 1.0
        };
        const double eps = 1e-12;
        int is_identity = 1;

        for (int i = 0; i < 16; i++)
        {
            if (fabs(matrix[i] - identity[i]) > eps)
            {
                is_identity = 0;
                break;
            }
        }

        if (is_identity)
            exact_geom_trans->is_identity = 1;
    }

    return 0;
}

prc_vec3
prc_exact_geom_apply_transform(prc_context *ctx,
    const prc_exact_geom_transform *exact_geom_trans, prc_vec3 point)
{
    prc_vec3 transformed = point;
    const double *m;

    if (exact_geom_trans == NULL || exact_geom_trans->is_identity)
        return transformed;

    m = exact_geom_trans->matrix;

    transformed.x = (m[0] * point.x) + (m[4] * point.y) + (m[8] * point.z) + m[12];
    transformed.y = (m[1] * point.x) + (m[5] * point.y) + (m[9] * point.z) + m[13];
    transformed.z = (m[2] * point.x) + (m[6] * point.y) + (m[10] * point.z) + m[14];

    return transformed;
}

/* A method to get the periodicity of a curve. Needed to determine the periodicity
   of a surface that makes use of this curve. */
void
prc_get_curve_periodicity(prc_context *ctx, prc_ptr_curve *curve,
    uint8_t *is_periodic, double *period)
{
    *is_periodic = 0;
    *period = 0.0;

    switch (curve->curve_type)
    {
    case PRC_TYPE_CRV_Circle:
    case PRC_TYPE_CRV_Ellipse:
        *is_periodic = 1;
        *period = 2.0 * PRC_PI;
        break;

    default:
        *is_periodic = 0;
        *period = 0;
    }
}

/* Line is defined by a start and a direction.  Project point onto this line */
prc_vec3
prc_project_point_onto_line(prc_context *ctx, prc_vec3 point, prc_vec3 origin,
    prc_vec3 vector)
{
    double temp1;
    prc_vec3 output;
    prc_vec3 b;
    prc_vec3 projected_point;

    temp1 = prc_vec_dot_product(vector, vector);
    if (temp1 == 0.0)
    {
        /* Quiet compiler */
        output.x = 0;
        output.y = 0;
        output.z = 0;
        prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid projection\n");
        return output;
    }

    prc_vec_sub(point, origin, &b);
    temp1 = prc_vec_dot_product(b, vector) / temp1;
    prc_vec_scale(temp1, &vector);
    prc_vec_add(origin, vector, &projected_point);

    return projected_point;
}

/* The NURBS Book (Piegl & Tiller) Algorithm A2.1 - find the knot span containing u */
uint32_t
prc_bspline_find_span(uint32_t degree, uint32_t num_ctrl_pts, double u, const double *knots)
{
    uint32_t n = num_ctrl_pts - 1;
    uint32_t low, high, mid;

    if (u >= knots[n + 1])
        return n;
    if (u <= knots[degree])
        return degree;

    low = degree;
    high = n + 1;
    mid = (low + high) / 2;
    while (u < knots[mid] || u >= knots[mid + 1])
    {
        if (u < knots[mid])
            high = mid;
        else
            low = mid;
        mid = (low + high) / 2;
    }
    return mid;
}

/* The NURBS Book (Piegl & Tiller) Algorithm A2.2 - the degree+1 nonzero basis functions at u */
void
prc_bspline_basis_funs(uint32_t span, double u, uint32_t degree, const double *knots, double *N)
{
    double left[PRC_BSPLINE_MAX_DEGREE + 1];
    double right[PRC_BSPLINE_MAX_DEGREE + 1];
    uint32_t j, r;

    N[0] = 1.0;
    for (j = 1; j <= degree; j++)
    {
        double saved = 0.0;

        left[j] = u - knots[span + 1 - j];
        right[j] = knots[span + j] - u;
        for (r = 0; r < j; r++)
        {
            double denom = right[r + 1] + left[j - r];
            double temp = (denom != 0.0) ? N[r] / denom : 0.0;

            N[r] = saved + right[r + 1] * temp;
            saved = left[j - r] * temp;
        }
        N[j] = saved;
    }
}

/* Minimizes the squared distance from point to curve_eval_func(t) via coarse sampling
   followed by Newton refinement on finite-difference derivatives of the squared distance */
prc_vec3
prc_project_point_onto_curve(prc_context *ctx, curve_func eval_func, void *curve_params,
    double min_u, double max_u, prc_vec3 point)
{
    const uint32_t coarse_samples = 64;
    const uint32_t max_iterations = 30;
    double h = (max_u - min_u) * 1e-5;
    double best_t = min_u;
    double best_dist_sq = -1.0;
    uint32_t i;
    double t;

    if (h < 1e-9)
        h = 1e-9;

    for (i = 0; i < coarse_samples; i++)
    {
        prc_vec3 p;
        double dx, dy, dz, dist_sq;

        t = min_u + (max_u - min_u) * ((double)i / (double)(coarse_samples - 1));
        p = eval_func(ctx, curve_params, t);
        dx = p.x - point.x;
        dy = p.y - point.y;
        dz = p.z - point.z;
        dist_sq = dx * dx + dy * dy + dz * dz;
        if (best_dist_sq < 0.0 || dist_sq < best_dist_sq)
        {
            best_dist_sq = dist_sq;
            best_t = t;
        }
    }

    t = best_t;
    for (i = 0; i < max_iterations; i++)
    {
        prc_vec3 p, p_plus, p_minus;
        double t_plus = t + h;
        double t_minus = t - h;
        double dx, dy, dz, dxp, dyp, dzp, dxm, dym, dzm;
        double f, f_plus, f_minus, f_prime, f_double_prime, delta;

        if (t_plus > max_u)
            t_plus = max_u;
        if (t_minus < min_u)
            t_minus = min_u;

        p = eval_func(ctx, curve_params, t);
        p_plus = eval_func(ctx, curve_params, t_plus);
        p_minus = eval_func(ctx, curve_params, t_minus);

        dx = p.x - point.x; dy = p.y - point.y; dz = p.z - point.z;
        f = dx * dx + dy * dy + dz * dz;
        dxp = p_plus.x - point.x; dyp = p_plus.y - point.y; dzp = p_plus.z - point.z;
        f_plus = dxp * dxp + dyp * dyp + dzp * dzp;
        dxm = p_minus.x - point.x; dym = p_minus.y - point.y; dzm = p_minus.z - point.z;
        f_minus = dxm * dxm + dym * dym + dzm * dzm;

        f_prime = (f_plus - f_minus) / (t_plus - t_minus);
        f_double_prime = (f_plus - 2.0 * f + f_minus) / (h * h);

        if (fabs(f_double_prime) < 1e-12)
            break;

        delta = f_prime / f_double_prime;
        t -= delta;
        if (t < min_u)
            t = min_u;
        if (t > max_u)
            t = max_u;

        if (fabs(delta) < 1e-12)
            break;
    }

    return eval_func(ctx, curve_params, t);
}

/* Gauss-Newton refinement of (u, v) (tangents from finite differences) starting
   from a caller-supplied guess, minimizing the squared distance from point to
   surface_eval_func(u,v). No coarse global search: callers that already have a
   good starting guess -- e.g. the previously projected sample along the same
   loop -- should prefer this over prc_project_point_onto_surface, since a fresh
   global coarse search per sample can converge to a different, equally valid
   but discontinuous (u,v) branch whenever the surface nearly meets itself (e.g.
   a NURBS cylinder's own seam, where u near min_u and u near max_u map to
   nearly the same 3D point); Newton's local convergence instead stays on
   whichever branch the guess is already on, keeping a sampled loop continuous
   in uv space */
prc_vec3
prc_refine_point_on_surface(prc_context *ctx, surface_func eval_func, void *surface_params,
    double min_u, double max_u, double min_v, double max_v, prc_vec3 point,
    double u0, double v0, double *out_u, double *out_v)
{
    const uint32_t max_iterations = 30;
    double hu = (max_u - min_u) * 1e-5;
    double hv = (max_v - min_v) * 1e-5;
    double u = u0, v = v0;
    uint32_t iter;

    if (hu < 1e-9)
        hu = 1e-9;
    if (hv < 1e-9)
        hv = 1e-9;

    for (iter = 0; iter < max_iterations; iter++)
    {
        prc_vec3 p, pu_plus, pu_minus, pv_plus, pv_minus;
        prc_vec3 su, sv, d;
        double u_plus = u + hu, u_minus = u - hu;
        double v_plus = v + hv, v_minus = v - hv;
        double g_u, g_v, h_uu, h_vv, h_uv, det, du, dv;

        if (u_plus > max_u)
            u_plus = max_u;
        if (u_minus < min_u)
            u_minus = min_u;
        if (v_plus > max_v)
            v_plus = max_v;
        if (v_minus < min_v)
            v_minus = min_v;

        p = eval_func(ctx, surface_params, u, v);
        pu_plus = eval_func(ctx, surface_params, u_plus, v);
        pu_minus = eval_func(ctx, surface_params, u_minus, v);
        pv_plus = eval_func(ctx, surface_params, u, v_plus);
        pv_minus = eval_func(ctx, surface_params, u, v_minus);

        su.x = (pu_plus.x - pu_minus.x) / (u_plus - u_minus);
        su.y = (pu_plus.y - pu_minus.y) / (u_plus - u_minus);
        su.z = (pu_plus.z - pu_minus.z) / (u_plus - u_minus);

        sv.x = (pv_plus.x - pv_minus.x) / (v_plus - v_minus);
        sv.y = (pv_plus.y - pv_minus.y) / (v_plus - v_minus);
        sv.z = (pv_plus.z - pv_minus.z) / (v_plus - v_minus);

        d.x = p.x - point.x;
        d.y = p.y - point.y;
        d.z = p.z - point.z;

        /* Gauss-Newton step minimizing |S(u,v) - point|^2 */
        g_u = d.x * su.x + d.y * su.y + d.z * su.z;
        g_v = d.x * sv.x + d.y * sv.y + d.z * sv.z;
        h_uu = su.x * su.x + su.y * su.y + su.z * su.z;
        h_vv = sv.x * sv.x + sv.y * sv.y + sv.z * sv.z;
        h_uv = su.x * sv.x + su.y * sv.y + su.z * sv.z;

        det = h_uu * h_vv - h_uv * h_uv;
        if (fabs(det) < 1e-14)
            break;

        du = (g_u * h_vv - g_v * h_uv) / det;
        dv = (g_v * h_uu - g_u * h_uv) / det;

        u -= du;
        v -= dv;
        if (u < min_u) u = min_u;
        if (u > max_u) u = max_u;
        if (v < min_v) v = min_v;
        if (v > max_v) v = max_v;

        if (fabs(du) < 1e-12 && fabs(dv) < 1e-12)
            break;
    }

    if (out_u != NULL)
        *out_u = u;
    if (out_v != NULL)
        *out_v = v;

    return eval_func(ctx, surface_params, u, v);
}

/* Minimizes the squared distance from point to surface_eval_func(u,v) via coarse grid
   sampling followed by Gauss-Newton refinement (prc_refine_point_on_surface).
   out_u/out_v are optional (pass NULL to ignore) and report the (u,v) found, for
   callers that need the surface parameter itself rather than just the projected point */
prc_vec3
prc_project_point_onto_surface(prc_context *ctx, surface_func eval_func, void *surface_params,
    double min_u, double max_u, double min_v, double max_v, prc_vec3 point,
    double *out_u, double *out_v)
{
    const uint32_t coarse_samples = 32;
    double best_u = min_u, best_v = min_v, best_dist_sq = -1.0;
    uint32_t i, j;
    double u, v;

    for (i = 0; i < coarse_samples; i++)
    {
        u = min_u + (max_u - min_u) * ((double)i / (double)(coarse_samples - 1));
        for (j = 0; j < coarse_samples; j++)
        {
            prc_vec3 p;
            double dx, dy, dz, dist_sq;

            v = min_v + (max_v - min_v) * ((double)j / (double)(coarse_samples - 1));
            p = eval_func(ctx, surface_params, u, v);
            dx = p.x - point.x; dy = p.y - point.y; dz = p.z - point.z;
            dist_sq = dx * dx + dy * dy + dz * dz;
            if (best_dist_sq < 0.0 || dist_sq < best_dist_sq)
            {
                best_dist_sq = dist_sq;
                best_u = u;
                best_v = v;
            }
        }
    }

    return prc_refine_point_on_surface(ctx, eval_func, surface_params,
        min_u, max_u, min_v, max_v, point, best_u, best_v, out_u, out_v);
}