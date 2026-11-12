// T85: amiga_audio (FIFO + rate adaption) under machine stalls. A machine that makes a 1 kHz sine (left) and a 440 Hz sine
// (right) in MACHINE time (ce28 ticks), stalls like the elastic clock, and a host that takes a frame every 512 mclk.
//   tb_audio <scenario> [seconds]      scenarios: clean, board (0.2 %, bursts to 3 ms), burst (0.2 %, bursts to 6 ms),
//                                      heavy (1 %, bursts to 5 ms), long (one 30 ms stall), pause (Freeze for 150 ms), rate (machine 0.6 % slow), rate2 (1.5 %), rate3 (2.5 %, beyond the clamp)
// Prints the pitch of the output in 100 ms windows against the nominal pitch and against what the old zero order hold
// path would have had (the ce28 ticks the machine actually made in that window), the largest step between two frames, the
// underflow / overflow counts. PASS when the scenario's limits hold.
#include "Vamiga_audio.h"
#include "verilated.h"
#include "Vamiga_audio___024root.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <random>

static const double SYS_HZ = 31527960.0, M_HZ = 24576000.0, CE_HZ = 28375160.0;
static const double A = 12000.0;
static int sx24(unsigned v) { return (int)(v << 8) >> 8; }

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    std::string sc = argc > 1 ? argv[1] : "board";
    double secs = argc > 2 ? atof(argv[2]) : 6.0;
    double pstart = 0, lmean = 0, lmax = 0, slow = 0;          // stall bursts: start probability per clock, mean and max length (clocks)
    if (sc == "board")      { lmean = 0.0003 * SYS_HZ; lmax = 0.003 * SYS_HZ; pstart = 1.0 / (0.150 * SYS_HZ); }
    else if (sc == "burst") { lmean = 0.0015 * SYS_HZ; lmax = 0.006 * SYS_HZ; pstart = 1.0 / (0.750 * SYS_HZ); }
    else if (sc == "heavy") { lmean = 0.0010 * SYS_HZ; lmax = 0.005 * SYS_HZ; pstart = 1.0 / (0.100 * SYS_HZ); }
    else if (sc == "rate")  { slow = 0.006; }
    else if (sc == "rate2") { slow = 0.015; }
    else if (sc == "rate3") { slow = 0.025; }
    bool longstall = sc == "long", pausetest = sc == "pause";
    double t_long = 2.0, d_long = 0.030, t_pause = 3.0, d_pause = 0.150;
    std::mt19937_64 rng(12345);
    std::uniform_real_distribution<double> U(0.0, 1.0);

    Vamiga_audio *d = new Vamiga_audio;
    double next_s = 0, next_m = 0, ps = 1e12 / SYS_HZ, pm = 1e12 / M_HZ;   // ps
    d->sys_clk = 0; d->mclk = 0; d->ce28 = 0; d->sample_en = 0; d->mute = 0; d->clr = 0; d->left = 0; d->right = 0;
    long sys_n = 0, m_n = 0, ce_total = 0, stall_left = 0;
    unsigned acc = 0;                                           // 9 of 10 clocks
    double slow_acc = 0;
    double tm = 0;                                              // machine time, seconds
    std::vector<double> out_l, out_r;
    std::vector<long> ce_at;                                    // ce28 total at each host frame
    std::vector<int> under_at; std::vector<char> heldf;
    long frame_n = 0; int m512 = 0;
    double fill_sum = 0; long fill_n = 0; int fill_lo = 99999, fill_hi = 0;   // fill at each host frame after 200 ms (priming done)
    bool clr_done = false; int under_before_clr = 0;
    long total_sys = (long)(secs * SYS_HZ);
    bool pause_on = false; long pause_end_sys = 0;
    int pause_zero_frames = 0; bool pause_zero_ok = true; long pause_frames = 0;
    d->eval();
    bool sys_lvl = false, m_lvl = false;
    while (sys_n < total_sys) {
        bool tick_s = next_s <= next_m;
        if (tick_s) {
            sys_lvl = !sys_lvl; d->sys_clk = sys_lvl; next_s += ps / 2;
            if (sys_lvl) {                                     // rising edge: decide the inputs for the next one
                sys_n++;
                bool ce = false;
                if (stall_left > 0) stall_left--;
                else if (pstart > 0 && U(rng) < pstart) {
                    double L = -lmean * std::log(1.0 - U(rng)); if (L > lmax) L = lmax; stall_left = (long)L;
                }
                double t = (double)sys_n / SYS_HZ;
                if (longstall && sys_n == (long)(t_long * SYS_HZ)) stall_left = (long)(d_long * SYS_HZ);
                if (pausetest && sys_n == (long)(t_pause * SYS_HZ)) { pause_on = true; pause_end_sys = sys_n + (long)(d_pause * SYS_HZ); }
                if (pause_on && sys_n >= pause_end_sys) pause_on = false;
                d->mute = pause_on;
                if (longstall) { bool c = sys_n >= (long)(4.0 * SYS_HZ) && sys_n < (long)(4.0 * SYS_HZ) + 2000; if (c && !clr_done) under_before_clr = d->st_under; d->clr = c; if (c) clr_done = true; }
                if (stall_left == 0 && !pause_on) {
                    acc += 9; if (acc >= 10) { acc -= 10; ce = true; }
                    if (ce && slow > 0) { slow_acc += slow; if (slow_acc >= 1.0) { slow_acc -= 1.0; ce = false; } }
                }
                d->ce28 = ce;
                if (ce) {
                    ce_total++; tm += 1.0 / CE_HZ;
                    d->left = (int)std::lround(A * std::sin(2 * M_PI * 1000.0 * tm));
                    d->right = (int)std::lround(A * std::sin(2 * M_PI * 440.0 * tm));
                }
            }
        } else {
            m_lvl = !m_lvl; d->mclk = m_lvl; next_m += pm / 2;
            if (m_lvl) {
                m_n++;
                if (d->sample_en) {                            // the host takes the frame that is on the port now
                    out_l.push_back((double)sx24(d->audio_l) / 256.0);
                    out_r.push_back((double)sx24(d->audio_r) / 256.0);
                    if (getenv("DBGT") && frame_n % 24000 == 0) printf("t %.2f s fill %d wptr %d rptr %d fa %d ph %d\n", frame_n / 48000.0, d->st_fill, d->rootp->amiga_audio__DOT__wptr, d->rootp->amiga_audio__DOT__rptr, d->rootp->amiga_audio__DOT__fa>>14, d->rootp->amiga_audio__DOT__ph);
                    if (getenv("DBG") && frame_n > 20000 && frame_n < 20030) printf("f%ld fill %d l %d under %d\n", frame_n, d->st_fill, sx24(d->audio_l), d->st_under);
                    if (frame_n > 9600 && d->st_fill > 1) { fill_sum += d->st_fill; fill_n++; if ((int)d->st_fill < fill_lo) fill_lo = d->st_fill; if ((int)d->st_fill > fill_hi) fill_hi = d->st_fill; }
                    ce_at.push_back(ce_total); under_at.push_back(d->st_under); heldf.push_back(pause_on); frame_n++;
                    if (pause_on) { pause_frames++; if (pause_frames > 300 && (d->audio_l != 0 || d->audio_r != 0)) pause_zero_ok = false; if (pause_frames > 300) pause_zero_frames++; }
                }
                d->sample_en = (++m512 == 512); if (m512 == 512) m512 = 0;
            }
        }
        d->eval();
    }
    // ---- analysis
    const int W = 4800;                                         // 100 ms
    double worst_dut = 0, worst_old = 0, worst_rate = 0, worst_r = 0; int nwin = 0;
    double mean_old_sum = 0, ss_dut = 0, ss_old = 0;
    // rate of the machine overall (ticks / nominal) = what an ideal rate adaption would converge to
    double machine_rate = (double)ce_total / (secs * CE_HZ);
    size_t start = 4800;                                        // skip the first 100 ms (priming)
    for (size_t w0 = start; w0 + W <= out_l.size(); w0 += W) {
        bool full = true; double pk = 0;
        for (size_t i = w0; i < w0 + W; i++) { pk = std::fmax(pk, std::fabs(out_l[i])); }
        for (size_t i = w0; i < w0 + W; i++) if (heldf[i] || (i + 400 < under_at.size() && under_at[i + 400] != under_at[i])) full = false;
        if (pk < A * 0.97) full = false;                        // faded (underflow, pause): no pitch to measure
        if (!full) continue;
        // pitch of both channels from the rising zero crossings, interpolated
        double f[2];
        for (int ch = 0; ch < 2; ch++) {
            std::vector<double> &o = ch ? out_r : out_l;
            double first = -1, last = -1; int n = 0;
            for (size_t i = w0 + 1; i < w0 + W; i++) if (o[i - 1] < 0 && o[i] >= 0) {
                double pos = (double)i - 1 + (-o[i - 1]) / (o[i] - o[i - 1]);
                if (first < 0) first = pos; last = pos; n++;
            }
            f[ch] = n > 1 ? (n - 1) / ((last - first) / 48000.0) : 0;
        }
        double fl = f[0] / 1000.0 - 1, fr = f[1] / 440.0 - 1;
        double old = (double)(ce_at[w0 + W - 1] - ce_at[w0]) / ((double)(W - 1) * CE_HZ / 48000.0) - 1;   // pitch of the old path in this window
        worst_dut = std::fmax(worst_dut, std::fabs(fl)); worst_r = std::fmax(worst_r, std::fabs(fr));
        worst_old = std::fmax(worst_old, std::fabs(old));
        worst_rate = std::fmax(worst_rate, std::fabs(fl - (machine_rate - 1)));
        mean_old_sum += old; nwin++; ss_dut += (fl - (machine_rate - 1)) * (fl - (machine_rate - 1)); ss_old += (old - (machine_rate - 1)) * (old - (machine_rate - 1));
    }
    // the largest step between two frames (left sine: a clean 1 kHz sine moves 0.131 A per frame)
    double maxstep = 0, maxstep_r = 0; size_t at = 0;
    for (size_t i = start + 1; i < out_l.size(); i++) {
        double s = std::fabs(out_l[i] - out_l[i - 1]); if (s > maxstep) { maxstep = s; at = i; }
        maxstep_r = std::fmax(maxstep_r, std::fabs(out_r[i] - out_r[i - 1]));
    }
    printf("scenario %s, %.1f s: machine ran at %.4f %% of nominal (stalled %.3f %%), %zu frames\n", sc.c_str(), secs, machine_rate * 100,
           (1 - machine_rate) * 100, out_l.size());
    printf("  pitch deviation from nominal, worst 100 ms window: left %.3f %% (%.1f cent), right %.3f %%; old zero-order-hold path %.3f %%\n",
           worst_dut * 100, 1200 * std::log2(1 + worst_dut), worst_r * 100, worst_old * 100);
    printf("  worst deviation from the machine's own average rate: %.3f %% (%d windows), rms %.3f %% (old path rms %.3f %%)\n", worst_rate * 100, nwin,
           nwin ? std::sqrt(ss_dut / nwin) * 100 : 0, nwin ? std::sqrt(ss_old / nwin) * 100 : 0);
    printf("  largest step between two frames: left %.0f (clean sine %.0f), right %.0f (clean %.0f), at frame %zu\n", maxstep, 0.1316 * A,
           maxstep_r, 2 * M_PI * 440 / 48000 * A, at);
    printf("  underflows %d, overflows %d, fill now %d\n", d->st_under, d->st_over, d->st_fill);
    double fill_mean = fill_n ? fill_sum / fill_n : 0;
    printf("  fill at the host frames: mean %.1f pairs = %.2f ms (min %d, max %d); KPI min %d max %d (ms %.2f .. %.2f)\n", fill_mean, fill_mean / 48.0, fill_lo, fill_hi,
           d->st_fmin, d->st_fmax, d->st_fmin / 48.0, d->st_fmax / 48.0);
    int errors = 0;
    auto check = [&](bool c, const char *what) { printf("%s: %s\n", c ? "ok  " : "FAIL", what); if (!c) errors++; };
    double sine_step = 0.1316 * A * 1.03;
    if (sc == "clean" || sc == "pause" || sc == "long")
        check(fill_mean > 470 && fill_mean < 490, "the FIFO holds 480 pairs = 10 ms (mean fill 470..490)");
    if (sc == "board" || sc == "burst" || sc == "rate") check(d->st_fmax >= 478 && d->st_fmax <= 490, "the fill peaks at the 10 ms target (KPI max 478..490)");
    if (sc == "clean" || sc == "board" || sc == "burst" || sc == "rate") {
        check(d->st_fmax < 1024, "fill below the RAM depth (1024)");
        check(d->st_fmin <= fill_lo && d->st_fmax >= fill_hi, "KPI min/max bracket every fill seen at the host frames");
    }
    if (sc == "clean") check(d->st_fmax - d->st_fmin < 40, "clean: KPI min/max spread under 40 pairs");
    if (sc == "clean" || sc == "board" || sc == "burst" || sc == "heavy") {
        if (sc != "heavy") check(d->st_under == 0, "no underflow in the normal stall patterns");
        check(d->st_over == 0, "no overflow");
        check(maxstep < sine_step, "no step larger than the sine's own slope (no click)");
        if (sc != "heavy") check(worst_dut < 0.012, "pitch within 1.2 % in every window");
        if (sc == "clean") check(worst_dut < 0.0015, "clean: pitch within 0.15 %");
        if (sc == "board") check(worst_dut < 0.008, "board: pitch within 0.8 % (14 cent) in every window");
    } else if (sc == "rate") {
        check(d->st_under == 0 && d->st_over == 0, "machine 0.6 % slow all the time: the pitch follows, no underflow");
        check(maxstep < sine_step, "no click");
    } else if (sc == "rate2") {      // 1.5 % slow: inside the 2 % clamp now that the target is 480 (equilibrium 480 - 0.015*65536/3 = 152)
        check(d->st_under == 0 && d->st_over == 0, "machine 1.5 % slow: the pitch follows, no underflow");
        check(d->st_fmin > 100, "KPI min fill settles near the equilibrium (>100)");
        check(maxstep < sine_step, "no click");
    } else if (sc == "rate3") {      // 2.5 % slow: beyond the 2 % clamp, underflows are expected, they must be clean
        check(d->st_under > 0, "underflows are counted (st_under moves)");
        check(d->st_fmin < 2, "KPI min fill reaches the underflow level");
        check(maxstep < sine_step, "no click while underflowing");
    } else if (sc == "long") {
        check(d->st_under == 1, "one underflow for the 30 ms stall");
        check(under_before_clr == 1 && clr_done, "the underflow was counted before the min/max clear");
        check(d->st_fmin > 400 && d->st_fmax > 400, "min/max cleared at 4 s (the stall's fill of 0 is gone), refilled to ~10 ms");
        check(maxstep < sine_step, "faded, no click");
    } else if (sc == "pause") {
        check(d->st_under == 0, "no underflow over the pause");
        check(pause_zero_ok && pause_zero_frames > 1000, "silent while held");
        check(maxstep < sine_step, "no click at the pause and back");
    }
    printf("%s tb_audio %s errors=%d\n", errors ? "FAIL" : "PASS", sc.c_str(), errors);
    delete d;
    return errors ? 1 : 0;
}
