/*
 * json.h — a small JSON reader (tokens over the source text, nothing copied)
 * and a JSON writer into an sbuf_t. Enough for JSON-RPC 2.0 requests.
 */
#ifndef TRXMON_JSON_H
#define TRXMON_JSON_H

#include "mlib.h"

enum { JT_NONE = 0, JT_OBJ, JT_ARR, JT_STR, JT_NUM, JT_TRUE, JT_FALSE, JT_NULL };

typedef struct
{
    uint8_t type;
    int start, end; /* the token's text: a string without its quotes */
    int size;       /* object: members, array: items */
    int next;       /* index of the first token after this one's subtree */
} jtok_t;

/* Tokenizes one JSON value (surrounding white space allowed). Returns the
 * number of tokens, or -1 for bad JSON, -2 when max tokens are not enough.
 * An object's members are key, value, key, value ... */
int json_parse(const char *s, int len, jtok_t *t, int max);

/* The value of `key` in the object at index obj, or -1. */
int json_get(const char *s, const jtok_t *t, int obj, const char *key);
/* The k-th item of the array at index arr, or -1. */
int json_item(const jtok_t *t, int arr, int k);

int json_is(const jtok_t *t, int i, int type);
/* An integer number (no fraction, no exponent) that fits in 64 bits. */
int json_u64(const char *s, const jtok_t *t, int i, uint64_t *v);
int json_u32(const char *s, const jtok_t *t, int i, uint32_t *v);
/* A string, unescaped into buf (NUL-terminated). Returns its length, -1 if
 * the token is not a string or does not fit. \u escapes above 0x7F become '?'. */
int json_str(const char *s, const jtok_t *t, int i, char *buf, int cap);
/* Compares a string token with a C string, without unescaping. */
int json_streq(const char *s, const jtok_t *t, int i, const char *lit);

/* Writer. Commas are placed by the writer. */
typedef struct
{
    sbuf_t *b;
    uint8_t first[24];
    int depth;
    int after_key;
} jw_t;

void jw_init(jw_t *w, sbuf_t *b);
void jw_obj(jw_t *w);
void jw_end_obj(jw_t *w);
void jw_arr(jw_t *w);
void jw_end_arr(jw_t *w);
void jw_key(jw_t *w, const char *k);
void jw_str(jw_t *w, const char *s);
void jw_strn(jw_t *w, const char *s, int n);
void jw_u64(jw_t *w, uint64_t v);
void jw_i64(jw_t *w, int64_t v);
void jw_bool(jw_t *w, int v);
void jw_null(jw_t *w);
void jw_raw(jw_t *w, const char *s, int n); /* a value already in JSON form */
/* Shorthands for one member */
void jw_kstr(jw_t *w, const char *k, const char *v);
void jw_ku64(jw_t *w, const char *k, uint64_t v);
void jw_kbool(jw_t *w, const char *k, int v);
void jw_knull(jw_t *w, const char *k);

#endif
