`timescale 1ns/1ps
module tb_bling;
    reg clk = 0, rx = 1;
    always #15.86 clk = ~clk;                 // 31.5278 MHz
    wire [63:0] keys; wire [7:0] last, other;
    bling_keyboard dut (.clk(clk), .v60(1'b0), .rx(rx), .keys(keys), .last(last), .other(other),
                        .rx_tgl(), .locked(), .lock_off(1'b0));
    task send(input [7:0] b); integer i; begin
        rx = 0; #4000;
        for (i = 0; i < 8; i = i + 1) begin rx = b[i]; #4000; end
        rx = 1; #20000;
    end endtask
    integer err = 0;
    initial begin
        #10000;
        send(8'h0A); if (keys !== (64'd1 << 10)) begin err = err + 1; $display("A down: %h", keys); end
        send(8'h3C); if (keys !== ((64'd1 << 10) | (64'd1 << 4))) begin err = err + 1; $display("space: %h", keys); end
        send(8'h8A); if (keys !== (64'd1 << 4)) begin err = err + 1; $display("A up: %h", keys); end
        send(8'h41); if (keys !== ((64'd1 << 4) | (64'd1 << 15))) begin err = err + 1; $display("lock: %h", keys); end
        send(8'h01); if (keys[57] !== 1'b1) begin err = err + 1; $display("return: %h", keys); end
        send(8'h40); if (dut.restore !== 1'b1) begin err = err + 1; $display("restore"); end
        send(8'h55); if (keys !== (64'd1 << 15)) begin err = err + 1; $display("all up: %h", keys); end
        send(8'h41); if (keys !== 64'd0) begin err = err + 1; $display("unlock: %h", keys); end
        send(8'h47); if (other !== 8'd1 || last !== 8'h47) begin err = err + 1; $display("other %h last %h", other, last); end
        if (err) $display("FAIL"); else $display("PASS"); $finish;
    end
endmodule
