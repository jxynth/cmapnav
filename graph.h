/**
 * graph.h - Road Network Graph (Adjacency List)
 * CMapNav - Terminal Navigation System
 *
 * Represents the map as a directed weighted graph where:
 *   - Nodes = intersections/locations
 *   - Edges = road segments with distance, speed, type
 */

#ifndef GRAPH_H
#define GRAPH_H

#include <stddef.h>

/* ── Road Classification ─────────────────────────────────── */
typedef enum {
    ROAD_HIGHWAY  = 0,
    ROAD_ARTERIAL = 1,
    ROAD_LOCAL    = 2
} RoadType;

/* ── Edge: one directed road segment ─────────────────────── */
typedef struct Edge {
    int            dest_id;        /* destination node id   */
    double         distance;       /* km                    */
    char           road_name[64];
    int            speed_limit;    /* km/h                  */
    RoadType       road_type;
    struct Edge   *next;           /* linked-list chain     */
} Edge;

/* ── Node: intersection / named location ─────────────────── */
typedef struct Node {
    int     id;
    double  lat, lon;
    char    name[100];
    Edge   *edges;               /* adjacency list head     */

    /* pathfinding scratch fields */
    int     visited;
    double  dist;                /* tentative shortest dist */
    int     parent_id;           /* for path reconstruction */
} Node;

/* ── Graph container ──────────────────────────────────────── */
typedef struct {
    Node  **nodes;               /* array of pointers (sparse ok) */
    int     num_nodes;
    int     capacity;
} Graph;

/* ── API ─────────────────────────────────────────────────── */
Graph *graph_create(int initial_capacity);
void   graph_destroy(Graph *g);

Node  *graph_add_node(Graph *g, int id, double lat, double lon, const char *name);
int    graph_add_edge(Graph *g, int from_id, int to_id,
                      double distance, const char *road_name,
                      int speed_limit, RoadType road_type);

Node  *graph_get_node(const Graph *g, int id);
void   graph_print(const Graph *g);

/* Reset all scratch fields before a fresh pathfinding run */
void   graph_reset_search(Graph *g);

#endif /* GRAPH_H */
