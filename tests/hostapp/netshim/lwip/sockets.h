#ifndef NETSHIM_SOCKETS_H
#define NETSHIM_SOCKETS_H
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <errno.h>
extern "C" {
int shim_socket(int, int, int);
int shim_setsockopt(int, int, int, const void *, socklen_t);
int shim_bind(int, const struct sockaddr *, socklen_t);
int shim_listen(int, int);
int shim_accept(int, struct sockaddr *, socklen_t *);
ssize_t shim_recv(int, void *, size_t, int);
ssize_t shim_send(int, const void *, size_t, int);
int shim_close(int);
}
#define socket shim_socket
#define setsockopt shim_setsockopt
#define bind shim_bind
#define listen shim_listen
#define accept shim_accept
#define recv shim_recv
#define send shim_send
#define closesocket shim_close
#endif
