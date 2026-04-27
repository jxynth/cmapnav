/**
 * mapdata.c - CSV Map Data Loader
 * CMapNav - Terminal Navigation System
 */

#include "mapdata.h"
#include "utils.h"
#include "graph.h"
#include "quadtree.h"
#include "hashtable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Portable case-insensitive comparison */
#ifdef _WIN32
#  include <string.h>
#  define strncasecmp _strnicmp
#endif

/* ── CSV parsing helpers ─────────────────────────────────── */

/* Extract next token from *p up to delim; advances *p */
static char *csv_next(char **p, char delim) {
    char *start = *p;
    while (**p && **p != delim && **p != '\n' && **p != '\r') (*p)++;
    if (**p) { **p = '\0'; (*p)++; }
    return start;
}

/* ── Public API ──────────────────────────────────────────── */

MapContext *mapctx_create(double min_lat, double max_lat,
                          double min_lon, double max_lon) {
    MapContext *ctx = (MapContext *)malloc(sizeof(MapContext));
    if (!ctx) return NULL;

    ctx->graph    = graph_create(256);
    ctx->quadtree = qt_create(min_lat, max_lat, min_lon, max_lon);
    ctx->ht_name  = ht_create(256);

    if (!ctx->graph || !ctx->quadtree || !ctx->ht_name) {
        mapctx_destroy(ctx);
        return NULL;
    }
    return ctx;
}

void mapctx_destroy(MapContext *ctx) {
    if (!ctx) return;
    graph_destroy(ctx->graph);
    qt_destroy(ctx->quadtree);
    ht_destroy(ctx->ht_name);
    free(ctx);
}

int mapctx_load_nodes(MapContext *ctx, const char *filename) {
    if (!ctx || !filename) return 0;

    FILE *f = fopen(filename, "r");
    if (!f) { fprintf(stderr, "[!] Cannot open %s\n", filename); return 0; }

    char line[256];
    int  count = 0;

    /* Skip header if first token is not a number */
    while (fgets(line, sizeof(line), f)) {
        str_trim(line);
        if (line[0] == '#' || line[0] == '\0') continue; /* comment / blank */

        char *p = line;
        char *s_id  = csv_next(&p, ',');
        char *s_lat = csv_next(&p, ',');
        char *s_lon = csv_next(&p, ',');
        char *s_name = csv_next(&p, ',');

        /* Skip header row */
        if (s_id[0] < '0' || s_id[0] > '9') continue;

        int    id  = atoi(s_id);
        double lat = atof(s_lat);
        double lon = atof(s_lon);
        str_trim(s_name);

        Node *n = graph_add_node(ctx->graph, id, lat, lon, s_name);
        if (n) {
            qt_insert(ctx->quadtree, id, lat, lon);
            ht_insert(ctx->ht_name, s_name, id);
            count++;
        }
    }
    fclose(f);
    printf("[+] Loaded %d nodes from %s\n", count, filename);
    return count;
}

int mapctx_load_edges(MapContext *ctx, const char *filename) {
    if (!ctx || !filename) return 0;

    FILE *f = fopen(filename, "r");
    if (!f) { fprintf(stderr, "[!] Cannot open %s\n", filename); return 0; }

    char line[256];
    int  count = 0;

    while (fgets(line, sizeof(line), f)) {
        str_trim(line);
        if (line[0] == '#' || line[0] == '\0') continue;

        char *p = line;
        char *s_from  = csv_next(&p, ',');
        char *s_to    = csv_next(&p, ',');
        char *s_dist  = csv_next(&p, ',');
        char *s_name  = csv_next(&p, ',');
        char *s_speed = csv_next(&p, ',');
        char *s_type  = csv_next(&p, ',');

        if (s_from[0] < '0' || s_from[0] > '9') continue; /* skip header */

        int      from  = atoi(s_from);
        int      to    = atoi(s_to);
        double   dist  = atof(s_dist);
        int      speed = atoi(s_speed);
        RoadType rtype = ROAD_LOCAL;

        str_trim(s_name);
        str_trim(s_type);

        if      (strncmp(s_type, "0", 1) == 0 ||
                 strncasecmp(s_type, "highway",  4) == 0) rtype = ROAD_HIGHWAY;
        else if (strncmp(s_type, "1", 1) == 0 ||
                 strncasecmp(s_type, "arterial", 4) == 0) rtype = ROAD_ARTERIAL;

        /* Add single direction (the CSV stores each direction explicitly) */
        if (graph_add_edge(ctx->graph, from, to,
                           dist, s_name, speed, rtype)) count++;
    }
    fclose(f);
    printf("[+] Loaded %d edge directions from %s\n", count, filename);
    return count;
}

void mapctx_list_locations(const MapContext *ctx) {
    if (!ctx) return;
    printf("\n  Named Locations:\n");
    print_separator('-', 50);
    int shown = 0;
    for (int i = 0; i < ctx->graph->capacity; i++) {
        Node *n = ctx->graph->nodes[i];
        if (!n || n->name[0] == '\0') continue;
        printf("  [%3d] %-35s (%.4f, %.4f)\n",
               n->id, n->name, n->lat, n->lon);
        shown++;
    }
    if (shown == 0) printf("  (none)\n");
    print_separator('-', 50);
}
