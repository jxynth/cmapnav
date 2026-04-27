/**
 * heap.h - Min-Heap Priority Queue
 * CMapNav - Terminal Navigation System
 *
 * Supports insert, extract-min, and decrease-key in O(log n).
 * Used by Dijkstra's and A* algorithms.
 */

#ifndef HEAP_H
#define HEAP_H

/* One entry in the heap */
typedef struct {
    int    node_id;
    double priority;  /* distance or f-score (A*) */
} HeapEntry;

typedef struct {
    HeapEntry *data;
    int       *pos;   /* pos[node_id] = index in data array (-1 if absent) */
    int        size;
    int        capacity;
    int        max_node_id; /* length of pos array */
} MinHeap;

/* ── API ─────────────────────────────────────────────────── */
MinHeap *heap_create(int capacity, int max_node_id);
void     heap_destroy(MinHeap *h);

int      heap_empty(const MinHeap *h);
void     heap_insert(MinHeap *h, int node_id, double priority);

/* Returns the node_id with lowest priority; sets *priority_out */
int      heap_extract_min(MinHeap *h, double *priority_out);

/* Lower an existing node's priority (for relaxation) */
void     heap_decrease_key(MinHeap *h, int node_id, double new_priority);

int      heap_contains(const MinHeap *h, int node_id);

#endif /* HEAP_H */
