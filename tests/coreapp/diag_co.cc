// T175 co-check: the application's compiler and decoder against the gateware's probe engine (cores/diag/sim/co/run.sh).
//   diag_co capture <test.toml> <steps.hex>   runs the application's runner against a recorder that only writes down the step words it is given
//   diag_co replay  <test.toml> <run.hex>     runs the runner against the recorded run of the iverilog engine (tb_co.v): the PCs it waited at, the final status
//                                              and the FIFO words; prints the result file the runner wrote
// steps.hex: the word count, then the words (hex, one per line). run.hex: the engine's last STATUS byte, the FIFO word count, the words, the wait count, the PCs.
// Both modes go through diag_run_file, so the compile, the PC-to-prompt mapping, the FIFO decode and the evaluation are the application's own code.
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include "fakes.h"
#include "diagtest.h"
#include "portdiag_regs.h"

static std::string slurp(const char *path)
{
    std::string s;
    FILE *f = fopen(path, "rb");
    if (!f) {
        return s;
    }
    char b[4096];
    size_t n;
    while ((n = fread(b, 1, sizeof(b), f)) > 0) {
        s.append(b, n);
    }
    fclose(f);
    return s;
}

struct CaptureIo : FakeIo {
    std::vector<uint32_t> words, final_words;
    uint32_t cur = 0;
    bool ran = false;
    uint8_t rd(uint32_t reg) override
    {
        reg &= 0xFFF;
        if (reg < 4) return (uint8_t)"DIAG"[reg];
        if (reg >= PD_TICKS && reg < PD_TICKS + 4) return (uint8_t)(0x6400 >> (8 * (reg - PD_TICKS)));
        if (reg == PD_STATUS) return PD_ST_DONE;
        return 0;
    }
    void wr(uint32_t reg, uint8_t v) override
    {
        reg &= 0xFFF;
        if (reg >= PD_STEP && reg < PD_STEP + 4) {
            if (reg == PD_STEP) cur = 0;
            cur |= (uint32_t)v << (8 * (reg - PD_STEP));
            if (reg == PD_STEP + 3) words.push_back(cur);
        } else if (reg == PD_STEP_ADDR) {
            words.clear();
        } else if (reg == PD_CTRL && (v & PD_CTRL_RUN)) {
            final_words = words;
            ran = true;
        }
    }
};

struct ReplayIo : FakeIo {
    uint8_t final_status = 0;
    std::vector<uint32_t> fifo, pcs;
    size_t head = 0, stage = 0;
    bool started = false;
    uint8_t rd(uint32_t reg) override
    {
        reg &= 0xFFF;
        if (reg < 4) return (uint8_t)"DIAG"[reg];
        if (reg >= PD_TICKS && reg < PD_TICKS + 4) return (uint8_t)(0x6400 >> (8 * (reg - PD_TICKS)));
        if (reg == PD_STATUS) return !started ? 0 : stage < pcs.size() ? (PD_ST_RUNNING | PD_ST_WAITGO) : (uint8_t)(final_status | PD_ST_DONE);
        if (reg == PD_PC) return stage < pcs.size() ? (uint8_t)pcs[stage] : 0;
        if (reg == PD_PC + 1) return 0;
        if (reg >= PD_FIFO && reg < PD_FIFO + 4) {
            uint32_t w = head < fifo.size() ? fifo[head] : 0;
            if (reg == PD_FIFO + 3 && head < fifo.size()) head++;
            return (uint8_t)(w >> (8 * (reg - PD_FIFO)));
        }
        if (reg == PD_FIFO_LEVEL) return (uint8_t)(fifo.size() - head);
        if (reg == PD_FIFO_LEVEL + 1) return (uint8_t)((fifo.size() - head) >> 8);
        return 0;
    }
    void wr(uint32_t reg, uint8_t v) override
    {
        reg &= 0xFFF;
        if (reg == PD_CTRL && (v & PD_CTRL_RUN)) started = true;
        if (reg == PD_CTRL && (v & PD_CTRL_GO) && stage < pcs.size()) stage++;
    }
};

struct Ui : DiagUi {
    int prompt(const char *, const char *, int, int, int) override { return DUI_CONTINUE; }
    void progress(const char *) override { }
    void show(const char *, const char *) override { }
};

static uint32_t g_ms;
static uint32_t now_ms() { return g_ms; }
static void delay_ms(uint32_t n) { g_ms += n; }
static bool stamp_fixed(char out[16]) { strcpy(out, "20261102-143015"); return true; }

static std::string base_name(const char *path)
{
    std::string p = path;
    size_t k = p.find_last_of('/');
    return k == std::string::npos ? p : p.substr(k + 1);
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: diag_co capture|replay <test.toml> <hex>\n");
        return 2;
    }
    std::string mode = argv[1];
    std::string text = slurp(argv[2]);
    if (text.empty()) {
        fprintf(stderr, "diag_co: cannot read %s\n", argv[2]);
        return 2;
    }
    CaptureIo cap;
    ReplayIo rep;
    CoreIo *io = &cap;
    if (mode == "replay") {
        std::string h = slurp(argv[3]);
        std::vector<uint32_t> v;
        const char *p = h.c_str();
        char *e;
        for (;;) {
            unsigned long x = strtoul(p, &e, 16);
            if (e == p) break;
            v.push_back((uint32_t)x);
            p = e;
        }
        if (v.size() < 3) {
            fprintf(stderr, "diag_co: %s is no run record\n", argv[3]);
            return 2;
        }
        rep.final_status = (uint8_t)v[0];
        size_t nf = v[1], k = 2;
        for (size_t i = 0; i < nf && k < v.size(); i++) rep.fifo.push_back(v[k++]);
        size_t np = k < v.size() ? v[k++] : 0;
        for (size_t i = 0; i < np && k < v.size(); i++) rep.pcs.push_back(v[k++]);
        io = &rep;
    }
    FakeFs fs;
    Ui ui;
    DiagEnv env;
    memset(&env, 0, sizeof(env));
    env.io = io;
    env.fs = &fs;
    env.ui = &ui;
    env.now_ms = now_ms;
    env.delay_ms = delay_ms;
    env.stamp = stamp_fixed;
    env.core_version = "0.1.0";
    env.folder = "/SD/cores/diag";
    fs.put_text(std::string("/SD/cores/diag/tests/") + base_name(argv[2]), text);
    // a test that needs the pins test to have passed: the result file of that run (the real run of pins is a co-check of its own)
    fs.put_text("/SD/cores/diag/results/pins_20260101-000000.toml", "[meta]\nid = \"pins\"\n\n[eval]\ndrivable = \"PASS\"\n");
    DiagOutcome out;
    memset(&out, 0, sizeof(out));
    int rc = diag_run_file(&env, (std::string("/SD/cores/diag/tests/") + base_name(argv[2])).c_str(), &out);
    if (mode == "capture") {
        if (!cap.ran) {
            fprintf(stderr, "diag_co: the runner never started the engine: %s\n", out.msg);
            return 1;
        }
        FILE *f = fopen(argv[3], "w");
        fprintf(f, "%zx\n", cap.final_words.size());          // hex like the words: the TB reads the file with $readmemh
        for (uint32_t w : cap.final_words) fprintf(f, "%08x\n", w);
        fclose(f);
        printf("capture %s: %zu step words\n", base_name(argv[2]).c_str(), cap.final_words.size());
        return 0;
    }
    std::string res;
    for (auto &kv : fs.files) {
        if (kv.first.find("/results/") != std::string::npos && kv.first.size() > 5 && kv.first.compare(kv.first.size() - 5, 5, ".toml") == 0 && kv.first.find("pins_20260101") == std::string::npos) {
            res = std::string(kv.second.begin(), kv.second.end());
        }
    }
    printf("%s", res.c_str());
    printf("co-check %s: rc=%d ntests=%d npass=%d nfail=%d nrefused=%d msg=\"%s\"\n", base_name(argv[2]).c_str(), rc, out.ntests, out.npass, out.nfail, out.nrefused, out.msg);
    // cd32 v2 has evals that must fail on the model (the ID bit: a model constant within a phase cannot shift); its verdicts are held to account by models/cd32.expect
    bool mixed = base_name(argv[2]) == "cd32.toml";
    return rc == 0 && out.ntests == 1 && (mixed ? out.nfail + out.npass == 1 : (out.npass == 1 && !out.nfail)) ? 0 : 1;
}
