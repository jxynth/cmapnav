/**
 * hashtable.h - Location Name → Node ID Hash Table
 * CMapNav - Terminal Navigation System
 *
 * Open-addressed chaining hash table providing O(1) average
 * lookup of a node by its string name.
 */

#ifndef HASHTABLE_H
#define HASHTABLE_H

#define HT_DEFAULT_BUCKETS 128

/* One entry in a bucket chain */
typedef struct HTEntry {
    char         key[100];   /* location name (normalised to lower-case) */
    int          node_id;
    struct HTEntry *next;
} HTEntry;

typedef struct {
    HTEntry **buckets;
    int       num_buckets;
    int       size;          /* total entries inserted */
} HashTable;

/* ── API ─────────────────────────────────────────────────── */
HashTable *ht_create(int num_buckets);
void       ht_destroy(HashTable *ht);

void       ht_insert(HashTable *ht, const char *name, int node_id);
int        ht_lookup(const HashTable *ht, const char *name); /* returns node_id or -1 */
void       ht_delete(HashTable *ht, const char *name);

/* Fuzzy search: returns node_id of best match or -1 */
int        ht_fuzzy_lookup(const HashTable *ht, const char *query,
                           char *matched_name, int matched_name_sz);

void       ht_print_stats(const HashTable *ht);

#endif /* HASHTABLE_H */
