// PROVENANCE: vendor/gideon/software/network/http_request.h (Gideon Zweijtzer's firmware, GPL-3.0), T104.2 UI: the HTTP client helpers of the Assembly64 / CommoServe search. Changed: the class HttpRequest (a user of
// StreamRamFile, which this application does not have) is dropped; the helpers the Assembly client calls stay as they were.
#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <sys/socket.h>
extern "C" {
    #include "server.h"
    #include "multipart.h"
}
#include "json.h"

typedef struct {
    int offset;
    int size;
    uint8_t buffer[16384];
} t_BufferedBody;

// Callbacks
void collect_in_buffer(HTTPReqMessage *req, HTTPRespMessage *resp);
//void write_to_temp(HTTPReqMessage *req, HTTPRespMessage *resp);

int   read_socket(int socket_fd, HTTPReqMessage& response);
void  get_response(int socket_fd, HTTPREQ_CALLBACK callback, HTTPReqMessage& response);
JSON *convert_buffer_to_json(t_BufferedBody *body);

#endif
