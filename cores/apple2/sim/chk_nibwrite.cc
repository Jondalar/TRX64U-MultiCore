// T89.1 (t89-write): why a .nib track written in place lost the address fields after the written data fields on the board (Wasteland, track 34: 8 of 16
// left). Pure model of the application side, no gateware: the track of a .nib as the application plays it (a2n_nib_bits), a DOS style in-place write of the
// data field after every address field (5 sync bytes of 10 cells, D5 AA AD, the field, DE AA EB, an FF cut short after 4 cells; first written cell K cells
// after the address field's epilogue, K = 0..16), the written bits back into nibbles (a2n_bits_nibbles, what save_track keeps). Usage: chk_nibwrite
//   mode 1  the application's bits (10-bit sync in the gaps): every one of the 16 address and 16 data fields has to come back, for every K
//   mode 0  the bits of main's application (8 cells per nibble, no sync cells): must LOSE address fields for some K (the control: the model can fail)
// The gateware is not in this loop: it writes the cells it is given (tb_a2_rwts checks that bit for bit).
#include "apple2_nib.h"
#include <stdio.h>
#include <string.h>
#include <vector>

static unsigned gb(const uint8_t *b, uint32_t p) { return (b[p >> 3] >> (7 - (p & 7))) & 1u; }
static void sb(uint8_t *b, uint32_t p, unsigned v) { if (v) b[p >> 3] |= (uint8_t)(0x80 >> (p & 7)); else b[p >> 3] &= (uint8_t)~(0x80 >> (p & 7)); }

static int run(const uint8_t *ring, int mode, int K, int *addr, int *data)
{
    static uint8_t bits[A2N_BUF_BITS / 8 + 64], wb[A2N_BUF_BITS / 8 + 64], out[A2N_TRACK + 16];
    memset(bits, 0, sizeof(bits));
    uint32_t nb = 0;
    if (mode == 1) {
        nb = a2n_nib_bits(ring, bits);
    } else {
        for (int i = 0; i < A2N_TRACK; i++) for (int k = 7; k >= 0; k--) sb(bits, nb++, (ring[i] >> k) & 1);
    }
    memcpy(wb, bits, sizeof(bits));
    std::vector<uint32_t> at;
    std::vector<uint8_t> nv;
    unsigned rsh = 0;
    for (uint32_t i = 0; i < nb; i++) {
        rsh = ((rsh << 1) | gb(bits, i)) & 255u;
        if (rsh & 128u) { at.push_back(i - 7); nv.push_back((uint8_t)rsh); rsh = 0; }
    }
    int nwr = 0;
    for (size_t k = 0; k + 14 < nv.size(); k++) {
        if (!(nv[k] == 0xD5 && nv[k + 1] == 0xAA && nv[k + 2] == 0x96)) continue;
        size_t d = k + 13;
        while (d < k + 60 && !(nv[d] == 0xD5 && nv[d + 1] == 0xAA && nv[d + 2] == 0xAD)) d++;
        if (d + 349 >= nv.size()) continue;
        uint32_t p = at[k + 12] + 8 + (uint32_t)K;
        std::vector<uint8_t> g;
        std::vector<int> gl;
        for (int s = 0; s < 5; s++) { g.push_back(0xFF); gl.push_back(10); }
        for (size_t q = d; q < d + 349; q++) { g.push_back(nv[q]); gl.push_back(8); }
        g.push_back(0xFF); gl.push_back(4);
        for (size_t q = 0; q < g.size(); q++) for (int c = 0; c < gl[q]; c++) { sb(wb, p % nb, c < 8 ? (g[q] >> (7 - c)) & 1u : 0u); p++; }
        nwr++;
    }
    uint32_t nn = a2n_bits_nibbles(wb, nb, out, A2N_TRACK);
    *addr = *data = 0;
    for (uint32_t i = 0; i + 2 < nn; i++) {
        *addr += out[i] == 0xD5 && out[i + 1] == 0xAA && out[i + 2] == 0x96;
        *data += out[i] == 0xD5 && out[i + 1] == 0xAA && out[i + 2] == 0xAD;
    }
    return nwr;
}

int main(int argc, char **argv)
{
    int mode = argc > 1 ? argv[1][0] - '0' : 1;
    std::vector<uint8_t> sec(4096);
    for (int f = 0; f < 16; f++) for (int i = 0; i < 256; i++) sec[f * 256 + i] = (uint8_t)(i * 7 + f * 13 + 1);
    static uint8_t ring[A2N_TRACK];
    a2n_nibblize_track(sec.data(), A2_ORDER_DOS, 0, ring);
    int worst = 16, worstd = 16;
    for (int K = 0; K <= 16; K++) {
        int a, d;
        run(ring, mode, K, &a, &d);
        if (a < worst) worst = a;
        if (d < worstd) worstd = d;
    }
    printf("chk_nibwrite mode %d: after a write of all 16 data fields, at worst %d of 16 address fields and %d of 16 data fields are left\n", mode, worst, worstd);
    if (mode == 1) return (worst == 16 && worstd == 16) ? 0 : 1;
    return (worst < 16) ? 0 : 1;
}
