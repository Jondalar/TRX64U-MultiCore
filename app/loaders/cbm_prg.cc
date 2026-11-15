#include "cbm_prg.h"
#include <stdio.h>
#include <string.h>

// A BASIC program is a chain of lines: two bytes link to the next line, two bytes line number, the
// tokenised text, a zero. A link of 0 ends the chain. Moving the program to another address means
// rewriting every link by the difference; nothing else in a line refers to an address.
uint16_t cbm_prg_relink(uint8_t *prog, uint32_t n, uint16_t from, uint16_t to)
{
    uint32_t off = 0;
    uint16_t addr = to;
    while (off + 1 < n) {
        uint16_t link = (uint16_t)(prog[off] | (prog[off + 1] << 8));
        if (!link) {
            return (uint16_t)(addr + 2);              // the two zero bytes belong to the program
        }
        if (link < from) {
            return (uint16_t)(addr + 2);              // a link that points backwards: not a chain we can follow
        }
        uint32_t next = (uint32_t)(link - from);
        if (next <= off || next >= n) {
            return (uint16_t)(addr + 2);              // a link outside the file: stop, do not write past it
        }
        uint16_t moved = (uint16_t)(link + (to - from));
        prog[off] = (uint8_t)(moved & 0xFF);
        prog[off + 1] = (uint8_t)(moved >> 8);
        addr = (uint16_t)(addr + (next - off));
        off = next;
    }
    return (uint16_t)(addr + 2);
}

static bool is_basic_start(uint16_t a, const CbmPrgParams &p)
{
    for (int i = 0; i < p.nbasic_starts && i < CBM_BASIC_STARTS_MAX; i++) {
        if (p.basic_starts[i] == a) {
            return true;
        }
    }
    return false;
}

void cbm_prg_mem_write(CoreIo *io, const CbmPrgParams &p, uint16_t addr, const void *data, uint32_t n)
{
    io->ram_write(addr, data, n);
    if (p.mem_ddr) {
        io->ddr_write(p.mem_ddr + addr, data, n);
    }
}

void cbm_prg_mem_read(CoreIo *io, const CbmPrgParams &p, uint16_t addr, void *data, uint32_t n)
{
    if (p.mem_ddr) {
        io->ddr_read(p.mem_ddr + addr, data, n);
    } else {
        io->ram_read(addr, data, n);
    }
}

static void wr16(CoreIo *io, const CbmPrgParams &p, uint16_t addr, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)(v & 0xFF), (uint8_t)(v >> 8) };
    cbm_prg_mem_write(io, p, addr, b, 2);
}

static uint16_t rd16(CoreIo *io, const CbmPrgParams &p, uint16_t addr)
{
    uint8_t b[2] = { 0, 0 };
    cbm_prg_mem_read(io, p, addr, b, 2);
    return (uint16_t)(b[0] | (b[1] << 8));
}

int cbm_prg_place(CoreIo *io, const uint8_t *data, uint32_t size, const CbmPrgParams &p, bool is_p00,
                  CbmPrgResult *out, char *msg, int msglen)
{
    memset(out, 0, sizeof(*out));
    uint32_t skip = is_p00 ? p.p00_header : 0;
    if (size < skip + p.min_size || size > p.max_size) {
        snprintf(msg, msglen, "%u bytes is not a program", (unsigned)size);
        return 1;
    }
    const uint8_t *f = data + skip;
    uint32_t n = size - skip;
    uint16_t load = (uint16_t)(f[0] | (f[1] << 8));
    const uint8_t *body = f + 2;
    uint32_t blen = n - 2;
    if (!blen) {
        snprintf(msg, msglen, "the file holds a load address and nothing else");
        return 1;
    }

    if ((uint32_t)load + blen > 0x10000u) {
        // the file would wrap round to $0000: refused, not folded back into the zero page
        snprintf(msg, msglen, "$%04X + %u bytes runs past $FFFF", load, (unsigned)blen);
        return 1;
    }

    if (!is_basic_start(load, p)) {
        // a machine-language file: it goes where it says, and SYS starts it there
        cbm_prg_mem_write(io, p, load, body, blen);
        out->load = load;
        out->end = (uint16_t)(load + blen);
        out->basic = false;
        out->start = load;
        return 0;
    }

    // A BASIC program goes to the machine's own start of BASIC, which the machine itself states - the
    // expansion it came up with decides it, and the plausibility window is what keeps a machine that has
    // not finished its reset from sending the program into nowhere.
    uint16_t txt = rd16(io, p, p.txttab);
    if (txt < p.txttab_min || txt > p.txttab_max) {
        snprintf(msg, msglen, "the machine says BASIC starts at $%04X, which cannot be right", txt);
        return 1;
    }
    if ((uint32_t)txt + blen > 0x10000u) {
        snprintf(msg, msglen, "the program does not fit above $%04X", txt);
        return 1;
    }
    static uint8_t buf[0x10000];
    uint32_t len = blen < sizeof(buf) ? blen : sizeof(buf);
    memcpy(buf, body, len);
    uint16_t end = cbm_prg_relink(buf, len, load, txt);
    cbm_prg_mem_write(io, p, txt, buf, len);
    wr16(io, p, p.vartab, end);
    wr16(io, p, p.arytab, end);
    wr16(io, p, p.strend, end);
    out->load = txt;
    out->end = end;
    out->basic = true;
    out->start = 0;
    return 0;
}

void cbm_prg_type_start(CoreIo *io, const CbmPrgParams &p, const CbmPrgResult &r)
{
    char line[16];
    int n;
    if (r.basic) {
        n = snprintf(line, sizeof(line), "RUN\r");
    } else {
        n = snprintf(line, sizeof(line), "SYS%u\r", (unsigned)r.start);
    }
    io->ram_write(p.kbd_buffer, line, (uint32_t)n);
    uint8_t c = (uint8_t)n;
    io->ram_write(p.kbd_count, &c, 1);
}

int cbm_prg_script_check(const char *text, char *msg, int msglen)
{
    size_t piece = 0;
    for (const char *p = text;; p++) {
        if (*p == '|' || !*p) {
            if (piece > 10) {
                snprintf(msg, msglen, "a typed piece of %u keys does not fit the keyboard buffer (10)", (unsigned)piece);
                return 1;
            }
            piece = 0;
            if (!*p) {
                return 0;
            }
        } else {
            piece++;
        }
    }
}

int cbm_prg_script_step(CoreIo *io, const CbmPrgParams &p, const char *script, int *pos)
{
    uint8_t cnt = 0;
    io->ram_read(p.kbd_count, &cnt, 1);
    if (cnt) {
        return 0;
    }
    const char *s = script + *pos;
    if (!*s) {
        return 1;
    }
    const char *end = strchr(s, '|');
    size_t n = end ? (size_t)(end - s) : strlen(s);
    io->ram_write(p.kbd_buffer, s, (uint32_t)n);
    uint8_t k = (uint8_t)n;
    io->ram_write(p.kbd_count, &k, 1);
    *pos += (int)n + (end ? 1 : 0);
    return 0;
}

// T206: the key of a PETSCII character on the C64 keyboard (moved here by T217 so the bench's writer links it without the core application)
// Unshifted keys by index (col * 8 + row), the PETSCII character they type; 0 = a key that types no character in a name or a command.
static const char key_char[64] = {
    0, '\r', 0, 0, 0, 0, 0, 0,        '3', 'W', 'A', '4', 'Z', 'S', 'E', 0,
    '5', 'R', 'D', '6', 'C', 'F', 'T', 'X',   '7', 'Y', 'G', '8', 'B', 'H', 'U', 'V',
    '9', 'I', 'J', '0', 'M', 'K', 'O', 'N',   '+', 'P', 'L', '-', '.', ':', '@', ',',
    0, '*', ';', 0, 0, '=', 0, '/',            '1', 0, 0, '2', ' ', 0, 'Q', 0 };
// The characters that need L SHIFT, and the key it goes with.
static const struct { char ch; char base; } shifted[] = {
    { '!', '1' }, { '"', '2' }, { '#', '3' }, { '$', '4' }, { '%', '5' }, { '&', '6' }, { '\'', '7' }, { '(', '8' }, { ')', '9' },
    { '<', ',' }, { '>', '.' }, { '?', '/' }, { '[', ':' }, { ']', ';' } };

static int idx_of(char ch)
{
    for (int i = 0; i < 64; i++) {
        if (key_char[i] == ch) {
            return i;
        }
    }
    return -1;
}

bool cbm_key_for(uint8_t ch, CbmKey *k)
{
    int i = ch ? idx_of((char)ch) : -1;
    if (i >= 0) {
        k->idx = (uint8_t)i;
        k->shift = false;
        return true;
    }
    if (ch >= 0xC1 && ch <= 0xDA) {                     // SHIFT + letter
        k->idx = (uint8_t)idx_of((char)(ch - 0xC1 + 'A'));
        k->shift = true;
        return true;
    }
    for (unsigned n = 0; n < sizeof(shifted) / sizeof(shifted[0]); n++) {
        if (shifted[n].ch == (char)ch) {
            k->idx = (uint8_t)idx_of(shifted[n].base);
            k->shift = true;
            return true;
        }
    }
    return false;
}

