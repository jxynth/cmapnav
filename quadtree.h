/**
 * quadtree.h - 2D Spatial Index (Quadtree)
 * CMapNav - Terminal Navigation System
 *
 * Partitions the (lat, lon) plane for efficient:
 *   - "find nearest node" queries
 *   - "all nodes within radius R" queries
 */

#ifndef QUADTREE_H
#define QUADTREE_H

/* Axis-aligned bounding box in lat/lon space */
typedef struct {
    double min_lat, max_lat;
    double min_lon, max_lon;
} BBox;

/* Maximum nodes per leaf before splitting */
#define QT_BUCKET_SIZE 8

/* Results list for radius searches */
typedef struct QTResult {
    int           node_id;
    double        distance_km; /* approximate great-circle distance */
    struct QTResult *next;
} QTResult;

typedef struct QTNode {
    BBox           bounds;

    /* Leaf data (used when children == NULL) */
    int            ids[QT_BUCKET_SIZE];
    double         lats[QT_BUCKET_SIZE];
    double         lons[QT_BUCKET_SIZE];
    int            count;

    /* Children: NW, NE, SW, SE */
    struct QTNode *children[4];
} QTNode;

typedef struct {
    QTNode *root;
} Quadtree;

/* ── API ─────────────────────────────────────────────────── */
Quadtree  *qt_create(double min_lat, double max_lat,
                     double min_lon, double max_lon);
void       qt_destroy(Quadtree *qt);

void       qt_insert(Quadtree *qt, int node_id, double lat, double lon);

/* Returns node_id of nearest point (-1 on empty tree) */
int        qt_nearest(const Quadtree *qt, double lat, double lon,
                      double *out_dist_km);

/* Allocates a linked list of all nodes within radius_km; caller frees */
QTResult  *qt_radius_search(const Quadtree *qt,
                             double lat, double lon,
                             double radius_km);

void       qt_result_free(QTResult *list);

#endif /* QUADTREE_H */
