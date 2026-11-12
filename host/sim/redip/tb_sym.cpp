// SPDX-License-Identifier: GPL-3.0-or-later
// T25.4 bench: the SIDs of sid_multi are the same SID. The same op goes to all
// of them in the same cycle (op_all); with equal settings (model, RES, DIGI, filter)
// the eight samples and the read-back of OSC3 / ENV3 / bus value must be equal in
// every cycle, whatever voice slot a SID occupies in the pipeline.
//   tb_sym <seed> <cycles> <mode 0|1|2>   0 all equal, 1 some differ, 2 PULSE_FIX = 0: even SIDs equal, odd SIDs equal,
//                                         and (8580) even differs from odd
// Run with PULSE_FIX = 1 (equal) and, as a control that the bench can see a
// difference, with PULSE_FIX = 0 (reDIP's pulse behaviour of the last voices of a
// pair of SIDs; 8580 final mix-in only, expect-differences 1).
#include "Vsid_multi.h"
#include "verilated.h"
#include <cstdio>
#include <cstdlib>
#include <random>

int main(int argc, char** argv)
{
    Verilated::commandArgs(argc, argv);
    const int seed = argc > 1 ? atoi(argv[1]) : 1;
    const long ncyc = argc > 2 ? atol(argv[2]) : 100000;
    const int expect_diff = argc > 3 ? atoi(argv[3]) : 0;
    std::mt19937 rnd(seed);
    Vsid_multi* d = new Vsid_multi;
    auto tick = [&]() { d->clk = 0; d->eval(); d->clk = 1; d->eval(); };
    const int m = rnd() & 1;
    d->cfg_8580 = m ? 3 : 0;
    d->cfg_res_high = (rnd() & 1) ? 3 : 0;
    const int dg = rnd() & 3;
    d->cfg_digi = dg | (dg << 2);
    // filter source 2 (5 * FC): the own 6581 curve differs between the even and odd SID of a pair (-5 on base and offset)
    d->cfg_fc_sel = 0xA; d->cfg_auto = 0; d->cfg_fc_base = 245 | (245 << 9); d->cfg_fc_off = 0; d->cfg_bypass = 0;
    d->start = 0; d->op_valid_i = 0; d->op_all_i = 1; d->lv_addr = 0;
    d->rst = 1;
    for (int i = 0; i < 64; i++) tick();
    d->rst = 0;
    for (int i = 0; i < 1024; i++) (void)rnd();   // the stimulus of the T25.4 bench (it drew its curve table here)
    long ndiff = 0, nread = 0, ncross = 0;
    long smp[8];
    auto field = [&](int s, int off) {
        int bit = 24 * s + off;
        unsigned x = 0;
        for (int i = 0; i < 8; i++) x |= ((d->rb_all[(bit + i) / 32] >> ((bit + i) % 32)) & 1u) << i;
        return x;
    };
    for (long cyc = 0; cyc < ncyc; cyc++) {
        bool have = (rnd() % 3) != 0;
        bool w = (rnd() % 5) != 0;
        unsigned r = w ? rnd() % 25 : 0x1b + (rnd() & 1);
        // waveform bits favoured: more pulse / saw / triangle combinations in the control registers
        unsigned v = rnd() & 0xff;
        if (w && r % 7 == 4 && r < 21) v = ((rnd() % 3) ? (1 << (4 + rnd() % 4)) | (1 << (4 + rnd() % 4)) : v) | (rnd() % 3 ? 1 : 0);
        d->op_valid_i = have;
        d->op_we_i = w;
        d->op_sid_i = 0;
        d->op_reg_i = r;
        d->op_wdata_i = v;
        d->op_rdata_i = 0;
        d->start = 1;
        for (int k = 0; k < 64; k++) {
            tick();
            d->start = 0;
            if (d->smp_valid) smp[d->smp_sid] = (int32_t)(d->smp_data << 12) >> 12;
        }
        auto same = [&](int a, int b) {
            return smp[a] == smp[b] && field(a, 0) == field(b, 0) && field(a, 8) == field(b, 8) && field(a, 16) == field(b, 16);
        };
        bool eq = true;
        if (expect_diff == 2) {
            // PULSE_FIX = 0: SIDs at the same place in a pair are the same SID; the two places differ (8580)
            for (int s = 2; s < 8; s++) eq &= same(s, s & 1);
            if (!same(0, 1)) ncross++;
        } else {
            for (int s = 1; s < 8; s++) eq &= same(s, 0);
        }
        if (!eq) ndiff++;
        if (have && !w) nread++;
        if (!expect_diff && !eq && ndiff <= 5)
            printf("cycle %ld: samples differ: %ld %ld %ld %ld %ld %ld %ld %ld\n", cyc, smp[0], smp[1], smp[2], smp[3], smp[4], smp[5], smp[6], smp[7]);
    }
    bool ok = expect_diff == 1 ? ndiff > 0 : ndiff == 0;
    if (expect_diff == 2) {
        // the quirk of reDIP's 8580 final mix-in must show between the two places, never with the 6581
        ok = ok && (m ? ncross > 0 : ncross == 0);
        printf("pair places differ in %ld cycles (8580: expected some, 6581: none)\n", ncross);
    }
    printf("%s: seed %d model %d, %ld cycles, %ld cycles with differing SIDs (%s)\n", ok ? "PASS" : "FAIL", seed, m ? 8580 : 6581,
           ncyc, ndiff, expect_diff == 1 ? "expected: some" : expect_diff == 2 ? "expected: even/odd classes equal" : "expected: none");
    delete d;
    return ok ? 0 : 1;
}
