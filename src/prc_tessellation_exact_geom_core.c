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

/* Segment of exact tessellation work that is the core part of computing
   the surface tessellations */

#include "prc_tessellation_exact_geom.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

/* One node of the circular doubly-linked-list polygon used for ear clipping.
      uv is a plain surface parameter (u, v), not a screen coordinate */
typedef struct prc_tri_vertex_s
{
    prc_vec2 uv;
    uint32_t next;
    uint32_t prev;
    uint8_t removed;
} prc_tri_vertex;

static double
prc_uv_loop_max_x(const prc_loop_samples *loop)
{
    uint32_t count = (loop->num_samples > 0) ? (loop->num_samples - 1) : 0;
    double max_x;
    uint32_t i;

    if (count == 0)
        return 0.0;

    max_x = loop->uv_samples[0].x;
    for (i = 1; i < count; i++)
    {
        if (loop->uv_samples[i].x > max_x)
            max_x = loop->uv_samples[i].x;
    }

    return max_x;
}

/* Twice the signed area (shoelace formula): positive for CCW, negative for CW */
static double
prc_uv_polygon_signed_area(uint32_t count, const prc_vec2 *pts)
{
    double area = 0.0;
    uint32_t i;

    for (i = 0; i < count; i++)
    {
        uint32_t next = (i + 1 == count) ? 0 : (i + 1);

        area += pts[i].x * pts[next].y - pts[next].x * pts[i].y;
    }

    return area;
}


static int
prc_sample_coedge(prc_context *ctx, prc_nano_brep_ref_data *brep_ref_data,
    prc_coedge_in_loop *topo_coedge, prc_coedge_samples *coedge_samples)
{
    int code;

    /* This function samples a coedge and stores the results in coedge_samples */
    if (topo_coedge->next_coedge.topo->tag != PRC_TYPE_TOPO_CoEdge)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Error in prc_sample_coedge\n");
        return PRC_ERROR_INTERNAL;
    }
    else
    {
        prc_topo *topo;

        /* First get the loop that we need */
        if (!topo_coedge->next_coedge.topo->topo_coedge->ptr_topology.is_stored)
        {
            topo = topo_coedge->next_coedge.topo->topo_coedge->ptr_topology.topo;
        }
        else
        {
            if (topo_coedge->next_coedge.topo->topo_coedge->ptr_topology.topo_identifier > brep_ref_data->number_of_topo_refs ||
                topo_coedge->next_coedge.topo->topo_coedge->ptr_topology.topo_identifier == 0)
            {
                prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid topo identifier in prc_sample_coedge\n");
                return PRC_ERROR_INTERNAL;
            }
            /* These are biased I believe */
            topo = brep_ref_data->topo_refs[topo_coedge->next_coedge.topo->topo_coedge->ptr_topology.topo_identifier - 1];
        }

        /* PRC_TYPE_TOPO_SingleWireBodyCompress comes when we are dealing with
           compressed bodies and we are trying to use the same code to tessellate
           as we use for the compressed case */
        if (topo->tag == PRC_TYPE_TOPO_Edge ||
            topo->tag == PRC_TYPE_TOPO_SingleWireBodyCompress ||
            topo->tag == PRC_TYPE_TOPO_WireEdge)
        {
            /* Now we finally get to the curve. However, the curve may be
               defined by two vertices */
            prc_topo_coedge *actual_coedge = topo_coedge->next_coedge.topo->topo_coedge;
            prc_exact_geom_wire_data wire_samples = { 0 };

            /* We could have a PRC_TYPE_TOPO_WireEdge or a PRC_TYPE_TOPO_Edge */
            if (topo->tag == PRC_TYPE_TOPO_WireEdge)
            {
                prc_topo_wire_edge *wire_edge = topo->topo_wire_edge;

                code = prc_sample_curve(ctx, &wire_edge->curve, &wire_samples);
                if (code < 0)
                {
                    if (wire_samples.points != NULL)
                    {
                        prc_free(ctx, wire_samples.points);
                    }
                    prc_error(ctx, code, "Error in prc_sample_curve\n");
                    return code;
                }
            }
            else if (topo->tag == PRC_TYPE_TOPO_Edge)
            {
                /* This is a confusing object. Not sure how the start
                   and end vertex are used and there is a curve. For now
                   check the curve. If it is no data use the vertices */
                prc_topo_edge *topo_edge = topo->topo_edge;

                if (topo_edge->wire_edge.ptr_curve.curve_type != PRC_TYPE_CRV_NONE)
                {
                    code = prc_sample_curve(ctx, &topo_edge->wire_edge, &wire_samples);
                    if (code < 0)
                    {
                        if (wire_samples.points != NULL)
                        {
                            prc_free(ctx, wire_samples.points);
                        }
                        prc_error(ctx, code, "Error in prc_sample_curve\n");
                        return code;
                    }
                }
                else
                {
                    prc_vec3 vertex1, vertex2;
                    code = prc_get_ptr_vertex(ctx, brep_ref_data,
                        &topo_edge->start_vertex, &vertex1);
                    if (code < 0)
                    {
                        prc_error(ctx, code, "Error in prc_get_ptr_vertex\n");
                        return code;
                    }

                    code = prc_get_ptr_vertex(ctx, brep_ref_data,
                        &topo_edge->end_vertex, &vertex2);
                    if (code < 0)
                    {
                        prc_error(ctx, code, "Error in prc_get_ptr_vertex\n");
                        return code;
                    }

                    /* If vertex1 == vertex2, we only need one point.  This
                       happens if we are clipping at a cone apex for example */
                    if (vertex1.x == vertex2.x && vertex1.y == vertex2.y &&
                        vertex1.z == vertex2.z)
                    {
                        wire_samples.number_of_points = 1;
                    }
                    else
                    {
                        wire_samples.number_of_points = 2;
                    }

                    wire_samples.points = (prc_vec3 *)prc_calloc(ctx,
                        wire_samples.number_of_points, sizeof(prc_vec3));
                    if (wire_samples.points == NULL)
                    {
                        prc_error(ctx, PRC_ERROR_MEMORY, "Error in prc_sample_coedge\n");
                        return PRC_ERROR_MEMORY;
                    }
                    wire_samples.points[0] = vertex1;
                    if (wire_samples.number_of_points > 1)
                    {
                        wire_samples.points[1] = vertex2;
                    }
                }
            }
            else if (topo->tag == PRC_TYPE_TOPO_SingleWireBodyCompress)
            {
                /* This is a special case where we have an iso or ana surface */
                prc_compressed_curve *trim_curve;

                code = prc_get_compressed_curve(ctx,
                    &topo->topo_single_wire_compress->ref_or_compressed_curve,
                    &trim_curve, topo->topo_single_wire_compress->ref_data);
                if (code < 0)
                {
                    prc_error(ctx, code, "Failed to get trim curve by id in prc_get_compressed_curve\n");
                    return code;
                }

                /* This is the case where we are coming from the compressed surface */
                code = prc_sample_compressed_curve(ctx, NULL, trim_curve, 0,
                    &wire_samples, topo->topo_single_wire_compress->ref_data);
                if (code < 0)
                {
                    if (wire_samples.points != NULL)
                    {
                        prc_free(ctx, wire_samples.points);
                    }
                    prc_error(ctx, code, "Error in prc_sample_curve\n");
                    return code;
                }
            }
            else
            {
                prc_error(ctx, PRC_ERROR_INTERNAL, "Error in prc_sample_coedge\n");
                return PRC_ERROR_INTERNAL;
            }

            /* coedge_orientation == 0 means this coedge is traversed opposite
               to its underlying edge curve's own direction; prc_sample_curve
               always samples in the curve's native direction, so reverse here
               to match the loop's actual boundary traversal (confirmed by a
               real file: without this, consecutive coedges' samples don't
               connect end-to-start, producing bogus discontinuities) */
            if (!actual_coedge->coedge_orientation && wire_samples.number_of_points > 1)
            {
                uint32_t lo = 0, hi = wire_samples.number_of_points - 1;

                while (lo < hi)
                {
                    prc_vec3 tmp = wire_samples.points[lo];

                    wire_samples.points[lo] = wire_samples.points[hi];
                    wire_samples.points[hi] = tmp;
                    lo++;
                    hi--;
                }
            }

            /* Transfer the samples to the coedge structure */
            coedge_samples->num_samples = wire_samples.number_of_points;
            coedge_samples->samples = wire_samples.points;
        }
    }
    return 0;
}

static int
prc_sample_loop(prc_context *ctx, prc_nano_brep_ref_data *brep_ref_data,
    prc_topo_face *topo_face, prc_ptr_topology *loop,
    prc_loop_samples *loop_samples, uint8_t *loop_degenerate)
{
    /* Loops are made up of several co-edges which should be curves.
       Here we make our way through a sampling approximation of the loop */
    prc_topo_loop *topo_loop;
    uint32_t num_coedges;
    uint32_t k, j;
    int code;
    prc_coedge_samples *coedge_samples;
    uint32_t total_samples = 0;

    /* First get the loop that we need */
    if (!loop->is_stored)
    {
        topo_loop = loop->topo->topo_loop;
    }
    else
    {
        if (loop->topo_identifier > brep_ref_data->number_of_topo_refs ||
            loop->topo_identifier == 0)
        {
            prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid loop identifier in prc_sample_loop\n");
            return PRC_ERROR_INTERNAL;
        }
        /* These are biased I believe */
        topo_loop = brep_ref_data->topo_refs[loop->topo_identifier - 1]->topo_loop;
    }

    num_coedges = topo_loop->number_of_coedges;
    coedge_samples = (prc_coedge_samples *)prc_calloc(ctx, num_coedges, sizeof(prc_coedge_samples));
    if (coedge_samples == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Failed in allocation prc_sample_loop\n");
        return PRC_ERROR_MEMORY;
    }

    /* We have to sample each of the coedges and concatenate them together. */
    for (k = 0; k < num_coedges; k++)
    {
        /* This method does fix the direction/orientation of the coedge */
        code = prc_sample_coedge(ctx, brep_ref_data, &topo_loop->coedge[k], &coedge_samples[k]);
        if (code < 0)
        {
            /* Clean up any coedges that did work */
            for (j = 0; j <= k; j++)
            {
                if (coedge_samples[j].samples != NULL)
                {
                    prc_free(ctx, coedge_samples[j].samples);
                }
            }
            prc_free(ctx, coedge_samples);
            prc_error(ctx, code, "Failed in prc_sample_coedge\n");
            return code;
        }
        total_samples += coedge_samples[k].num_samples;
    }

    /* Check that we don't have a degenerate loop */
    prc_vec3 first_prev = coedge_samples[0].samples[0];
    prc_vec3 last_prev = coedge_samples[0].samples[coedge_samples[0].num_samples - 1];
    for (k = 1; k < num_coedges; k++)
    {
        prc_vec3 first = coedge_samples[k].samples[0];
        prc_vec3 last = coedge_samples[k].samples[coedge_samples[k].num_samples - 1];

        if (k < num_coedges - 1)
        {
            if (prc_vec_dist_between_two_points(first, last_prev) > LOOP_DEGENERACY_TEST)
            {
                *loop_degenerate = 1;
            }
            last_prev = last;
        }
        else
        {
            /* Last coedge */
            if (prc_vec_dist_between_two_points(first_prev, last) > LOOP_DEGENERACY_TEST)
            {
                *loop_degenerate = 1;
            }
        }
    }

#ifdef PRC_DEBUG_EARCLIP
    fprintf(stderr, "prc_sample_loop: num_coedges=%u\n", num_coedges);
    for (k = 0; k < num_coedges; k++)
    {
        prc_vec3 first = coedge_samples[k].samples[0];
        prc_vec3 last = coedge_samples[k].samples[coedge_samples[k].num_samples - 1];

        fprintf(stderr, "  coedge[%u]: num_samples=%u first=(%.9f,%.9f,%.9f) last=(%.9f,%.9f,%.9f)\n",
            k, coedge_samples[k].num_samples,
            first.x, first.y, first.z, last.x, last.y, last.z);
    }
#endif

    /* Now we have to combine the coedges into one loop */
    /* Each coedge begins with the end point of the previous coedge so
       we want to drop any of those points as we make our way around the loop.
       This includes the last point of the last coedge being the first point
       of the first coedge. We will maintain that point. */
       /* Each coedge has one redundant point. */
    loop_samples->num_samples = total_samples - num_coedges + 1;
    loop_samples->samples = (prc_vec3 *)prc_calloc(ctx, loop_samples->num_samples, sizeof(prc_vec3));
    if (loop_samples->samples != NULL)
    {
        uint32_t pos = 0;
        for (j = 0; j < num_coedges; j++)
        {
            /* Parens matter: this drops exactly the coedge's last (redundant)
               point, not one byte of it */
            memcpy(&loop_samples->samples[pos], coedge_samples[j].samples, sizeof(prc_vec3) * (coedge_samples[j].num_samples - 1));
            pos += (coedge_samples[j].num_samples - 1);
        }
        /* Duplicate the first point to complete the loop */
        loop_samples->samples[pos] = coedge_samples[0].samples[0];

        /* A loop trimmed down to a single point (e.g. a cone's apex) samples
           as every point coinciding with the first -- flag it so later
           stages can skip the angular uv mapping and hole/ear-clip bridging
           that only make sense for a loop enclosing real area */
        {
            uint32_t s;
            prc_vec3 first = loop_samples->samples[0];
            uint8_t all_coincide = 1;

            for (s = 1; s < loop_samples->num_samples; s++)
            {
                if (prc_vec_dist_between_two_points(first, loop_samples->samples[s]) > CURVE_PRECISION)
                {
                    all_coincide = 0;
                    break;
                }
            }
            loop_samples->is_point_loop = all_coincide;
        }
    }

    /* Free up the coedge samples */
    for (j = 0; j < num_coedges; j++)
    {
        if (coedge_samples[j].samples != NULL)
        {
            prc_free(ctx, coedge_samples[j].samples);
        }
    }
    prc_free(ctx, coedge_samples);

    return 0;
}

/* Stores count points (dropping any closing duplicate is the caller's job)
   into a circular ring starting at slot base, forcing the winding direction
   given by reverse */
static void
prc_tri_build_ring(prc_tri_vertex *verts, uint32_t base, uint32_t count,
    const prc_vec2 *pts, int reverse)
{
    uint32_t i;

    for (i = 0; i < count; i++)
    {
        uint32_t idx = base + i;

        verts[idx].uv = reverse ? pts[count - 1 - i] : pts[i];
        verts[idx].next = base + ((i + 1) % count);
        verts[idx].prev = base + ((i + count - 1) % count);
        verts[idx].removed = 0;
    }
}

static double
prc_tri_cross(prc_vec2 o, prc_vec2 a, prc_vec2 b)
{
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

static int
prc_tri_point_in_triangle(prc_vec2 p, prc_vec2 a, prc_vec2 b, prc_vec2 c)
{
    double d1 = (p.x - b.x) * (a.y - b.y) - (a.x - b.x) * (p.y - b.y);
    double d2 = (p.x - c.x) * (b.y - c.y) - (b.x - c.x) * (p.y - c.y);
    double d3 = (p.x - a.x) * (c.y - a.y) - (c.x - a.x) * (p.y - a.y);
    int has_neg = (d1 < 0.0) || (d2 < 0.0) || (d3 < 0.0);
    int has_pos = (d1 > 0.0) || (d2 > 0.0) || (d3 > 0.0);

    return !(has_neg && has_pos);
}

/* idx is an ear of a CCW ring if it turns convex and no other remaining
   vertex of the ring falls inside the candidate triangle */
static int
prc_tri_is_ear(const prc_tri_vertex *verts, uint32_t idx)
{
    uint32_t prev = verts[idx].prev;
    uint32_t next = verts[idx].next;
    prc_vec2 a = verts[prev].uv;
    prc_vec2 b = verts[idx].uv;
    prc_vec2 c = verts[next].uv;
    uint32_t k;

    if (prc_tri_cross(a, b, c) <= 0.0)
        return 0;

    for (k = verts[next].next; k != prev; k = verts[k].next)
    {
        prc_vec2 p = verts[k].uv;

        /* A bridged hole duplicates its entry/exit vertices elsewhere in the
           ring (see prc_tri_eliminate_hole); finding that duplicate here just
           means it coincides with one of this triangle's own corners, which
           is not a real obstruction */
        if ((p.x == a.x && p.y == a.y) || (p.x == b.x && p.y == b.y) || (p.x == c.x && p.y == c.y))
            continue;

        if (prc_tri_point_in_triangle(p, a, b, c))
            return 0;
    }

    return 1;
}

/* Define PRC_DEBUG_EARCLIP to dump the ring state (each vertex's uv, whether
   it is reflex, and whichever other vertex is blocking it as an ear) when
   ear clipping cannot make progress */
#ifdef PRC_DEBUG_EARCLIP
static void
prc_tri_debug_dump_ring(prc_context *ctx, const prc_tri_vertex *verts, uint32_t head, uint32_t ring_count)
{
    uint32_t cur = head;
    uint32_t i;

    (void)ctx;
    fprintf(stderr, "prc_tri_earclip: ring_count=%u\n", ring_count);
    for (i = 0; i < ring_count; i++)
    {
        uint32_t prev = verts[cur].prev;
        uint32_t next = verts[cur].next;
        double cross = prc_tri_cross(verts[prev].uv, verts[cur].uv, verts[next].uv);
        uint32_t blocker = (uint32_t)-1;
        uint32_t k;

        if (cross > 0.0)
        {
            for (k = verts[next].next; k != prev; k = verts[k].next)
            {
                if (prc_tri_point_in_triangle(verts[k].uv, verts[prev].uv, verts[cur].uv, verts[next].uv))
                {
                    blocker = k;
                    break;
                }
            }
        }

        fprintf(stderr, "  [%u] uv=(%.9f,%.9f) prev=%u next=%u cross=%.9g %s\n",
            cur, verts[cur].uv.x, verts[cur].uv.y, prev, next, cross,
            (cross <= 0.0) ? "REFLEX" : (blocker != (uint32_t)-1) ? "BLOCKED" : "EAR");

        if (cross > 0.0 && blocker != (uint32_t)-1)
        {
            fprintf(stderr, "      blocked by [%u] uv=(%.9f,%.9f)\n",
                blocker, verts[blocker].uv.x, verts[blocker].uv.y);
        }

        cur = next;
    }
}
#endif /* PRC_DEBUG_EARCLIP */

/* Standard ear-clipping triangulation of the ring starting at head, which
   must be CCW (prc_tri_eliminate_hole guarantees this even with holes
   bridged in). Allocates and returns the triangle index array */
static int
prc_tri_earclip(prc_context *ctx, prc_tri_vertex *verts, uint32_t head,
    uint32_t ring_count, uint32_t **triangles_out, uint32_t *num_triangles_out)
{
    uint32_t remaining = ring_count;
    uint32_t *triangles;
    uint32_t tri_index = 0;
    uint32_t cur = head;

    if (ring_count < 3)
    {
        *triangles_out = NULL;
        *num_triangles_out = 0;
        return 0;
    }

    triangles = (uint32_t *)prc_calloc(ctx, (ring_count - 2) * 3, sizeof(uint32_t));
    if (triangles == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate triangles in prc_tri_earclip\n");
        return PRC_ERROR_MEMORY;
    }

    while (remaining > 3)
    {
        /* Scan every remaining vertex for a valid ear and take whichever
           produces the "roundest" triangle (smallest longest-edge length),
           rather than just the first one found by a linear scan: a long run
           of exactly-collinear boundary samples (e.g. many points along one
           straight edge of a trimmed loop, or a curve's own dense sampling)
           otherwise lets ear clipping degenerate into one long fan from a
           single vertex -- harmless on a flat plane, but a bad
           approximation on a curved surface, and even on a thin fringe
           strip it still shows up as visible "spoke" triangles */
        uint32_t best = (uint32_t)-1;
        double best_score = 0.0;
        uint32_t start = cur;
        uint32_t scan = cur;

        do
        {
            if (prc_tri_is_ear(verts, scan))
            {
                uint32_t prev = verts[scan].prev;
                uint32_t next = verts[scan].next;
                prc_vec2 a = verts[prev].uv, b = verts[scan].uv, c = verts[next].uv;
                double e0 = (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y);
                double e1 = (b.x - c.x) * (b.x - c.x) + (b.y - c.y) * (b.y - c.y);
                double e2 = (c.x - a.x) * (c.x - a.x) + (c.y - a.y) * (c.y - a.y);
                double score = e0 > e1 ? e0 : e1;

                score = score > e2 ? score : e2;

                if (best == (uint32_t)-1 || score < best_score)
                {
                    best = scan;
                    best_score = score;
                }
            }
            scan = verts[scan].next;
        } while (scan != start);

        if (best == (uint32_t)-1)
        {
#ifdef PRC_DEBUG_EARCLIP
            prc_tri_debug_dump_ring(ctx, verts, cur, remaining);
#endif
            /* No ear anywhere in a full pass means the ring is degenerate
               or self-intersecting */
            prc_free(ctx, triangles);
            prc_error(ctx, PRC_ERROR_INTERNAL, "Failed to triangulate planar loop boundary\n");
            return PRC_ERROR_INTERNAL;
        }

        {
            uint32_t prev = verts[best].prev;
            uint32_t next = verts[best].next;

            triangles[tri_index++] = prev;
            triangles[tri_index++] = best;
            triangles[tri_index++] = next;

            verts[prev].next = next;
            verts[next].prev = prev;
            verts[best].removed = 1;
            remaining--;
            cur = next;
        }
    }

    triangles[tri_index++] = verts[cur].prev;
    triangles[tri_index++] = cur;
    triangles[tri_index++] = verts[cur].next;

    *triangles_out = triangles;
    *num_triangles_out = ring_count - 2;

    return 0;
}

/* Finds the outer-ring vertex mutually visible with hole_start's uv, so a
   hole can be bridged into the outer ring without crossing it. Classic
   hole-elimination technique: cast a rightward ray from hole_start, take
   the rightmost endpoint of the outer edge it crosses first, then fall back
   to whichever reflex vertex inside that candidate triangle is most aligned
   with the ray, since that one is guaranteed visible */
static int
prc_tri_find_hole_bridge(const prc_tri_vertex *verts, uint32_t outer_head,
    uint32_t hole_start, uint32_t *bridge_out)
{
    prc_vec2 h = verts[hole_start].uv;
    double best_x = 0.0;
    uint8_t have_bridge = 0;
    uint32_t bridge = outer_head;
    uint32_t cur = outer_head;
    prc_vec2 intersect;

    do
    {
        uint32_t nxt = verts[cur].next;
        prc_vec2 a = verts[cur].uv;
        prc_vec2 b = verts[nxt].uv;

        if ((a.y > h.y) != (b.y > h.y))
        {
            double x = a.x + (h.y - a.y) * (b.x - a.x) / (b.y - a.y);

            if (x >= h.x && (!have_bridge || x < best_x))
            {
                best_x = x;
                bridge = (a.x > b.x) ? cur : nxt;
                have_bridge = 1;
            }
        }

        cur = nxt;
    } while (cur != outer_head);

    if (!have_bridge)
        return -1;

    intersect.x = best_x;
    intersect.y = h.y;

    {
        prc_vec2 m_point = verts[bridge].uv;
        uint32_t best_vertex = bridge;
        double best_angle = atan2(fabs(m_point.y - h.y), fabs(m_point.x - h.x));

        cur = outer_head;
        do
        {
            prc_vec2 p = verts[cur].uv;

            if (p.x >= h.x && prc_tri_point_in_triangle(p, h, intersect, m_point))
            {
                double angle = atan2(fabs(p.y - h.y), fabs(p.x - h.x));

                if (angle < best_angle)
                {
                    best_angle = angle;
                    best_vertex = cur;
                }
            }

            cur = verts[cur].next;
        } while (cur != outer_head);

        bridge = best_vertex;
    }

    *bridge_out = bridge;

    return 0;
}

/* Splices a hole ring (hole_head, hole_count vertices, CW) into the outer
   ring by duplicating the bridge vertex and the hole's own entry vertex,
   producing a single simple (if zero-width-slit) CCW ring */
static int
prc_tri_eliminate_hole(prc_context *ctx, prc_tri_vertex *verts, uint32_t *next_free_slot,
    uint32_t outer_head, uint32_t hole_head, uint32_t hole_count)
{
    uint32_t hole_start = hole_head;
    uint32_t cur = verts[hole_head].next;
    uint32_t k;
    uint32_t bridge;
    uint32_t h_dup, m_dup;
    uint32_t hole_last;
    uint32_t old_after_bridge;
    int code;

    /* The rightmost hole vertex is the conventional bridge entry point */
    for (k = 1; k < hole_count; k++)
    {
        if (verts[cur].uv.x > verts[hole_start].uv.x)
            hole_start = cur;
        cur = verts[cur].next;
    }

    code = prc_tri_find_hole_bridge(verts, outer_head, hole_start, &bridge);
    if (code < 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "No visible bridge to outer loop in prc_tri_eliminate_hole\n");
        return PRC_ERROR_INTERNAL;
    }

    h_dup = (*next_free_slot)++;
    m_dup = (*next_free_slot)++;
    verts[h_dup].uv = verts[hole_start].uv;
    verts[m_dup].uv = verts[bridge].uv;
    verts[h_dup].removed = 0;
    verts[m_dup].removed = 0;

    old_after_bridge = verts[bridge].next;
    hole_last = verts[hole_start].prev;

    verts[bridge].next = hole_start;
    verts[hole_start].prev = bridge;

    verts[hole_last].next = h_dup;
    verts[h_dup].prev = hole_last;
    verts[h_dup].next = m_dup;
    verts[m_dup].prev = h_dup;

    verts[m_dup].next = old_after_bridge;
    verts[old_after_bridge].prev = m_dup;

    return 0;
}

/* Builds one ear-clippable ring from the outer loop with every hole loop
   bridged in, triangulates it, and hands back the (u, v) vertex positions
   the triangle indices refer to. Caller owns and must free *verts_out and
   *triangles_out */
static int
prc_triangulate_planar_loops(prc_context *ctx, uint32_t num_loops, prc_loop_samples *loop_samples,
    prc_vec2 **verts_out, uint32_t *num_verts_out,
    uint32_t **triangles_out, uint32_t *num_triangles_out)
{
    uint32_t outer_index = num_loops;
    uint32_t outer_count = 0;
    uint32_t total_capacity = 0;
    uint32_t num_holes = 0;
    uint32_t k;
    prc_tri_vertex *verts;
    uint32_t next_free;
    uint32_t outer_head;
    uint32_t *hole_order = NULL;
    int code;

    for (k = 0; k < num_loops; k++)
    {
        uint32_t count = (loop_samples[k].num_samples > 0) ? (loop_samples[k].num_samples - 1) : 0;

        total_capacity += count;
        if (loop_samples[k].is_outer_loop)
        {
            outer_index = k;
            outer_count = count;
        }
        else
        {
            num_holes++;
        }
    }

    if (outer_index == num_loops || outer_count < 3)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "No usable outer loop in prc_triangulate_planar_loops\n");
        return PRC_ERROR_INTERNAL;
    }

    total_capacity += num_holes * 2;   /* two duplicate vertices per bridged hole */

    verts = (prc_tri_vertex *)prc_calloc(ctx, total_capacity, sizeof(prc_tri_vertex));
    if (verts == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate loop vertices in prc_triangulate_planar_loops\n");
        return PRC_ERROR_MEMORY;
    }

    prc_tri_build_ring(verts, 0, outer_count, loop_samples[outer_index].uv_samples,
        prc_uv_polygon_signed_area(outer_count, loop_samples[outer_index].uv_samples) < 0.0);
    outer_head = 0;
    next_free = outer_count;

    if (num_holes > 0)
    {
        uint32_t idx = 0;

        hole_order = (uint32_t *)prc_calloc(ctx, num_holes, sizeof(uint32_t));
        if (hole_order == NULL)
        {
            prc_free(ctx, verts);
            prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate hole order in prc_triangulate_planar_loops\n");
            return PRC_ERROR_MEMORY;
        }

        for (k = 0; k < num_loops; k++)
        {
            if (k != outer_index && !loop_samples[k].is_outer_loop)
                hole_order[idx++] = k;
        }

        /* Merge farthest-right holes first so bridge segments cannot cross */
        for (k = 1; k < num_holes; k++)
        {
            uint32_t cand = hole_order[k];
            double cand_max_x = prc_uv_loop_max_x(&loop_samples[cand]);
            int m = (int)k - 1;

            while (m >= 0 && prc_uv_loop_max_x(&loop_samples[hole_order[m]]) < cand_max_x)
            {
                hole_order[m + 1] = hole_order[m];
                m--;
            }
            hole_order[m + 1] = cand;
        }

        for (k = 0; k < num_holes; k++)
        {
            uint32_t li = hole_order[k];
            uint32_t count = (loop_samples[li].num_samples > 0) ? (loop_samples[li].num_samples - 1) : 0;
            uint32_t hole_head = next_free;

            if (count < 3)
                continue;   /* degenerate hole loop; skip rather than fail the whole face */

            prc_tri_build_ring(verts, hole_head, count, loop_samples[li].uv_samples,
                prc_uv_polygon_signed_area(count, loop_samples[li].uv_samples) > 0.0);
            next_free += count;

            code = prc_tri_eliminate_hole(ctx, verts, &next_free, outer_head, hole_head, count);
            if (code < 0)
            {
                prc_free(ctx, hole_order);
                prc_free(ctx, verts);
                return code;
            }
        }

        prc_free(ctx, hole_order);
    }

    code = prc_tri_earclip(ctx, verts, outer_head, next_free, triangles_out, num_triangles_out);
    if (code < 0)
    {
        prc_free(ctx, verts);
        return code;
    }

    *verts_out = (prc_vec2 *)prc_calloc(ctx, next_free, sizeof(prc_vec2));
    if (*verts_out == NULL)
    {
        prc_free(ctx, verts);
        prc_free(ctx, *triangles_out);
        *triangles_out = NULL;
        prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate output vertices in prc_triangulate_planar_loops\n");
        return PRC_ERROR_MEMORY;
    }
    for (k = 0; k < next_free; k++)
        (*verts_out)[k] = verts[k].uv;
    *num_verts_out = next_free;

    prc_free(ctx, verts);

    return 0;
}

/* Tessellates a planar face directly from its loop boundaries instead of the
   regular parametric grid: the plane's own domain need not match the face at
   all, only the loops define its real outer edge and holes */
static int
prc_tessellate_planar_face_from_loops(prc_context *ctx, prc_data *data, uint32_t shell_index,
    uint32_t face_index, uint32_t geom_count, uint8_t orientation,
    surface_func surface_eval_func, prc_surface_params *surf_params,
    const prc_surface_sampling_info *sampling_info,
    uint32_t num_loops, prc_loop_samples *loop_samples)
{
    prc_vec2 *poly_verts = NULL;
    uint32_t poly_num_verts = 0;
    uint32_t *poly_triangles = NULL;
    uint32_t poly_num_triangles = 0;
    prc_exact_geom_tess_data *tess_data;
    prc_vec3 normal;
    uint32_t v, t;
    int code;

    code = prc_triangulate_planar_loops(ctx, num_loops, loop_samples,
        &poly_verts, &poly_num_verts, &poly_triangles, &poly_num_triangles);
    if (code < 0)
    {
        prc_error(ctx, code, "Failed in prc_triangulate_planar_loops\n");
        return code;
    }

    data->exact_geom_tess_part[geom_count].shells[shell_index].faces[face_index].tess_data =
        (prc_exact_geom_tess_data *)prc_calloc(ctx, 1, sizeof(prc_exact_geom_tess_data));
    if (data->exact_geom_tess_part[geom_count].shells[shell_index].faces[face_index].tess_data == NULL)
    {
        prc_free(ctx, poly_verts);
        prc_free(ctx, poly_triangles);
        prc_error(ctx, PRC_ERROR_MEMORY, "Allocation failure of tess_data in prc_tessellate_surface\n");
        return PRC_ERROR_MEMORY;
    }
    tess_data = data->exact_geom_tess_part[geom_count].shells[shell_index].faces[face_index].tess_data;

    tess_data->number_of_vertices = poly_num_verts;
    tess_data->vertices = (prc_exact_geom_vertex *)prc_calloc(ctx, poly_num_verts, sizeof(prc_exact_geom_vertex));
    if (tess_data->vertices == NULL)
    {
        prc_free(ctx, poly_verts);
        prc_free(ctx, poly_triangles);
        prc_error(ctx, PRC_ERROR_MEMORY, "Allocation failure of tess_data vertices in prc_tessellate_surface\n");
        return PRC_ERROR_MEMORY;
    }

    /* The face is flat, so one evaluation gives the normal for every vertex */
    code = prc_compute_surface_normal(ctx, surface_eval_func, surf_params,
        poly_verts[0].x, poly_verts[0].y, PLANE_SURFACE_PRECISION, PLANE_SURFACE_PRECISION,
        poly_verts[0].x - 1.0, poly_verts[0].x + 1.0,
        poly_verts[0].y - 1.0, poly_verts[0].y + 1.0,
        sampling_info, orientation, &normal);
    if (code < 0)
    {
        prc_free(ctx, poly_verts);
        prc_free(ctx, poly_triangles);
        return code;
    }

    for (v = 0; v < poly_num_verts; v++)
    {
        prc_vec3 position = surface_eval_func(ctx, surf_params, poly_verts[v].x, poly_verts[v].y);

        tess_data->vertices[v].position[0] = (float)position.x;
        tess_data->vertices[v].position[1] = (float)position.y;
        tess_data->vertices[v].position[2] = (float)position.z;
        tess_data->vertices[v].normal[0] = (float)normal.x;
        tess_data->vertices[v].normal[1] = (float)normal.y;
        tess_data->vertices[v].normal[2] = (float)normal.z;
    }

    tess_data->number_of_triangles = poly_num_triangles;
    tess_data->triangles = (uint32_t *)prc_calloc(ctx, poly_num_triangles * 3, sizeof(uint32_t));
    if (tess_data->triangles == NULL)
    {
        prc_free(ctx, poly_verts);
        prc_free(ctx, poly_triangles);
        prc_error(ctx, PRC_ERROR_MEMORY, "Allocation failure of tess_data triangles in prc_tessellate_surface\n");
        return PRC_ERROR_MEMORY;
    }

    /* Ear clipping walked the CCW ring, which is the winding orientation == 1 expects */
    for (t = 0; t < poly_num_triangles; t++)
    {
        uint32_t i0 = poly_triangles[t * 3 + 0];
        uint32_t i1 = poly_triangles[t * 3 + 1];
        uint32_t i2 = poly_triangles[t * 3 + 2];

        if (orientation == 0)
        {
            tess_data->triangles[t * 3 + 0] = i0;
            tess_data->triangles[t * 3 + 1] = i2;
            tess_data->triangles[t * 3 + 2] = i1;
        }
        else
        {
            tess_data->triangles[t * 3 + 0] = i0;
            tess_data->triangles[t * 3 + 1] = i1;
            tess_data->triangles[t * 3 + 2] = i2;
        }
    }

    prc_free(ctx, poly_verts);
    prc_free(ctx, poly_triangles);

    return 0;
}

/* Computes a per-vertex surface normal for a point produced by loop-based
   (rather than regular-grid) tessellation, where u/v may legitimately fall
   outside the surface's nominal [start_u,end_u]x[start_v,end_v] domain (loop
   samples are unwrapped, so a periodic axis can run past one period tile).
   Sidesteps prc_compute_surface_normal's domain clamping/wrapping by feeding
   it a synthetic wide range that never clips the finite-difference step,
   the same trick prc_tessellate_planar_face_from_loops uses */
static int
prc_compute_loop_vertex_normal(prc_context *ctx, surface_func surface_eval_func,
    void *surf_params, double u, double v, double precision_u, double precision_v,
    const prc_surface_sampling_info *sampling_info, uint8_t orientation, prc_vec3 *normal)
{
    prc_surface_sampling_info local_info = *sampling_info;

    local_info.u_periodic = 0;
    local_info.v_periodic = 0;

    return prc_compute_surface_normal(ctx, surface_eval_func, surf_params,
        u, v, precision_u, precision_v,
        u - 1.0, u + 1.0, v - 1.0, v + 1.0,
        &local_info, orientation, normal);
}

/* Recursively centroid-splits a curved triangle (given as 3 uv corners)
   until the true surface point at its centroid is within tolerance of the
   flat (linearly-interpolated) centroid, or max_depth is reached. A split
   only ever adds a new interior point private to that one triangle -- it
   never touches a shared edge -- so, unlike edge-midpoint subdivision,
   neighboring triangles independently refined to different depths can never
   open a crack between them. Leaf triangles are appended as a flat,
   non-indexed triangle soup (3 fresh vertices each) to the growable output
   arrays, doubling capacity via prc_realloc as needed. Caller owns and must
   free *out_verts/*out_tris */
static int
prc_subdivide_curved_triangle(prc_context *ctx, surface_func surface_eval_func, void *surf_params,
    prc_vec2 uv0, prc_vec2 uv1, prc_vec2 uv2, double tolerance, uint32_t depth, uint32_t max_depth,
    prc_vec2 **out_verts, uint32_t *out_num_verts, uint32_t *out_verts_cap,
    uint32_t **out_tris, uint32_t *out_num_triangles, uint32_t *out_tris_cap)
{
    prc_vec3 p0, p1, p2, centroid_linear, centroid_true;
    prc_vec2 uvc;
    int code;

    p0 = surface_eval_func(ctx, surf_params, uv0.x, uv0.y);
    p1 = surface_eval_func(ctx, surf_params, uv1.x, uv1.y);
    p2 = surface_eval_func(ctx, surf_params, uv2.x, uv2.y);

    uvc.x = (uv0.x + uv1.x + uv2.x) / 3.0;
    uvc.y = (uv0.y + uv1.y + uv2.y) / 3.0;
    centroid_true = surface_eval_func(ctx, surf_params, uvc.x, uvc.y);
    centroid_linear.x = (p0.x + p1.x + p2.x) / 3.0;
    centroid_linear.y = (p0.y + p1.y + p2.y) / 3.0;
    centroid_linear.z = (p0.z + p1.z + p2.z) / 3.0;

    if (depth >= max_depth || prc_vec_dist_between_two_points(centroid_true, centroid_linear) <= tolerance)
    {
        if (*out_num_verts + 3 > *out_verts_cap)
        {
            uint32_t new_cap = (*out_verts_cap == 0) ? 64 : (*out_verts_cap * 2);
            prc_vec2 *new_verts;

            while (new_cap < *out_num_verts + 3)
                new_cap *= 2;
            new_verts = (prc_vec2 *)prc_realloc(ctx, *out_verts, new_cap * sizeof(prc_vec2));
            if (new_verts == NULL)
            {
                prc_error(ctx, PRC_ERROR_MEMORY, "Failed to grow vertices in prc_subdivide_curved_triangle\n");
                return PRC_ERROR_MEMORY;
            }
            *out_verts = new_verts;
            *out_verts_cap = new_cap;
        }
        if (*out_num_triangles + 1 > *out_tris_cap)
        {
            uint32_t new_cap = (*out_tris_cap == 0) ? 32 : (*out_tris_cap * 2);
            uint32_t *new_tris = (uint32_t *)prc_realloc(ctx, *out_tris, new_cap * 3 * sizeof(uint32_t));

            if (new_tris == NULL)
            {
                prc_error(ctx, PRC_ERROR_MEMORY, "Failed to grow triangles in prc_subdivide_curved_triangle\n");
                return PRC_ERROR_MEMORY;
            }
            *out_tris = new_tris;
            *out_tris_cap = new_cap;
        }

        (*out_verts)[*out_num_verts + 0] = uv0;
        (*out_verts)[*out_num_verts + 1] = uv1;
        (*out_verts)[*out_num_verts + 2] = uv2;
        (*out_tris)[*out_num_triangles * 3 + 0] = *out_num_verts + 0;
        (*out_tris)[*out_num_triangles * 3 + 1] = *out_num_verts + 1;
        (*out_tris)[*out_num_triangles * 3 + 2] = *out_num_verts + 2;
        *out_num_verts += 3;
        *out_num_triangles += 1;
        return 0;
    }

    code = prc_subdivide_curved_triangle(ctx, surface_eval_func, surf_params, uv0, uv1, uvc, tolerance, depth + 1, max_depth,
        out_verts, out_num_verts, out_verts_cap, out_tris, out_num_triangles, out_tris_cap);
    if (code < 0)
        return code;
    code = prc_subdivide_curved_triangle(ctx, surface_eval_func, surf_params, uv1, uv2, uvc, tolerance, depth + 1, max_depth,
        out_verts, out_num_verts, out_verts_cap, out_tris, out_num_triangles, out_tris_cap);
    if (code < 0)
        return code;
    code = prc_subdivide_curved_triangle(ctx, surface_eval_func, surf_params, uv2, uv0, uvc, tolerance, depth + 1, max_depth,
        out_verts, out_num_verts, out_verts_cap, out_tris, out_num_triangles, out_tris_cap);
    if (code < 0)
        return code;

    return 0;
}

static uint8_t
prc_surface_axis_wraps(double start_value, double end_value, uint8_t periodic, double period)
{
    double span;

    if (!periodic || period <= 0.0)
        return 0;

    span = fabs(end_value - start_value);
    return fabs(span - period) <= SURFACE_PRECISION ? 1 : 0;
}

static double
prc_get_surface_param(double start_value, double end_value, uint32_t index,
    uint32_t sample_count, uint8_t wraps)
{
    double denominator;

    if (sample_count <= 1)
        return start_value;

    denominator = wraps ? (double)sample_count : (double)(sample_count - 1);
    return start_value + (end_value - start_value) * ((double)index / denominator);
}

static int
prc_build_regular_grid(prc_context *ctx, surface_func surface_eval_func, prc_surface_params *surf_params,
    const prc_surface_sampling_info *sampling_info_in, uint8_t orientation, prc_regular_grid *grid_out)
{
    prc_surface_sampling_info sampling_info = *sampling_info_in;
    uint32_t num_samples_u = sampling_info.num_samples_u;
    uint32_t num_samples_v = sampling_info.num_samples_v;
    double precision_u = sampling_info.precision_u;
    double precision_v = sampling_info.precision_v;
    uint32_t max_samples_u = sampling_info.max_samples_u;
    uint32_t max_samples_v = sampling_info.max_samples_v;
    double start_u = sampling_info.start_u;
    double start_v = sampling_info.start_v;
    double end_u = sampling_info.end_u;
    double end_v = sampling_info.end_v;
    uint8_t wrap_u, wrap_v;
    uint32_t vertex_samples_u, vertex_samples_v, cell_count_u, cell_count_v;
    uint8_t surface_approx_good = 0;
    int code;

    wrap_u = prc_surface_axis_wraps(start_u, end_u, sampling_info.u_periodic, sampling_info.u_period);
    wrap_v = prc_surface_axis_wraps(start_v, end_v, sampling_info.v_periodic, sampling_info.v_period);
    vertex_samples_u = wrap_u ? (num_samples_u - 1) : num_samples_u;
    vertex_samples_v = wrap_v ? (num_samples_v - 1) : num_samples_v;
    cell_count_u = wrap_u ? vertex_samples_u : (vertex_samples_u - 1);
    cell_count_v = wrap_v ? vertex_samples_v : (vertex_samples_v - 1);

    while (!surface_approx_good)
    {
        uint32_t i, j;
        uint8_t refine_u = 0;
        uint8_t refine_v = 0;

        surface_approx_good = 1;

        if (!sampling_info.u_linear)
        {
            for (j = 0; j < vertex_samples_v; j++)
            {
                for (i = 0; i < cell_count_u; i++)
                {
                    uint32_t next_i = (i + 1 == vertex_samples_u && wrap_u) ? 0 : (i + 1);
                    double u0 = prc_get_surface_param(start_u, end_u, i, vertex_samples_u, wrap_u);
                    double u1 = prc_get_surface_param(start_u, end_u, next_i, vertex_samples_u, wrap_u);
                    double v = prc_get_surface_param(start_v, end_v, j, vertex_samples_v, wrap_v);
                    prc_vec3 p0 = surface_eval_func(ctx, surf_params, u0, v);
                    prc_vec3 p1 = surface_eval_func(ctx, surf_params, u1, v);
                    prc_vec3 mid = surface_eval_func(ctx, surf_params, 0.5 * (u0 + u1), v);
                    prc_vec3 seg_mid;

                    seg_mid.x = 0.5 * (p0.x + p1.x);
                    seg_mid.y = 0.5 * (p0.y + p1.y);
                    seg_mid.z = 0.5 * (p0.z + p1.z);

                    if (prc_vec_dist_between_two_points(mid, seg_mid) > precision_u)
                    {
                        refine_u = 1;
                        break;
                    }
                }

                if (refine_u)
                    break;
            }
        }

        if (!sampling_info.v_linear)
        {
            for (i = 0; i < vertex_samples_u; i++)
            {
                for (j = 0; j < cell_count_v; j++)
                {
                    uint32_t next_j = (j + 1 == vertex_samples_v && wrap_v) ? 0 : (j + 1);
                    double u = prc_get_surface_param(start_u, end_u, i, vertex_samples_u, wrap_u);
                    double v0 = prc_get_surface_param(start_v, end_v, j, vertex_samples_v, wrap_v);
                    double v1 = prc_get_surface_param(start_v, end_v, next_j, vertex_samples_v, wrap_v);
                    prc_vec3 p0 = surface_eval_func(ctx, surf_params, u, v0);
                    prc_vec3 p1 = surface_eval_func(ctx, surf_params, u, v1);
                    prc_vec3 mid = surface_eval_func(ctx, surf_params, u, 0.5 * (v0 + v1));
                    prc_vec3 seg_mid;

                    seg_mid.x = 0.5 * (p0.x + p1.x);
                    seg_mid.y = 0.5 * (p0.y + p1.y);
                    seg_mid.z = 0.5 * (p0.z + p1.z);

                    if (prc_vec_dist_between_two_points(mid, seg_mid) > precision_v)
                    {
                        refine_v = 1;
                        break;
                    }
                }

                if (refine_v)
                    break;
            }
        }

        surface_approx_good = !(refine_u || refine_v);

        if (!surface_approx_good)
        {
            if ((refine_u && num_samples_u >= max_samples_u) ||
                (refine_v && num_samples_v >= max_samples_v))
            {
                surface_approx_good = 1;
                break;
            }

            if (refine_u)
                num_samples_u *= 2;
            if (refine_v)
                num_samples_v *= 2;

            vertex_samples_u = wrap_u ? (num_samples_u - 1) : num_samples_u;
            vertex_samples_v = wrap_v ? (num_samples_v - 1) : num_samples_v;
            cell_count_u = wrap_u ? vertex_samples_u : (vertex_samples_u - 1);
            cell_count_v = wrap_v ? vertex_samples_v : (vertex_samples_v - 1);
        }
    }

    grid_out->vertex_samples_u = vertex_samples_u;
    grid_out->vertex_samples_v = vertex_samples_v;
    grid_out->wrap_u = wrap_u;
    grid_out->wrap_v = wrap_v;
    grid_out->uv = (prc_vec2 *)prc_calloc(ctx, vertex_samples_u * vertex_samples_v, sizeof(prc_vec2));
    grid_out->verts = (prc_exact_geom_vertex *)prc_calloc(ctx, vertex_samples_u * vertex_samples_v, sizeof(prc_exact_geom_vertex));
    if (grid_out->uv == NULL || grid_out->verts == NULL)
    {
        prc_free(ctx, grid_out->uv);
        prc_free(ctx, grid_out->verts);
        prc_error(ctx, PRC_ERROR_MEMORY, "Allocation failure in prc_build_regular_grid\n");
        return PRC_ERROR_MEMORY;
    }

    {
        uint32_t i, j;
        double du = (vertex_samples_u > 1) ? fabs(end_u - start_u) /
            (double)(wrap_u ? vertex_samples_u : (vertex_samples_u - 1)) : 1.0;
        double dv = (vertex_samples_v > 1) ? fabs(end_v - start_v) /
            (double)(wrap_v ? vertex_samples_v : (vertex_samples_v - 1)) : 1.0;

        for (j = 0; j < vertex_samples_v; j++)
        {
            for (i = 0; i < vertex_samples_u; i++)
            {
                uint32_t vertex_index = j * vertex_samples_u + i;
                prc_vec3 position;
                prc_vec3 normal;
                double u = prc_get_surface_param(start_u, end_u, i, vertex_samples_u, wrap_u);
                double v = prc_get_surface_param(start_v, end_v, j, vertex_samples_v, wrap_v);

                position = surface_eval_func(ctx, surf_params, u, v);

                code = prc_compute_surface_normal(ctx, surface_eval_func, surf_params,
                    u, v, du, dv, start_u, end_u, start_v, end_v, &sampling_info,
                    orientation, &normal);
                if (code < 0)
                {
                    prc_free(ctx, grid_out->uv);
                    prc_free(ctx, grid_out->verts);
                    return code;
                }

                grid_out->uv[vertex_index].x = u;
                grid_out->uv[vertex_index].y = v;
                grid_out->verts[vertex_index].position[0] = (float)position.x;
                grid_out->verts[vertex_index].position[1] = (float)position.y;
                grid_out->verts[vertex_index].position[2] = (float)position.z;
                grid_out->verts[vertex_index].normal[0] = (float)normal.x;
                grid_out->verts[vertex_index].normal[1] = (float)normal.y;
                grid_out->verts[vertex_index].normal[2] = (float)normal.z;
            }
        }
    }

    return 0;
}

/* Emits the standard 2-triangles-per-cell tessellation of every cell in a
   regular grid (the untrimmed case, where every cell is kept) */
static int
prc_emit_grid_all_triangles(prc_context *ctx, const prc_regular_grid *grid, uint8_t orientation,
    uint32_t **triangles_out, uint32_t *num_triangles_out)
{
    uint32_t cell_count_u = grid->wrap_u ? grid->vertex_samples_u : (grid->vertex_samples_u - 1);
    uint32_t cell_count_v = grid->wrap_v ? grid->vertex_samples_v : (grid->vertex_samples_v - 1);
    uint32_t *triangles;
    uint32_t triangle_index = 0;
    uint32_t i, j;

    triangles = (uint32_t *)prc_calloc(ctx, cell_count_u * cell_count_v * 6, sizeof(uint32_t));
    if (triangles == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Allocation failure in prc_emit_grid_all_triangles\n");
        return PRC_ERROR_MEMORY;
    }

    for (j = 0; j < cell_count_v; j++)
    {
        for (i = 0; i < cell_count_u; i++)
        {
            uint32_t next_i = (i + 1 == grid->vertex_samples_u && grid->wrap_u) ? 0 : (i + 1);
            uint32_t next_j = (j + 1 == grid->vertex_samples_v && grid->wrap_v) ? 0 : (j + 1);
            uint32_t i00 = j * grid->vertex_samples_u + i;
            uint32_t i10 = j * grid->vertex_samples_u + next_i;
            uint32_t i01 = next_j * grid->vertex_samples_u + i;
            uint32_t i11 = next_j * grid->vertex_samples_u + next_i;

            if (orientation == 0)
            {
                triangles[triangle_index++] = i00;
                triangles[triangle_index++] = i01;
                triangles[triangle_index++] = i10;
                triangles[triangle_index++] = i10;
                triangles[triangle_index++] = i01;
                triangles[triangle_index++] = i11;
            }
            else
            {
                triangles[triangle_index++] = i00;
                triangles[triangle_index++] = i10;
                triangles[triangle_index++] = i01;
                triangles[triangle_index++] = i10;
                triangles[triangle_index++] = i11;
                triangles[triangle_index++] = i01;
            }
        }
    }

    *triangles_out = triangles;
    *num_triangles_out = cell_count_u * cell_count_v * 2;
    return 0;
}

/* Finds where a loop that winds once around uv axis wrap_axis crosses the
   query point's own coordinate on that axis, and reports whether the query
   point is on the loop's material side there (interior-on-the-left
   convention -- see prc_build_periodic_outer_loop) */
static int
prc_wrapping_loop_side(const prc_loop_samples *loop, uint8_t wrap_axis, int32_t wind, prc_vec2 q)
{
    uint32_t count = (loop->num_samples > 0) ? (loop->num_samples - 1) : 0;
    double a_first, a_last, period, mid, qa, qa_shifted, qb;
    uint32_t i;

    if (count < 2)
        return 1;

    a_first = wrap_axis ? loop->uv_samples[0].y : loop->uv_samples[0].x;
    a_last = wrap_axis ? loop->uv_samples[count].y : loop->uv_samples[count].x;
    period = fabs(a_last - a_first);
    if (period <= 0.0)
        return 1;

    mid = (a_first + a_last) / 2.0;
    qa = wrap_axis ? q.y : q.x;
    qb = wrap_axis ? q.x : q.y;
    qa_shifted = qa + period * (double)lround((mid - qa) / period);

    for (i = 0; i < count; i++)
    {
        double ai = wrap_axis ? loop->uv_samples[i].y : loop->uv_samples[i].x;
        double bi = wrap_axis ? loop->uv_samples[i].x : loop->uv_samples[i].y;
        double ai1 = wrap_axis ? loop->uv_samples[i + 1].y : loop->uv_samples[i + 1].x;
        double bi1 = wrap_axis ? loop->uv_samples[i + 1].x : loop->uv_samples[i + 1].y;

        if ((ai <= qa_shifted && qa_shifted <= ai1) || (ai1 <= qa_shifted && qa_shifted <= ai))
        {
            double t = (ai1 != ai) ? (qa_shifted - ai) / (ai1 - ai) : 0.0;
            double b_curve = bi + t * (bi1 - bi);

            return (wind > 0) ? (qb > b_curve) : (qb < b_curve);
        }
    }

    /* No bracketing segment found (shouldn't happen for a loop spanning a
       full period) -- fall back to comparing against the nearest endpoint */
    {
        double b_curve = (fabs(qa_shifted - a_first) < fabs(qa_shifted - a_last)) ?
            (wrap_axis ? loop->uv_samples[0].x : loop->uv_samples[0].y) :
            (wrap_axis ? loop->uv_samples[count].x : loop->uv_samples[count].y);

        return (wind > 0) ? (qb > b_curve) : (qb < b_curve);
    }
}

/* Point-in-polygon test (even-odd / ray casting rule), used to determine
   which loop of a planar face encloses the others in uv space */
static int
prc_uv_point_in_polygon(prc_vec2 point, uint32_t num_verts, const prc_vec2 *verts)
{
    uint32_t i, j;
    int inside = 0;

    for (i = 0, j = num_verts - 1; i < num_verts; j = i++)
    {
        double xi = verts[i].x, yi = verts[i].y;
        double xj = verts[j].x, yj = verts[j].y;

        if (((yi > point.y) != (yj > point.y)) &&
            (point.x < (xj - xi) * (point.y - yi) / (yj - yi) + xi))
        {
            inside = !inside;
        }
    }

    return inside;
}


/* Whether uv point q lies inside the region a face's loop(s) bound.
   Non-wrapping loops use point-in-polygon nesting (inside an outer loop,
   outside every hole loop); a loop that winds once around a periodic axis
   instead divides the surface along that axis, classified via
   prc_wrapping_loop_side. Multiple loops simply AND together */
static int
prc_point_inside_trimmed_region(uint32_t num_loops, const prc_loop_samples *loop_samples, prc_vec2 q)
{
    uint32_t k;

    for (k = 0; k < num_loops; k++)
    {
        const prc_loop_samples *loop = &loop_samples[k];
        uint32_t count = (loop->num_samples > 0) ? (loop->num_samples - 1) : 0;

        /* A loop collapsed to a single point (e.g. a cone's apex) bounds
           zero area -- it can never exclude a grid point, so skip it rather
           than running it through point-in-polygon */
        if (loop->is_point_loop)
            continue;

        if (loop->wind_u != 0 || loop->wind_v != 0)
        {
            uint8_t wrap_axis = (loop->wind_u != 0) ? 0 : 1;
            int32_t wind = wrap_axis ? loop->wind_v : loop->wind_u;

            if (!prc_wrapping_loop_side(loop, wrap_axis, wind, q))
                return 0;
        }
        else if (count >= 3)
        {
            int contains = prc_uv_point_in_polygon(q, count, loop->uv_samples);

            if (loop->is_outer_loop)
            {
                if (!contains)
                    return 0;
            }
            else if (contains)
            {
                return 0;
            }
        }
    }

    return 1;
}

static int32_t
prc_wrap_cell_index(int32_t idx, uint32_t cell_count, uint8_t wraps)
{
    if (wraps)
    {
        int32_t m = (int32_t)cell_count;

        return ((idx % m) + m) % m;
    }
    if (idx < 0 || idx >= (int32_t)cell_count)
        return -1;
    return idx;
}

/* Traces the boundary/boundaries of the "kept" (fully inside) cells of a
   classified regular grid, marching-squares style: any cell edge whose
   neighboring cell is either out of range (a true domain edge) or not kept
   is a boundary edge; those edges are stitched end-to-end into one or more
   closed polylines. Returns each traced polyline as a prc_loop_samples
   (caller owns and must free each entry's uv_samples, and the array itself) */
static int
prc_trace_kept_region_boundaries(prc_context *ctx, const uint8_t *cell_inside,
    uint32_t vertex_samples_u, uint32_t vertex_samples_v, uint8_t wrap_u, uint8_t wrap_v,
    const prc_vec2 *grid_uv, prc_loop_samples **out_loops, uint32_t *out_num_loops)
{
    uint32_t cell_count_u = wrap_u ? vertex_samples_u : (vertex_samples_u - 1);
    uint32_t cell_count_v = wrap_v ? vertex_samples_v : (vertex_samples_v - 1);
    uint32_t num_verts = vertex_samples_u * vertex_samples_v;
    uint32_t i, j, k;
    prc_uint32_pair *edges;
    uint32_t num_edges = 0, edges_cap;
    uint32_t *adj0, *adj1;
    uint8_t *visited;
    prc_loop_samples *loops = NULL;
    uint32_t num_loops = 0, loops_cap = 0;

    edges_cap = cell_count_u * cell_count_v * 4 + 4;
    edges = (prc_uint32_pair *)prc_calloc(ctx, edges_cap, sizeof(prc_uint32_pair));
    if (edges == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate edges in prc_trace_kept_region_boundaries\n");
        return PRC_ERROR_MEMORY;
    }

    for (j = 0; j < cell_count_v; j++)
    {
        for (i = 0; i < cell_count_u; i++)
        {
            uint32_t vnext_i, vnext_j;
            int32_t ni_left, ni_right, nj_below, nj_above;

            if (!cell_inside[j * cell_count_u + i])
                continue;

            vnext_i = (i + 1 == vertex_samples_u && wrap_u) ? 0 : (i + 1);
            vnext_j = (j + 1 == vertex_samples_v && wrap_v) ? 0 : (j + 1);

            nj_below = prc_wrap_cell_index((int32_t)j - 1, cell_count_v, wrap_v);
            if (nj_below < 0 || !cell_inside[(uint32_t)nj_below * cell_count_u + i])
            {
                edges[num_edges].a = j * vertex_samples_u + i;
                edges[num_edges].b = j * vertex_samples_u + vnext_i;
                num_edges++;
            }

            nj_above = prc_wrap_cell_index((int32_t)j + 1, cell_count_v, wrap_v);
            if (nj_above < 0 || !cell_inside[(uint32_t)nj_above * cell_count_u + i])
            {
                edges[num_edges].a = vnext_j * vertex_samples_u + vnext_i;
                edges[num_edges].b = vnext_j * vertex_samples_u + i;
                num_edges++;
            }

            ni_left = prc_wrap_cell_index((int32_t)i - 1, cell_count_u, wrap_u);
            if (ni_left < 0 || !cell_inside[j * cell_count_u + (uint32_t)ni_left])
            {
                edges[num_edges].a = vnext_j * vertex_samples_u + i;
                edges[num_edges].b = j * vertex_samples_u + i;
                num_edges++;
            }

            ni_right = prc_wrap_cell_index((int32_t)i + 1, cell_count_u, wrap_u);
            if (ni_right < 0 || !cell_inside[j * cell_count_u + (uint32_t)ni_right])
            {
                edges[num_edges].a = j * vertex_samples_u + vnext_i;
                edges[num_edges].b = vnext_j * vertex_samples_u + vnext_i;
                num_edges++;
            }
        }
    }

    if (num_edges == 0)
    {
        prc_free(ctx, edges);
        *out_loops = NULL;
        *out_num_loops = 0;
        return 0;
    }

    adj0 = (uint32_t *)prc_calloc(ctx, num_verts, sizeof(uint32_t));
    adj1 = (uint32_t *)prc_calloc(ctx, num_verts, sizeof(uint32_t));
    visited = (uint8_t *)prc_calloc(ctx, num_edges, sizeof(uint8_t));
    if (adj0 == NULL || adj1 == NULL || visited == NULL)
    {
        prc_free(ctx, edges);
        prc_free(ctx, adj0);
        prc_free(ctx, adj1);
        prc_free(ctx, visited);
        prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate adjacency in prc_trace_kept_region_boundaries\n");
        return PRC_ERROR_MEMORY;
    }
    for (k = 0; k < num_verts; k++)
    {
        adj0[k] = (uint32_t)-1;
        adj1[k] = (uint32_t)-1;
    }
    for (k = 0; k < num_edges; k++)
    {
        uint32_t a = edges[k].a, b = edges[k].b;

        if (adj0[a] == (uint32_t)-1)
            adj0[a] = k;
        else
            adj1[a] = k;
        if (adj0[b] == (uint32_t)-1)
            adj0[b] = k;
        else
            adj1[b] = k;
    }

    for (k = 0; k < num_edges; k++)
    {
        uint32_t start, cur_vert, cur_edge;
        prc_vec2 *path = NULL;
        uint32_t path_len = 0, path_cap = 0;

        if (visited[k])
            continue;

        start = edges[k].a;
        cur_vert = edges[k].a;
        cur_edge = k;

        for (;;)
        {
            uint32_t other = (edges[cur_edge].a == cur_vert) ? edges[cur_edge].b : edges[cur_edge].a;
            uint32_t e0, e1, next_edge;

            visited[cur_edge] = 1;

            if (path_len + 1 > path_cap)
            {
                uint32_t new_cap = path_cap ? path_cap * 2 : 16;
                prc_vec2 *new_path = (prc_vec2 *)prc_realloc(ctx, path, new_cap * sizeof(prc_vec2));

                if (new_path == NULL)
                {
                    prc_free(ctx, path);
                    prc_free(ctx, edges);
                    prc_free(ctx, adj0);
                    prc_free(ctx, adj1);
                    prc_free(ctx, visited);
                    prc_error(ctx, PRC_ERROR_MEMORY, "Failed to grow traced path in prc_trace_kept_region_boundaries\n");
                    return PRC_ERROR_MEMORY;
                }
                path = new_path;
                path_cap = new_cap;
            }
            path[path_len++] = grid_uv[cur_vert];

            cur_vert = other;
            if (cur_vert == start)
                break;

            e0 = adj0[cur_vert];
            e1 = adj1[cur_vert];
            next_edge = (e0 != cur_edge && !visited[e0]) ? e0 :
                (e1 != (uint32_t)-1 && e1 != cur_edge && !visited[e1]) ? e1 : (uint32_t)-1;
            if (next_edge == (uint32_t)-1)
                break;   /* not a clean manifold boundary; stop this loop early rather than spin forever */
            cur_edge = next_edge;
        }

        if (path_len >= 3)
        {
            if (num_loops + 1 > loops_cap)
            {
                uint32_t new_cap = loops_cap ? loops_cap * 2 : 4;
                prc_loop_samples *new_loops = (prc_loop_samples *)prc_realloc(ctx, loops, new_cap * sizeof(prc_loop_samples));

                if (new_loops == NULL)
                {
                    prc_free(ctx, path);
                    prc_free(ctx, edges);
                    prc_free(ctx, adj0);
                    prc_free(ctx, adj1);
                    prc_free(ctx, visited);
                    prc_free(ctx, loops);
                    prc_error(ctx, PRC_ERROR_MEMORY, "Failed to grow traced loops in prc_trace_kept_region_boundaries\n");
                    return PRC_ERROR_MEMORY;
                }
                loops = new_loops;
                loops_cap = new_cap;
            }

            memset(&loops[num_loops], 0, sizeof(prc_loop_samples));
            loops[num_loops].num_samples = path_len + 1;
            loops[num_loops].uv_samples = (prc_vec2 *)prc_calloc(ctx, path_len + 1, sizeof(prc_vec2));
            if (loops[num_loops].uv_samples == NULL)
            {
                prc_free(ctx, path);
                prc_free(ctx, edges);
                prc_free(ctx, adj0);
                prc_free(ctx, adj1);
                prc_free(ctx, visited);
                prc_free(ctx, loops);
                prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate traced loop uv in prc_trace_kept_region_boundaries\n");
                return PRC_ERROR_MEMORY;
            }
            memcpy(loops[num_loops].uv_samples, path, path_len * sizeof(prc_vec2));
            loops[num_loops].uv_samples[path_len] = path[0];
            num_loops++;
        }

        prc_free(ctx, path);
    }

    prc_free(ctx, edges);
    prc_free(ctx, adj0);
    prc_free(ctx, adj1);
    prc_free(ctx, visited);

    *out_loops = loops;
    *out_num_loops = num_loops;

    return 0;
}

static void
prc_free_loop_array(prc_context *ctx, prc_loop_samples *loops, uint32_t count)
{
    uint32_t k;

    if (loops == NULL)
        return;
    for (k = 0; k < count; k++)
        prc_free(ctx, loops[k].uv_samples);
    prc_free(ctx, loops);
}

/* Assembles the outer boundary for a face whose loop(s) wind around a
   periodic uv axis (wrap_axis: 0 = u, 1 = v) into one flat, ordinary uv
   polygon by cutting a synthetic seam, so it can be handed to the same
   ear-clip machinery used for planar outer loops. Handles the two shapes
   this can take:
     - a single wrapping loop, closed off against the surface's own natural
       domain edge on the other axis (e.g. a cylinder trimmed at one rim
       only, still bounded by the surface's actual v extent on the other
       side)
     - two loops winding oppositely around the same axis, bridged directly
       to each other with no natural domain edge involved (a band trimmed
       by two rims, e.g. the middle section of a cylinder)
   The seam itself becomes a real (if thin) edge of the output mesh, the
   same trade-off the existing hole-bridging code already makes. Caller owns
   and must free outer_loop_out->uv_samples */
static int
prc_build_periodic_outer_loop(prc_context *ctx, uint32_t num_loops, prc_loop_samples *loop_samples,
    uint8_t wrap_axis, const prc_surface_sampling_info *sampling_info,
    prc_loop_samples *outer_loop_out)
{
    uint32_t k;
    uint32_t wrap_indices[2];
    uint32_t num_wrap = 0;
    uint32_t count_a;
    prc_vec2 *combined = NULL;
    uint32_t combined_count = 0;

    for (k = 0; k < num_loops; k++)
    {
        int32_t wind = wrap_axis ? loop_samples[k].wind_v : loop_samples[k].wind_u;

        if (wind != 0)
        {
            if (num_wrap >= 2)
            {
                prc_error(ctx, PRC_ERROR_INTERNAL, "Too many wrapping loops in prc_build_periodic_outer_loop\n");
                return PRC_ERROR_INTERNAL;
            }
            wrap_indices[num_wrap++] = k;
        }
    }

    if (num_wrap == 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "No wrapping loop in prc_build_periodic_outer_loop\n");
        return PRC_ERROR_INTERNAL;
    }

    count_a = (loop_samples[wrap_indices[0]].num_samples > 0) ? (loop_samples[wrap_indices[0]].num_samples - 1) : 0;
    if (count_a < 2)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Degenerate wrapping loop in prc_build_periodic_outer_loop\n");
        return PRC_ERROR_INTERNAL;
    }

    if (num_wrap == 2)
    {
        uint32_t idx_b = wrap_indices[1];
        uint32_t count_b = (loop_samples[idx_b].num_samples > 0) ? (loop_samples[idx_b].num_samples - 1) : 0;
        double a_end_a, a_start_b, shift;
        uint32_t j;

        if (count_b < 2)
        {
            prc_error(ctx, PRC_ERROR_INTERNAL, "Degenerate wrapping loop in prc_build_periodic_outer_loop\n");
            return PRC_ERROR_INTERNAL;
        }

        a_end_a = wrap_axis ? loop_samples[wrap_indices[0]].uv_samples[count_a].y
            : loop_samples[wrap_indices[0]].uv_samples[count_a].x;
        a_start_b = wrap_axis ? loop_samples[idx_b].uv_samples[0].y : loop_samples[idx_b].uv_samples[0].x;
        shift = a_end_a - a_start_b;

        combined_count = count_a + count_b;
        combined = (prc_vec2 *)prc_calloc(ctx, combined_count + 1, sizeof(prc_vec2));
        if (combined == NULL)
        {
            prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate combined seam ring in prc_build_periodic_outer_loop\n");
            return PRC_ERROR_INTERNAL;
        }

        for (j = 0; j < count_a; j++)
            combined[j] = loop_samples[wrap_indices[0]].uv_samples[j];

        for (j = 0; j < count_b; j++)
        {
            prc_vec2 p = loop_samples[idx_b].uv_samples[j];

            if (wrap_axis)
                p.y += shift;
            else
                p.x += shift;
            combined[count_a + j] = p;
        }

        /* --- STRAY LINE REMOVAL PATCH ---
           Explicitly close the loop back onto Loop A's starting coordinate
           frame by matching its absolute period position. */
        if (combined_count > 0)
        {
            if (wrap_axis == 0) // Longitude wrapping
            {
                combined[combined_count - 1].x = combined[0].x;
            }
            else // Latitude wrapping
            {
                combined[combined_count - 1].y = combined[0].y;
            }
        }

        /* Explicitly link the duplicate final end cap */
        combined[combined_count] = combined[0];

        /* === DUAL-WRAP PERIODIC SEAM CLOSURE DIAGNOSTIC === */
        fprintf(stderr, "=== CRITICAL SEAM DEBUG (num_wrap == 2) ===\n");
        fprintf(stderr, "Loop A Total Samples: %u, Loop B Total Samples: %u\n", count_a, count_b);
        fprintf(stderr, "Unroll Offset Shift Value: %.9f\n", shift);
        fprintf(stderr, "Loop A Start Point: (%.9f, %.9f)\n", combined[0].x, combined[0].y);
        fprintf(stderr, "Loop A End Point:   (%.9f, %.9f)\n", combined[count_a - 1].x, combined[count_a - 1].y);
        fprintf(stderr, "Loop B Start (Shifted):  (%.9f, %.9f)\n", combined[count_a].x, combined[count_a].y);
        fprintf(stderr, "Loop B End (Shifted):    (%.9f, %.9f)\n", combined[combined_count - 1].x, combined[combined_count - 1].y);
        fprintf(stderr, "Final Cap Ring Point:    (%.9f, %.9f)\n", combined[combined_count].x, combined[combined_count].y);
        /* ==================================================== */
    }
    else /* num_wrap == 1: close against the surface's natural domain edge */
    {
        int32_t wind = wrap_axis ? loop_samples[wrap_indices[0]].wind_v : loop_samples[wrap_indices[0]].wind_u;
        double edge_b, a_start, a_end;
        uint32_t j;
        uint8_t have_point_loop_edge = 0;
        double point_loop_edge = 0.0;

        /* A loop degenerated to a single point (e.g. a cone's apex) tells us
           definitively which domain edge the trimmed region closes against
           -- use it directly rather than the orientation-dependent guess
           below, which relies on loop sample direction matching the
           standard B-rep convention; prc_sample_coedge does not yet
           guarantee that (ignores coedge_orientation) */
        for (j = 0; j < num_loops; j++)
        {
            if (loop_samples[j].is_point_loop)
            {
                point_loop_edge = wrap_axis ? loop_samples[j].uv_samples[0].x : loop_samples[j].uv_samples[0].y;
                have_point_loop_edge = 1;
                break;
            }
        }

        /* Interior-on-left convention (walking the loop in its sampled
           direction, material is on the left in the uv plane): travelling
           in the increasing direction on the wrap axis puts the interior on
           the side of the larger-valued domain edge on the other axis, and
           vice versa. Following the fix in #140, prc_sample_coedge honors
           coedge_orientation, making this standard B-rep convention reliable. */
        edge_b = have_point_loop_edge ? point_loop_edge :
            (wind > 0) ? (wrap_axis ? sampling_info->end_u : sampling_info->end_v)
            : (wrap_axis ? sampling_info->start_u : sampling_info->start_v);

        a_start = wrap_axis ? loop_samples[wrap_indices[0]].uv_samples[0].y : loop_samples[wrap_indices[0]].uv_samples[0].x;
        a_end = wrap_axis ? loop_samples[wrap_indices[0]].uv_samples[count_a].y : loop_samples[wrap_indices[0]].uv_samples[count_a].x;

        combined_count = count_a + 2;
        combined = (prc_vec2 *)prc_calloc(ctx, combined_count, sizeof(prc_vec2));
        if (combined == NULL)
        {
            prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate combined seam ring in prc_build_periodic_outer_loop\n");
            return PRC_ERROR_MEMORY;
        }

        for (j = 0; j < count_a; j++)
            combined[j] = loop_samples[wrap_indices[0]].uv_samples[j];

        /* The two synthetic corners closing the ring against the domain edge */
        if (wrap_axis)
        {
            combined[count_a].x = edge_b;
            combined[count_a].y = a_end;
            combined[count_a + 1].x = edge_b;
            combined[count_a + 1].y = a_start;
        }
        else
        {
            combined[count_a].x = a_end;
            combined[count_a].y = edge_b;
            combined[count_a + 1].x = a_start;
            combined[count_a + 1].y = edge_b;
        }
    }

    memset(outer_loop_out, 0, sizeof(*outer_loop_out));
    outer_loop_out->num_samples = combined_count + 1;   /* +1 re-duplicates the closing point, matching prc_sample_loop's convention */
    outer_loop_out->uv_samples = (prc_vec2 *)prc_calloc(ctx, outer_loop_out->num_samples, sizeof(prc_vec2));
    if (outer_loop_out->uv_samples == NULL)
    {
        prc_free(ctx, combined);
        prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate outer loop uv_samples in prc_build_periodic_outer_loop\n");
        return PRC_ERROR_MEMORY;
    }
    memcpy(outer_loop_out->uv_samples, combined, combined_count * sizeof(prc_vec2));
    outer_loop_out->uv_samples[combined_count] = combined[0];
    outer_loop_out->is_outer_loop = 1;

    prc_free(ctx, combined);

    return 0;
}

/* A cone's radius is affine in v (radius = base_radius + v*tan(semi_angle)),
   so for fixed u, moving along v traces a straight 3D line (a ruling) all
   the way to the apex -- a straight edge from the apex to any boundary
   sample therefore lies exactly on the surface, with no curvature error.
   When a cone face's only loops are the apex (a single-point loop) and one
   loop winding once around the full circumference, that means a plain fan
   of triangles from the apex to each boundary sample is an exact
   tessellation of the whole face, simpler and more faithful than running it
   through the general grid+clip machinery (which has to treat the apex as
   just another regular-grid row, producing a dense cluster of nearly
   degenerate triangles there).
   The apex vertex is duplicated once per triangle: a true cone's normal
   depends only on u, not v (so it's constant along a whole ruling, and
   evaluating it anywhere on the ruling other than exactly at the apex -
   where the radius is zero and the finite-difference tangent degenerates -
   gives the exact value for that ruling, including at the apex), and each
   fan triangle spans a different u, so sharing one apex vertex/normal
   across all of them would be wrong. Winding/normal convention mirrors
   prc_tessellate_planar_face_from_loops (orientation 1 keeps the loop's own
   sample order, 0 swaps the last two indices) */
static int
prc_tessellate_cone_apex_fan_face(prc_context *ctx, prc_data *data, uint32_t shell_index,
    uint32_t face_index, uint32_t geom_count, uint8_t orientation,
    surface_func surface_eval_func, prc_surface_params *surf_params,
    const prc_surface_sampling_info *sampling_info,
    const prc_loop_samples *point_loop, const prc_loop_samples *wrap_loop)
{
    uint32_t count = (wrap_loop->num_samples > 0) ? (wrap_loop->num_samples - 1) : 0;
    prc_exact_geom_tess_data *tess_data;
    prc_vec3 apex_position;
    uint32_t i;
    int code;

    if (count < 2)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Degenerate wrapping loop in prc_tessellate_cone_apex_fan_face\n");
        return PRC_ERROR_INTERNAL;
    }

    data->exact_geom_tess_part[geom_count].shells[shell_index].faces[face_index].tess_data =
        (prc_exact_geom_tess_data *)prc_calloc(ctx, 1, sizeof(prc_exact_geom_tess_data));
    tess_data = data->exact_geom_tess_part[geom_count].shells[shell_index].faces[face_index].tess_data;
    if (tess_data == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Allocation failure of tess_data in prc_tessellate_cone_apex_fan_face\n");
        return PRC_ERROR_MEMORY;
    }

    tess_data->number_of_vertices = count * 2;   /* shared boundary ring + one apex copy per triangle */
    tess_data->vertices = (prc_exact_geom_vertex *)prc_calloc(ctx, tess_data->number_of_vertices, sizeof(prc_exact_geom_vertex));
    tess_data->number_of_triangles = count;
    tess_data->triangles = (uint32_t *)prc_calloc(ctx, count * 3, sizeof(uint32_t));
    if (tess_data->vertices == NULL || tess_data->triangles == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Allocation failure in prc_tessellate_cone_apex_fan_face\n");
        return PRC_ERROR_MEMORY;
    }

    apex_position = surface_eval_func(ctx, surf_params, point_loop->uv_samples[0].x, point_loop->uv_samples[0].y);

    for (i = 0; i < count; i++)
    {
        prc_vec3 position = surface_eval_func(ctx, surf_params, wrap_loop->uv_samples[i].x, wrap_loop->uv_samples[i].y);
        prc_vec3 normal;
        uint32_t apex_idx = count + i;
        uint32_t i_next = (i + 1 == count) ? 0 : (i + 1);

        code = prc_compute_loop_vertex_normal(ctx, surface_eval_func, surf_params,
            wrap_loop->uv_samples[i].x, wrap_loop->uv_samples[i].y,
            sampling_info->precision_u, sampling_info->precision_v,
            sampling_info, orientation, &normal);
        if (code < 0)
            return code;

        tess_data->vertices[i].position[0] = (float)position.x;
        tess_data->vertices[i].position[1] = (float)position.y;
        tess_data->vertices[i].position[2] = (float)position.z;
        tess_data->vertices[i].normal[0] = (float)normal.x;
        tess_data->vertices[i].normal[1] = (float)normal.y;
        tess_data->vertices[i].normal[2] = (float)normal.z;

        /* This ruling's normal (computed above, away from the degenerate
           apex) is exact for the apex copy too -- a cone's normal is
           constant along the whole ruling from base to apex */
        tess_data->vertices[apex_idx].position[0] = (float)apex_position.x;
        tess_data->vertices[apex_idx].position[1] = (float)apex_position.y;
        tess_data->vertices[apex_idx].position[2] = (float)apex_position.z;
        tess_data->vertices[apex_idx].normal[0] = (float)normal.x;
        tess_data->vertices[apex_idx].normal[1] = (float)normal.y;
        tess_data->vertices[apex_idx].normal[2] = (float)normal.z;

        if (orientation == 0)
        {
            tess_data->triangles[i * 3 + 0] = apex_idx;
            tess_data->triangles[i * 3 + 1] = i_next;
            tess_data->triangles[i * 3 + 2] = i;
        }
        else
        {
            tess_data->triangles[i * 3 + 0] = apex_idx;
            tess_data->triangles[i * 3 + 1] = i;
            tess_data->triangles[i * 3 + 2] = i_next;
        }
    }

    return 0;
}

/* Recursively splits a uv-space triangle (centroid fan-split, same pattern
   as prc_subdivide_curved_triangle) until each leaf is classified entirely
   inside or entirely outside the trimmed region (prc_point_inside_trimmed_
   region on its 3 corners), discarding outside leaves. Used instead of
   ear-clip-based boundary conformance for trim loops whose uv mapping is
   numerically projected rather than closed-form (NURBS/Blend02): a sharp
   corner or slightly noisy projected loop point there can make the ear-clip
   bridge/triangulate degenerate into long spoke/fan triangles, which this
   sidesteps entirely by never connecting two loop boundary points with a
   chord -- at the cost of a jagged (but, at max depth, far finer than one
   grid cell) boundary instead of an exact one */
static int
prc_subdivide_trim_boundary_triangle(prc_context *ctx, uint32_t num_loops, const prc_loop_samples *loop_samples,
    prc_vec2 uv0, prc_vec2 uv1, prc_vec2 uv2, uint32_t depth, uint32_t max_depth,
    prc_vec2 **out_verts, uint32_t *out_num_verts, uint32_t *out_verts_cap,
    uint32_t **out_tris, uint32_t *out_num_triangles, uint32_t *out_tris_cap)
{
    int in0, in1, in2, all_in, all_out;
    prc_vec2 uvc;
    int code;

    in0 = prc_point_inside_trimmed_region(num_loops, loop_samples, uv0);
    in1 = prc_point_inside_trimmed_region(num_loops, loop_samples, uv1);
    in2 = prc_point_inside_trimmed_region(num_loops, loop_samples, uv2);
    all_in = in0 && in1 && in2;
    all_out = !in0 && !in1 && !in2;

    if (depth >= max_depth || all_in)
    {
        if (!all_in)
        {
            /* Mixed or fully-outside corners at max depth: fall back to one
               centroid sample to decide whether this (by now tiny) leaf
               belongs to the trimmed region at all */
            uvc.x = (uv0.x + uv1.x + uv2.x) / 3.0;
            uvc.y = (uv0.y + uv1.y + uv2.y) / 3.0;
            if (!prc_point_inside_trimmed_region(num_loops, loop_samples, uvc))
                return 0;
        }

        if (*out_num_verts + 3 > *out_verts_cap)
        {
            uint32_t new_cap = (*out_verts_cap == 0) ? 64 : (*out_verts_cap * 2);
            prc_vec2 *new_verts;

            while (new_cap < *out_num_verts + 3)
                new_cap *= 2;
            new_verts = (prc_vec2 *)prc_realloc(ctx, *out_verts, new_cap * sizeof(prc_vec2));
            if (new_verts == NULL)
            {
                prc_error(ctx, PRC_ERROR_MEMORY, "Failed to grow vertices in prc_subdivide_trim_boundary_triangle\n");
                return PRC_ERROR_MEMORY;
            }
            *out_verts = new_verts;
            *out_verts_cap = new_cap;
        }
        if (*out_num_triangles + 1 > *out_tris_cap)
        {
            uint32_t new_cap = (*out_tris_cap == 0) ? 32 : (*out_tris_cap * 2);
            uint32_t *new_tris = (uint32_t *)prc_realloc(ctx, *out_tris, new_cap * 3 * sizeof(uint32_t));

            if (new_tris == NULL)
            {
                prc_error(ctx, PRC_ERROR_MEMORY, "Failed to grow triangles in prc_subdivide_trim_boundary_triangle\n");
                return PRC_ERROR_MEMORY;
            }
            *out_tris = new_tris;
            *out_tris_cap = new_cap;
        }

        (*out_verts)[*out_num_verts + 0] = uv0;
        (*out_verts)[*out_num_verts + 1] = uv1;
        (*out_verts)[*out_num_verts + 2] = uv2;
        (*out_tris)[*out_num_triangles * 3 + 0] = *out_num_verts + 0;
        (*out_tris)[*out_num_triangles * 3 + 1] = *out_num_verts + 1;
        (*out_tris)[*out_num_triangles * 3 + 2] = *out_num_verts + 2;
        *out_num_verts += 3;
        *out_num_triangles += 1;
        return 0;
    }

    if (all_out)
    {
        /* Cheap safety net for a trim feature thinner than this triangle:
           only bother subdividing further if the centroid disagrees with
           the 3 corners */
        uvc.x = (uv0.x + uv1.x + uv2.x) / 3.0;
        uvc.y = (uv0.y + uv1.y + uv2.y) / 3.0;
        if (!prc_point_inside_trimmed_region(num_loops, loop_samples, uvc))
            return 0;
    }
    else
    {
        uvc.x = (uv0.x + uv1.x + uv2.x) / 3.0;
        uvc.y = (uv0.y + uv1.y + uv2.y) / 3.0;
    }

    code = prc_subdivide_trim_boundary_triangle(ctx, num_loops, loop_samples, uv0, uv1, uvc, depth + 1, max_depth,
        out_verts, out_num_verts, out_verts_cap, out_tris, out_num_triangles, out_tris_cap);
    if (code < 0)
        return code;
    code = prc_subdivide_trim_boundary_triangle(ctx, num_loops, loop_samples, uv1, uv2, uvc, depth + 1, max_depth,
        out_verts, out_num_verts, out_verts_cap, out_tris, out_num_triangles, out_tris_cap);
    if (code < 0)
        return code;
    code = prc_subdivide_trim_boundary_triangle(ctx, num_loops, loop_samples, uv2, uv0, uvc, depth + 1, max_depth,
        out_verts, out_num_verts, out_verts_cap, out_tris, out_num_triangles, out_tris_cap);
    if (code < 0)
        return code;

    return 0;
}

/* Tessellates a curved (Cone/Cylinder/Sphere/Torus) face bounded by loop(s),
   whether a genuine simple closed trim polygon or a loop that winds around a
   periodic uv axis (degenerating to a line rather than an enclosed area in
   uv space, e.g. a circle around a cylinder's circumference). Rather than
   triangulating just the loop boundary (which can leave a few large
   ear-clip triangles cutting straight across a curved shape), this
   tessellates the FULL regular surface grid as usual, keeps whichever grid
   cells fall entirely inside the trimmed region, and ear-clip-fills only the
   thin fringe strip between the kept grid and the true trim boundary. The
   same building blocks (prc_build_regular_grid + prc_point_inside_trimmed_
   region) are general enough to later "cut" an already-tessellated surface,
   not just build a trimmed one from scratch.
   use_subdivision_fringe switches the fringe strip itself from that
   ear-clip method to prc_subdivide_trim_boundary_triangle -- used for
   surface types (NURBS/Blend02) whose loop uv mapping is numerically
   projected rather than closed-form, where ear-clip's boundary-conforming
   chords are prone to fan/spike artifacts at sharp corners or noisy
   projected points; Cone/Cylinder/Sphere/Torus keep the original ear-clip
   fringe unchanged */
static int
prc_tessellate_trimmed_face(prc_context *ctx, prc_data *data, uint32_t shell_index,
    uint32_t face_index, uint32_t geom_count, uint8_t orientation,
    surface_func surface_eval_func, prc_surface_params *surf_params,
    const prc_surface_sampling_info *sampling_info,
    uint32_t num_loops, prc_loop_samples *loop_samples, uint8_t has_wrapping_loop, uint8_t wrap_axis,
    uint8_t use_subdivision_fringe)
{
    prc_regular_grid grid = { 0 };
    uint8_t *cell_inside = NULL;
    uint32_t cell_count_u, cell_count_v, i, j, k;
    uint32_t *kept_triangles = NULL;
    uint32_t num_kept_triangles = 0;
    prc_loop_samples *traced_loops = NULL;
    uint32_t num_traced = 0;
    prc_loop_samples true_outer_loop = { 0 };
    uint32_t true_outer_index = (uint32_t)-1;
    prc_loop_samples *combined_loops = NULL;
    uint32_t combined_num_loops;
    uint32_t idx;
    prc_vec2 *fringe_poly_verts = NULL;
    uint32_t fringe_poly_num_verts = 0;
    uint32_t *fringe_poly_triangles = NULL;
    uint32_t fringe_poly_num_triangles = 0;
    prc_vec2 *fringe_verts = NULL;
    uint32_t fringe_num_verts = 0, fringe_verts_cap = 0;
    uint32_t *fringe_triangles = NULL;
    uint32_t fringe_num_triangles = 0, fringe_tris_cap = 0;
    double tolerance;
    prc_exact_geom_tess_data *tess_data;
    uint32_t total_verts, total_triangles, grid_num_verts;
    uint32_t v, t;
    int code;
    uint8_t *cell_processed = NULL;

    code = prc_build_regular_grid(ctx, surface_eval_func, surf_params, sampling_info, orientation, &grid);
    if (code < 0)
        return code;
    grid_num_verts = grid.vertex_samples_u * grid.vertex_samples_v;

    cell_count_u = grid.wrap_u ? grid.vertex_samples_u : (grid.vertex_samples_u - 1);
    cell_count_v = grid.wrap_v ? grid.vertex_samples_v : (grid.vertex_samples_v - 1);

    /* True outer boundary for the fringe fill: the surface's own trim loop
   (already flagged is_outer_loop) for the closed case, or a synthetic
   seam-cut ring folding the wrapping loop(s) into an ordinary polygon */
    if (has_wrapping_loop)
    {
        code = prc_build_periodic_outer_loop(ctx, num_loops, loop_samples, wrap_axis, sampling_info, &true_outer_loop);
        if (code < 0)
        {
            if (has_wrapping_loop)
                prc_free(ctx, true_outer_loop.uv_samples);
            prc_free(ctx, grid.uv);
            prc_free(ctx, grid.verts);
            prc_free(ctx, cell_inside);
            prc_free(ctx, kept_triangles);
            prc_free_loop_array(ctx, traced_loops, num_traced);
            return code;
        }
    }
    else
    {
        for (k = 0; k < num_loops; k++)
        {
            if (loop_samples[k].is_outer_loop)
            {
                true_outer_loop = loop_samples[k];
                true_outer_index = k;
                break;
            }
        }
        if (true_outer_index == (uint32_t)-1)
        {
            if (has_wrapping_loop)
                prc_free(ctx, true_outer_loop.uv_samples);
            prc_free(ctx, grid.uv);
            prc_free(ctx, grid.verts);
            prc_free(ctx, cell_inside);
            prc_free(ctx, kept_triangles);
            prc_free_loop_array(ctx, traced_loops, num_traced);
            prc_error(ctx, PRC_ERROR_INTERNAL, "No outer loop found in prc_tessellate_trimmed_face\n");
            return PRC_ERROR_INTERNAL;
        }
    }

    {
        uint8_t *cell_inside_raw = (uint8_t *)prc_calloc(ctx, cell_count_u * cell_count_v, sizeof(uint8_t));
        cell_processed = (uint8_t *)prc_calloc(ctx, cell_count_u * cell_count_v, sizeof(uint8_t));

        cell_inside = (uint8_t *)prc_calloc(ctx, cell_count_u * cell_count_v, sizeof(uint8_t));
        kept_triangles = (uint32_t *)prc_calloc(ctx, cell_count_u * cell_count_v * 6, sizeof(uint32_t));
        if (cell_inside_raw == NULL || cell_inside == NULL || kept_triangles == NULL || cell_processed == NULL)
        {
            if (has_wrapping_loop)
                prc_free(ctx, true_outer_loop.uv_samples);
            prc_free(ctx, grid.uv);
            prc_free(ctx, grid.verts);
            prc_free(ctx, cell_inside_raw);
            prc_free(ctx, cell_inside);
            prc_free(ctx, cell_processed);
            prc_free(ctx, kept_triangles);
            prc_error(ctx, PRC_ERROR_MEMORY, "Allocation failure in prc_tessellate_trimmed_face\n");
            return PRC_ERROR_MEMORY;
        }

        /* DYNAMIC BOUNDS CALCULATION: Extract active limits from unrolled seam shape */
        double min_trim_u = true_outer_loop.uv_samples[0].x;
        double max_trim_u = true_outer_loop.uv_samples[0].x;
        double min_trim_v = true_outer_loop.uv_samples[0].y;
        double max_trim_v = true_outer_loop.uv_samples[0].y;

        for (k = 1; k < true_outer_loop.num_samples; k++)
        {
            if (true_outer_loop.uv_samples[k].x < min_trim_u) min_trim_u = true_outer_loop.uv_samples[k].x;
            if (true_outer_loop.uv_samples[k].x > max_trim_u) max_trim_u = true_outer_loop.uv_samples[k].x;
            if (true_outer_loop.uv_samples[k].y < min_trim_v) min_trim_v = true_outer_loop.uv_samples[k].y;
            if (true_outer_loop.uv_samples[k].y > max_trim_v) max_trim_v = true_outer_loop.uv_samples[k].y;
        }

        for (j = 0; j < cell_count_v; j++)
        {
            for (i = 0; i < cell_count_u; i++)
            {
                uint32_t vnext_i = (i + 1 == grid.vertex_samples_u && grid.wrap_u) ? 0 : (i + 1);
                uint32_t vnext_j = (j + 1 == grid.vertex_samples_v && grid.wrap_v) ? 0 : (j + 1);
                uint32_t i00 = j * grid.vertex_samples_u + i;
                uint32_t i11 = vnext_j * grid.vertex_samples_u + vnext_i;

                if (has_wrapping_loop)
                {
                    if (wrap_axis == 0) // Loops wrap U-axis (Longitude)
                    {
                        double v0 = grid.uv[i00].y;
                        double v1 = grid.uv[i11].y;

                        /* Fix: Longitude wraps full 360, so ignore U trim limits completely.
                           Only restrict rows along the flat, linear V height axis. */
                        if (v0 >= min_trim_v - 0.001 && v1 <= max_trim_v + 0.001)
                        {
                            cell_inside_raw[j * cell_count_u + i] = 1;
                        }
                        else
                        {
                            cell_inside_raw[j * cell_count_u + i] = 0;
                            if (!grid.wrap_v) {
                                cell_processed[j * cell_count_u + i] = 1;
                            }
                        }
                    }
                    else // Loops wrap V-axis (Latitude)
                    {
                        double u0 = grid.uv[i00].x;
                        double u1 = grid.uv[i11].x;

                        /* Fix: Latitude wraps full 360, so ignore V trim limits completely.
                           Only restrict columns along the flat, linear U axis. */
                        if (u0 >= min_trim_u - 0.001 && u1 <= max_trim_u + 0.001)
                        {
                            cell_inside_raw[j * cell_count_u + i] = 1;
                        }
                        else
                        {
                            cell_inside_raw[j * cell_count_u + i] = 0;
                            if (!grid.wrap_u) {
                                cell_processed[j * cell_count_u + i] = 1;
                            }
                        }
                    }
                }
                else
                {
                    uint32_t i10 = j * grid.vertex_samples_u + vnext_i;
                    uint32_t i01 = vnext_j * grid.vertex_samples_u + i;
                    if (prc_point_inside_trimmed_region(num_loops, loop_samples, grid.uv[i00]) &&
                        prc_point_inside_trimmed_region(num_loops, loop_samples, grid.uv[i10]) &&
                        prc_point_inside_trimmed_region(num_loops, loop_samples, grid.uv[i11]) &&
                        prc_point_inside_trimmed_region(num_loops, loop_samples, grid.uv[i01]))
                    {
                        cell_inside_raw[j * cell_count_u + i] = 1;
                    }
                }
            }
        }

        /* Sync the erosion pass into cell_inside normally */
        /* Erode the raw classification by one cell in every direction (any
           cell touching a non-kept neighbor, or the true domain edge, is
           excluded) so the kept region's traced outline is always at least
           one cell away from both the true trim boundary and the surface's
           own natural domain edge. Without this, a face whose surface
           domain happens to already match its trim loop (common -- e.g. a
           1/8 torus authored with that as the surface's own domain box)
           leaves an almost-zero-width fringe: the traced outline and the
           true loop become nearly coincident, which is exactly the
           degenerate near-duplicate-collinear-point case that stalls
           ear-clipping. The subdivision fringe doesn't trace an outline or
           ear-clip at all, so it needs none of this -- it just subdivides
           every cell the raw classification didn't already accept whole */
           // if (use_subdivision_fringe || has_wrapping_loop)

        if (use_subdivision_fringe)
        {
            memcpy(cell_inside, cell_inside_raw, cell_count_u * cell_count_v * sizeof(uint8_t));
        }
        else
        {
            for (j = 0; j < cell_count_v; j++)
            {
                for (i = 0; i < cell_count_u; i++)
                {
                    if (!cell_inside_raw[j * cell_count_u + i])
                        continue;

                    int32_t ni_left = prc_wrap_cell_index((int32_t)i - 1, cell_count_u, grid.wrap_u);
                    int32_t ni_right = prc_wrap_cell_index((int32_t)i + 1, cell_count_u, grid.wrap_u);
                    int32_t nj_below = prc_wrap_cell_index((int32_t)j - 1, cell_count_v, grid.wrap_v);
                    int32_t nj_above = prc_wrap_cell_index((int32_t)j + 1, cell_count_v, grid.wrap_v);

                    /* A missing neighbor (index < 0) means this cell sits on a
                       non-wrapping axis's domain edge. That must erode just
                       like a non-kept neighbor would: the whole point of this
                       pass (see comment above) is to keep the traced outline
                       at least one cell away from the surface's own domain
                       edge as well as the true trim boundary, which matters
                       whenever a trim loop's edge coincides with that domain
                       edge (e.g. a sub-patch authored with its own surface
                       domain already tightened to its trim box) -- otherwise
                       that edge row/column is never eroded and the traced
                       outline doubles back on the coincident trim edge,
                       producing a zero-width degenerate ring. */
                    uint8_t u_valid = 1;
                    if (grid.wrap_u) {
                        if (ni_left < 0 || ni_right < 0 || !cell_inside_raw[j * cell_count_u + ni_left] || !cell_inside_raw[j * cell_count_u + ni_right]) {
                            u_valid = 0;
                        }
                    }
                    else {
                        if (ni_left < 0 || !cell_inside_raw[j * cell_count_u + ni_left])  u_valid = 0;
                        if (ni_right < 0 || !cell_inside_raw[j * cell_count_u + ni_right]) u_valid = 0;
                    }

                    uint8_t v_valid = 1;
                    if (grid.wrap_v) {
                        if (nj_below < 0 || nj_above < 0 || !cell_inside_raw[nj_below * cell_count_u + i] || !cell_inside_raw[nj_above * cell_count_u + i]) {
                            v_valid = 0;
                        }
                    }
                    else {
                        if (nj_below < 0 || !cell_inside_raw[nj_below * cell_count_u + i]) v_valid = 0;
                        if (nj_above < 0 || !cell_inside_raw[nj_above * cell_count_u + i]) v_valid = 0;
                    }

                    if (u_valid && v_valid)
                    {
                        cell_inside[j * cell_count_u + i] = 1;
                    }
                }
            }
        }

        /* Merge the flags so cell_processed tracks BOTH solid grid vertices and dead space */
        for (k = 0; k < cell_count_u * cell_count_v; k++)
        {
            if (cell_inside[k])
                cell_processed[k] = 1;
        }

        /* Set PRC_DIAG_TRIM_GRID=1 to dump the raw vs. eroded cell
           classification counts for this grid -- useful when a trimmed
           curved face's kept region looks like the wrong size/shape, or
           ear-clipping stalls on its traced boundary afterward */
        if (prc_diag_getenv("PRC_DIAG_TRIM_GRID") != NULL)
        {
            uint32_t raw_inside_count = 0;
            uint32_t eroded_inside_count = 0;
            uint32_t k_cell;

            for (k_cell = 0; k_cell < cell_count_u * cell_count_v; k_cell++) {
                if (cell_inside_raw[k_cell]) raw_inside_count++;
                if (cell_inside[k_cell]) eroded_inside_count++;
            }

            fprintf(stderr, "PRC_DIAG_TRIM_GRID: wrap_u=%u wrap_v=%u has_wrapping_loop=%u wrap_axis=%u "
                "cell_count_u=%u cell_count_v=%u raw_inside=%u eroded_inside=%u\n",
                grid.wrap_u, grid.wrap_v, has_wrapping_loop, wrap_axis,
                cell_count_u, cell_count_v, raw_inside_count, eroded_inside_count);
        }

        prc_free(ctx, cell_inside_raw);
    };

    for (j = 0; j < cell_count_v; j++)
    {
        for (i = 0; i < cell_count_u; i++)
        {
            if (cell_inside[j * cell_count_u + i])
            {
                uint32_t vnext_i = (i + 1 == grid.vertex_samples_u && grid.wrap_u) ? 0 : (i + 1);
                uint32_t vnext_j = (j + 1 == grid.vertex_samples_v && grid.wrap_v) ? 0 : (j + 1);
                uint32_t i00 = j * grid.vertex_samples_u + i;
                uint32_t i10 = j * grid.vertex_samples_u + vnext_i;
                uint32_t i11 = vnext_j * grid.vertex_samples_u + vnext_i;
                uint32_t i01 = vnext_j * grid.vertex_samples_u + i;

                if (orientation == 0)
                {
                    kept_triangles[num_kept_triangles * 3 + 0] = i00;
                    kept_triangles[num_kept_triangles * 3 + 1] = i01;
                    kept_triangles[num_kept_triangles * 3 + 2] = i10;
                    num_kept_triangles++;
                    kept_triangles[num_kept_triangles * 3 + 0] = i10;
                    kept_triangles[num_kept_triangles * 3 + 1] = i01;
                    kept_triangles[num_kept_triangles * 3 + 2] = i11;
                    num_kept_triangles++;
                }
                else
                {
                    kept_triangles[num_kept_triangles * 3 + 0] = i00;
                    kept_triangles[num_kept_triangles * 3 + 1] = i10;
                    kept_triangles[num_kept_triangles * 3 + 2] = i01;
                    num_kept_triangles++;
                    kept_triangles[num_kept_triangles * 3 + 0] = i10;
                    kept_triangles[num_kept_triangles * 3 + 1] = i11;
                    kept_triangles[num_kept_triangles * 3 + 2] = i01;
                    num_kept_triangles++;
                }
            }
        }
    }

    if (use_subdivision_fringe || has_wrapping_loop)
    {
        for (j = 0; j < cell_count_v; j++)
        {
            for (i = 0; i < cell_count_u; i++)
            {
                uint32_t vnext_i, vnext_j, i00, i10, i11, i01;

                if (cell_processed[j * cell_count_u + i])
                    continue;

                vnext_i = (i + 1 == grid.vertex_samples_u && grid.wrap_u) ? 0 : (i + 1);
                vnext_j = (j + 1 == grid.vertex_samples_v && grid.wrap_v) ? 0 : (j + 1);
                i00 = j * grid.vertex_samples_u + i;
                i10 = j * grid.vertex_samples_u + vnext_i;
                i11 = vnext_j * grid.vertex_samples_u + vnext_i;
                i01 = vnext_j * grid.vertex_samples_u + i;

                /* Fixed uv-space CCW winding regardless of orientation,
                   matching the ear-clip fringe's own convention below --
                   the shared triangle-output loop further down applies the
                   orientation flip once, the same way it already does for
                   that fringe */
                code = prc_subdivide_trim_boundary_triangle(ctx, num_loops, loop_samples,
                    grid.uv[i00], grid.uv[i10], grid.uv[i11], 0, PRC_TRIM_BOUNDARY_SUBDIVIDE_MAX_DEPTH,
                    &fringe_verts, &fringe_num_verts, &fringe_verts_cap,
                    &fringe_triangles, &fringe_num_triangles, &fringe_tris_cap);
                if (code >= 0)
                    code = prc_subdivide_trim_boundary_triangle(ctx, num_loops, loop_samples,
                        grid.uv[i00], grid.uv[i11], grid.uv[i01], 0, PRC_TRIM_BOUNDARY_SUBDIVIDE_MAX_DEPTH,
                        &fringe_verts, &fringe_num_verts, &fringe_verts_cap,
                        &fringe_triangles, &fringe_num_triangles, &fringe_tris_cap);
                if (code < 0)
                {
                    if (has_wrapping_loop)
                        prc_free(ctx, true_outer_loop.uv_samples);
                    prc_free(ctx, grid.uv);
                    prc_free(ctx, grid.verts);
                    prc_free(ctx, cell_inside);
                    prc_free(ctx, kept_triangles);
                    prc_free(ctx, fringe_verts);
                    prc_free(ctx, fringe_triangles);
                    return code;
                }
            }
        }
        goto fringe_done;
    }

    code = prc_trace_kept_region_boundaries(ctx, cell_inside, grid.vertex_samples_u, grid.vertex_samples_v,
        grid.wrap_u, grid.wrap_v, grid.uv, &traced_loops, &num_traced);
    if (code < 0)
    {
        if (has_wrapping_loop)
            prc_free(ctx, true_outer_loop.uv_samples);
        prc_free(ctx, grid.uv);
        prc_free(ctx, grid.verts);
        prc_free(ctx, cell_inside);
        prc_free(ctx, kept_triangles);
        return code;
    }

    if (has_wrapping_loop)
    {
        uint32_t count_a = (loop_samples[0].num_samples > 0) ? (loop_samples[0].num_samples - 1) : 0;
        fprintf(stderr, "=== SEAM TRANSITION DIAGNOSTIC ===\n");
        fprintf(stderr, "Outer Loop End [Index %u]: UV = (%.6f, %.6f)\n",
            count_a - 1, true_outer_loop.uv_samples[count_a - 1].x, true_outer_loop.uv_samples[count_a - 1].y);
        fprintf(stderr, "Seam Jump Start [Index %u]: UV = (%.6f, %.6f)\n",
            count_a, true_outer_loop.uv_samples[count_a].x, true_outer_loop.uv_samples[count_a].y);
        fprintf(stderr, "Inner Loop Start [Index %u]: UV = (%.6f, %.6f)\n",
            count_a + 1, true_outer_loop.uv_samples[count_a + 1].x, true_outer_loop.uv_samples[count_a + 1].y);
        fprintf(stderr, "==================================\n");
    }

    /* ====================================================================
    DIAGNOSTIC DEBUG: Print the unrolled polygon shape right before ear-clipping
    ==================================================================== */
    if (has_wrapping_loop)
    {
        uint32_t sample_count = true_outer_loop.num_samples;
        fprintf(stderr, "=== DIAGNOSTIC: UNROLLED PERIODIC OUTER LOOP POLYGON ===\n");
        fprintf(stderr, "Total vertices in unrolled ring: %u\n", sample_count);
        for (uint32_t d = 0; d < sample_count; d++)
        {
            fprintf(stderr, "  Vertex [%u]: UV = (%.6f, %.6f)\n",
                d, true_outer_loop.uv_samples[d].x, true_outer_loop.uv_samples[d].y);
        }
        fprintf(stderr, "========================================================\n");
    }

    /* Assemble: [true outer ring] + [traced kept-grid outline(s), holes] +
       [any additional non-wrapping loop not already the outer boundary,
       also a hole -- e.g. a drilled hole inside the trimmed region] */
    if (has_wrapping_loop)
    {
        combined_num_loops = 1;
    }
    else
    {
        combined_num_loops = 1 + num_traced;
    }

    for (k = 0; k < num_loops; k++)
    {
        if (loop_samples[k].wind_u != 0 || loop_samples[k].wind_v != 0 || k == true_outer_index ||
            loop_samples[k].is_point_loop)
            continue;
        combined_num_loops++;
    }

    combined_loops = (prc_loop_samples *)prc_calloc(ctx, combined_num_loops, sizeof(prc_loop_samples));
    if (combined_loops == NULL)
    {
        prc_free(ctx, grid.uv);
        prc_free(ctx, grid.verts);
        prc_free(ctx, cell_inside);
        prc_free(ctx, kept_triangles);
        prc_free_loop_array(ctx, traced_loops, num_traced);
        if (has_wrapping_loop)
            prc_free(ctx, true_outer_loop.uv_samples);
        prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate combined loops in prc_tessellate_trimmed_face\n");
        return PRC_ERROR_MEMORY;
    }

    combined_loops[0] = true_outer_loop;
    combined_loops[0].is_outer_loop = 1;
    idx = 1;
    if (!has_wrapping_loop)
    {
        for (k = 0; k < num_traced; k++)
        {
            combined_loops[idx] = traced_loops[k];
            combined_loops[idx].is_outer_loop = 0;
            idx++;
        }
    }
    for (k = 0; k < num_loops; k++)
    {
        if (loop_samples[k].wind_u != 0 || loop_samples[k].wind_v != 0 || k == true_outer_index ||
            loop_samples[k].is_point_loop)
            continue;
        combined_loops[idx] = loop_samples[k];
        combined_loops[idx].is_outer_loop = 0;
        idx++;
    }

    code = prc_triangulate_planar_loops(ctx, combined_num_loops, combined_loops,
        &fringe_poly_verts, &fringe_poly_num_verts, &fringe_poly_triangles, &fringe_poly_num_triangles);
    prc_free(ctx, combined_loops);
    prc_free_loop_array(ctx, traced_loops, num_traced);
    if (has_wrapping_loop)
    {
        prc_free(ctx, true_outer_loop.uv_samples);
        true_outer_loop.uv_samples = NULL;
    }
    if (code < 0)
    {
        prc_free(ctx, grid.uv);
        prc_free(ctx, grid.verts);
        prc_free(ctx, cell_inside);
        prc_free(ctx, kept_triangles);
        prc_error(ctx, code, "Failed in prc_triangulate_planar_loops\n");
        return code;
    }

    fprintf(stderr, "=== MESH CONFIGURATION VERIFICATION ===\n");
    fprintf(stderr, "Number of kept regular grid triangles: %u\n", num_kept_triangles);
    fprintf(stderr, "Number of boundary loop vertices for ear-clipping: %u\n", fringe_poly_num_verts);
    fprintf(stderr, "========================================\n");

    tolerance = (sampling_info->precision_u > sampling_info->precision_v) ?
        sampling_info->precision_u : sampling_info->precision_v;

    for (t = 0; t < fringe_poly_num_triangles; t++)
    {
        uint32_t i0 = fringe_poly_triangles[t * 3 + 0];
        uint32_t i1 = fringe_poly_triangles[t * 3 + 1];
        uint32_t i2 = fringe_poly_triangles[t * 3 + 2];

        code = prc_subdivide_curved_triangle(ctx, surface_eval_func, surf_params,
            fringe_poly_verts[i0], fringe_poly_verts[i1], fringe_poly_verts[i2], tolerance, 0,
            PRC_CURVED_LOOP_SUBDIVIDE_MAX_DEPTH,
            &fringe_verts, &fringe_num_verts, &fringe_verts_cap,
            &fringe_triangles, &fringe_num_triangles, &fringe_tris_cap);
        if (code < 0)
        {
            prc_free(ctx, grid.uv);
            prc_free(ctx, grid.verts);
            prc_free(ctx, cell_inside);
            prc_free(ctx, kept_triangles);
            prc_free(ctx, fringe_poly_verts);
            prc_free(ctx, fringe_poly_triangles);
            prc_free(ctx, fringe_verts);
            prc_free(ctx, fringe_triangles);
            return code;
        }
    }
    prc_free(ctx, fringe_poly_verts);
    prc_free(ctx, fringe_poly_triangles);

fringe_done:

    if (has_wrapping_loop && true_outer_loop.uv_samples != NULL)
    {
        prc_free(ctx, true_outer_loop.uv_samples);
        true_outer_loop.uv_samples = NULL;
    }

    prc_free(ctx, cell_processed);
    prc_free(ctx, cell_inside);

    total_verts = grid_num_verts + fringe_num_verts;
    total_triangles = num_kept_triangles + fringe_num_triangles;

    data->exact_geom_tess_part[geom_count].shells[shell_index].faces[face_index].tess_data =
        (prc_exact_geom_tess_data *)prc_calloc(ctx, 1, sizeof(prc_exact_geom_tess_data));
    if (data->exact_geom_tess_part[geom_count].shells[shell_index].faces[face_index].tess_data == NULL)
    {
        prc_free(ctx, grid.uv);
        prc_free(ctx, grid.verts);
        prc_free(ctx, kept_triangles);
        prc_free(ctx, fringe_verts);
        prc_free(ctx, fringe_triangles);
        prc_error(ctx, PRC_ERROR_MEMORY, "Allocation failure of tess_data in prc_tessellate_trimmed_face\n");
        return PRC_ERROR_MEMORY;
    }
    tess_data = data->exact_geom_tess_part[geom_count].shells[shell_index].faces[face_index].tess_data;

    tess_data->number_of_vertices = total_verts;
    tess_data->vertices = (prc_exact_geom_vertex *)prc_calloc(ctx, total_verts, sizeof(prc_exact_geom_vertex));
    if (tess_data->vertices == NULL)
    {
        prc_free(ctx, grid.uv);
        prc_free(ctx, grid.verts);
        prc_free(ctx, kept_triangles);
        prc_free(ctx, fringe_verts);
        prc_free(ctx, fringe_triangles);
        prc_error(ctx, PRC_ERROR_MEMORY, "Allocation failure of tess_data vertices in prc_tessellate_trimmed_face\n");
        return PRC_ERROR_MEMORY;
    }
    memcpy(tess_data->vertices, grid.verts, grid_num_verts * sizeof(prc_exact_geom_vertex));
    prc_free(ctx, grid.verts);

    for (v = 0; v < fringe_num_verts; v++)
    {
        prc_vec3 position = surface_eval_func(ctx, surf_params, fringe_verts[v].x, fringe_verts[v].y);
        prc_vec3 normal;

        code = prc_compute_loop_vertex_normal(ctx, surface_eval_func, surf_params,
            fringe_verts[v].x, fringe_verts[v].y,
            sampling_info->precision_u, sampling_info->precision_v,
            sampling_info, orientation, &normal);
        if (code < 0)
        {
            prc_free(ctx, grid.uv);
            prc_free(ctx, kept_triangles);
            prc_free(ctx, fringe_verts);
            prc_free(ctx, fringe_triangles);
            return code;
        }

        idx = grid_num_verts + v;
        tess_data->vertices[idx].position[0] = (float)position.x;
        tess_data->vertices[idx].position[1] = (float)position.y;
        tess_data->vertices[idx].position[2] = (float)position.z;
        tess_data->vertices[idx].normal[0] = (float)normal.x;
        tess_data->vertices[idx].normal[1] = (float)normal.y;
        tess_data->vertices[idx].normal[2] = (float)normal.z;
    }
    prc_free(ctx, grid.uv);
    prc_free(ctx, fringe_verts);

    tess_data->number_of_triangles = total_triangles;
    tess_data->triangles = (uint32_t *)prc_calloc(ctx, total_triangles * 3, sizeof(uint32_t));
    if (tess_data->triangles == NULL)
    {
        prc_free(ctx, kept_triangles);
        prc_free(ctx, fringe_triangles);
        prc_error(ctx, PRC_ERROR_MEMORY, "Allocation failure of tess_data triangles in prc_tessellate_trimmed_face\n");
        return PRC_ERROR_MEMORY;
    }
    memcpy(tess_data->triangles, kept_triangles, num_kept_triangles * 3 * sizeof(uint32_t));
    prc_free(ctx, kept_triangles);

    for (t = 0; t < fringe_num_triangles; t++)
    {
        uint32_t i0 = fringe_triangles[t * 3 + 0] + grid_num_verts;
        uint32_t i1 = fringe_triangles[t * 3 + 1] + grid_num_verts;
        uint32_t i2 = fringe_triangles[t * 3 + 2] + grid_num_verts;
        uint32_t out_idx = (num_kept_triangles + t) * 3;

        if (orientation == 0)
        {
            tess_data->triangles[out_idx + 0] = i0;
            tess_data->triangles[out_idx + 1] = i2;
            tess_data->triangles[out_idx + 2] = i1;
        }
        else
        {
            tess_data->triangles[out_idx + 0] = i0;
            tess_data->triangles[out_idx + 1] = i1;
            tess_data->triangles[out_idx + 2] = i2;
        }
    }
    prc_free(ctx, fringe_triangles);

    return 0;
}


static int
prc_get_surface_transform_inverse(prc_context *ctx, prc_type_surf *surface,
    prc_exact_geom_transform *inverse_transform)
{
    prc_exact_geom_transform *exact_transform;
    int code;

    inverse_transform->is_identity = 1;

    switch (surface->surface_type)
    {
    case PRC_TYPE_SURF_FromCurves:
    {
        exact_transform = &surface->surf_fromcurves->exact_geom_transform;
        break;
    }

    case PRC_TYPE_SURF_Cone:
    {
        exact_transform = &surface->surf_cone->exact_geom_transform;
        break;
    }

    case PRC_TYPE_SURF_Cylinder:
    {
        exact_transform = &surface->surf_cylinder->exact_geom_transform;
        break;
    }

    case PRC_TYPE_SURF_Sphere:
    {
        exact_transform = &surface->surf_sphere->exact_geom_transform;
        break;
    }

    case PRC_TYPE_SURF_Torus:
    {
        exact_transform = &surface->surf_torus->exact_geom_transform;
        break;
    }

    case PRC_TYPE_SURF_Cylindrical:
    {
        exact_transform = &surface->surf_cylindrical->exact_geom_transform;
        break;
    }

    case PRC_TYPE_SURF_Extrusion:
    {
        exact_transform = &surface->surf_extrusion->exact_geom_transform;
        break;
    }

    case PRC_TYPE_SURF_Revolution:
    {
        exact_transform = &surface->surf_revolution->exact_geom_transform;
        break;
    }

    case PRC_TYPE_SURF_Plane:
    {
        exact_transform = &surface->surf_plane->exact_geom_transform;
        break;
    }

    case PRC_TYPE_SURF_Offset:
    {
        exact_transform = &surface->surf_offset->exact_geom_transform;
        break;
    }

    case PRC_TYPE_SURF_NURBS:
    {
        exact_transform = NULL;
        break;
    }

    case PRC_TYPE_SURF_Blend02:
    {
        exact_transform = &surface->surf_blend02->exact_geom_transform;
        break;
    }

    case PRC_TYPE_SURF_Blend01:
    {
        exact_transform = &surface->surf_blend01->exact_geom_transform;
        break;
    }

    case PRC_TYPE_SURF_Blend03:
    {
        exact_transform = &surface->surf_blend03->exact_geom_transform;
        break;
    }

    default:
        return 0;
    }

    if (exact_transform != NULL && !exact_transform->is_identity)
    {
        /* First compute the inverse matrix */
        code = prc_invert_exact_transform(ctx, exact_transform, inverse_transform);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed in prc_invert_exact_transform\n");
            return code;
        }
    }
    return 0;
}

/* A cone's single trim loop can itself detour out to the apex and back
   (e.g. one loop built from a full circumference circle, a line down to
   the apex, a degenerate zero-radius "circle" sampled entirely at the
   apex, then a line back to the circle) instead of the apex being its own
   dedicated loop -- this samples as one combined loop with a long run of
   coincident apex points buried in the middle, which is_point_loop (a
   whole-loop check) does not catch. atan2 is exactly as meaningless there
   as for a whole-loop point loop, and unwrapping straight through that run
   corrupts the whole loop's wind_u. Detect any such embedded run by local
   (post-inverse-transform) radius and split it out into its own point
   loop, leaving the remaining samples as a clean circumference loop -- the
   same shape the dedicated-point-loop case already handles via
   prc_tessellate_cone_apex_fan_face. Reallocates *loop_samples_ptr and
   updates *num_loops_ptr in place only when a split actually happens. */
static int
prc_split_cone_apex_runs(prc_context *ctx, prc_type_surf *surface, uint32_t *num_loops_ptr, prc_loop_samples **loop_samples_ptr)
{
    uint32_t orig_num_loops = *num_loops_ptr;
    prc_loop_samples *loops = *loop_samples_ptr;
    prc_exact_geom_transform inverse_transform;
    uint32_t *run_start_arr = NULL, *run_end_arr = NULL;
    prc_vec3 *apex_point_arr = NULL;
    uint32_t num_splits = 0;
    uint32_t k;
    int code;

    if (surface->surface_type != PRC_TYPE_SURF_Cone || orig_num_loops == 0 || loops == NULL)
        return 0;

    code = prc_get_surface_transform_inverse(ctx, surface, &inverse_transform);
    if (code < 0)
        return code;

    run_start_arr = (uint32_t *)prc_calloc(ctx, orig_num_loops, sizeof(uint32_t));
    run_end_arr = (uint32_t *)prc_calloc(ctx, orig_num_loops, sizeof(uint32_t));
    apex_point_arr = (prc_vec3 *)prc_calloc(ctx, orig_num_loops, sizeof(prc_vec3));
    if (run_start_arr == NULL || run_end_arr == NULL || apex_point_arr == NULL)
    {
        prc_free(ctx, run_start_arr);
        prc_free(ctx, run_end_arr);
        prc_free(ctx, apex_point_arr);
        prc_error(ctx, PRC_ERROR_MEMORY, "Failed in allocation prc_split_cone_apex_runs\n");
        return PRC_ERROR_MEMORY;
    }

    for (k = 0; k < orig_num_loops; k++)
    {
        prc_loop_samples *loop = &loops[k];
        uint32_t n = loop->num_samples;
        uint32_t run_start = (uint32_t)-1, run_end = (uint32_t)-1;
        double max_radius = 0.0;
        uint32_t i;

        run_start_arr[k] = (uint32_t)-1;

        if (loop->is_point_loop || n < 6)
            continue;

        for (i = 0; i < n; i++)
        {
            prc_vec3 p = inverse_transform.is_identity ? loop->samples[i] :
                prc_exact_geom_apply_transform(ctx, &inverse_transform, loop->samples[i]);
            double radius = sqrt(p.x * p.x + p.y * p.y);

            if (radius > max_radius)
                max_radius = radius;
        }

        {
            double tol = fmax(max_radius * 1e-6, 1e-9);

            for (i = 0; i < n; i++)
            {
                prc_vec3 p = inverse_transform.is_identity ? loop->samples[i] :
                    prc_exact_geom_apply_transform(ctx, &inverse_transform, loop->samples[i]);
                double radius = sqrt(p.x * p.x + p.y * p.y);

                if (radius <= tol)
                {
                    if (run_start == (uint32_t)-1)
                        run_start = i;
                    run_end = i;
                }
                else if (run_start != (uint32_t)-1)
                {
                    break;   /* only ever expect one embedded apex run */
                }
            }
        }

        /* Require a genuine interior run: not the whole loop, and not
           touching either end (a run touching the start/end would mean the
           apex coincides with the loop's own closing point, a shape this
           scan is not built to split) */
        if (run_start == (uint32_t)-1 || run_start == 0 || run_end >= n - 1)
            continue;

        run_start_arr[k] = run_start;
        run_end_arr[k] = run_end;
        apex_point_arr[k] = loop->samples[run_start];
        num_splits++;
    }

    if (num_splits == 0)
    {
        prc_free(ctx, run_start_arr);
        prc_free(ctx, run_end_arr);
        prc_free(ctx, apex_point_arr);
        return 0;
    }

    {
        prc_loop_samples *new_loops = (prc_loop_samples *)prc_calloc(ctx, orig_num_loops + num_splits, sizeof(prc_loop_samples));
        uint32_t next_new_loop = orig_num_loops;

        if (new_loops == NULL)
        {
            prc_free(ctx, run_start_arr);
            prc_free(ctx, run_end_arr);
            prc_free(ctx, apex_point_arr);
            prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate split loops in prc_split_cone_apex_runs\n");
            return PRC_ERROR_MEMORY;
        }
        memcpy(new_loops, loops, orig_num_loops * sizeof(prc_loop_samples));
        prc_free(ctx, loops);

        for (k = 0; k < orig_num_loops; k++)
        {
            prc_loop_samples *loop;
            prc_loop_samples *point_loop;
            uint32_t run_start = run_start_arr[k], run_end = run_end_arr[k];
            uint32_t n, kept_count, pos, i;
            prc_vec3 *kept;

            if (run_start == (uint32_t)-1)
                continue;

            loop = &new_loops[k];
            n = loop->num_samples;
            kept_count = n - (run_end - run_start + 1);
            kept = (prc_vec3 *)prc_calloc(ctx, kept_count, sizeof(prc_vec3));
            if (kept == NULL)
            {
                prc_free(ctx, new_loops);
                prc_free(ctx, run_start_arr);
                prc_free(ctx, run_end_arr);
                prc_free(ctx, apex_point_arr);
                prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate reduced loop in prc_split_cone_apex_runs\n");
                return PRC_ERROR_MEMORY;
            }
            pos = 0;
            for (i = 0; i < run_start; i++)
                kept[pos++] = loop->samples[i];
            for (i = run_end + 1; i < n; i++)
                kept[pos++] = loop->samples[i];

            prc_free(ctx, loop->samples);
            loop->samples = kept;
            loop->num_samples = kept_count;

            point_loop = &new_loops[next_new_loop++];
            memset(point_loop, 0, sizeof(*point_loop));
            point_loop->num_samples = 2;
            point_loop->samples = (prc_vec3 *)prc_calloc(ctx, 2, sizeof(prc_vec3));
            if (point_loop->samples == NULL)
            {
                prc_free(ctx, new_loops);
                prc_free(ctx, run_start_arr);
                prc_free(ctx, run_end_arr);
                prc_free(ctx, apex_point_arr);
                prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate apex point loop in prc_split_cone_apex_runs\n");
                return PRC_ERROR_MEMORY;
            }
            point_loop->samples[0] = apex_point_arr[k];
            point_loop->samples[1] = apex_point_arr[k];
            point_loop->is_point_loop = 1;
        }

        *loop_samples_ptr = new_loops;
        *num_loops_ptr = orig_num_loops + num_splits;
    }

    prc_free(ctx, run_start_arr);
    prc_free(ctx, run_end_arr);
    prc_free(ctx, apex_point_arr);
    return 0;
}

/* Finds the loop whose uv polygon contains a sample point from every other
   loop and marks it as the outer one (the rest are holes). Only meaningful
   when every loop is a genuine simple closed polygon in uv space (i.e. none
   of them wind around a periodic axis -- see wind_u/wind_v) */
static int
prc_assign_outer_loop_by_nesting(prc_context *ctx, uint32_t num_loops, prc_loop_samples *loop_samples)
{
    uint32_t candidate, other;
    int outer_found = 0;

    for (candidate = 0; candidate < num_loops && !outer_found; candidate++)
    {
        int contains_all = 1;

        for (other = 0; other < num_loops; other++)
        {
            if (other == candidate)
                continue;

            if (loop_samples[other].num_samples == 0 ||
                !prc_uv_point_in_polygon(loop_samples[other].uv_samples[0],
                    loop_samples[candidate].num_samples, loop_samples[candidate].uv_samples))
            {
                contains_all = 0;
                break;
            }
        }

        if (contains_all)
        {
            loop_samples[candidate].is_outer_loop = 1;
            outer_found = 1;
        }
    }

    if (!outer_found)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL,
            "Could not determine outer loop in prc_assign_outer_loop_by_nesting\n");
        return PRC_ERROR_INTERNAL;
    }

    return 0;
}

/* Here if we have multiple loops we may need to assign them as inner or outer
   loops. This seems very messy in the specification so this may need some work
   here */
static int
prc_assign_loop_inner_outer(prc_context *ctx, prc_topo_face *topo_face, uint8_t orientation,
    uint8_t topo_context_behavior, uint32_t num_loops, prc_loop_samples *loop_samples,
    uint32_t surface_type)
{
    uint8_t first_loop_is_outer = topo_context_behavior & PRC_CONTEXT_OuterLoopsFirst;

    if (num_loops == 0)
        return 0;

    /* If number of loops is one then that is the outer loop and there
       are no inner loops or if first_loop_is_outer is true */
    if (num_loops == 1 || first_loop_is_outer == 1)
    {
        loop_samples->is_outer_loop = 1;
        return 0;
    }

    /* The spec is a little funny with respect to index_of_output_loop description
       for the face. In one spot it says . If PRC_CONTEXT_OuterLoopsFirst
       is set to TRUE in the topological context the face is contained in,
       the index of the outer loop shall be defined. But if PRC_CONTEXT_OuterLoopsFirst
       then I would think 0 would be the index as that one is first.... */
    if (topo_face->index_of_outer_loop != -1 && topo_face->index_of_outer_loop < num_loops)
    {
        loop_samples[topo_face->index_of_outer_loop].is_outer_loop = 1;
    }
    else
    {
        uint32_t k;
        uint8_t any_wraps = 0;

        /* Now we have to figure out the outer loop by brute force. This is
           going to depend upon the surface type.  For some surfaces, the loop
           may actually be a straight line on the surface. Think of a loop going
           around the circumference of a cylinder. In this case, this loop is
           really just a boundary edge. If you have two of these you have two edges */
        for (k = 0; k < num_loops; k++)
        {
            if (loop_samples[k].wind_u != 0 || loop_samples[k].wind_v != 0)
            {
                any_wraps = 1;
                break;
            }
        }

        switch (surface_type)
        {
        case PRC_TYPE_SURF_FromCurves:
        {
            break;
        }

        /* A wrapping loop divides the surface along a periodic axis
           rather than nesting inside another loop's polygon, so the
           nesting test below does not apply to it -- that case is
           instead handled directly by prc_tessellate_surface via each
           loop's wind_u/wind_v (see prc_tessellate_periodic_face_from_loops) */
        case PRC_TYPE_SURF_Cone:
        case PRC_TYPE_SURF_Cylinder:
        {
            if (any_wraps)
                break;

            return prc_assign_outer_loop_by_nesting(ctx, num_loops, loop_samples);
        }

        /* For a Torus or the sphere we could have concentric wrapping rings. We must
           set the outer loop role to the larger bounding domain or greater
           radius profile */
        case PRC_TYPE_SURF_Torus:
        case PRC_TYPE_SURF_Sphere:
        {
            if (any_wraps)
            {
                uint32_t outer_idx = 0;
                uint32_t k;

                // 1. DYNAMIC AXIS DETECTION: Check if loops wrap V (1) or U (0)
                // We check loop 0 as a representative baseline sample
                uint8_t active_wrap_axis = (loop_samples[0].wind_v != 0) ? 1 : 0;

                if (active_wrap_axis == 0)
                {
                    /* For U-wrapping loops: the loop closer to V = 0
                        defines the wide outer radius footprint of the tube */
                    double min_v_val = loop_samples[0].uv_samples[0].y;
                    uint32_t s_count0 = (loop_samples[0].num_samples > 0) ? (loop_samples[0].num_samples - 1) : 0;

                    for (uint32_t i = 1; i < s_count0; i++)
                    {
                        if (loop_samples[0].uv_samples[i].y < min_v_val)
                            min_v_val = loop_samples[0].uv_samples[i].y;
                    }

                    for (k = 1; k < num_loops; k++)
                    {
                        uint32_t s_count_k = (loop_samples[k].num_samples > 0) ? (loop_samples[k].num_samples - 1) : 0;
                        if (s_count_k == 0) continue;

                        double test_min_v = loop_samples[k].uv_samples[0].y;
                        for (uint32_t i = 1; i < s_count_k; i++)
                        {
                            if (loop_samples[k].uv_samples[i].y < test_min_v)
                                test_min_v = loop_samples[k].uv_samples[i].y;
                        }

                        if (test_min_v < min_v_val)
                        {
                            min_v_val = test_min_v;
                            outer_idx = k;
                        }
                    }
                }
                else
                {
                    /* For V-wrapping loops: the loop closer to U = 0
                        defines the outer bounding base anchor */
                    double min_u_val = loop_samples[0].uv_samples[0].x;
                    uint32_t s_count0 = (loop_samples[0].num_samples > 0) ? (loop_samples[0].num_samples - 1) : 0;

                    for (uint32_t i = 1; i < s_count0; i++)
                    {
                        if (loop_samples[0].uv_samples[i].x < min_u_val)
                            min_u_val = loop_samples[0].uv_samples[i].x;
                    }

                    for (k = 1; k < num_loops; k++)
                    {
                        uint32_t s_count_k = (loop_samples[k].num_samples > 0) ? (loop_samples[k].num_samples - 1) : 0;
                        if (s_count_k == 0) continue;

                        double test_min_u = loop_samples[k].uv_samples[0].x;
                        for (uint32_t i = 1; i < s_count_k; i++)
                        {
                            if (loop_samples[k].uv_samples[i].x < test_min_u)
                                test_min_u = loop_samples[k].uv_samples[i].x;
                        }

                        if (test_min_u < min_u_val)
                        {
                            min_u_val = test_min_u;
                            outer_idx = k;
                        }
                    }
                }

                // 2. ASSIGN OUTER FLAG: Inform the tessellator who the base anchor is
                loop_samples[outer_idx].is_outer_loop = 1;

                // 3. ENFORCE OPPOSITE WINDING: Flip the inner loop if it runs parallel
                if (num_loops == 2)
                {
                    uint32_t inner_idx = (outer_idx == 0) ? 1 : 0;
                    int32_t wind_outer = active_wrap_axis ? loop_samples[outer_idx].wind_v : loop_samples[outer_idx].wind_u;
                    int32_t wind_inner = active_wrap_axis ? loop_samples[inner_idx].wind_v : loop_samples[inner_idx].wind_u;

                    if (wind_outer == wind_inner)
                    {
                        // 1. EXTRACT THE BODY COUNT: Isolate the unique vertices by ignoring the end slot
                        uint32_t body_count = loop_samples[inner_idx].num_samples - 1;

                        // 2. REVERSE THE 3D WORLD COORDINATE BODY ELEMENTS
                        for (uint32_t i = 0; i < body_count / 2; i++)
                        {
                            prc_vec3 temp = loop_samples[inner_idx].samples[i];
                            loop_samples[inner_idx].samples[i] = loop_samples[inner_idx].samples[body_count - 1 - i];
                            loop_samples[inner_idx].samples[body_count - 1 - i] = temp;
                        }

                        // 3. REVERSE THE 2D UV PARAMETRIC COORDINATE BODY ELEMENTS
                        for (uint32_t i = 0; i < body_count / 2; i++)
                        {
                            prc_vec2 temp = loop_samples[inner_idx].uv_samples[i];
                            loop_samples[inner_idx].uv_samples[i] = loop_samples[inner_idx].uv_samples[body_count - 1 - i];
                            loop_samples[inner_idx].uv_samples[body_count - 1 - i] = temp;
                        }

                        // 4. SYNCHRONIZE THE END CAP DUPLICATE: 
                        // Re-copy the newly assigned start point back to the very last index slot
                        uint32_t last_slot_idx = loop_samples[inner_idx].num_samples - 1;
                        loop_samples[inner_idx].samples[last_slot_idx] = loop_samples[inner_idx].samples[0];
                        loop_samples[inner_idx].uv_samples[last_slot_idx] = loop_samples[inner_idx].uv_samples[0];

                        // 5. Update the winding states natively
                        loop_samples[inner_idx].wind_u = -loop_samples[inner_idx].wind_u;
                        loop_samples[inner_idx].wind_v = -loop_samples[inner_idx].wind_v;
                    }
                }
                break;
            }
            return prc_assign_outer_loop_by_nesting(ctx, num_loops, loop_samples);
        }

        case PRC_TYPE_SURF_Cylindrical:
        {
            break;
        }

        case PRC_TYPE_SURF_Extrusion:
        {
            break;
        }

        case PRC_TYPE_SURF_Revolution:
        {
            break;
        }

        case PRC_TYPE_SURF_Plane:
        {
            /* In the case of a plane, there is definitely one loop that
               is the outer boundary and all the others are holes. */
            return prc_assign_outer_loop_by_nesting(ctx, num_loops, loop_samples);
        }

        case PRC_TYPE_SURF_Offset:
        {
            break;
        }

        case PRC_TYPE_SURF_NURBS:
        {
            /* NURBS loops are never periodic/wrapping (see prc_get_
               surface_data), so like Plane this is always plain
               point-in-polygon nesting. */
            return prc_assign_outer_loop_by_nesting(ctx, num_loops, loop_samples);
        }

        case PRC_TYPE_SURF_Blend02:
        {
            break;
        }

        case PRC_TYPE_SURF_Blend01:
        {
            break;
        }

        default:
            return 0;
        }
    }

    return 0;
}


/* Rewrites one coordinate (x if use_y_component is 0, else y) of a closed
   loop's uv samples into a continuous unwrapped polyline, undoing the 2*pi
   branch cuts atan2 introduces between consecutive samples, and reports how
   many full turns the loop winds around that axis before closing back up (0
   if it does not wrap, i.e. it is a genuine simple polygon on this axis) */
static int32_t
prc_unwrap_angular_component(prc_vec2 *pts, uint32_t count, uint8_t use_y_component)
{
    uint32_t j;
    double total;

    if (count < 2)
        return 0;

    for (j = 1; j < count; j++)
    {
        double prev = use_y_component ? pts[j - 1].y : pts[j - 1].x;
        double curr = use_y_component ? pts[j].y : pts[j].x;
        double delta = curr - prev;

        delta -= (2.0 * PRC_PI) * floor((delta + PRC_PI) / (2.0 * PRC_PI));
        if (use_y_component)
            pts[j].y = prev + delta;
        else
            pts[j].x = prev + delta;
    }

    total = (use_y_component ? pts[count - 1].y : pts[count - 1].x) -
        (use_y_component ? pts[0].y : pts[0].x);

    return (int32_t)lround(total / (2.0 * PRC_PI));
}

static double
prc_map_to_two_pi(double angle)
{
    if (angle < 0.0)
    {
        angle += 2.0 * PRC_PI;
    }
    return angle;
}

/* Based upon surface type, map the 3D points to the UV surface storing
   the values in loop_samples structure. At this point, the loops are samples of
   the path in 3D space on the surface. We will move this to the uv parametric
   space. The loops at this point have the same start and end point and are
   sampled at a sufficient approximation with all the coedge samples placed in
   a single loop */
static int
prc_map_loops_to_surface(prc_context *ctx, prc_topo_face *topo_face, uint8_t orientation,
    prc_type_surf *surface, uint32_t num_loops, prc_loop_samples *loop_samples)
{
    int code;
    uint32_t k, j;
    prc_exact_geom_transform *exact_transform;
    prc_exact_geom_transform inverse_transform;
    prc_loop_samples *curr_loop;
    uint32_t num_samples;

    code = prc_get_surface_transform_inverse(ctx, surface, &inverse_transform);
    if (code < 0)
    {
        prc_error(ctx, code, "Failed in prc_get_surface_transform_inverse\n");
        return code;
    }

    /* Lets map the loop values using the inverse transform if needed */
    if (!inverse_transform.is_identity)
    {
        for (k = 0; k < num_loops; k++)
        {
            curr_loop = &loop_samples[k];
            num_samples = curr_loop->num_samples;
            for (j = 0; j < num_samples; j++)
            {
                curr_loop->samples[j] = prc_exact_geom_apply_transform(ctx,
                    &inverse_transform, curr_loop->samples[j]);
            }
        }
    }

    /* At this point all the loops should be in the uv base space for the surface */

    /* Allocate space for the uv values of the loops */
    for (k = 0; k < num_loops; k++)
    {
        loop_samples[k].uv_samples = (prc_vec2 *)prc_calloc(ctx, loop_samples[k].num_samples, sizeof(prc_vec2));
        if (loop_samples[k].uv_samples == NULL)
        {
            for (j = 0; j < (k - 1); j++)
            {
                prc_free(ctx, loop_samples[j].uv_samples);
                loop_samples[j].uv_samples = NULL;
            }
            prc_error(ctx, PRC_ERROR_MEMORY, "Failed in prc_map_loops_to_surface\n");
            return PRC_ERROR_MEMORY;
        }
    }

    /* Now actually map from the 3D base space to the uv surface space */
    switch (surface->surface_type)
    {
    case PRC_TYPE_SURF_FromCurves:
    {
        /* Use numerical projection */
        prc_surf_fromcurves *from_curves = surface->surf_fromcurves;
        prc_surface_params curve_eval_params = { 0 };
        prc_curve_sampling_info curve1_sampling_info;
        prc_curve_sampling_info curve2_sampling_info;
        prc_vec3 test_xyz;

        prc_get_curve_sample_info(ctx, &from_curves->first_curve, &curve1_sampling_info);
        prc_get_curve_sample_info(ctx, &from_curves->second_curve, &curve2_sampling_info);

        double min_u = curve1_sampling_info.start;
        double max_u = curve1_sampling_info.end;
        double min_v = curve2_sampling_info.start;
        double max_v = curve2_sampling_info.end;
        double u = 0, v = 0;
        double u_coeff_a = (from_curves->parameterization.u_param_coeff_a != 0.0) ? from_curves->parameterization.u_param_coeff_a : 1.0;
        double v_coeff_a = (from_curves->parameterization.v_param_coeff_a != 0.0) ? from_curves->parameterization.v_param_coeff_a : 1.0;

        curve_eval_params.surface_params = (void *)from_curves;
        if (min_v >= max_v)
        {
            min_v = 0;
            max_v = 1;
        }

        for (k = 0; k < num_loops; k++)
        {
            curr_loop = &loop_samples[k];
            num_samples = curr_loop->num_samples;

            for (j = 0; j < num_samples; j++)
            {
                uint8_t used_fallback = 0;

                if (j == 0)
                {
                    prc_project_point_onto_surface(ctx, prc_evaluate_surf_fromcurves, &curve_eval_params,
                        min_u, max_u, min_v, max_v, curr_loop->samples[j], &u, &v);
                }
                else
                {
                    /* There is a question about the uv parameterization here...  */
                    prc_refine_point_on_surface(ctx, prc_evaluate_surf_fromcurves, &curve_eval_params,
                        min_u, max_u, min_v, max_v, curr_loop->samples[j], u, v, &u, &v);

                    /* Check that this is a valid point. If not, then fallback onto course sample */
                    test_xyz = prc_evaluate_surf_fromcurves(ctx, &curve_eval_params, u, v);
                    if (prc_vec_dist_between_two_points(test_xyz, curr_loop->samples[j]) > 1e-6)
                    {
                        used_fallback = 1;
                        prc_project_point_onto_surface(ctx, prc_evaluate_surf_fromcurves, &curve_eval_params,
                            min_u, max_u, min_v, max_v, curr_loop->samples[j], &u, &v);
                    }
                }

                curr_loop->uv_samples[j].x = u;
                curr_loop->uv_samples[j].y = v;

#if CHECK_SURFACE_PROJECTION
                test_xyz = prc_evaluate_surf_fromcurves(ctx, &curve_eval_params, u, v);
                fprintf(stderr, " PRC_TYPE_SURF_FromCurves projection: curve_sample=%u raw_uv_from_solve=(%.9f,%.9f) KnownSurface_xyz = (%.3f,%.3f,%.3f) Surface_uv_from_projection = (%.3f,%.3f) Surface_XYZ_from_uv = (%.3f,%.3f,%.3f) (delta=(%.3f,%.3f,%.3f) used_fallback=%u\n",
                    j, u, v, curr_loop->samples[j].x, curr_loop->samples[j].y, curr_loop->samples[j].z, curr_loop->uv_samples[j].x, curr_loop->uv_samples[j].y,
                    test_xyz.x, test_xyz.y, test_xyz.z,
                    curr_loop->samples[j].x - test_xyz.x, curr_loop->samples[j].y - test_xyz.y, curr_loop->samples[j].z - test_xyz.z,
                    used_fallback);
#endif
            }
        }
        break;
    }

    case PRC_TYPE_SURF_Cone:
    {
        /* Use analytical projection */
        prc_surf_cone *cone = surface->surf_cone;
        double u_coeff_a = (cone->parameterization.u_param_coeff_a != 0.0) ? cone->parameterization.u_param_coeff_a : 1.0;
        double v_coeff_a = (cone->parameterization.v_param_coeff_a != 0.0) ? cone->parameterization.v_param_coeff_a : 1.0;
        double u, v;

        for (k = 0; k < num_loops; k++)
        {
            curr_loop = &loop_samples[k];
            num_samples = curr_loop->num_samples;

            /* A loop collapsed to the cone's apex has x=y=0 (within
               tolerance) at every sample, so atan2(y, x) there is just
               amplifying floating-point noise into an arbitrary angle --
               skip it and park every sample at a single, stable u since
               the loop bounds no area and no actual u value matters */
            if (curr_loop->is_point_loop)
            {
                v = curr_loop->samples[0].z;
                for (j = 0; j < num_samples; j++)
                {
                    curr_loop->uv_samples[j].x = (0.0 - cone->parameterization.u_param_coeff_b) / u_coeff_a;
                    curr_loop->uv_samples[j].y = (v - cone->parameterization.v_param_coeff_b) / v_coeff_a;
                }
                curr_loop->wind_u = 0;
                curr_loop->wind_v = 0;
            }
            else
            {
                for (j = 0; j < num_samples; j++)
                {
                    double x = curr_loop->samples[j].x;
                    double y = curr_loop->samples[j].y;

                    v = curr_loop->samples[j].z;

                    /* x = radius(v)*cos(u), y = radius(v)*sin(u): wherever
                       this cone's linear radius(v) = radius + v*tan(semi_angle)
                       is negative (common away from the apex -- a cone's
                       bottom_radius/semi_angle pair can put the whole usable
                       v range on the negative side), atan2(y, x) recovers
                       u + pi instead of u, since negating radius is the same
                       as rotating u by pi. Negate x,y first to undo that so
                       the recovered u matches the surface's own u = atan2(y,x)
                       convention used by prc_evaluate_surf_cone. */
                    if (cone->radius + v * tan(cone->semi_angle) < 0.0)
                    {
                        x = -x;
                        y = -y;
                    }
                    u = atan2(y, x);
                    u = prc_map_to_two_pi(u);

                    /* Store the raw angle/height for now; unwrap before the
                       affine parameterization map so the 2*pi period used to
                       detect wraparound isn't distorted by u_coeff_a */
                    curr_loop->uv_samples[j].x = u;
                    curr_loop->uv_samples[j].y = v;
                }
                curr_loop->wind_u = prc_unwrap_angular_component(curr_loop->uv_samples, num_samples, 0);
                curr_loop->wind_v = 0;
            }

            for (j = 0; j < num_samples; j++)
            {
                curr_loop->uv_samples[j].x = (curr_loop->uv_samples[j].x - cone->parameterization.u_param_coeff_b) / u_coeff_a;
                curr_loop->uv_samples[j].y = (curr_loop->uv_samples[j].y - cone->parameterization.v_param_coeff_b) / v_coeff_a;
#if CHECK_SURFACE_PROJECTION
                prc_vec3 test_xyz, test2_xyz;
                prc_surface_params cone_eval_params = { 0 };
                cone_eval_params.surface_params = (void *)cone;
                // Reconstruct the actual u and v that the surface expects after tuning:
                double eval_u = (curr_loop->uv_samples[j].x * u_coeff_a) + cone->parameterization.u_param_coeff_b;
                double eval_v = (curr_loop->uv_samples[j].y * v_coeff_a) + cone->parameterization.v_param_coeff_b;
                test_xyz = prc_evaluate_surf_cone(ctx, &cone_eval_params, eval_u, eval_v);

                /* We need to apply the transformation to curve xyz as test_xyz does have that applied it */
                if (cone->has_transform && !cone->exact_geom_transform.is_identity)
                {
                    test2_xyz = prc_exact_geom_apply_transform(ctx, &cone->exact_geom_transform, curr_loop->samples[j]);
                }
                else
                {
                    test2_xyz = curr_loop->samples[j];
                }
                fprintf(stderr, " PRC_TYPE_SURF_Cone projection: curve_sample=%u KnownSurface_xyz = (%.3f,%.3f,%.3f) Surface_uv_from_projection = (%.3f,%.3f) Surface_XYZ_from_uv = (%.3f,%.3f,%.3f) (delta=(%.3f,%.3f,%.3f)\n",
                    j, test2_xyz.x, test2_xyz.y, test2_xyz.z, curr_loop->uv_samples[j].x, curr_loop->uv_samples[j].y,
                    test_xyz.x, test_xyz.y, test_xyz.z, test2_xyz.x - test_xyz.x, test2_xyz.y - test_xyz.y, test2_xyz.z - test_xyz.z);
#endif
            }
        }
        break;
    }

    case PRC_TYPE_SURF_Cylinder:
    {
        /* Use analytical projection */
        prc_surf_cylinder *cylinder = surface->surf_cylinder;
        double u_coeff_a = (cylinder->parameterization.u_param_coeff_a != 0.0) ? cylinder->parameterization.u_param_coeff_a : 1.0;
        double v_coeff_a = (cylinder->parameterization.v_param_coeff_a != 0.0) ? cylinder->parameterization.v_param_coeff_a : 1.0;
        double u, v;

        for (k = 0; k < num_loops; k++)
        {
            curr_loop = &loop_samples[k];
            num_samples = curr_loop->num_samples;
            for (j = 0; j < num_samples; j++)
            {
                u = atan2(curr_loop->samples[j].y, curr_loop->samples[j].x);
                u = prc_map_to_two_pi(u);
                v = curr_loop->samples[j].z;

                curr_loop->uv_samples[j].x = u;
                curr_loop->uv_samples[j].y = v;
            }

            curr_loop->wind_u = prc_unwrap_angular_component(curr_loop->uv_samples, num_samples, 0);
            curr_loop->wind_v = 0;

            for (j = 0; j < num_samples; j++)
            {
                curr_loop->uv_samples[j].x = (curr_loop->uv_samples[j].x - cylinder->parameterization.u_param_coeff_b) / u_coeff_a;
                curr_loop->uv_samples[j].y = (curr_loop->uv_samples[j].y - cylinder->parameterization.v_param_coeff_b) / v_coeff_a;
#if CHECK_SURFACE_PROJECTION
                prc_vec3 test_xyz, test2_xyz;
                prc_surface_params cylinder_eval_params = { 0 };
                cylinder_eval_params.surface_params = (void *)cylinder;
                // Reconstruct the actual u and v that the surface expects after tuning:
                double eval_u = (curr_loop->uv_samples[j].x * u_coeff_a) + cylinder->parameterization.u_param_coeff_b;
                double eval_v = (curr_loop->uv_samples[j].y * v_coeff_a) + cylinder->parameterization.v_param_coeff_b;
                test_xyz = prc_evaluate_surf_cylinder(ctx, &cylinder_eval_params, eval_u, eval_v);

                /* We need to apply the transformation to curve xyz as test_xyz does have that applied it */
                if (cylinder->has_transform && !cylinder->exact_geom_transform.is_identity)
                {
                    test2_xyz = prc_exact_geom_apply_transform(ctx, &cylinder->exact_geom_transform, curr_loop->samples[j]);
                }
                else
                {
                    test2_xyz = curr_loop->samples[j];
                }
                fprintf(stderr, " PRC_TYPE_SURF_Cylinder projection: curve_sample=%u KnownSurface_xyz = (%.3f,%.3f,%.3f) Surface_uv_from_projection = (%.3f,%.3f) Surface_XYZ_from_uv = (%.3f,%.3f,%.3f) (delta=(%.3f,%.3f,%.3f)\n",
                    j, test2_xyz.x, test2_xyz.y, test2_xyz.z, curr_loop->uv_samples[j].x, curr_loop->uv_samples[j].y,
                    test_xyz.x, test_xyz.y, test_xyz.z, test2_xyz.x - test_xyz.x, test2_xyz.y - test_xyz.y, test2_xyz.z - test_xyz.z);
#endif
            }
        }
        break;
    }

    case PRC_TYPE_SURF_Sphere:
    {
        /* Use analytical projection */
        prc_surf_sphere *sphere = surface->surf_sphere;
        double u_coeff_a = (sphere->parameterization.u_param_coeff_a != 0.0) ? sphere->parameterization.u_param_coeff_a : 1.0;
        double v_coeff_a = (sphere->parameterization.v_param_coeff_a != 0.0) ? sphere->parameterization.v_param_coeff_a : 1.0;
        double u, v;
        double horizontal_radius;

        for (k = 0; k < num_loops; k++)
        {
            curr_loop = &loop_samples[k];
            num_samples = curr_loop->num_samples;
            for (j = 0; j < num_samples; j++)
            {
                /* Points are already in local coordinate space via the inverse matrix step above */
                double local_x = curr_loop->samples[j].x;
                double local_y = curr_loop->samples[j].y;
                double local_z = curr_loop->samples[j].z;

                u = atan2(local_y, local_x);
                u = prc_map_to_two_pi(u);
                horizontal_radius = sqrt(local_y * local_y + local_x * local_x);
                v = atan2(local_z, horizontal_radius);

                curr_loop->uv_samples[j].x = u;
                curr_loop->uv_samples[j].y = v;
            }

            /* v is latitude and does not wrap a full 2*pi (it runs pole
               to pole), so only u (longitude) is ever unwrapped here */
            curr_loop->wind_u = prc_unwrap_angular_component(curr_loop->uv_samples, num_samples, 0);
            curr_loop->wind_v = 0;

            for (j = 0; j < num_samples; j++)
            {
                curr_loop->uv_samples[j].x = (curr_loop->uv_samples[j].x - sphere->parameterization.u_param_coeff_b) / u_coeff_a;
                curr_loop->uv_samples[j].y = (curr_loop->uv_samples[j].y - sphere->parameterization.v_param_coeff_b) / v_coeff_a;

#if CHECK_SURFACE_PROJECTION
                prc_vec3 test_xyz;
                prc_surface_params sphere_eval_params = { 0 };
                sphere_eval_params.surface_params = (void *)sphere;
                double eval_u = (curr_loop->uv_samples[j].x * u_coeff_a) + sphere->parameterization.u_param_coeff_b;
                double eval_v = (curr_loop->uv_samples[j].y * v_coeff_a) + sphere->parameterization.v_param_coeff_b;
                test_xyz = prc_evaluate_surf_sphere(ctx, &sphere_eval_params, eval_u, eval_v);

                fprintf(stderr, " PRC_TYPE_SURF_Sphere projection: curve_sample=%u LocalSurface_xyz = (%.3f,%.3f,%.3f) Surface_uv_from_projection = (%.3f,%.3f) Surface_XYZ_from_uv = (%.3f,%.3f,%.3f) (delta=(%.3f,%.3f,%.3f)\n",
                    j, curr_loop->samples[j].x, curr_loop->samples[j].y, curr_loop->samples[j].z, curr_loop->uv_samples[j].x, curr_loop->uv_samples[j].y,
                    test_xyz.x, test_xyz.y, test_xyz.z, curr_loop->samples[j].x - test_xyz.x, curr_loop->samples[j].y - test_xyz.y, curr_loop->samples[j].z - test_xyz.z);
#endif
            }
        }
        break;
    }

    case PRC_TYPE_SURF_Torus:
    {
        /* Use analytical projection */
        prc_surf_torus *torus = surface->surf_torus;
        double u_coeff_a = (torus->parameterization.u_param_coeff_a != 0.0) ? torus->parameterization.u_param_coeff_a : 1.0;
        double v_coeff_a = (torus->parameterization.v_param_coeff_a != 0.0) ? torus->parameterization.v_param_coeff_a : 1.0;
        double minor_radius = torus->minor_radius;
        double major_radius = torus->major_radius;
        double u, v, current_radius;

        for (k = 0; k < num_loops; k++)
        {
            curr_loop = &loop_samples[k];
            num_samples = curr_loop->num_samples;
            for (j = 0; j < num_samples; j++)
            {
                u = atan2(curr_loop->samples[j].y, curr_loop->samples[j].x);
                u = prc_map_to_two_pi(u);
                current_radius =
                    sqrt(curr_loop->samples[j].y * curr_loop->samples[j].y + curr_loop->samples[j].x * curr_loop->samples[j].x);
                v = atan2(curr_loop->samples[j].z, current_radius - major_radius);
                v = prc_map_to_two_pi(v);

                curr_loop->uv_samples[j].x = u;
                curr_loop->uv_samples[j].y = v;
            }

            /* Both axes are angular on a torus, so a loop can wind
               around either (or, in principle, both -- not yet handled
               by the tessellation dispatch below) */
            curr_loop->wind_u = prc_unwrap_angular_component(curr_loop->uv_samples, num_samples, 0);
            curr_loop->wind_v = prc_unwrap_angular_component(curr_loop->uv_samples, num_samples, 1);

            /*   Align multiple loops to the same 2*PI phase period box  */
            if (k > 0)
            {
                // Use a point midway through the loops to check the true phase box alignment
                uint32_t mid_idx = num_samples / 2;
                uint32_t base_mid_idx = loop_samples[0].num_samples / 2;

                // Find the midway U position of the base loop (Loop 0)
                double base_u_mid = loop_samples[0].uv_samples[base_mid_idx].x;

                // Find the midway U position of the current loop
                double current_u_mid = curr_loop->uv_samples[mid_idx].x;

                // Calculate how many full 2*PI phases separate the bodies of these loops
                double phase_diff = current_u_mid - base_u_mid;
                double shifts = lround(phase_diff / (2.0 * PRC_PI));

                if (shifts != 0)
                {
                    // Shift all samples of this loop back into alignment with Loop 0
                    for (j = 0; j < num_samples; j++)
                    {
                        curr_loop->uv_samples[j].x -= shifts * (2.0 * PRC_PI);
                    }
                }
            }

            for (j = 0; j < num_samples; j++)
            {
                curr_loop->uv_samples[j].x = (curr_loop->uv_samples[j].x - torus->parameterization.u_param_coeff_b) / u_coeff_a;
                curr_loop->uv_samples[j].y = (curr_loop->uv_samples[j].y - torus->parameterization.v_param_coeff_b) / v_coeff_a;

#if CHECK_SURFACE_PROJECTION
                prc_vec3 test_xyz, test2_xyz;
                prc_surface_params torus_eval_params = { 0 };
                torus_eval_params.surface_params = (void *)torus;
                // Reconstruct the actual u and v that the surface expects after tuning:
                double eval_u = (curr_loop->uv_samples[j].x * u_coeff_a) + torus->parameterization.u_param_coeff_b;
                double eval_v = (curr_loop->uv_samples[j].y * v_coeff_a) + torus->parameterization.v_param_coeff_b;

                test_xyz = prc_evaluate_surf_torus(ctx, &torus_eval_params, eval_u, eval_v);

                /* We need to apply the transformation to curve xyz as test_xyz does have that applied it */
                if (torus->has_transform && !torus->exact_geom_transform.is_identity)
                {
                    test2_xyz = prc_exact_geom_apply_transform(ctx, &torus->exact_geom_transform, curr_loop->samples[j]);
                }
                else
                {
                    test2_xyz = curr_loop->samples[j];
                }
                fprintf(stderr, " PRC_TYPE_SURF_Torus projection: curve_sample=%u KnownSurface_xyz = (%.3f,%.3f,%.3f) Surface_uv_from_projection = (%.3f,%.3f) Surface_XYZ_from_uv = (%.3f,%.3f,%.3f) (delta=(%.3f,%.3f,%.3f)\n",
                    j, test2_xyz.x, test2_xyz.y, test2_xyz.z, curr_loop->uv_samples[j].x, curr_loop->uv_samples[j].y,
                    test_xyz.x, test_xyz.y, test_xyz.z, test2_xyz.x - test_xyz.x, test2_xyz.y - test_xyz.y, test2_xyz.z - test_xyz.z);
#endif
            }
        }
        break;
    }

    case PRC_TYPE_SURF_Extrusion:
    {
        /* Use numerical projection */
        prc_surf_extrusion *extrusion = surface->surf_extrusion;
        prc_surface_params extrusion_eval_params = { 0 };
        prc_curve_sampling_info curve_sampling_info;
        prc_vec3 test_xyz;

        code = prc_get_curve_sample_info(ctx, &extrusion->base_curve, &curve_sampling_info);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed in prc_get_curve_sample_info\n");
            return code;
        }
        double min_u = curve_sampling_info.start;
        double max_u = curve_sampling_info.end;
        double min_v = extrusion->parameterization.surface_domain.min_uv.y;
        double max_v = extrusion->parameterization.surface_domain.max_uv.y;

        double u = 0, v = 0;
        double u_coeff_a = (extrusion->parameterization.u_param_coeff_a != 0.0) ? extrusion->parameterization.u_param_coeff_a : 1.0;
        double v_coeff_a = (extrusion->parameterization.v_param_coeff_a != 0.0) ? extrusion->parameterization.v_param_coeff_a : 1.0;

        extrusion_eval_params.surface_params = (void *)extrusion;

        for (k = 0; k < num_loops; k++)
        {
            curr_loop = &loop_samples[k];
            num_samples = curr_loop->num_samples;
            for (j = 0; j < num_samples; j++)
            {
                uint8_t used_fallback = 0;

                if (j == 0)
                {
                    prc_project_point_onto_surface(ctx, prc_evaluate_surf_extrusion, &extrusion_eval_params,
                        min_u, max_u, min_v, max_v, curr_loop->samples[j], &u, &v);
                }
                else
                {
                    /* There is a question about the uv parameterization here...  */
                    prc_refine_point_on_surface(ctx, prc_evaluate_surf_extrusion, &extrusion_eval_params,
                        min_u, max_u, min_v, max_v, curr_loop->samples[j], u, v, &u, &v);

                    /* Check that this is a valid point. If not, then fallback onto course sample */
                    test_xyz = prc_evaluate_surf_extrusion(ctx, &extrusion_eval_params, u, v);
                    if (prc_vec_dist_between_two_points(test_xyz, curr_loop->samples[j]) > 1e-6)
                    {
                        used_fallback = 1;
                        prc_project_point_onto_surface(ctx, prc_evaluate_surf_extrusion, &extrusion_eval_params,
                            min_u, max_u, min_v, max_v, curr_loop->samples[j], &u, &v);
                    }
                }

                curr_loop->uv_samples[j].x = u;
                curr_loop->uv_samples[j].y = v;

#if CHECK_SURFACE_PROJECTION
                test_xyz = prc_evaluate_surf_extrusion(ctx, &extrusion_eval_params, u, v);
                fprintf(stderr, " PRC_TYPE_SURF_Extrusion projection: curve_sample=%u raw_uv_from_solve=(%.9f,%.9f) KnownSurface_xyz = (%.3f,%.3f,%.3f) Surface_uv_from_projection = (%.3f,%.3f) Surface_XYZ_from_uv = (%.3f,%.3f,%.3f) (delta=(%.3f,%.3f,%.3f) used_fallback=%u\n",
                    j, u, v, curr_loop->samples[j].x, curr_loop->samples[j].y, curr_loop->samples[j].z, curr_loop->uv_samples[j].x, curr_loop->uv_samples[j].y,
                    test_xyz.x, test_xyz.y, test_xyz.z,
                    curr_loop->samples[j].x - test_xyz.x, curr_loop->samples[j].y - test_xyz.y, curr_loop->samples[j].z - test_xyz.z,
                    used_fallback);
#endif
            }
        }
        break;
    }

    case PRC_TYPE_SURF_Revolution:
    {
        /* Use numerical projection */
        prc_surf_revolution *revolution = surface->surf_revolution;
        prc_surface_params revolution_eval_params = { 0 };
        prc_curve_sampling_info revolution_sampling_info;
        prc_vec3 test_xyz;

        code = prc_get_curve_sample_info(ctx, &revolution->base_curve, &revolution_sampling_info);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed in prc_get_curve_sample_info\n");
            return code;
        }

        double min_u = 0;
        double max_u = 2 * PRC_PI;
        double min_v = revolution_sampling_info.start;
        double max_v = revolution_sampling_info.end;

        double u = 0, v = 0;
        double u_coeff_a = (revolution->parameterization.u_param_coeff_a != 0.0) ? revolution->parameterization.u_param_coeff_a : 1.0;
        double v_coeff_a = (revolution->parameterization.v_param_coeff_a != 0.0) ? revolution->parameterization.v_param_coeff_a : 1.0;

        revolution_eval_params.surface_params = (void *)revolution;

        for (k = 0; k < num_loops; k++)
        {
            curr_loop = &loop_samples[k];
            num_samples = curr_loop->num_samples;
            for (j = 0; j < num_samples; j++)
            {
                uint8_t used_fallback = 0;

                if (j == 0)
                {
                    prc_project_point_onto_surface(ctx, prc_evaluate_surf_revolution, &revolution_eval_params,
                        min_u, max_u, min_v, max_v, curr_loop->samples[j], &u, &v);
                }
                else
                {
                    /* There is a question about the uv parameterization here...  */
                    prc_refine_point_on_surface(ctx, prc_evaluate_surf_revolution, &revolution_eval_params,
                        min_u, max_u, min_v, max_v, curr_loop->samples[j], u, v, &u, &v);

                    /* Check that this is a valid point. If not, then fallback onto course sample */
                    test_xyz = prc_evaluate_surf_revolution(ctx, &revolution_eval_params, u, v);
                    if (prc_vec_dist_between_two_points(test_xyz, curr_loop->samples[j]) > 1e-6)
                    {
                        used_fallback = 1;
                        prc_project_point_onto_surface(ctx, prc_evaluate_surf_revolution, &revolution_eval_params,
                            min_u, max_u, min_v, max_v, curr_loop->samples[j], &u, &v);
                    }
                }

                curr_loop->uv_samples[j].x = u;
                curr_loop->uv_samples[j].y = v;

#if CHECK_SURFACE_PROJECTION
                test_xyz = prc_evaluate_surf_revolution(ctx, &revolution_eval_params, u, v);
                fprintf(stderr, " PRC_TYPE_SURF_Revolution projection: curve_sample=%u raw_uv_from_solve=(%.9f,%.9f) KnownSurface_xyz = (%.3f,%.3f,%.3f) Surface_uv_from_projection = (%.3f,%.3f) Surface_XYZ_from_uv = (%.3f,%.3f,%.3f) (delta=(%.3f,%.3f,%.3f) used_fallback=%u\n",
                    j, u, v, curr_loop->samples[j].x, curr_loop->samples[j].y, curr_loop->samples[j].z, curr_loop->uv_samples[j].x, curr_loop->uv_samples[j].y,
                    test_xyz.x, test_xyz.y, test_xyz.z,
                    curr_loop->samples[j].x - test_xyz.x, curr_loop->samples[j].y - test_xyz.y, curr_loop->samples[j].z - test_xyz.z,
                    used_fallback);
#endif
            }
        }
        break;
    }

    case PRC_TYPE_SURF_Offset:
    {
        /* Use numerical projection */
        prc_surf_offset *offset = surface->surf_offset;
        prc_surface_params offset_eval_params = { 0 };
        prc_surface_sampling_info offset_sampling_info;
        prc_vec3 test_xyz;

        code = prc_get_surface_data(ctx, &offset->base_surface.surface, &offset_sampling_info);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed in prc_get_surface_data\n");
            return code;
        }

        double min_u = offset_sampling_info.start_u;
        double max_u = offset_sampling_info.end_u;
        double min_v = offset_sampling_info.start_v;
        double max_v = offset_sampling_info.end_v;

        double u = 0, v = 0;
        double u_coeff_a = (offset->parameterization.u_param_coeff_a != 0.0) ? offset->parameterization.u_param_coeff_a : 1.0;
        double v_coeff_a = (offset->parameterization.v_param_coeff_a != 0.0) ? offset->parameterization.v_param_coeff_a : 1.0;

        offset_eval_params.surface_params = (void *)offset;

        for (k = 0; k < num_loops; k++)
        {
            curr_loop = &loop_samples[k];
            num_samples = curr_loop->num_samples;

            for (j = 0; j < num_samples; j++)
            {
                uint8_t used_fallback = 0;

                /* Use warm start approach. Otherwise trimloop-cylindrical.pdf fails */
                if (j == 0)
                {
                    prc_project_point_onto_surface(ctx, prc_evaluate_surf_offset, &offset_eval_params,
                        min_u, max_u, min_v, max_v, curr_loop->samples[j], &u, &v);
                }
                else
                {
                    /* There is a question about the uv parameterization here...  */
                    prc_refine_point_on_surface(ctx, prc_evaluate_surf_offset, &offset_eval_params,
                        min_u, max_u, min_v, max_v, curr_loop->samples[j], u, v, &u, &v);

                    /* Check that this is a valid point. If not, then fallback onto course sample */
                    test_xyz = prc_evaluate_surf_offset(ctx, &offset_eval_params, u, v);
                    if (prc_vec_dist_between_two_points(test_xyz, curr_loop->samples[j]) > 1e-6)
                    {
                        used_fallback = 1;
                        prc_project_point_onto_surface(ctx, prc_evaluate_surf_offset, &offset_eval_params,
                            min_u, max_u, min_v, max_v, curr_loop->samples[j], &u, &v);
                    }
                }

                curr_loop->uv_samples[j].x = u;
                curr_loop->uv_samples[j].y = v;

#if CHECK_SURFACE_PROJECTION
                test_xyz = prc_evaluate_surf_offset(ctx, &offset_eval_params, u, v);
                fprintf(stderr, " PRC_TYPE_SURF_Offset projection: curve_sample=%u raw_uv_from_solve=(%.9f,%.9f) KnownSurface_xyz = (%.3f,%.3f,%.3f) Surface_uv_from_projection = (%.3f,%.3f) Surface_XYZ_from_uv = (%.3f,%.3f,%.3f) (delta=(%.3f,%.3f,%.3f) used_fallback=%u\n",
                    j, u, v, curr_loop->samples[j].x, curr_loop->samples[j].y, curr_loop->samples[j].z, curr_loop->uv_samples[j].x, curr_loop->uv_samples[j].y,
                    test_xyz.x, test_xyz.y, test_xyz.z,
                    curr_loop->samples[j].x - test_xyz.x, curr_loop->samples[j].y - test_xyz.y, curr_loop->samples[j].z - test_xyz.z,
                    used_fallback);
#endif
            }
        }
        break;
    }

    case PRC_TYPE_SURF_Transform:
    {
        /* Use numerical projection */
        prc_surf_transform *transform = surface->surf_transform;
        prc_surface_params transform_eval_params = { 0 };
        prc_surface_sampling_info transform_sampling_info;
        prc_vec3 test_xyz;

        code = prc_get_surface_data(ctx, &transform->base_surface.surface, &transform_sampling_info);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed in prc_get_surface_data\n");
            return code;
        }
        double min_u = transform_sampling_info.start_u;
        double max_u = transform_sampling_info.end_u;
        double min_v = transform_sampling_info.start_v;
        double max_v = transform_sampling_info.end_v;
        double u = 0, v = 0;
        double u_coeff_a = (transform->parameterization.u_param_coeff_a != 0.0) ? transform->parameterization.u_param_coeff_a : 1.0;
        double v_coeff_a = (transform->parameterization.v_param_coeff_a != 0.0) ? transform->parameterization.v_param_coeff_a : 1.0;

        transform_eval_params.surface_params = (void *)transform;

        for (k = 0; k < num_loops; k++)
        {
            curr_loop = &loop_samples[k];
            num_samples = curr_loop->num_samples;

            for (j = 0; j < num_samples; j++)
            {
                uint8_t used_fallback = 0;

                if (j == 0)
                {
                    prc_project_point_onto_surface(ctx, prc_evaluate_surf_transform, &transform_eval_params,
                        min_u, max_u, min_v, max_v, curr_loop->samples[j], &u, &v);
                }
                else
                {
                    /* There is a question about the uv parameterization here...  */
                    prc_refine_point_on_surface(ctx, prc_evaluate_surf_transform, &transform_eval_params,
                        min_u, max_u, min_v, max_v, curr_loop->samples[j], u, v, &u, &v);

                    /* Check that this is a valid point. If not, then fallback onto course sample */
                    test_xyz = prc_evaluate_surf_transform(ctx, &transform_eval_params, u, v);
                    if (prc_vec_dist_between_two_points(test_xyz, curr_loop->samples[j]) > 1e-6)
                    {
                        used_fallback = 1;
                        prc_project_point_onto_surface(ctx, prc_evaluate_surf_transform, &transform_eval_params,
                            min_u, max_u, min_v, max_v, curr_loop->samples[j], &u, &v);
                    }
                }

                curr_loop->uv_samples[j].x = u;
                curr_loop->uv_samples[j].y = v;

#if CHECK_SURFACE_PROJECTION
                test_xyz = prc_evaluate_surf_transform(ctx, &transform_eval_params, u, v);
                fprintf(stderr, " PRC_TYPE_SURF_Transform projection: curve_sample=%u raw_uv_from_solve=(%.9f,%.9f) KnownSurface_xyz = (%.3f,%.3f,%.3f) Surface_uv_from_projection = (%.3f,%.3f) Surface_XYZ_from_uv = (%.3f,%.3f,%.3f) (delta=(%.3f,%.3f,%.3f) used_fallback=%u\n",
                    j, u, v, curr_loop->samples[j].x, curr_loop->samples[j].y, curr_loop->samples[j].z, curr_loop->uv_samples[j].x, curr_loop->uv_samples[j].y,
                    test_xyz.x, test_xyz.y, test_xyz.z,
                    curr_loop->samples[j].x - test_xyz.x, curr_loop->samples[j].y - test_xyz.y, curr_loop->samples[j].z - test_xyz.z,
                    used_fallback);
#endif
            }
        }
        break;
    }

    case PRC_TYPE_SURF_Cylindrical:
    {
        /* Use numerical projection */
        prc_surf_cylindrical *cylindrical = surface->surf_cylindrical;
        prc_surface_params cylindrical_eval_params = { 0 };
        prc_surface_sampling_info cylindrical_sampling_info;
        prc_vec3 test_xyz;

        /* prc_evaluate_surf_cylindrical takes (u, v) in THIS surface's own
           declared parameterization and converts to the base surface's
           implicit parameter internally -- so the domain to solve/search
           within is this surface's own declared domain, not the base
           surface's (they differ whenever u/v_param_coeff_a != 1, e.g. a
           base surface with a full 0..2*pi domain declared here as
           0..pi with coeff_a 2 to recover the full circle) */
        code = prc_get_surface_data(ctx, surface, &cylindrical_sampling_info);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed in prc_get_surface_data\n");
            return code;
        }

        double min_u = cylindrical_sampling_info.start_u;
        double max_u = cylindrical_sampling_info.end_u;
        double min_v = cylindrical_sampling_info.start_v;
        double max_v = cylindrical_sampling_info.end_v;

        double u = 0, v = 0;

        cylindrical_eval_params.surface_params = (void *)cylindrical;

#if CHECK_SURFACE_PROJECTION
        {
            surface_func base_eval_func_dbg = NULL;
            void *base_params_dbg = NULL;
            int base_code_dbg = prc_get_surface_eval_func(ctx, &cylindrical->base_surface.surface, &base_eval_func_dbg, &base_params_dbg);

            fprintf(stderr, " PRC_TYPE_SURF_Cylindrical: base_surface.surface_type=%u (code=%d) domain u=[%.9f,%.9f] v=[%.9f,%.9f] u_coeff_a=%.9f u_coeff_b=%.9f v_coeff_a=%.9f v_coeff_b=%.9f\n",
                cylindrical->base_surface.surface.surface_type, base_code_dbg, min_u, max_u, min_v, max_v,
                cylindrical->parameterization.u_param_coeff_a, cylindrical->parameterization.u_param_coeff_b,
                cylindrical->parameterization.v_param_coeff_a, cylindrical->parameterization.v_param_coeff_b);
        }
#endif

        for (k = 0; k < num_loops; k++)
        {
            curr_loop = &loop_samples[k];
            num_samples = curr_loop->num_samples;
            for (j = 0; j < num_samples; j++)
            {
                uint8_t used_fallback = 0;

                /* Use warm start approach. Otherwise trimloop-cylindrical.pdf fails */
                if (j == 0)
                {
                    prc_project_point_onto_surface(ctx, prc_evaluate_surf_cylindrical, &cylindrical_eval_params,
                        min_u, max_u, min_v, max_v, curr_loop->samples[j], &u, &v);
                }
                else
                {
                    /* Seed from (u, v), still holding the previous sample's
                       solved value: prc_evaluate_surf_cylindrical now applies
                       the UVParameterization coeffs itself, so the solver
                       already works entirely in this surface's own declared
                       (u, v) space and no separate remap is needed below */
                    prc_refine_point_on_surface(ctx, prc_evaluate_surf_cylindrical, &cylindrical_eval_params,
                        min_u, max_u, min_v, max_v, curr_loop->samples[j], u, v, &u, &v);

                    /* Check that this is a valid point. If not, then fallback onto course sample */
                    test_xyz = prc_evaluate_surf_cylindrical(ctx, &cylindrical_eval_params, u, v);
                    if (prc_vec_dist_between_two_points(test_xyz, curr_loop->samples[j]) > 1e-6)
                    {
                        used_fallback = 1;
                        prc_project_point_onto_surface(ctx, prc_evaluate_surf_cylindrical, &cylindrical_eval_params,
                            min_u, max_u, min_v, max_v, curr_loop->samples[j], &u, &v);
                    }
                }

                curr_loop->uv_samples[j].x = u;
                curr_loop->uv_samples[j].y = v;

#if CHECK_SURFACE_PROJECTION
                test_xyz = prc_evaluate_surf_cylindrical(ctx, &cylindrical_eval_params, u, v);
                fprintf(stderr, " PRC_TYPE_SURF_Cylindrical projection: curve_sample=%u raw_uv_from_solve=(%.9f,%.9f) KnownSurface_xyz = (%.3f,%.3f,%.3f) Surface_uv_from_projection = (%.3f,%.3f) Surface_XYZ_from_uv = (%.3f,%.3f,%.3f) (delta=(%.3f,%.3f,%.3f) used_fallback=%u\n",
                    j, u, v, curr_loop->samples[j].x, curr_loop->samples[j].y, curr_loop->samples[j].z, curr_loop->uv_samples[j].x, curr_loop->uv_samples[j].y,
                    test_xyz.x, test_xyz.y, test_xyz.z,
                    curr_loop->samples[j].x - test_xyz.x, curr_loop->samples[j].y - test_xyz.y, curr_loop->samples[j].z - test_xyz.z,
                    used_fallback);
#endif
            }
        }
        break;
    }

    case PRC_TYPE_SURF_Plane:
    {
        /* Use analytial projection */
        /* The Z = 0 plane which these samples should already be mapped to
           with the inverse transform is local (x, y) space, not (u, v):
           prc_evaluate_surf_plane maps x = u * coeff_a + coeff_b (same for
           v/y), so that affine map has to be inverted here to recover the
           true surface parameters the tessellator evaluates against */
        prc_surf_plane *plane = surface->surf_plane;
        double u_coeff_a = (plane->u_parameter_coeff_a != 0.0) ? plane->u_parameter_coeff_a : 1.0;
        double v_coeff_a = (plane->v_parameter_coeff_a != 0.0) ? plane->v_parameter_coeff_a : 1.0;

        for (k = 0; k < num_loops; k++)
        {
            curr_loop = &loop_samples[k];
            num_samples = curr_loop->num_samples;
            for (j = 0; j < num_samples; j++)
            {
                curr_loop->uv_samples[j].x = (curr_loop->samples[j].x - plane->u_parameter_coeff_b) / u_coeff_a;
                curr_loop->uv_samples[j].y = (curr_loop->samples[j].y - plane->v_parameter_coeff_b) / v_coeff_a;
            }
        }
        break;
    }

    case PRC_TYPE_SURF_NURBS:
    {
        /* Use numerical projection */
        /* No closed-form inverse like the analytic surfaces above (cone/
           cylinder/sphere/torus can recover u,v directly from x,y,z via
           atan2/etc.) -- each 3D sample is instead projected back onto
           the surface numerically (coarse grid search + Gauss-Newton,
           prc_project_point_onto_surface), the same machinery already
           used to invert points for the Blend01/02 bound surfaces */
        prc_surf_nurbs *nurbs = surface->surf_nurbs;
        prc_surface_params nurbs_eval_params = { 0 };
        double min_u = nurbs->knot_vector_u[nurbs->du];
        double max_u = nurbs->knot_vector_u[nurbs->highest_index_of_knots_u - nurbs->du];
        double min_v = nurbs->knot_vector_v[nurbs->dv];
        double max_v = nurbs->knot_vector_v[nurbs->highest_index_of_knots_v - nurbs->dv];
        prc_vec3 test_xyz;
        double u, v;

        nurbs_eval_params.surface_params = (void *)nurbs;

        for (k = 0; k < num_loops; k++)
        {
            curr_loop = &loop_samples[k];
            num_samples = curr_loop->num_samples;
            for (j = 0; j < num_samples; j++)
            {
                uint8_t used_fallback = 0;

                if (j == 0)
                {
                    prc_project_point_onto_surface(ctx, prc_evaluate_surf_nurbs, &nurbs_eval_params,
                        min_u, max_u, min_v, max_v, curr_loop->samples[j], &u, &v);
                }
                else
                {
                    /* There is a question about the uv parameterization here...  */
                    prc_refine_point_on_surface(ctx, prc_evaluate_surf_nurbs, &nurbs_eval_params,
                        min_u, max_u, min_v, max_v, curr_loop->samples[j], u, v, &u, &v);

                    /* Check that this is a valid point. If not, then fallback onto course sample */
                    test_xyz = prc_evaluate_surf_nurbs(ctx, &nurbs_eval_params, u, v);
                    if (prc_vec_dist_between_two_points(test_xyz, curr_loop->samples[j]) > 1e-6)
                    {
                        used_fallback = 1;
                        prc_project_point_onto_surface(ctx, prc_evaluate_surf_nurbs, &nurbs_eval_params,
                            min_u, max_u, min_v, max_v, curr_loop->samples[j], &u, &v);
                    }
                }

                curr_loop->uv_samples[j].x = u;
                curr_loop->uv_samples[j].y = v;

#if CHECK_SURFACE_PROJECTION
                test_xyz = prc_evaluate_surf_nurbs(ctx, &nurbs_eval_params, u, v);
                fprintf(stderr, " PRC_TYPE_SURF_NURBS projection: curve_sample=%u raw_uv_from_solve=(%.9f,%.9f) KnownSurface_xyz = (%.3f,%.3f,%.3f) Surface_uv_from_projection = (%.3f,%.3f) Surface_XYZ_from_uv = (%.3f,%.3f,%.3f) (delta=(%.3f,%.3f,%.3f) used_fallback=%u\n",
                    j, u, v, curr_loop->samples[j].x, curr_loop->samples[j].y, curr_loop->samples[j].z, curr_loop->uv_samples[j].x, curr_loop->uv_samples[j].y,
                    test_xyz.x, test_xyz.y, test_xyz.z,
                    curr_loop->samples[j].x - test_xyz.x, curr_loop->samples[j].y - test_xyz.y, curr_loop->samples[j].z - test_xyz.z,
                    used_fallback);
#endif
            }
        }
        break;
    }

    case PRC_TYPE_SURF_Blend03:
    {
        /* Use numerical projection */
        prc_surf_blend03 *blend = surface->surf_blend03;
        prc_surface_params blend_eval_params = { 0 };
        curve_func center_eval_func = NULL;
        void *center_params = NULL;
        double min_u = blend->parameters[0];
        double max_u = blend->parameters[blend->number_of_elements - 1];
        double min_v = blend->trim_v_min;
        double max_v = blend->trim_v_max;
        double u = 0, v = 0;
        double u_coeff_a = (blend->parameterization.u_param_coeff_a != 0.0) ? blend->parameterization.u_param_coeff_a : 1.0;
        double v_coeff_a = (blend->parameterization.v_param_coeff_a != 0.0) ? blend->parameterization.v_param_coeff_a : 1.0;
        prc_vec3 test_xyz;

        blend_eval_params.surface_params = (void *)blend;
        if (min_v >= max_v)
        {
            min_v = 0;
            max_v = 1;
        }

        for (k = 0; k < num_loops; k++)
        {
            curr_loop = &loop_samples[k];
            num_samples = curr_loop->num_samples;

            for (j = 0; j < num_samples; j++)
            {
                uint8_t used_fallback = 0;

                if (j == 0)
                {
                    prc_project_point_onto_surface(ctx, prc_evaluate_surf_blend03, &blend_eval_params,
                        min_u, max_u, min_v, max_v, curr_loop->samples[j], &u, &v);
                }
                else
                {
                    /* There is a question about the uv parameterization here...  */
                    prc_refine_point_on_surface(ctx, prc_evaluate_surf_blend03, &blend_eval_params,
                        min_u, max_u, min_v, max_v, curr_loop->samples[j], u, v, &u, &v);

                    /* Check that this is a valid point. If not, then fallback onto course sample */
                    test_xyz = prc_evaluate_surf_blend03(ctx, &blend_eval_params, u, v);
                    if (prc_vec_dist_between_two_points(test_xyz, curr_loop->samples[j]) > 1e-6)
                    {
                        used_fallback = 1;
                        prc_project_point_onto_surface(ctx, prc_evaluate_surf_blend03, &blend_eval_params,
                            min_u, max_u, min_v, max_v, curr_loop->samples[j], &u, &v);
                    }
                }

                curr_loop->uv_samples[j].x = u;
                curr_loop->uv_samples[j].y = v;

#if CHECK_SURFACE_PROJECTION
                test_xyz = prc_evaluate_surf_blend03(ctx, &blend_eval_params, u, v);
                fprintf(stderr, " PRC_TYPE_SURF_Blend03 projection: curve_sample=%u raw_uv_from_solve=(%.9f,%.9f) KnownSurface_xyz = (%.3f,%.3f,%.3f) Surface_uv_from_projection = (%.3f,%.3f) Surface_XYZ_from_uv = (%.3f,%.3f,%.3f) (delta=(%.3f,%.3f,%.3f) used_fallback=%u\n",
                    j, u, v, curr_loop->samples[j].x, curr_loop->samples[j].y, curr_loop->samples[j].z, curr_loop->uv_samples[j].x, curr_loop->uv_samples[j].y,
                    test_xyz.x, test_xyz.y, test_xyz.z,
                    curr_loop->samples[j].x - test_xyz.x, curr_loop->samples[j].y - test_xyz.y, curr_loop->samples[j].z - test_xyz.z,
                    used_fallback);
#endif
            }
        }
        break;
    }

    case PRC_TYPE_SURF_Ruled:
    {
        prc_surf_ruled *ruled = surface->surf_ruled;
        prc_surface_params ruled_eval_params = { 0 };
        double min_u = 0.0;
        double max_u = 1.0;
        double min_v = 0.0;
        double max_v = 1.0;
        double u = 0, v = 0;
        double u_coeff_a = (ruled->parameterization.u_param_coeff_a != 0 ? ruled->parameterization.u_param_coeff_a : 1.0);
        double v_coeff_a = (ruled->parameterization.v_param_coeff_a != 0 ? ruled->parameterization.v_param_coeff_a : 1.0);
        curve_func curve1_eval_func = NULL;
        void *curve1_params = NULL;
        prc_vec3 test_xyz;

        code = prc_get_curve_eval_func(ctx, &ruled->first_curve, &curve1_eval_func,
            &curve1_params, &min_v, &max_v);
        if (code < 0)
        {
            prc_error(ctx, code, "Invalid center curve type in prc_map_loops_to_surface (Blend02)\n");
            return code;
        }
        ruled_eval_params.surface_params = (void *)ruled;

        for (k = 0; k < num_loops; k++)
        {
            curr_loop = &loop_samples[k];
            num_samples = curr_loop->num_samples;

            for (j = 0; j < num_samples; j++)
            {
                uint8_t used_fallback = 0;

                if (j == 0)
                {
                    prc_project_point_onto_surface(ctx, prc_evaluate_surf_ruled, &ruled_eval_params,
                        min_u, max_u, min_v, max_v, curr_loop->samples[j], &u, &v);
                }
                else
                {
                    /* There is a question about the uv parameterization here...  */
                    prc_refine_point_on_surface(ctx, prc_evaluate_surf_ruled, &ruled_eval_params,
                        min_u, max_u, min_v, max_v, curr_loop->samples[j], u, v, &u, &v);

                    /* Check that this is a valid point. If not, then fallback onto course sample */
                    test_xyz = prc_evaluate_surf_ruled(ctx, &ruled_eval_params, u, v);
                    if (prc_vec_dist_between_two_points(test_xyz, curr_loop->samples[j]) > 1e-6)
                    {
                        used_fallback = 1;
                        prc_project_point_onto_surface(ctx, prc_evaluate_surf_ruled, &ruled_eval_params,
                            min_u, max_u, min_v, max_v, curr_loop->samples[j], &u, &v);
                    }
                }

                curr_loop->uv_samples[j].x = u;
                curr_loop->uv_samples[j].y = v;

#if CHECK_SURFACE_PROJECTION
                test_xyz = prc_evaluate_surf_ruled(ctx, &ruled_eval_params, u, v);
                fprintf(stderr, " PRC_TYPE_SURF_Ruled projection: curve_sample=%u raw_uv_from_solve=(%.9f,%.9f) KnownSurface_xyz = (%.3f,%.3f,%.3f) Surface_uv_from_projection = (%.3f,%.3f) Surface_XYZ_from_uv = (%.3f,%.3f,%.3f) (delta=(%.3f,%.3f,%.3f) used_fallback=%u\n",
                    j, u, v, curr_loop->samples[j].x, curr_loop->samples[j].y, curr_loop->samples[j].z, curr_loop->uv_samples[j].x, curr_loop->uv_samples[j].y,
                    test_xyz.x, test_xyz.y, test_xyz.z,
                    curr_loop->samples[j].x - test_xyz.x, curr_loop->samples[j].y - test_xyz.y, curr_loop->samples[j].z - test_xyz.z,
                    used_fallback);
#endif
            }
        }
        break;
    }

    case PRC_TYPE_SURF_Blend02:
    {
        /* Use numerical projection */
        prc_surf_blend02 *blend = surface->surf_blend02;
        prc_surface_params blend_eval_params = { 0 };
        curve_func center_eval_func = NULL;
        void *center_params = NULL;
        double min_u = 0.0, max_u = 0.0;
        double min_v = 0.0;
        double max_v = (blend->parameterization_type == 0) ? 1.0 : (2.0 * PRC_PI);
        double u = 0, v = 0;
        double u_coeff_a = (blend->parameterization.u_param_coeff_a != 0.0) ? blend->parameterization.u_param_coeff_a : 1.0;
        double v_coeff_a = (blend->parameterization.v_param_coeff_a != 0.0) ? blend->parameterization.v_param_coeff_a : 1.0;
        prc_vec3 test_xyz;

        code = prc_get_curve_eval_func(ctx, &blend->center_curve, &center_eval_func,
            &center_params, &min_u, &max_u);
        if (code < 0)
        {
            prc_error(ctx, code, "Invalid center curve type in prc_map_loops_to_surface (Blend02)\n");
            return code;
        }

        blend_eval_params.surface_params = (void *)blend;

        for (k = 0; k < num_loops; k++)
        {
            curr_loop = &loop_samples[k];
            num_samples = curr_loop->num_samples;

            for (j = 0; j < num_samples; j++)
            {
                uint8_t used_fallback = 0;

                if (j == 0)
                {
                    prc_project_point_onto_surface(ctx, prc_evaluate_surf_blend02, &blend_eval_params,
                        min_u, max_u, min_v, max_v, curr_loop->samples[j], &u, &v);
                }
                else
                {
                    /* There is a question about the uv parameterization here...  */
                    prc_refine_point_on_surface(ctx, prc_evaluate_surf_blend02, &blend_eval_params,
                        min_u, max_u, min_v, max_v, curr_loop->samples[j], u, v, &u, &v);

                    /* Check that this is a valid point. If not, then fallback onto course sample */
                    test_xyz = prc_evaluate_surf_blend02(ctx, &blend_eval_params, u, v);
                    if (prc_vec_dist_between_two_points(test_xyz, curr_loop->samples[j]) > 1e-6)
                    {
                        used_fallback = 1;
                        prc_project_point_onto_surface(ctx, prc_evaluate_surf_blend02, &blend_eval_params,
                            min_u, max_u, min_v, max_v, curr_loop->samples[j], &u, &v);
                    }
                }

                curr_loop->uv_samples[j].x = u;
                curr_loop->uv_samples[j].y = v;

#if CHECK_SURFACE_PROJECTION
                test_xyz = prc_evaluate_surf_blend02(ctx, &blend_eval_params, u, v);
                fprintf(stderr, " PRC_TYPE_SURF_Blend02 projection: curve_sample=%u raw_uv_from_solve=(%.9f,%.9f) KnownSurface_xyz = (%.3f,%.3f,%.3f) Surface_uv_from_projection = (%.3f,%.3f) Surface_XYZ_from_uv = (%.3f,%.3f,%.3f) (delta=(%.3f,%.3f,%.3f) used_fallback=%u\n",
                    j, u, v, curr_loop->samples[j].x, curr_loop->samples[j].y, curr_loop->samples[j].z, curr_loop->uv_samples[j].x, curr_loop->uv_samples[j].y,
                    test_xyz.x, test_xyz.y, test_xyz.z,
                    curr_loop->samples[j].x - test_xyz.x, curr_loop->samples[j].y - test_xyz.y, curr_loop->samples[j].z - test_xyz.z,
                    used_fallback);
#endif
            }
        }
        break;
    }

    case PRC_TYPE_SURF_Blend01:
    {
        /* Use numerical projection */
        prc_surf_blend01 *blend = surface->surf_blend01;
        prc_surface_params blend_eval_params = { 0 };
        curve_func center_eval_func = NULL;
        void *center_params = NULL;
        double min_u = 0.0, max_u = 2.0 * PRC_PI;
        double min_v = 0.0, max_v = 1;
        double u = 0, v = 0;
        double u_coeff_a = (blend->parameterization.u_param_coeff_a != 0.0) ? blend->parameterization.u_param_coeff_a : 1.0;
        double v_coeff_a = (blend->parameterization.v_param_coeff_a != 0.0) ? blend->parameterization.v_param_coeff_a : 1.0;
        prc_vec3 test_xyz;

        /* u is the angle around the pipe; v is the center curve parameter */
        code = prc_get_curve_eval_func(ctx, &blend->center_curve, &center_eval_func,
            &center_params, &min_v, &max_v);
        if (code < 0)
        {
            prc_error(ctx, code, "Invalid center curve type in prc_get_surface_data (Blend01)\n");
            return code;
        }

        blend_eval_params.surface_params = (void *)blend;

        for (k = 0; k < num_loops; k++)
        {
            curr_loop = &loop_samples[k];
            num_samples = curr_loop->num_samples;

            for (j = 0; j < num_samples; j++)
            {
                uint8_t used_fallback = 0;

                if (j == 0)
                {
                    prc_project_point_onto_surface(ctx, prc_evaluate_surf_blend01, &blend_eval_params,
                        min_u, max_u, min_v, max_v, curr_loop->samples[j], &u, &v);
                }
                else
                {
                    /* There is a question about the uv parameterization here...  */
                    prc_refine_point_on_surface(ctx, prc_evaluate_surf_blend01, &blend_eval_params,
                        min_u, max_u, min_v, max_v, curr_loop->samples[j], u, v, &u, &v);

                    /* Check that this is a valid point. If not, then fallback onto course sample */
                    test_xyz = prc_evaluate_surf_blend01(ctx, &blend_eval_params, u, v);
                    if (prc_vec_dist_between_two_points(test_xyz, curr_loop->samples[j]) > 1e-6)
                    {
                        used_fallback = 1;
                        prc_project_point_onto_surface(ctx, prc_evaluate_surf_blend01, &blend_eval_params,
                            min_u, max_u, min_v, max_v, curr_loop->samples[j], &u, &v);
                    }
                }

                curr_loop->uv_samples[j].x = u;
                curr_loop->uv_samples[j].y = v;

#if CHECK_SURFACE_PROJECTION
                test_xyz = prc_evaluate_surf_blend01(ctx, &blend_eval_params, u, v);
                fprintf(stderr, " PRC_TYPE_SURF_Blend01 projection: curve_sample=%u raw_uv_from_solve=(%.9f,%.9f) KnownSurface_xyz = (%.3f,%.3f,%.3f) Surface_uv_from_projection = (%.3f,%.3f) Surface_XYZ_from_uv = (%.3f,%.3f,%.3f) (delta=(%.3f,%.3f,%.3f) used_fallback=%u\n",
                    j, u, v, curr_loop->samples[j].x, curr_loop->samples[j].y, curr_loop->samples[j].z, curr_loop->uv_samples[j].x, curr_loop->uv_samples[j].y,
                    test_xyz.x, test_xyz.y, test_xyz.z,
                    curr_loop->samples[j].x - test_xyz.x, curr_loop->samples[j].y - test_xyz.y, curr_loop->samples[j].z - test_xyz.z,
                    used_fallback);
#endif
            }
        }
        break;
    }

    default:
        return 0;
    }
    return 0;
}

int
prc_tessellate_surface(prc_context *ctx, prc_data *data, uint32_t shell_index,
    uint32_t face_index, prc_topo_face *topo_face,
    uint8_t orientation, prc_nano_brep_ref_data *brep_ref_data,
    prc_topo_context *topo_context)
{
    int code;
    uint32_t k, j;
    prc_surface_params surf_params = { 0 };
    surface_func surface_eval_func = NULL;
    uint32_t geom_count = data->exact_geom_tess_part_count;
    prc_surface_sampling_info sampling_info = { 0 };
    double start_u = 0.0;
    double start_v = 0.0;
    double end_u = 0.0;
    double end_v = 0.0;
    prc_exact_geom_transform exact_geom_trans;
    prc_type_surf surface = topo_face->surface_geometry.surface;
    uint32_t num_loops = topo_face->number_of_loops;
    prc_ptr_topology *loops = topo_face->loops;
    prc_loop_samples *loop_samples = NULL;
    uint8_t topo_context_behavior = 0;
    uint8_t *loop_degenerate = NULL;
    uint32_t num_degenerate_loops = 0;

    if (topo_context != NULL)
    {
        topo_context_behavior = topo_context->behavior;
    }

    /* Orientation is either 0 (opposite direction), 1 (same direction), or 2 (unknown.
       If unknown it is needed to do geometric tests to determine the correct orientation.
       The normal should point outside the material of the shell if the shell is closed
       This also sets the transformation */
    code = prc_get_surface_data(ctx, &surface, &sampling_info);
    if (code < 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Failed to get surface data in prc_tessellate_surface\n");
        return code;
    }
    start_u = sampling_info.start_u;
    start_v = sampling_info.start_v;
    end_u = sampling_info.end_u;
    end_v = sampling_info.end_v;

    /* Lets get any loops that may be associated with the surface. Loops
        are curves (or vertices -- e.g. the tip of a cone) that make cuts
        on the parametric surface. I would expect to see them primarily used
        with planar surfaces to cut things like a washer for example. These have
        to be PRC_TYPE_TOPO_Loop a vertex type is supposed to be a line with
        the same starting and ending position */
        /* Lets to a sanity check that indeed they are PRC_TYPE_TOPO_Loop. We will
            not check the is stored values as we should have already tested those */
    for (k = 0; k < num_loops; k++)
    {
        if (!loops[k].is_stored)
        {
            if (loops[k].topo->tag != PRC_TYPE_TOPO_Loop)
            {
                prc_error(ctx, PRC_ERROR_INTERNAL, "Surface loop is of wrong type\n");
                return PRC_ERROR_INTERNAL;
            }
        }
    }

    /* For each of these loops we will need to get a set of samples that
        are sufficient for us to approximate them before we can make use of them.
        Lets do that first and store the loops in a sampled form */
    if (num_loops > 0)
    {
        loop_degenerate = (uint8_t *)prc_calloc(ctx, num_loops, sizeof(uint8_t));
        if (loop_degenerate == NULL)
        {
            prc_error(ctx, PRC_ERROR_MEMORY, "Failed in allocation prc_tessellate_surface\n");
            return PRC_ERROR_MEMORY;
        }
        loop_samples = (prc_loop_samples *)prc_calloc(ctx, num_loops, sizeof(prc_loop_samples));
        if (loop_samples == NULL)
        {
            prc_error(ctx, PRC_ERROR_MEMORY, "Failed in allocation prc_tessellate_surface\n");
            return PRC_ERROR_MEMORY;
        }
        for (k = 0; k < num_loops; k++)
        {
            code = prc_sample_loop(ctx, brep_ref_data, topo_face, &loops[k],
                &loop_samples[k], &loop_degenerate[k]);
            if (code < 0)
            {
                for (j = 0; j <= k; j++)
                {
                    if (loop_samples[j].samples != NULL)
                    {
                        prc_free(ctx, loop_samples[j].samples);
                    }
                }
                prc_free(ctx, loop_samples);
                prc_error(ctx, PRC_ERROR_INTERNAL, "Failed in prc_sample_loop\n");
                return PRC_ERROR_INTERNAL;
            }
            if (loop_degenerate[k])
            {
                num_degenerate_loops++;
            }
        }

        /* Remove any degenerate loops */
        if (num_degenerate_loops > 0)
        {
            uint32_t new_num_loops = num_loops - num_degenerate_loops;

            if (new_num_loops > 0)
            {
                prc_loop_samples *new_loop_samples = (prc_loop_samples *)prc_calloc(ctx, new_num_loops, sizeof(prc_loop_samples));
                if (new_loop_samples == NULL)
                {
                    prc_error(ctx, PRC_ERROR_MEMORY, "Failed in allocation prc_tessellate_surface\n");
                    return PRC_ERROR_MEMORY;
                }
                uint32_t new_index = 0;
                for (k = 0; k < num_loops; k++)
                {
                    if (!loop_degenerate[k])
                    {
                        new_loop_samples[new_index] = loop_samples[k];
                        new_index++;
                    }
                    else
                    {
                        if (loop_samples[k].samples != NULL)
                        {
                            prc_free(ctx, loop_samples[k].samples);
                        }
                        if (loop_samples[k].uv_samples != NULL)
                        {
                            prc_free(ctx, loop_samples[k].uv_samples);
                        }
                    }
                }
                prc_free(ctx, loop_samples);
                loop_samples = new_loop_samples;
                num_loops = new_num_loops;
            }
            else
            {
                /* All the loops were degenerate */
                for (k = 0; k < num_loops; k++)
                {
                    if (loop_samples[k].samples != NULL)
                    {
                        prc_free(ctx, loop_samples[k].samples);
                    }
                    if (loop_samples[k].uv_samples != NULL)
                    {
                        prc_free(ctx, loop_samples[k].uv_samples);
                    }
                }
                prc_free(ctx, loop_samples);
                loop_samples = NULL;
                num_loops = 0;
            }
        }
        prc_free(ctx, loop_degenerate);

        /* A cone's single trim loop can detour out to the apex and back
            (full circumference, line to apex, degenerate point, line
            back) instead of the apex being its own loop -- split any such
            embedded run out into its own point loop before the uv
            mapping below, which would otherwise try to atan2 straight
            through it. No-op (and cheap) for every other case */
        code = prc_split_cone_apex_runs(ctx, &surface, &num_loops, &loop_samples);
        if (code < 0)
        {
            for (j = 0; j < num_loops; j++)
            {
                if (loop_samples[j].samples != NULL)
                {
                    prc_free(ctx, loop_samples[j].samples);
                }
            }
            prc_free(ctx, loop_samples);
            prc_error(ctx, code, "Failed in prc_split_cone_apex_runs\n");
            return code;
        }

        /* Now lets get the loops onto the parametric surface so they can
            be used as a edge boundary in the tessellation process */
        code = prc_map_loops_to_surface(ctx, topo_face, orientation, &surface,
            num_loops, loop_samples);
        if (code < 0)
        {
            for (j = 0; j < num_loops; j++)
            {
                if (loop_samples[j].samples != NULL)
                {
                    prc_free(ctx, loop_samples[j].samples);
                }
                if (loop_samples[j].uv_samples != NULL)
                {
                    prc_free(ctx, loop_samples[j].uv_samples);
                }
            }
            prc_free(ctx, loop_samples);
            prc_error(ctx, code, "Failed in prc_map_loops_to_surface\n");
            return code;
        }

        /* Now lets see if we can figure out which of these is an outer
            and which is an inner loop */
        code = prc_assign_loop_inner_outer(ctx, topo_face, orientation,
            topo_context_behavior, num_loops, loop_samples, surface.surface_type);
        if (code < 0)
        {
            for (j = 0; j < num_loops; j++)
            {
                if (loop_samples[j].samples != NULL)
                {
                    prc_free(ctx, loop_samples[j].samples);
                }
                if (loop_samples[j].uv_samples != NULL)
                {
                    prc_free(ctx, loop_samples[j].uv_samples);
                }
            }
            prc_free(ctx, loop_samples);
            prc_error(ctx, code, "Failed in prc_assign_loop_inner_outer\n");
            return code;
        }
    }
    surf_params.loop_samples = loop_samples;
    surf_params.num_loops = num_loops;

    /* --- SWAP_UV ROBUST UNIVERSAL BOUNDS FIX --- */
    if (num_loops > 0 && loop_samples != NULL)
    {
        double min_trim_u = loop_samples[0].uv_samples[0].x;
        double max_trim_u = loop_samples[0].uv_samples[0].x;
        double min_trim_v = loop_samples[0].uv_samples[0].y;
        double max_trim_v = loop_samples[0].uv_samples[0].y;

        for (k = 0; k < num_loops; k++) {
            if (loop_samples[k].is_point_loop) continue;

            uint32_t s_count = loop_samples[k].num_samples;
            for (j = 0; j < s_count; j++) {
                if (loop_samples[k].uv_samples[j].x < min_trim_u) min_trim_u = loop_samples[k].uv_samples[j].x;
                if (loop_samples[k].uv_samples[j].x > max_trim_u) max_trim_u = loop_samples[k].uv_samples[j].x;
                if (loop_samples[k].uv_samples[j].y < min_trim_v) min_trim_v = loop_samples[k].uv_samples[j].y;
                if (loop_samples[k].uv_samples[j].y > max_trim_v) max_trim_v = loop_samples[k].uv_samples[j].y;
            }
        }

        /* 1. Handle Grid U Axis:
            If U is the linear axis (either natively or due to swap_uv),
            force it safely to the trim extent boundaries. */
        if (sampling_info.u_linear) {
            sampling_info.start_u = min_trim_u;
            sampling_info.end_u = max_trim_u;
        }
        /* Otherwise, if U is non-periodic, safely tighten the boundaries */
        else if (!sampling_info.u_periodic) {
            sampling_info.start_u = fmax(sampling_info.start_u, min_trim_u);
            sampling_info.end_u = fmin(sampling_info.end_u, max_trim_u);
        }

        /* 2. Handle Grid V Axis:
            If V is the linear axis (either natively or due to swap_uv),
            force it safely to the trim extent boundaries. */
        if (sampling_info.v_linear) {
            sampling_info.start_v = min_trim_v;
            sampling_info.end_v = max_trim_v;
        }
        /* Otherwise, if V is non-periodic, safely tighten the boundaries */
        else if (!sampling_info.v_periodic) {
            sampling_info.start_v = fmax(sampling_info.start_v, min_trim_v);
            sampling_info.end_v = fmin(sampling_info.end_v, max_trim_v);
        }
    }
    /* ------------------------------------------- */

    switch (surface.surface_type)
    {
    case PRC_TYPE_SURF_FromCurves:
    {
        prc_surf_fromcurves *from_curves = surface.surf_fromcurves;
        prc_uv_parameterization params = from_curves->parameterization;

        surf_params.surface_params = (void *)from_curves;
        surface_eval_func = prc_evaluate_surf_fromcurves;
        break;
    }

    case PRC_TYPE_SURF_Cone:
    {
        prc_surf_cone *cone = surface.surf_cone;
        prc_uv_parameterization params = cone->parameterization;

        surf_params.surface_params = (void *)cone;
        surface_eval_func = prc_evaluate_surf_cone;
        break;
    }

    case PRC_TYPE_SURF_Cylinder:
    {
        prc_surf_cylinder *cylinder = surface.surf_cylinder;
        prc_uv_parameterization params = cylinder->parameterization;

        surf_params.surface_params = (void *)cylinder;
        surface_eval_func = prc_evaluate_surf_cylinder;
        break;
    }

    case PRC_TYPE_SURF_Sphere:
    {
        prc_surf_sphere *sphere = surface.surf_sphere;
        prc_uv_parameterization params = sphere->parameterization;

        surf_params.surface_params = (void *)sphere;
        surface_eval_func = prc_evaluate_surf_sphere;
        break;
    }

    case PRC_TYPE_SURF_Torus:
    {
        prc_surf_torus *torus = surface.surf_torus;
        prc_uv_parameterization params = torus->parameterization;

        surf_params.surface_params = (void *)torus;
        surface_eval_func = prc_evaluate_surf_torus;
        break;
    }

    case PRC_TYPE_SURF_Cylindrical:
    {
        prc_surf_cylindrical *cylindrical = surface.surf_cylindrical;
        prc_uv_parameterization params = cylindrical->parameterization;

        surf_params.surface_params = (void *)cylindrical;
        surface_eval_func = prc_evaluate_surf_cylindrical;
        break;
    }

    case PRC_TYPE_SURF_Extrusion:
    {
        prc_surf_extrusion *extrusion = surface.surf_extrusion;
        prc_uv_parameterization params = extrusion->parameterization;

        surf_params.surface_params = (void *)extrusion;
        surface_eval_func = prc_evaluate_surf_extrusion;
        break;
    }

    case PRC_TYPE_SURF_Revolution:
    {
        prc_surf_revolution *revolution = surface.surf_revolution;
        prc_uv_parameterization params = revolution->parameterization;

        surf_params.surface_params = (void *)revolution;
        surface_eval_func = prc_evaluate_surf_revolution;
        break;
    }

    case PRC_TYPE_SURF_Plane:
    {
        prc_surf_plane *plane = surface.surf_plane;
        prc_domain params = plane->domain;

        surf_params.surface_params = (void *)plane;
        surface_eval_func = prc_evaluate_surf_plane;
        break;
    }

    case PRC_TYPE_SURF_Offset:
    {
        prc_surf_offset *offset = surface.surf_offset;

        surf_params.surface_params = (void *)offset;
        surface_eval_func = prc_evaluate_surf_offset;
        break;
    }

    case PRC_TYPE_SURF_NURBS:
    {
        prc_surf_nurbs *nurbs = surface.surf_nurbs;

        surf_params.surface_params = (void *)nurbs;
        surface_eval_func = prc_evaluate_surf_nurbs;
        break;
    }

    case PRC_TYPE_SURF_Blend02:
    {
        prc_surf_blend02 *blend = surface.surf_blend02;

        surf_params.surface_params = (void *)blend;
        surface_eval_func = prc_evaluate_surf_blend02;
        break;
    }

    case PRC_TYPE_SURF_Blend01:
    {
        prc_surf_blend01 *blend = surface.surf_blend01;

        surf_params.surface_params = (void *)blend;
        surface_eval_func = prc_evaluate_surf_blend01;
        break;
    }

    case PRC_TYPE_SURF_Blend03:
    {
        prc_surf_blend03 *blend = surface.surf_blend03;

        surf_params.surface_params = (void *)blend;
        surface_eval_func = prc_evaluate_surf_blend03;
        break;
    }

    case PRC_TYPE_SURF_Transform:
    {
        prc_surf_transform *transform = surface.surf_transform;

        surf_params.surface_params = (void *)transform;
        surface_eval_func = prc_evaluate_surf_transform;
        break;
    }

    case PRC_TYPE_SURF_Ruled:
    {
        prc_surf_ruled *ruled = surface.surf_ruled;

        surf_params.surface_params = (void *)ruled;
        surface_eval_func = prc_evaluate_surf_ruled;
        break;
    }
    default:
        for (j = 0; j < num_loops; j++)
        {
            if (loop_samples[j].samples != NULL)
            {
                prc_free(ctx, loop_samples[j].samples);
            }
            if (loop_samples[j].uv_samples != NULL)
            {
                prc_free(ctx, loop_samples[j].uv_samples);
            }
        }
        prc_free(ctx, loop_samples);
        data->exact_geom_tess_part[data->exact_geom_tess_part_count].shells[shell_index].faces[face_index].type = PRC_EXACT_GEOM_UNKNOWN;
        return 0;
    }

    if (surface_eval_func == NULL)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "Invalid surface evaluation function in prc_tessellate_surface\n");
        return PRC_ERROR_INTERNAL;
    }

    /* A planar face bounded by loops is tessellated straight from those loop
       boundaries (ear-clipped, holes bridged in) instead of the regular grid
       below: the plane's own schema domain need not match the face at all,
       only the loops define its real outer edge and holes */
    if ((surface.surface_type == PRC_TYPE_SURF_Plane || surface.surface_type == PRC_TYPE_SURF_Ruled) &&
        num_loops > 0 && loop_samples != NULL)
    {
        code = prc_tessellate_planar_face_from_loops(ctx, data, shell_index, face_index,
            geom_count, orientation, surface_eval_func, &surf_params, &sampling_info,
            num_loops, loop_samples);
        for (j = 0; j < num_loops; j++)
        {
            if (loop_samples[j].samples != NULL)
            {
                prc_free(ctx, loop_samples[j].samples);
            }
            if (loop_samples[j].uv_samples != NULL)
            {
                prc_free(ctx, loop_samples[j].uv_samples);
            }
        }
        prc_free(ctx, loop_samples);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed in prc_tessellate_planar_face_from_loops\n");
        }
        return code;
    }

    /* A cone trimmed down to exactly its apex (a single-point loop) plus one
       loop winding once around the full circumference: an exact fan of
       rulings from the apex, see prc_tessellate_cone_apex_fan_face. Anything
       more complex (extra holes, more than one wrapping loop, etc.) falls
       through to the general grid+clip path below like before */
    if (surface.surface_type == PRC_TYPE_SURF_Cone && num_loops == 2 && loop_samples != NULL)
    {
        uint32_t point_idx = (uint32_t)-1, wrap_idx = (uint32_t)-1;

        for (k = 0; k < num_loops; k++)
        {
            if (loop_samples[k].is_point_loop)
                point_idx = k;
            else if (loop_samples[k].wind_u != 0)
                wrap_idx = k;
        }

        if (point_idx != (uint32_t)-1 && wrap_idx != (uint32_t)-1)
        {
            code = prc_tessellate_cone_apex_fan_face(ctx, data, shell_index, face_index,
                geom_count, orientation, surface_eval_func, &surf_params, &sampling_info,
                &loop_samples[point_idx], &loop_samples[wrap_idx]);
            for (j = 0; j < num_loops; j++)
            {
                if (loop_samples[j].samples != NULL)
                {
                    prc_free(ctx, loop_samples[j].samples);
                }
                if (loop_samples[j].uv_samples != NULL)
                {
                    prc_free(ctx, loop_samples[j].uv_samples);
                }
            }
            prc_free(ctx, loop_samples);
            if (code < 0)
            {
                prc_error(ctx, code, "Failed in prc_tessellate_cone_apex_fan_face\n");
            }
            return code;
        }
    }

    /* Curved periodic surfaces (Cone/Cylinder/Sphere/Torus) bounded by loops:
       classify each loop by how many times it winds around a periodic uv
       axis before closing up (0 = a genuine simple polygon in uv space, e.g.
       a hole or a self-contained trim region; nonzero = the loop is itself a
       cut across the periodic direction, e.g. a circle around a cylinder's
       circumference, which degenerates to a line rather than an enclosed
       area in uv space). NURBS is included here too -- it is never periodic
       (see prc_get_surface_data), so every loop's wind_u/wind_v is always 0
       and it always takes the num_wrapping == 0 closed-loop branch below,
       the same grid+clip machinery already used for the non-wrapping
       Cone/Cylinder/Sphere/Torus case */
    if (num_loops > 0 && loop_samples != NULL &&
        (surface.surface_type == PRC_TYPE_SURF_Cone || surface.surface_type == PRC_TYPE_SURF_Cylinder ||
            surface.surface_type == PRC_TYPE_SURF_Sphere || surface.surface_type == PRC_TYPE_SURF_Torus ||
            surface.surface_type == PRC_TYPE_SURF_NURBS || surface.surface_type == PRC_TYPE_SURF_Blend03 ||
            surface.surface_type == PRC_TYPE_SURF_Blend02 || surface.surface_type == PRC_TYPE_SURF_Blend01 ||
            surface.surface_type == PRC_TYPE_SURF_Extrusion || surface.surface_type == PRC_TYPE_SURF_FromCurves ||
            surface.surface_type == PRC_TYPE_SURF_Revolution || surface.surface_type == PRC_TYPE_SURF_Offset ||
            surface.surface_type == PRC_TYPE_SURF_Cylindrical || surface.surface_type == PRC_TYPE_SURF_Transform))
    {
        uint32_t num_wrapping = 0;
        uint8_t wrap_axis = 0;
        uint8_t mixed_axes = 0;
        uint8_t bad_winding = 0;

        for (k = 0; k < num_loops; k++)
        {
            int32_t wu = loop_samples[k].wind_u;
            int32_t wv = loop_samples[k].wind_v;

            if (wu != 0 && wv != 0)
            {
                mixed_axes = 1;
            }
            else if (wu != 0)
            {
                if (num_wrapping > 0 && wrap_axis != 0)
                    mixed_axes = 1;
                wrap_axis = 0;
                if (wu != 1 && wu != -1)
                    bad_winding = 1;
                num_wrapping++;
            }
            else if (wv != 0)
            {
                if (num_wrapping > 0 && wrap_axis != 1)
                    mixed_axes = 1;
                wrap_axis = 1;
                if (wv != 1 && wv != -1)
                    bad_winding = 1;
                num_wrapping++;
            }
        }

        if (num_wrapping == 0 || (!mixed_axes && !bad_winding && num_wrapping <= 2))
        {
            code = prc_tessellate_trimmed_face(ctx, data, shell_index, face_index,
                geom_count, orientation, surface_eval_func, &surf_params, &sampling_info,
                num_loops, loop_samples, num_wrapping > 0, wrap_axis,
                surface.surface_type == PRC_TYPE_SURF_NURBS);
            for (j = 0; j < num_loops; j++)
            {
                if (loop_samples[j].samples != NULL)
                {
                    prc_free(ctx, loop_samples[j].samples);
                }
                if (loop_samples[j].uv_samples != NULL)
                {
                    prc_free(ctx, loop_samples[j].uv_samples);
                }
            }
            prc_free(ctx, loop_samples);
            if (code < 0)
            {
                prc_error(ctx, code, "Failed in prc_tessellate_trimmed_face\n");
            }
            return code;
        }
        /* Anything else (wraps both axes, winds more than once, more than
           two wrapping loops) isn't handled yet -- fall through to the
           regular parametric grid below, same as before this loop support
           existed */
    }

    {
        prc_regular_grid grid = { 0 };
        prc_exact_geom_tess_data *tess_data;

        code = prc_build_regular_grid(ctx, surface_eval_func, &surf_params, &sampling_info, orientation, &grid);
        if (code < 0)
        {
            prc_error(ctx, code, "Failed in prc_tessellate_trimmed_face\n");
            for (j = 0; j < num_loops; j++)
            {
                if (loop_samples[j].samples != NULL)
                {
                    prc_free(ctx, loop_samples[j].samples);
                }
                if (loop_samples[j].uv_samples != NULL)
                {
                    prc_free(ctx, loop_samples[j].uv_samples);
                }
            }
            prc_free(ctx, loop_samples);
            return code;
        }

        /* Per-surface developer trace. Gated, and on stderr rather than stdout:
           it fired 7171 times over a 310-file corpus, and being on stdout it
           corrupts any consumer that emits machine-readable output there -- it
           was found by it landing in the middle of a generated CSV. The env
           lookup is cached so the hot path pays one getenv for the whole run,
           mirroring prc_debug_hooks_init in prc_decode_compressed_tess.c, and
           the whole hook compiles out when PRC_ENABLE_DIAG_ENV is OFF.
           Set PRC_DIAG_TESSELLATE_SURFACE=1 to get the old always-on output. */
        {
            static int surf_trace_read = 0;
            static int surf_trace_on = 0;

            if (!surf_trace_read)
            {
                const char *v = prc_diag_getenv("PRC_DIAG_TESSELLATE_SURFACE");
                surf_trace_read = 1;
                surf_trace_on = (v != NULL && v[0] != 0 && v[0] != '0');
            }

            if (surf_trace_on)
                fprintf(stderr, "prc_tessellate_surface: surface_type=%u orientation=%u wrap_u=%u wrap_v=%u vertex_samples_u=%u vertex_samples_v=%u start_u=%f end_u=%f start_v=%f end_v=%f\n",
                    topo_face->surface_geometry.surface.surface_type,
                    orientation,
                    grid.wrap_u,
                    grid.wrap_v,
                    grid.vertex_samples_u,
                    grid.vertex_samples_v,
                    start_u,
                    end_u,
                    start_v,
                    end_v);
        }

        data->exact_geom_tess_part[geom_count].shells[shell_index].faces[face_index].tess_data =
            (prc_exact_geom_tess_data *)prc_calloc(ctx, 1, sizeof(prc_exact_geom_tess_data));
        if (data->exact_geom_tess_part[geom_count].shells[shell_index].faces[face_index].tess_data == NULL)
        {
            prc_free(ctx, grid.uv);
            prc_free(ctx, grid.verts);
            prc_error(ctx, PRC_ERROR_MEMORY, "Allocation failure of tess_data in prc_tessellate_surface\n");
            return PRC_ERROR_MEMORY;
        }
        tess_data = data->exact_geom_tess_part[geom_count].shells[shell_index].faces[face_index].tess_data;

        tess_data->number_of_vertices = grid.vertex_samples_u * grid.vertex_samples_v;
        tess_data->vertices = grid.verts;
        prc_free(ctx, grid.uv);

        for (j = 0; j < num_loops; j++)
        {
            if (loop_samples[j].samples != NULL)
            {
                prc_free(ctx, loop_samples[j].samples);
            }
            if (loop_samples[j].uv_samples != NULL)
            {
                prc_free(ctx, loop_samples[j].uv_samples);
            }
        }
        prc_free(ctx, loop_samples);

        code = prc_emit_grid_all_triangles(ctx, &grid, orientation, &tess_data->triangles, &tess_data->number_of_triangles);
        if (code < 0)
            return code;
    }
    return 0;
}
