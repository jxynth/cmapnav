/**
 * router.js — Dijkstra, A*, and Turn-by-Turn Directions
 * CMapNav Web Frontend
 *
 * Mirrors pathfinding.c / heap.c from the C engine.
 */

/* ── Haversine distance (km) ────────────────────────────── */

const R_EARTH = 6371.0;
const DEG2RAD = Math.PI / 180;

export function haversine(lat1, lon1, lat2, lon2) {
    const dLat = (lat2 - lat1) * DEG2RAD;
    const dLon = (lon2 - lon1) * DEG2RAD;
    const a = Math.sin(dLat / 2) ** 2 +
              Math.cos(lat1 * DEG2RAD) * Math.cos(lat2 * DEG2RAD) *
              Math.sin(dLon / 2) ** 2;
    return R_EARTH * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
}

/* ── Bearing (degrees 0–360) ────────────────────────────── */

export function bearing(lat1, lon1, lat2, lon2) {
    const φ1 = lat1 * DEG2RAD, φ2 = lat2 * DEG2RAD;
    const Δλ = (lon2 - lon1) * DEG2RAD;
    const y = Math.sin(Δλ) * Math.cos(φ2);
    const x = Math.cos(φ1) * Math.sin(φ2) -
              Math.sin(φ1) * Math.cos(φ2) * Math.cos(Δλ);
    return (Math.atan2(y, x) * 180 / Math.PI + 360) % 360;
}

/* ── Min-Heap (with decrease-key) ───────────────────────── */

class MinHeap {
    constructor() {
        this.heap = [];            // { id, cost }
        this.pos  = new Map();     // id → index in heap
    }

    get size() { return this.heap.length; }

    _swap(i, j) {
        [this.heap[i], this.heap[j]] = [this.heap[j], this.heap[i]];
        this.pos.set(this.heap[i].id, i);
        this.pos.set(this.heap[j].id, j);
    }

    _bubbleUp(i) {
        while (i > 0) {
            const parent = (i - 1) >> 1;
            if (this.heap[i].cost < this.heap[parent].cost) {
                this._swap(i, parent);
                i = parent;
            } else break;
        }
    }

    _sinkDown(i) {
        const n = this.heap.length;
        while (true) {
            let smallest = i;
            const l = 2 * i + 1, r = 2 * i + 2;
            if (l < n && this.heap[l].cost < this.heap[smallest].cost) smallest = l;
            if (r < n && this.heap[r].cost < this.heap[smallest].cost) smallest = r;
            if (smallest === i) break;
            this._swap(i, smallest);
            i = smallest;
        }
    }

    push(id, cost) {
        const idx = this.heap.length;
        this.heap.push({ id, cost });
        this.pos.set(id, idx);
        this._bubbleUp(idx);
    }

    pop() {
        if (this.heap.length === 0) return null;
        const top = this.heap[0];
        this.pos.delete(top.id);
        const last = this.heap.pop();
        if (this.heap.length > 0) {
            this.heap[0] = last;
            this.pos.set(last.id, 0);
            this._sinkDown(0);
        }
        return top;
    }

    decreaseKey(id, newCost) {
        const i = this.pos.get(id);
        if (i === undefined) {
            this.push(id, newCost);
            return;
        }
        if (newCost < this.heap[i].cost) {
            this.heap[i].cost = newCost;
            this._bubbleUp(i);
        }
    }

    has(id) { return this.pos.has(id); }
}

/* ── Route modes ────────────────────────────────────────── */
export const ROUTE_SHORTEST = 'shortest';
export const ROUTE_FASTEST  = 'fastest';

/**
 * Compute edge weight based on mode.
 */
function edgeWeight(edge, mode) {
    if (mode === ROUTE_FASTEST) {
        const speed = edge.speedLimit > 0 ? edge.speedLimit : 40;
        return edge.distance / speed;   // hours
    }
    return edge.distance;               // km
}

/* ── Dijkstra ────────────────────────────────────────────── */

/**
 * @param {import('./graph.js').Graph} graph
 * @param {number} startId
 * @param {number} endId
 * @param {string} mode  — 'shortest' or 'fastest'
 * @returns {{ found: boolean, path: number[], totalDist: number, totalTime: number }}
 */
export function dijkstra(graph, startId, endId, mode = ROUTE_SHORTEST) {
    const dist = new Map();
    const prev = new Map();
    const visited = new Set();
    const pq = new MinHeap();

    dist.set(startId, 0);
    pq.push(startId, 0);

    while (pq.size > 0) {
        const { id: u } = pq.pop();
        if (visited.has(u)) continue;
        visited.add(u);

        if (u === endId) break;

        const node = graph.getNode(u);
        if (!node) continue;

        for (const edge of node.edges) {
            if (visited.has(edge.destId)) continue;
            const w = edgeWeight(edge, mode);
            const newDist = dist.get(u) + w;
            if (!dist.has(edge.destId) || newDist < dist.get(edge.destId)) {
                dist.set(edge.destId, newDist);
                prev.set(edge.destId, u);
                pq.decreaseKey(edge.destId, newDist);
            }
        }
    }

    return reconstructPath(graph, startId, endId, prev);
}

/* ── A* ──────────────────────────────────────────────────── */

/**
 * @param {import('./graph.js').Graph} graph
 * @param {number} startId
 * @param {number} endId
 * @param {string} mode
 * @returns {{ found: boolean, path: number[], totalDist: number, totalTime: number }}
 */
export function astar(graph, startId, endId, mode = ROUTE_SHORTEST) {
    const goal = graph.getNode(endId);
    if (!goal) return { found: false, path: [], totalDist: 0, totalTime: 0 };

    const gScore = new Map();
    const prev   = new Map();
    const visited = new Set();
    const pq = new MinHeap();

    gScore.set(startId, 0);
    const startNode = graph.getNode(startId);
    const h0 = startNode ? heuristic(startNode, goal, mode) : 0;
    pq.push(startId, h0);

    while (pq.size > 0) {
        const { id: u } = pq.pop();
        if (visited.has(u)) continue;
        visited.add(u);

        if (u === endId) break;

        const node = graph.getNode(u);
        if (!node) continue;

        for (const edge of node.edges) {
            if (visited.has(edge.destId)) continue;
            const w = edgeWeight(edge, mode);
            const tentG = gScore.get(u) + w;
            if (!gScore.has(edge.destId) || tentG < gScore.get(edge.destId)) {
                gScore.set(edge.destId, tentG);
                prev.set(edge.destId, u);
                const dest = graph.getNode(edge.destId);
                const h = dest ? heuristic(dest, goal, mode) : 0;
                pq.decreaseKey(edge.destId, tentG + h);
            }
        }
    }

    return reconstructPath(graph, startId, endId, prev);
}

function heuristic(node, goal, mode) {
    const dist = haversine(node.lat, node.lon, goal.lat, goal.lon);
    if (mode === ROUTE_FASTEST) {
        return dist / 120; // optimistic: 120 km/h
    }
    return dist;
}

/* ── Path reconstruction ─────────────────────────────────── */

function reconstructPath(graph, startId, endId, prev) {
    if (!prev.has(endId) && startId !== endId) {
        return { found: false, path: [], totalDist: 0, totalTime: 0 };
    }

    const path = [];
    let cur = endId;
    while (cur !== undefined) {
        path.push(cur);
        cur = prev.get(cur);
    }
    path.reverse();

    // Calculate totals
    let totalDist = 0;
    let totalTime = 0;
    for (let i = 0; i < path.length - 1; i++) {
        const from = graph.getNode(path[i]);
        const to   = graph.getNode(path[i + 1]);
        if (!from || !to) continue;
        const edge = from.edges.find(e => e.destId === path[i + 1]);
        if (edge) {
            totalDist += edge.distance;
            const speed = edge.speedLimit > 0 ? edge.speedLimit : 40;
            totalTime += edge.distance / speed;
        }
    }

    return { found: true, path, totalDist, totalTime };
}

/* ── Turn-by-turn directions ─────────────────────────────── */

/**
 * Generate human-readable directions from a path.
 * @param {import('./graph.js').Graph} graph
 * @param {number[]} path  — array of node IDs
 * @returns {Array<{text: string, distance: number, roadName: string, lat: number, lon: number}>}
 */
export function generateDirections(graph, path) {
    if (path.length < 2) return [];

    const steps = [];
    let currentRoad = '';
    let segmentDist = 0;
    let segmentStartIdx = 0;

    for (let i = 0; i < path.length - 1; i++) {
        const from = graph.getNode(path[i]);
        const to   = graph.getNode(path[i + 1]);
        if (!from || !to) continue;

        const edge = from.edges.find(e => e.destId === path[i + 1]);
        if (!edge) continue;

        const road = edge.roadName || 'unnamed road';

        if (road !== currentRoad && currentRoad !== '') {
            // Emit the previous segment
            const startNode = graph.getNode(path[segmentStartIdx]);
            const prevNode  = graph.getNode(path[i]);
            const nextNode  = to;

            // Determine turn direction
            let turnDir = 'Continue';
            if (startNode && prevNode && nextNode && i > 0) {
                const prevPrev = graph.getNode(path[i - 1]);
                if (prevPrev) {
                    const b1 = bearing(prevPrev.lat, prevPrev.lon, from.lat, from.lon);
                    const b2 = bearing(from.lat, from.lon, to.lat, to.lon);
                    const angle = ((b2 - b1) + 360) % 360;
                    if (angle > 30 && angle < 170) turnDir = 'Turn right';
                    else if (angle > 190 && angle < 330) turnDir = 'Turn left';
                    else if (angle >= 170 && angle <= 190) turnDir = 'Make a U-turn';
                    else turnDir = 'Continue';
                }
            }

            steps.push({
                text: `${steps.length === 0 ? 'Head' : turnDir} onto ${currentRoad}`,
                distance: segmentDist,
                roadName: currentRoad,
                lat: startNode ? startNode.lat : 0,
                lon: startNode ? startNode.lon : 0,
            });

            segmentDist = 0;
            segmentStartIdx = i;
        }

        currentRoad = road;
        segmentDist += edge.distance;
    }

    // Last segment
    if (currentRoad) {
        const startNode = graph.getNode(path[segmentStartIdx]);
        steps.push({
            text: `${steps.length === 0 ? 'Head' : 'Continue'} on ${currentRoad} — Arrive`,
            distance: segmentDist,
            roadName: currentRoad,
            lat: startNode ? startNode.lat : 0,
            lon: startNode ? startNode.lon : 0,
        });
    }

    return steps;
}
