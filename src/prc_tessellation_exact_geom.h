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

#ifndef PRC_TESSELLATION_EXACT_GEOMETRY_H
#define PRC_TESSELLATION_EXACT_GEOMETRY_H

#include "prc_data.h"
#include "prc_parse_common.h"
#include "prc_diag_env.h"

typedef enum
{
    PRC_NOT_ISO_TORUS_CIRCLE,
    PRC_MINOR_ISO_TORUS_CIRCLE,
    PRC_MAJOR_ISO_TORUS_CIRCLE
} prc_iso_torus_circle_t;

typedef struct prc_iso_torus_circle_data_s
{
    prc_iso_torus_circle_t type;
    prc_vec3 center;
    prc_vec3 normal;
} prc_iso_torus_circle;


/* One tessellation of a surface's full natural uv domain on a regular,
   curvature-adaptive grid (same refinement prc_tessellate_surface's
   untrimmed path already used) -- shared by the untrimmed path and by
   prc_tessellate_trimmed_face, which keeps whichever cells fall inside a
   face's trim loop(s) and only needs to ear-clip-fill the thin leftover
   fringe. Caller owns and must free grid->uv and grid->verts */
typedef struct prc_regular_grid_s
{
    prc_vec2 *uv;
    prc_exact_geom_vertex *verts;
    uint32_t vertex_samples_u;
    uint32_t vertex_samples_v;
    uint8_t wrap_u;
    uint8_t wrap_v;
} prc_regular_grid;

/* An undirected pair of grid vertex indices, used to represent one boundary
   edge while tracing the outline of a classified grid's kept cells */
typedef struct prc_uint32_pair_s
{
    uint32_t a;
    uint32_t b;
} prc_uint32_pair;

#define PRC_DEBUG_EARCLIP 1
#define CHECK_SURFACE_PROJECTION 1

#define ANA_CURVE_SAMPLE 4

#define CURVE_SAMPLES 32
#define SURFACE_SAMPLES 32
/* Temporarily loosened for debugging (was 1e-3) to get a coarser boundary
   loop mesh while diagnosing the periodic-loop trim work -- restore before
   shipping */
#define CURVE_PRECISION 1e-1
#define SURFACE_PRECISION 1e-4
#define SURFACE_MAX_SAMPLES 1024
#define CYLINDER_SURFACE_PRECISION 1e-2
#define CYLINDER_MAX_SAMPLES 128
#define CONE_SURFACE_PRECISION 1e-2
#define CONE_MAX_SAMPLES 128
#define SPHERE_SURFACE_PRECISION 1e-2
#define SPHERE_MAX_SAMPLES 128
#define TORUS_SURFACE_PRECISION 1e-2
#define TORUS_MAX_SAMPLES 128
#define CYLINDRICAL_SURFACE_PRECISION 1e-2
#define CYLINDRICAL_MAX_SAMPLES 128
#define EXTRUSION_SURFACE_PRECISION 1e-1
#define EXTRUSION_MAX_SAMPLES 128
#define REVOLUTION_SURFACE_PRECISION 1e-1
#define REVOLUTION_MAX_SAMPLES 128
#define PLANE_MAX_SAMPLES 4
#define PLANE_SURFACE_PRECISION 1e-2
#define NURBS_SURFACE_PRECISION 1e-4
#define NURBS_MAX_SAMPLES 1024
   /* Largest B-spline degree the fixed-size basis function buffers below can hold */
#define PRC_BSPLINE_MAX_DEGREE 15
/* Safety cap on prc_subdivide_curved_triangle's recursion (3^depth leaf
   triangles worst case per boundary ear-clip triangle). Kept low for now
   while debugging -- raise once the trim shape itself is confirmed correct */
#define PRC_CURVED_LOOP_SUBDIVIDE_MAX_DEPTH 3
   /* Safety cap on prc_subdivide_trim_boundary_triangle's recursion. In
      practice only the one branch of the fan-split that keeps straddling the
      trim curve recurses this deep (siblings resolve to all-in/all-out after
      a level or two), so cost is roughly linear in boundary cell count, not
      3^depth */
#define PRC_TRIM_BOUNDARY_SUBDIVIDE_MAX_DEPTH 7

/* A standard type for curve sampling */
typedef prc_vec3(*curve_func)(prc_context *ctx, void *params, double input);

/* And for surfaces */
typedef prc_vec3(*surface_func)(prc_context *ctx, void *params, double u, double v);

/* Something to hold loop samples that should be sufficiently fine to use
   in the tessellation of a surface. These will be boundary points that should
   be included in the tessellation of the surface */
typedef struct prc_loop_samples_s
{
    uint32_t num_samples;
    prc_vec3 *samples;
    prc_vec2 *uv_samples;
    uint8_t is_outer_loop;
    /* Full 2*pi turns this loop winds around each periodic uv axis before
       closing back up (0 if it is a genuine simple polygon within one tile).
       Set by prc_map_loops_to_surface, consulted by prc_tessellate_surface
       to pick between the closed-loop and seam-cut periodic tessellation
       paths */
    int32_t wind_u;
    int32_t wind_v;
    /* Set by prc_sample_loop when every sample coincides (e.g. a loop
       trimmed down to a cone's apex): such a loop bounds zero area and must
       not be fed to the hole/ear-clip machinery or an angular (atan2-based)
       uv mapping, both of which are numerically unstable or meaningless for
       a degenerate single point */
    uint8_t is_point_loop;
} prc_loop_samples;

typedef struct prc_coedge_samples_s
{
    uint32_t num_samples;
    prc_vec3 *samples;
} prc_coedge_samples;

typedef struct prc_surface_sampling_info_s
{
    uint8_t u_periodic;
    uint8_t v_periodic;
    uint8_t u_linear;
    uint8_t v_linear;
    double u_period;
    double v_period;

    double start_u;
    double end_u;
    double start_v;
    double end_v;
    uint32_t max_samples_u;
    uint32_t max_samples_v;

    double precision_u;
    double precision_v;

    uint32_t num_samples_u;
    uint32_t num_samples_v;
} prc_surface_sampling_info;

typedef struct prc_curve_sampling_info_s
{
    double start;
    double end;
    uint32_t num_samples;
    void *curve_params;
    curve_func curve_eval_func;
} prc_curve_sampling_info;

typedef struct prc_surface_params_s
{
    void *surface_params;
    uint32_t num_loops;
    prc_loop_samples *loop_samples;
} prc_surface_params;

/* Base prototypes */
int prc_get_surface_data(prc_context *ctx, prc_type_surf *surface, prc_surface_sampling_info *sampling_info);

/* Core prototypes */
int prc_tessellate_surface(prc_context *ctx, prc_data *data, uint32_t shell_index, uint32_t face_index, prc_topo_face *topo_face, uint8_t orientation, prc_nano_brep_ref_data *brep_ref_data, prc_topo_context *topo_context);

/* Surfaces prototypes */
prc_vec3 prc_evaluate_surf_torus(prc_context *ctx, void *params, double u, double v);
prc_vec3 prc_evaluate_surf_revolution(prc_context *ctx, void *params, double u, double v);
prc_vec3 prc_evaluate_surf_extrusion(prc_context *ctx, void *params, double u, double v);
prc_vec3 prc_evaluate_surf_nurbs(prc_context *ctx, void *params, double u, double v);
prc_vec3 prc_evaluate_surf_plane(prc_context *ctx, void *params, double u, double v);
prc_vec3 prc_evaluate_surf_cylinder(prc_context *ctx, void *params, double u, double v);
prc_vec3 prc_evaluate_surf_sphere(prc_context *ctx, void *params, double u, double v);
prc_vec3 prc_evaluate_surf_cone(prc_context *ctx, void *params, double u, double v);
prc_vec3 prc_evaluate_surf_fromcurves(prc_context *ctx, void *params, double u, double v);
prc_vec3 prc_evaluate_surf_offset(prc_context *ctx, void *params, double u, double v);
prc_vec3 prc_evaluate_surf_transform(prc_context *ctx, void *params, double u, double v);
prc_vec3 prc_evaluate_surf_cylindrical(prc_context *ctx, void *params, double u, double v);
prc_vec3 prc_evaluate_surf_blend02(prc_context *ctx, void *params, double u, double v);
prc_vec3 prc_evaluate_surf_blend01(prc_context *ctx, void *params, double u, double v);
prc_vec3 prc_evaluate_surf_blend03(prc_context *ctx, void *params, double u, double v);
prc_vec3 prc_evaluate_surf_ruled(prc_context *ctx, void *params, double u, double v);
int prc_get_surface_eval_func(prc_context *ctx, prc_type_surf *surface, surface_func *eval_func, void **params);
int prc_compute_surface_normal(prc_context *ctx, surface_func surface_eval_func, void *surface_params, double u, double v, double du, double dv, double start_u, double end_u, double start_v, double end_v, const prc_surface_sampling_info *sampling_info, uint8_t orientation, prc_vec3 *normal);

/* Curve prototypes */
int prc_get_curve_sample_info(prc_context *ctx, prc_ptr_curve *ptr_curve, prc_curve_sampling_info *sample_info);
int prc_sample_compressed_curve(prc_context *ctx, prc_data *data, prc_compressed_curve *curve, double curve_tolerance, prc_exact_geom_wire_data *wire_data, prc_nano_brep_compressed_data *compressed_data);
int prc_sample_compressed_curve_store_in_body(prc_context *ctx, prc_data *data, uint32_t shell_index, uint32_t face_index, prc_compressed_curve *curve, double curve_tolerance, prc_nano_brep_compressed_data *compressed_data);
int prc_sample_curve(prc_context *ctx, prc_content_wire_edge *curve, prc_exact_geom_wire_data *wire_data);
int prc_get_curve_eval_func(prc_context *ctx, prc_ptr_curve *curve, curve_func *eval_func, void **params, double *min_u, double *max_u);
int prc_get_hcg_circle_data(prc_context *ctx, prc_hcg_circle *hcg_circle, prc_hcg_circle_information *info, prc_nano_brep_compressed_data *compressed_data);
int prc_get_hcg_line_data(prc_context *ctx, prc_hcg_line *data, prc_vec3 *start, prc_vec3 *end, prc_nano_brep_compressed_data *compressed_data);
int prc_get_compressed_curve_sample_info(prc_context *ctx, prc_compressed_curve *curve, prc_curve_sampling_info *sample_info);

/* Ana-Iso prototypes */
int prc_tessellate_compressed_face(prc_context *ctx, prc_data *data, uint32_t shell_index, uint32_t face_index, prc_compressed_face *topo_face, prc_nano_brep_compressed_data *compressed_data);

/* Refs prototypes */
void prc_get_compressed_point(prc_context *ctx, prc_compressed_point *comp_point, prc_vec3 *vertex);
int prc_get_ptr_vertex(prc_context *ctx, prc_nano_brep_ref_data *brep_ref_data, prc_ptr_topology *ptr_topo_in, prc_vec3 *vertex);
int prc_get_compressed_curve(prc_context *ctx, prc_ref_or_compressed_curve *ref_or_comp_curve, prc_compressed_curve **comp_curve, prc_nano_brep_compressed_data *compressed_data);
int prc_get_compressed_vertex(prc_context *ctx, prc_compressed_vertex *comp_vertex, prc_vec3 *vertex, prc_nano_brep_compressed_data *compressed_data);
int prc_get_start_end_data(prc_context *ctx, prc_start_end_data *data, prc_vec3 *start, prc_vec3 *end, prc_nano_brep_compressed_data *compressed_data);

/* Utility prototypes */
prc_vec3 prc_project_point_onto_surface(prc_context *ctx, surface_func eval_func, void *surface_params, double min_u, double max_u, double min_v, double max_v, prc_vec3 point, double *out_u, double *out_v);
prc_vec3 prc_refine_point_on_surface(prc_context *ctx, surface_func eval_func, void *surface_params, double min_u, double max_u, double min_v, double max_v, prc_vec3 point, double u0, double v0, double *out_u, double *out_v);
prc_vec3 prc_project_point_onto_curve(prc_context *ctx, curve_func eval_func, void *curve_params, double min_u, double max_u, prc_vec3 point);
uint32_t prc_bspline_find_span(uint32_t degree, uint32_t num_ctrl_pts, double u, const double *knots);
prc_vec3 prc_project_point_onto_line(prc_context *ctx, prc_vec3 point, prc_vec3 origin, prc_vec3 vector);
void prc_get_curve_periodicity(prc_context *ctx, prc_ptr_curve *curve, uint8_t *is_periodic, double *period);
prc_vec3 prc_exact_geom_apply_transform(prc_context *ctx, const prc_exact_geom_transform *exact_geom_trans, prc_vec3 point);
int prc_exact_geom_set_transform(prc_context *ctx, prc_exact_geom_transform *exact_geom_trans, const prc_trans_3d *prc_trans);
int prc_invert_exact_transform(prc_context *ctx, prc_exact_geom_transform *transform_in, prc_exact_geom_transform *inverse_transform_out);
int prc_apply_math_transform(prc_context *ctx, prc_math_fct_3d *function, prc_vec3 base_point, prc_vec3 *output);
int prc_apply_nonlinear_math_transform(prc_context *ctx, prc_math_fct_3d_nonlinear *nonlinear, prc_vec3 base_point, prc_vec3 *output);
int prc_apply_linear_math_transform(prc_context *ctx, prc_math_fct_3d_linear *linear, prc_vec3 base_point, prc_vec3 *output);
void prc_bspline_basis_funs(uint32_t span, double u, uint32_t degree, const double *knots, double *N);

#endif
