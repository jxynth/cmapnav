/**
 * graph.js — Adjacency-List Graph (mirrors graph.c / graph.h)
 * CMapNav Web Frontend
 *
 * Classes: Node, Edge, Graph
 * The Graph uses a Map<int, Node> for O(1) node lookups.
 */

export class Edge {
    /**
     * @param {number} destId
     * @param {number} distance   — km
     * @param {string} roadName
     * @param {number} speedLimit — km/h
     * @param {number} roadType   — 0=highway 1=arterial 2=local
     */
    constructor(destId, distance, roadName, speedLimit, roadType) {
        this.destId     = destId;
        this.distance   = distance;
        this.roadName   = roadName;
        this.speedLimit = speedLimit;
        this.roadType   = roadType;
    }
}

export class Node {
    /**
     * @param {number} id
     * @param {number} lat
     * @param {number} lon
     * @param {string} name
     */
    constructor(id, lat, lon, name) {
        this.id   = id;
        this.lat  = lat;
        this.lon  = lon;
        this.name = name;
        /** @type {Edge[]} */
        this.edges = [];
    }
}

export class Graph {
    constructor() {
        /** @type {Map<number, Node>} */
        this.nodes = new Map();
        this.numNodes = 0;
        this.numEdges = 0;
    }

    /**
     * Add a node (or update if exists).
     * @returns {Node}
     */
    addNode(id, lat, lon, name) {
        if (this.nodes.has(id)) {
            const n = this.nodes.get(id);
            n.lat  = lat;
            n.lon  = lon;
            n.name = name;
            return n;
        }
        const n = new Node(id, lat, lon, name);
        this.nodes.set(id, n);
        this.numNodes++;
        return n;
    }

    /** @returns {Node|undefined} */
    getNode(id) {
        return this.nodes.get(id);
    }

    /**
     * Add a directed edge from → to.
     */
    addEdge(fromId, toId, distance, roadName, speedLimit, roadType) {
        const from = this.nodes.get(fromId);
        if (!from) return;
        from.edges.push(new Edge(toId, distance, roadName, speedLimit, roadType));
        this.numEdges++;
    }

    /** Clear everything */
    clear() {
        this.nodes.clear();
        this.numNodes = 0;
        this.numEdges = 0;
    }

    /**
     * Find the node closest to a lat/lon coordinate (brute force).
     * Good enough for click-to-snap – O(n).
     * @returns {Node|null}
     */
    nearestNode(lat, lon) {
        let best = null;
        let bestDist = Infinity;
        for (const node of this.nodes.values()) {
            const d = (node.lat - lat) ** 2 + (node.lon - lon) ** 2;
            if (d < bestDist) {
                bestDist = d;
                best = node;
            }
        }
        return best;
    }

    /**
     * Fuzzy name search — returns nodes whose name includes the query.
     * @param {string} query
     * @param {number} maxResults
     * @returns {Node[]}
     */
    searchByName(query, maxResults = 20) {
        const q = query.toLowerCase();
        const results = [];
        for (const node of this.nodes.values()) {
            if (node.name.toLowerCase().includes(q)) {
                results.push(node);
                if (results.length >= maxResults) break;
            }
        }
        return results;
    }
}
