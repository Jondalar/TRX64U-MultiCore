#include "dmasvc.h"
#include "ops_glue.h"
#include "network_config.h"
#include "filemanager.h"
#include "flash.h"
#include "product.h"
#include "itu.h"
#include "u64.h"
#include "FreeRTOS.h"
#include "task.h"
#include "socket.h"
#include "newfile.h"                 // nf_copy: no string precision in a format (the board's printf has none)
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The command words and their order follow FW socket_dma.cc:24-55. 0xFF71 replaces the SID player's image (dma.sidcrt). 0xFF72 is read and dropped: FW's boot cartridge buffer is overwritten with its
// built-in image before every DMA load (c64_subsys.cc:1027), so the sent bytes never run.
#define CMD_DMA 0xFF01
#define CMD_DMARUN 0xFF02
#define CMD_KEYB 0xFF03
#define CMD_RESET 0xFF04
#define CMD_WAIT 0xFF05
#define CMD_DMAWRITE 0xFF06
#define CMD_REUWRITE 0xFF07
#define CMD_KERNALWRITE 0xFF08
#define CMD_DMAJUMP 0xFF09
#define CMD_MOUNT_IMG 0xFF0A
#define CMD_RUN_IMG 0xFF0B
#define CMD_POWEROFF 0xFF0C
#define CMD_RUN_CRT 0xFF0D
#define CMD_IDENTIFY 0xFF0E
#define CMD_AUTHENTICATE 0xFF1F
#define CMD_VICSTREAM_ON 0xFF20
#define CMD_AUDIOSTREAM_ON 0xFF21
#define CMD_DEBUGSTREAM_ON 0xFF22
#define CMD_VICSTREAM_OFF 0xFF30
#define CMD_AUDIOSTREAM_OFF 0xFF31
#define CMD_DEBUGSTREAM_OFF 0xFF32
#define CMD_LOADSIDCRT 0xFF71
#define CMD_LOADBOOTCRT 0xFF72
#define CMD_READFLASH 0xFF75
#define CMD_DEBUG_REG 0xFF76

#define BUFFER_SIZE 200000                       // FW SOCKET_BUFFER_SIZE (socket_dma.h)

static CoreCtx *g_c;

static bool dma_enabled(void)
{
    return networkConfig.cfg->get_value(CFG_NETWORK_ULTIMATE_DMA_SERVICE) != 0;
}

static void set_timeouts(int fd)
{
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv, sizeof(tv));
}

// FW :369-389: the same loops, in pieces of 8192 and 2048 bytes.
static int read_socket(int fd, void *buffer, int max_remain)
{
    uint8_t *buf = (uint8_t *)buffer;
    int received = 0;
    while (max_remain > 0) {
        int n = recv(fd, buf, max_remain > 8192 ? 8192 : max_remain, 0);
        if (n <= 0) {
            return received > 0 ? received : n;
        }
        buf += n;
        max_remain -= n;
        received += n;
    }
    return received;
}

static int write_socket(int fd, const void *buffer, int length)
{
    const uint8_t *buf = (const uint8_t *)buffer;
    int sent = 0;
    while (length > 0) {
        int n = send(fd, buf, length > 2048 ? 2048 : length, 0);
        if (n <= 0) {
            return sent > 0 ? sent : n;
        }
        buf += n;
        length -= n;
        sent += n;
    }
    return sent;
}

// An operation of the core. A refusal is logged and goes no further: FW answers none of the load, run, mount and write commands either.
static int call(const char *op, const OpIn *in, int n)
{
    OpResult r;
    int rc = ops_call(g_c, op, in, n, &r);
    if (rc) {
        printf("[dma] %s: %s\n", op, r.msg);
    }
    return rc;
}

// FW :179-209: the body as a file in the Temp folder (FileManager::save_temp_file, category "socket"); the path comes back.
static bool stage(const char *name, const uint8_t *buf, uint32_t len, char *path, int n)
{
    mstring tp;
    if (FileManager::getFileManager()->save_temp_file("socket", name, buf, len, &tp) != FR_OK) {
        printf("[dma] the Temp folder did not take %s\n", name);
        return false;
    }
    snprintf(path, n, "%s", tp.c_str());
    return true;
}

static void dma_file(const uint8_t *buf, uint32_t len, int mode)
{
    char path[FS_PATH_MAX];
    if (len >= 2 && stage("tcpdma.prg", buf, len, path, sizeof(path))) {
        OpIn in[2] = { in_str("path", path), in_int("mode", mode) };
        call("dma.prg", in, 2);
    }
}

static void stream_on(int id, int stop_id, uint8_t *buf, uint32_t len)
{
    if (stop_id >= 0) {
        OpIn s[1] = { in_int("id", stop_id) };
        call("stream.stop", s, 1);
    }
    char dest[37];
    dest[0] = 0;
    if (len > 2) {                                             // FW :210-216: the destination follows the two bytes of the stop-after
        nf_copy(dest, sizeof(dest), (const char *)buf + 2, (size_t)(len - 2 < 36 ? len - 2 : 36));
    }
    OpIn in[3] = { in_int("id", id), in_str("dest", dest), in_int("stop_after", len >= 2 ? (buf[0] | (buf[1] << 8)) : 0) };
    call("stream.start_to", in, 3);
}

static void stream_off(int id)
{
    OpIn s[1] = { in_int("id", id) };
    call("stream.stop", s, 1);
}

// FW :124-366. False = the connection ends (a command before the password, a wrong password).
static bool perform(int fd, uint8_t *buf, uint16_t cmd, uint32_t len, bool &authenticated)
{
    char path[FS_PATH_MAX];
    if (!authenticated && cmd != CMD_AUTHENTICATE) {
        return false;
    }
    switch (cmd) {
    case CMD_AUTHENTICATE: {
        const char *password = networkConfig.cfg->get_string(CFG_NETWORK_PASSWORD);
        bool ok = !*password || (strlen(password) == len && !memcmp(password, buf, len));
        uint8_t answer = ok ? 1 : 0;
        if (ok) {
            authenticated = true;
        } else {
            vTaskDelay(1000 / portTICK_PERIOD_MS);              // throttles failed attempts
        }
        write_socket(fd, &answer, 1);
        return ok;
    }
    case CMD_IDENTIFY: {
        char title[52];
        getProductTitleString(title + 1, sizeof(title) - 1, true);
        title[0] = (char)strlen(title + 1);
        write_socket(fd, title, 1 + title[0]);
        break;
    }
    case CMD_DMA:
        dma_file(buf, len, 0);
        break;
    case CMD_DMARUN:
        dma_file(buf, len, 1);
        break;
    case CMD_DMAJUMP:
        dma_file(buf, len, 2);
        break;
    case CMD_DMAWRITE:
        if (len > 2) {
            OpIn in[2] = { in_int("address", buf[0] | (buf[1] << 8)), in_blob("data", buf + 2, len - 2) };
            call("dma.write", in, 2);
        }
        break;
    case CMD_KEYB: {
        if (len) {
            OpIn k[2] = { in_int("address", 0x0277), in_blob("data", buf, len) };
            call("dma.write", k, 2);
        }
        buf[0] = (uint8_t)len;
        OpIn n[2] = { in_int("address", 0x00C6), in_blob("data", buf, 1) };
        call("dma.write", n, 2);
        break;
    }
    case CMD_RESET:
        call("machine.warm_reset", 0, 0);
        break;
    case CMD_POWEROFF:
        call("dma.power_off", 0, 0);
        break;
    case CMD_WAIT:
        vTaskDelay(len);
        break;
    case CMD_REUWRITE:
        if (len > 3) {
            OpIn in[2] = { in_int("offset", buf[0] | (buf[1] << 8) | (buf[2] << 16)), in_blob("data", buf + 3, len - 3) };
            call("dma.reu", in, 2);
        }
        break;
    case CMD_KERNALWRITE:
        if (len >= 2 + 8192) {
            OpIn in[1] = { in_blob("data", buf + 2, 8192) };
            call("dma.kernal", in, 1);
        }
        break;
    case CMD_LOADSIDCRT:
        if (len) {
            OpIn in[1] = { in_blob("data", buf, len > 0x2000 ? 0x2000 : len) };
            call("dma.sidcrt", in, 1);
        }
        break;
    case CMD_LOADBOOTCRT:
        break;
    case CMD_MOUNT_IMG:
    case CMD_RUN_IMG:
        if (stage("tcpimage.d64", buf, len, path, sizeof(path))) {
            OpIn in[2] = { in_str("path", path), in_int("run", cmd == CMD_RUN_IMG) };
            call("dma.mount", in, 2);
        }
        break;
    case CMD_RUN_CRT:
        if (stage("tcpimage.crt", buf, len, path, sizeof(path))) {
            OpIn in[1] = { in_str("path", path) };
            call("dma.crt", in, 1);
        }
        break;
    case CMD_VICSTREAM_ON:
        stream_on(0, 2, buf, len);                              // the debug stream off first
        break;
    case CMD_AUDIOSTREAM_ON:
        stream_on(1, -1, buf, len);
        break;
    case CMD_DEBUGSTREAM_ON:
        stream_on(2, 0, buf, len);                              // the VIC stream off first
        break;
    case CMD_VICSTREAM_OFF:
        stream_off(0);
        break;
    case CMD_AUDIOSTREAM_OFF:
        stream_off(1);
        break;
    case CMD_DEBUGSTREAM_OFF:
        stream_off(2);
        break;
    case CMD_DEBUG_REG: {
        uint8_t reg = U64_DEBUG_REGISTER;
        write_socket(fd, &reg, 1);
        if (len > 0) {
            U64_DEBUG_REGISTER = buf[0];
        }
        break;
    }
    case CMD_READFLASH: {
        Flash *flash = get_flash();
        if (!flash || !len) {
            break;
        }
        int psize = flash->get_page_size();
        if (buf[0] == 0 || buf[0] == 1) {
            int v = buf[0] == 0 ? psize : flash->get_number_of_pages();
            uint8_t tmp[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
            write_socket(fd, tmp, 4);
        } else if (buf[0] == 2 && len >= 4) {
            int page = buf[1] | (buf[2] << 8) | (buf[3] << 16);
            uint8_t *page_buf = (uint8_t *)malloc(psize);
            if (page_buf) {
                memset(page_buf, 0, psize);
                flash->read_page(page, page_buf);
                write_socket(fd, page_buf, psize);
                free(page_buf);
            }
        }
        break;
    }
    }
    return true;
}

static void serve(int fd, uint8_t *mem)
{
    bool authenticated = !*networkConfig.cfg->get_string(CFG_NETWORK_PASSWORD);
    uint8_t buf[3];
    while (dma_enabled()) {
        if (recv(fd, buf, 2, 0) <= 0) {                         // an idle client is let go after the second of the socket's timeout (FW :481-485)
            break;
        }
        uint16_t cmd = (uint16_t)(buf[0] | (buf[1] << 8));
        if ((cmd & 0xFF00) != 0xFF00) {
            printf("[dma] illegal command %04x\n", cmd);
            continue;
        }
        if (recv(fd, buf, 2, 0) <= 0) {
            break;
        }
        uint32_t len = (uint32_t)(buf[0] | (buf[1] << 8));
        if (cmd == CMD_MOUNT_IMG || cmd == CMD_RUN_IMG || cmd == CMD_RUN_CRT) {
            recv(fd, buf + 2, 1, 0);                            // the third length byte of the three that bring a file
            len |= (uint32_t)buf[2] << 16;
        }
        if (len > BUFFER_SIZE) {
            len = BUFFER_SIZE;
        }
        if (len && read_socket(fd, mem, (int)len) <= 0) {
            break;
        }
        mem[len] = 0;
        if (!perform(fd, mem, cmd, len, authenticated)) {
            break;
        }
    }
}

static void dma_task(void *)
{
    for (;;) {
        while (!dma_enabled()) {
            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }
        uint8_t *mem = (uint8_t *)malloc(BUFFER_SIZE + 1);
        int fd = mem ? socket(AF_INET, SOCK_STREAM, 0) : -1;
        if (fd < 0) {
            free(mem);
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            continue;
        }
        int reuse = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
        set_timeouts(fd);
        struct sockaddr_in serv;
        memset(&serv, 0, sizeof(serv));
        serv.sin_family = AF_INET;
        serv.sin_addr.s_addr = INADDR_ANY;
        serv.sin_port = htons(64);
        if (bind(fd, (struct sockaddr *)&serv, sizeof(serv)) < 0) {
            printf("[dma] port 64 is taken\n");
            lwip_close(fd);
            free(mem);
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            continue;
        }
        listen(fd, 5);
        printf("[dma] Ultimate DMA service on TCP 64\n");
        while (dma_enabled()) {
            struct sockaddr_in cli;
            socklen_t cl = sizeof(cli);
            int nfd = accept(fd, (struct sockaddr *)&cli, &cl);
            if (nfd < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    vTaskDelay(100 / portTICK_PERIOD_MS);
                }
                continue;
            }
            if (!dma_enabled()) {
                lwip_close(nfd);
                break;
            }
            set_timeouts(nfd);
            serve(nfd, mem);
            lwip_close(nfd);
        }
        lwip_close(fd);
        free(mem);
    }
}

void dmasvc_start(CoreCtx *c)
{
    static bool started;
    OpRegistry *reg = ops_of(c);
    if (started || !reg || !reg->find_op("dma.prg")) {
        return;
    }
    started = true;
    g_c = c;
    xTaskCreate(dma_task, "DMA Load Task", 4 * configMINIMAL_STACK_SIZE, NULL, PRIO_NETSERVICE, NULL);
}
