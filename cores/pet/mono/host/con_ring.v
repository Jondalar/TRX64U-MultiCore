// The last 4 KB written to the RISC-V console UART, readable from another
// clock (the JTAG debug port; no serial adapter is connected to the board).
//
// count is the number of bytes written since power-on (wraps at 2^32); byte
// n sits at ring offset n mod 4096. A reader keeps its own count and fetches
// what is new; more than 4 KB behind, the oldest part is gone.
// count crosses clocks in Gray code: it moves by one per byte, so a sample
// taken mid-change is either the old or the new value.
module con_ring (
    input  wire        wclk,
    input  wire        we,
    input  wire [7:0]  wdata,

    input  wire        rclk,
    input  wire [9:0]  raddr,           // 32-bit word: ring bytes 4*raddr .. +3
    output reg  [31:0] rdata,           // byte 4*raddr in bits 7:0
    output reg  [31:0] count
);
    reg [31:0] wptr = 32'd0;
    reg [31:0] wgray = 32'd0;
    (* ram_style = "block" *) reg [7:0] lane0 [0:1023];
    (* ram_style = "block" *) reg [7:0] lane1 [0:1023];
    (* ram_style = "block" *) reg [7:0] lane2 [0:1023];
    (* ram_style = "block" *) reg [7:0] lane3 [0:1023];
    always @(posedge wclk)
        if (we) begin
            case (wptr[1:0])
                2'd0: lane0[wptr[11:2]] <= wdata;
                2'd1: lane1[wptr[11:2]] <= wdata;
                2'd2: lane2[wptr[11:2]] <= wdata;
                2'd3: lane3[wptr[11:2]] <= wdata;
            endcase
            wptr  <= wptr + 32'd1;
            wgray <= (wptr + 32'd1) ^ ((wptr + 32'd1) >> 1);
        end

    (* ASYNC_REG = "TRUE" *) reg [31:0] gray_s1 = 32'd0, gray_s2 = 32'd0;
    integer i;
    reg [31:0] bin;
    always @(posedge rclk) begin
        rdata   <= {lane3[raddr], lane2[raddr], lane1[raddr], lane0[raddr]};
        gray_s1 <= wgray;
        gray_s2 <= gray_s1;
        bin[31] = gray_s2[31];
        for (i = 30; i >= 0; i = i - 1)
            bin[i] = bin[i + 1] ^ gray_s2[i];
        count <= bin;
    end
endmodule
