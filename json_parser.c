/**
 * json_parser.c - Recursive-Descent JSON Parser
 * CMapNav - Terminal Navigation System
 *
 * Parses JSON text into a JValue tree. Handles:
 *   strings (with escape sequences), numbers, booleans, null,
 *   arrays, and objects.
 *
 * The parser is intentionally minimal — no error recovery,
 * no line/column tracking. Sufficient for well-formed
 * Overpass API responses.
 */

#include "json_parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* ── Reader state ────────────────────────────────────────── */

typedef struct {
    const char *d;   /* data pointer  */
    int         p;   /* current index */
} JP;

static void jp_ws(JP *r) {
    while (r->d[r->p] && isspace((unsigned char)r->d[r->p])) r->p++;
}

static char jp_peek(JP *r) { jp_ws(r); return r->d[r->p]; }

/* ── Allocators ──────────────────────────────────────────── */

static JValue *jv_new(JVType t) {
    JValue *v = (JValue *)calloc(1, sizeof(JValue));
    if (v) v->type = t;
    return v;
}

/* forward */
static JValue *jp_value(JP *r);

/* ── String ──────────────────────────────────────────────── */

static JValue *jp_string(JP *r) {
    if (r->d[r->p] != '"') return NULL;
    r->p++;   /* skip opening " */

    /* First pass — measure length (handling escapes) */
    int len = 0, i = r->p;
    while (r->d[i] && r->d[i] != '"') {
        if (r->d[i] == '\\') i++;   /* skip escaped char */
        i++; len++;
    }

    char *s = (char *)malloc((size_t)len + 1);
    if (!s) return NULL;

    /* Second pass — copy with escape resolution */
    int j = 0;
    while (r->d[r->p] && r->d[r->p] != '"') {
        if (r->d[r->p] == '\\') {
            r->p++;
            switch (r->d[r->p]) {
                case '"':  s[j++] = '"';  break;
                case '\\': s[j++] = '\\'; break;
                case '/':  s[j++] = '/';  break;
                case 'b':  s[j++] = '\b'; break;
                case 'f':  s[j++] = '\f'; break;
                case 'n':  s[j++] = '\n'; break;
                case 'r':  s[j++] = '\r'; break;
                case 't':  s[j++] = '\t'; break;
                case 'u':  s[j++] = '?';  r->p += 4; break; /* \uXXXX → '?' */
                default:   s[j++] = r->d[r->p]; break;
            }
        } else {
            s[j++] = r->d[r->p];
        }
        r->p++;
    }
    s[j] = '\0';
    if (r->d[r->p] == '"') r->p++;  /* skip closing " */

    JValue *v  = jv_new(JV_STRING);
    v->string  = s;
    return v;
}

/* ── Number ──────────────────────────────────────────────── */

static JValue *jp_number(JP *r) {
    char *end;
    double num = strtod(r->d + r->p, &end);
    if (end == r->d + r->p) return NULL;
    r->p = (int)(end - r->d);

    JValue *v = jv_new(JV_NUMBER);
    v->number = num;
    return v;
}

/* ── Array ───────────────────────────────────────────────── */

static JValue *jp_array(JP *r) {
    r->p++;          /* skip '[' */
    jp_ws(r);

    JValue *v = jv_new(JV_ARRAY);
    v->items = NULL;
    v->count = 0;

    if (jp_peek(r) == ']') { r->p++; return v; }

    int cap = 64;
    v->items = (JValue **)malloc((size_t)cap * sizeof(JValue *));

    while (1) {
        JValue *item = jp_value(r);
        if (!item) break;

        if (v->count >= cap) {
            cap *= 2;
            v->items = (JValue **)realloc(v->items,
                        (size_t)cap * sizeof(JValue *));
        }
        v->items[v->count++] = item;

        jp_ws(r);
        if (r->d[r->p] == ',') r->p++;
        else break;
    }
    jp_ws(r);
    if (r->d[r->p] == ']') r->p++;
    return v;
}

/* ── Object ──────────────────────────────────────────────── */

static JValue *jp_object(JP *r) {
    r->p++;          /* skip '{' */
    jp_ws(r);

    JValue *v = jv_new(JV_OBJECT);
    v->keys  = NULL;
    v->vals  = NULL;
    v->count = 0;

    if (jp_peek(r) == '}') { r->p++; return v; }

    int cap = 16;
    v->keys = (char   **)malloc((size_t)cap * sizeof(char *));
    v->vals = (JValue **)malloc((size_t)cap * sizeof(JValue *));

    while (1) {
        jp_ws(r);
        if (r->d[r->p] != '"') break;

        /* key */
        JValue *kv = jp_string(r);
        if (!kv) break;
        char *key  = kv->string;
        kv->string = NULL;
        json_free(kv);

        /* colon */
        jp_ws(r);
        if (r->d[r->p] != ':') { free(key); break; }
        r->p++;

        /* value */
        JValue *val = jp_value(r);
        if (!val) { free(key); break; }

        if (v->count >= cap) {
            cap *= 2;
            v->keys = (char   **)realloc(v->keys,
                       (size_t)cap * sizeof(char *));
            v->vals = (JValue **)realloc(v->vals,
                       (size_t)cap * sizeof(JValue *));
        }
        v->keys[v->count] = key;
        v->vals[v->count] = val;
        v->count++;

        jp_ws(r);
        if (r->d[r->p] == ',') r->p++;
        else break;
    }
    jp_ws(r);
    if (r->d[r->p] == '}') r->p++;
    return v;
}

/* ── Dispatch ────────────────────────────────────────────── */

static JValue *jp_value(JP *r) {
    jp_ws(r);
    char c = r->d[r->p];

    if (c == '"')                         return jp_string(r);
    if (c == '{')                         return jp_object(r);
    if (c == '[')                         return jp_array(r);
    if (c == '-' || (c >= '0' && c <= '9')) return jp_number(r);

    if (strncmp(r->d + r->p, "true",  4) == 0) {
        r->p += 4; JValue *v = jv_new(JV_BOOL); v->boolean = 1; return v;
    }
    if (strncmp(r->d + r->p, "false", 5) == 0) {
        r->p += 5; JValue *v = jv_new(JV_BOOL); v->boolean = 0; return v;
    }
    if (strncmp(r->d + r->p, "null",  4) == 0) {
        r->p += 4; return jv_new(JV_NULL);
    }
    return NULL;  /* parse error */
}

/* ══════════════════════════════════════════════════════════
 *  Public API
 * ═════════════════════════════════════════════════════════ */

JValue *json_parse(const char *text) {
    if (!text) return NULL;
    JP r = { text, 0 };
    return jp_value(&r);
}

JValue *json_parse_file(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "  [!] json_parse_file: cannot open '%s'\n", filename);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0) { fclose(f); return NULL; }

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = '\0';
    fclose(f);

    printf("  [*] Parsing %.1f MB of JSON...\n", (double)rd / (1024.0 * 1024.0));

    JValue *v = json_parse(buf);
    free(buf);
    return v;
}

void json_free(JValue *v) {
    if (!v) return;
    switch (v->type) {
    case JV_STRING:
        free(v->string);
        break;
    case JV_ARRAY:
        for (int i = 0; i < v->count; i++) json_free(v->items[i]);
        free(v->items);
        break;
    case JV_OBJECT:
        for (int i = 0; i < v->count; i++) {
            free(v->keys[i]);
            json_free(v->vals[i]);
        }
        free(v->keys);
        free(v->vals);
        break;
    default: break;
    }
    free(v);
}

JValue *json_get(const JValue *obj, const char *key) {
    if (!obj || obj->type != JV_OBJECT || !key) return NULL;
    for (int i = 0; i < obj->count; i++)
        if (strcmp(obj->keys[i], key) == 0) return obj->vals[i];
    return NULL;
}

JValue *json_at(const JValue *arr, int idx) {
    if (!arr || arr->type != JV_ARRAY) return NULL;
    if (idx < 0 || idx >= arr->count)  return NULL;
    return arr->items[idx];
}

double json_number(const JValue *v, double fb) {
    return (v && v->type == JV_NUMBER) ? v->number : fb;
}

long long json_int(const JValue *v, long long fb) {
    return (v && v->type == JV_NUMBER) ? (long long)v->number : fb;
}

const char *json_string(const JValue *v, const char *fb) {
    return (v && v->type == JV_STRING) ? v->string : fb;
}

int json_bool_val(const JValue *v, int fb) {
    return (v && v->type == JV_BOOL) ? v->boolean : fb;
}

int json_length(const JValue *v) {
    if (!v) return 0;
    return (v->type == JV_ARRAY || v->type == JV_OBJECT) ? v->count : 0;
}
