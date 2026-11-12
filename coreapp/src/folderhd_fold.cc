// The fold-back of the folder disk (T128 D5, Q1, Q9, C11): the Amiga's view of the disk (overlay over synthesis) is walked from the root exactly as the
// Amiga would walk it, compared with the index of the host tree, and the host tree is brought to it - in steps, so that the sector service is never held
// longer than FH_SLICE_MS. Phases:
//   WALK      one object per step; finds what is new, renamed, changed in content or properties, and (at the end) what is gone
//   MKDIR     new directories
//   MATERIAL  the data of every new or rewritten file is collected in a temp file below <root>/trx64
//   -- up to here a write of the Amiga drops the whole run (nothing on the host depends on it) --
//   REN1      every renamed or moved object goes to limbo (<root>/trx64), so that no name can clash and no directory still holds a child that left
//   DELETE    what the Amiga deleted (children before their directory)
//   REN2      the renamed objects to their final place
//   PLACE     temp file over the file (remove + rename: FatFs has no atomic replace)
//   PROPS     the .uaem files (protection, comment)
//   FINISH    the index takes over what the host now holds; data that is on the host is dropped from the scratch file
// An item that fails (medium full, gone, read only) keeps its data in the overlay and is tried again later (S7).
#include "folderhd_impl.h"
#include <strings.h>

using namespace fh;

#define RETRY_MS 5000
#define BM_WAIT_MS 10000

bool Impl::dirty() const
{
    if (!host_rw) {
        return false;
    }
    if (gen != folded_gen || phase != PH_IDLE) {
        return true;
    }
    for (int i = 0; i < FH_HANDLES; i++) {
        if (hd[i].f && hd[i].wrote) {
            return true;
        }
    }
    return false;
}

void Impl::tmp_path(uint32_t seq, char *out, int n)
{
    snprintf(out, (size_t)n, "%s/folderhd%d.fb%u", tmpdir, unit, (unsigned)seq);
}

// Where the data of a pending file is collected. Every use goes through here.
void Impl::pend_tmp(const Pend &p, char *out, int n)
{
    tmp_path(p.tmp_seq, out, n);
}

// Is the object not there, as against: the medium does not answer? (CoreFs has no error codes: a parent that can be listed means the medium is there.)
bool Impl::host_absent(const char *path)
{
    uint64_t sz;
    bool isd;
    if (fs->stat(path, &sz, &isd)) {
        return false;
    }
    char par[300];
    snprintf(par, sizeof(par), "%s", path);
    char *sl = strrchr(par, '/');
    if (!sl || sl == par) {
        return false;
    }
    *sl = 0;
    FsEntry e[1];
    return fs->list(par, e, 1) >= 0;
}

void Impl::bak_path(uint32_t seq, char *out, int n)
{
    snprintf(out, (size_t)n, "%s/folderhd%d.bk%u", tmpdir, unit, (unsigned)seq);
}

// A replace that was cut between its two renames leaves the file nowhere but in the temp and the backup file below <root>/trx64. Before anything
// else is done the next run puts one of them back (the new content, else the old).
bool Impl::run_repair()
{
    while (nrepair) {
        Repair &r = repair[nrepair - 1];
        char path[300], tp[300], bp[300];
        uint64_t sz;
        bool isd;
        tmp_path(r.seq, tp, sizeof(tp));
        bak_path(r.seq, bp, sizeof(bp));
        if (nodes[r.node].flags & F_DEAD) {
            nrepair--;
            continue;
        }
        if (!path_of(r.node, path, sizeof(path))) {
            nrepair--;
            continue;
        }
        if (fs->stat(path, &sz, &isd)) {
            nrepair--;                                                 // it is back
            continue;
        }
        bool ok;
        if (fs->stat(tp, &sz, &isd)) {
            ok = fs->rename(tp, path);
            if (ok) {
                nodes[r.node].hsize = r.size;                          // the file now holds the new content
            }
        } else if (fs->stat(bp, &sz, &isd)) {
            ok = fs->rename(bp, path);
        } else {
            nrepair--;
            continue;
        }
        if (!ok) {
            return false;
        }
        nrepair--;
        printf("[folderhd] unit %d: %s was put back after an interrupted replace\n", unit, path);
    }
    return true;
}

void Impl::fail(const char *what, uint32_t idx)
{
    st->fold_errors++;
    fold_failed = true;
    retry_ms = io->now_ms() + RETRY_MS;
    char nm[160] = "";
    if (idx) {
        host_name(idx, nm, sizeof(nm));
    }
    static uint32_t said;
    if (said++ < 40) {                                   // a medium that stays unwritable would repeat this every few seconds
        printf("[folderhd] unit %d: %s %s (the change stays in the overlay)\n", unit, what, nm);
    }
}

// ---- the iterator over the data blocks of a file in the Amiga's view ----
bool Impl::it_start(uint32_t hdr_block)
{
    it_hops = 0;
    if (!view_read(hdr_block, it_blk) || !ffs_sum_ok(it_blk, FFS_LONGS)) {
        return false;
    }
    it_j = 0;
    it_n = ffs_rd(it_blk, FL_HIGHSEQ);
    if (it_n > FFS_HT) it_n = FFS_HT;
    it_ext = ffs_rd(it_blk, FL_EXTENSION);
    return true;
}

bool Impl::it_next(uint32_t *blk)
{
    while (it_j >= it_n) {
        if (!it_ext || ++it_hops > 1000000 || it_ext >= nblk || !view_read(it_ext, it_blk) || !ffs_sum_ok(it_blk, FFS_LONGS) ||
            ffs_rd(it_blk, FL_TYPE) != FFS_T_LIST) {
            return false;
        }
        it_j = 0;
        it_n = ffs_rd(it_blk, FL_HIGHSEQ);
        if (it_n > FFS_HT) it_n = FFS_HT;
        it_ext = ffs_rd(it_blk, FL_EXTENSION);
    }
    *blk = ffs_data_ptr(it_blk, (int)it_j++);
    return true;
}

static bool ptr_matches(Impl *m, uint32_t idx, uint32_t j, uint32_t b)
{
    const Node &nd = m->nodes[idx];
    if (idx < m->nbase && b == nd.hdr + 1 + j && j < m->file_blocks(nd)) {
        return true;
    }
    int x = m->xr_find(b);
    return x >= 0 && m->xr[x].node == idx && m->xr[x].fblk + (b - m->xr[x].start) == j;
}

// Has the file another content than the host file has? (Size, a data block in the overlay, another list of blocks.)
int Impl::content_changed(uint32_t idx, const FfsHdr &h)
{
    Node &nd = nodes[idx];
    if (h.size != nd.hsize) {
        return 2;
    }
    if (delta_has(nd.hdr)) {                                // the header is not as it was synthesised: is it the same list of blocks?
        if (!it_start(nd.hdr)) {
            return 2;
        }
        uint32_t n = (h.size + FFS_BLOCK - 1) / FFS_BLOCK;
        for (uint32_t j = 0; j < n; j++) {
            uint32_t b;
            if (!it_next(&b) || !ptr_matches(this, idx, j, b)) {
                return 2;
            }
        }
    }
    if (nd.flags & F_DELTA) {
        return idx < nbase ? 1 : 2;                         // data blocks written in place: only those are put into the host file
    }
    return 0;
}

bool Impl::pend_add(const Pend &p)
{
    if (npend >= pend_cap) {
        if (pend_cap >= 4096) {
            walk_full = true;
            return false;
        }
        uint32_t nc = pend_cap ? pend_cap * 2 : 256;
        Pend *q = (Pend *)mem_realloc(pend, pend_cap * sizeof(Pend), nc * sizeof(Pend));
        if (!q) {
            walk_full = true;
            return false;
        }
        pend = q;
        pend_cap = nc;
    }
    pend[npend++] = p;
    return true;
}

// ---- begin, abort ----
void Impl::fold_begin()
{
    job_gen = gen;
    late = false;
    fold_failed = false;
    walk_full = false;
    npend = 0;
    nn_start = nn;
    plen_start = plen;
    cursor = 0;
    sp = 0;
    visited = 0;
    bm_waiting = false;
    if (!seen) {
        seen = (uint8_t *)mem_alloc(FH_SEEN_BYTES);
    }
    if (!buf) {
        buf = (uint8_t *)mem_alloc(FH_CHUNK);
    }
    if (!seen || !buf) {
        phase = PH_IDLE;
        retry_ms = io->now_ms() + RETRY_MS;
        printf("[folderhd] unit %d: out of memory for the fold-back\n", unit);
        return;
    }
    memset(seen, 0, FH_SEEN_BYTES);
    // which files have data blocks in the overlay or the scratch file: those are rewritten whatever their header says
    for (uint32_t i = 1; i < nn; i++) {
        nodes[i].flags &= (uint8_t)~F_DELTA;
    }
    for (uint32_t r = 0; r < nsr; r++) {
        uint32_t b = sr[r].start, e = sr[r].start + sr[r].count;
        while (b < e) {
            int x = xr_find(b);
            if (x >= 0) {
                nodes[xr[x].node].flags |= F_DELTA;
                b = xr[x].start + xr[x].count;
                continue;
            }
            int i = span_find(b);
            if (i >= 0) {
                if (!(nodes[i].flags & F_DIR) && b > nodes[i].hdr && b <= nodes[i].hdr + file_blocks(nodes[i])) {
                    nodes[i].flags |= F_DELTA;
                }
                b = nodes[i].hdr + span_len(nodes[i]);
                continue;
            }
            b++;
        }
    }
    {
        uint8_t rb[FFS_BLOCK];
        view_read(R, rb);
        consistent = ffs_rd(rb, FL_BM_FLAG) == 0xFFFFFFFFu;
    }
    stack[0].block = R;
    stack[0].node = 0;
    stack[0].slot = 0;
    stack[0].chain = 0;
    sp = 1;
    phase = PH_WALK;
}

void Impl::tmp_drop_all()
{
    if (tmp) {
        fs->close(tmp);
        tmp = 0;
    }
    for (uint32_t i = 0; i < npend; i++) {
        if (pend[i].tmp_seq) {
            char tp[300];
            pend_tmp(pend[i], tp, sizeof(tp));
            fs->remove(tp);
            pend[i].tmp_seq = 0;
        }
    }
}

void Impl::fold_abort(const char *why)
{
    tmp_drop_all();
    nn = nn_start;                                    // the nodes the walk made, and their names
    plen = plen_start;
    npend = 0;
    phase = PH_IDLE;
    st->aborted++;
    printf("[folderhd] unit %d: fold-back dropped: %s\n", unit, why);
}

void Impl::node_dead(uint32_t idx)
{
    nodes[idx].flags |= F_DEAD;
    for (uint32_t i = 0; i < ncre; i++) {
        if (cre[i] == idx) {
            memmove(&cre[i], &cre[i + 1], (ncre - i - 1) * sizeof(uint16_t));
            ncre--;
            break;
        }
    }
}

// ---- the walk ----
static bool bytes_eq(const uint8_t *pstr_, const uint8_t *b, int n)
{
    return pstr_[0] == n && !memcmp(pstr_ + 1, b, (size_t)n);
}

bool Impl::walk_step()
{
    Frame &f = stack[sp - 1];
    if (f.chain == 0) {
        if (f.slot >= FFS_HT) {
            sp--;
            if (sp == 0) {
                walk_end();
            }
            return true;
        }
        uint8_t db[FFS_BLOCK];
        view_read(f.block, db);
        f.chain = ffs_hash_slot(db, f.slot);
        f.slot++;
        return true;
    }
    uint32_t blk = f.chain;
    f.chain = 0;
    if (++visited > nn_start + FH_CREATED_MAX + 4096 || blk < 2 || blk >= nblk) {
        printf("[folderhd] unit %d: the directory structure is damaged (block %u); the rest of the directory is not folded back\n", unit, (unsigned)blk);
        return true;
    }
    uint8_t b[FFS_BLOCK];
    view_read(blk, b);
    FfsHdr h;
    if (!ffs_parse_hdr(b, &h) || h.key != blk) {
        printf("[folderhd] unit %d: block %u is no header; its chain is cut\n", unit, (unsigned)blk);
        return true;
    }
    f.chain = h.chain;
    bool isdir = h.sectype == FFS_ST_DIR;
    if (!isdir && h.sectype != FFS_ST_FILE) {
        char nm[FFS_NAME_MAX + 1];
        folderhd_cpy(nm, sizeof(nm), h.name, h.nlen);
        printf("[folderhd] unit %d: a link (\"%s\") is not saved in the folder\n", unit, nm);
        return true;
    }
    uint32_t parent = f.node;
    int idx = node_by_hdr(blk);
    bool same = false;
    if (idx > 0 && !seen_get((uint32_t)idx) && (((nodes[idx].flags & F_DIR) != 0) == isdir)) {
        bool name_eq = bytes_eq(pstr(nodes[idx].name), h.name, h.nlen);
        if (name_eq) {
            same = true;
        } else if (!isdir) {
            same = h.size == nodes[idx].hsize && content_changed((uint32_t)idx, h) != 2;               // a rename: the very same blocks
        } else if ((uint32_t)idx < nbase) {
            uint32_t heads[FFS_HT];
            dir_heads((uint32_t)idx, heads);
            same = true;
            for (int s = 0; s < FFS_HT; s++) {
                if (ffs_hash_slot(b, s) != heads[s]) same = false;
            }
        }
    }
    uint32_t me;
    Pend p;
    memset(&p, 0, sizeof(p));
    if (same) {
        me = (uint32_t)idx;
        Node &nd = nodes[me];
        seen_set(me);
        p.node = me;
        p.aparent = (uint16_t)parent;
        p.aname = nd.name;
        p.acomment = nd.comment;
        p.size = h.size;
        p.protect = (uint8_t)h.protect;
        p.days = (uint16_t)h.date.days;
        p.mins = (uint16_t)h.date.mins;
        p.ticks = (uint16_t)h.date.ticks;
        if (nd.parent != parent || !bytes_eq(pstr(nd.name), h.name, h.nlen)) {
            p.ops |= OP_REN;
            p.aname = pool_add(h.name, h.nlen);
        }
        bool com_eq = nd.comment ? bytes_eq(pstr(nd.comment), h.comment, h.clen) : h.clen == 0;
        if ((uint8_t)h.protect != nd.protect || !com_eq || p.days != nd.days || p.mins != nd.mins || p.ticks != nd.ticks) {
            p.ops |= OP_PROPS;
            if (!com_eq) p.acomment = pool_add(h.comment, h.clen);
        }
        if (!isdir) {
            int cc = content_changed(me, h);
            if (cc == 2) {
                p.ops |= OP_CONTENT;
            } else if (cc == 1 && consistent) {
                p.ops |= OP_PATCH;          // with the Amiga's bitmap flag invalid the disk is in the middle of a change: who owns the block is not known, the data stays in the overlay
            }
        }
        if (p.ops) {
            pend_add(p);
        }
    } else {
        char enc[160];
        if (nn - nbase >= FH_CREATED_MAX) {
            char nm[FFS_NAME_MAX + 1];
            folderhd_cpy(nm, sizeof(nm), h.name, h.nlen);
            printf("[folderhd] unit %d: more than %u objects created in one mount; \"%s\" is not saved\n", unit, (unsigned)FH_CREATED_MAX, nm);
            return true;
        }
        if (h.nlen == 0 || folderhd_aname_to_host(h.name, h.nlen, enc, sizeof(enc)) < 0 || strlen(enc) >= 54) {
            char nm[FFS_NAME_MAX + 1];
            folderhd_cpy(nm, sizeof(nm), h.name, h.nlen);
            printf("[folderhd] unit %d: \"%s\" has no host name that the listing could read back; not saved\n", unit, nm);
            return true;
        }
        Node *nd;
        if (!node_add(&nd, &me)) {
            walk_full = true;
            return true;
        }
        nd->flags = (uint8_t)(F_NEW | F_CREATED | (isdir ? F_DIR : 0));
        nd->hdr = blk;
        nd->size = nd->hsize = isdir ? 0 : h.size;
        nd->parent = nd->hparent = (uint16_t)parent;
        nd->name = pool_add(h.name, h.nlen);
        nd->comment = h.clen ? pool_add(h.comment, h.clen) : 0;
        nd->protect = (uint8_t)h.protect;
        nd->days = (uint16_t)h.date.days;
        nd->mins = (uint16_t)h.date.mins;
        nd->ticks = (uint16_t)h.date.ticks;
        seen_set(me);
        p.node = me;
        p.ops = OP_NEW;
        p.aparent = (uint16_t)parent;
        p.aname = nd->name;
        p.acomment = nd->comment;
        p.size = nd->size;
        p.protect = nd->protect;
        p.days = nd->days;
        p.mins = nd->mins;
        p.ticks = nd->ticks;
        pend_add(p);
    }
    if (isdir) {
        if (sp >= 40) {
            printf("[folderhd] unit %d: directories nested deeper than 39 levels are not folded back\n", unit);
        } else {
            stack[sp].block = blk;
            stack[sp].node = (uint16_t)me;
            stack[sp].slot = 0;
            stack[sp].chain = 0;
            sp++;
        }
    }
    if (walk_full) {
        sp = 0;
        walk_end();
    }
    return true;
}

// The walk is done: what was not seen is gone, and the commit phases start.
void Impl::walk_end()
{
    if (!walk_full) {
        for (uint32_t i = 1; i < nn_start; i++) {
            if (!(nodes[i].flags & F_DEAD) && !seen_get(i)) {
                Pend p;
                memset(&p, 0, sizeof(p));
                p.node = i;
                p.ops = OP_DEL;
                if (!pend_add(p)) {
                    break;
                }
            }
        }
    }
    cursor = 0;
    sub = 0;
    phase = npend ? PH_MKDIR : PH_FINISH;
}

// ---- the commit phases; each step is one host operation or one chunk ----
bool Impl::step_mkdir()
{
    while (cursor < npend) {
        Pend &p = pend[cursor++];
        if (!(p.ops & OP_NEW) || !(nodes[p.node].flags & F_DIR)) {
            continue;
        }
        char path[300];
        uint64_t sz;
        bool isd;
        if (!path_of(p.node, path, sizeof(path))) {
            p.ops |= OP_FAIL;
            fail("no path for the new folder", p.node);
            continue;
        }
        if (fs->stat(path, &sz, &isd)) {
            if (!isd) {
                p.ops |= OP_FAIL;
                fail("a file is in the way of the new folder", p.node);
            }
            continue;                                         // a folder of that name is there already: it is taken over
        }
        if (!fs->mkdir(path)) {
            step_err = true;                                  // the medium: the caller drops the run
            return false;
        }
        st->mkdirs++;
        return true;
    }
    return false;
}

bool Impl::step_material()
{
    if (!tmp) {
        while (cursor < npend) {
            Pend &p = pend[cursor];
            bool file_new = (p.ops & OP_NEW) && !(nodes[p.node].flags & F_DIR);
            if ((file_new || (p.ops & OP_CONTENT)) && !(p.ops & OP_FAIL)) {
                break;
            }
            cursor++;
        }
        if (cursor >= npend) {
            return false;
        }
        Pend &p = pend[cursor];
        p.tmp_seq = ++tmp_seq;
        char tp[300];
        pend_tmp(p, tp, sizeof(tp));
        fs->mkdir(tmpdir);
        if (!fs->save(tp, "", 0) || !(tmp = fs->open(tp, true)) || !tmp->writable()) {
            if (tmp) { fs->close(tmp); tmp = 0; }
            p.tmp_seq = 0;
            printf("[folderhd] unit %d: cannot make a temp file in %s\n", unit, tmpdir);
            phase = PH_IDLE;                                  // the caller sees the request
            return true;
        }
        if (!it_start(nodes[p.node].hdr)) {
            it_n = it_j = 0;
            it_ext = 0;
        }
        tmp_off = 0;
        tmp_left = p.size;
        if (tmp_left == 0) {
            tmp->sync();
            fs->close(tmp);
            tmp = 0;
            p.ops |= OP_TMP;
            cursor++;
        }
        return true;
    }
    Pend &p = pend[cursor];
    uint32_t n = tmp_left < FH_CHUNK ? (uint32_t)tmp_left : FH_CHUNK;
    for (uint32_t off = 0; off < n; off += FFS_BLOCK) {
        uint32_t b;
        uint8_t blk[FFS_BLOCK];
        if (!it_next(&b) || b < 2 || b >= nblk) {
            memset(blk, 0, sizeof(blk));
            printf("[folderhd] unit %d: the list of blocks of a file ends early\n", unit);
        } else {
            view_read(b, blk);
        }
        memcpy(buf + off, blk, n - off < FFS_BLOCK ? n - off : FFS_BLOCK);
    }
    if (!tmp->write(tmp_off, buf, n)) {
        fs->close(tmp);
        tmp = 0;
        printf("[folderhd] unit %d: writing the temp file failed (medium full?)\n", unit);
        phase = PH_IDLE;
        return true;
    }
    tmp_off += n;
    tmp_left -= n;
    if (tmp_left == 0) {
        bool ok = tmp->sync();
        fs->close(tmp);
        tmp = 0;
        if (!ok) {
            printf("[folderhd] unit %d: syncing the temp file failed\n", unit);
            phase = PH_IDLE;
            return true;
        }
        p.ops |= OP_TMP;
        cursor++;
    }
    return true;
}

bool Impl::step_rename(int pass)
{
    while (cursor < npend) {
        Pend &p = pend[cursor++];
        Node &nd = nodes[p.node];
        if ((p.ops & OP_FAIL) || (p.ops & OP_NEW)) {
            continue;
        }
        char from[300], to[300], sf[320], st_[320];
        uint64_t sz;
        bool isd;
        if (pass == 1) {
            if (!(p.ops & OP_REN) || (nd.flags & F_DEAD)) continue;
            hclose_all();
            if (!path_of(p.node, from, sizeof(from))) { p.ops |= OP_FAIL; fail("no path for", p.node); continue; }
            {   // straight to the final place when nothing is in the way: no window in which the object is nowhere
                uint32_t o_hname = nd.hname, o_name = nd.name;
                uint16_t o_hparent = nd.hparent, o_parent = nd.parent;
                nd.parent = nd.hparent = p.aparent;
                nd.name = p.aname;
                nd.hname = 0;
                if (path_of(p.node, to, sizeof(to)) && !fs->stat(to, &sz, &isd) && fs->rename(from, to)) {
                    snprintf(sf, sizeof(sf), "%s.uaem", from);
                    snprintf(st_, sizeof(st_), "%s.uaem", to);
                    if (fs->stat(sf, &sz, &isd)) {
                        fs->rename(sf, st_);
                    }
                    p.ops &= (uint16_t)~OP_REN;                 // done
                    st->renamed++;
                    return true;
                }
                nd.hname = o_hname; nd.name = o_name; nd.hparent = o_hparent; nd.parent = o_parent;
            }
            uint32_t old_hname = nd.hname;
            uint16_t old_hparent = nd.hparent;
            char hop[40];
            snprintf(hop, sizeof(hop), "mv%u", (unsigned)++mv_seq);
            nd.hname = pool_add((const uint8_t *)hop, (int)strlen(hop));
            if (pool_bad) {
                pool_bad = false;
                nd.hname = old_hname;
                p.ops |= OP_FAIL;
                fail("no room for the name of", p.node);
                return true;
            }
            nd.hparent = FH_LIMBO;
            snprintf(to, sizeof(to), "%s/%s", tmpdir, hop);
            fs->mkdir(tmpdir);
            if (!fs->rename(from, to)) {
                nd.hname = old_hname;
                nd.hparent = old_hparent;
                p.ops |= OP_FAIL;
                fail("cannot move", p.node);
                return true;
            }
            snprintf(sf, sizeof(sf), "%s.uaem", from);
            snprintf(st_, sizeof(st_), "%s.uaem", to);
            if (fs->stat(sf, &sz, &isd)) {
                fs->rename(sf, st_);
            }
            p.ops |= OP_HOP;
            return true;
        }
        // pass 2: from limbo to the place the Amiga gave it
        if (!(p.ops & OP_HOP)) continue;
        hclose_all();
        if (!path_of(p.node, from, sizeof(from))) { p.ops |= OP_FAIL; fail("no path for", p.node); continue; }
        uint32_t old_hname = nd.hname, old_name = nd.name;
        uint16_t old_hparent = nd.hparent, old_parent = nd.parent;
        nd.parent = nd.hparent = p.aparent;
        nd.name = p.aname;
        nd.hname = 0;
        bool ok = path_of(p.node, to, sizeof(to));
        if (ok && fs->stat(to, &sz, &isd)) {
            ok = false;
            printf("[folderhd] unit %d: %s exists on the medium and is not part of the disk; the rename is not done\n", unit, to);
        }
        if (!ok || !fs->rename(from, to)) {
            nd.parent = old_parent; nd.hparent = old_hparent; nd.name = old_name; nd.hname = old_hname;
            p.ops |= OP_FAIL;
            fail("cannot place", p.node);
            return true;
        }
        snprintf(sf, sizeof(sf), "%s.uaem", from);
        snprintf(st_, sizeof(st_), "%s.uaem", to);
        if (fs->stat(sf, &sz, &isd)) {
            fs->rename(sf, st_);
        }
        st->renamed++;
        return true;
    }
    return false;
}

bool Impl::step_delete()
{
    while (cursor > 0) {
        Pend &p = pend[--cursor];
        if (!(p.ops & OP_DEL)) {
            continue;
        }
        Node &nd = nodes[p.node];
        hclose_all();
        char path[300], sc[320];
        uint64_t sz;
        bool isd;
        if (!path_of(p.node, path, sizeof(path))) {
            node_dead(p.node);
            continue;
        }
        bool gone = fs->remove(path) || host_absent(path);
        if (!gone) {
            FsEntry le[1];
            if ((nd.flags & F_DIR) && fs->list(path, le, 1) > 0) {
                // files the disk never showed (hidden, too long ...) are in it: the folder stays, and is no part of the disk any more
                printf("[folderhd] unit %d: %s is deleted on the Amiga but holds files the disk does not show; it stays on the medium\n", unit, path);
                node_dead(p.node);
                return true;
            }
            p.ops |= OP_FAIL;
            fail("cannot delete", p.node);
            return true;
        }
        snprintf(sc, sizeof(sc), "%s.uaem", path);
        if (fs->stat(sc, &sz, &isd)) {
            fs->remove(sc);
        }
        (nd.flags & F_DIR ? st->rmdirs : st->deleted)++;
        node_dead(p.node);
        return true;
    }
    return false;
}

bool Impl::step_place()
{
    while (cursor < npend) {
        Pend &p = pend[cursor++];
        if (!(p.ops & OP_TMP) || (p.ops & OP_FAIL)) {
            continue;
        }
        hclose_all();
        char path[300], tp[300];
        uint64_t sz;
        bool isd;
        pend_tmp(p, tp, sizeof(tp));
        if (!path_of(p.node, path, sizeof(path))) {
            p.ops |= OP_FAIL;
            fail("no path for", p.node);
            continue;
        }
        bool exists = fs->stat(path, &sz, &isd);
        if (exists && ((p.ops & OP_NEW) || isd)) {
            // a host object the Amiga does not know (hidden, too long, 4 GiB) has this name: it is not overwritten
            printf("[folderhd] unit %d: %s exists on the medium and is not part of the disk; the new file is not saved\n", unit, path);
            fs->remove(tp);
            p.tmp_seq = 0;
            p.ops |= OP_FAIL;
            if (p.ops & OP_NEW) node_dead(p.node);
            fold_failed = true;
            return true;
        }
        if (exists) {
            // the old file is moved aside, not removed: when the second rename fails it is put back, and if that fails too the repair list remembers it
            char bp[300];
            bak_path(p.tmp_seq, bp, sizeof(bp));
            if (!fs->rename(path, bp)) {
                p.ops |= OP_FAIL;
                fail("cannot replace", p.node);
                return true;
            }
            if (!fs->rename(tp, path)) {
                bool back = fs->rename(bp, path);
                p.ops |= OP_FAIL;
                fail("cannot put the new content in place of", p.node);
                if (!back && nrepair < 8) {
                    repair[nrepair].node = p.node;
                    repair[nrepair].seq = p.tmp_seq;
                    repair[nrepair].size = p.size;
                    nrepair++;
                    p.tmp_seq = 0;                                     // the temp file is kept for the repair
                }
                return true;
            }
            fs->remove(bp);
        } else if (!fs->rename(tp, path)) {
            p.ops |= OP_FAIL;
            fail("cannot put the new content in place of", p.node);
            return true;
        }
        p.tmp_seq = 0;
        p.ops &= (uint16_t)~OP_TMP;
        ((p.ops & OP_NEW) ? st->created : st->rewritten)++;
        return true;
    }
    return false;
}

// A file whose header is as it was but whose data blocks the Amiga wrote: those blocks go into the host file, in place, now that the metadata that owns them is known.
bool Impl::step_patch()
{
    while (cursor < npend) {
        Pend &p = pend[cursor];
        Node &nd = nodes[p.node];
        if (!(p.ops & OP_PATCH) || (p.ops & OP_FAIL) || (nd.flags & F_DEAD)) {
            cursor++;
            sub = 0;
            continue;
        }
        uint32_t lo = nd.hdr + 1, hi = lo + file_blocks(nd);
        if (sub < lo) sub = lo;
        Handle *h = hopen(p.node, true);
        if (!h || !h->rw) {
            p.ops |= OP_FAIL;
            fail("cannot write in place into", p.node);
            cursor++;
            sub = 0;
            return true;
        }
        int done = 0;
        while (sub < hi && done < 32) {
            int a = 0, b = (int)nsr - 1, r = -1;               // the first run that ends after sub
            while (a <= b) {
                int mid = (a + b) / 2;
                if (sr[mid].start + sr[mid].count > sub) { r = mid; b = mid - 1; } else { a = mid + 1; }
            }
            if (r < 0 || sr[r].start >= hi) {
                sub = hi;
                break;
            }
            if (sr[r].start > sub) sub = sr[r].start;
            uint8_t blk[FFS_BLOCK];
            view_read(sub, blk);
            uint64_t off = (uint64_t)(sub - lo) * FFS_BLOCK;
            uint32_t n = nd.hsize - off < FFS_BLOCK ? (uint32_t)(nd.hsize - off) : FFS_BLOCK;
            if (off < nd.hsize && !h->f->write(off, blk, n)) {
                p.ops |= OP_FAIL;
                fail("writing in place failed for", p.node);
                cursor++;
                sub = 0;
                return true;
            }
            h->wrote = true;
            h->ra_len = 0;
            sub++;
            done++;
        }
        if (sub >= hi) {
            h->f->sync();
            h->wrote = false;
            st->patched++;
            cursor++;
            sub = 0;
        }
        return true;
    }
    return false;
}

// The .uaem of an object: protection other than "all allowed" or a comment (C3); otherwise none.
bool Impl::sidecar_path(uint32_t idx, char *out, int n)
{
    char path[300];
    if (!path_of(idx, path, sizeof(path))) {
        return false;
    }
    snprintf(out, (size_t)n, "%s.uaem", path);
    return true;
}

bool Impl::write_sidecar(uint32_t idx, const Pend &p)
{
    char sp[320];
    uint64_t sz;
    bool isd;
    if (!sidecar_path(idx, sp, sizeof(sp))) {
        return false;
    }
    const uint8_t *com = pstr(p.acomment);
    int cl = p.acomment ? com[0] : 0;
    if (p.protect == 0 && cl == 0) {
        if (fs->stat(sp, &sz, &isd)) {
            return fs->remove(sp);
        }
        return true;
    }
    AmDate d = { p.days, p.mins, p.ticks };
    int y, mo, dd, h, mi, s, hu;
    ffs_date_civil(d, &y, &mo, &dd, &h, &mi, &s, &hu);
    char line[200];
    int o = snprintf(line, sizeof(line), "%c%c%c%c%c%c%c%c %04d-%02d-%02d %02d:%02d:%02d.%02d ",
                     (p.protect & 0x80) ? 'h' : '-', (p.protect & 0x40) ? 's' : '-', (p.protect & 0x20) ? 'p' : '-', (p.protect & 0x10) ? 'a' : '-',
                     (p.protect & 0x08) ? '-' : 'r', (p.protect & 0x04) ? '-' : 'w', (p.protect & 0x02) ? '-' : 'e', (p.protect & 0x01) ? '-' : 'd',
                     y, mo, dd, h, mi, s, hu);
    for (int i = 0; i < cl && o < (int)sizeof(line) - 2; i++) {
        line[o++] = (char)com[1 + i];
    }
    line[o++] = '\n';
    return fs->save(sp, line, (uint32_t)o);
}

bool Impl::step_props()
{
    while (cursor < npend) {
        Pend &p = pend[cursor++];
        if ((p.ops & (OP_FAIL | OP_DEL)) || !(p.ops & (OP_PROPS | OP_NEW)) || (nodes[p.node].flags & F_DEAD)) {
            continue;
        }
        if (!write_sidecar(p.node, p)) {
            fail("cannot write the .uaem of", p.node);
        }
        return true;
    }
    return false;
}

// ---- the end: the index takes over what the host holds ----
void Impl::fold_finish()
{
    bool keep = late || fold_failed;
    for (uint32_t i = 0; i < npend; i++) {
        Pend &p = pend[i];
        Node &nd = nodes[p.node];
        if (p.ops & OP_FAIL) {
            if ((p.ops & OP_NEW) && !(nd.flags & F_DEAD)) {
                node_dead(p.node);                           // not on the host: the next walk finds it new again
            }
            continue;
        }
        if (p.ops & OP_DEL) {
            continue;
        }
        if (p.ops & OP_PATCH) {
            nd.flags &= (uint8_t)~F_DELTA;
            if (!keep) sr_remove_range(nd.hdr + 1, file_blocks(nd));
        }
        if (p.ops & OP_NEW) {
            nd.flags &= (uint8_t)~F_NEW;
            if (ncre < FH_CREATED_MAX) {
                if (ncre == cre_cap) {
                    uint16_t *q = (uint16_t *)mem_realloc(cre, cre_cap * sizeof(uint16_t), (cre_cap + 256) * sizeof(uint16_t));
                    if (!q) { fold_failed = keep = true; continue; }
                    cre = q;
                    cre_cap += 256;
                }
                int lo = 0, hi = (int)ncre;
                while (lo < hi) { int mid = (lo + hi) / 2; if (nodes[cre[mid]].hdr < nd.hdr) lo = mid + 1; else hi = mid; }
                memmove(&cre[lo + 1], &cre[lo], (ncre - (uint32_t)lo) * sizeof(uint16_t));
                cre[lo] = (uint16_t)p.node;
                ncre++;
            }
        }
        if (p.ops & (OP_PROPS | OP_NEW)) {
            nd.protect = p.protect;
            nd.comment = p.acomment;
            nd.days = p.days;
            nd.mins = p.mins;
            nd.ticks = p.ticks;
        }
        if (!(nd.flags & F_DIR) && (p.ops & (OP_CONTENT | OP_NEW))) {
            nd.hsize = p.size;
            nd.flags &= (uint8_t)~F_DELTA;
            uint32_t n = (p.size + FFS_BLOCK - 1) / FFS_BLOCK;
            if (n && it_start(nd.hdr)) {
                uint32_t run_b = 0, run_j = 0, run_n = 0;
                bool xr_ok = true;
                for (uint32_t j = 0; j <= n; j++) {
                    uint32_t b = 0;
                    bool have = j < n && it_next(&b);
                    bool pristine = have && p.node < nbase && b == nd.hdr + 1 + j;
                    if (have && !pristine && run_n && b == run_b + run_n && j == run_j + run_n) {
                        run_n++;
                        continue;
                    }
                    if (run_n) {
                        xr_ok = xr_insert(run_b, run_n, run_j, p.node) && xr_ok;
                        if (!keep && xr_ok) sr_remove_range(run_b, run_n);
                        run_n = 0;
                    }
                    if (have && !pristine) {
                        run_b = b; run_j = j; run_n = 1;
                    } else if (have && pristine && !keep) {
                        sr_remove_range(b, 1);
                    }
                }
                if (!xr_ok) {
                    fold_failed = keep = true;
                    printf("[folderhd] unit %d: the list of folded-back blocks is full; the data stays in the scratch file\n", unit);
                }
            }
        }
    }
    memo_blk = 0xFFFFFFFFu;
    meta_gen++;
    st->foldbacks++;
    if (!keep && nsr == 0 && smeta == 0) {
        scratch_reset();
    }
    if (fold_failed) {
        retry_ms = io->now_ms() + RETRY_MS;
    } else {
        folded_gen = job_gen;
    }
    if (!fold_failed && late) {
        folded_gen = job_gen;                                 // gen moved on: the next run folds what came meanwhile
    }
    npend = 0;
    phase = PH_IDLE;
}

// ---- the dispatcher ----
bool Impl::fold_step()
{
    if (!host_rw) {
        return false;
    }
    if (phase >= PH_WALK && phase <= PH_MATERIAL && gen != job_gen) {
        fold_abort("the Amiga wrote while the changes were collected");
        return true;
    }
    if (phase >= PH_WALK && phase <= PH_MATERIAL && pool_bad) {
        pool_bad = false;
        fold_abort("the room for new names is used up (FH_POOL_EXTRA); the disk has to be mounted again");
        retry_ms = io->now_ms() + RETRY_MS;
        st->fold_errors++;
        return false;
    }
    switch (phase) {
    case PH_IDLE: {
        hsync_all();
        if (gen == folded_gen) {
            return false;
        }
        if (!force && (int32_t)(io->now_ms() - retry_ms) < 0) {
            return false;
        }
        if (nrepair && !run_repair()) {
            retry_ms = io->now_ms() + RETRY_MS;
            return false;
        }
        if (!force) {                                          // the Amiga marks the bitmap invalid while it is in the middle of a change
            uint8_t rb[FFS_BLOCK];
            view_read(R, rb);
            if (ffs_rd(rb, FL_BM_FLAG) != 0xFFFFFFFFu) {
                if (!bm_waiting) {
                    bm_waiting = true;
                    bm_wait_ms = io->now_ms();
                }
                if (io->now_ms() - bm_wait_ms < BM_WAIT_MS) {
                    return false;
                }
            } else {
                bm_waiting = false;
            }
        }
        fold_begin();
        return phase != PH_IDLE;
    }
    case PH_WALK:
        walk_step();
        return true;
    case PH_MKDIR:
        if (!step_mkdir()) {
            if (step_err) {                                    // the medium refused
                step_err = false;
                fold_abort("a folder could not be made");
                retry_ms = io->now_ms() + RETRY_MS;
                st->fold_errors++;
                return false;
            }
            cursor = 0;
            phase = PH_MATERIAL;
        }
        return true;
    case PH_MATERIAL:
        if (!step_material()) {
            cursor = 0;
            phase = PH_REN1;
            if (late) late = false;
        } else if (phase == PH_IDLE) {
            fold_abort("the temp file could not be written");
            retry_ms = io->now_ms() + RETRY_MS;
            st->fold_errors++;
            return false;
        }
        return true;
    case PH_REN1:
        if (!step_rename(1)) {
            cursor = npend;
            phase = PH_DELETE;
        }
        return true;
    case PH_DELETE:
        if (!step_delete()) {
            cursor = 0;
            phase = PH_REN2;
        }
        return true;
    case PH_REN2:
        if (!step_rename(2)) {
            cursor = 0;
            phase = PH_PLACE;
        }
        return true;
    case PH_PLACE:
        if (!step_place()) {
            cursor = 0;
            sub = 0;
            phase = PH_PATCH;
        }
        return true;
    case PH_PATCH:
        if (!step_patch()) {
            cursor = 0;
            phase = PH_PROPS;
        }
        return true;
    case PH_PROPS:
        if (!step_props()) {
            phase = PH_FINISH;
        }
        return true;
    case PH_FINISH:
        fold_finish();
        return false;
    }
    return false;
}

// ============================================================================================================================================
// FolderDisk: sync (everything now) and the idle slice
// ============================================================================================================================================
bool FolderDisk::sync()
{
    Impl *m = p;
    bool ok = m->hsync_all();
    if (!m->host_rw) {
        return ok;
    }
    m->force = true;
    uint32_t guard = 0;
    uint32_t errs = m->st->fold_errors;
    while ((m->gen != m->folded_gen || m->phase != PH_IDLE) && guard++ < 200000000u) {
        if (!m->fold_step() && m->phase == PH_IDLE) {
            break;
        }
    }
    m->force = false;
    return ok && m->st->fold_errors == errs && !m->fold_failed;
}

bool FolderDisk::idle_slice()
{
    Impl *m = p;
    uint32_t t0 = m->io->now_ms(), last = t0;
    bool more = true;
    uint32_t longest = 0;
    while (more) {
        more = m->fold_step();
        uint32_t now = m->io->now_ms();
        if (now - last > longest) longest = now - last;
        last = now;
        if (now - t0 + longest >= FH_SLICE_MS) {
            break;
        }
    }
    uint32_t used = m->io->now_ms() - t0;
    if (used > m->st->slice_max_ms) {
        m->st->slice_max_ms = used;
    }
    return more;
}
