// T104.2 S8, S15, S16: a .prg on the C64 model. "Run PRG" (reset, READY, place, type RUN or SYS), a large PRG from $0801 to $FFF7 that lands
// byte for byte under BASIC, the I/O area and the KERNAL, and "Load PRG and start from" (the CPU's address set through the state bus, with the
// typed-SYS start as its fallback and its control). The descriptor and its operations are the real ones; the machine is C64Model.
#include <string>
#include "check.h"
#include "c64_rig.h"
#include "cbm_prg.h"

const CoreDesc *const core_table[] = { &atari800_desc, &nes_desc, &c64_desc, 0 };

typedef std::vector<uint8_t> Bytes;

static Bytes prg(uint16_t load, const Bytes &body)
{
    Bytes v;
    v.push_back((uint8_t)(load & 0xFF));
    v.push_back((uint8_t)(load >> 8));
    v.insert(v.end(), body.begin(), body.end());
    return v;
}

// "10 PRINT", "20 PRINT" as a program at $0801
static Bytes basic_prg()
{
    Bytes b = { 0x07, 0x08, 10, 0, 0x99, 0, 0x0D, 0x08, 20, 0, 0x99, 0, 0, 0 };
    return prg(0x0801, b);
}

// the operations run while the menu holds the machine
static int run(C64Rig &r, const char *path, OpResult *res)
{
    OpIn in = in_str("path", path);
    r.menu_op_begin();
    return ops_call(&r.c, "c64.run_prg", &in, 1, res);
}
static int load(C64Rig &r, const char *path, int start, OpResult *res)
{
    OpIn in[2] = { in_str("path", path), in_int("start", start) };
    r.menu_op_begin();
    return ops_call(&r.c, "c64.load_prg", in, 2, res);
}

static C64Rig *booted(bool monolith = true)
{
    C64Rig *r = new C64Rig(monolith);
    r->put_roms();
    if (!r->boot()) { printf("FAIL the rig did not boot\n"); g_fails++; }
    r->io.events.clear();
    r->io.assert_t.clear();
    r->io.release_t.clear();
    r->io.log.clear();
    return r;
}

static std::string keybuf(C64Model &io)
{
    return std::string((const char *)&io.ram[0x0277], io.ram[0xC6]);
}

int main()
{
    OpResult res;

    // ---- S8: a BASIC program is relinked to the machine's own start, the pointers follow, RUN is typed with $C6 = 4
    {
        C64Rig &r = *booted();
        r.fs.put("/usb0/games/two.prg", basic_prg());
        CHECK_EQ(run(r, "/usb0/games/two.prg", &res), OPE_OK);
        r.menu_closed();
        CHECK_STR(ev_first(r.io.events), "mode_reset");     // the machine was reset first ...
        CHECK_STR(ev_last(r.io.events), "mode_unreset");
        CHECK(r.io.booted);                                        // ... and had said READY when the program went in
        CHECK_EQ(r.io.ram[0x0801] | (r.io.ram[0x0802] << 8), 0x0807);
        CHECK_EQ(r.io.ram[0x0807] | (r.io.ram[0x0808] << 8), 0x080D);
        CHECK_EQ(r.io.ram[0x0809], 20);
        for (int p : { 0x2D, 0x2F, 0x31 }) CHECK_EQ(r.io.ram[p] | (r.io.ram[p + 1] << 8), 0x080F);    // $2D-$32: the end of the program
        CHECK_EQ(r.io.ram[0xC6], 4);
        CHECK(keybuf(r.io) == "RUN\r");
        CHECK_EQ(r.io.writes_while_stopped, 0);                    // never as stopped bus cycles (S15)
        CHECK_EQ(r.io.bus_cycles, 0);
        CHECK(!r.io.memonly);                                      // the window is back to what it was
        const char *prog = ops_state(&r.c, "c64.program");
        CHECK(prog && !strcmp(prog, "two.prg"));
        delete &r;
    }
    // a machine-language file goes where it says and SYS starts it there
    {
        C64Rig &r = *booted();
        r.fs.put("/usb0/ml.prg", prg(0xC000, { 0x60, 0x60 }));
        CHECK_EQ(run(r, "/usb0/ml.prg", &res), OPE_OK);
        r.menu_closed();
        CHECK(r.io.ram[0xC000] == 0x60 && r.io.ram[0xC001] == 0x60);
        CHECK(keybuf(r.io) == "SYS49152\r");
        delete &r;
    }
    // a .P00 carries 0x1A bytes before the load address
    {
        C64Rig &r = *booted();
        Bytes p00(0x1A, 0);
        Bytes f = basic_prg();
        p00.insert(p00.end(), f.begin(), f.end());
        r.fs.put("/usb0/x.P00", p00);
        CHECK_EQ(run(r, "/usb0/x.P00", &res), OPE_OK);
        r.menu_closed();
        CHECK_EQ(r.io.ram[0x0809], 20);
        CHECK(keybuf(r.io) == "RUN\r");
        delete &r;
    }
    // a file that is no program is refused before the machine is touched: no reset, nothing typed
    {
        C64Rig &r = *booted();
        r.fs.put("/usb0/tiny.prg", Bytes{ 0x01, 0x08 });
        CHECK_EQ(run(r, "/usb0/tiny.prg", &res), OPE_FAILED);
        CHECK(strstr(res.msg, "not a program") != 0);
        CHECK(r.io.events.empty());
        r.fs.put("/usb0/huge.prg", Bytes(0xF81C, 1));
        CHECK_EQ(run(r, "/usb0/huge.prg", &res), OPE_FAILED);
        CHECK(r.io.events.empty());
        // the longest that fits the size window but would wrap round to $0000
        Bytes wrap(0xF81B, 3);
        wrap[0] = 0x01; wrap[1] = 0x08;
        r.fs.put("/usb0/wrap.prg", wrap);
        CHECK_EQ(run(r, "/usb0/wrap.prg", &res), OPE_FAILED);
        CHECK(strstr(res.msg, "past $FFFF") != 0);
        CHECK(r.io.events.empty());
        CHECK_EQ(run(r, "/usb0/missing.prg", &res), OPE_FAILED);
        CHECK(r.io.events.empty());
        delete &r;
    }
    // the machine is held while the operation runs (the menu), so the program waits for READY and goes in only when the machine runs; the
    // operation itself has put nothing into RAM and typed nothing
    {
        C64Rig &r = *booted();
        r.fs.put("/usb0/two.prg", basic_prg());
        CHECK_EQ(run(r, "/usb0/two.prg", &res), OPE_OK);
        CHECK(ops_state(&r.c, "c64.pending") && !strcmp(ops_state(&r.c, "c64.pending"), "run"));
        CHECK_EQ(r.io.ram[0x0801], 0);
        CHECK_EQ(r.io.ram[0xC6], 0);
        CHECK(!r.io.booted);                                       // reset, and held: the KERNAL has not run
        for (int i = 0; i < 40; i++) {                             // service polls while the machine is stopped would be wrong; the poll task does not make them
            r.io.delay_ms(50);
        }
        CHECK(!r.io.booted);
        CHECK_EQ(r.io.ram[0x0801], 0);
        r.menu_closed();                                           // the menu closes: now it runs, READY comes, the program goes in
        CHECK(!ops_state(&r.c, "c64.pending"));
        CHECK(!r.c.notice_pending);
        CHECK_EQ(r.io.ram[0x0809], 20);
        delete &r;
    }
    // a machine that never says READY: nothing is placed, nothing typed, and the reason is told once, on the screen of the next menu
    {
        C64Rig &r = *booted();
        r.io.boot_ms = 600000;
        r.fs.put("/usb0/two.prg", basic_prg());
        CHECK_EQ(run(r, "/usb0/two.prg", &res), OPE_OK);
        uint32_t t0 = r.io.t;
        r.menu_closed();
        CHECK(!ops_state(&r.c, "c64.pending"));                    // given up
        CHECK(r.c.notice_pending && strstr(r.c.notice, "READY") != 0);
        CHECK(r.io.t - t0 < 7000);
        CHECK_EQ(r.io.ram[0x0801], 0);
        CHECK_EQ(r.io.ram[0xC6], 0);
        delete &r;
    }
    // a reset while the program waits ends the wait; a second program replaces the first
    {
        C64Rig &r = *booted();
        r.fs.put("/usb0/two.prg", basic_prg());
        r.fs.put("/usb0/ml.prg", prg(0xC000, { 0x60, 0x60 }));
        CHECK_EQ(run(r, "/usb0/two.prg", &res), OPE_OK);
        CHECK(ops_state(&r.c, "c64.pending") != 0);
        CHECK_EQ(ops_call(&r.c, "machine.cold_reset", 0, 0, &res), OPE_OK);
        CHECK(ops_state(&r.c, "c64.pending") == 0);
        r.menu_closed();
        CHECK_EQ(r.io.ram[0x0801], 0);                             // it never came
        CHECK_EQ(run(r, "/usb0/two.prg", &res), OPE_OK);
        CHECK_EQ(run(r, "/usb0/ml.prg", &res), OPE_OK);
        r.menu_closed();
        CHECK_EQ(r.io.ram[0xC000], 0x60);
        CHECK_EQ(r.io.ram[0x0801], 0);                             // the first one was replaced, not run as well
        CHECK(std::string((const char *)&r.io.ram[0x0277], r.io.ram[0xC6]) == "SYS49152\r");
        delete &r;
    }

    // ---- S15: a large PRG, $0801-$FFF7: a SYS stub and then bytes under BASIC, the I/O area and the KERNAL
    {
        const uint32_t first = 0x0801, last = 0xFFF7;
        Bytes body(last - first + 1);
        for (size_t i = 0; i < body.size(); i++) body[i] = (uint8_t)(i * 13 + (i >> 8) * 5 + 1);
        const Bytes stub = { 0x0B, 0x08, 0x0A, 0x00, 0x9E, '2', '0', '6', '1', 0x00, 0x00, 0x00 };   // 10 SYS2061; link 0 ends the chain
        for (size_t i = 0; i < stub.size(); i++) body[i] = stub[i];
        Bytes file = prg(0x0801, body);
        {
            C64Rig &r = *booted();
            r.fs.put("/usb0/big.prg", file);
            CHECK_EQ(run(r, "/usb0/big.prg", &res), OPE_OK);
        r.menu_closed();
            int diff = 0;
            for (uint32_t a = first; a <= last; a++) diff += r.io.ram[a] != body[a - first];
            CHECK_EQ(diff, 0);                                      // byte for byte, $A000, $D000 and $E000 included
            CHECK_EQ(r.io.ram[0xD020], body[0xD020 - first]);
            CHECK(r.io.chip_writes.empty());                        // no byte reached a chip
            CHECK_EQ(r.io.bus_cycles, 0);
            CHECK_EQ(r.io.writes_while_stopped, 0);
            CHECK_EQ(r.io.ddr, 0x2F);                               // the 6510 port is the KERNAL's
            CHECK_EQ(r.io.port, 0x37);
            CHECK(keybuf(r.io) == "RUN\r");
            delete &r;
        }
        // the control: the same bytes as bus cycles with the machine stopped. $D000-$DFFF goes to the chips and is missing from RAM; a
        // bench that could not tell the two would pass above for nothing
        {
            C64Model m;
            m.stop(true);                                           // C64_STOP, MEMONLY off: the stock window
            CbmPrgParams p;
            memset(&p, 0, sizeof(p));
            p.min_size = 3; p.max_size = 0x10020;
            CbmPrgResult cr;
            char msg[100];
            CHECK_EQ(cbm_prg_place(&m, file.data(), (uint32_t)file.size(), p, false, &cr, msg, sizeof(msg)), 0);
            int diff = 0;
            for (uint32_t a = first; a <= last; a++) diff += m.ram[a] != body[a - first];
            CHECK(diff >= 0x1000 - 0x100);                          // the I/O page's bytes are not in RAM
            CHECK(!m.chip_writes.empty());
            CHECK(m.bus_cycles > 0x1000);
            // running, MEMONLY off: still a bus cycle for $Dxxx (G06) - which is why the application sets MEMONLY
            C64Model m2;
            CHECK_EQ(cbm_prg_place(&m2, file.data(), (uint32_t)file.size(), p, false, &cr, msg, sizeof(msg)), 0);
            CHECK(!m2.chip_writes.empty());
            // MEMONLY on, the machine running or stopped: the second port, all of it
            C64Model m3;
            m3.ram_memonly(true);
            CHECK_EQ(cbm_prg_place(&m3, file.data(), (uint32_t)file.size(), p, false, &cr, msg, sizeof(msg)), 0);
            diff = 0;
            for (uint32_t a = first; a <= last; a++) diff += m3.ram[a] != body[a - first];
            CHECK_EQ(diff, 0);
            CHECK(m3.chip_writes.empty());
        }
    }

    // ---- S16: "Load PRG and start from": $0000-$1FFF, started at $1000
    Bytes body(0x2000);
    for (size_t i = 0; i < body.size(); i++) body[i] = (uint8_t)(i * 3 + 0x40);
    body[0] = 0xAA; body[1] = 0xBB;                                 // $00 and $01: the file wants them as RAM
    body[0x1000] = 0x4C;                                            // JMP
    {
        C64Rig &r = *booted();
        r.fs.put("/usb0/zp.prg", prg(0x0000, body));
        CHECK_EQ(load(r, "/usb0/zp.prg", 0x1000, &res), OPE_OK);
        r.menu_closed();
        CHECK_EQ(r.io.exec_pc, 0x1000);                              // PC = $1000 on the state bus
        CHECK_EQ((int)r.io.getbits(r.io.ff, C64Model::B_DIN, 8), 0x4C);   // the fetch's opcode
        CHECK_EQ((int)r.io.getbits(r.io.ff, C64Model::B_IR, 8), 0xEA);    // the previous instruction's write-back taken out
        CHECK_EQ((int)r.io.getbits(r.io.ff, C64Model::B_CTRL + 12, 1), 0);
        CHECK_EQ(r.io.cpu_loads, 1);
        int diff = 0;
        for (size_t i = 0; i < body.size(); i++) diff += i != 0xBA && r.io.ram[i] != body[i];
        CHECK_EQ(diff, 0);
        CHECK_EQ(r.io.ram[0xBA], 8);                                 // T228 R21: "DMA Load Mimics ID" (default 8) goes into $BA after the load (FW c64_subsys.cc:1070)
        CHECK_EQ(r.io.ddr, 0x2F);                                    // $00/$01 went into the RAM under the port, not into the port
        CHECK_EQ(r.io.port, 0x37);
        CHECK_EQ(r.io.writes_while_stopped, 0);                      // the app's C64_STOP was never used ...
        CHECK_EQ(r.io.writes_while_debug_stop, (int)body.size() + 1);   // ... the debugger held the machine while the file and the device number in $BA went in
        CHECK_EQ(r.io.bus_cycles, 0);
        CHECK(!r.io.memonly);
        CHECK(r.io.cpu_a == 0x11 && r.io.cpu_x == 0x22 && r.io.cpu_y == 0x33 && r.io.cpu_s == 0xF5);   // A X Y S stay
        CHECK_EQ(r.io.cpu_p & 0x0C, 0);                              // I and D are cleared: it was stopped inside an interrupt
        CHECK_EQ((int)r.io.last_ipc_set, (int)((1u << 24) | (0x4Cu << 16) | 0x1000));
        CHECK(r.io.cmds == std::vector<uint32_t>({ C64_CMD_PAUSE_IB, C64_CMD_RUN }));
        CHECK_EQ(r.io.owner, 0);                                     // C64_STOP is the menu's again
        CHECK(!r.io.shut && !r.io.load_armed);
        const char *prog = ops_state(&r.c, "c64.program");
        CHECK(prog && !strcmp(prog, "zp.prg"));
        delete &r;
    }
    // the control: the same file started the other way, a typed SYS. It does not run: the file is over the zero page - BASIC's pointers
    // and the KERNAL's vectors the SYS goes through - and over the keyboard buffer the SYS line is typed into. The application refuses it.
    {
        C64Rig &r = *booted();
        r.io.cpu_write = false;                                      // a bitstream without the CPU write path
        r.fs.put("/usb0/zp.prg", prg(0x0000, body));
        CHECK_EQ(load(r, "/usb0/zp.prg", 0x1000, &res), OPE_FAILED);
        CHECK(strstr(res.msg, "zero page") != 0);
        CHECK(r.io.events.empty() && r.io.cmds.empty());
        // what the refusal protects against: placed and typed anyway, the system is gone and the file's own bytes at $0277 are overwritten
        C64Rig &q = *booted();
        CbmPrgParams p;
        memset(&p, 0, sizeof(p));
        p.min_size = 3; p.max_size = 0x10020; p.kbd_buffer = 0x0277; p.kbd_count = 0xC6;
        CbmPrgResult cr;
        char msg[100];
        Bytes file = prg(0x0000, body);
        CHECK(q.io.system_alive());
        q.io.ram_memonly(true);
        CHECK_EQ(cbm_prg_place(&q.io, file.data(), (uint32_t)file.size(), p, false, &cr, msg, sizeof(msg)), 0);
        cr.start = 0x1000;
        cbm_prg_type_start(&q.io, p, cr);
        CHECK(!q.io.system_alive());                                 // BASIC would never get to the SYS
        CHECK(q.io.ram[0x0277] == 'S');                              // and the typed line has eaten the file's bytes there
        CHECK(q.io.ram[0x0277] != body[0x0277]);
        CHECK(q.io.exec_pc != 0x1000);                               // nothing ran at $1000
        delete &r;
        delete &q;
    }
    // without the CPU write path a file above the work areas is started by a typed SYS
    {
        C64Rig &r = *booted();
        r.io.cpu_write = false;
        r.fs.put("/usb0/hi.prg", prg(0xC000, Bytes(32, 0x60)));
        CHECK_EQ(load(r, "/usb0/hi.prg", 0xC010, &res), OPE_OK);
        r.menu_closed();
        CHECK(keybuf(r.io) == "SYS49168\r");
        CHECK(r.io.cmds.empty());
        CHECK_EQ(r.io.ram[0xC000], 0x60);
        delete &r;
    }
    // the machine does not stop on an instruction boundary: the debugger is handed back and the reason told
    {
        C64Rig &r = *booted();
        r.io.pause_after_ms = -1;
        r.fs.put("/usb0/zp.prg", prg(0x0000, body));
        CHECK_EQ(load(r, "/usb0/zp.prg", 0x1000, &res), OPE_OK);
        r.menu_closed();
        CHECK(r.c.notice_pending && strstr(r.c.notice, "instruction boundary") != 0);
        CHECK_EQ(r.io.owner, 0);
        CHECK(!r.io.cmds.empty() && r.io.cmds.back() == C64_CMD_RUN);
        CHECK_EQ(r.io.cpu_loads, 0);
        CHECK_EQ(r.io.ram[0x1000], 0);                               // nothing was placed
        CHECK(ops_state(&r.c, "c64.program") == 0);                  // and it is not the program that runs
        delete &r;
    }
    // the state bus refuses the staged words (the gate is not shut): the debugger is handed back, nothing is loaded
    {
        C64Rig &r = *booted();
        r.io.refuse_stage = true;
        r.fs.put("/usb0/zp.prg", prg(0x0000, body));
        CHECK_EQ(load(r, "/usb0/zp.prg", 0x1000, &res), OPE_OK);
        r.menu_closed();
        CHECK(r.c.notice_pending && strstr(r.c.notice, "refused") != 0);
        CHECK_EQ(r.io.owner, 0);
        CHECK(!r.io.load_armed);
        CHECK_EQ(r.io.cpu_loads, 0);
        delete &r;
    }
    // a start address in the I/O area has no opcode the application can read without a bus cycle
    {
        C64Rig &r = *booted();
        r.fs.put("/usb0/io.prg", prg(0x1000, Bytes(16, 0xEA)));
        CHECK_EQ(load(r, "/usb0/io.prg", 0xD020, &res), OPE_OK);
        r.menu_closed();
        CHECK(r.c.notice_pending && strstr(r.c.notice, "I/O") != 0);
        CHECK_EQ(r.io.owner, 0);
        r.io.delay_ms(10);
        r.io.settle();
        CHECK(!r.io.shut);                                           // the machine runs again
        CHECK(!r.io.cmds.empty() && r.io.cmds.back() == C64_CMD_RUN);
        delete &r;
    }
    // the KERNAL's opcode is read through the ROM aperture
    {
        C64Rig &r = *booted();
        r.fs.put("/usb0/k.prg", prg(0x1000, Bytes(16, 0xEA)));
        CHECK_EQ(load(r, "/usb0/k.prg", 0xFCE2, &res), OPE_OK);
        r.menu_closed();
        CHECK_EQ(r.io.exec_pc, 0xFCE2);
        CHECK_EQ((int)r.io.getbits(r.io.ff, C64Model::B_DIN, 8), r.kernal[0x1CE2]);
        delete &r;
    }
    // T104.1 D15, Mount & Run's half of the machine: the typed script. Reset, then at READY one piece of at most ten keys at a time, the next
    // one only when the machine has taken the one before; a piece that does not fit the keyboard buffer is refused before the machine is touched.
    {
        C64Rig &r = *booted();
        OpIn in = in_str("text", "LOAD\"*\",8|,1\r|RUN\r");
        r.menu_op_begin();
        CHECK_EQ(ops_call(&r.c, "c64.type_script", &in, 1, &res), OPE_OK);
        CHECK_STR(ev_first(r.io.events), "mode_reset");
        CHECK(ops_state(&r.c, "c64.pending") && !strcmp(ops_state(&r.c, "c64.pending"), "type"));
        CHECK_EQ(r.io.ram[0xC6], 0);                               // nothing typed before the machine is at READY
        r.io.stop(false);
        std::string typed;
        for (int i = 0; i < 400 && ops_state(&r.c, "c64.pending"); i++) {
            r.io.delay_ms(50);
            OpResult x;
            ops_call(&r.c, "c64.service", 0, 0, &x);
            if (r.io.ram[0xC6] && r.io.booted) {
                std::string k = keybuf(r.io);
                if (typed.empty() || i % 2) {                      // the editor takes a piece every other poll
                    typed += k;
                    r.io.ram[0xC6] = 0;
                }
            }
        }
        CHECK(typed == "LOAD\"*\",8,1\rRUN\r");
        CHECK(ops_state(&r.c, "c64.pending") == 0);
        OpIn bad = in_str("text", "ELEVEN KEYS");
        r.io.events.clear();
        CHECK_EQ(ops_call(&r.c, "c64.type_script", &bad, 1, &res), OPE_FAILED);
        CHECK(strstr(res.msg, "keyboard buffer") != 0);
        CHECK(r.io.events.empty());                                // refused before any reset
        delete &r;
    }
    DONE("test_c64_prg");
}
