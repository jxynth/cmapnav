# CMapNav — Terminal Navigation System

A simplified Google Maps clone implemented in **pure C** using advanced data structures.  
Calculates optimal routes on a road network using Dijkstra's algorithm and A\*.

**Now powered by live OpenStreetMap data** — download real road networks for any city in the world.

---

## Features

| Feature | Detail |
|---|---|
| **Road network** | Adjacency-list graph (directed, weighted) |
| **Spatial indexing** | Quadtree — nearest-node & radius search |
| **Route planning** | Dijkstra's (guaranteed optimal) & A\* (faster in practice) |
| **Route modes** | Shortest distance **or** fastest time |
| **Name lookup** | djb2 hash table — O(1) average, fuzzy Levenshtein search |
| **Turn directions** | Bearing-based "Turn left/right/Continue" generation |
| **Live OSM data** | Overpass API — download any city's road network on demand |
| **One-way roads** | Correct handling of motorways and `oneway` OSM tags |
| **Route export** | Text file with full itinerary |
| **Session features** | Route history, favourites, live map stats |

---

## Project Structure

```
cmapnav/
├── main.c           — Terminal UI, startup flow, session management
├── graph.c/h        — Adjacency-list road network graph
├── heap.c/h         — Min-heap priority queue (decrease-key)
├── hashtable.c/h    — Chaining hash table for name → node_id
├── quadtree.c/h     — 2-D spatial index for lat/lon queries
├── pathfinding.c/h  — Dijkstra & A* algorithms, turn-by-turn
├── mapdata.c/h      — CSV loader for cached/offline data
├── json_parser.c/h  — Minimal recursive-descent JSON parser
├── osm_loader.c/h   — Overpass API client & OSM→graph converter
├── utils.c/h        — Haversine, bearing, string helpers
├── nodes.csv        — 60-node sample city (Navapur demo)
├── edges.csv        — 110+ road segments
├── Makefile         — Build configuration
└── README.md
```

---

## Compilation

### Linux / macOS

```bash
cd cmapnav
make
./cmapnav
```

### Windows — Option A: Winlibs GCC (recommended, no installer needed)

1. Download the latest **winlibs** GCC bundle:  
   https://github.com/brechtsanders/winlibs_mingw/releases/latest
2. Extract to e.g. `C:\mingw64\`
3. Open **PowerShell** and run:

```powershell
$env:Path = "C:\mingw64\bin;" + $env:Path
cd "C:\Users\JAYANTH SIA\medchain9999\cmapnav"
gcc -Wall -Wextra -O2 main.c graph.c heap.c hashtable.c quadtree.c pathfinding.c mapdata.c utils.c json_parser.c osm_loader.c -o cmapnav -lm
.\cmapnav.exe
```

### Windows — Option B: TDM-GCC (click-and-go installer)

1. Download from https://jmeubank.github.io/tdm-gcc/ and install.
2. Open a new terminal:

```bat
cd "C:\Users\JAYANTH SIA\medchain9999\cmapnav"
gcc -Wall -Wextra -O2 *.c -o cmapnav -lm
cmapnav.exe
```

### Dependencies

- **C standard library + libm** (no external packages)
- **curl** (for OSM downloads) — Windows 10+ includes `curl.exe` in System32

---

## Usage

On startup, CMapNav offers three data sources:

```
  DATA SOURCE
  ----------------------------------------------------------
  1. Download LIVE data from OpenStreetMap  (requires curl)
  2. Load from cached CSV files  (nodes.csv / edges.csv)
  3. Use built-in sample city  (Navapur demo data)
  ----------------------------------------------------------
  Choice [1/2/3, default 1]:
```

### Downloading from OpenStreetMap

Choose option 1, then pick a preset area or enter a custom bounding box:

```
  Download Road Network from OpenStreetMap
  ----------------------------------------------------------
   1. Koramangala, Bengaluru
   2. Indiranagar, Bengaluru
   3. MG Road area, Bengaluru
   4. Connaught Place, New Delhi
   5. Bandra West, Mumbai
   6. T. Nagar, Chennai
   7. Salt Lake, Kolkata
   8. Manhattan (midtown), NYC
   9. Central London, UK
  10. Custom bounding box
  ----------------------------------------------------------
```

The system will:
1. Send an Overpass QL query to `overpass-api.de`
2. Download the JSON response (~2-10 MB for a neighbourhood)
3. Parse nodes and ways, remap OSM IDs, detect one-way roads
4. Build the adjacency-list graph, quadtree, and name hash table
5. Optionally cache the result to `nodes.csv` / `edges.csv`

### Main Menu

```
  MAIN MENU
  -------------------------------------------------------
   1. Search location
   2. Plan route (A → B)
   3. Find nearby locations (radius search)
   4. View last route details
   5. Export route to file
   6. Route history
   7. Favorites
   8. List all named locations
   9. Graph statistics
  10. Download new area from OSM
   0. Exit
  -------------------------------------------------------
```

### Example Session (Live OSM Data)

```
Choice: 2
From location: MG Road
To   location: Koramangala
(Did you mean 'koramangala 1st block / 80 feet road'? using that)
Route mode [S=shortest / F=fastest]: F
Algorithm  [D=Dijkstra / A=A*]: A

============================================================
  Route: MG Road  -->  Koramangala 1st Block / 80 Feet Road
  Total Distance : 4.83 km
  Estimated Time : 7 min
------------------------------------------------------------
  Directions:
   1. Head South on MG Road (0.3 km, ~0 min)
   2. Turn right onto Hosur Road (1.8 km, ~2 min)
   3. Continue onto Inner Ring Road (1.2 km, ~1 min)
   4. Turn left onto 80 Feet Road (1.5 km, ~3 min) — Arrive
============================================================
```

---

## How OSM Integration Works

### Overpass API Query

CMapNav constructs this Overpass QL query for a bounding box:

```
[out:json][timeout:120];
(
  way["highway"~"^(motorway|motorway_link|trunk|trunk_link
    |primary|primary_link|secondary|secondary_link
    |tertiary|tertiary_link|residential|unclassified
    |living_street)$"](south,west,north,east);
);
out body;
>;
out skel qt;
```

This returns all road **ways** (with tags) and their constituent **nodes** (with coordinates).

### Processing Pipeline

1. **JSON Parsing**: The response is parsed by the built-in recursive-descent JSON parser
2. **ID Remapping**: OSM node IDs (64-bit, e.g. `2876543210`) are mapped to compact 0-based integers via an open-addressing hash table
3. **Node Creation**: Each OSM node becomes a graph vertex with lat/lon
4. **Edge Creation**: Consecutive nodes within each way become graph edges with:
   - Distance computed via haversine formula
   - Speed limit from `maxspeed` tag (or defaults based on road type)
   - Road type classification (highway/arterial/local)
5. **One-way Handling**: Checks `oneway=yes/-1` and implicit motorway direction
6. **Intersection Naming**: Way endpoints get named after their road; junctions get combined names like `"MG Road / Hosur Road"`

---

## Algorithm Complexity

| Algorithm | Time | Space | Notes |
|---|---|---|---|
| **Dijkstra** | O((V + E) log V) | O(V) | Guaranteed optimal |
| **A\*** | O((V + E) log V) worst | O(V) | Sub-linear with haversine heuristic |
| **Quadtree insert** | O(log n) expected | O(n) | Splits at 8-item bucket |
| **Quadtree nearest** | O(log n) expected | O(1) | BBox pruning |
| **Quadtree radius** | O(k + log n) | O(k) | k = results |
| **Hash table insert/lookup** | O(1) average | O(n) | djb2 + chaining |
| **Fuzzy search** | O(n × \|key\|) | O(\|key\|) | Levenshtein distance |
| **OSM ID remap** | O(1) average | O(n) | Open-addressing hash |
| **JSON parse** | O(n) | O(n) | n = file size |

---

## Data File Format (CSV Cache)

### nodes.csv

```
node_id,lat,lon,name
0,12.9716,77.5946,City Centre Square
1,12.9750,77.5960,North Gate Junction
```

### edges.csv

```
from_id,to_id,distance_km,road_name,speed_limit,road_type
0,1,0.4,MG Road,60,1
```

`road_type`: `0` = highway, `1` = arterial, `2` = local

---

## Memory Management

Every allocation has a corresponding `free()`.  
On Linux, verify with valgrind:

```bash
make valgrind
```

---

## License

MIT — free for educational and personal use.
