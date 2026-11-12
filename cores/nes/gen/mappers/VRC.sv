
// VRC2 and VRC4
module VRC24(
	input ce_in,
	input        clk,         // System clock
	input        ce,          // M2 ~cpu_clk
	input        enable,      // Mapper enabled
	input [31:0] flags,       // Cart flags
	input [15:0] prg_ain,     // prg address
	output [21:0] prg_aout_b,  // prg address out
	input        prg_read,    // prg read
	input        prg_write,   // prg write
	input  [7:0] prg_din,     // prg data in
	output  [7:0] prg_dout_b,  // prg data out
	output        prg_allow_b, // Enable access to memory for the specified operation.
	input [13:0] chr_ain,     // chr address in
	output [21:0] chr_aout_b,  // chr address out
	input        chr_read,    // chr ram read
	output        chr_allow_b, // chr allow write
	output        vram_a10_b,  // Value for A10 address line
	output        vram_ce_b,   // True if the address should be routed to the internal 2kB VRAM.
	output        irq_b,       // IRQ
	input [15:0] audio_in,    // Inverted audio from APU
	output [15:0] audio_b,     // Mixed audio output
	output [15:0] flags_out_b, // flags {0, 0, 0, 0, has_savestate, prg_conflict, prg_bus_write, has_chr_dout}
	// savestates              
	input       [63:0]  SaveStateBus_Din,
	input       [ 9:0]  SaveStateBus_Adr,
	input               SaveStateBus_wren,
	input               SaveStateBus_rst,
	input               SaveStateBus_load,
	output      [63:0]  SaveStateBus_Dout
);
wire prg_allow;  // T95: hoisted above its first use (implicit net)
wire chr_allow;  // T95: hoisted above its first use (implicit net)
logic vram_a10;  // T95: hoisted above its first use (implicit net)
wire vram_ce;  // T95: hoisted above its first use (implicit net)
wire irq;  // T95: hoisted above its first use (implicit net)
reg [15:0] flags_out = {12'h0, 1'b1, 3'b0};  // T95: hoisted above its first use (implicit net)


assign prg_aout_b   = enable ? prg_aout : 22'd0;
assign prg_dout_b   = enable ? 8'hFF : {8{1'b1}};
assign prg_allow_b  = enable ? prg_allow : 1'd0;
assign chr_aout_b   = enable ? chr_aout : 22'd0;
assign chr_allow_b  = enable ? chr_allow : 1'd0;
assign vram_a10_b   = enable ? vram_a10 : 1'd0;
assign vram_ce_b    = enable ? vram_ce : 1'd0;
assign irq_b        = enable ? irq : 1'd0;
assign flags_out_b  = enable ? flags_out : 16'd0;
assign audio_b      = enable ? {1'b0, audio_in[15:1]} : 16'd0;

wire [21:0] prg_aout, chr_aout;


reg [4:0] prg_bank0, prg_bank1;
reg [8:0] chr_bank0, chr_bank1, chr_bank2, chr_bank3, chr_bank4, chr_bank5, chr_bank6, chr_bank7;
reg [1:0] mirroring;
reg [4:0] prg_tmp;
reg [8:0] chr_tmp;
reg prg_invert;
wire mapper21 = (flags[7:0] == 21);
wire mapper22 = (flags[7:0] == 22);
wire mapper23 = (flags[7:0] == 23 | flags[7:0] == 27);
//wire mapper25 = (flags[7:0] == 25); //default
wire mapperVRC4 = (flags[7:0] != 22) && (flags[24:21] != 3);
wire [1:0] registers = {mapper21 ?  {(prg_ain[7]|prg_ain[2]),(prg_ain[6]|prg_ain[1])} :
                        mapper22 ?  {(prg_ain[0]),           (prg_ain[1])           } :
                        mapper23 ?  {(prg_ain[3]|prg_ain[1]),(prg_ain[2]|prg_ain[0])} :
                        /*mapper25*/{(prg_ain[2]|prg_ain[0]),(prg_ain[3]|prg_ain[1])}};

always @(posedge clk) if (ce_in)
	if (~enable) begin
		// Set value for mirroring
		mirroring[1:0] <= {1'b0, !flags[14]};
		prg_invert <= 0;
		prg_bank0 <= 5'd0;
		prg_bank1 <= 5'd1;
		chr_bank0 <= 9'd0;
		chr_bank1 <= 9'd1;
		chr_bank2 <= 9'd2;
		chr_bank3 <= 9'd3;
		chr_bank4 <= 9'd4;
		chr_bank5 <= 9'd5;
		chr_bank6 <= 9'd6;
		chr_bank7 <= 9'd7;
	end else if (SaveStateBus_load) begin
		prg_bank0  <= SS_MAP1[ 4: 0];
		prg_bank1  <= SS_MAP1[ 9: 5];
		chr_bank0  <= SS_MAP1[18:10];
		chr_bank1  <= SS_MAP1[27:19];
		chr_bank2  <= SS_MAP1[36:28];
		chr_bank3  <= SS_MAP1[45:37];
		chr_bank4  <= SS_MAP1[54:46];
		chr_bank5  <= SS_MAP1[63:55];
		chr_bank6  <= SS_MAP2[ 8: 0];
		chr_bank7  <= SS_MAP2[17: 9];
		mirroring  <= SS_MAP2[19:18];
		prg_invert <= SS_MAP2[   20];
	end else if (ce) begin
		if (prg_ain[15] & prg_write) begin
			casez ({prg_ain[14:12], registers, mapperVRC4})
				6'b000_??_?:  prg_bank0      <= prg_din[4:0];  // PRG bank 0x8000-0x9FFF or 0xC000-0xDFFF
				6'b001_??_0:  mirroring[0]   <= prg_din[0];
				6'b001_0?_1:  mirroring      <= prg_din[1:0];
				6'b001_1?_1:  prg_invert     <= prg_din[1];
				6'b010_??_?:  prg_bank1      <= prg_din[4:0];  // PRG bank 0xA000-0xBFFF
				6'b011_00_?:  chr_bank0[3:0] <= prg_din[3:0];  // CHR bank 0x0000-0x03FF
				6'b011_01_?:  chr_bank0[8:4] <= prg_din[4:0];  // CHR bank 0x0000-0x03FF
				6'b011_10_?:  chr_bank1[3:0] <= prg_din[3:0];  // CHR bank 0x0400-0x07FF
				6'b011_11_?:  chr_bank1[8:4] <= prg_din[4:0];  // CHR bank 0x0400-0x07FF
				6'b100_00_?:  chr_bank2[3:0] <= prg_din[3:0];  // CHR bank 0x0800-0x0BFF
				6'b100_01_?:  chr_bank2[8:4] <= prg_din[4:0];  // CHR bank 0x0800-0x0BFF
				6'b100_10_?:  chr_bank3[3:0] <= prg_din[3:0];  // CHR bank 0x0C00-0x0FFF
				6'b100_11_?:  chr_bank3[8:4] <= prg_din[4:0];  // CHR bank 0x0C00-0x0FFF
				6'b101_00_?:  chr_bank4[3:0] <= prg_din[3:0];  // CHR bank 0x1000-0x13FF
				6'b101_01_?:  chr_bank4[8:4] <= prg_din[4:0];  // CHR bank 0x1000-0x13FF
				6'b101_10_?:  chr_bank5[3:0] <= prg_din[3:0];  // CHR bank 0x1400-0x17FF
				6'b101_11_?:  chr_bank5[8:4] <= prg_din[4:0];  // CHR bank 0x1400-0x17FF
				6'b110_00_?:  chr_bank6[3:0] <= prg_din[3:0];  // CHR bank 0x1800-0x1BFF
				6'b110_01_?:  chr_bank6[8:4] <= prg_din[4:0];  // CHR bank 0x1800-0x1BFF
				6'b110_10_?:  chr_bank7[3:0] <= prg_din[3:0];  // CHR bank 0x1C00-0x1FFF
				6'b110_11_?:  chr_bank7[8:4] <= prg_din[4:0];  // CHR bank 0x1C00-0x1FFF
				//6'b111_??_1:  IRQ Stuff;  // IRQ
			endcase
		end
	end
	
assign SS_MAP1_BACK[ 4: 0] = prg_bank0;
assign SS_MAP1_BACK[ 9: 5] = prg_bank1;
assign SS_MAP1_BACK[18:10] = chr_bank0;
assign SS_MAP1_BACK[27:19] = chr_bank1;
assign SS_MAP1_BACK[36:28] = chr_bank2;
assign SS_MAP1_BACK[45:37] = chr_bank3;
assign SS_MAP1_BACK[54:46] = chr_bank4;
assign SS_MAP1_BACK[63:55] = chr_bank5;
assign SS_MAP2_BACK[ 8: 0] = chr_bank6;
assign SS_MAP2_BACK[17: 9] = chr_bank7;
assign SS_MAP2_BACK[19:18] = mirroring;
assign SS_MAP2_BACK[   20] = prg_invert;
assign SS_MAP2_BACK[63:21] = 43'b0; // free to be used

always @* begin
	// mirroring mode
	casez(mirroring[1:0])
		2'b00   :   vram_a10 = {chr_ain[10]};    // vertical
		2'b01   :   vram_a10 = {chr_ain[11]};    // horizontal
		2'b1?   :   vram_a10 = {mirroring[0]};   // 1 screen
	endcase

	// PRG ROM bank size select
	casez({prg_ain[14:13],prg_invert})
		3'b00_0 : prg_tmp = prg_bank0;
		3'b00_1 : prg_tmp = 5'b11110;
		3'b01_? : prg_tmp = prg_bank1;
		3'b10_0 : prg_tmp = 5'b11110;
		3'b10_1 : prg_tmp = prg_bank0;
		3'b11_? : prg_tmp = 5'b11111;
	endcase

	// PRG ROM bank size select
	casez(chr_ain[12:10])
		3'b000 : chr_tmp = chr_bank0;
		3'b001 : chr_tmp = chr_bank1;
		3'b010 : chr_tmp = chr_bank2;
		3'b011 : chr_tmp = chr_bank3;
		3'b100 : chr_tmp = chr_bank4;
		3'b101 : chr_tmp = chr_bank5;
		3'b110 : chr_tmp = chr_bank6;
		3'b111 : chr_tmp = chr_bank7;
	endcase
end

assign vram_ce = chr_ain[13];
wire [21:13] prg_aout_tmp = {4'b00_00, prg_tmp};
wire [21:13] prg_ram = {9'b11_1100_000};
wire prg_is_ram = (prg_ain[15:13] == 3'b011);//prg_ain >= 'h6000 && prg_ain < 'h8000;
assign prg_aout[21:13] = prg_is_ram ? prg_ram : prg_aout_tmp;
assign prg_aout[12:0] = prg_ain[12:0];
assign prg_allow = (prg_ain[15] && !prg_write) || prg_is_ram;
assign chr_allow = flags[15];
assign chr_aout = {3'b10_0, vram_ce ? {5'b00000, chr_ain[13:10]} : mapper22 ? {1'b0, chr_tmp[8:1]} : chr_tmp, chr_ain[9:0]};

wire irqll = {prg_ain[15:12],registers[1:0]}==6'b1111_00; // 0xF000
wire irqlh = {prg_ain[15:12],registers[1:0]}==6'b1111_01; // 0xF001
wire irqc  = {prg_ain[15:12],registers[1:0]}==6'b1111_10; // 0xF002
wire irqa  = {prg_ain[15:12],registers[1:0]}==6'b1111_11; // 0xF003
wire irqout;
assign irq = irqout & mapperVRC4;
vrcIRQ vrc4irq
( ce_in,
	clk,1'b0,enable,prg_write,{irqlh,irqll},irqc,irqa,prg_din,irqout,ce,
	// savestates
	SaveStateBus_Din, 
	SaveStateBus_Adr,
	SaveStateBus_wren,
	SaveStateBus_rst,
	SaveStateBus_load,
	SaveStateBus_wired_or[2]
);

// savestate
localparam SAVESTATE_MODULES    = 3;
wire [63:0] SaveStateBus_wired_or[0:SAVESTATE_MODULES-1];
wire [63:0] SS_MAP1, SS_MAP2;
wire [63:0] SS_MAP1_BACK, SS_MAP2_BACK;	
wire [63:0] SaveStateBus_Dout_active = SaveStateBus_wired_or[0] | SaveStateBus_wired_or[1] | SaveStateBus_wired_or[2];
	
assign SaveStateBus_wired_or[0] = 64'd0;  // T95: savestate bus (no framework)
assign SS_MAP1 = 64'h0000000000000000;  // the register's value when nothing loads a state
assign SaveStateBus_wired_or[1] = 64'd0;  // T95: savestate bus (no framework)
assign SS_MAP2 = 64'h0000000000000000;  // the register's value when nothing loads a state

assign SaveStateBus_Dout = enable ? SaveStateBus_Dout_active : 64'h0000000000000000;

endmodule

module vrcIRQ(
	input ce_in,
	input clk20,
	input vrc5,
	input enable,
	input nesprg_we,
	input [1:0] irqlatch_add,
	input irqctrl_add,
	input irqack_add,
	input [7:0] nesprgdin,
	output irq,
	input ce,
	// savestates              
	input       [63:0]  SaveStateBus_Din,
	input       [ 9:0]  SaveStateBus_Adr,
	input               SaveStateBus_wren,
	input               SaveStateBus_rst,
	input               SaveStateBus_load,
	output      [63:0]  SaveStateBus_Dout
);
wire [63:0] SS_MAP1_BACK;  // T95: hoisted above its first use (implicit net)


//Could count up with scalar instead of down and use for both scalar and VRC5 low 8-bits latch/counter
reg [7:0] irqlatch;
reg [7:0] irqlatchL;
reg irqM,irqE,irqA;
always@(posedge clk20) if (ce_in) begin
	if (~enable) begin
		{irqM, irqA, irqlatch, irqlatchL} <= 0;
	end else if (SaveStateBus_load) begin
		irqM     <= SS_MAP1[    0];
		irqA     <= SS_MAP1[    1];
		irqlatch <= SS_MAP1[ 9: 2];
		irqlatchL<= SS_MAP1[36:29];
	end else if(ce && nesprg_we) begin
		if (irqlatch_add == 2'b11)
			irqlatch<=nesprgdin;                      //F000/D700
		else if (vrc5 && irqlatch_add == 2'b10)
			irqlatchL<=nesprgdin;                     //D600
		else if (irqlatch_add == 2'b10)
			irqlatch[7:4]<=nesprgdin[3:0];            //F000h
		else if (irqlatch_add == 2'b01)
			irqlatch[3:0]<=nesprgdin[3:0];            //F000l
		else if (irqctrl_add)
			{irqM,irqA}<={nesprgdin[2],nesprgdin[0]}; //F001
	end
end

//IRQ
reg [7:0] irqcnt;
reg [7:0] irqcntL;
reg timeout;
reg [6:0] scalar;
reg [1:0] line;
wire irqclk=vrc5|irqM|(scalar==0);
wire setE=nesprg_we & irqctrl_add & nesprgdin[1];
always@(posedge clk20) if (ce_in) begin
	if (~enable) begin
		{irqcnt, irqcntL, scalar, line} <= 0;
	end else if (SaveStateBus_load) begin
		irqcnt <= SS_MAP1[17:10];
		scalar <= SS_MAP1[24:18];
		line   <= SS_MAP1[26:25];
		irqcnt <= SS_MAP1[44:37];
	end else if(setE) begin
		scalar<=113;
		line<=0;
		irqcnt<=irqlatch;
		irqcntL<=irqlatchL;
	end else if(ce && irqE) begin
		if(scalar!=0)
			scalar<=scalar-1'd1;
		else begin
			scalar<=(~line[1])?7'd113:7'd112;
			line<=line[1]?2'd0:line+1'd1;
		end
		if(irqclk) begin
			if(irqcnt==255 && (!vrc5 | irqcntL==255)) begin
				irqcnt<=irqlatch;
				irqcntL<=irqlatchL;
			end else if (vrc5) begin
				{irqcnt,irqcntL}<={irqcnt,irqcntL}+1'd1;
			end else
				irqcnt<=irqcnt+1'd1;
		end
	end
end

always@(posedge clk20) if (ce_in) begin
	if(~enable) begin
		irqE<=0;
		timeout<=0;
	end else if (SaveStateBus_load) begin
		irqE    <= SS_MAP1[   27];
		timeout <= SS_MAP1[   28];
	end else if (ce) begin
		if(nesprg_we & (irqctrl_add | irqack_add)) //write Fxx1 or Fxx2
			timeout<=0;
		else if(irqclk & irqcnt==255 && (!vrc5 | irqcntL==255))
			timeout<=1;

		if(nesprg_we & irqctrl_add) //write Fxx1
			irqE<=nesprgdin[1];
		else if(nesprg_we & irqack_add) //write Fxx2
			irqE<=irqA;
	end
end

assign irq=timeout & irqE;

// savestate
assign SS_MAP1_BACK[    0] = irqM;
assign SS_MAP1_BACK[    1] = irqA;
assign SS_MAP1_BACK[ 9: 2] = irqlatch;
assign SS_MAP1_BACK[17:10] = irqcnt;
assign SS_MAP1_BACK[24:18] = scalar;
assign SS_MAP1_BACK[26:25] = line;
assign SS_MAP1_BACK[   27] = irqE;
assign SS_MAP1_BACK[   28] = timeout;
assign SS_MAP1_BACK[36:29] = irqlatchL;
assign SS_MAP1_BACK[44:37] = irqcntL;
assign SS_MAP1_BACK[63:45] = 19'b0; // free to be used

wire [63:0] SS_MAP1;
assign SaveStateBus_Dout = 64'd0;  // T95: savestate bus (no framework)
assign SS_MAP1 = 64'h0000000000000000;  // the register's value when nothing loads a state

endmodule
