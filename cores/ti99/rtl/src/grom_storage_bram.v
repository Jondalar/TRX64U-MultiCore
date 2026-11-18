// grom_storage_bram.v - drop-in replacement for Mega99's gateware/grom_storage_bram.v.
// Same ports and behaviour; the GROM storage is split into one 2K x 8 array per (page, GROM)
// instead of three arrays of 2048*NUM_GROMS bytes addressed by a 14-bit index. Vivado sized the
// original arrays by their 14-bit address (16K deep) whatever NUM_GROMS was; this version costs
// 3 * NUM_GROMS RAMB18 (NUM_GROMS = 6: 9 RAMB36 tiles; NUM_GROMS = 8: 12, as before).
// GROM ids >= NUM_GROMS read as 0 and ignore writes.
module grom_storage_bram #(parameter NUM_GROMS = 8)
			(input		  clk,

			 input [0:3]	  prefetch_grom_set,
			 input [0:15]	  prefetch_grom_addr,
			 input		  do_prefetch,
			 output		  prefetch_complete,
			 output [0:7]	  prefetch_data,

			 // GROM access wishbone slave
			 input [0:15]	  wb_adr_i,
			 input [0:7]	  wb_dat_i,
			 output reg [0:7] wb_dat_o,
			 input		  wb_we_i,
			 input [0:0]	  wb_sel_i,
			 input		  wb_stb_i,
			 output		  wb_ack_o,
			 input		  wb_cyc_i);

   wire [0:2]  grom_id = prefetch_grom_addr[0:2];
   wire [0:10] pf_off  = prefetch_grom_addr[5:15];
   wire [0:2]  wb_id   = wb_adr_i[0:2];
   wire [0:10] wb_off  = wb_adr_i[5:15];

   reg [0:2]  grom_page_active;
   reg	      wb_read_complete;
   reg [0:2]  sel_id;

   assign prefetch_complete = 1'b1;
   assign wb_ack_o = (wb_cyc_i && wb_stb_i && wb_we_i) || wb_read_complete;

   initial begin
      grom_page_active <= 3'b000;
      wb_read_complete <= 1'b0;
   end

   wire	       wr_en = wb_cyc_i && wb_stb_i && wb_we_i && wb_sel_i[0];
   wire	       rd_en = do_prefetch || (wb_cyc_i && wb_stb_i && !wb_we_i && !wb_read_complete);
   wire [0:2]  rd_id  = do_prefetch ? grom_id : wb_id;
   wire [0:10] rd_off = do_prefetch ? pf_off : wb_off;

   // the outputs of the arrays: byte of GROM g in page 0 / 1 / 2
   wire [0:8*NUM_GROMS-1] q0, q1, q2;

   genvar g;
   generate
      for (g = 0; g < NUM_GROMS; g = g + 1) begin : gr
	 reg [0:7] m0 [0:2047];
	 reg [0:7] m1 [0:2047];
	 reg [0:7] m2 [0:2047];
	 reg [0:7] r0, r1, r2;
	 wire	   we_g = wr_en && (wb_id == g);
	 wire	   rd_g = rd_en && (rd_id == g);
	 always @(posedge clk) begin
	    if (we_g && wb_adr_i[3:4] == 2'b00) m0[wb_off] <= wb_dat_i;
	    if (we_g && wb_adr_i[3:4] == 2'b01) m1[wb_off] <= wb_dat_i;
	    if (we_g && wb_adr_i[3:4] == 2'b10) m2[wb_off] <= wb_dat_i;
	    if (rd_g) begin
	       r0 <= m0[rd_off];
	       r1 <= m1[rd_off];
	       r2 <= m2[rd_off];
	    end
	 end
	 assign q0[8*g +: 8] = r0;
	 assign q1[8*g +: 8] = r1;
	 assign q2[8*g +: 8] = r2;
      end
   endgenerate

   always @(posedge clk)
     if (rd_en) sel_id <= rd_id;

   // the byte registers of the original: the array outputs of the GROM read last
   reg [0:7] grom_page0_byte, grom_page1_byte, grom_page2_byte;
   integer   i;
   always @(*) begin
      grom_page0_byte = 8'h00;
      grom_page1_byte = 8'h00;
      grom_page2_byte = 8'h00;
      for (i = 0; i < NUM_GROMS; i = i + 1)
	if (sel_id == i) begin
	   grom_page0_byte = q0[8*i +: 8];
	   grom_page1_byte = q1[8*i +: 8];
	   grom_page2_byte = q2[8*i +: 8];
	end
   end

   assign prefetch_data = (grom_page_active[0] ? grom_page0_byte : 8'h00) |
			  (grom_page_active[1] ? grom_page1_byte : 8'h00) |
			  (grom_page_active[2] ? grom_page2_byte : 8'h00);

   always @(*)
     case (wb_adr_i[3:4])
       2'b00: wb_dat_o = grom_page0_byte;
       2'b01: wb_dat_o = grom_page1_byte;
       2'b10: wb_dat_o = grom_page2_byte;
       2'b11: wb_dat_o = grom_page1_byte | grom_page2_byte;
       default: wb_dat_o = 8'h00;
     endcase

   always @(posedge clk) begin
      if (wb_read_complete)
	wb_read_complete <= 1'b0;
      else
	wb_read_complete <= (wb_cyc_i && wb_stb_i && !wb_we_i && !do_prefetch);

      if (do_prefetch) begin
	 if (grom_id < NUM_GROMS)
	   grom_page_active <= { prefetch_grom_addr[3:4] == 2'b00,
				 prefetch_grom_addr[4] == 1'b1,
				 prefetch_grom_addr[3] == 1'b1 };
	 else
	   grom_page_active <= 3'b000;
      end
   end
endmodule // grom_storage_bram
