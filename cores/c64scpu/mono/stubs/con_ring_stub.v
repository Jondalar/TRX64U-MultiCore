// T113 §5.4 variant stub: the console ring (host/con_ring.v) removed. Same ports.
module con_ring (
    input  wire        wclk,
    input  wire        we,
    input  wire [7:0]  wdata,
    input  wire        rclk,
    input  wire [9:0]  raddr,
    output reg  [31:0] rdata,
    output reg  [31:0] count
);
    always @(posedge rclk) begin rdata <= 32'd0; count <= 32'd0; end
endmodule
