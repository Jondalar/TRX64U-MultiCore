// Copy of cores/c64u/cart_slot_c64u.v, taken 2026-11-23 (T257 D11): the C128 reads its own copy, never the c64u tree. Content unchanged below this line.
// The Ultimate's cartridge emulation on c64_top_c64u's external cartridge port
// (icepi patch 0030), in the C64 clock.
//
// The mapping is the descriptor-driven model (vendor/icepi/c64_cartridge.v
// with c64_flash_amd and c64_eeprom_93c86; T33, TREX-PROP-002): the app
// writes the descriptor (trx_io page 4) next to CART_TYPE, and this block
// takes a copy of it at every CPU reset and cartridge force, together with
// the decision whether a cartridge is on (CART_TYPE != 0 and the descriptor
// valid). Nothing here knows a cartridge type.
// The cartridge ROM image (the app copies it to DDR2 0x3C00000, 16 KB per
// bank: ROML of bank n at n * 16K, ROMH 8K above) and the cartridge RAM
// (DDR2 0xEF0000, 64 KB) both stay in DDR2; the C64 reads them through a 4 KB
// cache in one block RAM: two ways of 32 lines of 64 bytes. A line is fetched
// when the C64 reads from it; a CPU read waits (RDY) until it is in. The VIC
// is not held: a VIC fetch from a line not in yet reads stale data and
// fetches the line. Writes the model makes to its RAM go into DDR2 and into
// the cached line.
//
// Flash (ARCH-lean-state-access §7.4): the model's AMD responder, configured
// by the app (fl_*: part, unlock addresses, IDs), answers autoselect and
// status reads itself and stops at a PROGRAM / SECTOR ERASE / CHIP ERASE
// until it is acknowledged. The operation goes to the app as it is (evt_tgl,
// evt_word); the app applies it to the image in DDR2 with its own stores,
// then toggles inval_tgl (drop every cached line) and fl_ack_tgl. Nothing here
// writes the ROM image.
// EEPROM (GMod2): the 93C86 model's array is a 2 KB block RAM here, its
// second port the app's (Gideon's window 0x1004C000, trx_io.v); every
// change toggles ee_tgl.
//
// Towards DDR2 everything is one ordered queue of requests (line fetches and
// byte writes), handed over one at a time with a toggle and answered by one
// (cart_fill_c64u does them, in another clock). The cache's block RAM is written
// only from that side; a CPU read waits while requests are queued, so it
// never sees a line before an earlier write has reached it. Only the CPU waits
// (RDY, c64_top_c64u's ext_cart_wait); the VIC and the raster never do.
module cart_slot_c64u (
    input  wire        clk,
    // the app's cartridge registers (another clock: type is static around
    // reset, the toggles flip once per register write)
    input  wire  [7:0] cart_type,
    input  wire [343:0] desc,           // the app's descriptor (static around reset / force)
    input  wire        cart_kill_tgl,
    input  wire        cart_force_tgl,
    output wire        cart_active,

    // c64_top_c64u's cartridge port
    input  wire [15:0] addr,
    input  wire  [7:0] din,
    input  wire        we,
    input  wire        roml_n,
    input  wire        romh_n,
    input  wire        io1_n,
    input  wire        io2_n,
    input  wire        phi2_p,
    input  wire        phi2_h,
    input  wire        phi2_n,
    input  wire        reset,
    input  wire        freeze,          // the freeze button, held
    output wire        exrom,           // 1 = high (not asserted)
    output wire        game,
    output wire  [7:0] dout,
    output wire        nmi,
    output wire        irq,
    output wire        io2_we_valid,
    output wire        wait_cpu,

    // requests to DDR2: held stable from a toggle of req_tgl to the next ack_tgl
    output reg         req_tgl = 1'b0,
    output wire        req_we,          // 1: write a byte, 0: fetch a line
    output wire [23:0] req_addr,        // b22: cartridge RAM, else ROM image offset {b23, b21:0} (T51: 8 MiB)
    output wire  [7:0] req_data,
    output wire  [5:0] req_slot,        // way, line: where the line goes / the byte is cached
    output wire        req_cached,      // write: the line is (or will be) at req_slot
    input  wire        ack_tgl,
    // the cache's block RAM, written from cart_fill_c64u's clock
    input  wire        fill_clk,
    input  wire  [7:0] fill_be,
    input  wire  [8:0] fill_addr,       // slot, 8-byte word
    input  wire [63:0] fill_data,

    // flash responder configuration from the app (static while it is used)
    input  wire        fl_enable,
    input  wire [15:0] fl_magic1,
    input  wire [15:0] fl_magic2,
    input  wire [15:0] fl_mask,
    input  wire [23:0] fl_id,           // device ID address, device, manufacturer
    // the pending flash operation: a toggle per operation, the responder's
    // event word stable from it until the acknowledge ([31:30] 1 program,
    // 2 sector erase, 3 chip erase, [29] chip, [28:8] chip address, [7:0] data)
    output reg         evt_tgl = 1'b0,
    output wire [33:0] evt_word,
    input  wire        fl_ack_tgl,      // the app has applied it
    input  wire        inval_tgl,       // drop every cached line
    output reg         ee_tgl = 1'b0,   // a toggle per EEPROM change
    // the EEPROM's second port (the app's clock), read data one clock later
    input  wire        ee_clk,
    input  wire [10:0] ee_addr,
    input  wire        ee_we,
    input  wire  [7:0] ee_wdata,
    output reg   [7:0] ee_rdata,
    // the model's banking registers {reg_1, reg_0} (state bus, c64_core_c64u.v)
    output wire [15:0] st_bank,
    // its RAM writes as they go into the queue (write log, trx_engine.v)
    output wire        wtap,
    output wire [14:0] wtap_addr,
    output wire  [7:0] wtap_data,
    // a look without a bus cycle (the lens unit, T12 §11 Q2, T21 §3.4.1): what a
    // read of pk_addr as ROML (pk_cls 0), ROMH (1), IO1 (2) or IO2 (3) would
    // return now: nothing (pk_hit 0: open bus), a register value (pk_mem 0:
    // pk_data), or the image / cartridge RAM byte at pk_maddr (b22 RAM), which
    // the caller reads from DDR2. Combinational from the model's registers
    // (stable while the machine is paused); no state changes, no line fetch.
    input  wire [15:0] pk_addr,
    input  wire  [1:0] pk_cls,
    output wire        pk_hit,
    output wire        pk_mem,
    output wire [23:0] pk_maddr,
    output wire  [7:0] pk_data
);
    // ---- register toggles into this clock --------------------------------------
    (* ASYNC_REG = "TRUE" *) reg [2:0] kill_s = 3'b000, force_s = 3'b000, ack_s = 3'b000;
    (* ASYNC_REG = "TRUE" *) reg [2:0] flack_s = 3'b000, inval_s = 3'b000;
    always @(posedge clk) begin
        kill_s  <= {kill_s[1:0], cart_kill_tgl};
        force_s <= {force_s[1:0], cart_force_tgl};
        ack_s   <= {ack_s[1:0], ack_tgl};
        flack_s <= {flack_s[1:0], fl_ack_tgl};
        inval_s <= {inval_s[1:0], inval_tgl};
    end
    wire kill   = kill_s[2] != kill_s[1];
    wire force_ = force_s[2] != force_s[1];
    wire ack    = ack_s[2] != ack_s[1];
    wire fl_ack = flack_s[2] != flack_s[1];
    wire inval  = inval_s[2] != inval_s[1];

    reg cpu_half = 1'b0;                        // phi2 high: the CPU (or REU) has the bus
    always @(posedge clk)
        if (phi2_p) cpu_half <= 1'b1;
        else if (phi2_n) cpu_half <= 1'b0;

    // ---- the descriptor: a copy in this clock, taken with the cartridge ---------
    reg         reset_in = 1'b1;
    reg         cart_en = 1'b0;
    reg [343:0] d = 344'd0;
    wire        c_kill;                         // the model's mode table: switch off
    always @(posedge clk) begin
        reset_in <= reset;
        if (reset_in || force_) begin
            d       <= desc;
            cart_en <= reset_in && cart_type[4:0] != 5'd0 && desc[8*36];    // a forced type starts off
        end
        if (kill || (cart_en && c_kill))
            cart_en <= 1'b0;
    end

    wire ar = d[8*32 + 4];                      // the Action Replay block
    wire ee = d[8*32 + 3];                      // the 93C86 EEPROM on R0

    // ---- the model -------------------------------------------------------------
    wire [22:0] c_rom_addr;
    wire [14:0] c_ram_addr;
    wire        c_rom_sel, c_ram_sel, c_ram_we, c_ram_own;
    wire  [7:0] c_ram_wdata, c_dout;
    wire        c_exrom, c_game, c_nmi, c_irq, c_io2_we_valid, ee_written, c_io_serve;
    wire  [7:0] c_dmask;
    wire        fevt_pulse;
    wire  [7:0] cache_byte, ram_byte;
    wire        pk_io_sel, pk_rom_sel, pk_ram_sel;
    wire [22:0] pk_rom_addr;
    wire [14:0] pk_ram_addr;

    // the model arms a freeze whenever it sees the button: one clock per push
    reg frz_d = 1'b0;
    always @(posedge clk) frz_d <= freeze;
    wire frz_push = freeze & !frz_d;

    c64_cartridge model (
        .clk(clk), .reset(reset), .phi2_p(phi2_p), .phi2_h(phi2_h), .phi2_n(phi2_n),
        .sdram_pending(phi2_h),             // the EEPROM's array access starts in the CPU half
        .desc(d),
        .flash_enable(cart_en && fl_enable), .flash_magic1(fl_magic1), .flash_magic2(fl_magic2),
        .flash_mask(fl_mask), .flash_id_mfr(fl_id[7:0]), .flash_id_dev(fl_id[15:8]), .flash_id_ofs(fl_id[23:16]),
        .flash_evt_0(evt_word), .flash_evt_pulse(fevt_pulse), .eeprom_written(ee_written), .flash_ack(fl_ack),
        .addr(addr), .din(din), .dout(c_dout), .dout_mask(c_dmask), .io_serve(c_io_serve), .kill(c_kill),
        .we(we), .ba(1'b1), .exrom(c_exrom), .game(c_game), .freeze(frz_push),
        .roml(roml_n), .romh(romh_n), .io1_cen(io1_n), .io2_cen(io2_n),
        .nmi(c_nmi), .irq(c_irq), .io2_we_valid(c_io2_we_valid),
        .rom_addr(c_rom_addr), .rom_enable(), .rom_ready(1'b1), .rom_dout(cache_byte), .rom_select(c_rom_sel),
        .ram_addr(c_ram_addr), .ram_enable(), .ram_we(c_ram_we), .ram_wdata(c_ram_wdata),
        .ram_wdata_own(c_ram_own), .ram_ready(1'b1), .ram_dout(ram_byte), .ram_select(c_ram_sel),
        .st_bank(st_bank),
        .pk_addr(pk_addr), .pk_roml(pk_cls == 2'd0), .pk_romh(pk_cls == 2'd1), .pk_io1(pk_cls == 2'd2),
        .pk_io2(pk_cls == 2'd3), .pk_io_sel(pk_io_sel), .pk_io_dout(pk_data), .pk_rom_sel(pk_rom_sel),
        .pk_rom_addr(pk_rom_addr), .pk_ram_sel(pk_ram_sel), .pk_ram_addr(pk_ram_addr)
    );

    assign cart_active  = cart_en;
    assign exrom        = c_exrom || !cart_en;
    assign game         = c_game || !cart_en;
    assign nmi          = c_nmi && cart_en;
    assign irq          = c_irq && cart_en;
    assign io2_we_valid = c_io2_we_valid && cart_en;

    // the look: as below, for pk_addr (the model's rom_addr is the image
    // offset; it reads ROM before RAM, the Action Replay RAM before ROM)
    wire        pk_ram  = pk_ram_sel && (ar || !pk_rom_sel);
    assign pk_hit   = cart_en && (pk_io_sel || pk_rom_sel || pk_ram_sel);
    assign pk_mem   = !pk_io_sel;
    assign pk_maddr = pk_ram ? {9'b010000000, pk_ram_addr} : {pk_rom_addr[22], 1'b0, pk_rom_addr[21:0]};

    // what this bus access reads from the cartridge: the image (the model's
    // rom_addr is the offset in it) or RAM through the cache, or the model's
    // own answer (registers, the EEPROM's data bit); the rest is open bus
    wire mem_read = cart_en && !we && (c_rom_sel || c_ram_sel) && !c_ram_own;
    wire served   = mem_read || (cart_en && c_io_serve);
    wire [23:0] maddr = c_ram_sel ? {9'b010000000, c_ram_addr} : {c_rom_addr[22], 1'b0, c_rom_addr[21:0]};

    // writes into the model's RAM, once per CPU write cycle
    wire mem_write = phi2_h && we && cart_en && c_ram_sel && c_ram_we && !c_ram_own;

    // ---- the 93C86 EEPROM array (GMod2): the model's port, and the app's ---------------------------
    (* ram_style = "block" *) reg [7:0] eeprom [0:2047];
    reg  [7:0] ee_q;
    integer k;
    initial for (k = 0; k < 2048; k = k + 1) eeprom[k] = 8'hFF;
    always @(posedge clk) begin
        if (c_ram_own && c_ram_we) eeprom[c_ram_addr[10:0]] <= c_ram_wdata;
        ee_q <= eeprom[c_ram_addr[10:0]];
    end
    always @(posedge ee_clk) begin
        if (ee_we) eeprom[ee_addr] <= ee_wdata;
        ee_rdata <= eeprom[ee_addr];
    end
    assign ram_byte = (ee && io2_n) ? ee_q : cache_byte;
    always @(posedge clk) begin
        if (ee_written) ee_tgl <= ~ee_tgl;
        if (fevt_pulse) evt_tgl <= ~evt_tgl;
    end

    // ---- cache lookup ---------------------------------------------------------------
    wire  [4:0] set  = maddr[10:6];
    wire [12:0] tagv = maddr[23:11];
    reg  [12:0] tag0 [0:31];
    reg  [12:0] tag1 [0:31];
    reg  [31:0] used0 = 32'd0, used1 = 32'd0;   // the way holds tagv (or is fetching it)
    reg  [31:0] val0 = 32'd0, val1 = 32'd0;     // ... and the data is in
    reg  [31:0] lru = 32'd0;                    // the way to replace next
    integer i;
    initial for (i = 0; i < 32; i = i + 1) begin tag0[i] = 13'd0; tag1[i] = 13'd0; end

    wire m0      = used0[set] && tag0[set] == tagv;
    wire m1      = used1[set] && tag1[set] == tagv;
    wire hit_way = m1;

    // data visible two clocks after it is marked in (block RAM, output register)
    reg [31:0] rd0 = 32'd0, rd1 = 32'd0, rd0_d = 32'd0, rd1_d = 32'd0;
    wire ready = (m0 && rd0_d[set]) || (m1 && rd1_d[set]);

    (* ram_style = "block" *) reg [63:0] cache [0:511];
    reg  [63:0] c_q;
    integer b;
    always @(posedge fill_clk)
        for (b = 0; b < 8; b = b + 1)
            if (fill_be[b]) cache[fill_addr][8*b +: 8] <= fill_data[8*b +: 8];
    always @(posedge clk)
        c_q <= cache[{hit_way, set, maddr[5:3]}];

    // read data one clock after the address: the model's answer, with the
    // cached byte as its ROM / RAM data
    reg       q_served = 1'b0;
    reg [2:0] q_byte = 3'd0;
    reg [7:0] q_mask = 8'hFF;
    reg [7:0] last_bus = 8'hFF;
    always @(posedge clk) begin
        q_served <= served;
        q_byte   <= maddr[2:0];
        q_mask   <= c_dmask;
        last_bus <= din;                        // din is the bus: open-bus reads keep it
    end
    assign cache_byte = c_q[8*q_byte +: 8];
    assign dout = q_served ? (c_dout & q_mask) | (last_bus & ~q_mask) : last_bus;

    // ---- requests to DDR2: an ordered queue of 8 ----------------------------------------
    reg  [39:0] rq [0:7];                       // we, cached, slot, data, addr
    reg   [3:0] rq_head = 4'd0, rq_tail = 4'd0;
    reg         rq_busy = 1'b0;
    wire        rq_empty = rq_head == rq_tail;
    wire        rq_full  = rq_tail == {~rq_head[3], rq_head[2:0]};
    assign {req_we, req_cached, req_slot, req_data, req_addr} = rq[rq_head[2:0]];

    // the CPU waits for a line not in yet, and while requests are queued
    assign wait_cpu = cpu_half && mem_read && (!ready || !rq_empty);

    // fetch: one at a time, for a read (CPU or VIC) from a line not in the cache
    reg        fetching = 1'b0;
    reg        drop = 1'b0;                     // the line in flight is no longer wanted
    reg        f_way = 1'b0;
    reg  [4:0] f_set = 5'd0;
    wire       victim = !used0[set] ? 1'b0 : !used1[set] ? 1'b1 : lru[set];
    wire       wr_req = mem_write && !rq_full;
    assign wtap      = wr_req;
    assign wtap_addr = c_ram_addr;
    assign wtap_data = din;
    wire       flush  = reset || force_ || inval;
    wire       start  = mem_read && !(m0 || m1) && !fetching && !rq_full && !wr_req && !flush;

    always @(posedge clk) begin
        rd0   <= val0;
        rd1   <= val1;
        rd0_d <= rd0 & val0;
        rd1_d <= rd1 & val1;

        if (wr_req) begin
            rq[rq_tail[2:0]] <= {1'b1, m0 || m1, hit_way, set, din, maddr};
            rq_tail <= rq_tail + 1'b1;
        end else if (start) begin
            rq[rq_tail[2:0]] <= {1'b0, 1'b0, victim, set, 8'h00, maddr[23:6], 6'd0};
            rq_tail  <= rq_tail + 1'b1;
            fetching <= 1'b1;
            f_way    <= victim;
            f_set    <= set;
            if (victim) begin
                tag1[set] <= tagv; used1[set] <= 1'b1; val1[set] <= 1'b0;
            end else begin
                tag0[set] <= tagv; used0[set] <= 1'b1; val0[set] <= 1'b0;
            end
        end

        // hand the head over, take the answer
        if (!rq_busy && !rq_empty) begin
            req_tgl <= ~req_tgl;
            rq_busy <= 1'b1;
        end else if (rq_busy && ack) begin
            rq_busy <= 1'b0;
            rq_head <= rq_head + 1'b1;
            if (!req_we) begin                  // a fetch is in
                fetching <= 1'b0;
                drop     <= 1'b0;
                if (!drop) begin
                    if (f_way) val1[f_set] <= 1'b1;
                    else       val0[f_set] <= 1'b1;
                end
            end
        end

        // the way the CPU read from stays
        if (phi2_h && mem_read && (m0 || m1))
            lru[set] <= !hit_way;

        if (flush) begin
            used0 <= 32'd0; used1 <= 32'd0;
            val0  <= 32'd0; val1  <= 32'd0;
            rd0   <= 32'd0; rd1   <= 32'd0; rd0_d <= 32'd0; rd1_d <= 32'd0;
            if (fetching && !(rq_busy && ack && !req_we)) drop <= 1'b1;
        end
    end
endmodule
