/**
 * quadtree.c - Quadtree Spatial Index Implementation
 * CMapNav - Terminal Navigation System
 *
 * Each internal node splits its bounding box into four equal quadrants
 * (NW, NE, SW, SE).  Splitting happens when a leaf's bucket overflows.
 *
 * Complexity:
 *   insert / nearest  : O(log n) expected on uniform data
 *   radius search     : O(k + log n) where k = results
 */

#include "quadtree.h"
#include "utils.h"
#include <stdlib.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ── Internal helpers ────────────────────────────────────── */

static QTNode *qtnode_create(BBox b) {
    QTNode *n = (QTNode *)calloc(1, sizeof(QTNode));
    if (!n) return NULL;
    n->bounds = b;
    n->count  = 0;
    return n;
}

static int qtnode_is_leaf(const QTNode *n) {
    return n->children[0] == NULL;
}

static void qtnode_split(QTNode *n) {
    double mid_lat = (n->bounds.min_lat + n->bounds.max_lat) / 2.0;
    double mid_lon = (n->bounds.min_lon + n->bounds.max_lon) / 2.0;

    /* NW */ BBox nw = { mid_lat, n->bounds.max_lat, n->bounds.min_lon, mid_lon };
    /* NE */ BBox ne = { mid_lat, n->bounds.max_lat, mid_lon, n->bounds.max_lon };
    /* SW */ BBox sw = { n->bounds.min_lat, mid_lat, n->bounds.min_lon, mid_lon };
    /* SE */ BBox se = { n->bounds.min_lat, mid_lat, mid_lon, n->bounds.max_lon };

    n->children[0] = qtnode_create(nw);
    n->children[1] = qtnode_create(ne);
    n->children[2] = qtnode_create(sw);
    n->children[3] = qtnode_create(se);

    /* Re-insert existing points into children */
    for (int i = 0; i < n->count; i++) {
        double lat = n->lats[i];
        double lon = n->lons[i];
        int    id  = n->ids[i];

        for (int c = 0; c < 4; c++) {
            QTNode *ch = n->children[c];
            if (lat >= ch->bounds.min_lat && lat <= ch->bounds.max_lat &&
                lon >= ch->bounds.min_lon && lon <= ch->bounds.max_lon) {
                ch->ids [ch->count] = id;
                ch->lats[ch->count] = lat;
                ch->lons[ch->count] = lon;
                ch->count++;
                break;
            }
        }
    }
    n->count = 0; /* internal nodes don't hold data */
}

static void qtnode_insert(QTNode *n, int id, double lat, double lon) {
    if (!n) return;

    /* Point must be within bounds (clamp edge cases) */

    if (qtnode_is_leaf(n)) {
        if (n->count < QT_BUCKET_SIZE) {
            n->ids [n->count] = id;
            n->lats[n->count] = lat;
            n->lons[n->count] = lon;
            n->count++;
        } else {
            qtnode_split(n);
            qtnode_insert(n, id, lat, lon); /* re-route after split */
        }
    } else {
        /* Descend into the appropriate quadrant */
        for (int c = 0; c < 4; c++) {
            QTNode *ch = n->children[c];
            if (lat >= ch->bounds.min_lat && lat <= ch->bounds.max_lat &&
                lon >= ch->bounds.min_lon && lon <= ch->bounds.max_lon) {
                qtnode_insert(ch, id, lat, lon);
                return;
            }
        }
        /* Fall-back: clamp to first child if on exact boundary */
        qtnode_insert(n->children[0], id, lat, lon);
    }
}

static void qtnode_destroy(QTNode *n) {
    if (!n) return;
    for (int c = 0; c < 4; c++) qtnode_destroy(n->children[c]);
    free(n);
}

/* ── Nearest-neighbour search ── */

static void qtnode_nearest(const QTNode *n, double lat, double lon,
                            int *best_id, double *best_dist) {
    if (!n) return;

    if (qtnode_is_leaf(n)) {
        for (int i = 0; i < n->count; i++) {
            double d = haversine_distance(lat, lon, n->lats[i], n->lons[i]);
            if (d < *best_dist) {
                *best_dist = d;
                *best_id   = n->ids[i];
            }
        }
        return;
    }

    /* Visit children in order of increasing min-possible distance */
    for (int c = 0; c < 4; c++) {
        QTNode *ch = n->children[c];
        if (!ch) continue;
        /* Bounding-box min distance: if best is already closer, prune */
        double clat = (lat < ch->bounds.min_lat) ? ch->bounds.min_lat :
                      (lat > ch->bounds.max_lat) ? ch->bounds.max_lat : lat;
        double clon = (lon < ch->bounds.min_lon) ? ch->bounds.min_lon :
                      (lon > ch->bounds.max_lon) ? ch->bounds.max_lon : lon;
        double min_d = haversine_distance(lat, lon, clat, clon);
        if (min_d < *best_dist)
            qtnode_nearest(ch, lat, lon, best_id, best_dist);
    }
}

/* ── Radius search ── */

static void qtnode_radius(const QTNode *n, double lat, double lon,
                           double radius_km, QTResult **list) {
    if (!n) return;

    /* Prune: compute minimum possible distance from point to bbox */
    double clat = (lat < n->bounds.min_lat) ? n->bounds.min_lat :
                  (lat > n->bounds.max_lat) ? n->bounds.max_lat : lat;
    double clon = (lon < n->bounds.min_lon) ? n->bounds.min_lon :
                  (lon > n->bounds.max_lon) ? n->bounds.max_lon : lon;
    double min_d = haversine_distance(lat, lon, clat, clon);
    if (min_d > radius_km) return; /* entire subtree too far */

    if (qtnode_is_leaf(n)) {
        for (int i = 0; i < n->count; i++) {
            double d = haversine_distance(lat, lon, n->lats[i], n->lons[i]);
            if (d <= radius_km) {
                QTResult *r = (QTResult *)malloc(sizeof(QTResult));
                if (!r) return;
                r->node_id     = n->ids[i];
                r->distance_km = d;
                r->next        = *list;
                *list          = r;
            }
        }
        return;
    }

    for (int c = 0; c < 4; c++)
        qtnode_radius(n->children[c], lat, lon, radius_km, list);
}

/* ── Public API ──────────────────────────────────────────── */

Quadtree *qt_create(double min_lat, double max_lat,
                    double min_lon, double max_lon) {
    Quadtree *qt = (Quadtree *)malloc(sizeof(Quadtree));
    if (!qt) return NULL;
    BBox b = { min_lat, max_lat, min_lon, max_lon };
    qt->root = qtnode_create(b);
    if (!qt->root) { free(qt); return NULL; }
    return qt;
}

void qt_destroy(Quadtree *qt) {
    if (!qt) return;
    qtnode_destroy(qt->root);
    free(qt);
}

void qt_insert(Quadtree *qt, int node_id, double lat, double lon) {
    if (!qt) return;
    qtnode_insert(qt->root, node_id, lat, lon);
}

int qt_nearest(const Quadtree *qt, double lat, double lon,
               double *out_dist_km) {
    if (!qt || !qt->root) return -1;
    int    best_id   = -1;
    double best_dist = DBL_MAX;
    qtnode_nearest(qt->root, lat, lon, &best_id, &best_dist);
    if (out_dist_km) *out_dist_km = best_dist;
    return best_id;
}

QTResult *qt_radius_search(const Quadtree *qt,
                            double lat, double lon,
                            double radius_km) {
    if (!qt) return NULL;
    QTResult *list = NULL;
    qtnode_radius(qt->root, lat, lon, radius_km, &list);
    return list;
}

void qt_result_free(QTResult *list) {
    while (list) {
        QTResult *tmp = list->next;
        free(list);
        list = tmp;
    }
}
