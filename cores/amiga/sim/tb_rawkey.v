// T141: the raw key queue of the shared amiga_input (A500 and A1200) (rk_data / rk_tgl / rk_room): the application's Amiga key events {release, code}
// come out as type-2 events with the matrix keys' spacing; the C64 matrix and the queue work together, each key once.
`timescale 1ns/1ps
module tb_rawkey;
    reg sys_clk = 0;
    always #15.86 sys_clk = ~sys_clk;
    reg ce28 = 1; reg [3:0] n10 = 0;
    always @(posedge sys_clk) begin n10 <= (n10 == 9) ? 0 : n10 + 1; ce28 <= (n10 != 3); end
    reg sys_rst = 1;
    initial #1000 sys_rst = 0;
    reg [63:0] keys = 0; reg restore = 0;
    reg [7:0] rkd = 0; reg rkt = 0;
    wire room; wire kml; wire [1:0] kmt; wire [7:0] kmd; wire [2:0] mb; wire [7:0] j0, j1; wire rk;
    amiga_input dut (.sys_clk(sys_clk), .sys_rst(sys_rst), .ce28(ce28), .keys(keys), .restore(restore), .joy1(5'd0), .joy2(5'd0), .joy_swap(1'b0),
        .mouse_dx(8'd0), .mouse_dy(8'd0), .mouse_btn(3'd0), .mouse_tgl(1'b0), .rk_data(rkd), .rk_tgl(rkt), .rk_room(room),
        .kbd_mouse_level(kml), .kbd_mouse_type(kmt), .kbd_mouse_data(kmd), .mouse_buttons(mb), .joystick0(j0), .joystick1(j1), .reset_kbd(rk));
    reg [1:0] ev_t [0:127]; reg [7:0] ev_d [0:127]; time ev_ns [0:127]; integer nev = 0;
    always @(kml) if (!sys_rst) begin ev_t[nev] = kmt; ev_d[nev] = kmd; ev_ns[nev] = $time; nev = nev + 1; end
    integer errors = 0;
    task check(input cond, input [8*72-1:0] what);
        begin if (!cond) begin errors = errors + 1; $display("FAIL: %0s", what); end else $display("ok  : %0s", what); end
    endtask
    // the register block's write: data first, then the toggle (as amiga_regs does it, one cpu_clk edge), a few clocks apart
    task send(input [7:0] c);
        begin rkd = c; @(posedge sys_clk); rkt = ~rkt; repeat (8) @(posedge sys_clk); end
    endtask
    integer n0, i, gmin, gmax;
    initial begin
        #5000;
        check(room === 1'b1, "queue empty: room");
        n0 = nev; send(8'h45); #1500000;                                  // ESC down
        check(nev == n0 + 1 && ev_t[n0] == 2 && ev_d[n0] == 8'h45, "ESC press: raw code 45, one event");
        n0 = nev; send(8'hC5); #1500000;
        check(nev == n0 + 1 && ev_t[n0] == 2 && ev_d[n0] == 8'hC5, "ESC release: C5 (bit 7), one event");
        #3000000;
        check(nev == n0 + 1, "nothing more arrives by itself");
        // a burst of 8 events written back to back: all in order, spaced like matrix keys
        n0 = nev;
        send(8'h60); send(8'h50); send(8'h51); send(8'h5F); send(8'hDF); send(8'hD1); send(8'hD0); send(8'hE0);
        #12000000;
        check(nev == n0 + 8, "burst of 8: eight events");
        check(ev_d[n0] == 8'h60 && ev_d[n0+1] == 8'h50 && ev_d[n0+2] == 8'h51 && ev_d[n0+3] == 8'h5F &&
              ev_d[n0+4] == 8'hDF && ev_d[n0+5] == 8'hD1 && ev_d[n0+6] == 8'hD0 && ev_d[n0+7] == 8'hE0, "burst: order kept, each once");
        gmin = 32'h7fffffff; gmax = 0;
        for (i = 1; i < 8; i = i + 1) begin
            if (ev_ns[n0+i] - ev_ns[n0+i-1] < gmin) gmin = ev_ns[n0+i] - ev_ns[n0+i-1];
            if (ev_ns[n0+i] - ev_ns[n0+i-1] > gmax) gmax = ev_ns[n0+i] - ev_ns[n0+i-1];
        end
        check(gmin > 1000000 && gmax < 1500000, "burst: events 1.0 to 1.5 ms apart");
        // room: nine events in the queue take the room away, it comes back when the queue drains
        for (i = 0; i < 10; i = i + 1) send(8'h10 + i);
        check(room === 1'b0, "ten events queued: no room");
        #20000000;
        check(room === 1'b1, "queue drained: room again");
        // the C64 matrix and the queue at the same time: A (matrix, col 1 row 2 = 20) and ESC from the queue
        n0 = nev; keys[10] = 1; send(8'h45); #4000000;
        check(nev == n0 + 2, "matrix A + queue ESC together: two events");
        check((ev_d[n0] == 8'h20 && ev_d[n0+1] == 8'h45) || (ev_d[n0] == 8'h45 && ev_d[n0+1] == 8'h20), "matrix A (20) and queue ESC (45), each once");
        n0 = nev; keys[10] = 0; send(8'hC5); #4000000;
        check(nev == n0 + 2, "both released: two events");
        check((ev_d[n0] == 8'hA0 && ev_d[n0+1] == 8'hC5) || (ev_d[n0] == 8'hC5 && ev_d[n0+1] == 8'hA0), "releases A0 and C5, each once");
        // the hold (sys_rst) empties the queue
        n0 = nev; for (i = 0; i < 4; i = i + 1) send(8'h30 + i);
        sys_rst = 1; #200000; sys_rst = 0; #8000000;
        check(nev <= n0 + 1, "reset: the queue is emptied");
        $display("%0s tb_rawkey errors=%0d", errors == 0 ? "PASS" : "FAIL", errors);
        $finish;
    end
endmodule
