// PROVENANCE: copied from vendor/gideon/software/network/ftp_client.h (the 1541ultimate firmware, GPL-3.0; the client came upstream in pull request #662 by the TRX64 author, merged 2026-03-17, and was taken into the virtual file system in #695), T104.2 UI: the FTP client of the Library's FTP medium (the library "ftp", ftp_exports.cc).
// T186: peer_ip, drop_control() and finish_transfer() added, adapted from omnicron-c64/ultimate-community-edition f7f665ab (GPL-3.0), see ftp_client.cc.
#ifndef FTP_CLIENT_H
#define FTP_CLIENT_H

#include <stdint.h>
#include "filemanager.h"

#define FTP_MAX_RESPONSE 512

class FTPClient
{
    int ctrl_fd;
    char response[FTP_MAX_RESPONSE];
    int response_code;
    uint32_t peer_ip;   // the control connection's peer, network byte order

    int read_response();
    int send_cmd(const char *cmd);
    int send_cmd(const char *cmd, const char *arg);
    int send_all(int fd, const void *buf, int len);
    int open_data_connection();
    int parse_pasv(uint32_t &ip, uint16_t &port);
    void drop_control();
    int finish_transfer(int data_fd, bool ok);

public:
    FTPClient();
    ~FTPClient();

    int open(const char *host, uint16_t port);
    int login(const char *user, const char *pass);
    void disconnect();
    bool is_connected();

    int cwd(const char *path);
    int pwd(char *buf, int bufsize);
    int list(const char *path, char *buf, int bufsize, int *bytes_read);
    int retr(const char *remote_path, File *file, int *bytes_read);
    int stor(const char *remote_path, File *file);
    int dele(const char *path);
    int mkd(const char *path);
    int rmd(const char *path);
    int rnfr_rnto(const char *from, const char *to);
    int type_binary();

    const char *last_response() { return response; }
    int last_code() { return response_code; }
};

#endif
