// The little JSON the transports need (T104.1 D3): one flat object of string, number, boolean and null values - the arguments of an operation - and,
// for JSON-RPC, a request envelope whose "params" is such an object. Nothing nested beyond that, no arrays: an argument is a typed scalar (D1), and a
// request that is anything else is refused with a sentence. Bounded and allocation free, so a hostile body costs a refusal and nothing else.
#ifndef COREAPP_SVCJSON_H
#define COREAPP_SVCJSON_H

#include <stdint.h>
#include <stddef.h>

#define JSON_KV_MAX 8
#define JSON_KEY_MAX 32
#define JSON_BUF_MAX 640

enum JsonType { J_STR = 0, J_NUM, J_BOOL, J_NULL, J_RAW };

struct JsonVal {
    uint8_t t;
    const char *s;                        // J_STR: the decoded text (in the object's buffer)
    int32_t n;                            // J_NUM
    bool b;                               // J_BOOL
    bool frac;                            // J_NUM had a fraction or an exponent: not an integer
                                          // J_RAW: an array or an object, not decoded: s points at its first character in the text that was parsed, n is its length
};

struct JsonObj {
    int n;
    char key[JSON_KV_MAX][JSON_KEY_MAX];
    JsonVal v[JSON_KV_MAX];
    char buf[JSON_BUF_MAX];
    int used;
    const JsonVal *get(const char *k) const;
};

// A request of JSON-RPC 2.0: {"jsonrpc":"2.0","id":..,"method":"..","params":{..}}. `id` is kept as JSON text, ready to be echoed; has_id false
// for a notification.
struct JsonRpcReq {
    bool has_id;
    char id[48];
    char method[64];
    bool has_params;
    JsonObj params;
    bool version_ok;
};

// Parses the flat object in s[0..n). 0, or -1 and a sentence in why.
int json_parse_object(const char *s, int n, JsonObj *o, char *why, int whylen);
// -1 with a JSON-RPC code in *code (-32700 parse error, -32600 invalid request) and a sentence; 0 on success.
int json_parse_rpc(const char *s, int n, JsonRpcReq *r, int *code, char *why, int whylen);

// Appends `text` as a JSON string (quotes included) to out[*pos..cap), escaping what JSON needs. Returns false when it does not fit; *pos is
// then where it stood.
bool json_put_string(char *out, int cap, int *pos, const char *text);
// Appends formatted text; false when it does not fit (then *pos is unchanged).
bool json_put(char *out, int cap, int *pos, const char *fmt, ...) __attribute__((format(printf, 4, 5)));

// The element `idx` of the JSON array in raw[0..n) (a J_RAW value): its text, or false past the end / on text that is not an array. Elements are scalars or objects.
bool json_array_item(const char *raw, int n, int idx, const char **item, int *ilen);

// base64 (RFC 4648, with padding) of n bytes into out (NUL terminated); returns its length, -1 if cap is too small.
int b64_encode(const uint8_t *d, int n, char *out, int cap);
// The other way: the bytes of a base64 text (padding optional, whitespace not allowed); -1 for a bad character or when cap is too small.
int b64_decode(const char *s, uint8_t *out, int cap);

#endif
