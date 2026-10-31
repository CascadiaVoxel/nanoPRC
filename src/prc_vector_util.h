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

#ifndef PRC_VECTOR_UTIL_H
#define PRC_VECTOR_UTIL_H

#define PRC_VEC_ERROR -1

typedef struct prc_vec3_s prc_vec3;
typedef struct prc_vec2_s prc_vec2;


/* Table 24 */
struct prc_vec2_s
{
    double x;
    double y;
};

/* Table 25 */
struct prc_vec3_s
{
    double x;
    double y;
    double z;
};

typedef enum {
    TRIANGLE_HAS_NO_NEIGHBORS = 0,
    TRIANGLE_HAS_RIGHT_NEIGHBOR,
    TRIANGLE_HAS_LEFT_NEIGHBOR,
    TRIANGLE_HAS_LEFTRIGHT_NEIGHBOR
} prc_triangle_neighbors_t;

typedef struct prc_basis_s prc_basis;
struct prc_basis_s
{
    prc_vec3 X;
    prc_vec3 Y;
    prc_vec3 Z;
};

/* A special structure for dealing with compressed triangles */
typedef struct prc_compress_triangle_s prc_compress_triangle;
struct prc_compress_triangle_s
{
    prc_vec3 V0;
    prc_vec3 V1;
    prc_vec3 V2;

    prc_vec3 normal;

    prc_triangle_neighbors_t neighbors;
    uint8_t v1v2_is_left;

    prc_compress_triangle *left;
    prc_compress_triangle *right;
    prc_compress_triangle *parent;

    uint8_t is_initialized;
};

int prc_vec_make_orth_basis_normals(prc_basis *basis);
int prc_vec_make_orth_basis(prc_basis *basis);
int prc_vec_compute_basis_origin(prc_vec3 v0, prc_vec3 v1, prc_vec3 v3, prc_basis *basis, prc_vec3 *origin);
void prc_vec_apply_basis_origin(prc_basis basis, prc_vec3 origin, prc_vec3 input_point, prc_vec3 *output_point);
void prc_vec_set(prc_vec3 *vec, double x, double y, double z);
void prc_vec_avg(prc_vec3 vec1, prc_vec3 vec2, prc_vec3 *output);
void prc_vec_avg3_test(prc_vec3 vec1, prc_vec3 vec2, prc_vec3 vec3, prc_vec3 *output);
void prc_vec_avg3(prc_vec3 vec1, prc_vec3 vec2, prc_vec3 vec3, prc_vec3 *output);
void prc_vec_swap(prc_vec3 *vec1, prc_vec3 *vec2);
void prc_vec_sub(prc_vec3 vec1, prc_vec3 vec2, prc_vec3 *output);
void prc_vec_add(prc_vec3 vec1, prc_vec3 vec2, prc_vec3 *output);
int prc_vec_compute_normal_vector(prc_vec3 v0, prc_vec3 v1, prc_vec3 v2, double theta, double phi, prc_vec3 *normal);
int prc_vec_normalize(prc_vec3 *vec);
int prc_vec_angle_between_vectors(prc_vec3 vec1, prc_vec3 vec2, double *angle);
int prc_vec_vectors_are_equal(prc_vec3 vec1, prc_vec3 vec2, double epsilon);
double prc_vec_length(prc_vec3 vec);
void prc_vec_cross(prc_vec3 vec1, prc_vec3 vec2, prc_vec3 *output);
double prc_vec_dot_product(prc_vec3 vec1, prc_vec3 vec2);
void prc_vec_copy(prc_vec3 vec_in, prc_vec3 *vec_out, uint8_t negate);
void prc_vec_negate(prc_vec3 *vec);
void prc_vec_scale(double scale, prc_vec3 *vec);
double prc_vec_area_of_triangle(prc_vec3 pt1, prc_vec3 pt2, prc_vec3 pt3);
void prc_vec_compute_find_normal_from_sides(prc_vec3 vec1, prc_vec3 vec2, prc_vec3 vec3, prc_vec3 *normal);
void prc_vec_set_triangle(prc_vec3 v0, prc_vec3 v1, prc_vec3 v2, prc_vec3 normal, uint8_t edge,
    prc_compress_triangle *triangle, double norm_test, uint8_t null_children);
double prc_vec_dist_between_two_points(prc_vec3 pt1, prc_vec3 pt2);
int prc_vec_angle_between_vectors_normal(prc_vec3 vec1, prc_vec3 vec2, double *angle);

#endif
