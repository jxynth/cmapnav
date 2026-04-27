/**
 * hashtable.c - Hash Table Implementation
 * CMapNav - Terminal Navigation System
 *
 * Hash function: djb2 (fast, well-distributed for strings).
 * Collision resolution: separate chaining (linked lists per bucket).
 * All keys stored as lower-case for case-insensitive search.
 *
 * Complexity:
 *   insert / lookup / delete : O(1) average, O(n) worst case
 */

#include "hashtable.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* ── Internal helpers ────────────────────────────────────── */

/** djb2 hash by Dan Bernstein */
static unsigned long djb2(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = (unsigned char)*str++))
        hash = ((hash << 5) + hash) + (unsigned long)c; /* hash*33 + c */
    return hash;
}

/** Copy src to dst in lower-case, NUL-terminated, up to dst_sz-1 chars */
static void to_lower(char *dst, const char *src, int dst_sz) {
    int i;
    for (i = 0; i < dst_sz - 1 && src[i]; i++)
        dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = '\0';
}

static int bucket_idx(const HashTable *ht, const char *key) {
    return (int)(djb2(key) % (unsigned long)ht->num_buckets);
}

/* Simple Levenshtein distance (for fuzzy matching) – capped at max_dist */
static int levenshtein(const char *a, const char *b, int max_dist) {
    int la = (int)strlen(a);
    int lb = (int)strlen(b);
    if (abs(la - lb) > max_dist) return max_dist + 1;

    /* Use two rows to save memory */
    int  prev[256], curr[256];
    if (lb >= 256) return max_dist + 1;

    for (int j = 0; j <= lb; j++) prev[j] = j;

    for (int i = 1; i <= la; i++) {
        curr[0] = i;
        for (int j = 1; j <= lb; j++) {
            int cost = (a[i-1] == b[j-1]) ? 0 : 1;
            int del  = prev[j]   + 1;
            int ins  = curr[j-1] + 1;
            int sub  = prev[j-1] + cost;
            int m    = (del < ins) ? del : ins;
            curr[j]  = (m < sub)  ? m   : sub;
        }
        memcpy(prev, curr, (size_t)(lb + 1) * sizeof(int));
    }
    return prev[lb];
}

/* ── Public API ──────────────────────────────────────────── */

HashTable *ht_create(int num_buckets) {
    if (num_buckets <= 0) num_buckets = HT_DEFAULT_BUCKETS;

    HashTable *ht = (HashTable *)malloc(sizeof(HashTable));
    if (!ht) return NULL;

    ht->buckets = (HTEntry **)calloc((size_t)num_buckets, sizeof(HTEntry *));
    if (!ht->buckets) { free(ht); return NULL; }

    ht->num_buckets = num_buckets;
    ht->size        = 0;
    return ht;
}

void ht_destroy(HashTable *ht) {
    if (!ht) return;
    for (int i = 0; i < ht->num_buckets; i++) {
        HTEntry *e = ht->buckets[i];
        while (e) {
            HTEntry *tmp = e->next;
            free(e);
            e = tmp;
        }
    }
    free(ht->buckets);
    free(ht);
}

void ht_insert(HashTable *ht, const char *name, int node_id) {
    if (!ht || !name) return;

    char key[100];
    to_lower(key, name, sizeof(key));

    int idx = bucket_idx(ht, key);

    /* update if already present */
    for (HTEntry *e = ht->buckets[idx]; e; e = e->next) {
        if (strcmp(e->key, key) == 0) {
            e->node_id = node_id;
            return;
        }
    }

    HTEntry *e = (HTEntry *)malloc(sizeof(HTEntry));
    if (!e) return;
    strncpy(e->key, key, sizeof(e->key) - 1);
    e->key[sizeof(e->key)-1] = '\0';
    e->node_id  = node_id;
    e->next     = ht->buckets[idx];
    ht->buckets[idx] = e;
    ht->size++;
}

int ht_lookup(const HashTable *ht, const char *name) {
    if (!ht || !name) return -1;

    char key[100];
    to_lower(key, name, sizeof(key));

    int idx = bucket_idx(ht, key);
    for (HTEntry *e = ht->buckets[idx]; e; e = e->next) {
        if (strcmp(e->key, key) == 0) return e->node_id;
    }
    return -1;
}

void ht_delete(HashTable *ht, const char *name) {
    if (!ht || !name) return;

    char key[100];
    to_lower(key, name, sizeof(key));

    int idx = bucket_idx(ht, key);
    HTEntry *prev = NULL;
    HTEntry *e    = ht->buckets[idx];

    while (e) {
        if (strcmp(e->key, key) == 0) {
            if (prev) prev->next = e->next;
            else      ht->buckets[idx] = e->next;
            free(e);
            ht->size--;
            return;
        }
        prev = e;
        e    = e->next;
    }
}

int ht_fuzzy_lookup(const HashTable *ht, const char *query,
                    char *matched_name, int matched_name_sz) {
    if (!ht || !query) return -1;

    char q[100];
    to_lower(q, query, sizeof(q));

    int best_id   = -1;
    int best_dist = 999;

    for (int i = 0; i < ht->num_buckets; i++) {
        for (HTEntry *e = ht->buckets[i]; e; e = e->next) {
            int d = levenshtein(q, e->key, best_dist);
            if (d < best_dist) {
                best_dist = d;
                best_id   = e->node_id;
                if (matched_name)
                    strncpy(matched_name, e->key, (size_t)matched_name_sz - 1);
            }
            if (best_dist == 0) goto done; /* exact match */
        }
    }
done:
    return best_id;
}

void ht_print_stats(const HashTable *ht) {
    if (!ht) return;
    int occupied = 0, max_chain = 0;
    for (int i = 0; i < ht->num_buckets; i++) {
        int len = 0;
        for (HTEntry *e = ht->buckets[i]; e; e = e->next) len++;
        if (len > 0) occupied++;
        if (len > max_chain) max_chain = len;
    }
    printf("HashTable: %d entries, %d/%d buckets used, max chain=%d\n",
           ht->size, occupied, ht->num_buckets, max_chain);
}
