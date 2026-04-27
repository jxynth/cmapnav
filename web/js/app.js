/**
 * app.js — Main Application Controller
 * CMapNav Web Frontend
 *
 * Initialises the Leaflet map, wires the UI, manages
 * origin/destination markers, route overlays, and
 * the download workflow.
 */

import { PRESETS, fetchOSMData, roadColor, roadWeight } from './osm.js';
import { dijkstra, astar, generateDirections, ROUTE_SHORTEST, ROUTE_FASTEST, haversine } from './router.js';

/* ═══════════════════════════════════════════════════════════
 *  State
 * ══════════════════════════════════════════════════════════ */

let map;
let graph = null;
let waySegments = [];

let originMarker  = null;
let destMarker    = null;
let originNode    = null;
let destNode      = null;
let routePolyline = null;
let roadLayer     = null;

let clickMode = null;   // 'origin' | 'dest' | null

/* ═══════════════════════════════════════════════════════════
 *  Custom Marker Icons
 * ══════════════════════════════════════════════════════════ */

function createIcon(color, label) {
    return L.divIcon({
        className: 'custom-marker',
        html: `<div class="marker-pin" style="background:${color}">
                 <span>${label}</span>
               </div>`,
        iconSize:   [30, 42],
        iconAnchor: [15, 42],
        popupAnchor: [0, -42],
    });
}

const originIcon = createIcon('#00d26a', 'A');
const destIcon   = createIcon('#ff4757', 'B');

/* ═══════════════════════════════════════════════════════════
 *  Map Initialisation
 * ══════════════════════════════════════════════════════════ */

function initMap() {
    map = L.map('map', {
        center: [12.935, 77.625],   // default: Bengaluru
        zoom: 14,
        zoomControl: false,
    });

    // Dark tile layer
    L.tileLayer('https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png', {
        attribution: '&copy; <a href="https://carto.com/">CARTO</a> &copy; <a href="https://www.openstreetmap.org/copyright">OSM</a>',
        subdomains: 'abcd',
        maxZoom: 19,
    }).addTo(map);

    L.control.zoom({ position: 'bottomright' }).addTo(map);

    // Click handler
    map.on('click', onMapClick);
}

/* ═══════════════════════════════════════════════════════════
 *  Map Click — set origin / destination
 * ══════════════════════════════════════════════════════════ */

function onMapClick(e) {
    if (!graph) {
        showToast('Download an area first!', 'warning');
        return;
    }

    const node = graph.nearestNode(e.latlng.lat, e.latlng.lng);
    if (!node) return;

    const dist = haversine(e.latlng.lat, e.latlng.lng, node.lat, node.lon);
    if (dist > 1.0) {
        showToast('Too far from any road. Click closer to the downloaded area.', 'warning');
        return;
    }

    if (clickMode === 'origin' || (!clickMode && !originNode)) {
        setOrigin(node);
    } else {
        setDest(node);
    }
}

function setOrigin(node) {
    originNode = node;
    if (originMarker) map.removeLayer(originMarker);
    originMarker = L.marker([node.lat, node.lon], { icon: originIcon })
                    .addTo(map)
                    .bindPopup(`<b>A</b> ${node.name}`);
    document.getElementById('origin-input').value = node.name;
    clickMode = null;
    updateSetButtons();
    autoRoute();
}

function setDest(node) {
    destNode = node;
    if (destMarker) map.removeLayer(destMarker);
    destMarker = L.marker([node.lat, node.lon], { icon: destIcon })
                  .addTo(map)
                  .bindPopup(`<b>B</b> ${node.name}`);
    document.getElementById('dest-input').value = node.name;
    clickMode = null;
    updateSetButtons();
    autoRoute();
}

function updateSetButtons() {
    document.getElementById('btn-set-origin').classList.toggle('active', clickMode === 'origin');
    document.getElementById('btn-set-dest').classList.toggle('active', clickMode === 'dest');
}

/* ═══════════════════════════════════════════════════════════
 *  Routing
 * ══════════════════════════════════════════════════════════ */

function autoRoute() {
    if (originNode && destNode) {
        computeRoute();
    }
}

function computeRoute() {
    if (!graph || !originNode || !destNode) {
        showToast('Set origin and destination first.', 'warning');
        return;
    }

    if (originNode.id === destNode.id) {
        showToast('Origin and destination are the same.', 'warning');
        return;
    }

    const mode = document.getElementById('route-mode').value;
    const algo = document.getElementById('algo-select').value;

    const t0 = performance.now();
    const result = algo === 'astar'
        ? astar(graph, originNode.id, destNode.id, mode)
        : dijkstra(graph, originNode.id, destNode.id, mode);
    const dt = (performance.now() - t0).toFixed(1);

    if (!result.found) {
        showToast('No route found. The roads may not be connected.', 'error');
        clearRoute();
        return;
    }

    // Build polyline coordinates
    const coords = result.path.map(id => {
        const n = graph.getNode(id);
        return [n.lat, n.lon];
    });

    // Clear previous route
    if (routePolyline) map.removeLayer(routePolyline);

    // Animated route drawing
    routePolyline = L.polyline([], {
        color: '#7c3aed',
        weight: 6,
        opacity: 0.9,
        lineCap: 'round',
        lineJoin: 'round',
    }).addTo(map);

    // Glow effect
    const glow = L.polyline(coords, {
        color: '#7c3aed',
        weight: 12,
        opacity: 0.25,
        lineCap: 'round',
    }).addTo(map);

    // Animate
    let step = 0;
    const totalSteps = coords.length;
    const batchSize = Math.max(1, Math.floor(totalSteps / 60));
    function animateStep() {
        const end = Math.min(step + batchSize, totalSteps);
        for (let i = step; i < end; i++) {
            routePolyline.addLatLng(coords[i]);
        }
        step = end;
        if (step < totalSteps) {
            requestAnimationFrame(animateStep);
        }
    }
    animateStep();

    // Fit bounds
    map.fitBounds(L.latLngBounds(coords).pad(0.15));

    // Directions
    const directions = generateDirections(graph, result.path);
    showDirections(result, directions, dt);
}

function clearRoute() {
    if (routePolyline) {
        map.removeLayer(routePolyline);
        routePolyline = null;
    }
    // Remove any glow layers
    map.eachLayer(layer => {
        if (layer instanceof L.Polyline && layer !== routePolyline && !(layer._cmapRoad)) {
            // Keep road layers, remove route glows
            if (layer.options && layer.options.color === '#7c3aed') {
                map.removeLayer(layer);
            }
        }
    });
    document.getElementById('directions-panel').innerHTML = '';
    document.getElementById('route-summary').style.display = 'none';
}

/* ═══════════════════════════════════════════════════════════
 *  Directions Panel
 * ══════════════════════════════════════════════════════════ */

function showDirections(result, directions, computeTime) {
    const summary = document.getElementById('route-summary');
    summary.style.display = 'block';
    summary.innerHTML = `
        <div class="summary-row">
            <span class="summary-label">Distance</span>
            <span class="summary-value">${result.totalDist.toFixed(2)} km</span>
        </div>
        <div class="summary-row">
            <span class="summary-label">Est. Time</span>
            <span class="summary-value">${formatTime(result.totalTime)}</span>
        </div>
        <div class="summary-row subtle">
            <span class="summary-label">Computed in</span>
            <span class="summary-value">${computeTime} ms</span>
        </div>
    `;

    const panel = document.getElementById('directions-panel');
    if (directions.length === 0) {
        panel.innerHTML = '<p class="no-directions">No directions available.</p>';
        return;
    }

    panel.innerHTML = directions.map((step, i) => `
        <div class="direction-step" data-lat="${step.lat}" data-lon="${step.lon}">
            <div class="step-number">${i + 1}</div>
            <div class="step-content">
                <div class="step-text">${step.text}</div>
                <div class="step-dist">${step.distance.toFixed(2)} km</div>
            </div>
        </div>
    `).join('');

    // Click to pan
    panel.querySelectorAll('.direction-step').forEach(el => {
        el.addEventListener('click', () => {
            const lat = parseFloat(el.dataset.lat);
            const lon = parseFloat(el.dataset.lon);
            map.setView([lat, lon], 17);
        });
    });
}

function formatTime(hours) {
    const mins = Math.round(hours * 60);
    if (mins < 60) return `${mins} min`;
    const h = Math.floor(mins / 60);
    const m = mins % 60;
    return `${h} h ${m} min`;
}

/* ═══════════════════════════════════════════════════════════
 *  OSM Download
 * ──────────────────────────────────────────────────────────
 */

async function downloadArea() {
    const presetSelect = document.getElementById('preset-select');
    const idx = parseInt(presetSelect.value, 10);

    let south, west, north, east;

    if (idx === -1) {
        // custom bbox
        south = parseFloat(document.getElementById('custom-south').value);
        west  = parseFloat(document.getElementById('custom-west').value);
        north = parseFloat(document.getElementById('custom-north').value);
        east  = parseFloat(document.getElementById('custom-east').value);
        if (isNaN(south) || isNaN(west) || isNaN(north) || isNaN(east)) {
            showToast('Enter valid bounding box coordinates.', 'error');
            return;
        }
    } else {
        const p = PRESETS[idx];
        south = p.south; west = p.west; north = p.north; east = p.east;
    }

    const btn = document.getElementById('btn-download');
    const statusEl = document.getElementById('download-status');
    btn.disabled = true;
    btn.textContent = 'Downloading...';
    statusEl.textContent = '';

    try {
        const result = await fetchOSMData(south, west, north, east, msg => {
            statusEl.textContent = msg;
        });

        graph = result.graph;
        waySegments = result.waySegments;

        // Clear old markers/routes
        clearAll();

        // Draw road network
        drawRoadNetwork(result.waySegments);

        // Fit map to bounds
        map.fitBounds([
            [south, west],
            [north, east]
        ]);

        // Update stats
        document.getElementById('stats-nodes').textContent = graph.numNodes.toLocaleString();
        document.getElementById('stats-edges').textContent = graph.numEdges.toLocaleString();
        document.getElementById('stats-panel').style.display = 'block';

        showToast(`Loaded ${graph.numNodes.toLocaleString()} nodes, ${graph.numEdges.toLocaleString()} edges`, 'success');

    } catch (err) {
        statusEl.textContent = '';
        showToast(`Download failed: ${err.message}`, 'error');
        console.error(err);
    } finally {
        btn.disabled = false;
        btn.textContent = 'Download Area';
    }
}

function drawRoadNetwork(segments) {
    if (roadLayer) map.removeLayer(roadLayer);
    roadLayer = L.layerGroup();

    for (const seg of segments) {
        const poly = L.polyline(seg.coords, {
            color:   roadColor(seg.roadType),
            weight:  roadWeight(seg.roadType),
            opacity: 0.5,
        });
        poly._cmapRoad = true; // flag to keep on clear
        roadLayer.addLayer(poly);
    }
    roadLayer.addTo(map);
}

function clearAll() {
    if (originMarker) { map.removeLayer(originMarker); originMarker = null; }
    if (destMarker)   { map.removeLayer(destMarker);   destMarker = null; }
    originNode = null;
    destNode   = null;
    document.getElementById('origin-input').value = '';
    document.getElementById('dest-input').value = '';
    clearRoute();
}

/* ═══════════════════════════════════════════════════════════
 *  Search
 * ══════════════════════════════════════════════════════════ */

function setupSearch(inputId, callback) {
    const input = document.getElementById(inputId);
    const results = input.nextElementSibling; // .search-results
    let debounce = null;

    input.addEventListener('input', () => {
        clearTimeout(debounce);
        debounce = setTimeout(() => {
            const q = input.value.trim();
            if (!q || !graph) { results.style.display = 'none'; return; }
            const matches = graph.searchByName(q, 8);
            if (matches.length === 0) { results.style.display = 'none'; return; }
            results.innerHTML = matches.map(n =>
                `<div class="search-item" data-id="${n.id}">${n.name}</div>`
            ).join('');
            results.style.display = 'block';
            results.querySelectorAll('.search-item').forEach(el => {
                el.addEventListener('click', () => {
                    const node = graph.getNode(parseInt(el.dataset.id, 10));
                    if (node) { callback(node); input.value = node.name; }
                    results.style.display = 'none';
                });
            });
        }, 200);
    });

    // Close on outside click
    document.addEventListener('click', e => {
        if (!input.contains(e.target) && !results.contains(e.target)) {
            results.style.display = 'none';
        }
    });
}

/* ═══════════════════════════════════════════════════════════
 *  Toast Notifications
 * ══════════════════════════════════════════════════════════ */

function showToast(message, type = 'info') {
    const container = document.getElementById('toast-container');
    const toast = document.createElement('div');
    toast.className = `toast toast-${type}`;
    toast.textContent = message;
    container.appendChild(toast);

    requestAnimationFrame(() => toast.classList.add('show'));

    setTimeout(() => {
        toast.classList.remove('show');
        setTimeout(() => toast.remove(), 400);
    }, 3500);
}

/* ═══════════════════════════════════════════════════════════
 *  Preset selector — toggle custom bbox
 * ══════════════════════════════════════════════════════════ */

function setupPresetSelector() {
    const select = document.getElementById('preset-select');

    // Populate
    PRESETS.forEach((p, i) => {
        const opt = document.createElement('option');
        opt.value = i;
        opt.textContent = p.name;
        select.appendChild(opt);
    });
    const custom = document.createElement('option');
    custom.value = -1;
    custom.textContent = '📐 Custom bounding box…';
    select.appendChild(custom);

    select.addEventListener('change', () => {
        document.getElementById('custom-bbox').style.display =
            select.value === '-1' ? 'grid' : 'none';
    });
}

/* ═══════════════════════════════════════════════════════════
 *  Wire Everything on DOM Ready
 * ══════════════════════════════════════════════════════════ */

document.addEventListener('DOMContentLoaded', () => {
    initMap();
    setupPresetSelector();

    // Download
    document.getElementById('btn-download').addEventListener('click', downloadArea);

    // Route
    document.getElementById('btn-route').addEventListener('click', computeRoute);

    // Clear
    document.getElementById('btn-clear').addEventListener('click', () => {
        clearAll();
        showToast('Markers and route cleared.', 'info');
    });

    // Origin/Dest click-mode buttons
    document.getElementById('btn-set-origin').addEventListener('click', () => {
        clickMode = clickMode === 'origin' ? null : 'origin';
        updateSetButtons();
        if (clickMode) showToast('Click the map to set origin (A)', 'info');
    });
    document.getElementById('btn-set-dest').addEventListener('click', () => {
        clickMode = clickMode === 'dest' ? null : 'dest';
        updateSetButtons();
        if (clickMode) showToast('Click the map to set destination (B)', 'info');
    });

    // Search inputs
    setupSearch('origin-input', setOrigin);
    setupSearch('dest-input', setDest);

    // Toggle road overlay
    document.getElementById('toggle-roads').addEventListener('change', e => {
        if (roadLayer) {
            if (e.target.checked) roadLayer.addTo(map);
            else map.removeLayer(roadLayer);
        }
    });

    // Swap origin/dest
    document.getElementById('btn-swap').addEventListener('click', () => {
        const tmpNode = originNode;
        const tmpMarker = originMarker;
        originNode   = destNode;
        originMarker = destMarker;
        destNode     = tmpNode;
        destMarker   = tmpMarker;
        // Update icons
        if (originMarker) originMarker.setIcon(originIcon);
        if (destMarker)   destMarker.setIcon(destIcon);
        // Update inputs
        document.getElementById('origin-input').value = originNode ? originNode.name : '';
        document.getElementById('dest-input').value   = destNode ? destNode.name : '';
        autoRoute();
    });
});
