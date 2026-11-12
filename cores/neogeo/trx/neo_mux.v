// T143: the memory multiplexer of the Neo Geo core, replacing sdram_mux.sv (upstream: SNK NeoGeo for MiSTer, Copyright (C) 2018 Sean 'Furrtek' Gonsalves, GPL v2 or
// later; the request conditions and the address formats below are those of rtl/mem/sdram_mux.sv, which is not built). Out: the Neo Geo CD paths, the SDRAM
// refresh and the ROM download. In: three request channels with a toggle handshake instead of the SDRAM controller's rd / ready pair, and the stall.
//
//   68k / system ROM   a request at the rising edge of ~&{nSROMOE, nROMOE, nPORTOE}; the word comes back, PROM_DATA_READY goes up and the 68k's DTACK follows
//                      (neogeo.sv: nDTACK_ADJ). The ready flag falls at the end of the 68k cycle (nAS rises).
//   fix graphics       a request when PCK2 falls, one 16 bit word (four pixels) of the S ROM
//   sprites            a request when PCK1 falls, 64 bits (the 16 pixels of one sprite line) of the C ROM
// A channel has one request open at a time: it is asked again only after its answer is in. The toggle handshake: the request flips *_req_tgl and holds
// the address; the memory service flips *_ack_tgl with the data stable. `ack_s` is the 3 flop synchroniser of the toggle.
// The stall: the sprite word is used a fixed time after PCK1 falls, the fix word after PCK2 falls (the machine's LOAD / S2H1 timing: C_DEADLINE / S_DEADLINE
// are the number of 48 MHz clocks of machine time that may pass between the request and the use; the register block holds them so that the bench can sweep them: cores/neogeo/sim/run.sh dl).
// The word becomes visible to the machine at a FIXED machine age, never at the moment it arrives: it waits in a stage register and is handed over when the machine clock count since
// the request reaches the deadline (+1, NEO_APPLY_OFS: the hand-over is a register, and a machine that stalls at the deadline stands two clocks later with the word already in).
// A word that is early and a word that was awaited therefore look the same to the machine; the visible picture does not depend on the memory's latency (bench: sim/run.sh dl, the
// same machine time with an ideal memory and the board's latency gives one picture, mut_arrival: handing the word over on arrival does not).
// When a word is not there at its deadline `stall` goes up and stays up until it is: the machine's 24 MHz enables stop (neo_core.v), nothing else changes.
`timescale 1ns / 1ps
`ifndef NEO_APPLY_OFS
`define NEO_APPLY_OFS 9'd1
`endif
module neo_mux (
	input  wire [7:0]  C_DEADLINE,
	input  wire [7:0]  S_DEADLINE,
	input  wire        CLK,
	input  wire        HELD,                       // the machine's enables are stopped this clock (neo_core stall_r: the stall or the freeze): machine time does not run
	input  wire        nRESET,
	input  wire        nSYSTEM_G,

	input  wire [20:1] M68K_ADDR,
	input  wire        nAS,
	input  wire        nROMOE,
	input  wire        nPORTOE,
	input  wire        nSROMOE,
	input  wire [26:0] P2ROM_ADDR,
	output reg  [15:0] PROM_DATA = 16'h0000,
	output reg         PROM_DATA_READY = 1'b0,

	input  wire        PCK1,
	input  wire [26:0] CROM_ADDR,
	input  wire [26:0] CROM_CAND,                  // T143 3b: the sprite address as the LSPC holds it long before PCK1 latches it (about 25 machine clocks)
	input  wire [15:0] S_CAND,                     // the same for the fix word (S_LATCH)
	output reg         ch_hint_tgl = 1'b0,         // hints for neo_mem.v: toggle + address, a new one at least 4 clocks after the one before
	output reg  [26:3] ch_hint_addr = 24'd0,
	output reg         sh_hint_tgl = 1'b0,
	output reg  [26:1] sh_hint_addr = 26'd0,
	output reg  [63:0] CR_DOUBLE = 64'd0,

	input  wire        PCK2,
	input  wire [15:0] S_LATCH,
	input  wire [1:0]  FIX_BANK,
	output reg  [15:0] SROM_DATA = 16'h0000,

	output reg         pm_req_tgl = 1'b0,
	output reg  [26:1] pm_addr = 26'd0,
	input  wire        pm_ack_tgl,
	input  wire [15:0] pm_data,
	output reg         s_req_tgl = 1'b0,
	output reg  [26:1] s_addr = 26'd0,
	input  wire        s_ack_tgl,
	input  wire [15:0] s_data,
	output reg         c_req_tgl = 1'b0,
	output reg  [26:3] c_addr = 24'd0,
	input  wire        c_ack_tgl,
	input  wire [63:0] c_data,

	output wire        c_pend,
	output wire        s_pend,
	output reg         stall = 1'b0,
	output reg  [7:0]  dbg_pck_age = 8'd0,
	output reg  [31:0] st_stall_clk = 32'd0,       // 48 MHz clocks the machine was held
	output reg  [31:0] st_c_late = 32'd0,          // sprite words that were late (a stall began)
	output reg  [31:0] st_s_late = 32'd0,          // fix words that were late
	output reg  [31:0] st_p_reads = 32'd0,         // 68k reads asked
	output reg  [7:0]  st_c_max = 8'd0,            // longest wait for a sprite word, in 48 MHz clocks of machine time, saturating
	output reg  [7:0]  st_s_max = 8'd0
);
	localparam [26:1] P2ROM_OFFSET_W = 26'h0180000;     // 27'h0300000 >> 1

	(* ASYNC_REG = "TRUE" *) reg [2:0] pm_ack_s = 3'b000, s_ack_s = 3'b000, c_ack_s = 3'b000;
	reg pm_seen = 1'b0, s_seen = 1'b0, c_seen = 1'b0;     // the ack value the last answer carried
	wire pm_pend = (pm_req_tgl != pm_seen);
	assign s_pend = (s_req_tgl != s_seen);
	assign c_pend = (c_req_tgl != c_seen);

	// ---- hints: the address the machine will ask for, sent as soon as it has been stable for HINT_STABLE clocks and differs from the line of the last hint. A hint is a guess
	// that neo_mem.v verifies by looking up the real request: wrong or late hints cost a DDR2 read, nothing else.
`ifndef NEO_HINT_STABLE
`define NEO_HINT_STABLE 4'd3
`endif
	reg [26:3] cc_q = 24'd0, cc_last = 24'd0;
	reg [3:0]  cc_st = 4'd0;
	reg        cc_lv = 1'b0;
	reg [3:0]  ch_gap = 4'd0;
	reg [14:0] sc_q = 15'd0, sc_last = 15'd0;
	reg [3:0]  sc_st = 4'd0, sh_gap = 4'd0;
	reg        sc_lv = 1'b0;
	wire [14:0] sc_now = {S_CAND[15:4], S_CAND[2:0]};          // S_LATCH without bit 3, the pixel pair (it flips right before the latch and picks the word inside the 16 byte line)
	function [26:1] s_word(input [15:0] sl, input [1:0] bank, input sysg);
		s_word = sysg ? {6'b0_0000_1, bank, sl[15:4], sl[2:0], ~sl[3]} : {8'b0_0000_001, sl[15:4], sl[2:0], ~sl[3]};
	endfunction
	always @(posedge CLK) begin
		cc_q <= CROM_CAND[26:3];
		sc_q <= sc_now;
		if (CROM_CAND[26:3] != cc_q) cc_st <= 4'd0; else if (cc_st != 4'hF) cc_st <= cc_st + 1'd1;
		if (sc_now != sc_q) sc_st <= 4'd0; else if (sc_st != 4'hF) sc_st <= sc_st + 1'd1;
		if (ch_gap != 4'hF) ch_gap <= ch_gap + 1'd1;
		if (sh_gap != 4'hF) sh_gap <= sh_gap + 1'd1;
		if (cc_st >= `NEO_HINT_STABLE && !(cc_lv && CROM_CAND[26:4] == cc_last[26:4]) && ch_gap >= 4'd4) begin
			ch_hint_addr <= CROM_CAND[26:3]; ch_hint_tgl <= ~ch_hint_tgl; cc_last <= CROM_CAND[26:3]; cc_lv <= 1'b1; ch_gap <= 4'd0;
		end
		if (sc_st >= `NEO_HINT_STABLE && !(sc_lv && sc_now == sc_last) && sh_gap >= 4'd4) begin
			sh_hint_addr <= s_word({sc_now[14:3], 1'b0, sc_now[2:0]}, FIX_BANK, nSYSTEM_G); sh_hint_tgl <= ~sh_hint_tgl; sc_last <= sc_now; sc_lv <= 1'b1; sh_gap <= 4'd0;
		end
	end

	reg sig_q = 1'b0, nAS_q = 1'b1, pck1_q = 1'b1, pck2_q = 1'b1;
	reg m_req = 1'b0;                  // a 68k access waits for its channel
	reg m_live = 1'b0;                 // the open request belongs to the cycle that is still running
	wire sig = ~&{nSROMOE, nROMOE, nPORTOE};

	reg [7:0] c_age = 8'd0, s_age = 8'd0;               // machine clocks since the request, counting until the word is handed over
	reg [63:0] c_stage = 64'd0;
	reg [15:0] s_stage = 16'd0;
	reg c_have = 1'b0, s_have = 1'b0;                   // the word is in the stage register and not yet handed over
	wire c_open = c_pend | c_have;
	wire s_open = s_pend | s_have;
	wire c_dl = c_pend && (c_age >= C_DEADLINE);
	wire s_dl = s_pend && (s_age >= S_DEADLINE);
	wire c_apply = c_have && ((c_age == 8'hFF) || ({1'b0, c_age} >= {1'b0, C_DEADLINE} + `NEO_APPLY_OFS));
	wire s_apply = s_have && ((s_age == 8'hFF) || ({1'b0, s_age} >= {1'b0, S_DEADLINE} + `NEO_APPLY_OFS));

	always @(posedge CLK) begin
		pm_ack_s <= {pm_ack_s[1:0], pm_ack_tgl};
		s_ack_s <= {s_ack_s[1:0], s_ack_tgl};
		c_ack_s <= {c_ack_s[1:0], c_ack_tgl};
		sig_q <= sig;
		nAS_q <= nAS;
		pck1_q <= PCK1;
		pck2_q <= PCK2;
		stall <= c_dl | s_dl;
		if (stall) st_stall_clk <= st_stall_clk + 1'd1;

		// ---- machine time since the request (frozen while the machine is held)
		if (!HELD) begin
			if (c_open && c_age != 8'hFF) c_age <= c_age + 1'd1;
			if (s_open && s_age != 8'hFF) s_age <= s_age + 1'd1;
		end
		if (c_dl && !stall) st_c_late <= st_c_late + 1'd1;
		if (s_dl && !stall) st_s_late <= st_s_late + 1'd1;

		// ---- 68k program / system ROM
		if (~nAS_q & nAS) begin PROM_DATA_READY <= 1'b0; m_live <= 1'b0; end
		if (~sig_q & sig) m_req <= 1'b1;
		if (!nRESET) begin
			m_req <= 1'b0; m_live <= 1'b0; PROM_DATA_READY <= 1'b0;
		end else if ((m_req | (~sig_q & sig)) && !pm_pend) begin
			m_req <= 1'b0;
			m_live <= 1'b1;
			pm_req_tgl <= ~pm_req_tgl;
			st_p_reads <= st_p_reads + 1'd1;
			casez ({~nROMOE, ~nPORTOE})
				2'b1?:   pm_addr <= {5'b0_0010, M68K_ADDR[19:1]};                     // P1 ROM $0200000~$02FFFFF
				2'b01:   pm_addr <= P2ROM_OFFSET_W + P2ROM_ADDR[26:1];                // P2 ROM (cart) $0300000~... bankswitched
				default: pm_addr <= {8'b0_0000_000, M68K_ADDR[16:1]};                 // System ROM (cart) $0000000~$001FFFF
			endcase
		end
		if (pm_ack_s[2] ^ pm_ack_s[1]) begin
			pm_seen <= pm_ack_s[1];
			if (m_live && !nAS) begin PROM_DATA <= pm_data; PROM_DATA_READY <= 1'b1; end
		end

		// ---- fix graphics: a request when PCK2 falls
		if (pck2_q & ~PCK2 && !s_pend) begin
			s_req_tgl <= ~s_req_tgl;
			s_age <= 8'd0;
			// S ROM (cart) $0080000~$00FFFFF, the system's SFIX ROM $0020000~$003FFFF
			s_addr <= nSYSTEM_G ? {6'b0_0000_1, FIX_BANK, S_LATCH[15:4], S_LATCH[2:0], ~S_LATCH[3]} :
			                      {8'b0_0000_001, S_LATCH[15:4], S_LATCH[2:0], ~S_LATCH[3]};
		end
		if (s_ack_s[2] ^ s_ack_s[1]) begin
			s_seen <= s_ack_s[1];
			s_stage <= s_data;
			s_have <= 1'b1;
			if (s_age > st_s_max) st_s_max <= s_age;
		end
		if (s_apply) begin SROM_DATA <= s_stage; s_have <= 1'b0; end

		// ---- sprites: a request when PCK1 falls
		if (pck1_q & ~PCK1 && !c_pend) begin
			c_req_tgl <= ~c_req_tgl;
			c_age <= 8'd0;
			c_addr <= CROM_ADDR[26:3];
		end
		if (c_ack_s[2] ^ c_ack_s[1]) begin
			c_seen <= c_ack_s[1];
			c_stage <= {c_data[15:0], c_data[31:16], c_data[47:32], c_data[63:48]};    // word 0 on top, as sdram_mux assembled it
			c_have <= 1'b1;
			dbg_pck_age <= c_age;
			if (c_age > st_c_max) st_c_max <= c_age;
		end
		if (c_apply) begin CR_DOUBLE <= c_stage; c_have <= 1'b0; end
	end
endmodule
