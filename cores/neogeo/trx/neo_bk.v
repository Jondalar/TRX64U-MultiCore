// T143 D4c: the MVS backup RAM (64 KB, battery SRAM at $D00000) in the DDR2 instead of 16 block RAM tiles. Written for this project (TREX), no upstream code; the
// access protocol is that of rtl/mem/backup.v (neogeo.sv: nSRAMOEL / nSRAMOEU read strobes, nBWL / nBWU write strobes of the byte lanes, word address M68K_ADDR[15:1];
// the low lane SRAML holds M68K_DATA[7:0]).
//   read    a cache of 8 lines of 16 bytes (8 words, direct mapped, in distributed RAM): a hit answers at once, as the block RAM did; a miss fetches the line
//           from the DDR2 and the 68k waits (bk_wait: neo_core.v adds it to nDTACK).
//   write   write through: the cache line is updated when it is there, and one write goes to the DDR2 with the byte lanes the 68k wrote. A write waits (bk_wait)
//           while a write or a fill is still on its way: one operation is open at a time, so a fill can never overtake a write and fetch the old word.
// The application reads and writes the DDR2 image (never the cache): it loads a saved image before the machine runs (reset_in clears the cache), and after
// neo_core's sv_dirty it reads the image back, which is current because every write was written through.
`timescale 1ns / 1ps
module neo_bk (
    input  wire         CLK,
    input  wire         clr,                // reset_in: the cache forgets
    input  wire         en,                 // MVS: the backup RAM exists
    input  wire [15:1]  M68K_ADDR,
    input  wire [15:0]  M68K_DATA,
    input  wire         nSRAMOEL,
    input  wire         nSRAMOEU,
    input  wire         nBWL,
    input  wire         nBWU,
    output wire [15:0]  SRAM_OUT,
    output wire         bk_wait,
    // towards neo_mem (toggle handshake, 48 MHz domain)
    output reg          req_tgl = 1'b0,
    output reg          req_wr = 1'b0,
    output reg  [15:1]  req_addr = 15'd0,
    output reg  [15:0]  req_wdata = 16'd0,
    output reg  [1:0]   req_be = 2'd0,
    input  wire         ack_tgl,
    input  wire [127:0] ack_line
);
    // the cache: 8 lines x 8 words, as two byte lanes of 64 words in distributed RAM (word index {line, word})
    reg [7:0]   dl [0:63];
    reg [7:0]   du [0:63];
    reg [8:0]   ltag [0:7];
    reg [7:0]   lval = 8'd0;
    (* ASYNC_REG = "TRUE" *) reg [2:0] ack_s = 3'b000;
    reg         seen = 1'b0;
    wire        busy = (req_tgl != seen);
    wire [2:0]  idx = M68K_ADDR[6:4];          // 16 byte line number A[15:4]: its low three bits index the cache
    wire [8:0]  tag = M68K_ADDR[15:7];
    wire [5:0]  widx = {idx, M68K_ADDR[3:1]};
    wire        hit = lval[idx] && (ltag[idx] == tag);
    wire        rd_act = en && (~nSRAMOEL | ~nSRAMOEU);
    wire        wr_act = en && (~nBWL | ~nBWU);
    assign SRAM_OUT = hit ? {du[widx], dl[widx]} : 16'h0000;
    reg         wr_taken = 1'b0;
    reg [2:0]   fidx = 3'd0;
    reg [8:0]   ftag = 9'd0;
    reg         fill = 1'b0;
    reg [3:0]   fk = 4'd8;                     // words of the fetched line written so far; 8 = done
    reg [127:0] fline = 128'd0;
    // a write is taken into the open slot once per 68k cycle; until then, and while a fill is open, the 68k waits
    assign bk_wait = (rd_act && !hit) || (wr_act && !wr_taken);

    wire        fwr = (fk < 4'd8);
    wire [5:0]  faddr = {fidx, fk[2:0]};
    wire [15:0] fword = fline[16 * fk[2:0] +: 16];
    always @(posedge CLK) begin
        ack_s <= {ack_s[1:0], ack_tgl};
        if (!wr_act) wr_taken <= 1'b0;
        if (clr) lval <= 8'd0;
        // the answer of the open operation: a fill is copied into the lines word by word (8 clocks), the line is valid after it
        if (ack_s[2] ^ ack_s[1]) begin
            seen <= ack_s[1];
            if (fill) begin
                fline <= ack_line; ltag[fidx] <= ftag; fk <= 4'd0; fill <= 1'b0;
            end
        end
        if (fwr) begin
            dl[faddr] <= fword[7:0];
            du[faddr] <= fword[15:8];
            fk <= fk + 4'd1;
            if (fk == 4'd7) lval[fidx] <= 1'b1;
        end else if (wr_act && hit) begin
            // a write that hits keeps the line current (the strobe may last several clocks: the same bytes again)
            if (!nBWL) dl[widx] <= M68K_DATA[7:0];
            if (!nBWU) du[widx] <= M68K_DATA[15:8];
        end
        if (!busy && !clr && fk == 4'd8) begin
            if (wr_act && !wr_taken) begin
                req_wr <= 1'b1; req_addr <= M68K_ADDR[15:1]; req_wdata <= M68K_DATA; req_be <= {~nBWU, ~nBWL};
                req_tgl <= ~req_tgl; wr_taken <= 1'b1;
            end else if (rd_act && !hit && !wr_act) begin
                req_wr <= 1'b0; req_addr <= {M68K_ADDR[15:4], 3'd0}; fidx <= idx; ftag <= tag; fill <= 1'b1;
                lval[idx] <= 1'b0;
                req_tgl <= ~req_tgl;
            end
        end
    end
endmodule
