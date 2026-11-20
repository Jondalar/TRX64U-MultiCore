`timescale 1ns/1ps
// bench: ti99_keys. Each case presses C64 keys (matrix index col*8+row) and lists the TI keys that must be down (index col*8+row of the TI matrix); all others must be up.
module tb_keys;
  reg [63:0] k = 0; reg rs = 0; wire [0:47] ks; wire [0:4] t1, t2;
  ti99_keys d(.keys(k), .restore(rs), .joy1(5'd0), .joy2(5'd0), .key_state(ks), .tj1(t1), .tj2(t2));
  integer e = 0, n;
  localparam LS = 15, RS = 52;
  localparam F=4, S=5, C=6;                 // FCTN, SHIFT, CTRL
  localparam K1=44,K2=12,K3=20,K4=28,K5=36,K6=35,K7=27,K8=19,K9=11,K0=43, EQ=0, SL=40, SE=41, PP=42, OO=10, II=18, RR=30, TT=38, SS=13, DD=21, EE=22, XX=15, CM=16, PD=8;
  task chk(input [127:0] name, input [47:0] want); begin
    #1; for (n = 0; n < 48; n = n + 1) if (ks[n] !== want[n]) begin e = e + 1; $display("FAIL %0s: TI key %0d is %b want %b", name, n, ks[n], want[n]); end
    k = 0; rs = 0; end endtask
  function [47:0] b(input integer a0, input integer a1, input integer a2); begin b = 0; if (a0 >= 0) b[a0] = 1; if (a1 >= 0) b[a1] = 1; if (a2 >= 0) b[a2] = 1; end endfunction
  initial begin
    k[56] = 1; chk("1", b(K1,-1,-1));
    k[40] = 1; chk("+", b(EQ,S,-1));
    k[53] = 1; chk("=", b(EQ,-1,-1));
    k[43] = 1; chk("-", b(SL,S,-1));
    k[49] = 1; chk("*", b(K8,S,-1));
    k[46] = 1; chk("@", b(K2,S,-1));
    k[54] = 1; chk("^", b(K6,S,-1));
    k[45] = 1; chk(":", b(SE,S,-1));
    k[50] = 1; chk(";", b(SE,-1,-1));
    k[LS] = 1; k[59] = 1; chk("shift2 = \"", b(F,PP,-1));
    k[LS] = 1; k[19] = 1; chk("shift6 = &", b(K7,S,-1));
    k[RS] = 1; k[24] = 1; chk("shift7 = '", b(F,OO,-1));
    k[LS] = 1; k[27] = 1; chk("shift8 = (", b(K9,S,-1));
    k[LS] = 1; k[32] = 1; chk("shift9 = )", b(K0,S,-1));
    k[LS] = 1; k[56] = 1; chk("shift1 = !", b(K1,S,-1));
    k[LS] = 1; k[8] = 1;  chk("shift3 = #", b(K3,S,-1));
    k[LS] = 1; k[55] = 1; chk("shift/ = ?", b(F,II,-1));
    k[LS] = 1; k[45] = 1; chk("shift: = [", b(F,RR,-1));
    k[LS] = 1; k[50] = 1; chk("shift; = ]", b(F,TT,-1));
    k[LS] = 1; k[47] = 1; chk("shift, = <", b(CM,S,-1));
    k[LS] = 1; k[53] = 1; chk("shift= = +", b(EQ,S,-1));
    k[61] = 1; k[41] = 1; chk("C= P = \"", b(F,PP,-1));
    k[0] = 1; chk("DEL", b(F,K1,-1));
    k[LS] = 1; k[0] = 1; chk("shift DEL = INS", b(F,K2,-1));
    k[57] = 1; chk("<- = QUIT", b(F,EQ,-1));
    k[2] = 1; chk("crsr right", b(F,DD,-1));
    k[LS] = 1; k[7] = 1; chk("shift crsr down = up", b(F,EE,-1));
    rs = 1; chk("restore", b(F,K9,-1));
    k[10] = 1; k[LS] = 1; chk("shift A", b(45,S,-1));
    if (e == 0) $display("PASS ti99_keys"); else $display("FAILED %0d", e);
    $finish;
  end
endmodule
