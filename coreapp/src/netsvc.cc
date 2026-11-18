#include "netsvc.h"
#include "dmasvc.h"
#include "rest.h"
#include "network_config.h"
#include "network_interface.h"
#include "syslog.h"
#include "product.h"
#include "itu.h"
#include "u64.h"
#include "versions.h"
#include "trx_io.h"
#include "json.h"
#include "FreeRTOS.h"
#include "task.h"
#include "socket.h"
#include "netdb.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>

Syslog syslog;                                          // the instance syslog.h declares extern
static volatile bool g_syslog_on;

void netsvc_log_char(int c)
{
    if (g_syslog_on) {
        syslog.charout(c);
    }
}

// ---- what REST asks of the store -------------------------------------------------------------------------------------------------------------------------
static const char *rest_password(void)
{
    return networkConfig.cfg->get_string(CFG_NETWORK_PASSWORD);
}
static bool rest_enabled(void)
{
    return networkConfig.cfg->get_value(CFG_NETWORK_HTTP_SERVICE) != 0;
}
static const char *unique_id(void)
{
    const char *u = networkConfig.cfg->get_string(CFG_NETWORK_UNIQUE_ID);
    if (u && !strcmp(u, "Default")) {
        return getProductUniqueId();
    }
    return u;
}

// ---- Ultimate ident, UDP 64 (FW socket_dma.cc:531-640): the answer to "json..." is a JSON object, any other text is echoed with the host name and the title ------
static void ident_task(void *)
{
    char menu_header[64];
    getProductTitleString(menu_header, sizeof(menu_header), true);
    for (;;) {
        while (!networkConfig.cfg->get_value(CFG_NETWORK_ULTIMATE_IDENT_SERVICE)) {
            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }
        int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0) {
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            continue;
        }
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv));
        struct sockaddr_in serv, cli;
        memset(&serv, 0, sizeof(serv));
        serv.sin_family = AF_INET;
        serv.sin_addr.s_addr = INADDR_ANY;
        serv.sin_port = htons(64);
        if (bind(sockfd, (struct sockaddr *)&serv, sizeof(serv)) < 0) {
            lwip_close(sockfd);
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }
        while (networkConfig.cfg->get_value(CFG_NETWORK_ULTIMATE_IDENT_SERVICE)) {
            char msg[256];
            socklen_t cl = sizeof(cli);
            int n = recvfrom(sockfd, msg, sizeof(msg) - 1, 0, (struct sockaddr *)&cli, &cl);
            if (n < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    vTaskDelay(500);
                }
                continue;
            }
            msg[n] = 0;
            const char *hostname = networkConfig.cfg->get_string(CFG_NETWORK_HOSTNAME);
            if (!strncmp(msg, "json", 4)) {
                char product[64];
                getProductVersionString(product, sizeof(product), true);
                char fpga_version[8];
                snprintf(fpga_version, sizeof(fpga_version), "1%02x", getFpgaVersion());
                char core_version[8];
                snprintf(core_version, sizeof(core_version), "1.%02x", C64_CORE_VERSION);
                if (n > 36) {
                    msg[36] = 0;
                }
                JSON_Object *obj = JSON::Obj();
                obj->add("product", product)->add("firmware_version", APPL_VERSION_ASCII)->add("fpga_version", fpga_version)->add("core_version", core_version)
                   ->add("hostname", hostname)->add("menu_header", menu_header)->add("your_string", msg + 4);
                if (*networkConfig.cfg->get_string(CFG_NETWORK_PASSWORD)) {
                    obj->add("password_protected", true);                // FW socket_dma.cc:607: the flag only; ident itself is not guarded
                }
                const char *uid = unique_id();
                if (uid && *uid) {
                    obj->add("unique_id", uid);
                }
                JSON_Object *trx = trx_io_ident();
                if (trx) {
                    obj->add("trx64", trx);
                }
                const char *out = obj->render();
                sendto(sockfd, out, strlen(out), 0, (struct sockaddr *)&cli, cl);
                delete obj;
            } else {
                if (n > 32) {
                    n = 32;
                }
                snprintf(msg + n, sizeof(msg) - n, ",%s,%s", hostname, menu_header);
                sendto(sockfd, msg, strlen(msg), 0, (struct sockaddr *)&cli, cl);
            }
        }
        lwip_close(sockfd);
    }
}

void netsvc_apply(CoreCtx *c)
{
    static bool started;
    if (started || !networkConfig.cfg) {
        return;
    }
    started = true;
    RestHooks h;
    h.password = rest_password;
    h.enabled = rest_enabled;
    h.unique_id = unique_id;
    rest_set_hooks(h);
    xTaskCreate(ident_task, "UDP Ident Task", 2 * configMINIMAL_STACK_SIZE, NULL, PRIO_NETSERVICE, NULL);
    dmasvc_start(c);                                                // the Ultimate DMA service (TCP 64): only for a core that has the dma.* operations
    g_syslog_on = syslog.init(8192);                                // FW syslog.cc:14-55: only with a valid server; the setting is read here, once
}
