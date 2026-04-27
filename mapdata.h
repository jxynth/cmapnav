/**
 * mapdata.h - CSV Map Data Loader
 * CMapNav - Terminal Navigation System
 */

#ifndef MAPDATA_H
#define MAPDATA_H

#include "graph.h"
#include "quadtree.h"
#include "hashtable.h"

/* Aggregated map context passed throughout the app */
typedef struct {
    Graph     *graph;
    Quadtree  *quadtree;
    HashTable *ht_name;    /* name  → node_id */
} MapContext;

/* Load nodes from CSV:  node_id,lat,lon,name */
int mapctx_load_nodes(MapContext *ctx, const char *filename);

/* Load edges from CSV:  from_id,to_id,distance_km,road_name,speed_limit[,road_type] */
int mapctx_load_edges(MapContext *ctx, const char *filename);

/* Create a fresh MapContext with a given lat/lon bounding box */
MapContext *mapctx_create(double min_lat, double max_lat,
                          double min_lon, double max_lon);

/* Free all resources */
void mapctx_destroy(MapContext *ctx);

/* List all named locations to stdout */
void mapctx_list_locations(const MapContext *ctx);

#endif /* MAPDATA_H */
