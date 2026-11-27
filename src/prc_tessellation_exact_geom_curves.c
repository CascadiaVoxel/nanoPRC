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

/* Segment of exact tessellation work that is related to curves */

#include "prc_tessellation_exact_geom.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

static int
prc_get_hcg_line_data(prc_context *ctx, prc_hcg_line *data,
    prc_vec3 *start, prc_vec3 *end, prc_nano_brep_compressed_data *compressed_data)
{
    int code;

    code = prc_get_start_end_data(ctx, &data->start_end_data, start, end, compressed_data);

    return code;
}

/* The length below which two points of a circle are the same point, or a
   radius is zero. It goes with the size of the circle (its largest distance
   between two of the points known) and with the size of the coordinates, which
   are stored to about seven figures, not with a fixed number of model units:
   a fixed number takes every arc of a model drawn in metres, whose radii are
   a few hundredths, for a point. */
static double
prc_circle_tolerance(const prc_hcg_circle_information *info)
{
    const prc_vec3 *points[4];
    uint32_t count = 0;
    uint32_t i, j;
    double size = 0.0;
    double coordinates = 0.0;

    if (info->has_center)
        points[count++] = &info->center;
    if (info->has_start_end_points)
    {
        points[count++] = &info->start_point;
        points[count++] = &info->end_point;
    }
    if (info->has_middle_of_arc_point)
        points[count++] = &info->middle_of_arc_point;

    for (i = 0; i < count; i++)
    {
        coordinates = fmax(coordinates, fmax(fabs(points[i]->x), fmax(fabs(points[i]->y), fabs(points[i]->z))));
        for (j = i + 1; j < count; j++)
            size = fmax(size, prc_vec_dist_between_two_points(*points[i], *points[j]));
    }
    return fmax(1e-6 * size, 1e-7 * coordinates);
}

static void
prc_compute_hcg_circle_theta(prc_hcg_circle_information *info)
{
    prc_vec3 start_rel;
    prc_vec3 end_rel;
    prc_vec3 normal;
    prc_vec3 cross_vec;
    double dot_se;
    double signed_angle;
    double tolerance = prc_circle_tolerance(info);

    info->theta = 0.0;

    if (!info->has_center || !info->has_normal || !info->has_start_end_points)
    {
        if (info->has_center && info->has_start_end_points && info->has_circle_angle_bit)
        {
            /* In this case, we have the general circle and the flag that specifies
               if it is greater than pi.  */
            prc_vec3 diff1, diff2;
            double dot;
            double norm1, norm2;
            double cos_theta;

            prc_vec_sub(info->start_point, info->center, &diff1);
            prc_vec_sub(info->end_point, info->center, &diff2);
            dot = prc_vec_dot_product(diff1, diff2);
            norm1 = prc_vec_length_sqrt(diff1);
            norm2 = prc_vec_length_sqrt(diff2);

            if (norm1 == 0 || norm2 == 0)
            {
                return;
            }

            cos_theta = dot / (norm1 * norm2);
            if (cos_theta > 1.0f)
            {
                cos_theta = 1.0f;
            }
            if (cos_theta < -1.0f)
            {
                cos_theta = -1.0f;
            }

            info->theta = acos(cos_theta);

            if (info->circle_angle_bit)
            {
                info->theta = (2.0f * PRC_PI) - info->theta;
            }
        }
        return;
    }

    if (info->is_full_circle)
    {
        info->theta = 2.0 * PRC_PI;
        return;
    }

    if (info->is_arc_of_zero_pi_or_twopi)
    {
        /* A half circle: its center is the midpoint of start and end and its
           normal is set from the middle of the arc (prc_get_hcg_circle_data).
           Any other arc of zero, pi or two pi has no plane to sweep in */
        if (info->has_middle_of_arc_point &&
            prc_vec_dist_between_two_points(info->start_point, info->end_point) > tolerance)
        {
            info->theta = PRC_PI;
        }
        return;
    }

    prc_vec_sub(info->start_point, info->center, &start_rel);
    prc_vec_sub(info->end_point, info->center, &end_rel);

    if (prc_vec_length_sqrt(start_rel) <= tolerance ||
        prc_vec_length_sqrt(end_rel) <= tolerance)
    {
        return;
    }

    normal = info->normal;
    prc_vec_normalize(&normal);
    prc_vec_normalize(&start_rel);
    prc_vec_normalize(&end_rel);

    dot_se = prc_vec_dot_product(start_rel, end_rel);
    prc_vec_cross(start_rel, end_rel, &cross_vec);
    signed_angle = atan2(prc_vec_dot_product(normal, cross_vec), dot_se);
    if (signed_angle < 0.0)
    {
        signed_angle += 2.0 * PRC_PI;
    }

    if (info->has_middle_of_arc_point)
    {
        prc_vec3 mid_rel;
        prc_vec3 mid_cross;
        double mid_dot;

        prc_vec_sub(info->middle_of_arc_point, info->center, &mid_rel);
        if (prc_vec_length_sqrt(mid_rel) > tolerance)
        {
            prc_vec_normalize(&mid_rel);
            prc_vec_cross(start_rel, mid_rel, &mid_cross);
            mid_dot = prc_vec_dot_product(normal, mid_cross);
            if (mid_dot < 0.0)
            {
                signed_angle = 2.0 * PRC_PI - signed_angle;
            }
        }
    }

    info->theta = signed_angle;
}

int
prc_get_hcg_circle_data(prc_context *ctx, prc_hcg_circle *hcg_circle,
    prc_hcg_circle_information *info, prc_nano_brep_compressed_data *compressed_data)
{
    int code;

    memset(info, 0, sizeof(*info));

    if (hcg_circle->is_particular_circle)
    {
        info->is_arc_of_zero_pi_or_twopi = 1;
        info->is_full_circle = hcg_circle->particular_circle.full_circle;
        if (!hcg_circle->particular_circle.compressed_iso_spline)
        {
            info->has_start_end_points = 1;

            if (info->is_full_circle)
            {
                prc_start_end_data *start_end = &hcg_circle->particular_circle.start_end_data;

                /* Just get the start point and that is also the end point */
                if (start_end->is_vertex)
                {
                    code = prc_get_compressed_vertex(ctx, &start_end->start_vertex, &info->start_point, compressed_data);
                    if (code < 0)
                    {
                        prc_error(ctx, code, "Failed to get start vertex by id in prc_get_start_end_data\n");
                        return code;
                    }
                }
                else
                {
                    prc_get_compressed_point(ctx, &start_end->start_point, &info->start_point);
                }
                info->end_point = info->start_point;
            }
            else
            {
                code = prc_get_start_end_data(ctx, &hcg_circle->particular_circle.start_end_data,
                    &info->start_point, &info->end_point, compressed_data);
                if (code < 0)
                {
                    prc_error(ctx, code, "Failed to get start end in prc_get_hcg_circle_data\n");
                    return code;
                }
            }
        }
        if (info->is_full_circle)
        {
            info->has_center = 1;
            info->center = hcg_circle->particular_circle.center.point;
            info->normal = hcg_circle->particular_circle.normal_plane.point;
            if (prc_vec_normalize_sqrt(&info->normal) == 0)
            {
                info->has_normal = 1;
            }
        }
        else
        {
            info->has_middle_of_arc_point = 1;
            info->middle_of_arc_point = hcg_circle->particular_circle.middle_of_arc.point;
        }
    }
    else
    {
        if (!hcg_circle->general_circle.compressed_iso_spline)
        {
            info->has_start_end_points = 1;
            code = prc_get_start_end_data(ctx, &hcg_circle->general_circle.start_end_data,
                &info->start_point, &info->end_point, compressed_data);
            if (code < 0)
            {
                prc_error(ctx, code, "Failed to get start end in prc_get_hcg_circle_data\n");
                return code;
            }
        }
        /* In the general circle case, we have a center */
        info->has_center = 1;
        info->center = hcg_circle->general_circle.center.point;

        /* In the general circle case, the circle_angle_bit is true if the angle > pi */
        info->has_circle_angle_bit = 1;
        info->circle_angle_bit = hcg_circle->general_circle.circle_angle;
    }

    /* Pointed out as an issue by Ian */
    if (!hcg_circle->is_particular_circle && info->has_center && info->has_start_end_points)
    {
        prc_vec3 v1, v2;
        prc_vec3 computed_normal = { 0 };

        /* Reconstruct vectors relative to the center anchor point */
        prc_vec_sub(info->start_point, info->center, &v1);
        prc_vec_sub(info->end_point, info->center, &v2);

        /* Normal = (Start - Center) x (End - Center) */
        if (prc_vec_normalize_sqrt(&v1) == 0 && prc_vec_normalize_sqrt(&v2) == 0)
        {
            prc_vec_cross(v1, v2, &computed_normal);
            if (prc_vec_normalize_sqrt(&computed_normal) == 0)
            {
                if (info->circle_angle_bit)
                {
                    computed_normal.x = -computed_normal.x;
                    computed_normal.y = -computed_normal.y;
                    computed_normal.z = -computed_normal.z;
                }
                info->normal = computed_normal;
                info->has_normal = 1;
            }
        }
    }

    if (!info->has_center && !info->is_full_circle && info->has_start_end_points)
    {
        prc_vec3 midpoint;
        double tolerance = prc_circle_tolerance(info);

        if (prc_vec_dist_between_two_points(info->start_point, info->end_point) <= tolerance)
        {
            if (info->has_middle_of_arc_point &&
                prc_vec_dist_between_two_points(info->start_point, info->middle_of_arc_point) > tolerance)
            {
                /* Full circle: the opposite point is the arc midpoint and the center is
                   at the midpoint between the repeated start/end and that opposite point. */
                prc_vec_avg(info->start_point, info->middle_of_arc_point, &midpoint);
                info->center = midpoint;
                info->has_center = 1;
                info->is_arc_of_zero_pi_or_twopi = 1;
            }
            else
            {
                /* Zero-angle arc: nothing to infer; treat as degenerate and reject. */
                info->is_arc_of_zero_pi_or_twopi = 1;
            }
        }
        else if (info->has_middle_of_arc_point)
        {
            /* Half-circle: the circle center lies at the midpoint of the diameter endpoints. */
            prc_vec3 start_rel, mid_rel, plane_normal;

            prc_vec_avg(info->start_point, info->end_point, &midpoint);
            info->center = midpoint;
            info->has_center = 1;
            info->is_arc_of_zero_pi_or_twopi = 1;

            /* The arc runs from start through its middle to end, so its
               normal is start x middle, taken from the center */
            prc_vec_sub(info->start_point, info->center, &start_rel);
            prc_vec_sub(info->middle_of_arc_point, info->center, &mid_rel);
            if (prc_vec_normalize_sqrt(&start_rel) == 0 && prc_vec_normalize_sqrt(&mid_rel) == 0)
            {
                prc_vec_cross(start_rel, mid_rel, &plane_normal);
                if (prc_vec_normalize_sqrt(&plane_normal) == 0)
                {
                    info->normal = plane_normal;
                    info->has_normal = 1;
                }
            }
        }
    }

    /* If we have a valid, non-degenerate circle but no explicit endpoints, use the
       arc midpoint and center to synthesize a diameter pair. This keeps the caller
       supplied with start/end geometry for drawing and trimming while ignoring the
       truly degenerate zero-angle case. */
    if (!info->has_start_end_points && info->has_center && !info->is_full_circle &&
        !info->is_arc_of_zero_pi_or_twopi && info->has_middle_of_arc_point)
    {
        prc_vec3 radius_vec;

        prc_vec_sub(info->middle_of_arc_point, info->center, &radius_vec);
        if (prc_vec_length_sqrt(radius_vec) > prc_circle_tolerance(info))
        {
            prc_vec3 start = info->center;
            prc_vec3 end = info->center;

            prc_vec_sub(info->center, radius_vec, &start);
            prc_vec_add(info->center, radius_vec, &end);
            info->start_point = start;
            info->end_point = end;
            info->has_start_end_points = 1;
        }
    }

    if (info->has_center && info->has_start_end_points)
    {
        prc_compute_hcg_circle_theta(info);
    }

    return 0;
}

/* Evaluate parabola at a single point.

   The published type-0 formula is wrong; this uses the corrected one. We
   derived a correction independently, and it turned out to be algebraically
   identical to the one proposed in pdf-issues #782 -- two implementations
   arriving at the same evaluation from different starting points.

   The arrangement below is #782's rather than ours: y = 2*sqrt(x*f) instead of
   the equivalent y = 2*f*sqrt(x/f). Identical for any f > 0, but it avoids
   dividing by the focal length and taking the square root of a quotient, so it
   stays well behaved as f becomes small and needs no separate guard against a
   zero focal length -- where the old form evaluated 0/0, this one gives 0. We
   said on that issue that we would adopt it. */
static prc_vec3
prc_evaluate_parabola(prc_context *ctx, void *params, double input)
{
    prc_vec3 output;
    prc_crv_parabola *parabola = (prc_crv_parabola *)params;
    double focal_length = parabola->focal_length;
    output.z = 0;

    if (parabola->type == 0)
    {
        double param2 = input * input;
        double p2 = param2 / sqrt(16.0 * focal_length * focal_length + param2);
        output.x = p2;
        output.y = 2.0 * sqrt(output.x * focal_length);
        if (input < 0.0)
        {
            output.y = -output.y;
        }
    }
    else
    {
        output.x = focal_length * input * input;
        output.y = 2.0 * focal_length * input;
    }

    if (parabola->has_transform && !parabola->exact_geom_transform.is_identity)
    {
        output = prc_exact_geom_apply_transform(ctx, &parabola->exact_geom_transform, output);
    }

    return output;
}

/* Evaluate line at a single point */
static prc_vec3
prc_evaluate_line(prc_context *ctx, void *params, double input)
{
    prc_vec3 output;
    prc_crv_line *line = (prc_crv_line *)params;

    output.x = input;
    output.y = 0;
    output.z = 0;

    if (line->has_transform && !line->exact_geom_transform.is_identity)
    {
        output = prc_exact_geom_apply_transform(ctx, &line->exact_geom_transform, output);
    }

    return output;
}

/* For the compressed circle, we will always be running from zero to one
   and falling along the circle arc.  We should always have center and
   the start and end point and the normal vector */
static prc_vec3
prc_evaluate_circle_compressed(prc_context *ctx, void *params, double input)
{
    prc_vec3 output = { 0 };
    prc_hcg_circle *circle = (prc_hcg_circle *)params;
    prc_hcg_circle_information *info = &circle->circle_data;
    prc_vec3 basis_vector1;
    prc_vec3 basis_vector2;
    prc_vec3 normal;
    prc_vec3 sum;
    double scale1, scale2;
    double target_angle;
    int code;

    if (info->theta == 0)
    {
        return output;
    }

    if (input == 0)
    {
        return info->start_point;
    }

    if (input == 1)
    {
        return info->end_point;
    }

    /* Degenerate or zero-angle arc: fall back to a straight chord between endpoints */
    if (!info->has_normal || info->theta == 0.0)
    {
        output.x = (1.0 - input) * info->start_point.x + input * info->end_point.x;
        output.y = (1.0 - input) * info->start_point.y + input * info->end_point.y;
        output.z = (1.0 - input) * info->start_point.z + input * info->end_point.z;
        return output;
    }

    /* Get the basis vectors for our parameterization */
    prc_vec_sub(info->start_point, info->center, &basis_vector1);

    /* Make sure the normal vector is normalized */
    prc_vec_copy(info->normal, &normal, 0);
    code = prc_vec_normalize(&normal);
    if (code < 0)
    {
        return output;
    }

    /* FIXED: Pass the 'normal' copy (which is guaranteed to be unit length)
       instead of the un-normalized 'info->normal' parameter */
    prc_vec_cross(normal, basis_vector1, &basis_vector2);

    /* theta already accounts for circle_angle_bit: for a general circle
       prc_get_hcg_circle_data negates the normal when the bit is set, and
       prc_compute_hcg_circle_theta measures the angle about that normal, so
       a long arc arrives here as 2*pi minus the short angle. Flipping it
       again would sweep the short angle the wrong way round. */
    target_angle = input * info->theta;

    scale1 = cos(target_angle);
    scale2 = sin(target_angle);

    prc_vec_scale(scale1, &basis_vector1);
    prc_vec_scale(scale2, &basis_vector2);
    prc_vec_add(basis_vector1, basis_vector2, &sum);
    prc_vec_add(info->center, sum, &output);

    return output;
}

static prc_vec3
prc_evaluate_hermite_compressed(prc_context *ctx, void *params, double input)
{
    prc_vec3 output = { 0 };
    prc_hcg_bspline_hermite_curve *curve = (prc_hcg_bspline_hermite_curve *)params;
    prc_vec3 start_point;
    prc_vec3 end_point;
    prc_vec3 *key_points = NULL;
    prc_vec3 *key_tangents = NULL;
    prc_vec3 segment_delta;
    prc_vec3 p0, p3, p1, p2;
    prc_vec3 t0, t1;
    double segment_length;
    double segment_u;
    uint32_t segment_index;
    uint32_t segment_count;
    uint32_t i;
    double b0, b1, b2, b3;

    if (curve == NULL)
    {
        return output;
    }

    start_point = curve->start_end_data.start_point.point;
    end_point = curve->start_end_data.end_point.point;

    if (input <= 0.0)
    {
        return start_point;
    }
    if (input >= 1.0)
    {
        return end_point;
    }

    if (curve->number_points < 2)
    {
        return start_point;
    }

    key_points = (prc_vec3 *)prc_calloc(ctx, curve->number_points, sizeof(prc_vec3));
    if (key_points == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate key_points in prc_evaluate_hermite_compressed\n");
        return start_point;
    }

    key_points[0] = start_point;
    for (i = 0; i < curve->number_points - 2; i++)
    {
        if (curve->points != NULL)
        {
            prc_vec_add(key_points[i], curve->points[i], &key_points[i + 1]);
        }
        else
        {
            key_points[i + 1] = key_points[i];
        }
    }
    key_points[curve->number_points - 1] = end_point;

    if (curve->tangents != NULL)
    {
        /* Per spec Table 238, only the points (Ptc) are stored as a running delta;
           Tgtc[i] is used directly as the tangent at key point i (P1/P2/P4 formulas
           reference Tgtc[0..2] and Tgtc[3..5] with no cumulative sum). Accumulating
           the tangents compounds quantization noise across the curve, producing
           high-frequency wiggles. */
        key_tangents = curve->tangents;
    }

    segment_count = curve->number_points - 1;
    segment_u = input * (double)segment_count;
    segment_index = (uint32_t)segment_u;
    if (segment_index >= segment_count)
    {
        segment_index = segment_count - 1;
    }
    segment_u = segment_u - (double)segment_index;

    p0 = key_points[segment_index];
    p3 = key_points[segment_index + 1];

    if (curve->tangents == NULL)
    {
        prc_free(ctx, key_points);
        return p0;
    }

    t0 = key_tangents[segment_index];
    t1 = key_tangents[segment_index + 1];

    prc_vec_sub(p3, p0, &segment_delta);
    segment_length = prc_vec_length_sqrt(segment_delta);
    if (segment_length <= CURVE_PRECISION)
    {
        prc_free(ctx, key_points);
        return p0;
    }

    if (prc_vec_length_sqrt(t0) > CURVE_PRECISION)
    {
        prc_vec_scale(segment_length / 3.0 / prc_vec_length_sqrt(t0), &t0);
    }
    if (prc_vec_length_sqrt(t1) > CURVE_PRECISION)
    {
        prc_vec_scale(segment_length / 3.0 / prc_vec_length_sqrt(t1), &t1);
    }

    prc_vec_add(p0, t0, &p1);
    prc_vec_sub(p3, t1, &p2);

    b0 = (1.0 - segment_u) * (1.0 - segment_u) * (1.0 - segment_u);
    b1 = 3.0 * (1.0 - segment_u) * (1.0 - segment_u) * segment_u;
    b2 = 3.0 * (1.0 - segment_u) * segment_u * segment_u;
    b3 = segment_u * segment_u * segment_u;

    output.x = b0 * p0.x + b1 * p1.x + b2 * p2.x + b3 * p3.x;
    output.y = b0 * p0.y + b1 * p1.y + b2 * p2.y + b3 * p3.y;
    output.z = b0 * p0.z + b1 * p1.z + b2 * p2.z + b3 * p3.z;

    prc_free(ctx, key_points);
    return output;
}

/* For the compressed line, which has the start_end_data as its parameters
   and we just run from zero to one.  We really should only be here if the
   data is given as a point not a vertex. */
static prc_vec3
prc_evaluate_line_compressed(prc_context *ctx, void *params, double input)
{
    prc_vec3 output = { 0 };
    prc_start_end_data *line = (prc_start_end_data *)params;
    prc_vec3 point1 = { 0 };
    prc_vec3 point2 = { 0 };
    int code;
    prc_nano_brep_compressed_data *compressed_data;

    compressed_data = ctx->internal.nano_compressed_brep_ref_data;

    if (line->is_vertex)
    {
        code = prc_get_compressed_vertex(ctx, &line->start_vertex,
            &point1, compressed_data);
        if (code < 0)
        {
            prc_error(ctx, PRC_ERROR_INTERNAL, "Issue in prc_get_compressed_vertex\n");
            return output;
        }

        code = prc_get_compressed_vertex(ctx, &line->end_vertex,
            &point2, compressed_data);
        if (code < 0)
        {
            prc_error(ctx, PRC_ERROR_INTERNAL, "Issue in prc_get_compressed_vertex\n");
            return output;
        }
    }
    else
    {
        point1 = line->start_point.point;
        point2 = line->end_point.point;
    }

    if (input <= 0)
    {
        output = point1;
        return output;
    }
    if (input >= 1)
    {
        output = point2;
        return output;
    }

    prc_vec_scale(1 - input, &point1);
    prc_vec_scale(input, &point2);
    prc_vec_add(point1, point2, &output);

    return output;
}

/* Evaluate hyperbola at a single point */
static prc_vec3
prc_evaluate_hyperbola(prc_context *ctx, void *params, double input)
{
    prc_vec3 output;
    prc_crv_hyperbola *hyperbola = (prc_crv_hyperbola *)params;
    double semi_axis_image = hyperbola->semi_axis_image;
    double semi_axis = hyperbola->semi_axis;
    double temp;

    output.z = 0;

    if (hyperbola->type == 0)
    {
        if (semi_axis_image == 0.0)
        {
            prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid hyperbola semi_axis_image in prc_evaluate_hyperbola\n");
            return output;
        }
        temp = input / semi_axis;
        output.x = semi_axis_image * sqrt(1.0 + temp * temp);
        output.y = input;
    }
    else
    {
        output.x = semi_axis_image * cosh(input);
        output.y = semi_axis * sinh(input);
    }

    if (hyperbola->has_transform && !hyperbola->exact_geom_transform.is_identity)
    {
        output = prc_exact_geom_apply_transform(ctx, &hyperbola->exact_geom_transform, output);
    }

    return output;
}

/* Evaluate an offset curve at a single point */
static prc_vec3
prc_evaluate_offset_curve(prc_context *ctx, void *params, double input)
{
    prc_vec3 output;
    prc_crv_offset *offset = (prc_crv_offset *)params;
    prc_vec3 base_point;
    prc_vec3 offset_dir;
    curve_func base_func;
    int code;

    if (offset->base_func == NULL || offset->base_params == NULL)
    {
        output.x = 0.0;
        output.y = 0.0;
        output.z = 0.0;
        prc_error(ctx, PRC_ERROR_INTERNAL, "Missing base curve evaluator in prc_evaluate_offset_curve\n");
        return output;
    }

    base_func = (curve_func)offset->base_func;
    base_point = base_func(ctx, offset->base_params, input);
    offset_dir.x = 0.0;
    offset_dir.y = 0.0;
    offset_dir.z = 0.0;

    /* The base evaluator is also used for the finite-difference derivative. */
    {
        double h = 1e-5;
        double before_input = input - h;
        double after_input = input + h;
        prc_vec3 before;
        prc_vec3 after;
        prc_vec3 base_deriv;

        if (before_input < offset->parameterization.interval.min_value)
            before_input = offset->parameterization.interval.min_value;
        if (after_input > offset->parameterization.interval.max_value)
            after_input = offset->parameterization.interval.max_value;
        before = base_func(ctx, offset->base_params, before_input);
        after = base_func(ctx, offset->base_params, after_input);
        if (after_input == before_input)
            return base_point;
        base_deriv.x = (after.x - before.x) / (after_input - before_input);
        base_deriv.y = (after.y - before.y) / (after_input - before_input);
        base_deriv.z = (after.z - before.z) / (after_input - before_input);
        prc_vec_cross(base_deriv, offset->offset_plane_normal, &offset_dir);
    }

    code = prc_vec_normalize(&offset_dir);
    if (code < 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Degenerate offset direction in prc_evaluate_offset_curve\n");
        return base_point;
    }

    output.x = base_point.x + offset->offset * offset_dir.x;
    output.y = base_point.y + offset->offset * offset_dir.y;
    output.z = base_point.z + offset->offset * offset_dir.z;

    if (offset->has_transform && !offset->exact_geom_transform.is_identity)
    {
        output = prc_exact_geom_apply_transform(ctx, &offset->exact_geom_transform, output);
    }

    return output;
}

/* Evaluate circle at a single point */
static prc_vec3
prc_evaluate_circle(prc_context *ctx, void *params, double input)
{
    prc_vec3 output;
    prc_crv_circle *circle = (prc_crv_circle *)params;
    double radius = circle->radius;

    output.z = 0;
    output.x = radius * cos(input);
    output.y = radius * sin(input);

    if (circle->has_transform && !circle->exact_geom_transform.is_identity)
    {
        output = prc_exact_geom_apply_transform(ctx, &circle->exact_geom_transform, output);
    }

    return output;
}

/* Evaluate a polyline at a single point */
static prc_vec3
prc_evaluate_polyline(prc_context *ctx, void *params, double input)
{
    prc_vec3 output;
    prc_crv_polyline *polyline = (prc_crv_polyline *)params;
    uint32_t num_points = polyline->number_of_points;
    uint8_t is_3d = polyline->curve_data.is_3d_flag;

    if (num_points < 2)
    {
        /* Quiet compiler */
        output.x = 0;
        output.y = 0;
        output.z = 0;
        prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid polyline with less than 2 points in prc_evaluate_polyline\n");
        return output;
    }

    /* Clamp input to the range of the polyline */
    if (input < 0.0)
        input = 0.0;
    if (input > (double)(num_points - 1))
        input = (double)(num_points - 1);
    uint32_t index = (uint32_t)input;
    double t = input - (double)index;
    if (index >= num_points - 1)
    {
        if (is_3d)
        {
            output = polyline->points[num_points - 1].point_3d;
        }
        else
        {
            output.x = polyline->points[num_points - 1].point_2d.x;
            output.y = polyline->points[num_points - 1].point_2d.y;
            output.z = 0.0;
        }
    }
    else
    {
        if (is_3d)
        {
            prc_vec3 p0 = polyline->points[index].point_3d;
            prc_vec3 p1 = polyline->points[index + 1].point_3d;
            output.x = (1.0 - t) * p0.x + t * p1.x;
            output.y = (1.0 - t) * p0.y + t * p1.y;
            output.z = (1.0 - t) * p0.z + t * p1.z;
        }
        else
        {
            prc_vec2 p0 = polyline->points[index].point_2d;
            prc_vec2 p1 = polyline->points[index + 1].point_2d;
            output.x = (1.0 - t) * p0.x + t * p1.x;
            output.y = (1.0 - t) * p0.y + t * p1.y;
            output.z = 0.0;
        }
    }

    if (polyline->has_transform && !polyline->exact_geom_transform.is_identity)
    {
        output = prc_exact_geom_apply_transform(ctx, &polyline->exact_geom_transform, output);
    }

    return output;
}

/* Evaluate an ellipse at a single point */
static prc_vec3
prc_evaluate_ellipse(prc_context *ctx, void *params, double input)
{
    prc_vec3 output;
    prc_crv_ellipse *ellipse = (prc_crv_ellipse *)params;
    double rx = ellipse->rx;
    double ry = ellipse->ry;

    output.z = 0;
    output.x = rx * cos(input);
    output.y = ry * sin(input);

    if (ellipse->has_transform && !ellipse->exact_geom_transform.is_identity)
    {
        output = prc_exact_geom_apply_transform(ctx, &ellipse->exact_geom_transform, output);
    }

    return output;
}

/* Evaluate a 1D math function (ISO 14739-1, PRC_TYPE_MATH_FCT_1D and its
   five kinds) at param. A missing or unknown function evaluates to 0;
   fractions and combinations nest, so depth bounds the recursion */
static double
prc_evaluate_math_fct_1d(const prc_math_fct_1d *fct, double param, int depth)
{
    uint32_t i;
    double output = 0.0;

    if (fct == NULL || depth > 32)
        return 0.0;

    switch (fct->tag)
    {
    case PRC_TYPE_MATH_FCT_1D_Polynom:
        if (fct->polynom != NULL && fct->polynom->coefficients != NULL)
        {
            /* Horner's rule: coefficient[i] multiplies param to the power i */
            for (i = fct->polynom->number_of_coefficients; i > 0; i--)
                output = output * param + fct->polynom->coefficients[i - 1];
        }
        break;
    case PRC_TYPE_MATH_FCT_1D_Trigonometric:
        if (fct->trigonometric != NULL)
            output = fct->trigonometric->dc_offset + fct->trigonometric->amplitude *
                cos(param * fct->trigonometric->freq - fct->trigonometric->phase);
        break;
    case PRC_TYPE_MATH_FCT_1D_Fraction:
        if (fct->fraction != NULL)
        {
            double denominator = prc_evaluate_math_fct_1d(&fct->fraction->denominator, param, depth + 1);
            if (denominator != 0.0)
                output = prc_evaluate_math_fct_1d(&fct->fraction->numerator, param, depth + 1) / denominator;
        }
        break;
    case PRC_TYPE_MATH_FCT_1D_ArctanCos:
        if (fct->arctancos != NULL)
            output = atan(fct->arctancos->amplitude *
                cos(param * fct->arctancos->frequency + fct->arctancos->phase)) * fct->arctancos->a;
        break;
    case PRC_TYPE_MATH_FCT_1D_Combination:
        if (fct->combination != NULL && fct->combination->coefficients != NULL)
        {
            for (i = 0; i < fct->combination->number_of_coefficients; i++)
                output += fct->combination->coefficients[i].coefficient *
                    prc_evaluate_math_fct_1d(&fct->combination->coefficients[i].function, param, depth + 1);
        }
        break;
    default:
        break;
    }
    return output;
}

/* Coefficient i of a polynomial law, 0 if it has fewer */
static double
prc_helix_law_coefficient(const prc_math_fct_1d *law, uint32_t i)
{
    if (law->tag != PRC_TYPE_MATH_FCT_1D_Polynom || law->polynom == NULL ||
        law->polynom->coefficients == NULL || i >= law->polynom->number_of_coefficients)
        return 0.0;
    return law->polynom->coefficients[i];
}

/* Unit vector, or fallback when v has no length */
static prc_vec3
prc_helix_unit(prc_vec3 v, prc_vec3 fallback)
{
    double len = sqrt(v.x * v.x + v.y * v.y + v.z * v.z);

    if (len <= 1e-12)
        return fallback;
    v.x /= len;
    v.y /= len;
    v.z /= len;
    return v;
}

/* Evaluate a helix at a single point. Both sub-types build a point in a
   local frame and place it by transform_point(o, X, Z, p) = o + p.x X +
   p.y (Z x X) + p.z Z, the definition pdf-issues #853 proposes for the
   function the clause calls without defining.
   Type 0: o is start projected onto the axis, Z the axis direction, and X
   runs from o to start. TRUE turns clockwise seen from +Z, so the branches
   are exchanged against the pseudocode, as Adobe Acrobat draws it (#853).
   With start on the axis X has no direction; it is then Z x (1, 0, 0), and
   for an axis along x, where that is zero, world +y (both +x and -x), as
   Acrobat does.
   Type 1: o is start, Z is unit_z, and X is unit_u made orthogonal to Z:
   Acrobat draws a type 1 helix whose unit_u is not orthogonal to unit_z as
   the same curve as one whose unit_u is (#853). TRUE follows the
   pseudocode as written, as Acrobat does for this sub-type. */
static prc_vec3
prc_evaluate_helix(prc_context *ctx, void *params, double input)
{
    prc_vec3 output;
    prc_crv_helix01 *helix = (prc_crv_helix01 *)params;
    prc_vec3 o, x_axis, y_axis, z_axis, local;
    prc_vec3 unit_x = { 1.0, 0.0, 0.0 };
    prc_vec3 unit_y = { 0.0, 1.0, 0.0 };
    prc_vec3 unit_z = { 0.0, 0.0, 1.0 };
    double radius, angle;

    if (helix->type == 0)
    {
        prc_vec3 origin, to_start;
        double along;

        origin.x = helix->type0_helix.origin_0;
        origin.y = helix->type0_helix.origin_1;
        origin.z = helix->type0_helix.origin_2;
        z_axis.x = helix->type0_helix.direction_0;
        z_axis.y = helix->type0_helix.direction_1;
        z_axis.z = helix->type0_helix.direction_2;
        z_axis = prc_helix_unit(z_axis, unit_z);

        /* origin_on_axis: start projected onto the axis */
        to_start.x = helix->start.x - origin.x;
        to_start.y = helix->start.y - origin.y;
        to_start.z = helix->start.z - origin.z;
        along = to_start.x * z_axis.x + to_start.y * z_axis.y + to_start.z * z_axis.z;
        o.x = origin.x + along * z_axis.x;
        o.y = origin.y + along * z_axis.y;
        o.z = origin.z + along * z_axis.z;

        x_axis.x = helix->start.x - o.x;
        x_axis.y = helix->start.y - o.y;
        x_axis.z = helix->start.z - o.z;
        radius = sqrt(x_axis.x * x_axis.x + x_axis.y * x_axis.y + x_axis.z * x_axis.z);
        if (radius > 1e-12)
        {
            x_axis.x /= radius;
            x_axis.y /= radius;
            x_axis.z /= radius;
        }
        else
        {
            prc_vec_cross(z_axis, unit_x, &x_axis);
            x_axis = prc_helix_unit(x_axis, unit_y);
        }
        radius += input * helix->type0_helix.radius;
        angle = helix->orientation ? -input : input;
        local.x = radius * cos(angle);
        local.y = radius * sin(angle);
        local.z = helix->type0_helix.pitch * input;
    }
    else
    {
        const prc_type1_helix_data *t1 = &helix->type1_helix;
        double r1 = prc_helix_law_coefficient(&t1->radius_law, 0);
        double r2 = prc_helix_law_coefficient(&t1->radius_law, 1);
        double a1 = prc_helix_law_coefficient(&t1->theta_law, 0);
        double a2 = prc_helix_law_coefficient(&t1->theta_law, 1);
        double param = input;
        double dot;

        o = helix->start;
        z_axis.x = t1->unit_z0;
        z_axis.y = t1->unit_z1;
        z_axis.z = t1->unit_z2;
        z_axis = prc_helix_unit(z_axis, unit_z);
        x_axis.x = t1->unit_u0;
        x_axis.y = t1->unit_u1;
        x_axis.z = t1->unit_u2;
        dot = x_axis.x * z_axis.x + x_axis.y * z_axis.y + x_axis.z * z_axis.z;
        x_axis.x -= dot * z_axis.x;
        x_axis.y -= dot * z_axis.y;
        x_axis.z -= dot * z_axis.z;
        if (x_axis.x * x_axis.x + x_axis.y * x_axis.y + x_axis.z * x_axis.z <= 1e-24)
            prc_vec_cross(z_axis, fabs(z_axis.x) > 0.9 ? unit_y : unit_x, &x_axis);
        x_axis = prc_helix_unit(x_axis, unit_x);

        /* The clause's formula, as written */
        if (r1 + r2 != 0.0)
            param = (param / (r1 + r2)) * 2.0;
        radius = r1;
        if (a2 != a1)
            radius = r1 + (param - a1) * (r2 - r1) / (a2 - a1);
        angle = helix->orientation ? param : -param;
        local.x = radius * cos(angle);
        local.y = radius * sin(angle);
        local.z = prc_evaluate_math_fct_1d(&t1->z_law, param, 0);
    }

    prc_vec_cross(z_axis, x_axis, &y_axis);
    output.x = o.x + local.x * x_axis.x + local.y * y_axis.x + local.z * z_axis.x;
    output.y = o.y + local.x * x_axis.y + local.y * y_axis.y + local.z * z_axis.y;
    output.z = o.z + local.x * x_axis.z + local.y * y_axis.z + local.z * z_axis.z;

    if (helix->has_transform && !helix->exact_geom_transform.is_identity)
    {
        output = prc_exact_geom_apply_transform(ctx, &helix->exact_geom_transform, output);
    }

    return output;
}

static prc_vec3
prc_evaluate_crv_nurbs(prc_context *ctx, void *params, double u)
{
    prc_crv_nurbs *nurbs = (prc_crv_nurbs *)params;
    prc_vec3 output = { 0.0, 0.0, 0.0 };
    double N[PRC_BSPLINE_MAX_DEGREE + 1];
    uint32_t num_ctrl = nurbs->highest_index_of_control_points + 1;
    uint32_t span, i;
    double x = 0.0, y = 0.0, z = 0.0, weight_sum = 0.0;

    if (nurbs->d > PRC_BSPLINE_MAX_DEGREE)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "NURBS curve degree exceeds supported maximum\n");
        return output;
    }

    span = prc_bspline_find_span(nurbs->d, num_ctrl, u, nurbs->u);
    prc_bspline_basis_funs(span, u, nurbs->d, nurbs->u, N);

    for (i = 0; i <= nurbs->d; i++)
    {
        uint32_t ctrl = span - nurbs->d + i;
        prc_control_points_nurbs_crv *cp = &nurbs->p[ctrl];
        double weight = nurbs->is_rational ? cp->w : 1.0;
        double basis = N[i] * weight;

        x += basis * cp->x;
        y += basis * cp->y;
        z += basis * cp->z;
        weight_sum += basis;
    }

    if (weight_sum != 0.0)
    {
        output.x = x / weight_sum;
        output.y = y / weight_sum;
        output.z = z / weight_sum;
    }

#if PRC_DEBUG_TESSELLATION_TRACE
    if (output.x > 400.0 || output.y > 400.0 || output.x < -400.0 || output.y < -400.0)
    {
        fprintf(stderr,
            "[NURBS eval debug] u=%g span=%u weight_sum=%g\n"
            "  output=(%g,%g,%g)\n",
            u, span, weight_sum,
            output.x, output.y, output.z);
        for (i = 0; i <= nurbs->d; i++)
        {
            uint32_t ctrl = span - nurbs->d + i;
            prc_control_points_nurbs_crv *cp = &nurbs->p[ctrl];
            fprintf(stderr,
                "  ctrl[%u]=(x=%g,y=%g,z=%g,w=%g) N=%g weighted=%g\n",
                ctrl, cp->x, cp->y, cp->z, cp->w,
                N[i], N[i] * (nurbs->is_rational ? cp->w : 1.0));
        }
    }
#endif

    return output;
}

static prc_vec3
prc_evaluate_onsurf(prc_context *ctx, void *params, double w)
{
    prc_crv_onsurf *onsurf = (prc_crv_onsurf *)params;
    prc_vec3 output = { 0.0, 0.0, 0.0 };
    curve_func base_curve_eval;
    prc_vec3 curve_output;
    surface_func base_surf_eval;

    if (onsurf == NULL || onsurf->base_curve_func == NULL ||
        onsurf->base_curve_params == NULL || onsurf->base_surface_func == NULL ||
        onsurf->base_surface_params == NULL)
    {
        return output;
    }

    /* Evaluate w on the curve to get the u, v position */
    base_curve_eval = (curve_func)onsurf->base_curve_func;
    curve_output = base_curve_eval(ctx, onsurf->base_curve_params, w);

    /* Evaluate u, v on the surface to get the XYZ position */
    base_surf_eval = (surface_func)onsurf->base_surface_func;
    output = base_surf_eval(ctx, onsurf->base_surface_params, curve_output.x, curve_output.y);

    return output;
}

static prc_vec3
prc_evaluate_composite(prc_context *ctx, void *params, double u)
{
    prc_crv_composite *composite = (prc_crv_composite *)params;
    prc_vec3 output = { 0.0, 0.0, 0.0 };
    double implicit_parameter;
    uint32_t subcurve_index;
    double delta;
    double local_param;
    prc_curve_sampling_info subcurve_info;
    prc_composite_subcurve *subcurve;
    curve_func subcurve_eval;
    void *subcurve_params;
    int code;

    if (composite == NULL || composite->subcurves == NULL || composite->number_of_subcurves == 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid composite curve in prc_evaluate_composite\n");
        return output;
    }

    if (composite->parameterization.coeff_a != 0.0)
    {
        implicit_parameter = (u - composite->parameterization.coeff_b) / composite->parameterization.coeff_a;
    }
    else
    {
        implicit_parameter = u;
    }

    if (implicit_parameter < 0.0)
        implicit_parameter = 0.0;
    else if (implicit_parameter > (double)composite->number_of_subcurves)
        implicit_parameter = (double)composite->number_of_subcurves;

    subcurve_index = (uint32_t)implicit_parameter;
    if (subcurve_index >= composite->number_of_subcurves)
        subcurve_index = composite->number_of_subcurves - 1;

    subcurve = &composite->subcurves[subcurve_index];
    memset(&subcurve_info, 0, sizeof(subcurve_info));
    code = prc_get_curve_sample_info(ctx, &subcurve->ptr_curve, &subcurve_info);
    if (code < 0)
    {
        prc_error(ctx, code, "Failed to sample composite subcurve in prc_evaluate_composite\n");
        return output;
    }

    if (subcurve->base_func != NULL && subcurve->base_params != NULL)
    {
        subcurve_eval = (curve_func)subcurve->base_func;
        subcurve_params = subcurve->base_params;
    }
    else
    {
        subcurve_eval = subcurve_info.curve_eval_func;
        subcurve_params = subcurve_info.curve_params;
    }

    delta = implicit_parameter - (double)subcurve_index;
    if (subcurve->sense)
    {
        local_param = subcurve_info.start + delta * (subcurve_info.end - subcurve_info.start);
    }
    else
    {
        local_param = subcurve_info.end - delta * (subcurve_info.end - subcurve_info.start);
    }

    output = subcurve_eval(ctx, subcurve_params, local_param);
    return output;
}

static int
prc_get_compressed_curve_sample_info(prc_context *ctx, prc_compressed_curve *curve,
    prc_curve_sampling_info *sample_info)
{
    int code;

    switch (curve->curve_type)
    {
    case PRC_HCG_Line:
    {
        /* We will sample from 0 to 1 and run along the start and end data */
        sample_info->curve_params = &curve->hcg_line.start_end_data;
        sample_info->curve_eval_func = prc_evaluate_line_compressed;
        sample_info->start = 0;
        sample_info->end = 1;
        sample_info->num_samples = 2;
        break;
    }
    case PRC_HCG_Circle:
    {
        /* We will sample from 0 to 1 and run along the circle arc length
           specified.  First though distill the circle information from
           the particular/general circle forms that we have */
        if (!curve->hcg_circle.information_valid)
        {
            code = prc_get_hcg_circle_data(ctx, &curve->hcg_circle,
                &curve->hcg_circle.circle_data, ctx->internal.nano_compressed_brep_ref_data);
            if (code < 0)
            {
                prc_error(ctx, PRC_ERROR_INTERNAL, "Failed in prc_get_hcg_circle_data\n");
                return PRC_ERROR_INTERNAL;
            }
            curve->hcg_circle.information_valid = 1;
        }
        sample_info->curve_params = &curve->hcg_circle;
        sample_info->curve_eval_func = prc_evaluate_circle_compressed;
        sample_info->start = 0;
        sample_info->end = 1;
        sample_info->num_samples = CURVE_SAMPLES;
        break;
    }

    case PRC_HCG_BsplineHermiteCurve:
    {
        /* We will sample from 0 to 1 and run along the start and end data */
        sample_info->curve_params = &curve->hcg_bspline_hermite_curve;
        sample_info->curve_eval_func = prc_evaluate_hermite_compressed;
        sample_info->start = 0;
        sample_info->end = 1;
        sample_info->num_samples = CURVE_SAMPLES;
        break;
    }

    case PRC_HCG_CompositeCurve:
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "TODO implement this\n");
        return PRC_ERROR_INTERNAL;

        break;
    }

    default:
        prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid base base curve type in prc_get_compressed_curve_sample_info\n");
        return PRC_ERROR_INTERNAL;

    }
    return 0;
}

int
prc_get_curve_sample_info(prc_context *ctx, prc_ptr_curve *ptr_curve,
    prc_curve_sampling_info *sample_info)
{
    int code;

    switch (ptr_curve->curve_type)
    {
    case PRC_TYPE_CRV_NURBS:
    {
        prc_crv_nurbs *nurbs = ptr_curve->crv_nurbs;

        sample_info->curve_params = (void *)nurbs;
        sample_info->curve_eval_func = prc_evaluate_crv_nurbs;
        sample_info->start = nurbs->u[nurbs->d];
        sample_info->end = nurbs->u[nurbs->highest_index_of_knots - nurbs->d];
        sample_info->num_samples = CURVE_SAMPLES;
        break;
    }

    case PRC_TYPE_CRV_Parabola:
    {
        prc_crv_parabola *parabola = ptr_curve->crv_parabola;
        prc_parameterization params = parabola->parameterization;

        sample_info->curve_params = (void *)parabola;
        sample_info->curve_eval_func = prc_evaluate_parabola;
        sample_info->start = params.interval.min_value;
        sample_info->end = params.interval.max_value;
        sample_info->num_samples = CURVE_SAMPLES;
        break;
    }

    case PRC_TYPE_CRV_Line:
    {
        prc_crv_line *line = ptr_curve->crv_line;
        prc_parameterization params = line->parameterization;

        sample_info->curve_params = (void *)line;
        sample_info->curve_eval_func = prc_evaluate_line;
        sample_info->start = params.interval.min_value;
        sample_info->end = params.interval.max_value;
        sample_info->num_samples = 2;
        break;
    }

    case PRC_TYPE_CRV_Hyperbola:
    {
        prc_crv_hyperbola *hyperbola = ptr_curve->crv_hyperbola;
        prc_parameterization params = hyperbola->parameterization;

        sample_info->curve_params = (void *)hyperbola;
        sample_info->curve_eval_func = prc_evaluate_hyperbola;
        sample_info->start = params.interval.min_value;
        sample_info->end = params.interval.max_value;
        sample_info->num_samples = CURVE_SAMPLES;
        break;
    }

    case PRC_TYPE_CRV_Circle:
    {
        prc_crv_circle *circle = ptr_curve->crv_circle;
        prc_parameterization params = circle->parameterization;

        sample_info->curve_params = (void *)circle;
        sample_info->curve_eval_func = prc_evaluate_circle;
        sample_info->start = params.interval.min_value;
        sample_info->end = params.interval.max_value;
        sample_info->num_samples = CURVE_SAMPLES;
        break;
    }

    case PRC_TYPE_CRV_Ellipse:
    {
        prc_crv_ellipse *ellipse = ptr_curve->crv_ellipse;
        prc_parameterization params = ellipse->parameterization;

        sample_info->curve_params = (void *)ellipse;
        sample_info->curve_eval_func = prc_evaluate_ellipse;
        sample_info->start = params.interval.min_value;
        sample_info->end = params.interval.max_value;
        sample_info->num_samples = CURVE_SAMPLES;
        break;
    }

    case PRC_TYPE_CRV_Helix01:
    {
        prc_crv_helix01 *helix = ptr_curve->crv_helix01;
        prc_parameterization params = helix->parameterization;

        sample_info->curve_params = (void *)helix;
        sample_info->curve_eval_func = prc_evaluate_helix;
        sample_info->start = params.interval.min_value;
        sample_info->end = params.interval.max_value;
        sample_info->num_samples = CURVE_SAMPLES;
        break;
    }

    /* A series of straight line segments */
    case PRC_TYPE_CRV_PolyLine:
    {
        prc_crv_polyline *polyline = ptr_curve->crv_polyline;

        sample_info->curve_params = (void *)polyline;
        sample_info->curve_eval_func = prc_evaluate_polyline;
        sample_info->start = 0.0;
        sample_info->end = (double)(polyline->number_of_points - 1);
        sample_info->num_samples = polyline->number_of_points;
        break;
    }

    case PRC_TYPE_CRV_Offset:
    {
        prc_crv_offset *offset = ptr_curve->crv_offset;
        prc_ptr_curve base_curve = offset->base_curve;

        /* Make sure the base_curve is NOT PRC_TYPE_CRV_Offset to avoid
           deep recursions */
        if (base_curve.curve_type == PRC_TYPE_CRV_Offset)
        {
            prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid base base curve type in prc_get_curve_sample_info\n");
            return PRC_ERROR_INTERNAL;
        }

        /* Sample info returns with details for the base_curve */
        code = prc_get_curve_sample_info(ctx, &base_curve, sample_info);
        if (code < 0)
        {
            return code;
        }
        break;
    }

    case PRC_TYPE_CRV_Composite:
    {
        prc_crv_composite *composite = ptr_curve->crv_composite;

        sample_info->curve_params = (void *)composite;
        sample_info->curve_eval_func = prc_evaluate_composite;
        sample_info->start = 0.0;
        sample_info->end = (double)(composite->number_of_subcurves);
        sample_info->num_samples = (composite->number_of_subcurves > 0) ?
            (composite->number_of_subcurves * 2U + 1U) : 1U;
        break;
    }

    case PRC_TYPE_CRV_OnSurf:
    {
        prc_crv_onsurf *onsurf = ptr_curve->crv_onsurf;
        prc_curve_sampling_info base_curve_sample_info;
        prc_surface_sampling_info surface_sample_info;
        surface_func surf_eval_func = NULL;
        void *surf_eval_params = NULL;
        prc_surface_sampling_info surface_samp_info;

        sample_info->curve_params = (void *)onsurf;
        sample_info->curve_eval_func = prc_evaluate_onsurf;

        /* Get details of base curve sample type */
        code = prc_get_curve_sample_info(ctx, &onsurf->uv_curve, &base_curve_sample_info);
        if (code < 0)
        {
            return code;
        }
        onsurf->base_curve_func = base_curve_sample_info.curve_eval_func;
        onsurf->base_curve_params = base_curve_sample_info.curve_params;
        sample_info->start = base_curve_sample_info.start;
        sample_info->end = base_curve_sample_info.end;
        sample_info->num_samples = base_curve_sample_info.num_samples;

        /* Get details on surface sample type. This sets up the matrix */
        code = prc_get_surface_data(ctx, &onsurf->surface.surface, &surface_samp_info);
        if (code < 0)
        {
            return code;
        }

        code = prc_get_surface_eval_func(ctx, &onsurf->surface.surface,
            &surf_eval_func, &surf_eval_params);
        if (code < 0)
        {
            return code;
        }
        onsurf->base_surface_func = surf_eval_func;
        onsurf->base_surface_params = surf_eval_params;
        break;
    }

    default:
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid base base curve type in prc_get_curve_sample_info\n");
        return PRC_ERROR_INTERNAL;
    }
    }
    return 0;
}

int
prc_sample_compressed_curve(prc_context *ctx, prc_data *data,
    prc_compressed_curve *curve, double curve_tolerance, prc_exact_geom_wire_data *wire_data,
    prc_nano_brep_compressed_data *compressed_data)
{
    uint8_t curve_approx_good = 0;
    void *curve_params = NULL;
    curve_func curve_eval_func = NULL;
    double start;
    double end;
    uint32_t i;
    double t, t0, t1, dist;
    prc_vec3 p0, p1, mid, seg_mid;
    uint32_t num_samples;
    prc_curve_sampling_info sample_info;
    prc_exact_geom_transform exact_geom_trans;
    prc_trans_3d transform;
    int code;
    double tolerance = fmax(CURVE_PRECISION, curve_tolerance);
    prc_nano_brep_compressed_data *curr_ctx_compressed_data;

    /* The compressed curve handling needs access to the the reference data
       but we want to keep the same prototype as the non-compressed curve
       so to keep from splitting them we pass the compressed data in through
       the context */
    curr_ctx_compressed_data = ctx->internal.nano_compressed_brep_ref_data;
    ctx->internal.nano_compressed_brep_ref_data = compressed_data;

    code = prc_get_compressed_curve_sample_info(ctx, curve, &sample_info);
    if (code < 0)
    {
        ctx->internal.nano_compressed_brep_ref_data = curr_ctx_compressed_data;
        return code;
    }
    start = sample_info.start;
    end = sample_info.end;
    num_samples = sample_info.num_samples;
    curve_params = sample_info.curve_params;
    curve_eval_func = sample_info.curve_eval_func;

    exact_geom_trans.is_identity = 1;
    transform.behavior = 0;

    if (curve_eval_func == NULL)
    {
        ctx->internal.nano_compressed_brep_ref_data = curr_ctx_compressed_data;
        prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid curve evaluation function in prc_sample_curve\n");
        return PRC_ERROR_INTERNAL;
    }

    while (!curve_approx_good)
    {
        curve_approx_good = 1;

        for (i = 0; i < num_samples - 1; i++)
        {
            t0 = start + (end - start) * ((double)i / (double)(num_samples - 1));
            t1 = start + (end - start) * ((double)(i + 1) / (double)(num_samples - 1));
            p0 = curve_eval_func(ctx, curve_params, t0);
            p1 = curve_eval_func(ctx, curve_params, t1);
            mid = curve_eval_func(ctx, curve_params, (t0 + t1) / 2.0);

            /* Evaluate the midpoint of the segment */
            seg_mid.x = (p0.x + p1.x) / 2.0;
            seg_mid.y = (p0.y + p1.y) / 2.0;
            seg_mid.z = (p0.z + p1.z) / 2.0;

            /* Calculate the distance from the midpoint to the curve. Use the encoded
               curve tolerance as the actual acceptance threshold, with a small flooring
               value to avoid zero-tolerance degenerate cases. */
            dist = sqrt((mid.x - seg_mid.x) * (mid.x - seg_mid.x) +
                (mid.y - seg_mid.y) * (mid.y - seg_mid.y) +
                (mid.z - seg_mid.z) * (mid.z - seg_mid.z));
            {
                if (dist > tolerance)
                {
                    curve_approx_good = 0;
                    break;
                }
            }
        }
        if (!curve_approx_good)
        {
            if (num_samples >= (1u << 20))
            {
                ctx->internal.nano_compressed_brep_ref_data = curr_ctx_compressed_data;
                prc_error(ctx, PRC_ERROR_INTERNAL,
                    "Compressed curve approximation failed to converge in prc_sample_compressed_curve\n");
                return PRC_ERROR_INTERNAL;
            }
            num_samples *= 2;
        }
    }

    wire_data->number_of_points = num_samples;
    wire_data->points = (prc_vec3 *)prc_calloc(ctx, num_samples, sizeof(prc_vec3));
    if (wire_data->points == NULL)
    {
        ctx->internal.nano_compressed_brep_ref_data = curr_ctx_compressed_data;
        prc_error(ctx, PRC_ERROR_MEMORY, "Allocation failure of wire_data points in prc_sample_curve\n");
        prc_free(ctx, wire_data);
        return PRC_ERROR_MEMORY;
    }

    for (i = 0; i < num_samples; i++)
    {
        t = start + (end - start) * ((double)i / (double)(num_samples - 1));
        wire_data->points[i] = curve_eval_func(ctx, curve_params, t);
    }

    ctx->internal.nano_compressed_brep_ref_data = curr_ctx_compressed_data;
    return 0;
}

/* Sample curve but dealing with the compressed curve case. It would be
   nice to reduce replicated code with the non-compressed case */
int
prc_sample_compressed_curve_store_in_body(prc_context *ctx, prc_data *data, uint32_t shell_index,
    uint32_t face_index, prc_compressed_curve *curve, double curve_tolerance,
    prc_nano_brep_compressed_data *compressed_data)
{
    uint32_t geom_count = data->exact_geom_tess_part_count;
    uint32_t file_index = data->exact_geom_tess_part[geom_count].file_index;
    uint32_t topo_index = data->exact_geom_tess_part[geom_count].topo_context_index;
    uint32_t body_index = data->exact_geom_tess_part[geom_count].body_index;
    int code;

    /* We now have a sufficient precision on the curve. Lets generate the
       XYZ sample points and store them */
    data->exact_geom_tess_part[geom_count].shells[shell_index].faces[face_index].wire_data =
        (prc_exact_geom_wire_data *)prc_calloc(ctx, 1, sizeof(prc_exact_geom_wire_data));
    if (data->exact_geom_tess_part[geom_count].shells[shell_index].faces[face_index].wire_data == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Allocation failure in prc_sample_compressed_curve_store_in_body\n");
        return PRC_ERROR_MEMORY;
    }

    code = prc_sample_compressed_curve(ctx, data, curve, curve_tolerance,
        data->exact_geom_tess_part[geom_count].shells[shell_index].faces[face_index].wire_data,
        compressed_data);
    if (code < 0)
    {
        prc_error(ctx, code, "Failure in prc_sample_compressed_curve\n");
        return code;
    }
    return 0;
}

int
prc_sample_curve(prc_context *ctx, prc_content_wire_edge *curve,
    prc_exact_geom_wire_data *wire_data)
{
    uint8_t curve_approx_good = 0;
    void *curve_params = NULL;
    curve_func curve_eval_func = NULL;
    double start;
    double end;
    uint32_t i;
    double t, t0, t1, dist;
    prc_vec3 p0, p1, mid, seg_mid;
    uint32_t num_samples;
    prc_exact_geom_transform *exact_geom_trans = NULL;
    prc_trans_3d *transform = NULL;
    prc_curve_sampling_info sample_info;
    prc_surface_params surf_params = { 0 }; /* Wrap up the any surface params in this */
    int code;

    code = prc_get_curve_sample_info(ctx, &curve->ptr_curve, &sample_info);
    if (code < 0)
    {
        return code;
    }
    start = sample_info.start;
    end = sample_info.end;
    num_samples = sample_info.num_samples;
    curve_params = sample_info.curve_params;
    curve_eval_func = sample_info.curve_eval_func;

    if (curve->is_trimmed)
    {
        if (curve->trim_interval.min_value > start)
        {
            start = curve->trim_interval.min_value;
        }
        if (curve->trim_interval.max_value < end)
        {
            end = curve->trim_interval.max_value;
        }
    }

    switch (curve->ptr_curve.curve_type)
    {
    case PRC_TYPE_CRV_NURBS:
    {
        prc_crv_nurbs *nurbs = curve->ptr_curve.crv_nurbs;

        break;
    }

    case PRC_TYPE_CRV_Parabola:
    {
        prc_crv_parabola *parabola = curve->ptr_curve.crv_parabola;
        prc_parameterization params = parabola->parameterization;

        exact_geom_trans = &parabola->exact_geom_transform;
        transform = &parabola->transform;
        break;
    }

    case PRC_TYPE_CRV_Line:
    {
        prc_crv_line *line = curve->ptr_curve.crv_line;
        prc_parameterization params = line->parameterization;

        exact_geom_trans = &line->exact_geom_transform;
        transform = &line->transform;
        break;
    }

    case PRC_TYPE_CRV_Hyperbola:
    {
        prc_crv_hyperbola *hyperbola = curve->ptr_curve.crv_hyperbola;
        prc_parameterization params = hyperbola->parameterization;

        exact_geom_trans = &hyperbola->exact_geom_transform;
        transform = &hyperbola->transform;
        break;
    }

    case PRC_TYPE_CRV_Circle:
    {
        prc_crv_circle *circle = curve->ptr_curve.crv_circle;
        prc_parameterization params = circle->parameterization;

        exact_geom_trans = &circle->exact_geom_transform;
        transform = &circle->transform;
        break;
    }

    case PRC_TYPE_CRV_Ellipse:
    {
        prc_crv_ellipse *ellipse = curve->ptr_curve.crv_ellipse;
        prc_parameterization params = ellipse->parameterization;

        exact_geom_trans = &ellipse->exact_geom_transform;
        transform = &ellipse->transform;
        break;
    }

    case PRC_TYPE_CRV_Helix01:
    {
        prc_crv_helix01 *helix = curve->ptr_curve.crv_helix01;
        prc_parameterization params = helix->parameterization;

        exact_geom_trans = &helix->exact_geom_transform;
        transform = &helix->transform;
        break;
    }

    /* A series of straight line segments */
    case PRC_TYPE_CRV_PolyLine:
    {
        prc_crv_polyline *polyline = curve->ptr_curve.crv_polyline;

        exact_geom_trans = &polyline->exact_geom_transform;
        transform = &polyline->transform;
        break;
    }

    case PRC_TYPE_CRV_Offset:
    {
        prc_crv_offset *offset = curve->ptr_curve.crv_offset;

        /* Set the base one in the params so it can be used in
           prc_evaluate_offset_curve */
        offset->base_func = sample_info.curve_eval_func;
        offset->base_params = sample_info.curve_params;
        curve_eval_func = prc_evaluate_offset_curve;
        curve_params = (void *)offset;
        break;
    }

    case PRC_TYPE_CRV_OnSurf:
    {
        prc_crv_onsurf *onsurf = curve->ptr_curve.crv_onsurf;

        exact_geom_trans = &onsurf->exact_geom_transform;
        transform = &onsurf->transform;
        /* Wrap these parameters up */
        surf_params.surface_params = onsurf->base_surface_params;
        onsurf->base_surface_params = &surf_params.surface_params;
        break;
    }

    case PRC_TYPE_CRV_Composite:
    {
        prc_crv_composite *composite = curve->ptr_curve.crv_composite;
        uint32_t num_sub_curves = composite->number_of_subcurves;
        uint32_t k;

        /* Get the needed data to evaluate each of the subcurves and determine
           how many samples each needs to achieve the same curvature tolerance. */
        for (k = 0; k < num_sub_curves; k++)
        {
            prc_composite_subcurve *sub_curve = &composite->subcurves[k];
            prc_curve_sampling_info sub_curve_info;
            curve_func subcurve_eval_func;
            void *subcurve_params;
            uint32_t subcurve_num_samples;
            uint8_t subcurve_approx_good = 0;
            uint32_t j;

            memset(&sub_curve_info, 0, sizeof(sub_curve_info));
            code = prc_get_curve_sample_info(ctx, &sub_curve->ptr_curve, &sub_curve_info);
            if (code < 0)
            {
                return code;
            }

            subcurve_eval_func = sub_curve_info.curve_eval_func;
            subcurve_params = sub_curve_info.curve_params;
            subcurve_num_samples = sub_curve_info.num_samples;

            switch (sub_curve->ptr_curve.curve_type)
            {
            case PRC_TYPE_CRV_Line:
                subcurve_num_samples = 2;
                break;

            case PRC_TYPE_CRV_PolyLine:
                subcurve_num_samples = (uint32_t)((prc_crv_polyline *)sub_curve->ptr_curve.crv_polyline)->number_of_points;
                if (subcurve_num_samples < 2)
                    subcurve_num_samples = 2;
                break;

            default:
                if (subcurve_num_samples == 0)
                    subcurve_num_samples = 2;

                while (!subcurve_approx_good)
                {
                    subcurve_approx_good = 1;
                    for (j = 0; j < subcurve_num_samples - 1; j++)
                    {
                        double t0 = sub_curve_info.start +
                            (sub_curve_info.end - sub_curve_info.start) *
                            ((double)j / (double)(subcurve_num_samples - 1));
                        double t1 = sub_curve_info.start +
                            (sub_curve_info.end - sub_curve_info.start) *
                            ((double)(j + 1) / (double)(subcurve_num_samples - 1));
                        prc_vec3 p0 = subcurve_eval_func(ctx, subcurve_params, t0);
                        prc_vec3 p1 = subcurve_eval_func(ctx, subcurve_params, t1);
                        prc_vec3 mid = subcurve_eval_func(ctx, subcurve_params, (t0 + t1) / 2.0);
                        prc_vec3 seg_mid = { 0.0, 0.0, 0.0 };
                        double dist;

                        seg_mid.x = (p0.x + p1.x) / 2.0;
                        seg_mid.y = (p0.y + p1.y) / 2.0;
                        seg_mid.z = (p0.z + p1.z) / 2.0;

                        dist = sqrt((mid.x - seg_mid.x) * (mid.x - seg_mid.x) +
                            (mid.y - seg_mid.y) * (mid.y - seg_mid.y) +
                            (mid.z - seg_mid.z) * (mid.z - seg_mid.z));
                        if (dist > CURVE_PRECISION)
                        {
                            subcurve_approx_good = 0;
                            break;
                        }
                    }
                    if (!subcurve_approx_good)
                    {
                        subcurve_num_samples *= 2;
                    }
                }
                break;
            }

            sub_curve->base_func = sub_curve_info.curve_eval_func;
            sub_curve->base_params = sub_curve_info.curve_params;
            sub_curve->num_samples = subcurve_num_samples;
        }

        /* Use the aggregate per-subcurve sample budget for the composite's global sample count. */
        num_samples = 0;
        for (k = 0; k < num_sub_curves; k++)
        {
            num_samples += composite->subcurves[k].num_samples;
        }
        if (num_samples == 0)
            num_samples = 1;
        curve_approx_good = 1;
        break;
    }

    default:
        prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid curve type in prc_sample_curve\n");
        return PRC_ERROR_INTERNAL;
    }

    if (exact_geom_trans != NULL && transform != NULL)
    {
        code = prc_exact_geom_set_transform(ctx, exact_geom_trans, transform);
        if (code < 0)
        {
            prc_error(ctx, code, "Error in prc_exact_geom_set_transform\n");
            return code;
        }
    }

    /* Create a set of samples across the range, evaluate the curve
     * then evaluate at the midpoint of each segment and see if the distance is within tolerance.
     * If not, subdivide the segment and repeat until we have a good set of samples.
     * Composite curves are piecewise-defined; the midpoint test is not valid across subcurve joins,
     * so their sample count is based on the sum of the subcurve sample counts instead of global
     * recursive subdivision. */

    if (curve_eval_func == NULL)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid curve evaluation function in prc_sample_curve\n");
        return PRC_ERROR_INTERNAL;
    }

    if (curve->ptr_curve.curve_type == PRC_TYPE_CRV_Composite)
    {
        curve_approx_good = 1;
    }
    else
    {
        while (!curve_approx_good)
        {
            curve_approx_good = 1;

            for (i = 0; i < num_samples - 1; i++)
            {
                t0 = start + (end - start) * ((double)i / (double)(num_samples - 1));
                t1 = start + (end - start) * ((double)(i + 1) / (double)(num_samples - 1));
                p0 = curve_eval_func(ctx, curve_params, t0);
                p1 = curve_eval_func(ctx, curve_params, t1);
                mid = curve_eval_func(ctx, curve_params, (t0 + t1) / 2.0);

                /* Evaluate the midpoint of the segment */
                seg_mid.x = (p0.x + p1.x) / 2.0;
                seg_mid.y = (p0.y + p1.y) / 2.0;
                seg_mid.z = (p0.z + p1.z) / 2.0;

                /* Calculate the distance from the midpoint to the curve */
                dist = sqrt((mid.x - seg_mid.x) * (mid.x - seg_mid.x) +
                    (mid.y - seg_mid.y) * (mid.y - seg_mid.y) +
                    (mid.z - seg_mid.z) * (mid.z - seg_mid.z));
                if (dist > CURVE_PRECISION)
                {
                    curve_approx_good = 0;
                    break;
                }
            }
            if (!curve_approx_good)
            {
                num_samples *= 2;
            }
        }
    }

    /* We now have a sufficient precision on the curve. Lets generate the
       XYZ sample points and store them */
    wire_data->number_of_points = num_samples;
    wire_data->points = (prc_vec3 *)prc_calloc(ctx, num_samples, sizeof(prc_vec3));
    if (wire_data->points == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Allocation failure of wire_data points in prc_sample_curve\n");
        return PRC_ERROR_MEMORY;
    }

    if (curve->ptr_curve.curve_type == PRC_TYPE_CRV_Composite)
    {
        prc_crv_composite *composite = curve->ptr_curve.crv_composite;
        uint32_t point_index = 0;

        for (i = 0; i < composite->number_of_subcurves; i++)
        {
            prc_composite_subcurve *sub_curve = &composite->subcurves[i];
            uint32_t j;
            uint32_t sub_count = sub_curve->num_samples;

            if (sub_count == 0)
                sub_count = 2;

            for (j = 0; j < sub_count; j++)
            {
                double sub_t = (sub_count > 1) ?
                    ((double)j / (double)(sub_count - 1)) : 0.0;
                double composite_t = (double)i + sub_t;

                wire_data->points[point_index++] = curve_eval_func(ctx, curve_params, composite_t);
            }
        }
    }
    else
    {
        for (i = 0; i < num_samples; i++)
        {
            t = start + (end - start) * ((double)i / (double)(num_samples - 1));
            wire_data->points[i] = curve_eval_func(ctx, curve_params, t);
        }
    }

#if 0
    /* Lets print the points for a sanity check */
    for (uint32_t i = 0; i < num_samples; i++)
    {
        prc_vec3 p = data->exact_geom_tess[geom_count].wire_data->points[i];
        printf("prc_sample_curve: point[%u] = (%f, %f, %f)\n", i, p.x, p.y, p.z);
    }
#endif

    return 0;
}

/* Get a base curve eval function and params and limits */
int
prc_get_curve_eval_func(prc_context *ctx, prc_ptr_curve *curve,
    curve_func *eval_func, void **params, double *min_u,
    double *max_u)
{
    prc_parameterization params_interval;
    prc_exact_geom_transform *exact_geom_trans = NULL;
    prc_trans_3d *transform = NULL;
    int code;

    memset(&params_interval, 0, sizeof(prc_parameterization));

    switch (curve->curve_type)
    {
    case PRC_TYPE_CRV_Parabola:
        *eval_func = prc_evaluate_parabola;
        *params = (void *)curve->crv_parabola;
        params_interval = curve->crv_parabola->parameterization;
        if (curve->crv_parabola->has_transform)
        {
            exact_geom_trans = &curve->crv_parabola->exact_geom_transform;
            transform = &curve->crv_parabola->transform;
        }
        break;
    case PRC_TYPE_CRV_Line:
        *eval_func = prc_evaluate_line;
        *params = (void *)curve->crv_line;
        params_interval = curve->crv_line->parameterization;
        if (curve->crv_line->has_transform)
        {
            exact_geom_trans = &curve->crv_line->exact_geom_transform;
            transform = &curve->crv_line->transform;
        }
        break;
    case PRC_TYPE_CRV_Hyperbola:
        *eval_func = prc_evaluate_hyperbola;
        *params = (void *)curve->crv_hyperbola;
        params_interval = curve->crv_hyperbola->parameterization;
        if (curve->crv_hyperbola->has_transform)
        {
            exact_geom_trans = &curve->crv_hyperbola->exact_geom_transform;
            transform = &curve->crv_hyperbola->transform;
        }
        break;
    case PRC_TYPE_CRV_Circle:
        *eval_func = prc_evaluate_circle;
        *params = (void *)curve->crv_circle;
        params_interval = curve->crv_circle->parameterization;
        if (curve->crv_circle->has_transform)
        {
            exact_geom_trans = &curve->crv_circle->exact_geom_transform;
            transform = &curve->crv_circle->transform;
        }
        break;
    case PRC_TYPE_CRV_Ellipse:
        *eval_func = prc_evaluate_ellipse;
        *params = (void *)curve->crv_ellipse;
        params_interval = curve->crv_ellipse->parameterization;
        if (curve->crv_ellipse->has_transform)
        {
            exact_geom_trans = &curve->crv_ellipse->exact_geom_transform;
            transform = &curve->crv_ellipse->transform;
        }
        break;
    case PRC_TYPE_CRV_Helix01:
        *eval_func = prc_evaluate_helix;
        *params = (void *)curve->crv_helix01;
        params_interval = curve->crv_helix01->parameterization;
        if (curve->crv_helix01->has_transform)
        {
            exact_geom_trans = &curve->crv_helix01->exact_geom_transform;
            transform = &curve->crv_helix01->transform;
        }
        break;
    case PRC_TYPE_CRV_PolyLine:
        *eval_func = prc_evaluate_polyline;
        *params = (void *)curve->crv_polyline;
        params_interval = curve->crv_polyline->parameterization;
        if (curve->crv_polyline->has_transform)
        {
            exact_geom_trans = &curve->crv_polyline->exact_geom_transform;
            transform = &curve->crv_polyline->transform;
        }
        break;
    case PRC_TYPE_CRV_NURBS:
        /* prc_crv_nurbs_s has no transform fields and no parameterization interval;
           the valid domain comes directly from the clamped knot vector */
        *eval_func = prc_evaluate_crv_nurbs;
        *params = (void *)curve->crv_nurbs;
        *min_u = curve->crv_nurbs->u[curve->crv_nurbs->d];
        *max_u = curve->crv_nurbs->u[curve->crv_nurbs->highest_index_of_knots - curve->crv_nurbs->d];
        return 0;
    case PRC_TYPE_CRV_Offset:
    {
        prc_crv_offset *offset = curve->crv_offset;
        prc_ptr_curve base_curve = offset->base_curve;
        curve_func base_eval_func = NULL;

        if (base_curve.curve_type == PRC_TYPE_CRV_Offset)
            return PRC_ERROR_INTERNAL;

        code = prc_get_curve_eval_func(ctx, &base_curve, &base_eval_func,
            &offset->base_params, min_u, max_u);
        if (code < 0)
            return code;

        offset->base_func = (void *)base_eval_func;
        *eval_func = prc_evaluate_offset_curve;
        *params = (void *)offset;
        *min_u = offset->parameterization.interval.min_value;
        *max_u = offset->parameterization.interval.max_value;
        return 0;
    }
    default:
        return PRC_ERROR_INTERNAL;
    }

    /* We should probably pull this out of the evaluation TODO */
    if (exact_geom_trans != NULL && transform != NULL)
    {
        code = prc_exact_geom_set_transform(ctx, exact_geom_trans, transform);
        if (code < 0)
        {
            prc_error(ctx, code, "Error in prc_exact_geom_set_transform\n");
            return code;
        }
    }
    *min_u = params_interval.interval.min_value;
    *max_u = params_interval.interval.max_value;
    return 0;
}