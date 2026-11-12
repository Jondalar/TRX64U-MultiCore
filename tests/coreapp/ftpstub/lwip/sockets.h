// Test stand-in for lwIP's socket API (T186): ftp_client.cc is compiled on the Mac against the scripted server model of test_ftp.cc.
#ifndef FTPSTUB_LWIP_SOCKETS_H
#define FTPSTUB_LWIP_SOCKETS_H
#include <stddef.h>
#include <stdint.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>

int fk_socket(int, int, int);
int fk_setsockopt(int, int, int, const void *, socklen_t);
long fk_recv(int, void *, size_t, int);
long fk_send(int, const void *, size_t, int);
int lwip_connect(int, const struct sockaddr *, socklen_t);
int lwip_close(int);
int lwip_gethostbyname_r(const char *, struct hostent *, char *, size_t, struct hostent **, int *);

#define socket fk_socket
#define setsockopt fk_setsockopt
#define recv fk_recv
#define send fk_send
#endif
