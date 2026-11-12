// DVI TMDS encoding, two symbols per clock (the GTP moves 20 bits per word).
// The running disparity carries from the first symbol to the second and on
// to the next clock.
module tmds_pair (
    input  wire        clk,
    input  wire        rst,
    input  wire [7:0]  d0, d1,      // pixel data, first and second symbol
    input  wire [1:0]  c0, c1,      // control bits during blanking
    input  wire        de0, de1,    // data enable
    output reg  [19:0] q            // {second, first}, first goes out first
);
    function [14:0] enc;            // {new_disparity[4:0], symbol[9:0]}
        input [7:0] d;
        input [1:0] c;
        input       de;
        input signed [4:0] disp;
        integer n1d, n1q, n0q, nd, i;
        reg   [8:0] qm;
        reg   [9:0] qo;
        begin
            n1d = 0;
            for (i = 0; i < 8; i = i + 1) n1d = n1d + (d[i] ? 1 : 0);
            qm[0] = d[0];
            if (n1d > 4 || (n1d == 4 && d[0] == 0)) begin
                for (i = 1; i < 8; i = i + 1) qm[i] = ~(qm[i-1] ^ d[i]);
                qm[8] = 1'b0;
            end else begin
                for (i = 1; i < 8; i = i + 1) qm[i] = qm[i-1] ^ d[i];
                qm[8] = 1'b1;
            end
            n1q = 0;
            for (i = 0; i < 8; i = i + 1) n1q = n1q + (qm[i] ? 1 : 0);
            n0q = 8 - n1q;
            if (!de) begin
                case (c)
                    2'b00: qo = 10'b1101010100;
                    2'b01: qo = 10'b0010101011;
                    2'b10: qo = 10'b0101010100;
                    default: qo = 10'b1010101011;
                endcase
                nd = 0;
            end else if (disp == 0 || n1q == n0q) begin
                qo = {~qm[8], qm[8], qm[8] ? qm[7:0] : ~qm[7:0]};
                nd = qm[8] ? disp + (n1q - n0q) : disp + (n0q - n1q);
            end else if ((disp > 0 && n1q > n0q) || (disp < 0 && n0q > n1q)) begin
                qo = {1'b1, qm[8], ~qm[7:0]};
                nd = disp + (qm[8] ? 2 : 0) + (n0q - n1q);
            end else begin
                qo = {1'b0, qm[8], qm[7:0]};
                nd = disp - (qm[8] ? 0 : 2) + (n1q - n0q);
            end
            enc = {nd[4:0], qo};
        end
    endfunction

    reg signed [4:0] disp;
    wire [14:0] e0 = enc(d0, c0, de0, disp);
    wire [14:0] e1 = enc(d1, c1, de1, e0[14:10]);
    always @(posedge clk) begin
        if (rst) begin
            disp <= 0;
            q    <= 20'd0;
        end else begin
            disp <= e1[14:10];
            q    <= {e1[9:0], e0[9:0]};
        end
    end
endmodule
