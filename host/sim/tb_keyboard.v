// c64u_keyboard against a PCA9555 model at 0x21 with one key down (column 3,
// row 5): expects keys[29] = 1 and nothing else after a few scans.
`timescale 1ns/1ps
module tb;
    reg clk = 0; always #10 clk = ~clk;          // 50 MHz
    reg rst = 1;
    wire scl_low, sda_low; wire [63:0] keys;
    reg  slave_sda_low = 0;
    wire scl = ~scl_low;
    wire sda = ~(sda_low | slave_sda_low);
    c64u_keyboard dut (.clk(clk), .rst(rst), .sda_in(sda), .scl_low(scl_low), .sda_low(sda_low), .keys(keys), .pause(1'b0), .parked());

    // PCA9555 model
    reg [7:0] out0 = 8'hFF, cfg0 = 8'hFF, cfg1 = 8'hFF, ptr = 0;
    wire [7:0] in1 = out0[3] ? 8'hFF : 8'b1101_1111;    // col 3 low -> row 5 low
    reg [7:0] sh; integer nb = 0, bytes = 0; reg rd = 0, addressed = 0, sda_d = 1, scl_d = 1;
    reg [7:0] txb;
    always @(posedge clk) begin
        sda_d <= sda; scl_d <= scl;
        if (scl && sda_d && !sda) begin nb <= 0; bytes <= 0; addressed <= 0; rd <= 0; end   // (re)start
        else if (scl && !sda_d && sda) begin nb <= 0; end                                    // stop
        else if (scl && !scl_d) begin                                                        // SCL rises
            if (nb < 8) begin sh <= {sh[6:0], sda}; nb <= nb + 1; end
            else nb <= 0;                                                                    // ack slot
        end else if (!scl && scl_d) begin                                                    // SCL falls
            if (nb == 8 && !rd) begin                                                        // byte done: ack?
                if (bytes == 0) begin addressed <= (sh[7:1] == 7'h21); rd <= sh[0];
                                      slave_sda_low <= (sh[7:1] == 7'h21); txb <= in1; end
                else if (addressed) begin
                    slave_sda_low <= 1;
                    if (bytes == 1) ptr <= sh;
                    else begin
                        case (ptr) 8'h02: out0 <= sh; 8'h06: cfg0 <= sh; 8'h07: cfg1 <= sh; endcase
                        ptr <= ptr ^ 8'h01;           // pairs: 2<->3, 6<->7
                    end
                end
                bytes <= bytes + 1;
            end else if (rd && addressed && nb < 8) slave_sda_low <= ~txb[7 - nb];
            else slave_sda_low <= 0;
        end
    end

    // bus log: print every transfer to the codec (0x1A)
    reg [7:0] tr [0:7]; integer tn = 0, nb2 = 0; reg [7:0] sh2; reg sda_d2 = 1, scl_d2 = 1;
    integer codec_writes = 0;
    always @(posedge clk) begin
        sda_d2 <= sda; scl_d2 <= scl;
        if (scl && sda_d2 && !sda) begin tn = 0; nb2 = 0; end
        else if (scl && !sda_d2 && sda) begin
            if (tn == 3 && tr[0] == 8'h34) begin
                $display("codec R%02h = %03h", tr[1] >> 1, {tr[1][0], tr[2]});
                codec_writes = codec_writes + 1;
            end
            tn = 0;
        end else if (scl && !scl_d2) begin
            if (nb2 < 8) begin sh2 = {sh2[6:0], sda}; nb2 = nb2 + 1; end
            else begin if (tn < 8) tr[tn] = sh2; tn = tn + 1; nb2 = 0; end
        end
    end

    initial begin
        #1000 rst = 0;
        #200_000_000;
        $display("cfg0=%02x cfg1=%02x keys=%016x", cfg0, cfg1, keys);
        // the scan alternates the port directions, so the configs end either way
        if (keys == 64'd1 << 29 && codec_writes == 11) $display("PASS"); else $display("FAIL");
        $finish;
    end
endmodule
