/**
 * graph.c - Road Network Graph Implementation
 * CMapNav - Terminal Navigation System
 */

#include "graph.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>

/* ── Internal helpers ────────────────────────────────────── */

/**
 * Grow the nodes array when more capacity is needed.
 * Doubles capacity each time (amortised O(1) insertion).
 */
static int graph_grow(Graph *g) {
    int new_cap = g->capacity * 2;
    Node **tmp = (Node **)realloc(g->nodes, (size_t)new_cap * sizeof(Node *));
    if (!tmp) return 0;
    /* zero out the new slots */
    memset(tmp + g->capacity, 0, (size_t)(new_cap - g->capacity) * sizeof(Node *));
    g->nodes    = tmp;
    g->capacity = new_cap;
    return 1;
}

/* ── Public API ──────────────────────────────────────────── */

Graph *graph_create(int initial_capacity) {
    if (initial_capacity <= 0) initial_capacity = 64;

    Graph *g = (Graph *)malloc(sizeof(Graph));
    if (!g) return NULL;

    g->nodes = (Node **)calloc((size_t)initial_capacity, sizeof(Node *));
    if (!g->nodes) { free(g); return NULL; }

    g->num_nodes = 0;
    g->capacity  = initial_capacity;
    return g;
}

void graph_destroy(Graph *g) {
    if (!g) return;
    for (int i = 0; i < g->capacity; i++) {
        Node *n = g->nodes[i];
        if (!n) continue;
        /* free edge linked list */
        Edge *e = n->edges;
        while (e) {
            Edge *tmp = e->next;
            free(e);
            e = tmp;
        }
        free(n);
    }
    free(g->nodes);
    free(g);
}

Node *graph_add_node(Graph *g, int id, double lat, double lon, const char *name) {
    if (!g || id < 0) return NULL;

    /* grow array if the id would be out of bounds */
    while (id >= g->capacity) {
        if (!graph_grow(g)) return NULL;
    }

    /* re-use existing slot only if it is genuinely empty */
    if (g->nodes[id]) return g->nodes[id]; /* already present */

    Node *n = (Node *)calloc(1, sizeof(Node));
    if (!n) return NULL;

    n->id       = id;
    n->lat      = lat;
    n->lon      = lon;
    strncpy(n->name, name, sizeof(n->name) - 1);
    n->edges     = NULL;
    n->visited   = 0;
    n->dist      = DBL_MAX;
    n->parent_id = -1;

    g->nodes[id] = n;
    g->num_nodes++;
    return n;
}

int graph_add_edge(Graph *g, int from_id, int to_id,
                   double distance, const char *road_name,
                   int speed_limit, RoadType road_type) {
    if (!g) return 0;
    Node *from = graph_get_node(g, from_id);
    Node *to   = graph_get_node(g, to_id);
    if (!from || !to) return 0;

    Edge *e = (Edge *)malloc(sizeof(Edge));
    if (!e) return 0;

    e->dest_id    = to_id;
    e->distance   = distance;
    strncpy(e->road_name, road_name, sizeof(e->road_name) - 1);
    e->speed_limit = speed_limit;
    e->road_type   = road_type;
    e->next        = from->edges;  /* prepend - O(1) */
    from->edges    = e;
    return 1;
}

Node *graph_get_node(const Graph *g, int id) {
    if (!g || id < 0 || id >= g->capacity) return NULL;
    return g->nodes[id];
}

void graph_reset_search(Graph *g) {
    if (!g) return;
    for (int i = 0; i < g->capacity; i++) {
        Node *n = g->nodes[i];
        if (!n) continue;
        n->visited   = 0;
        n->dist      = DBL_MAX;
        n->parent_id = -1;
    }
}

void graph_print(const Graph *g) {
    if (!g) return;
    printf("Graph (%d nodes):\n", g->num_nodes);
    for (int i = 0; i < g->capacity; i++) {
        Node *n = g->nodes[i];
        if (!n) continue;
        printf("  [%d] %-30s (%.5f, %.5f)\n", n->id, n->name, n->lat, n->lon);
        for (Edge *e = n->edges; e; e = e->next) {
            const char *type_str[] = {"HWY", "ART", "LOC"};
            printf("       -> [%d] %-20s %.2f km  %d km/h  %s\n",
                   e->dest_id, e->road_name,
                   e->distance, e->speed_limit,
                   type_str[e->road_type]);
        }
    }
}
