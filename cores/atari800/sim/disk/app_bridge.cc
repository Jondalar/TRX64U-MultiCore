// T108 D1: the application's SIO responder (coreapp/cores/atari_sio.cc, unmodified) driving the register window of the
// simulated Atari core. GHDL calls in through VHPIDIRECT (tb_a8_disk.vhd). The engine is written as straight code that
// blocks on every bridge access, so it runs in a thread of its own: each access becomes a request to the testbench, which
// performs it on the core's register window, lets the right amount of simulated time pass, and answers.
//
//   app_init()            reads the bench's environment, opens the disk, starts the engine
//   app_kick(now_us)      one service call, as a8_service does it from the application's loop
//   app_next(now_us)      the next thing the engine wants: 0 done, 1 read reg, 2 write reg, 3 wait us
//   app_reply(v)          the answer to a read / the end of a write or a wait
// Environment: A8_ATR = path of the image mounted to D1: (unset: nothing mounted), A8_GHOST = a drive that answers although
// no image is mounted (the negative control of S2).
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <vector>
#include "atari_atr.h"
#include "atari_sio.h"
#include "atari800_regs.h"

namespace {

// ---- a file system over stdio, enough for atr_open
class StdFile : public CoreFile {
public:
    explicit StdFile(FILE *f) : f_(f) { fseek(f, 0, SEEK_END); size_ = (uint64_t)ftell(f); }
    ~StdFile() { fclose(f_); }
    uint64_t size() { return size_; }
    bool writable() { return false; }
    bool read(uint64_t off, void *buf, uint32_t len)
    {
        return off + len <= size_ && !fseek(f_, off, SEEK_SET) && fread(buf, 1, len, f_) == len;
    }
    bool write(uint64_t, const void *, uint32_t) { return false; }
    bool sync() { return true; }
private:
    FILE *f_;
    uint64_t size_;
};

class StdFs : public CoreFs {
public:
    int list(const char *, FsEntry *, int) { return -1; }
    bool stat(const char *, uint64_t *, bool *) { return false; }
    int load(const char *, void *, uint32_t) { return -1; }
    bool save(const char *, const void *, uint32_t) { return false; }
    bool remove(const char *) { return false; }
    CoreFile *open(const char *path, bool) { FILE *f = fopen(path, "rb"); return f ? new StdFile(f) : 0; }
    void close(CoreFile *f) { delete f; }
};

// A drive that is not mounted but answers: sectors of zeros. Only the control of S2 uses it.
class GhostDisk : public AtariDisk {
public:
    uint32_t sectors() { return 720; }
    uint16_t sector_size(uint32_t) { return 128; }
    uint16_t image_sector_size() { return 128; }
    bool read(uint32_t, uint8_t *buf) { memset(buf, 0, 128); return true; }
    bool write(uint32_t, const uint8_t *) { return false; }
    bool writable() { return false; }
    const char *name() { return "ghost"; }
};

// ---- the hand-over between the testbench (the simulator's thread) and the engine's thread
pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
enum { K_NONE, K_RD, K_WR, K_WAIT, K_DONE };
int req_kind = K_NONE, req_a = 0, req_d = 0;       // posted by the engine, taken by the testbench
bool req_ready = false;
bool rep_ready = false;
int rep_v = 0;
bool kick = false;
uint32_t vnow_us = 0;                              // simulated time, as the testbench last said it

int post_and_wait(int kind, int a, int d)
{
    pthread_mutex_lock(&mu);
    req_kind = kind; req_a = a; req_d = d; req_ready = true;
    pthread_cond_broadcast(&cv);
    while (!rep_ready) {
        pthread_cond_wait(&cv, &mu);
    }
    rep_ready = false;
    int v = rep_v;
    pthread_mutex_unlock(&mu);
    return v;
}

class BenchIo : public CoreIo {
public:
    uint8_t rd(uint32_t reg) { return (uint8_t)post_and_wait(K_RD, (int)reg, 0); }
    void wr(uint32_t reg, uint8_t v) { post_and_wait(K_WR, (int)reg, v); }
    void ddr_write(uint32_t, const void *, uint32_t) { abort(); }
    void ddr_read(uint32_t, void *, uint32_t) { abort(); }
    void rom_write(uint32_t, const void *, uint32_t) { abort(); }
    void rom_read(uint32_t, void *, uint32_t) { abort(); }
    void ram_write(uint32_t, const void *, uint32_t) { abort(); }
    void ram_read(uint32_t, void *, uint32_t) { abort(); }
    void stop(bool) { abort(); }
    void mode_reset(bool) { abort(); }
    uint32_t now_ms() { return vnow_us / 1000; }
    void delay_ms(uint32_t ms) { post_and_wait(K_WAIT, 0, (int)ms * 1000); }
    void delay_us(uint32_t us) { post_and_wait(K_WAIT, 0, (int)us); }
};

BenchIo io;
StdFs fs;
SioLink *link_;
SioEngine *eng;
AtariDisk *drives[SIO_DRIVES];
GhostDisk ghost;

void *engine_main(void *)
{
    for (;;) {
        pthread_mutex_lock(&mu);
        while (!kick) {
            pthread_cond_wait(&cv, &mu);
        }
        kick = false;
        pthread_mutex_unlock(&mu);
        bool busy = eng->poll();
        eng->flush_idle(1000);
        pthread_mutex_lock(&mu);
        req_kind = K_DONE; req_a = 0; req_d = busy ? 1 : 0; req_ready = true;
        pthread_cond_broadcast(&cv);
        pthread_mutex_unlock(&mu);
    }
    return 0;
}

}

extern "C" {

int app_init(void)
{
    memset(drives, 0, sizeof(drives));
    const char *atr = getenv("A8_ATR");
    if (atr && *atr) {
        char err[200] = "";
        drives[0] = atr_open(&fs, atr, err, sizeof(err));
        if (!drives[0]) {
            printf("app: cannot mount %s: %s\n", atr, err);
            fflush(stdout);
            return 1;
        }
        printf("app: D1: %s, %u sectors of %u bytes\n", drives[0]->name(), (unsigned)drives[0]->sectors(), drives[0]->image_sector_size());
    } else if (getenv("A8_GHOST")) {
        drives[0] = &ghost;
        printf("app: D1: empty, but a ghost drive answers (control)\n");
    } else {
        printf("app: D1: empty\n");
    }
    fflush(stdout);
    link_ = new SioLink(&io);
    eng = new SioEngine(link_, drives);
    pthread_t t;
    pthread_create(&t, 0, engine_main, 0);
    return 0;
}

int app_kick(int now_us)
{
    pthread_mutex_lock(&mu);
    vnow_us = (uint32_t)now_us;
    kick = true;
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&mu);
    return 0;
}

// bits 31:28 kind, 27:16 register or flag, 15:0 data; a wait of more than 65535 us is never asked for
int app_next(int now_us)
{
    pthread_mutex_lock(&mu);
    while (!req_ready) {
        pthread_cond_wait(&cv, &mu);
    }
    req_ready = false;
    vnow_us = (uint32_t)now_us;
    int r = (req_kind << 28) | ((req_a & 0xFFF) << 16) | (req_d & 0xFFFF);
    pthread_mutex_unlock(&mu);
    return r;
}

int app_reply(int v)
{
    pthread_mutex_lock(&mu);
    rep_v = v; rep_ready = true;
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&mu);
    return 0;
}

// 0 frames the engine saw, 1 bad checksums, 2 unknown commands, 3 bridge timeouts
int app_stat(int i)
{
    switch (i) {
    case 0: return eng->frames();
    case 1: return eng->bad_checksums();
    case 2: return eng->unknown_commands();
    default: return link_->failures();
    }
}

}
