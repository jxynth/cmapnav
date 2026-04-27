/**
 * main.c - CMapNav Terminal Navigation System
 * =============================================
 * A C implementation of a simplified navigation system featuring:
 *   - Adjacency-list graph for road networks
 *   - Quadtree for spatial queries
 *   - Min-heap priority queue for Dijkstra & A*
 *   - Chaining hash table for O(1) name lookup
 *   - LIVE OpenStreetMap data via Overpass API
 *
 * Build:
 *   gcc -Wall -Wextra -O2 *.c -o cmapnav -lm
 *
 * Run:
 *   ./cmapnav
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "graph.h"
#include "pathfinding.h"
#include "quadtree.h"
#include "hashtable.h"
#include "mapdata.h"
#include "osm_loader.h"
#include "utils.h"

/* ── Constants ───────────────────────────────────────────── */
#define VERSION        "2.0.0"
#define DATA_NODES     "nodes.csv"
#define DATA_EDGES     "edges.csv"
#define EXPORT_FILE    "route_export.txt"
#define HISTORY_MAX    20

/* ── Session state ───────────────────────────────────────── */
typedef struct {
    MapContext *map;
    Path       *last_route;
    int         history[HISTORY_MAX][2]; /* {from, to} */
    int         history_count;
    char        favorites[20][100];
    int         fav_count;
    RouteMode   mode;
} Session;

/* ── UI helpers ──────────────────────────────────────────── */

static void clear_screen(void) {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

static void banner(void) {
    print_separator('=', 62);
    printf("  ██████╗███╗   ███╗ █████╗ ██████╗ ███╗   ██╗ █████╗ ██╗   ██╗\n");
    printf(" ██╔════╝████╗ ████║██╔══██╗██╔══██╗████╗  ██║██╔══██╗██║   ██║\n");
    printf(" ██║     ██╔████╔██║███████║██████╔╝██╔██╗ ██║███████║██║   ██║\n");
    printf(" ██║     ██║╚██╔╝██║██╔══██║██╔═══╝ ██║╚██╗██║██╔══██║╚██╗ ██╔╝\n");
    printf(" ╚██████╗██║ ╚═╝ ██║██║  ██║██║     ██║ ╚████║██║  ██║ ╚████╔╝ \n");
    printf("  ╚═════╝╚═╝     ╚═╝╚═╝  ╚═╝╚═╝     ╚═╝  ╚═══╝╚═╝  ╚═╝  ╚═══╝  \n");
    printf("        Terminal Navigation System v%s  |  OSM Powered\n", VERSION);
    print_separator('=', 62);
}

static void read_line(char *buf, int sz) {
    if (!fgets(buf, sz, stdin)) buf[0] = '\0';
    buf[strcspn(buf, "\r\n")] = '\0';
    str_trim(buf);
}

/**
 * Resolve a location by name (exact or fuzzy).
 * Returns node_id or -1.
 */
static int resolve_location(const MapContext *ctx, const char *input) {
    int id = ht_lookup(ctx->ht_name, input);
    if (id >= 0) return id;

    char matched[100] = {0};
    id = ht_fuzzy_lookup(ctx->ht_name, input, matched, sizeof(matched));
    if (id >= 0) {
        printf("  (Did you mean '%s'? using that)\n", matched);
    }
    return id;
}

/* ═══════════════════════════════════════════════════════════
 *  Startup – choose data source
 * ══════════════════════════════════════════════════════════ */

static MapContext *startup_flow(void) {
    printf("\n");
    print_separator('-', 58);
    printf("  DATA SOURCE\n");
    print_separator('-', 58);
    printf("  1. Download LIVE data from OpenStreetMap  (requires curl)\n");
    printf("  2. Load from cached CSV files  (nodes.csv / edges.csv)\n");
    printf("  3. Use built-in sample city  (Navapur demo data)\n");
    print_separator('-', 58);
    printf("  Choice [1/2/3, default 1]: ");

    char buf[16];
    read_line(buf, sizeof(buf));

    /* ── Option 1: Live OSM download ──── */
    if (buf[0] == '\0' || buf[0] == '1') {
        MapContext *ctx = osm_interactive_download();
        if (ctx) return ctx;
        printf("  [!] OSM download failed. Falling back to CSV...\n\n");
        /* fall through to CSV */
    }

    /* ── Option 2: Load cached CSV ────── */
    if (buf[0] == '2' || buf[0] == '\0' || buf[0] == '1') {
        /* Try loading CSV with a generous bounding box */
        MapContext *ctx = mapctx_create(-90.0, 90.0, -180.0, 180.0);
        if (!ctx) return NULL;

        int nc = mapctx_load_nodes(ctx, DATA_NODES);
        int ec = mapctx_load_edges(ctx, DATA_EDGES);

        if (nc > 0) {
            printf("  [+] Loaded from cache: %d nodes, %d edges\n", nc, ec);
            return ctx;
        }
        mapctx_destroy(ctx);

        if (buf[0] == '2') {
            fprintf(stderr, "  [!] No CSV files found in current directory.\n");
            return NULL;
        }
        /* fall through to built-in sample */
    }

    /* ── Option 3: Built-in sample ────── */
    {
        printf("  Loading built-in sample city...\n");
        MapContext *ctx = mapctx_create(12.8000, 13.2000, 77.4000, 77.8000);
        if (!ctx) return NULL;

        int nc = mapctx_load_nodes(ctx, DATA_NODES);
        int ec = mapctx_load_edges(ctx, DATA_EDGES);

        if (nc == 0) {
            fprintf(stderr,
                "  [!] Sample data files not found.\n"
                "      Make sure nodes.csv and edges.csv are in the\n"
                "      current directory.\n");
            mapctx_destroy(ctx);
            return NULL;
        }
        printf("  [+] Sample ready: %d nodes, %d edges\n", nc, ec);
        return ctx;
    }
}

/* ── Menu handlers ───────────────────────────────────────── */

static void menu_search(const Session *s) {
    char buf[128];
    printf("\n  Enter location name (partial ok): ");
    read_line(buf, sizeof(buf));
    if (buf[0] == '\0') return;

    char matched[100] = {0};
    int id = ht_fuzzy_lookup(s->map->ht_name, buf, matched, sizeof(matched));
    if (id < 0) {
        printf("  [!] Location not found.\n");
        return;
    }
    Node *n = graph_get_node(s->map->graph, id);
    if (!n) return;

    printf("\n");
    print_separator('-', 50);
    printf("  Found: %s\n", n->name);
    printf("  ID  : %d\n", n->id);
    printf("  Lat : %.6f\n", n->lat);
    printf("  Lon : %.6f\n", n->lon);
    int edges = 0;
    for (Edge *e = n->edges; e; e = e->next) edges++;
    printf("  Roads connecting: %d\n", edges);
    print_separator('-', 50);
}

static void menu_plan_route(Session *s) {
    char from_buf[128], to_buf[128];
    char mode_buf[8];

    printf("\n  From location: ");
    read_line(from_buf, sizeof(from_buf));
    printf("  To   location: ");
    read_line(to_buf, sizeof(to_buf));

    int from_id = resolve_location(s->map, from_buf);
    int to_id   = resolve_location(s->map, to_buf);

    if (from_id < 0) { printf("  [!] Source not found.\n");      return; }
    if (to_id   < 0) { printf("  [!] Destination not found.\n"); return; }
    if (from_id == to_id) { printf("  [!] Same origin and destination.\n"); return; }

    printf("  Route mode [S=shortest / F=fastest] (default S): ");
    read_line(mode_buf, sizeof(mode_buf));
    RouteMode mode = (mode_buf[0] == 'F' || mode_buf[0] == 'f')
                     ? ROUTE_FASTEST : ROUTE_SHORTEST;

    printf("  Algorithm  [D=Dijkstra / A=A*] (default A): ");
    read_line(mode_buf, sizeof(mode_buf));
    int use_astar = !(mode_buf[0] == 'D' || mode_buf[0] == 'd');

    printf("\n  Computing route...\n");

    path_free(s->last_route);
    s->last_route = path_alloc();
    if (!s->last_route) { printf("  [!] Memory error.\n"); return; }

    if (use_astar)
        astar(s->map->graph, from_id, to_id, mode, s->last_route);
    else
        dijkstra(s->map->graph, from_id, to_id, mode, s->last_route);

    path_print(s->map->graph, s->last_route);

    /* Save to history */
    if (s->last_route->found && s->history_count < HISTORY_MAX) {
        s->history[s->history_count][0] = from_id;
        s->history[s->history_count][1] = to_id;
        s->history_count++;
    }
}

static void menu_nearby(const Session *s) {
    char buf[128];
    char rbuf[32];

    printf("\n  Centre location (name or leave blank for coords): ");
    read_line(buf, sizeof(buf));

    double lat, lon;
    int found = 0;

    if (buf[0] != '\0') {
        int id = resolve_location(s->map, buf);
        if (id >= 0) {
            Node *n = graph_get_node(s->map->graph, id);
            if (n) { lat = n->lat; lon = n->lon; found = 1; }
        }
    }

    if (!found) {
        printf("  Latitude  : "); fflush(stdout);
        read_line(buf, sizeof(buf)); lat = atof(buf);
        printf("  Longitude : "); fflush(stdout);
        read_line(buf, sizeof(buf)); lon = atof(buf);
        found = 1;
    }

    printf("  Search radius (km, default 2.0): ");
    read_line(rbuf, sizeof(rbuf));
    double radius = (rbuf[0] != '\0') ? atof(rbuf) : 2.0;
    if (radius <= 0) radius = 2.0;

    QTResult *results = qt_radius_search(s->map->quadtree, lat, lon, radius);
    if (!results) {
        printf("  [!] No locations found within %.1f km.\n", radius);
        return;
    }

    printf("\n  Locations within %.1f km:\n", radius);
    print_separator('-', 55);
    int cnt = 0;
    for (QTResult *r = results; r; r = r->next) {
        Node *n = graph_get_node(s->map->graph, r->node_id);
        printf("  [%5d] %-35s  %.2f km\n",
               r->node_id,
               n ? n->name : "(unknown)",
               r->distance_km);
        cnt++;
        if (cnt >= 50) { printf("  ... (showing first 50)\n"); break; }
    }
    printf("  Total: %d location(s)\n", cnt);
    print_separator('-', 55);
    qt_result_free(results);
}

static void menu_view_route(const Session *s) {
    if (!s->last_route || !s->last_route->found) {
        printf("  [!] No route computed yet. Use option 2 first.\n");
        return;
    }
    path_print(s->map->graph, s->last_route);
}

static void menu_export(const Session *s) {
    if (!s->last_route || !s->last_route->found) {
        printf("  [!] No route to export.\n");
        return;
    }
    if (path_export(s->map->graph, s->last_route, EXPORT_FILE))
        printf("  [+] Route exported to '%s'\n", EXPORT_FILE);
    else
        printf("  [!] Export failed.\n");
}

static void menu_history(const Session *s) {
    if (s->history_count == 0) {
        printf("  (No route history yet)\n");
        return;
    }
    printf("\n  Route History:\n");
    print_separator('-', 50);
    for (int i = 0; i < s->history_count; i++) {
        Node *f = graph_get_node(s->map->graph, s->history[i][0]);
        Node *t = graph_get_node(s->map->graph, s->history[i][1]);
        printf("  %2d. %-25s  ->  %s\n",
               i + 1,
               f ? f->name : "?",
               t ? t->name : "?");
    }
    print_separator('-', 50);
}

static void menu_favorites(Session *s) {
    char sub[8];
    printf("\n  Favorites: [A]dd  [L]ist  [R]emove  > ");
    read_line(sub, sizeof(sub));

    if (sub[0] == 'A' || sub[0] == 'a') {
        if (s->fav_count >= 20) { printf("  [!] Favorites list full.\n"); return; }
        char loc[100];
        printf("  Location name: ");
        read_line(loc, sizeof(loc));
        int id = resolve_location(s->map, loc);
        if (id < 0) { printf("  [!] Not found.\n"); return; }
        Node *n = graph_get_node(s->map->graph, id);
        strncpy(s->favorites[s->fav_count++], n ? n->name : loc,
                sizeof(s->favorites[0]) - 1);
        printf("  [+] Saved '%s' to favorites.\n", n ? n->name : loc);

    } else if (sub[0] == 'L' || sub[0] == 'l') {
        if (s->fav_count == 0) { printf("  (empty)\n"); return; }
        for (int i = 0; i < s->fav_count; i++)
            printf("  %2d. %s\n", i + 1, s->favorites[i]);

    } else if (sub[0] == 'R' || sub[0] == 'r') {
        char idx_s[8];
        printf("  Remove # (1-%d): ", s->fav_count);
        read_line(idx_s, sizeof(idx_s));
        int idx = atoi(idx_s) - 1;
        if (idx < 0 || idx >= s->fav_count) { printf("  [!] Invalid.\n"); return; }
        printf("  [-] Removed '%s'.\n", s->favorites[idx]);
        for (int i = idx; i < s->fav_count - 1; i++)
            strcpy(s->favorites[i], s->favorites[i+1]);
        s->fav_count--;
    }
}

static void menu_list_all(const Session *s) {
    mapctx_list_locations(s->map);
}

static void menu_graph_stats(const Session *s) {
    const Graph *g = s->map->graph;
    int edge_count = 0;
    for (int i = 0; i < g->capacity; i++) {
        Node *n = g->nodes[i];
        if (!n) continue;
        for (Edge *e = n->edges; e; e = e->next) edge_count++;
    }
    printf("\n  Graph Statistics:\n");
    print_separator('-', 55);
    printf("  Nodes (intersections)    : %d\n", g->num_nodes);
    printf("  Edge directions (roads)  : %d\n", edge_count);
    printf("  Named locations (hash)   : %d\n", s->map->ht_name->size);
    ht_print_stats(s->map->ht_name);
    print_separator('-', 55);
}

/** Download a new area from OSM, replacing current data */
static void menu_download_osm(Session *s) {
    MapContext *new_ctx = osm_interactive_download();
    if (!new_ctx) {
        printf("  [!] Download failed. Keeping current map.\n");
        return;
    }

    /* Free old data */
    path_free(s->last_route);
    s->last_route     = NULL;
    s->history_count  = 0;
    mapctx_destroy(s->map);

    s->map = new_ctx;
    printf("  [+] Map replaced successfully.\n");
}

/* ── Main menu loop ──────────────────────────────────────── */

static void print_menu(void) {
    printf("\n");
    print_separator('-', 55);
    printf("  MAIN MENU\n");
    print_separator('-', 55);
    printf("   1. Search location\n");
    printf("   2. Plan route (A → B)\n");
    printf("   3. Find nearby locations (radius search)\n");
    printf("   4. View last route details\n");
    printf("   5. Export route to file\n");
    printf("   6. Route history\n");
    printf("   7. Favorites\n");
    printf("   8. List all named locations\n");
    printf("   9. Graph statistics\n");
    printf("  10. Download new area from OSM\n");
    printf("   0. Exit\n");
    print_separator('-', 55);
    printf("  Choice: ");
}

int main(void) {
    clear_screen();
    banner();

    MapContext *ctx = startup_flow();
    if (!ctx) {
        fprintf(stderr, "\n  [!] No map data available. Exiting.\n");
        return 1;
    }

    Session session = {0};
    session.map          = ctx;
    session.last_route   = NULL;
    session.history_count = 0;
    session.fav_count    = 0;
    session.mode         = ROUTE_SHORTEST;

    /* Main loop */
    char choice[8];
    while (1) {
        print_menu();
        read_line(choice, sizeof(choice));

        /* handle "10" specially */
        if (strcmp(choice, "10") == 0) {
            menu_download_osm(&session);
            continue;
        }

        switch (choice[0]) {
            case '1': menu_search(&session);        break;
            case '2': menu_plan_route(&session);    break;
            case '3': menu_nearby(&session);        break;
            case '4': menu_view_route(&session);    break;
            case '5': menu_export(&session);        break;
            case '6': menu_history(&session);       break;
            case '7': menu_favorites(&session);     break;
            case '8': menu_list_all(&session);      break;
            case '9': menu_graph_stats(&session);   break;
            case '0':
                printf("\n  [*] Goodbye! Safe travels.\n\n");
                path_free(session.last_route);
                mapctx_destroy(session.map);
                return 0;
            default:
                printf("  [!] Invalid choice. Try again.\n");
        }
    }
}
