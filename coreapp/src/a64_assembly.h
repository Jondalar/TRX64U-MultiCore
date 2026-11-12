// PROVENANCE: vendor/gideon/software/network/assembly.h (Gideon Zweijtzer's firmware, GPL-3.0), T104.2 UI: the client of the Assembly64 / CommoServe search. Changed: http_request.h is a64_http.h; the download is held in RAM
// (a64_grow_t) instead of a temp file; the class is otherwise as it was.
#ifndef ASSEMBLY_H
#define ASSEMBLY_H

#include "json.h"
#include "a64_http.h"
#include "product.h"

// a download held in RAM (a binary of the library; at most A64_MAX_DOWNLOAD)
#define A64_MAX_DOWNLOAD (6u << 20)
typedef struct {
    uint8_t *b;
    uint32_t len, cap;
    bool failed;
} a64_grow_t;

class Assembly
{
    JSON *presets;
    int socket_fd;
    mstring client_id;
    HTTPReqMessage response;

    int   connect_to_server(void);
    void  close_connection(void);
    JSON *take_response_json(void);
public:
    Assembly() {
        presets = NULL;
        socket_fd = -1;
        // Which library is searched unless the user picks the other one:
        // Assembly64 for an Ultimate, CommoServe for a C64 Ultimate, which
        // is Commodore's own. The header is what selects it.
        client_id = isCommodoreBoard() ? "Commodore" : "Ultimate";
    }
    ~Assembly() {
        if (presets)
            delete presets;
    }
    void *get_user_context() { return response.userContext; }
    // The last request_binary(): the bytes (the caller frees ->b and the struct), null when it failed.
    a64_grow_t *take_download(void) { a64_grow_t *g = (a64_grow_t *)response.userContext; response.userContext = NULL; return g; }
    const char *get_client_id() { return client_id.c_str(); }
    bool  set_client_id(const char *id);
    JSON *get_presets(void);
    JSON *send_query(const char *query);
    JSON *request_entries(const char *id, int cat);
    void  request_binary(const char *id, int cat, int idx);
    void  request_binary(const char *path, const char *filename);
};

extern Assembly assembly;

#endif // ASSEMBLY_H