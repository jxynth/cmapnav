/**
 * heap.c - Min-Heap Priority Queue Implementation
 * CMapNav - Terminal Navigation System
 *
 * Binary min-heap with a parallel position-tracker array so that
 * decrease_key can locate any node in O(1) and bubble it up in O(log n).
 *
 * Complexity:
 *   insert        : O(log n)
 *   extract_min   : O(log n)
 *   decrease_key  : O(log n)
 */

#include "heap.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <float.h>

/* ── Internal helpers ────────────────────────────────────── */

static inline void swap(MinHeap *h, int i, int j) {
    HeapEntry tmp = h->data[i];
    h->data[i]    = h->data[j];
    h->data[j]    = tmp;
    /* keep position tracker in sync */
    h->pos[h->data[i].node_id] = i;
    h->pos[h->data[j].node_id] = j;
}

static void bubble_up(MinHeap *h, int idx) {
    while (idx > 0) {
        int parent = (idx - 1) / 2;
        if (h->data[parent].priority <= h->data[idx].priority) break;
        swap(h, parent, idx);
        idx = parent;
    }
}

static void bubble_down(MinHeap *h, int idx) {
    while (1) {
        int left  = 2 * idx + 1;
        int right = 2 * idx + 2;
        int smallest = idx;

        if (left  < h->size && h->data[left].priority  < h->data[smallest].priority)
            smallest = left;
        if (right < h->size && h->data[right].priority < h->data[smallest].priority)
            smallest = right;

        if (smallest == idx) break;
        swap(h, idx, smallest);
        idx = smallest;
    }
}

/* ── Public API ──────────────────────────────────────────── */

MinHeap *heap_create(int capacity, int max_node_id) {
    if (capacity   <= 0) capacity   = 64;
    if (max_node_id <= 0) max_node_id = 512;

    MinHeap *h = (MinHeap *)malloc(sizeof(MinHeap));
    if (!h) return NULL;

    h->data = (HeapEntry *)malloc((size_t)capacity * sizeof(HeapEntry));
    h->pos  = (int *)malloc((size_t)max_node_id * sizeof(int));

    if (!h->data || !h->pos) {
        free(h->data); free(h->pos); free(h);
        return NULL;
    }

    for (int i = 0; i < max_node_id; i++) h->pos[i] = -1;

    h->size        = 0;
    h->capacity    = capacity;
    h->max_node_id = max_node_id;
    return h;
}

void heap_destroy(MinHeap *h) {
    if (!h) return;
    free(h->data);
    free(h->pos);
    free(h);
}

int heap_empty(const MinHeap *h) {
    return (!h || h->size == 0);
}

void heap_insert(MinHeap *h, int node_id, double priority) {
    if (!h) return;

    /* grow if needed */
    if (h->size == h->capacity) {
        int new_cap = h->capacity * 2;
        HeapEntry *tmp = (HeapEntry *)realloc(h->data,
                         (size_t)new_cap * sizeof(HeapEntry));
        if (!tmp) return;
        h->data     = tmp;
        h->capacity = new_cap;
    }

    /* grow pos array if node_id is out of range */
    if (node_id >= h->max_node_id) {
        int new_max = node_id * 2 + 1;
        int *tmp2 = (int *)realloc(h->pos, (size_t)new_max * sizeof(int));
        if (!tmp2) return;
        for (int i = h->max_node_id; i < new_max; i++) tmp2[i] = -1;
        h->pos        = tmp2;
        h->max_node_id = new_max;
    }

    int idx = h->size++;
    h->data[idx].node_id  = node_id;
    h->data[idx].priority = priority;
    h->pos[node_id]       = idx;
    bubble_up(h, idx);
}

int heap_extract_min(MinHeap *h, double *priority_out) {
    if (!h || h->size == 0) return -1;

    int    min_id  = h->data[0].node_id;
    double min_pri = h->data[0].priority;

    if (priority_out) *priority_out = min_pri;

    h->pos[min_id] = -1;
    h->size--;

    if (h->size > 0) {
        h->data[0]               = h->data[h->size];
        h->pos[h->data[0].node_id] = 0;
        bubble_down(h, 0);
    }
    return min_id;
}

void heap_decrease_key(MinHeap *h, int node_id, double new_priority) {
    if (!h || node_id < 0 || node_id >= h->max_node_id) return;
    int idx = h->pos[node_id];
    if (idx < 0) return; /* not in heap */
    if (new_priority >= h->data[idx].priority) return; /* not a decrease */
    h->data[idx].priority = new_priority;
    bubble_up(h, idx);
}

int heap_contains(const MinHeap *h, int node_id) {
    if (!h || node_id < 0 || node_id >= h->max_node_id) return 0;
    return h->pos[node_id] >= 0;
}
