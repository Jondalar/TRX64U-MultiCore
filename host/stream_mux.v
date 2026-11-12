// Two UDP stream senders (video, audio) on the one stream port of the RMII
// arbiter in top.v: whole frames in turn, alternating when both wait. The
// choice is made before req goes up and kept until the chosen frame's eof
// byte is taken (the arbiter frees grant on that same edge).
module stream_mux (
    input  wire       clk,
    input  wire       rst,
    input  wire       a_req, b_req,
    output wire       a_grant, b_grant,
    input  wire [7:0] a_data, b_data,
    input  wire       a_valid, b_valid,
    input  wire       a_eof, b_eof,
    output wire       a_ready, b_ready,
    output wire       req,
    input  wire       grant,
    output wire [7:0] data,
    output wire       valid,
    output wire       eof,
    input  wire       ready
);
    reg act = 1'b0, sel = 1'b0, last_b = 1'b0;     // sel: 0 a, 1 b
    always @(posedge clk)
        if (rst) begin
            act <= 1'b0; last_b <= 1'b0;
        end else if (!act) begin
            if (a_req && (!b_req || last_b)) begin act <= 1'b1; sel <= 1'b0; end
            else if (b_req)                  begin act <= 1'b1; sel <= 1'b1; end
        end else if (grant && valid && eof && ready) begin
            act <= 1'b0; last_b <= sel;
        end
    assign req     = act;
    assign a_grant = act & ~sel & grant;
    assign b_grant = act &  sel & grant;
    assign data    = sel ? b_data  : a_data;
    assign eof     = sel ? b_eof   : a_eof;
    assign valid   = act & (sel ? b_valid : a_valid);
    assign a_ready = act & ~sel & ready;
    assign b_ready = act &  sel & ready;
endmodule
