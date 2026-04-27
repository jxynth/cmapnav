/**
 * pathfinding.c - Dijkstra & A* Implementation
 * CMapNav - Terminal Navigation System
 *
 * Both algorithms share the same path-reconstruction logic.
 * The only difference is the priority key:
 *   Dijkstra : key = g(v)             (cost so far)
 *   A*       : key = g(v) + h(v)      (cost so far + heuristic)
 *
 * Edge weight (for RouteMode):
 *   SHORTEST → distance (km)
 *   FASTEST  → time (minutes) = distance / speed_limit * 60
 */

#include "pathfinding.h"
#include "heap.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <stdio.h>
#include <math.h>

/* ── Weight helpers ──────────────────────────────────────── */

static inline double edge_weight(const Edge *e, RouteMode mode) {
    if (mode == ROUTE_FASTEST)
        return travel_time_min(e->distance, e->speed_limit);
    return e->distance;
}

/* Heuristic for A*: straight-line lower bound in the chosen mode */
static double heuristic(const Graph *g, int v_id, int goal_id, RouteMode mode) {
    Node *v    = graph_get_node(g, v_id);
    Node *goal = graph_get_node(g, goal_id);
    if (!v || !goal) return 0.0;
    double dist_km = haversine_distance(v->lat, v->lon, goal->lat, goal->lon);
    if (mode == ROUTE_FASTEST) {
        /* Assume best possible speed = 120 km/h (highway) */
        return travel_time_min(dist_km, 120);
    }
    return dist_km;
}

/* ── Path reconstruction ─────────────────────────────────── */

/**
 * Walk parent_ids[] backwards from end→start, reversing to get
 * start→end order.  Then build Step directions.
 */
static void reconstruct_path(Graph *g, int start_id, int end_id,
                              int *parent_ids,      /* parent_ids[node_id] */
                              double *g_costs,
                              RouteMode mode,
                              Path *result) {
    /* Count path length */
    int len = 0;
    int cur = end_id;
    while (cur != -1) { len++; cur = parent_ids[cur]; }

    result->node_ids  = (int *)malloc((size_t)len * sizeof(int));
    result->steps     = (Step *)malloc((size_t)(len)  * sizeof(Step));
    if (!result->node_ids || !result->steps) return;

    /* Fill node_ids in reverse */
    cur = end_id;
    for (int i = len - 1; i >= 0; i--) {
        result->node_ids[i] = cur;
        cur = parent_ids[cur];
    }
    result->num_nodes = len;

    /* Build steps */
    result->num_steps = 0;
    result->total_distance_km = 0.0;
    result->total_time_min    = 0.0;

    for (int i = 0; i < len - 1; i++) {
        int u = result->node_ids[i];
        int v = result->node_ids[i+1];

        Node *nu = graph_get_node(g, u);
        Node *nv = graph_get_node(g, v);
        if (!nu || !nv) continue;

        /* Find the matching edge */
        Edge *best_edge = NULL;
        for (Edge *e = nu->edges; e; e = e->next) {
            if (e->dest_id == v) { best_edge = e; break; }
        }

        Step *s = &result->steps[result->num_steps++];
        s->from_id = u;
        s->to_id   = v;

        if (best_edge) {
            s->distance_km = best_edge->distance;
            s->time_min    = travel_time_min(best_edge->distance,
                                              best_edge->speed_limit);
            safe_strcpy(s->road_name, best_edge->road_name,
                        sizeof(s->road_name));
        } else {
            /* Fallback: haversine */
            s->distance_km = haversine_distance(nu->lat, nu->lon,
                                                nv->lat, nv->lon);
            s->time_min    = travel_time_min(s->distance_km, 50);
            safe_strcpy(s->road_name, "Unknown Rd", sizeof(s->road_name));
        }

        result->total_distance_km += s->distance_km;
        result->total_time_min    += s->time_min;

        /* Generate instruction */
        double b = bearing(nu->lat, nu->lon, nv->lat, nv->lon);
        if (i == 0) {
            snprintf(s->instruction, sizeof(s->instruction),
                     "Head %s on %s", compass_direction(b), s->road_name);
        } else {
            int prev_u = result->node_ids[i-1];
            Node *np   = graph_get_node(g, prev_u);
            if (np) {
                double prev_b = bearing(np->lat, np->lon, nu->lat, nu->lon);
                double delta  = b - prev_b;
                if (delta < -180) delta += 360;
                if (delta >  180) delta -= 360;

                const char *turn;
                if (delta > 45  && delta <= 135)  turn = "Turn right onto";
                else if (delta < -45 && delta >= -135) turn = "Turn left onto";
                else if (fabs(delta) > 135)        turn = "Make a U-turn onto";
                else                               turn = "Continue onto";

                snprintf(s->instruction, sizeof(s->instruction),
                         "%s %s", turn, s->road_name);
            } else {
                snprintf(s->instruction, sizeof(s->instruction),
                         "Continue on %s", s->road_name);
            }
        }
    }

    /* Final "arrive" step */
    if (result->num_steps > 0) {
        Node *dest = graph_get_node(g, end_id);
        Step *last = &result->steps[result->num_steps - 1];
        snprintf(last->instruction, sizeof(last->instruction),
                 "%s — Arrive at %s",
                 last->instruction,
                 dest ? dest->name : "destination");
    }

    result->found = 1;
}

/* ── Core search (shared by Dijkstra and A*) ─────────────── */

static void search_core(Graph *g, int start_id, int end_id,
                         RouteMode mode, int use_astar, Path *result) {
    if (!g || !result) return;
    result->found = 0;

    Node *start = graph_get_node(g, start_id);
    Node *end   = graph_get_node(g, end_id);
    if (!start || !end) return;

    graph_reset_search(g);

    int max_id = g->capacity;

    /* g_cost[v] = best known cost from start to v */
    double *g_cost    = (double *)malloc((size_t)max_id * sizeof(double));
    int    *parent_id = (int    *)malloc((size_t)max_id * sizeof(int));
    if (!g_cost || !parent_id) { free(g_cost); free(parent_id); return; }

    for (int i = 0; i < max_id; i++) {
        g_cost[i]    = DBL_MAX;
        parent_id[i] = -1;
    }
    g_cost[start_id] = 0.0;

    MinHeap *heap = heap_create(g->num_nodes + 1, max_id);
    if (!heap) { free(g_cost); free(parent_id); return; }

    double h0 = use_astar ? heuristic(g, start_id, end_id, mode) : 0.0;
    heap_insert(heap, start_id, g_cost[start_id] + h0);

    while (!heap_empty(heap)) {
        double cur_f;
        int u = heap_extract_min(heap, &cur_f);
        if (u == end_id) break;

        Node *nu = graph_get_node(g, u);
        if (!nu) continue;

        for (Edge *e = nu->edges; e; e = e->next) {
            int v = e->dest_id;
            if (v < 0 || v >= max_id) continue;
            if (!graph_get_node(g, v)) continue;

            double new_g = g_cost[u] + edge_weight(e, mode);
            if (new_g < g_cost[v]) {
                g_cost[v]    = new_g;
                parent_id[v] = u;
                double f = new_g + (use_astar ? heuristic(g, v, end_id, mode) : 0.0);
                if (heap_contains(heap, v))
                    heap_decrease_key(heap, v, f);
                else
                    heap_insert(heap, v, f);
            }
        }
    }

    if (g_cost[end_id] < DBL_MAX)
        reconstruct_path(g, start_id, end_id, parent_id, g_cost, mode, result);

    heap_destroy(heap);
    free(g_cost);
    free(parent_id);
}

/* ── Public API ──────────────────────────────────────────── */

Path *path_alloc(void) {
    Path *p = (Path *)calloc(1, sizeof(Path));
    return p;
}

void path_free(Path *p) {
    if (!p) return;
    free(p->node_ids);
    free(p->steps);
    free(p);
}

void dijkstra(Graph *g, int start_id, int end_id,
              RouteMode mode, Path *result) {
    search_core(g, start_id, end_id, mode, 0 /* no heuristic */, result);
}

void astar(Graph *g, int start_id, int end_id,
           RouteMode mode, Path *result) {
    search_core(g, start_id, end_id, mode, 1 /* use heuristic */, result);
}

void path_print(const Graph *g, const Path *p) {
    if (!p || !p->found) {
        printf("  [!] No route found.\n");
        return;
    }

    Node *start = graph_get_node(g, p->node_ids[0]);
    Node *end   = graph_get_node(g, p->node_ids[p->num_nodes - 1]);

    print_separator('=', 60);
    printf("  Route: %s  -->  %s\n",
           start ? start->name : "?",
           end   ? end->name   : "?");
    printf("  Total Distance : %.2f km\n", p->total_distance_km);
    printf("  Estimated Time : %.0f min\n", p->total_time_min);
    print_separator('-', 60);
    printf("  Directions:\n");

    for (int i = 0; i < p->num_steps; i++) {
        Step *s = &p->steps[i];
        if (i < p->num_steps - 1)
            printf("  %2d. %s (%.1f km, ~%.0f min)\n",
                   i + 1, s->instruction, s->distance_km, s->time_min);
        else
            printf("  %2d. %s\n", i + 1, s->instruction);
    }
    print_separator('=', 60);
}

int path_export(const Graph *g, const Path *p, const char *filename) {
    if (!p || !filename) return 0;
    FILE *f = fopen(filename, "w");
    if (!f) { perror("fopen"); return 0; }

    Node *start = p->num_nodes > 0 ? graph_get_node(g, p->node_ids[0]) : NULL;
    Node *end   = p->num_nodes > 0 ?
                  graph_get_node(g, p->node_ids[p->num_nodes-1]) : NULL;

    fprintf(f, "CMapNav Route Export\n");
    fprintf(f, "====================\n");
    if (p->found) {
        fprintf(f, "From  : %s\n", start ? start->name : "?");
        fprintf(f, "To    : %s\n", end   ? end->name   : "?");
        fprintf(f, "Dist  : %.2f km\n", p->total_distance_km);
        fprintf(f, "Time  : %.0f min\n\n", p->total_time_min);
        fprintf(f, "Turn-by-turn Directions\n");
        fprintf(f, "-----------------------\n");
        for (int i = 0; i < p->num_steps; i++) {
            Step *s = &p->steps[i];
            fprintf(f, "%2d. %s", i+1, s->instruction);
            if (i < p->num_steps - 1)
                fprintf(f, "  (%.1f km)", s->distance_km);
            fprintf(f, "\n");
        }
    } else {
        fprintf(f, "No route found.\n");
    }
    fclose(f);
    return 1;
}
