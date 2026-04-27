/**
 * osm_loader.h - OpenStreetMap / Overpass API Loader
 * CMapNav - Terminal Navigation System
 *
 * Downloads live road-network data from the Overpass API,
 * parses the JSON response, and populates the MapContext
 * (graph + quadtree + hash table).
 *
 * Requires `curl` to be available on the system PATH.
 * (Windows 10+ ships curl.exe; Linux/macOS have it pre-installed.)
 */

#ifndef OSM_LOADER_H
#define OSM_LOADER_H

#include "mapdata.h"

/* ── Preset areas for quick selection ────────────────────── */

typedef struct {
    const char *name;
    double south, west, north, east;
} OSMPreset;

extern const OSMPreset osm_presets[];
extern const int       osm_num_presets;

/* ── API ─────────────────────────────────────────────────── */

/**
 * Download and load OSM road network for a bounding box.
 * Creates a new MapContext (caller must free with mapctx_destroy).
 * Returns the populated MapContext, or NULL on failure.
 */
MapContext *osm_download_bbox(double south, double west,
                              double north, double east);

/**
 * Cache the current graph to CSV files (nodes.csv / edges.csv)
 * so subsequent runs can load instantly from cache.
 */
int osm_cache_to_csv(const MapContext *ctx,
                     const char *nodes_file,
                     const char *edges_file);

/** Check whether curl is available on the system */
int osm_check_curl(void);

/** Interactive: show presets, let user pick, download → MapContext* */
MapContext *osm_interactive_download(void);

#endif /* OSM_LOADER_H */
