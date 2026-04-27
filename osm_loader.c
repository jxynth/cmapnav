/**
 * osm_loader.c - OpenStreetMap / Overpass API Loader
 * CMapNav - Terminal Navigation System
 *
 * Flow:
 *  1. Write an Overpass QL query to a temp file
 *  2. POST it with system `curl` → _osm_data.json
 *  3. Parse the JSON with json_parser
 *  4. Pass 1 — collect all "node" elements → graph nodes
 *  5. Pass 2 — process all "way" elements → graph edges
 *  6. Name intersection nodes after their roads
 *  7. Optionally cache to CSV
 *
 * OSM node IDs are 64-bit integers (e.g. 2876543210).
 * We remap them to compact 0-based integers using an
 * open-addressing hash table (IDMap).
 */

#include "osm_loader.h"
#include "json_parser.h"
#include "graph.h"
#include "quadtree.h"
#include "hashtable.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ── Preset areas ────────────────────────────────────────── */

const OSMPreset osm_presets[] = {
    { "Koramangala, Bengaluru",      12.9200, 77.6100, 12.9500, 77.6400 },
    { "Indiranagar, Bengaluru",      12.9650, 77.6300, 12.9900, 77.6600 },
    { "MG Road area, Bengaluru",     12.9650, 77.5900, 12.9850, 77.6200 },
    { "Connaught Place, New Delhi",  28.6250, 77.2050, 28.6450, 77.2300 },
    { "Bandra West, Mumbai",         19.0450, 72.8200, 19.0700, 72.8450 },
    { "T. Nagar, Chennai",           13.0300, 80.2200, 13.0550, 80.2500 },
    { "Salt Lake, Kolkata",          22.5600, 88.3850, 22.5900, 88.4200 },
    { "Manhattan (midtown), NYC",    40.7480, -73.9900, 40.7620, -73.9700 },
    { "Central London, UK",          51.5050, -0.1350, 51.5200, -0.1100 },
};
const int osm_num_presets = (int)(sizeof(osm_presets) / sizeof(osm_presets[0]));

/* ═══════════════════════════════════════════════════════════
 *  OSM Node-ID → Compact-ID hash map  (open addressing)
 * ══════════════════════════════════════════════════════════ */

typedef struct {
    long long osm_id;
    int       compact_id;
    int       occupied;
} IDEntry;

typedef struct {
    IDEntry *entries;
    int      capacity;
    int      count;       /* next compact_id to assign */
} IDMap;

static IDMap *idmap_create(int cap) {
    if (cap < 256) cap = 256;
    IDMap *m = (IDMap *)calloc(1, sizeof(IDMap));
    if (!m) return NULL;
    m->entries  = (IDEntry *)calloc((size_t)cap, sizeof(IDEntry));
    m->capacity = cap;
    m->count    = 0;
    return m;
}

static void idmap_destroy(IDMap *m) {
    if (!m) return;
    free(m->entries);
    free(m);
}

static unsigned int idmap_hash(long long key, int cap) {
    unsigned long long k = (unsigned long long)key;
    k ^= (k >> 33);
    k *= 0xff51afd7ed558ccdULL;
    k ^= (k >> 33);
    return (unsigned int)(k % (unsigned long long)cap);
}

/* Insert raw entry (used during rehash) */
static void idmap_raw_put(IDEntry *arr, int cap,
                          long long osm_id, int cid) {
    unsigned int h = idmap_hash(osm_id, cap);
    for (int i = 0; i < cap; i++) {
        int idx = (int)((h + (unsigned int)i) % (unsigned int)cap);
        if (!arr[idx].occupied) {
            arr[idx].osm_id     = osm_id;
            arr[idx].compact_id = cid;
            arr[idx].occupied   = 1;
            return;
        }
    }
}

static void idmap_grow(IDMap *m) {
    int new_cap    = m->capacity * 2;
    IDEntry *old   = m->entries;
    int old_cap    = m->capacity;

    m->entries  = (IDEntry *)calloc((size_t)new_cap, sizeof(IDEntry));
    m->capacity = new_cap;

    for (int i = 0; i < old_cap; i++) {
        if (old[i].occupied)
            idmap_raw_put(m->entries, new_cap, old[i].osm_id, old[i].compact_id);
    }
    free(old);
}

/** Get compact id for an OSM id, or -1 if not present */
static int idmap_get(const IDMap *m, long long osm_id) {
    unsigned int h = idmap_hash(osm_id, m->capacity);
    for (int i = 0; i < m->capacity; i++) {
        int idx = (int)((h + (unsigned int)i) % (unsigned int)m->capacity);
        if (!m->entries[idx].occupied) return -1;
        if (m->entries[idx].osm_id == osm_id)
            return m->entries[idx].compact_id;
    }
    return -1;
}

/** Get existing or assign new compact id. Returns compact id. */
static int idmap_get_or_insert(IDMap *m, long long osm_id) {
    /* grow if load factor > 0.5 */
    if (m->count * 2 >= m->capacity) idmap_grow(m);

    unsigned int h = idmap_hash(osm_id, m->capacity);
    for (int i = 0; i < m->capacity; i++) {
        int idx = (int)((h + (unsigned int)i) % (unsigned int)m->capacity);
        if (!m->entries[idx].occupied) {
            int cid = m->count++;
            m->entries[idx].osm_id     = osm_id;
            m->entries[idx].compact_id = cid;
            m->entries[idx].occupied   = 1;
            return cid;
        }
        if (m->entries[idx].osm_id == osm_id)
            return m->entries[idx].compact_id;
    }
    return -1; /* should never happen after grow */
}

/* ═══════════════════════════════════════════════════════════
 *  Road classification helpers
 * ══════════════════════════════════════════════════════════ */

static int default_speed(const char *hw) {
    if (!hw) return 50;
    if (strstr(hw, "motorway"))    return 120;
    if (strstr(hw, "trunk"))       return 100;
    if (strcmp(hw, "primary") == 0 ||
        strcmp(hw, "primary_link") == 0) return 80;
    if (strstr(hw, "secondary"))   return 60;
    if (strstr(hw, "tertiary"))    return 50;
    if (strcmp(hw, "residential") == 0 ||
        strcmp(hw, "living_street") == 0) return 30;
    return 40;
}

static RoadType classify_road(const char *hw) {
    if (!hw) return ROAD_LOCAL;
    if (strstr(hw, "motorway") || strstr(hw, "trunk"))
        return ROAD_HIGHWAY;
    if (strstr(hw, "primary") || strstr(hw, "secondary"))
        return ROAD_ARTERIAL;
    return ROAD_LOCAL;
}

/* ═══════════════════════════════════════════════════════════
 *  Core download + parse
 * ══════════════════════════════════════════════════════════ */

int osm_check_curl(void) {
#ifdef _WIN32
    int r = system("where curl >nul 2>nul");
#else
    int r = system("which curl >/dev/null 2>&1");
#endif
    return r == 0;
}

MapContext *osm_download_bbox(double south, double west,
                              double north, double east) {
    /* ── 1. Write Overpass query ─────────────────────────── */
    const char *qfile = "_overpass_query.tmp";
    const char *jfile = "_osm_data.json";

    FILE *qf = fopen(qfile, "w");
    if (!qf) { perror("fopen query"); return NULL; }

    fprintf(qf,
        "[out:json][timeout:120];\n"
        "(\n"
        "  way[\"highway\"~\"^(motorway|motorway_link|trunk|trunk_link"
        "|primary|primary_link|secondary|secondary_link"
        "|tertiary|tertiary_link|residential|unclassified"
        "|living_street)$\"](%.6f,%.6f,%.6f,%.6f);\n"
        ");\n"
        "out body;\n"
        ">;\n"
        "out skel qt;\n",
        south, west, north, east);
    fclose(qf);

    /* ── 2. Download with system curl ────────────────────── */
    printf("\n  [*] Querying Overpass API for bbox (%.4f,%.4f,%.4f,%.4f)...\n",
           south, west, north, east);
    printf("      This may take 30-90 seconds for large areas.\n");
    fflush(stdout);

    char cmd[512];
    snprintf(cmd, sizeof(cmd),
#ifdef _WIN32
             "curl.exe -s --max-time 180 -o \"%s\" "
             "-X POST -d @\"%s\" "
             "\"https://overpass-api.de/api/interpreter\"",
#else
             "curl -s --max-time 180 -o '%s' "
             "-X POST -d @'%s' "
             "'https://overpass-api.de/api/interpreter'",
#endif
             jfile, qfile);

    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "  [!] curl failed (exit %d). Is curl installed?\n", rc);
        return NULL;
    }

    /* ── 3. Parse JSON ───────────────────────────────────── */
    JValue *root = json_parse_file(jfile);
    if (!root) {
        fprintf(stderr, "  [!] Failed to parse JSON.\n");
        return NULL;
    }

    JValue *elements = json_get(root, "elements");
    if (!elements || elements->type != JV_ARRAY || json_length(elements) == 0) {
        fprintf(stderr, "  [!] No elements in the Overpass response.\n");
        fprintf(stderr, "      The area may be too small or the query timed out.\n");
        json_free(root);
        return NULL;
    }

    int n = json_length(elements);
    printf("  [+] Received %d elements from OSM.\n", n);

    /* ── 4. Create data structures ───────────────────────── */
    MapContext *ctx = mapctx_create(south - 0.01, north + 0.01,
                                   west  - 0.01, east  + 0.01);
    if (!ctx) { json_free(root); return NULL; }

    IDMap *idmap = idmap_create(n > 2048 ? n : 4096);
    if (!idmap) { json_free(root); mapctx_destroy(ctx); return NULL; }

    /* ── 5. Pass 1 — nodes ───────────────────────────────── */
    int node_cnt = 0;
    for (int i = 0; i < n; i++) {
        JValue *el = json_at(elements, i);
        const char *type = json_string(json_get(el, "type"), "");
        if (strcmp(type, "node") != 0) continue;

        long long osm_id = json_int(json_get(el, "id"), -1);
        double lat = json_number(json_get(el, "lat"), 0.0);
        double lon = json_number(json_get(el, "lon"), 0.0);
        if (osm_id < 0) continue;

        int cid = idmap_get_or_insert(idmap, osm_id);

        char name_buf[100];
        snprintf(name_buf, sizeof(name_buf), "Intersection #%d", cid);

        graph_add_node(ctx->graph, cid, lat, lon, name_buf);
        qt_insert(ctx->quadtree, cid, lat, lon);
        node_cnt++;
    }
    printf("  [+] Mapped %d OSM nodes → compact IDs 0..%d\n",
           node_cnt, idmap->count - 1);

    /* ── 6. Pass 2 — ways → edges ────────────────────────── */
    int edge_cnt = 0, way_cnt = 0;
    for (int i = 0; i < n; i++) {
        JValue *el = json_at(elements, i);
        const char *type = json_string(json_get(el, "type"), "");
        if (strcmp(type, "way") != 0) continue;

        JValue *jnodes = json_get(el, "nodes");
        JValue *jtags  = json_get(el, "tags");
        if (!jnodes || jnodes->type != JV_ARRAY) continue;

        const char *highway = json_string(json_get(jtags, "highway"), "road");
        const char *w_name  = json_string(json_get(jtags, "name"),    "");
        const char *oneway_s= json_string(json_get(jtags, "oneway"),  "no");
        const char *maxsp_s = json_string(json_get(jtags, "maxspeed"), "");

        int speed    = atoi(maxsp_s);
        if (speed <= 0) speed = default_speed(highway);
        RoadType rt  = classify_road(highway);

        int is_oneway  = (strcmp(oneway_s, "yes") == 0 ||
                          strcmp(oneway_s, "1")   == 0);
        int is_reverse = (strcmp(oneway_s, "-1")  == 0);

        /* motorways are implicitly one-way */
        if (strstr(highway, "motorway") && !is_reverse) is_oneway = 1;

        char road_name[64];
        if (w_name[0] != '\0')
            safe_strcpy(road_name, w_name, sizeof(road_name));
        else
            snprintf(road_name, sizeof(road_name), "%s", highway);

        int wlen = json_length(jnodes);
        for (int j = 0; j < wlen - 1; j++) {
            long long from_osm = json_int(json_at(jnodes, j),     -1);
            long long to_osm   = json_int(json_at(jnodes, j + 1), -1);

            int from_cid = idmap_get(idmap, from_osm);
            int to_cid   = idmap_get(idmap, to_osm);
            if (from_cid < 0 || to_cid < 0) continue;

            Node *nf = graph_get_node(ctx->graph, from_cid);
            Node *nt = graph_get_node(ctx->graph, to_cid);
            if (!nf || !nt) continue;

            double dist = haversine_distance(nf->lat, nf->lon,
                                             nt->lat, nt->lon);

            /* forward direction */
            if (!is_reverse) {
                graph_add_edge(ctx->graph, from_cid, to_cid,
                               dist, road_name, speed, rt);
                edge_cnt++;
            }
            /* backward direction (if not one-way forward) */
            if (!is_oneway) {
                graph_add_edge(ctx->graph, to_cid, from_cid,
                               dist, road_name, speed, rt);
                edge_cnt++;
            }
        }

        /* ── Name the endpoints of named ways ────────────── */
        if (w_name[0] != '\0') {
            /* first node of way */
            long long first_osm = json_int(json_at(jnodes, 0), -1);
            int first_cid = (first_osm >= 0) ? idmap_get(idmap, first_osm) : -1;
            if (first_cid >= 0) {
                Node *nd = graph_get_node(ctx->graph, first_cid);
                if (nd) {
                    if (strncmp(nd->name, "Intersection #", 14) == 0) {
                        safe_strcpy(nd->name, w_name, sizeof(nd->name));
                    } else {
                        /* append as intersection name */
                        char combined[100];
                        snprintf(combined, sizeof(combined),
                                 "%.40s / %.40s", nd->name, w_name);
                        safe_strcpy(nd->name, combined, sizeof(nd->name));
                    }
                    ht_insert(ctx->ht_name, nd->name, first_cid);
                }
            }

            /* last node of way */
            long long last_osm = json_int(json_at(jnodes, wlen - 1), -1);
            int last_cid = (last_osm >= 0) ? idmap_get(idmap, last_osm) : -1;
            if (last_cid >= 0 && last_cid != first_cid) {
                Node *nd = graph_get_node(ctx->graph, last_cid);
                if (nd) {
                    if (strncmp(nd->name, "Intersection #", 14) == 0) {
                        safe_strcpy(nd->name, w_name, sizeof(nd->name));
                    } else {
                        char combined[100];
                        snprintf(combined, sizeof(combined),
                                 "%.40s / %.40s", nd->name, w_name);
                        safe_strcpy(nd->name, combined, sizeof(nd->name));
                    }
                    ht_insert(ctx->ht_name, nd->name, last_cid);
                }
            }
        }

        way_cnt++;
    }

    printf("  [+] Processed %d ways → %d edge directions\n", way_cnt, edge_cnt);
    printf("  [+] Named locations in hash table: %d\n", ctx->ht_name->size);

    /* ── Cleanup ─────────────────────────────────────────── */
    json_free(root);
    idmap_destroy(idmap);
    remove(qfile);
    /* keep _osm_data.json as cache */

    return ctx;
}

/* ═══════════════════════════════════════════════════════════
 *  CSV Cache Export
 * ══════════════════════════════════════════════════════════ */

int osm_cache_to_csv(const MapContext *ctx,
                     const char *nodes_file,
                     const char *edges_file) {
    if (!ctx || !ctx->graph) return 0;

    /* ── nodes.csv ───────────────────────────────────────── */
    FILE *nf = fopen(nodes_file, "w");
    if (!nf) { perror("fopen nodes"); return 0; }
    fprintf(nf, "node_id,lat,lon,name\n");
    int ncnt = 0;
    for (int i = 0; i < ctx->graph->capacity; i++) {
        Node *n = ctx->graph->nodes[i];
        if (!n) continue;
        fprintf(nf, "%d,%.7f,%.7f,%s\n", n->id, n->lat, n->lon, n->name);
        ncnt++;
    }
    fclose(nf);

    /* ── edges.csv ───────────────────────────────────────── */
    FILE *ef = fopen(edges_file, "w");
    if (!ef) { perror("fopen edges"); return 0; }
    fprintf(ef, "from_id,to_id,distance_km,road_name,speed_limit,road_type\n");
    int ecnt = 0;
    for (int i = 0; i < ctx->graph->capacity; i++) {
        Node *n = ctx->graph->nodes[i];
        if (!n) continue;
        for (Edge *e = n->edges; e; e = e->next) {
            fprintf(ef, "%d,%d,%.4f,%s,%d,%d\n",
                    n->id, e->dest_id, e->distance,
                    e->road_name, e->speed_limit, e->road_type);
            ecnt++;
        }
    }
    fclose(ef);

    printf("  [+] Cached %d nodes → %s\n", ncnt, nodes_file);
    printf("  [+] Cached %d edges → %s\n", ecnt, edges_file);
    return 1;
}

/* ═══════════════════════════════════════════════════════════
 *  Interactive preset selector
 * ══════════════════════════════════════════════════════════ */

static void read_ln(char *buf, int sz) {
    if (!fgets(buf, sz, stdin)) buf[0] = '\0';
    buf[strcspn(buf, "\r\n")] = '\0';
    str_trim(buf);
}

MapContext *osm_interactive_download(void) {
    printf("\n");
    print_separator('=', 58);
    printf("  Download Road Network from OpenStreetMap\n");
    print_separator('-', 58);

    for (int i = 0; i < osm_num_presets; i++)
        printf("  %2d. %s\n", i + 1, osm_presets[i].name);
    printf("  %2d. Custom bounding box\n", osm_num_presets + 1);
    print_separator('-', 58);

    printf("  Choice: ");
    char buf[64];
    read_ln(buf, sizeof(buf));
    int choice = atoi(buf);

    double s, w, n, e;

    if (choice >= 1 && choice <= osm_num_presets) {
        const OSMPreset *p = &osm_presets[choice - 1];
        s = p->south; w = p->west; n = p->north; e = p->east;
        printf("  Selected: %s\n", p->name);
    } else {
        printf("  Enter bounding box (south,west,north,east):\n");
        printf("    South latitude  : "); read_ln(buf, sizeof(buf)); s = atof(buf);
        printf("    West  longitude : "); read_ln(buf, sizeof(buf)); w = atof(buf);
        printf("    North latitude  : "); read_ln(buf, sizeof(buf)); n = atof(buf);
        printf("    East  longitude : "); read_ln(buf, sizeof(buf)); e = atof(buf);
    }

    if (n <= s || e <= w) {
        fprintf(stderr, "  [!] Invalid bounding box.\n");
        return NULL;
    }

    if (!osm_check_curl()) {
        fprintf(stderr,
            "  [!] 'curl' not found on PATH.\n"
            "      Windows 10+ should have curl.exe in System32.\n"
            "      On Linux: sudo apt install curl\n");
        return NULL;
    }

    MapContext *ctx = osm_download_bbox(s, w, n, e);

    if (ctx) {
        char ans[8];
        printf("\n  Save as CSV cache for faster future loads? [Y/n] ");
        read_ln(ans, sizeof(ans));
        if (ans[0] != 'n' && ans[0] != 'N') {
            osm_cache_to_csv(ctx, "nodes.csv", "edges.csv");
        }
    }

    return ctx;
}
