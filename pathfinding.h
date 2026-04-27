/**
 * pathfinding.h - Dijkstra & A* Route Planning
 * CMapNav - Terminal Navigation System
 */

#ifndef PATHFINDING_H
#define PATHFINDING_H

#include "graph.h"

/* One step in a route */
typedef struct Step {
    int    from_id;
    int    to_id;
    double distance_km;
    double time_min;
    char   road_name[64];
    char   instruction[128]; /* "Turn right onto Highway 45" */
} Step;

/* A complete route result */
typedef struct {
    int    *node_ids;      /* ordered list of node IDs along the path */
    int     num_nodes;     /* length of node_ids */
    Step   *steps;         /* turn-by-turn directions */
    int     num_steps;
    double  total_distance_km;
    double  total_time_min;
    int     found;         /* 1 if a path was found */
} Path;

/* Weight mode for route planning */
typedef enum {
    ROUTE_SHORTEST = 0,  /* minimise distance */
    ROUTE_FASTEST  = 1   /* minimise travel time */
} RouteMode;

/* ── API ─────────────────────────────────────────────────── */
Path *path_alloc(void);
void  path_free(Path *p);

/**
 * Dijkstra's algorithm – guaranteed optimal on non-negative weights.
 * Complexity: O((V + E) log V) with binary heap.
 */
void dijkstra(Graph *g, int start_id, int end_id,
              RouteMode mode, Path *result);

/**
 * A* search – uses haversine heuristic; faster in practice than Dijkstra
 * when the destination is geographically distant.
 * Complexity: O((V + E) log V) worst-case, sub-linear in practice.
 */
void astar(Graph *g, int start_id, int end_id,
           RouteMode mode, Path *result);

/* Print formatted turn-by-turn directions to stdout */
void path_print(const Graph *g, const Path *p);

/* Export route to a text file */
int  path_export(const Graph *g, const Path *p, const char *filename);

#endif /* PATHFINDING_H */
