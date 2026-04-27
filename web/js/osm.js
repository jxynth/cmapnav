/**
 * osm.js — Overpass API Client & OSM→Graph Builder
 * CMapNav Web Frontend
 *
 * Mirrors osm_loader.c from the C engine.
 * Fetches road-network JSON from the Overpass API,
 * remaps OSM node IDs, and populates a Graph.
 */

import { Graph } from './graph.js';

/* ── Preset areas ────────────────────────────────────────── */

export const PRESETS = [
    { name: 'Koramangala, Bengaluru',     south: 12.9200, west: 77.6100, north: 12.9500, east: 77.6400 },
    { name: 'Indiranagar, Bengaluru',     south: 12.9650, west: 77.6300, north: 12.9900, east: 77.6600 },
    { name: 'MG Road area, Bengaluru',    south: 12.9650, west: 77.5900, north: 12.9850, east: 77.6200 },
    { name: 'Connaught Place, New Delhi', south: 28.6250, west: 77.2050, north: 28.6450, east: 77.2300 },
    { name: 'Bandra West, Mumbai',        south: 19.0450, west: 72.8200, north: 19.0700, east: 72.8450 },
    { name: 'T. Nagar, Chennai',          south: 13.0300, west: 80.2200, north: 13.0550, east: 80.2500 },
    { name: 'Salt Lake, Kolkata',         south: 22.5600, west: 88.3850, north: 22.5900, east: 88.4200 },
    { name: 'Manhattan (midtown), NYC',   south: 40.7480, west: -73.9900, north: 40.7620, east: -73.9700 },
    { name: 'Central London, UK',         south: 51.5050, west: -0.1350, north: 51.5200, east: -0.1100 },
    { name: 'Shibuya, Tokyo',             south: 35.6550, west: 139.6900, north: 35.6700, east: 139.7100 },
    { name: 'Le Marais, Paris',           south: 48.8530, west: 2.3480,  north: 48.8620, east: 2.3650 },
];

/* ── Road classification helpers ─────────────────────────── */

function defaultSpeed(highway) {
    if (!highway) return 50;
    if (highway.includes('motorway'))    return 120;
    if (highway.includes('trunk'))       return 100;
    if (highway === 'primary' || highway === 'primary_link') return 80;
    if (highway.includes('secondary'))   return 60;
    if (highway.includes('tertiary'))    return 50;
    if (highway === 'residential' || highway === 'living_street') return 30;
    return 40;
}

function classifyRoad(highway) {
    if (!highway) return 2;
    if (highway.includes('motorway') || highway.includes('trunk'))    return 0; // highway
    if (highway.includes('primary')  || highway.includes('secondary')) return 1; // arterial
    return 2; // local
}

/* Road-type color mapping for map rendering */
export function roadColor(roadType) {
    switch (roadType) {
        case 0: return '#ff9500'; // highway — orange
        case 1: return '#4cc9f0'; // arterial — cyan
        case 2: return '#7b8794'; // local — grey
        default: return '#7b8794';
    }
}

export function roadWeight(roadType) {
    switch (roadType) {
        case 0: return 4;
        case 1: return 3;
        case 2: return 2;
        default: return 2;
    }
}

/* ── Overpass query builder ───────────────────────────────── */

function buildQuery(south, west, north, east) {
    return `[out:json][timeout:120];
(
  way["highway"~"^(motorway|motorway_link|trunk|trunk_link|primary|primary_link|secondary|secondary_link|tertiary|tertiary_link|residential|unclassified|living_street)$"](${south},${west},${north},${east});
);
out body;
>;
out skel qt;`;
}

/* ── Fetch & parse ───────────────────────────────────────── */

/**
 * Download OSM road data for a bounding box and build a Graph.
 *
 * @param {number} south
 * @param {number} west
 * @param {number} north
 * @param {number} east
 * @param {(msg: string) => void} onProgress — status callback
 * @returns {Promise<{ graph: Graph, bounds: [number,number,number,number], waySegments: Array }>}
 */
export async function fetchOSMData(south, west, north, east, onProgress = () => {}) {
    onProgress('Building Overpass query...');

    const query = buildQuery(south, west, north, east);
    const url   = 'https://overpass-api.de/api/interpreter';

    onProgress('Downloading from Overpass API — this may take 30–90 s...');

    const res = await fetch(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: 'data=' + encodeURIComponent(query),
    });

    if (!res.ok) {
        throw new Error(`Overpass API error: ${res.status} ${res.statusText}`);
    }

    onProgress('Parsing JSON response...');
    const json = await res.json();

    const elements = json.elements;
    if (!elements || elements.length === 0) {
        throw new Error('No elements returned. Try a larger area.');
    }

    onProgress(`Processing ${elements.length} elements...`);

    const graph = new Graph();
    const osmToCompact = new Map();  // OSM id → compact id
    let nextId = 0;

    // Collect way segments for road-network overlay
    const waySegments = [];

    // ── Pass 1: Nodes ──
    for (const el of elements) {
        if (el.type !== 'node') continue;
        const cid = nextId++;
        osmToCompact.set(el.id, cid);
        graph.addNode(cid, el.lat, el.lon, `Intersection #${cid}`);
    }

    onProgress(`Mapped ${osmToCompact.size} nodes. Processing ways...`);

    // ── Pass 2: Ways → Edges ──
    let edgeCount = 0;
    for (const el of elements) {
        if (el.type !== 'way') continue;
        if (!el.nodes || el.nodes.length < 2) continue;

        const tags = el.tags || {};
        const highway  = tags.highway || 'road';
        const wayName  = tags.name || '';
        const onewayS  = tags.oneway || 'no';
        const maxspS   = tags.maxspeed || '';

        let speed = parseInt(maxspS, 10);
        if (!speed || speed <= 0) speed = defaultSpeed(highway);

        const rt = classifyRoad(highway);

        const isOneway  = onewayS === 'yes' || onewayS === '1';
        const isReverse = onewayS === '-1';
        const implicitOneway = highway.includes('motorway') && !isReverse;

        const roadName = wayName || highway;

        // Walk consecutive node pairs
        const nodeCoords = []; // for overlay
        for (let j = 0; j < el.nodes.length; j++) {
            const osmId = el.nodes[j];
            const cid = osmToCompact.get(osmId);
            if (cid === undefined) continue;
            const nd = graph.getNode(cid);
            if (nd) nodeCoords.push([nd.lat, nd.lon]);
        }

        for (let j = 0; j < el.nodes.length - 1; j++) {
            const fromCid = osmToCompact.get(el.nodes[j]);
            const toCid   = osmToCompact.get(el.nodes[j + 1]);
            if (fromCid === undefined || toCid === undefined) continue;

            const nf = graph.getNode(fromCid);
            const nt = graph.getNode(toCid);
            if (!nf || !nt) continue;

            const dist = haversineLocal(nf.lat, nf.lon, nt.lat, nt.lon);

            // Forward direction
            if (!isReverse) {
                graph.addEdge(fromCid, toCid, dist, roadName, speed, rt);
                edgeCount++;
            }
            // Backward direction (if not one-way)
            if (!isOneway && !implicitOneway) {
                graph.addEdge(toCid, fromCid, dist, roadName, speed, rt);
                edgeCount++;
            }
        }

        // Name way endpoints
        if (wayName) {
            nameEndpoint(graph, osmToCompact, el.nodes[0], wayName);
            if (el.nodes.length > 1) {
                nameEndpoint(graph, osmToCompact, el.nodes[el.nodes.length - 1], wayName);
            }
        }

        if (nodeCoords.length >= 2) {
            waySegments.push({ coords: nodeCoords, roadType: rt, name: roadName });
        }
    }

    onProgress(`Done! ${graph.numNodes} nodes, ${edgeCount} edge directions.`);

    return { graph, bounds: [south, west, north, east], waySegments };
}

/* ── Helpers ─────────────────────────────────────────────── */

function haversineLocal(lat1, lon1, lat2, lon2) {
    const R = 6371.0;
    const dLat = (lat2 - lat1) * Math.PI / 180;
    const dLon = (lon2 - lon1) * Math.PI / 180;
    const a = Math.sin(dLat / 2) ** 2 +
              Math.cos(lat1 * Math.PI / 180) * Math.cos(lat2 * Math.PI / 180) *
              Math.sin(dLon / 2) ** 2;
    return R * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
}

function nameEndpoint(graph, osmToCompact, osmId, wayName) {
    const cid = osmToCompact.get(osmId);
    if (cid === undefined) return;
    const nd = graph.getNode(cid);
    if (!nd) return;
    if (nd.name.startsWith('Intersection #')) {
        nd.name = wayName;
    } else if (!nd.name.includes(wayName)) {
        nd.name = nd.name.substring(0, 40) + ' / ' + wayName.substring(0, 40);
    }
}
