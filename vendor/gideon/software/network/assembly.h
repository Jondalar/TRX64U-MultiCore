#ifndef ASSEMBLY_H
#define ASSEMBLY_H

#include "json.h"
#include "http_request.h"
#include "product.h"

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