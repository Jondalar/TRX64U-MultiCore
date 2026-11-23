// Base 3.3 of the library API (T239.3 section 5): the groups sync, settings, ctx, io and board that follow `lib` in LibApi (the members added to SvcSys and SvcFs are in libload.cc).
// They are the generic application's own services, the contracts are the comments of the svc_*.h headers. Pure: what needs the
// machine (a mutex, the 1 ms counter, the ESP32, the PLL, the SID expander) is a LibHost virtual, what needs the core is the CoreCtx the loader was given (set_ctx).
#include "libload.h"
#include "ctx.h"
#include "exec.h"
#include "corefolder.h"
#include "descrec.h"
#include "overlay.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

struct LibSvc {
    static LibLoader *L() { return LibLoader::self(); }
    static CoreCtx *C() { return L() ? L()->ctx_ : 0; }
    static CoreIo *IO() { CoreCtx *c = C(); return c ? c->io : 0; }
    static Settings *S() { CoreCtx *c = C(); return c ? c->settings : 0; }
    static bool six(const Settings *s, int ix) { return s && ix >= 0 && ix < s->count(); }

    // ---- sync (svc_sync.h)
    static SvcMutex *mutex_new()
    {
        LibLoader *l = L();
        SvcMutex *m = (SvcMutex *)l->host_->alloc(sizeof(SvcMutex));
        if (!m) {
            return 0;
        }
        m->h = l->host_->mutex_new();
        if (!m->h) {
            l->host_->release(m);
            return 0;
        }
        int owner = l->owner_at(__builtin_return_address(0));
        m->owner = owner;
        l->host_->enter();
        SvcMutex **head = &l->mus_[owner < 0 ? LIB_MAX : owner];
        m->prev = 0;
        m->next = *head;
        if (*head) {
            (*head)->prev = m;
        }
        *head = m;
        l->host_->leave();
        return m;
    }
    static bool mutex_lock(SvcMutex *m, uint32_t timeout_ms) { return m && L()->host_->mutex_lock(m->h, timeout_ms); }
    static void mutex_unlock(SvcMutex *m)
    {
        if (m) {
            L()->host_->mutex_unlock(m->h);
        }
    }
    static void mutex_free(SvcMutex *m)
    {
        if (!m) {
            return;
        }
        LibLoader *l = L();
        l->host_->enter();
        SvcMutex **head = &l->mus_[m->owner < 0 ? LIB_MAX : m->owner];
        if (m->prev) {
            m->prev->next = m->next;
        } else {
            *head = m->next;
        }
        if (m->next) {
            m->next->prev = m->prev;
        }
        l->host_->leave();
        l->host_->mutex_free(m->h);
        l->host_->release(m);
    }
    static void enter() { L()->host_->enter(); }
    static void leave() { L()->host_->leave(); }

    // ---- settings (svc_settings.h)
    static uint32_t generation() { Settings *s = S(); return s ? s->generation() : 0; }
    static int find(const char *key) { Settings *s = S(); return s && key ? s->find(key) : -1; }
    static int value(int ix)
    {
        Settings *s = S();
        return six(s, ix) && !s->item(ix).kind ? s->value(ix) : -1;
    }
    static const char *text(int ix)
    {
        Settings *s = S();
        return six(s, ix) ? s->text(ix) : "";
    }
    static int layer(int ix)
    {
        Settings *s = S();
        return six(s, ix) ? (int)s->layer(ix) : -1;
    }
    static bool set(int ix, int v)
    {
        Settings *s = S();
        return six(s, ix) && !s->item(ix).kind && s->set(ix, v);
    }
    static void override_value(int ix, int v)
    {
        Settings *s = S();
        if (six(s, ix)) {
            s->override_value(ix, v);
        }
    }
    static void apply(int ix)
    {
        LibLoader *l = L();
        CoreCtx *c = l->ctx_;
        if (c && six(c->settings, ix) && l->apply_) {
            l->apply_(c, ix);
        }
    }
    static bool info(int ix, int *nvalues, int *base)
    {
        Settings *s = S();
        if (!six(s, ix)) {
            return false;
        }
        if (nvalues) {
            *nvalues = s->item(ix).nvalues;
        }
        if (base) {
            *base = s->item(ix).base;
        }
        return true;
    }
    static bool extra_get(const char *key, char *val, int n)
    {
        Settings *s = S();
        if (!s || !key || !val || n <= 0) {
            return false;
        }
        return s->extra_get(key, val, n);
    }
    static bool extra_set(const char *key, const char *val)
    {
        Settings *s = S();
        return s && key && val && s->extra_set(key, val);
    }
    static int cfg_value(const char *key, int def)
    {
        Settings *s = S();
        int ix = s && key ? s->find(key) : -1;
        return ix >= 0 && !s->item(ix).kind ? s->value(ix) : def;
    }
    static const char *cfg_text(const char *key)
    {
        Settings *s = S();
        int ix = s && key ? s->find(key) : -1;
        const char *t = ix >= 0 ? s->text(ix) : "";
        while (*t == ' ') {
            t++;
        }
        return t;
    }

    // ---- ctx (svc_ctx.h)
    static uint32_t state()
    {
        CoreCtx *c = C();
        if (!c) {
            return 0;
        }
        return (c->started ? CTX_STARTED : 0u) | (c->paused ? CTX_PAUSED : 0u) | (c->frozen ? CTX_FROZEN : 0u) | (c->menu_open ? CTX_MENU_OPEN : 0u) |
               (c->media_restoring ? CTX_MEDIA_RESTORING : 0u);
    }
    static void set_paused(bool on)
    {
        CoreCtx *c = C();
        if (c) {
            c->paused = on;
        }
    }
    static bool notice(const char *text, bool only_if_free)
    {
        CoreCtx *c = C();
        if (!c || !text || (only_if_free && c->notice_pending)) {
            return false;
        }
        snprintf(c->notice, sizeof(c->notice), "%s", text);
        c->notice_pending = true;
        return true;
    }
    static const char *media_dir()
    {
        CoreCtx *c = C();
        return c ? c->media_dir : "";
    }
    static bool target_path(const char *name, const char *ext, char *out, int n)
    {
        CoreCtx *c = C();
        return c && name && out && n > 0 && ctx_target_path(c, name, ext ? ext : "", out, n);
    }
    static bool core_folder(const char *sub, char *out, int n)
    {
        CoreCtx *c = C();
        if (!out || n <= 0) {
            return false;
        }
        out[0] = 0;
        if (!c || !c->cor_path[0]) {
            return false;
        }
        return sub && sub[0] ? corefolder_sub(c->cor_path, sub, out, n) : corefolder_dir(c->cor_path, out, n);
    }
    static bool job_step() { CoreCtx *c = C(); return c && c->job_step; }
    static bool job_cancelled()
    {
        CoreCtx *c = C();
        bool was = c && c->job_cancel;
        if (was) {
            c->job_cancel = false;
        }
        return was;
    }
    static void job_progress(int pct, const char *path)
    {
        CoreCtx *c = C();
        if (!c) {
            return;
        }
        c->job_pct = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
        if (path) {
            snprintf(c->job_path, sizeof(c->job_path), "%s", path);
        }
    }
    // the name of a slot without its colon, compared without regard to case
    static bool same_slot(const char *have, const char *want)
    {
        size_t hl = strlen(have), wl = strlen(want);
        hl -= hl && have[hl - 1] == ':';
        wl -= wl && want[wl - 1] == ':';
        return hl == wl && !strncasecmp(have, want, hl);
    }
    // marks the slot of the core's [mount] list (MountSlots::names); the base stores its spec on its next pass (mounts_pass, mediamounts.cc)
    static void mount_notify(const char *slot)
    {
        CoreCtx *c = C();
        const MountSlots *ms = c && c->desc && slot ? c->desc->mount_slots : 0;
        for (int i = 0; ms && i < ms->n && i < 32; i++) {
            if (same_slot(ms->names[i], slot)) {
                c->mounts_dirty[i] = 1;
                return;
            }
        }
    }
    static bool media_source(const char *slot, char *buf, int len)
    {
        CoreCtx *c = C();
        if (!buf || len <= 0) {
            return false;
        }
        buf[0] = 0;
        if (!c || !c->desc || !slot || !slot[0]) {
            return false;
        }
        const MountSlots *ms = c->desc->mount_slots;
        for (int i = 0; ms && i < ms->n; i++) {
            if (same_slot(ms->names[i], slot)) {
                // the engine's slot_spec is Task: lock (core_hooks.h) and answers in one static buffer: read it under the core lock, and copy before it is let go
                CoreLock *lk = c->lock && !c->lock->mine() ? c->lock : 0;
                if (lk) {
                    lk->lock();
                }
                const char *spec = ms->spec(c, i);
                bool have = spec && spec[0];
                if (have) {
                    snprintf(buf, len, "%s", spec);
                }
                if (lk) {
                    lk->unlock();
                }
                return have;
            }
        }
        // a window drive or a sector unit, which the base mounts itself (the engines of winslot.h and sectors.h): the base's own lookup (mediamounts.cc, set_media_source)
        if (L()->media_src_) {
            int r = L()->media_src_(c, slot, buf, len);
            if (r >= 0) {
                return r > 0;
            }
        }
        // a media slot of the descriptor: what its state says, when that is a spec (the medium's path, a word in front allowed; a path starts with '/')
        for (int i = 0; i < c->desc->nmedia; i++) {
            const MediaSlot &m = c->desc->media[i];
            if (m.name && m.state && same_slot(m.name, slot)) {
                const char *t = c->ops ? c->ops->state(c, m.state, 0) : 0;
                if (!t || !(t[0] == '/' || strstr(t, " /"))) {
                    return false;
                }
                snprintf(buf, len, "%s", t);
                return true;
            }
        }
        return false;
    }

    // ---- io (svc_io.h)
    static uint8_t rd(uint32_t reg) { CoreIo *io = IO(); return io ? io->rd(reg) : 0; }
    static void wr(uint32_t reg, uint8_t v) { CoreIo *io = IO(); if (io) io->wr(reg, v); }
    static void ddr_read(uint32_t off, void *p, uint32_t n) { CoreIo *io = IO(); if (io) io->ddr_read(off, p, n); else memset(p, 0, n); }
    static void ddr_write(uint32_t off, const void *p, uint32_t n) { CoreIo *io = IO(); if (io) io->ddr_write(off, p, n); }
    static void rom_read(uint32_t off, void *p, uint32_t n) { CoreIo *io = IO(); if (io) io->rom_read(off, p, n); else memset(p, 0, n); }
    static void rom_write(uint32_t off, const void *p, uint32_t n) { CoreIo *io = IO(); if (io) io->rom_write(off, p, n); }
    static void ram_read(uint32_t a, void *p, uint32_t n) { CoreIo *io = IO(); if (io) io->ram_read(a, p, n); else memset(p, 0, n); }
    static void ram_write(uint32_t a, const void *p, uint32_t n) { CoreIo *io = IO(); if (io) io->ram_write(a, p, n); }
    static void ram_memonly(bool on) { CoreIo *io = IO(); if (io) io->ram_memonly(on); }
    static uint8_t drv_rd(uint32_t off) { CoreIo *io = IO(); return io ? io->drv_rd(off) : 0; }
    static void drv_wr(uint32_t off, uint8_t v) { CoreIo *io = IO(); if (io) io->drv_wr(off, v); }
    static void ddr_abs_read(uint32_t a, void *p, uint32_t n) { CoreIo *io = IO(); if (io) io->ddr_abs_read(a, p, n); else memset(p, 0, n); }
    static void ddr_abs_write(uint32_t a, const void *p, uint32_t n) { CoreIo *io = IO(); if (io) io->ddr_abs_write(a, p, n); }
    static uint8_t cart_rd(uint32_t reg) { CoreIo *io = IO(); return io ? io->cart_rd(reg) : 0; }
    static void cart_wr(uint32_t reg, uint8_t v) { CoreIo *io = IO(); if (io) io->cart_wr(reg, v); }
    static void bus_wr(uint32_t off, uint8_t v) { CoreIo *io = IO(); if (io) io->bus_wr(off, v); }
    static uint8_t bus_rd(uint32_t off) { CoreIo *io = IO(); return io ? io->bus_rd(off) : 0; }
    static void eeprom_read(uint32_t off, void *p, uint32_t n) { CoreIo *io = IO(); if (io) io->eeprom_read(off, p, n); else memset(p, 0, n); }
    static void eeprom_write(uint32_t off, const void *p, uint32_t n) { CoreIo *io = IO(); if (io) io->eeprom_write(off, p, n); }
    static bool keys_read(uint8_t m[8], bool *restore) { CoreIo *io = IO(); return io && io->keys_read(m, restore); }
    static void stop(bool on) { CoreIo *io = IO(); if (io) io->stop(on); }
    static void mode_reset(bool on) { CoreIo *io = IO(); if (io) io->mode_reset(on); }
    static void video_format(uint8_t v) { CoreIo *io = IO(); if (io) io->video_format(v); }
    static volatile uint8_t *window(const char *name, uint32_t *size)
    {
        // the windows of the running core's description record (section 19); a block of a version 7 record was refused at the read when it overlapped a block of the base
        const DescRecord *rec = L()->windows_rec_;
        const DescWindow *w = rec && name ? descrec_window(*rec, name) : 0;
        if (size) {
            *size = w ? w->size : 0;
        }
        return w ? (volatile uint8_t *)(uintptr_t)w->address : 0;
    }

    // ---- overlay (svc_overlay.h): the calls are validated by the service at once; their effect is the UI task's next frame. The caller is the image whose code makes the call (owner_at),
    // so the elements of a library are its own and go with it.
    static int ovl_show(const char *name, const SvcSurface *sf)
    {
        LibLoader *l = L();
        return l->overlay_svc_ ? l->overlay_svc_->show(l->owner_at(__builtin_return_address(0)), name, sf) : OVL_E_NOSERVICE;
    }
    static int ovl_update(int el)
    {
        LibLoader *l = L();
        return l->overlay_svc_ ? l->overlay_svc_->update(l->owner_at(__builtin_return_address(0)), el) : OVL_E_NOSERVICE;
    }
    static int ovl_remove(int el)
    {
        LibLoader *l = L();
        return l->overlay_svc_ ? l->overlay_svc_->remove(l->owner_at(__builtin_return_address(0)), el) : OVL_E_NOSERVICE;
    }
    static void ovl_text(const SvcSurface *sf, int x, int y, const char *s, uint16_t colour)
    {
        LibLoader *l = L();
        if (l->overlay_svc_) {
            l->overlay_svc_->text(l->owner_at(__builtin_return_address(0)), sf, x, y, s, colour);
        }
    }
    static void ovl_menu_hide()
    {
        if (L()->overlay_svc_) {
            L()->overlay_svc_->menu_hide();
        }
    }
    static void ovl_menu_show()
    {
        if (L()->overlay_svc_) {
            L()->overlay_svc_->menu_show();
        }
    }
    static const char *ovl_error(int code) { return OverlaySvc::error(code); }

    // ---- board (svc_board.h)
    static uint32_t fpga_caps() { return L()->host_->fpga_caps(); }
    static bool is_commodore() { return L()->host_->is_commodore(); }
    static int esp32_rpc(uint8_t cmd, const uint8_t *args, int nargs, uint8_t *reply, int maxreply, int timeout_ms)
    {
        return L()->host_->esp32_rpc(cmd, args, nargs, reply, maxreply, timeout_ms);
    }
    static void video_set_system(int sys) { L()->host_->video_set_system(C(), sys); }
    static void video_set_pll(int sys, int ppm) { L()->host_->video_set_pll(sys, ppm); }
    // The control byte of the expander: bits 3:0 socket 0, 7:4 socket 1. A nibble: bits 1:0 the supply (00 off, 10 on, 11 on for a 6581), bit 2 the 1 kOhm shunt, bit 3 the
    // filter capacitor option, inverted (the setting says 1 for "capacitors on", the line says 0).
    static int sid_socket(int socket, int state, bool shunt, bool filter_caps)
    {
        LibLoader *l = L();
        if (socket < 0 || socket > 1 || state < SID_SOCKET_OFF || state > SID_SOCKET_ON_TYPE1) {
            return -1;
        }
        uint8_t supply = state == SID_SOCKET_OFF ? 0 : (state == SID_SOCKET_ON ? 2 : 3);
        uint8_t nib = (uint8_t)(supply | (shunt ? 4 : 0) | (filter_caps ? 0 : 8));
        int open = l->host_->sid_i2c_open();
        if (open < 0) {
            return -2;                                  // the bus lock was not taken: nothing written, the control byte stays
        }
        if (open > 0) {
            l->sid_ctrl_ = socket ? (uint8_t)((l->sid_ctrl_ & 0x0F) | (nib << 4)) : (uint8_t)((l->sid_ctrl_ & 0xF0) | nib);     // under the lock
            l->host_->sid_i2c_write(0x01, l->sid_ctrl_);
            l->host_->sid_i2c_write(0x03, 0x00);
            l->host_->sid_i2c_close();
        }
        return 0;
    }
    static void case_led_select(uint8_t v) { CoreIo *io = IO(); if (io) io->case_led_select(v); }

    // "set.<Key>" (a space in the key is written '_', the case is folded): the value in force of the setting <Key> as text, null when the core has no such setting;
    // "net.gated": "yes" while the REST token gate is on.
    static const char *extra_state(CoreCtx *c, const char *name)
    {
        if (!strcmp(name, "net.gated")) {
            LibLoader *l = L();
            return l && l->gated_ && l->gated_() ? "yes" : 0;
        }
        if (!c || !c->settings) {
            return 0;
        }
        const Settings *s = c->settings;
        for (int i = 0; i < s->count(); i++) {
            const char *a = s->item(i).key, *b = name + 4;
            while (*a && *b && (*a == ' ' ? '_' : tolower((unsigned char)*a)) == (*b == ' ' ? '_' : tolower((unsigned char)*b))) {
                a++;
                b++;
            }
            if (!*a && !*b) {
                const Setting &it = s->item(i);
                return it.kind ? s->text(i) : it.values[s->value(i)];
            }
        }
        return 0;
    }

    static bool has_window(const CoreCtx *c) { return c && c->io && !(c->desc && c->desc->menu_first); }

    // ---- net and job (svctab.h, S14): the loader's own tables over the ones the base gives (set_net, set_job). Every socket and every job is on the account of the image whose code asked
    // (owner_at), so that freeing the image closes its sockets and stops its jobs, and the end of one application touches nothing of another image.
    static int net_new_rec(LibLoader *l, int owner)
    {
        int k = -1;
        l->host_->enter();
        for (int i = 0; i < LIB_NET_MAX && k < 0; i++) {
            if (!l->nets_[i].used) {
                k = i;
                l->nets_[i].used = true;
                l->nets_[i].h = -1;
                l->nets_[i].owner = owner;
            }
        }
        l->host_->leave();
        return k;
    }
    static int net_adopt(LibLoader *l, int k, int h)
    {
        l->host_->enter();
        if (h < 0) {
            l->nets_[k].used = false;
        } else {
            for (int i = 0; i < LIB_NET_MAX; i++) {
                if (i != k && l->nets_[i].used && l->nets_[i].h == h) {
                    l->nets_[i].used = false;           // a handle the base hands out again was closed behind the loader's back: the old record is dead
                }
            }
            l->nets_[k].h = h;
        }
        l->host_->leave();
        return h;
    }
    static int net_listen(uint16_t port, int backlog)
    {
        LibLoader *l = L();
        if (!l->base_net_ || !l->base_net_->listen) {
            return -7;
        }
        int k = net_new_rec(l, l->owner_at(__builtin_return_address(0)));
        if (k < 0) {
            return -5;
        }
        return net_adopt(l, k, l->base_net_->listen(port, backlog));
    }
    static int net_accept(int listener)
    {
        LibLoader *l = L();
        if (!l->base_net_ || !l->base_net_->accept) {
            return -7;
        }
        int k = net_new_rec(l, l->owner_at(__builtin_return_address(0)));
        if (k < 0) {
            return -5;
        }
        return net_adopt(l, k, l->base_net_->accept(listener));
    }
    static int net_connect(const char *host, uint16_t port, uint32_t timeout_ms)
    {
        LibLoader *l = L();
        if (!l->base_net_ || !l->base_net_->connect || l->base_net_->size < offsetof(SvcNet, resolve)) {
            return -7;
        }
        int k = net_new_rec(l, l->owner_at(__builtin_return_address(0)));
        if (k < 0) {
            return -5;
        }
        return net_adopt(l, k, l->base_net_->connect(host, port, timeout_ms));
    }
    static int net_resolve(const char *name, char *buf, int len) { const SvcNet *n = L()->base_net_; return n && n->size >= sizeof(SvcNet) && n->resolve ? n->resolve(name, buf, len) : -1; }
    static int net_close(int h)
    {
        LibLoader *l = L();
        if (!l->base_net_ || !l->base_net_->close) {
            return -1;
        }
        l->host_->enter();
        for (int i = 0; i < LIB_NET_MAX; i++) {                 // the record goes first: once the base has closed the handle, another task may be given the same number and takes a record of its own
            if (l->nets_[i].used && l->nets_[i].h == h) {
                l->nets_[i].used = false;
            }
        }
        l->host_->leave();
        return l->base_net_->close(h);
    }
    static int net_recv(int h, void *buf, int len) { const SvcNet *n = L()->base_net_; return n && n->recv ? n->recv(h, buf, len) : -1; }
    static int net_send(int h, const void *buf, int len) { const SvcNet *n = L()->base_net_; return n && n->send ? n->send(h, buf, len) : -1; }
    static int net_wait(const int *handles, int n, uint32_t timeout_ms) { const SvcNet *t = L()->base_net_; return t && t->wait ? t->wait(handles, n, timeout_ms) : -1; }
    static int net_peer(int h, char *buf, int len) { const SvcNet *n = L()->base_net_; return n && n->peer ? n->peer(h, buf, len) : -1; }

    // the job function runs on a task of its own, with the image that started it as the owner of what it takes
    static int job_tramp(SvcJobH *j, void *p)
    {
        LibLoader::JobRec *r = (LibLoader::JobRec *)p;
        LibLoader::Scope scope(L(), r->owner);
        return r->fn(j, r->user);
    }
    static SvcJobH *job_start(int (*fn)(SvcJobH *, void *), void *user, uint32_t stack_bytes)
    {
        LibLoader *l = L();
        if (!fn || !l->base_job_ || !l->base_job_->start) {
            return 0;
        }
        int owner = l->owner_at(__builtin_return_address(0));
        LibLoader::JobRec *r = 0;
        l->host_->enter();
        for (int i = 0; i < LIB_JOB_MAX && !r; i++) {           // a free record, or one whose job has ended and was not joined (the base reuses its slot as well)
            LibLoader::JobRec &c = l->jobs_[i];
            if (!c.used || (c.h && l->base_job_->running && !l->base_job_->running(c.h))) {
                r = &c;
            }
        }
        if (r) {
            r->used = true;
            r->h = 0;
            r->owner = owner;
            r->fn = fn;
            r->user = user;
        }
        l->host_->leave();
        if (!r) {
            return 0;
        }
        SvcJobH *j = l->base_job_->start(job_tramp, r, stack_bytes);
        l->host_->enter();
        if (!j) {
            r->used = false;
        } else {
            for (int i = 0; i < LIB_JOB_MAX; i++) {
                if (&l->jobs_[i] != r && l->jobs_[i].used && l->jobs_[i].h == j) {
                    l->jobs_[i].used = false;                   // the slot of a job that had ended is the base's again
                }
            }
            r->h = j;
        }
        l->host_->leave();
        return j;
    }
    static bool job_cancelled(SvcJobH *j) { const SvcJob *t = L()->base_job_; return t && t->cancelled && t->cancelled(j); }
    static bool job_running(SvcJobH *j) { const SvcJob *t = L()->base_job_; return t && t->running && t->running(j); }
    // "The application is asked to end": it is no question for a library, whose jobs end by their own cancel flag or when the library goes (S14)
    static bool job_stop_requested(void)
    {
        LibLoader *l = L();
        int o = l->owner_at(__builtin_return_address(0));
        if (o >= 0 && o < LIB_MAX && l->slot_[o].used && l->slot_[o].info.kind == U2A3_KIND_LIB) {
            return false;
        }
        const SvcJob *t = l->base_job_;
        return t && t->stop_requested && t->stop_requested();
    }
    static int job_join(SvcJobH *j)
    {
        LibLoader *l = L();
        if (!l->base_job_ || !l->base_job_->join) {
            return -1;
        }
        int rc = l->base_job_->join(j);
        l->host_->enter();
        for (int i = 0; i < LIB_JOB_MAX; i++) {
            if (l->jobs_[i].used && l->jobs_[i].h == j) {
                l->jobs_[i].used = false;
            }
        }
        l->host_->leave();
        return rc;
    }
};

void LibLoader::add_services()
{
    services_ = true;
    extra_state_ = LibSvc::extra_state;
    net_.size = sizeof(net_);                          // api->net and api->job: the loader's own tables (account per image, S14)
    net_.listen = LibSvc::net_listen;
    net_.accept = LibSvc::net_accept;
    net_.recv = LibSvc::net_recv;
    net_.send = LibSvc::net_send;
    net_.close = LibSvc::net_close;
    net_.wait = LibSvc::net_wait;
    net_.peer = LibSvc::net_peer;
    net_.connect = LibSvc::net_connect;
    net_.resolve = LibSvc::net_resolve;
    job_.size = sizeof(job_);
    job_.start = LibSvc::job_start;
    job_.cancelled = LibSvc::job_cancelled;
    job_.join = LibSvc::job_join;
    job_.running = LibSvc::job_running;
    job_.stop_requested = LibSvc::job_stop_requested;
    api_.net = &net_;
    api_.job = &job_;
    api_.sync = &sync_;
    api_.settings = &settings_;
    api_.ctx = &ctxtab_;
    api_.board = &board_;
    api_.overlay = &overlay_;
    api_.io = LibSvc::has_window(ctx_) ? &io_ : 0;
    memset(&sync_, 0, sizeof(sync_));
    sync_.size = sizeof(sync_);
    sync_.mutex_new = LibSvc::mutex_new;
    sync_.mutex_lock = LibSvc::mutex_lock;
    sync_.mutex_unlock = LibSvc::mutex_unlock;
    sync_.mutex_free = LibSvc::mutex_free;
    sync_.enter = LibSvc::enter;
    sync_.leave = LibSvc::leave;

    memset(&settings_, 0, sizeof(settings_));
    settings_.size = sizeof(settings_);
    settings_.generation = LibSvc::generation;
    settings_.find = LibSvc::find;
    settings_.value = LibSvc::value;
    settings_.text = LibSvc::text;
    settings_.layer = LibSvc::layer;
    settings_.set = LibSvc::set;
    settings_.override_value = LibSvc::override_value;
    settings_.apply = LibSvc::apply;
    settings_.info = LibSvc::info;
    settings_.extra_get = LibSvc::extra_get;
    settings_.extra_set = LibSvc::extra_set;
    settings_.cfg_value = LibSvc::cfg_value;
    settings_.cfg_text = LibSvc::cfg_text;

    memset(&ctxtab_, 0, sizeof(ctxtab_));
    ctxtab_.size = sizeof(ctxtab_);
    ctxtab_.state = LibSvc::state;
    ctxtab_.set_paused = LibSvc::set_paused;
    ctxtab_.notice = LibSvc::notice;
    ctxtab_.media_dir = LibSvc::media_dir;
    ctxtab_.target_path = LibSvc::target_path;
    ctxtab_.core_folder = LibSvc::core_folder;
    ctxtab_.job_step = LibSvc::job_step;
    ctxtab_.job_cancelled = LibSvc::job_cancelled;
    ctxtab_.job_progress = LibSvc::job_progress;
    ctxtab_.media_source = LibSvc::media_source;
    ctxtab_.mount_notify = LibSvc::mount_notify;

    memset(&io_, 0, sizeof(io_));
    io_.size = sizeof(io_);
    io_.rd = LibSvc::rd;
    io_.wr = LibSvc::wr;
    io_.ddr_read = LibSvc::ddr_read;
    io_.ddr_write = LibSvc::ddr_write;
    io_.rom_read = LibSvc::rom_read;
    io_.rom_write = LibSvc::rom_write;
    io_.ram_read = LibSvc::ram_read;
    io_.ram_write = LibSvc::ram_write;
    io_.ram_memonly = LibSvc::ram_memonly;
    io_.drv_rd = LibSvc::drv_rd;
    io_.drv_wr = LibSvc::drv_wr;
    io_.ddr_abs_read = LibSvc::ddr_abs_read;
    io_.ddr_abs_write = LibSvc::ddr_abs_write;
    io_.cart_rd = LibSvc::cart_rd;
    io_.cart_wr = LibSvc::cart_wr;
    io_.bus_wr = LibSvc::bus_wr;
    io_.bus_rd = LibSvc::bus_rd;
    io_.eeprom_read = LibSvc::eeprom_read;
    io_.eeprom_write = LibSvc::eeprom_write;
    io_.keys_read = LibSvc::keys_read;
    io_.stop = LibSvc::stop;
    io_.mode_reset = LibSvc::mode_reset;
    io_.video_format = LibSvc::video_format;
    io_.delay_us = LibLoader::svc_delay_us;           // the same call as sys.delay_us
    io_.window = LibSvc::window;

    memset(&board_, 0, sizeof(board_));
    board_.size = sizeof(board_);
    board_.fpga_caps = LibSvc::fpga_caps;
    board_.is_commodore = LibSvc::is_commodore;
    board_.esp32_rpc = LibSvc::esp32_rpc;
    board_.video_set_system = LibSvc::video_set_system;
    board_.video_set_pll = LibSvc::video_set_pll;
    board_.sid_socket = LibSvc::sid_socket;
    board_.case_led_select = LibSvc::case_led_select;

    memset(&overlay_, 0, sizeof(overlay_));
    overlay_.size = sizeof(overlay_);
    overlay_.show = LibSvc::ovl_show;
    overlay_.update = LibSvc::ovl_update;
    overlay_.remove = LibSvc::ovl_remove;
    overlay_.text = LibSvc::ovl_text;
    overlay_.menu_hide = LibSvc::ovl_menu_hide;
    overlay_.menu_show = LibSvc::ovl_menu_show;
    overlay_.error = LibSvc::ovl_error;
}

// where a file of the menu goes: the folder the menu was on (MenuCtl::note_folder), else the first medium; the extension is added when the name has none
bool ctx_target_path(CoreCtx *c, const char *name, const char *ext, char *out, int n)
{
    char dir[200];
    snprintf(dir, sizeof(dir), "%s", c->media_dir);
    if (!dir[0] && c->fs) {
        FsEntry r[4];
        int k = c->fs->list("/", r, 4);
        for (int i = 0; i < k; i++) {
            if (strcasecmp(r[i].name, "flash")) {
                snprintf(dir, sizeof(dir), "/%s", r[i].name);
                break;
            }
        }
    }
    if (!dir[0]) {
        return false;
    }
    const char *dot = strrchr(name, '.');
    int dl = (int)strlen(dir);
    snprintf(out, n, "%s%s%s%s", dir, dl && dir[dl - 1] == '/' ? "" : "/", name, dot ? "" : ext);
    return true;
}
