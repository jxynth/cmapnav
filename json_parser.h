/**
 * json_parser.h - Minimal Recursive-Descent JSON Parser
 * CMapNav - Terminal Navigation System
 *
 * Parses JSON text into a tree of JValue nodes.
 * Designed specifically for Overpass API responses but handles
 * all valid JSON types.
 */

#ifndef JSON_PARSER_H
#define JSON_PARSER_H

typedef enum {
    JV_NULL,
    JV_BOOL,
    JV_NUMBER,
    JV_STRING,
    JV_ARRAY,
    JV_OBJECT
} JVType;

typedef struct JValue {
    JVType type;
    double         number;   /* JV_NUMBER                             */
    int            boolean;  /* JV_BOOL  (0 or 1)                     */
    char          *string;   /* JV_STRING (heap-allocated, caller-freed) */
    struct JValue **items;   /* JV_ARRAY  elements                    */
    char          **keys;    /* JV_OBJECT keys   (parallel with vals) */
    struct JValue **vals;    /* JV_OBJECT values                      */
    int            count;    /* arr_len or obj_member_count            */
} JValue;

/* ── API ─────────────────────────────────────────────────── */

/* Parse a NUL-terminated JSON string → tree. NULL on error. */
JValue     *json_parse(const char *text);

/* Read an entire file into memory and parse it. */
JValue     *json_parse_file(const char *filename);

/* Free a JValue tree recursively. */
void        json_free(JValue *v);

/* Object member lookup by key. Returns NULL if missing. */
JValue     *json_get(const JValue *obj, const char *key);

/* Array element by index. Returns NULL if out of bounds. */
JValue     *json_at(const JValue *arr, int index);

/* Type-safe extractors with fallback defaults. */
double      json_number(const JValue *v, double fallback);
long long   json_int(const JValue *v, long long fallback);
const char *json_string(const JValue *v, const char *fallback);
int         json_bool_val(const JValue *v, int fallback);
int         json_length(const JValue *v);

#endif /* JSON_PARSER_H */
