// T91 D3: the ZX Spectrum core's Z80 (T80pa, VHDL, rtl/T80, BSD wording, Daniel Wallner / MikeJ / Sorgelig) converted by GHDL
// --synth --out=verilog; tools/zx_gen.sh made this file from the VHDL whose hash is in t80pa.sha256. Do not edit.
module t80_reg
  (input  clk,
   input  cen,
   input  weh,
   input  wel,
   input  [2:0] addra,
   input  [2:0] addrb,
   input  [2:0] addrc,
   input  [7:0] dih,
   input  [7:0] dil,
   input  dirset,
   input  [127:0] dir,
   output [7:0] doah,
   output [7:0] doal,
   output [7:0] dobh,
   output [7:0] dobl,
   output [7:0] doch,
   output [7:0] docl,
   output [127:0] dor);
  wire [63:0] regsh;
  wire [63:0] regsl;
  wire [7:0] n8278_o;
  wire [7:0] n8279_o;
  wire [7:0] n8280_o;
  wire [7:0] n8281_o;
  wire [7:0] n8282_o;
  wire [7:0] n8283_o;
  wire [7:0] n8284_o;
  wire [7:0] n8285_o;
  wire [7:0] n8286_o;
  wire [7:0] n8287_o;
  wire [7:0] n8288_o;
  wire [7:0] n8289_o;
  wire [7:0] n8290_o;
  wire [7:0] n8291_o;
  wire [7:0] n8292_o;
  wire [7:0] n8293_o;
  wire [2:0] n8296_o;
  wire [63:0] n8299_o;
  wire [2:0] n8302_o;
  wire [63:0] n8305_o;
  wire n8306_o;
  wire n8307_o;
  wire [63:0] n8308_o;
  wire [63:0] n8309_o;
  wire [63:0] n8310_o;
  wire [63:0] n8311_o;
  wire [2:0] n8317_o;
  wire [2:0] n8322_o;
  wire [2:0] n8327_o;
  wire [2:0] n8332_o;
  wire [2:0] n8337_o;
  wire [2:0] n8342_o;
  wire [7:0] n8345_o;
  wire [7:0] n8346_o;
  wire [15:0] n8347_o;
  wire [7:0] n8348_o;
  wire [23:0] n8349_o;
  wire [7:0] n8350_o;
  wire [31:0] n8351_o;
  wire [7:0] n8352_o;
  wire [39:0] n8353_o;
  wire [7:0] n8354_o;
  wire [47:0] n8355_o;
  wire [7:0] n8356_o;
  wire [55:0] n8357_o;
  wire [7:0] n8358_o;
  wire [63:0] n8359_o;
  wire [7:0] n8360_o;
  wire [71:0] n8361_o;
  wire [7:0] n8362_o;
  wire [79:0] n8363_o;
  wire [7:0] n8364_o;
  wire [87:0] n8365_o;
  wire [7:0] n8366_o;
  wire [95:0] n8367_o;
  wire [7:0] n8368_o;
  wire [103:0] n8369_o;
  wire [7:0] n8370_o;
  wire [111:0] n8371_o;
  wire [7:0] n8372_o;
  wire [119:0] n8373_o;
  wire [7:0] n8374_o;
  wire [127:0] n8375_o;
  reg [63:0] n8376_q;
  reg [63:0] n8377_q;
  wire n8378_o;
  wire n8379_o;
  wire n8380_o;
  wire n8381_o;
  wire n8382_o;
  wire n8383_o;
  wire n8384_o;
  wire n8385_o;
  wire n8386_o;
  wire n8387_o;
  wire n8388_o;
  wire n8389_o;
  wire n8390_o;
  wire n8391_o;
  wire n8392_o;
  wire n8393_o;
  wire n8394_o;
  wire n8395_o;
  wire [7:0] n8396_o;
  wire [7:0] n8397_o;
  wire [7:0] n8398_o;
  wire [7:0] n8399_o;
  wire [7:0] n8400_o;
  wire [7:0] n8401_o;
  wire [7:0] n8402_o;
  wire [7:0] n8403_o;
  wire [7:0] n8404_o;
  wire [7:0] n8405_o;
  wire [7:0] n8406_o;
  wire [7:0] n8407_o;
  wire [7:0] n8408_o;
  wire [7:0] n8409_o;
  wire [7:0] n8410_o;
  wire [7:0] n8411_o;
  wire [63:0] n8412_o;
  wire n8413_o;
  wire n8414_o;
  wire n8415_o;
  wire n8416_o;
  wire n8417_o;
  wire n8418_o;
  wire n8419_o;
  wire n8420_o;
  wire n8421_o;
  wire n8422_o;
  wire n8423_o;
  wire n8424_o;
  wire n8425_o;
  wire n8426_o;
  wire n8427_o;
  wire n8428_o;
  wire n8429_o;
  wire n8430_o;
  wire [7:0] n8431_o;
  wire [7:0] n8432_o;
  wire [7:0] n8433_o;
  wire [7:0] n8434_o;
  wire [7:0] n8435_o;
  wire [7:0] n8436_o;
  wire [7:0] n8437_o;
  wire [7:0] n8438_o;
  wire [7:0] n8439_o;
  wire [7:0] n8440_o;
  wire [7:0] n8441_o;
  wire [7:0] n8442_o;
  wire [7:0] n8443_o;
  wire [7:0] n8444_o;
  wire [7:0] n8445_o;
  wire [7:0] n8446_o;
  wire [63:0] n8447_o;
  wire [7:0] n8448_o;
  wire [7:0] n8449_o;
  wire [7:0] n8450_o;
  wire [7:0] n8451_o;
  wire [7:0] n8452_o;
  wire [7:0] n8453_o;
  wire [7:0] n8454_o;
  wire [7:0] n8455_o;
  wire [1:0] n8456_o;
  reg [7:0] n8457_o;
  wire [1:0] n8458_o;
  reg [7:0] n8459_o;
  wire n8460_o;
  wire [7:0] n8461_o;
  wire [7:0] n8462_o;
  wire [7:0] n8463_o;
  wire [7:0] n8464_o;
  wire [7:0] n8465_o;
  wire [7:0] n8466_o;
  wire [7:0] n8467_o;
  wire [7:0] n8468_o;
  wire [7:0] n8469_o;
  wire [1:0] n8470_o;
  reg [7:0] n8471_o;
  wire [1:0] n8472_o;
  reg [7:0] n8473_o;
  wire n8474_o;
  wire [7:0] n8475_o;
  wire [7:0] n8476_o;
  wire [7:0] n8477_o;
  wire [7:0] n8478_o;
  wire [7:0] n8479_o;
  wire [7:0] n8480_o;
  wire [7:0] n8481_o;
  wire [7:0] n8482_o;
  wire [7:0] n8483_o;
  wire [1:0] n8484_o;
  reg [7:0] n8485_o;
  wire [1:0] n8486_o;
  reg [7:0] n8487_o;
  wire n8488_o;
  wire [7:0] n8489_o;
  wire [7:0] n8490_o;
  wire [7:0] n8491_o;
  wire [7:0] n8492_o;
  wire [7:0] n8493_o;
  wire [7:0] n8494_o;
  wire [7:0] n8495_o;
  wire [7:0] n8496_o;
  wire [7:0] n8497_o;
  wire [1:0] n8498_o;
  reg [7:0] n8499_o;
  wire [1:0] n8500_o;
  reg [7:0] n8501_o;
  wire n8502_o;
  wire [7:0] n8503_o;
  wire [7:0] n8504_o;
  wire [7:0] n8505_o;
  wire [7:0] n8506_o;
  wire [7:0] n8507_o;
  wire [7:0] n8508_o;
  wire [7:0] n8509_o;
  wire [7:0] n8510_o;
  wire [7:0] n8511_o;
  wire [1:0] n8512_o;
  reg [7:0] n8513_o;
  wire [1:0] n8514_o;
  reg [7:0] n8515_o;
  wire n8516_o;
  wire [7:0] n8517_o;
  wire [7:0] n8518_o;
  wire [7:0] n8519_o;
  wire [7:0] n8520_o;
  wire [7:0] n8521_o;
  wire [7:0] n8522_o;
  wire [7:0] n8523_o;
  wire [7:0] n8524_o;
  wire [7:0] n8525_o;
  wire [1:0] n8526_o;
  reg [7:0] n8527_o;
  wire [1:0] n8528_o;
  reg [7:0] n8529_o;
  wire n8530_o;
  wire [7:0] n8531_o;
  assign doah = n8461_o; //(module output)
  assign doal = n8475_o; //(module output)
  assign dobh = n8489_o; //(module output)
  assign dobl = n8503_o; //(module output)
  assign doch = n8517_o; //(module output)
  assign docl = n8531_o; //(module output)
  assign dor = n8375_o; //(module output)
  /* T80_Reg.vhd:101:16  */
  assign regsh = n8376_q; // (signal)
  /* T80_Reg.vhd:102:16  */
  assign regsl = n8377_q; // (signal)
  /* T80_Reg.vhd:110:48  */
  assign n8278_o = dir[7:0]; // extract
  /* T80_Reg.vhd:111:48  */
  assign n8279_o = dir[15:8]; // extract
  /* T80_Reg.vhd:113:48  */
  assign n8280_o = dir[23:16]; // extract
  /* T80_Reg.vhd:114:48  */
  assign n8281_o = dir[31:24]; // extract
  /* T80_Reg.vhd:116:48  */
  assign n8282_o = dir[39:32]; // extract
  /* T80_Reg.vhd:117:48  */
  assign n8283_o = dir[47:40]; // extract
  /* T80_Reg.vhd:119:48  */
  assign n8284_o = dir[55:48]; // extract
  /* T80_Reg.vhd:120:48  */
  assign n8285_o = dir[63:56]; // extract
  /* T80_Reg.vhd:122:48  */
  assign n8286_o = dir[71:64]; // extract
  /* T80_Reg.vhd:123:48  */
  assign n8287_o = dir[79:72]; // extract
  /* T80_Reg.vhd:125:48  */
  assign n8288_o = dir[87:80]; // extract
  /* T80_Reg.vhd:126:48  */
  assign n8289_o = dir[95:88]; // extract
  /* T80_Reg.vhd:128:48  */
  assign n8290_o = dir[103:96]; // extract
  /* T80_Reg.vhd:129:48  */
  assign n8291_o = dir[111:104]; // extract
  /* T80_Reg.vhd:131:48  */
  assign n8292_o = dir[119:112]; // extract
  /* T80_Reg.vhd:132:48  */
  assign n8293_o = dir[127:120]; // extract
  /* T80_Reg.vhd:135:47  */
  assign n8296_o = 3'b111 - addra;
  /* T80_Reg.vhd:133:25  */
  assign n8299_o = n8306_o ? n8412_o : regsh;
  /* T80_Reg.vhd:138:47  */
  assign n8302_o = 3'b111 - addra;
  /* T80_Reg.vhd:133:25  */
  assign n8305_o = n8307_o ? n8447_o : regsl;
  /* T80_Reg.vhd:133:25  */
  assign n8306_o = weh & cen;
  /* T80_Reg.vhd:133:25  */
  assign n8307_o = wel & cen;
  assign n8308_o = {n8279_o, n8281_o, n8283_o, n8285_o, n8287_o, n8289_o, n8291_o, n8293_o};
  /* T80_Reg.vhd:109:25  */
  assign n8309_o = dirset ? n8308_o : n8299_o;
  /* T80_ALU.vhd:113:26  */
  assign n8310_o = {n8278_o, n8280_o, n8282_o, n8284_o, n8286_o, n8288_o, n8290_o, n8292_o};
  /* T80_Reg.vhd:109:25  */
  assign n8311_o = dirset ? n8310_o : n8305_o;
  /* T80_Reg.vhd:144:23  */
  assign n8317_o = 3'b111 - addra;
  /* T80_Reg.vhd:145:23  */
  assign n8322_o = 3'b111 - addra;
  /* T80_Reg.vhd:146:23  */
  assign n8327_o = 3'b111 - addrb;
  /* T80_Reg.vhd:147:23  */
  assign n8332_o = 3'b111 - addrb;
  /* T80_Reg.vhd:148:23  */
  assign n8337_o = 3'b111 - addrc;
  /* T80_Reg.vhd:149:23  */
  assign n8342_o = 3'b111 - addrc;
  /* T80_Reg.vhd:150:22  */
  assign n8345_o = regsh[7:0]; // extract
  /* T80_Reg.vhd:150:33  */
  assign n8346_o = regsl[7:0]; // extract
  /* T80_Reg.vhd:150:26  */
  assign n8347_o = {n8345_o, n8346_o};
  /* T80_Reg.vhd:150:44  */
  assign n8348_o = regsh[15:8]; // extract
  /* T80_Reg.vhd:150:37  */
  assign n8349_o = {n8347_o, n8348_o};
  /* T80_Reg.vhd:150:55  */
  assign n8350_o = regsl[15:8]; // extract
  /* T80_Reg.vhd:150:48  */
  assign n8351_o = {n8349_o, n8350_o};
  /* T80_Reg.vhd:150:66  */
  assign n8352_o = regsh[23:16]; // extract
  /* T80_Reg.vhd:150:59  */
  assign n8353_o = {n8351_o, n8352_o};
  /* T80_Reg.vhd:150:77  */
  assign n8354_o = regsl[23:16]; // extract
  /* T80_Reg.vhd:150:70  */
  assign n8355_o = {n8353_o, n8354_o};
  /* T80_Reg.vhd:150:88  */
  assign n8356_o = regsh[31:24]; // extract
  /* T80_Reg.vhd:150:81  */
  assign n8357_o = {n8355_o, n8356_o};
  /* T80_Reg.vhd:150:99  */
  assign n8358_o = regsl[31:24]; // extract
  /* T80_Reg.vhd:150:92  */
  assign n8359_o = {n8357_o, n8358_o};
  /* T80_Reg.vhd:150:110  */
  assign n8360_o = regsh[39:32]; // extract
  /* T80_Reg.vhd:150:103  */
  assign n8361_o = {n8359_o, n8360_o};
  /* T80_Reg.vhd:150:121  */
  assign n8362_o = regsl[39:32]; // extract
  /* T80_Reg.vhd:150:114  */
  assign n8363_o = {n8361_o, n8362_o};
  /* T80_Reg.vhd:150:132  */
  assign n8364_o = regsh[47:40]; // extract
  /* T80_Reg.vhd:150:125  */
  assign n8365_o = {n8363_o, n8364_o};
  /* T80_Reg.vhd:150:143  */
  assign n8366_o = regsl[47:40]; // extract
  /* T80_Reg.vhd:150:136  */
  assign n8367_o = {n8365_o, n8366_o};
  /* T80_Reg.vhd:150:154  */
  assign n8368_o = regsh[55:48]; // extract
  /* T80_Reg.vhd:150:147  */
  assign n8369_o = {n8367_o, n8368_o};
  /* T80_Reg.vhd:150:165  */
  assign n8370_o = regsl[55:48]; // extract
  /* T80_Reg.vhd:150:158  */
  assign n8371_o = {n8369_o, n8370_o};
  /* T80_Reg.vhd:150:176  */
  assign n8372_o = regsh[63:56]; // extract
  /* T80_Reg.vhd:150:169  */
  assign n8373_o = {n8371_o, n8372_o};
  /* T80_Reg.vhd:150:187  */
  assign n8374_o = regsl[63:56]; // extract
  /* T80_Reg.vhd:150:180  */
  assign n8375_o = {n8373_o, n8374_o};
  /* T80_Reg.vhd:108:17  */
  always @(posedge clk)
    n8376_q <= n8309_o;
  /* T80_Reg.vhd:108:17  */
  always @(posedge clk)
    n8377_q <= n8311_o;
  /* T80_Reg.vhd:135:41  */
  assign n8378_o = n8296_o[2]; // extract
  /* T80_Reg.vhd:135:41  */
  assign n8379_o = ~n8378_o;
  /* T80_Reg.vhd:135:41  */
  assign n8380_o = n8296_o[1]; // extract
  /* T80_Reg.vhd:135:41  */
  assign n8381_o = ~n8380_o;
  /* T80_Reg.vhd:135:41  */
  assign n8382_o = n8379_o & n8381_o;
  /* T80_Reg.vhd:135:41  */
  assign n8383_o = n8379_o & n8380_o;
  /* T80_Reg.vhd:135:41  */
  assign n8384_o = n8378_o & n8381_o;
  /* T80_Reg.vhd:135:41  */
  assign n8385_o = n8378_o & n8380_o;
  /* T80_Reg.vhd:135:41  */
  assign n8386_o = n8296_o[0]; // extract
  /* T80_Reg.vhd:135:41  */
  assign n8387_o = ~n8386_o;
  /* T80_Reg.vhd:135:41  */
  assign n8388_o = n8382_o & n8387_o;
  /* T80_Reg.vhd:135:41  */
  assign n8389_o = n8382_o & n8386_o;
  /* T80_Reg.vhd:135:41  */
  assign n8390_o = n8383_o & n8387_o;
  /* T80_Reg.vhd:135:41  */
  assign n8391_o = n8383_o & n8386_o;
  /* T80_Reg.vhd:135:41  */
  assign n8392_o = n8384_o & n8387_o;
  /* T80_Reg.vhd:135:41  */
  assign n8393_o = n8384_o & n8386_o;
  /* T80_Reg.vhd:135:41  */
  assign n8394_o = n8385_o & n8387_o;
  /* T80_Reg.vhd:135:41  */
  assign n8395_o = n8385_o & n8386_o;
  assign n8396_o = regsh[7:0]; // extract
  /* T80_Reg.vhd:135:41  */
  assign n8397_o = n8388_o ? dih : n8396_o;
  assign n8398_o = regsh[15:8]; // extract
  /* T80_Reg.vhd:135:41  */
  assign n8399_o = n8389_o ? dih : n8398_o;
  assign n8400_o = regsh[23:16]; // extract
  /* T80_Reg.vhd:135:41  */
  assign n8401_o = n8390_o ? dih : n8400_o;
  assign n8402_o = regsh[31:24]; // extract
  /* T80_Reg.vhd:135:41  */
  assign n8403_o = n8391_o ? dih : n8402_o;
  assign n8404_o = regsh[39:32]; // extract
  /* T80_Reg.vhd:135:41  */
  assign n8405_o = n8392_o ? dih : n8404_o;
  assign n8406_o = regsh[47:40]; // extract
  /* T80_Reg.vhd:135:41  */
  assign n8407_o = n8393_o ? dih : n8406_o;
  assign n8408_o = regsh[55:48]; // extract
  /* T80_Reg.vhd:135:41  */
  assign n8409_o = n8394_o ? dih : n8408_o;
  assign n8410_o = regsh[63:56]; // extract
  /* T80_Reg.vhd:135:41  */
  assign n8411_o = n8395_o ? dih : n8410_o;
  assign n8412_o = {n8411_o, n8409_o, n8407_o, n8405_o, n8403_o, n8401_o, n8399_o, n8397_o};
  /* T80_Reg.vhd:138:41  */
  assign n8413_o = n8302_o[2]; // extract
  /* T80_Reg.vhd:138:41  */
  assign n8414_o = ~n8413_o;
  /* T80_Reg.vhd:138:41  */
  assign n8415_o = n8302_o[1]; // extract
  /* T80_Reg.vhd:138:41  */
  assign n8416_o = ~n8415_o;
  /* T80_Reg.vhd:138:41  */
  assign n8417_o = n8414_o & n8416_o;
  /* T80_Reg.vhd:138:41  */
  assign n8418_o = n8414_o & n8415_o;
  /* T80_Reg.vhd:138:41  */
  assign n8419_o = n8413_o & n8416_o;
  /* T80_Reg.vhd:138:41  */
  assign n8420_o = n8413_o & n8415_o;
  /* T80_Reg.vhd:138:41  */
  assign n8421_o = n8302_o[0]; // extract
  /* T80_Reg.vhd:138:41  */
  assign n8422_o = ~n8421_o;
  /* T80_Reg.vhd:138:41  */
  assign n8423_o = n8417_o & n8422_o;
  /* T80_Reg.vhd:138:41  */
  assign n8424_o = n8417_o & n8421_o;
  /* T80_Reg.vhd:138:41  */
  assign n8425_o = n8418_o & n8422_o;
  /* T80_Reg.vhd:138:41  */
  assign n8426_o = n8418_o & n8421_o;
  /* T80_Reg.vhd:138:41  */
  assign n8427_o = n8419_o & n8422_o;
  /* T80_Reg.vhd:138:41  */
  assign n8428_o = n8419_o & n8421_o;
  /* T80_Reg.vhd:138:41  */
  assign n8429_o = n8420_o & n8422_o;
  /* T80_Reg.vhd:138:41  */
  assign n8430_o = n8420_o & n8421_o;
  assign n8431_o = regsl[7:0]; // extract
  /* T80_Reg.vhd:138:41  */
  assign n8432_o = n8423_o ? dil : n8431_o;
  assign n8433_o = regsl[15:8]; // extract
  /* T80_Reg.vhd:138:41  */
  assign n8434_o = n8424_o ? dil : n8433_o;
  assign n8435_o = regsl[23:16]; // extract
  /* T80_Reg.vhd:138:41  */
  assign n8436_o = n8425_o ? dil : n8435_o;
  assign n8437_o = regsl[31:24]; // extract
  /* T80_Reg.vhd:138:41  */
  assign n8438_o = n8426_o ? dil : n8437_o;
  assign n8439_o = regsl[39:32]; // extract
  /* T80_Reg.vhd:138:41  */
  assign n8440_o = n8427_o ? dil : n8439_o;
  assign n8441_o = regsl[47:40]; // extract
  /* T80_Reg.vhd:138:41  */
  assign n8442_o = n8428_o ? dil : n8441_o;
  assign n8443_o = regsl[55:48]; // extract
  /* T80_Reg.vhd:138:41  */
  assign n8444_o = n8429_o ? dil : n8443_o;
  assign n8445_o = regsl[63:56]; // extract
  /* T80_Reg.vhd:138:41  */
  assign n8446_o = n8430_o ? dil : n8445_o;
  assign n8447_o = {n8446_o, n8444_o, n8442_o, n8440_o, n8438_o, n8436_o, n8434_o, n8432_o};
  /* T80_Reg.vhd:138:47  */
  assign n8448_o = regsh[7:0]; // extract
  /* T80_Reg.vhd:138:41  */
  assign n8449_o = regsh[15:8]; // extract
  assign n8450_o = regsh[23:16]; // extract
  assign n8451_o = regsh[31:24]; // extract
  assign n8452_o = regsh[39:32]; // extract
  assign n8453_o = regsh[47:40]; // extract
  assign n8454_o = regsh[55:48]; // extract
  assign n8455_o = regsh[63:56]; // extract
  /* T80_Reg.vhd:144:22  */
  assign n8456_o = n8317_o[1:0]; // extract
  /* T80_Reg.vhd:144:22  */
  always @*
    case (n8456_o)
      2'b00: n8457_o = n8448_o;
      2'b01: n8457_o = n8449_o;
      2'b10: n8457_o = n8450_o;
      2'b11: n8457_o = n8451_o;
    endcase
  /* T80_Reg.vhd:144:22  */
  assign n8458_o = n8317_o[1:0]; // extract
  /* T80_Reg.vhd:144:22  */
  always @*
    case (n8458_o)
      2'b00: n8459_o = n8452_o;
      2'b01: n8459_o = n8453_o;
      2'b10: n8459_o = n8454_o;
      2'b11: n8459_o = n8455_o;
    endcase
  /* T80_Reg.vhd:144:22  */
  assign n8460_o = n8317_o[2]; // extract
  /* T80_Reg.vhd:144:22  */
  assign n8461_o = n8460_o ? n8459_o : n8457_o;
  /* T80_Reg.vhd:144:22  */
  assign n8462_o = regsl[7:0]; // extract
  /* T80_Reg.vhd:144:23  */
  assign n8463_o = regsl[15:8]; // extract
  assign n8464_o = regsl[23:16]; // extract
  assign n8465_o = regsl[31:24]; // extract
  assign n8466_o = regsl[39:32]; // extract
  assign n8467_o = regsl[47:40]; // extract
  assign n8468_o = regsl[55:48]; // extract
  assign n8469_o = regsl[63:56]; // extract
  /* T80_Reg.vhd:145:22  */
  assign n8470_o = n8322_o[1:0]; // extract
  /* T80_Reg.vhd:145:22  */
  always @*
    case (n8470_o)
      2'b00: n8471_o = n8462_o;
      2'b01: n8471_o = n8463_o;
      2'b10: n8471_o = n8464_o;
      2'b11: n8471_o = n8465_o;
    endcase
  /* T80_Reg.vhd:145:22  */
  assign n8472_o = n8322_o[1:0]; // extract
  /* T80_Reg.vhd:145:22  */
  always @*
    case (n8472_o)
      2'b00: n8473_o = n8466_o;
      2'b01: n8473_o = n8467_o;
      2'b10: n8473_o = n8468_o;
      2'b11: n8473_o = n8469_o;
    endcase
  /* T80_Reg.vhd:145:22  */
  assign n8474_o = n8322_o[2]; // extract
  /* T80_Reg.vhd:145:22  */
  assign n8475_o = n8474_o ? n8473_o : n8471_o;
  /* T80_Reg.vhd:145:22  */
  assign n8476_o = regsh[7:0]; // extract
  /* T80_Reg.vhd:145:23  */
  assign n8477_o = regsh[15:8]; // extract
  assign n8478_o = regsh[23:16]; // extract
  assign n8479_o = regsh[31:24]; // extract
  assign n8480_o = regsh[39:32]; // extract
  assign n8481_o = regsh[47:40]; // extract
  assign n8482_o = regsh[55:48]; // extract
  assign n8483_o = regsh[63:56]; // extract
  /* T80_Reg.vhd:146:22  */
  assign n8484_o = n8327_o[1:0]; // extract
  /* T80_Reg.vhd:146:22  */
  always @*
    case (n8484_o)
      2'b00: n8485_o = n8476_o;
      2'b01: n8485_o = n8477_o;
      2'b10: n8485_o = n8478_o;
      2'b11: n8485_o = n8479_o;
    endcase
  /* T80_Reg.vhd:146:22  */
  assign n8486_o = n8327_o[1:0]; // extract
  /* T80_Reg.vhd:146:22  */
  always @*
    case (n8486_o)
      2'b00: n8487_o = n8480_o;
      2'b01: n8487_o = n8481_o;
      2'b10: n8487_o = n8482_o;
      2'b11: n8487_o = n8483_o;
    endcase
  /* T80_Reg.vhd:146:22  */
  assign n8488_o = n8327_o[2]; // extract
  /* T80_Reg.vhd:146:22  */
  assign n8489_o = n8488_o ? n8487_o : n8485_o;
  /* T80_Reg.vhd:146:22  */
  assign n8490_o = regsl[7:0]; // extract
  /* T80_Reg.vhd:146:23  */
  assign n8491_o = regsl[15:8]; // extract
  assign n8492_o = regsl[23:16]; // extract
  assign n8493_o = regsl[31:24]; // extract
  assign n8494_o = regsl[39:32]; // extract
  assign n8495_o = regsl[47:40]; // extract
  assign n8496_o = regsl[55:48]; // extract
  assign n8497_o = regsl[63:56]; // extract
  /* T80_Reg.vhd:147:22  */
  assign n8498_o = n8332_o[1:0]; // extract
  /* T80_Reg.vhd:147:22  */
  always @*
    case (n8498_o)
      2'b00: n8499_o = n8490_o;
      2'b01: n8499_o = n8491_o;
      2'b10: n8499_o = n8492_o;
      2'b11: n8499_o = n8493_o;
    endcase
  /* T80_Reg.vhd:147:22  */
  assign n8500_o = n8332_o[1:0]; // extract
  /* T80_Reg.vhd:147:22  */
  always @*
    case (n8500_o)
      2'b00: n8501_o = n8494_o;
      2'b01: n8501_o = n8495_o;
      2'b10: n8501_o = n8496_o;
      2'b11: n8501_o = n8497_o;
    endcase
  /* T80_Reg.vhd:147:22  */
  assign n8502_o = n8332_o[2]; // extract
  /* T80_Reg.vhd:147:22  */
  assign n8503_o = n8502_o ? n8501_o : n8499_o;
  /* T80_Reg.vhd:147:22  */
  assign n8504_o = regsh[7:0]; // extract
  /* T80_Reg.vhd:147:23  */
  assign n8505_o = regsh[15:8]; // extract
  assign n8506_o = regsh[23:16]; // extract
  assign n8507_o = regsh[31:24]; // extract
  assign n8508_o = regsh[39:32]; // extract
  assign n8509_o = regsh[47:40]; // extract
  assign n8510_o = regsh[55:48]; // extract
  assign n8511_o = regsh[63:56]; // extract
  /* T80_Reg.vhd:148:22  */
  assign n8512_o = n8337_o[1:0]; // extract
  /* T80_Reg.vhd:148:22  */
  always @*
    case (n8512_o)
      2'b00: n8513_o = n8504_o;
      2'b01: n8513_o = n8505_o;
      2'b10: n8513_o = n8506_o;
      2'b11: n8513_o = n8507_o;
    endcase
  /* T80_Reg.vhd:148:22  */
  assign n8514_o = n8337_o[1:0]; // extract
  /* T80_Reg.vhd:148:22  */
  always @*
    case (n8514_o)
      2'b00: n8515_o = n8508_o;
      2'b01: n8515_o = n8509_o;
      2'b10: n8515_o = n8510_o;
      2'b11: n8515_o = n8511_o;
    endcase
  /* T80_Reg.vhd:148:22  */
  assign n8516_o = n8337_o[2]; // extract
  /* T80_Reg.vhd:148:22  */
  assign n8517_o = n8516_o ? n8515_o : n8513_o;
  /* T80_Reg.vhd:148:22  */
  assign n8518_o = regsl[7:0]; // extract
  /* T80_Reg.vhd:148:23  */
  assign n8519_o = regsl[15:8]; // extract
  assign n8520_o = regsl[23:16]; // extract
  assign n8521_o = regsl[31:24]; // extract
  assign n8522_o = regsl[39:32]; // extract
  assign n8523_o = regsl[47:40]; // extract
  assign n8524_o = regsl[55:48]; // extract
  assign n8525_o = regsl[63:56]; // extract
  /* T80_Reg.vhd:149:22  */
  assign n8526_o = n8342_o[1:0]; // extract
  /* T80_Reg.vhd:149:22  */
  always @*
    case (n8526_o)
      2'b00: n8527_o = n8518_o;
      2'b01: n8527_o = n8519_o;
      2'b10: n8527_o = n8520_o;
      2'b11: n8527_o = n8521_o;
    endcase
  /* T80_Reg.vhd:149:22  */
  assign n8528_o = n8342_o[1:0]; // extract
  /* T80_Reg.vhd:149:22  */
  always @*
    case (n8528_o)
      2'b00: n8529_o = n8522_o;
      2'b01: n8529_o = n8523_o;
      2'b10: n8529_o = n8524_o;
      2'b11: n8529_o = n8525_o;
    endcase
  /* T80_Reg.vhd:149:22  */
  assign n8530_o = n8342_o[2]; // extract
  /* T80_Reg.vhd:149:22  */
  assign n8531_o = n8530_o ? n8529_o : n8527_o;
endmodule

module t80_alu_0_0_1_2_3_4_5_6_7
  (input  arith16,
   input  z16,
   input  [15:0] wz,
   input  [1:0] xy_state,
   input  [3:0] alu_op,
   input  rot_akku,
   input  [5:0] ir,
   input  [1:0] iset,
   input  [7:0] busa,
   input  [7:0] busb,
   input  [7:0] f_in,
   output [7:0] q,
   output [7:0] f_out);
  wire usecarry;
  wire carry7_v;
  wire overflow_v;
  wire halfcarry_v;
  wire carry_v;
  wire [7:0] q_v;
  wire [7:0] bitmask;
  wire [2:0] n7690_o;
  wire n7693_o;
  wire n7696_o;
  wire n7699_o;
  wire n7702_o;
  wire n7705_o;
  wire n7708_o;
  wire n7711_o;
  wire [6:0] n7713_o;
  reg [7:0] n7714_o;
  wire n7715_o;
  wire n7716_o;
  wire n7717_o;
  wire n7718_o;
  wire [3:0] n7720_o;
  wire [3:0] n7721_o;
  wire n7722_o;
  wire n7723_o;
  wire n7724_o;
  wire n7725_o;
  wire n7726_o;
  wire [3:0] n7731_o;
  wire [3:0] n7732_o;
  wire [4:0] n7735_o;
  wire [5:0] n7736_o;
  wire [4:0] n7738_o;
  wire [5:0] n7740_o;
  wire [5:0] n7741_o;
  wire n7743_o;
  wire [3:0] n7744_o;
  wire [2:0] n7746_o;
  wire [2:0] n7747_o;
  wire n7748_o;
  wire [2:0] n7753_o;
  wire [2:0] n7754_o;
  wire [3:0] n7757_o;
  wire [4:0] n7758_o;
  wire [3:0] n7760_o;
  wire [4:0] n7762_o;
  wire [4:0] n7763_o;
  wire n7765_o;
  wire [2:0] n7766_o;
  wire n7768_o;
  wire n7769_o;
  wire n7770_o;
  wire n7775_o;
  wire n7776_o;
  wire [1:0] n7779_o;
  wire [2:0] n7780_o;
  wire [1:0] n7782_o;
  wire [2:0] n7784_o;
  wire [2:0] n7785_o;
  wire n7787_o;
  wire n7788_o;
  wire n7791_o;
  wire [2:0] n7799_o;
  wire n7801_o;
  wire n7803_o;
  wire n7804_o;
  wire n7806_o;
  wire n7807_o;
  wire n7809_o;
  wire n7811_o;
  wire n7812_o;
  wire n7814_o;
  wire n7815_o;
  wire [7:0] n7816_o;
  wire n7819_o;
  wire [7:0] n7820_o;
  wire n7823_o;
  wire [7:0] n7824_o;
  wire [3:0] n7826_o;
  reg n7827_o;
  reg n7828_o;
  wire n7829_o;
  reg n7830_o;
  reg n7831_o;
  reg [7:0] n7832_o;
  wire [2:0] n7833_o;
  wire n7835_o;
  wire n7836_o;
  wire n7837_o;
  wire n7838_o;
  wire n7839_o;
  wire n7840_o;
  wire n7841_o;
  wire n7843_o;
  wire n7845_o;
  wire n7846_o;
  wire n7848_o;
  wire n7849_o;
  wire [2:0] n7850_o;
  wire n7852_o;
  wire n7854_o;
  wire n7855_o;
  wire n7857_o;
  wire n7858_o;
  wire n7860_o;
  wire n7861_o;
  wire n7863_o;
  wire n7864_o;
  wire n7865_o;
  wire n7866_o;
  wire n7867_o;
  wire n7868_o;
  wire n7869_o;
  wire n7870_o;
  wire n7871_o;
  wire n7872_o;
  wire n7873_o;
  wire n7874_o;
  wire n7875_o;
  wire n7876_o;
  wire n7877_o;
  wire n7878_o;
  wire n7879_o;
  wire n7880_o;
  reg n7881_o;
  wire n7882_o;
  wire n7883_o;
  wire n7884_o;
  wire [1:0] n7885_o;
  wire n7886_o;
  wire [1:0] n7887_o;
  wire [1:0] n7888_o;
  wire n7890_o;
  wire n7892_o;
  wire n7893_o;
  wire n7895_o;
  wire n7896_o;
  wire n7898_o;
  wire n7899_o;
  wire n7901_o;
  wire n7902_o;
  wire n7904_o;
  wire n7905_o;
  wire n7907_o;
  wire n7908_o;
  wire n7910_o;
  wire n7911_o;
  wire n7912_o;
  wire n7915_o;
  wire n7916_o;
  wire [8:0] n7917_o;
  wire [3:0] n7918_o;
  wire n7920_o;
  wire n7921_o;
  wire n7922_o;
  wire [8:0] n7923_o;
  wire [3:0] n7924_o;
  wire n7926_o;
  wire n7929_o;
  wire [8:0] n7930_o;
  wire [8:0] n7932_o;
  wire n7933_o;
  wire [8:0] n7934_o;
  wire [8:0] n7935_o;
  wire [4:0] n7936_o;
  wire n7938_o;
  wire n7939_o;
  wire n7940_o;
  wire [8:0] n7942_o;
  wire [8:0] n7943_o;
  wire [8:0] n7944_o;
  wire [3:0] n7945_o;
  wire n7947_o;
  wire n7948_o;
  wire n7949_o;
  wire [8:0] n7950_o;
  wire [3:0] n7951_o;
  wire n7953_o;
  wire n7955_o;
  wire [8:0] n7956_o;
  wire [7:0] n7957_o;
  wire [7:0] n7959_o;
  wire n7960_o;
  wire [7:0] n7961_o;
  wire n7963_o;
  wire n7964_o;
  wire n7965_o;
  wire [8:0] n7966_o;
  wire [8:0] n7968_o;
  wire [8:0] n7969_o;
  wire [8:0] n7970_o;
  wire n7971_o;
  wire [8:0] n7972_o;
  wire n7973_o;
  wire n7974_o;
  wire n7975_o;
  wire n7976_o;
  wire n7977_o;
  wire [7:0] n7978_o;
  wire [7:0] n7979_o;
  wire n7981_o;
  wire n7984_o;
  wire n7985_o;
  wire n7986_o;
  wire n7987_o;
  wire n7988_o;
  wire n7989_o;
  wire n7990_o;
  wire n7991_o;
  wire n7992_o;
  wire n7993_o;
  wire n7994_o;
  wire n7995_o;
  wire n7996_o;
  wire n7997_o;
  wire n7998_o;
  wire n7999_o;
  wire n8000_o;
  wire n8001_o;
  wire n8003_o;
  wire [3:0] n8004_o;
  wire n8005_o;
  wire [3:0] n8006_o;
  wire [3:0] n8007_o;
  wire [3:0] n8008_o;
  wire [7:0] n8011_o;
  wire n8012_o;
  wire [7:0] n8013_o;
  wire n8014_o;
  wire [7:0] n8015_o;
  wire n8017_o;
  wire n8020_o;
  wire [7:0] n8021_o;
  wire n8022_o;
  wire [7:0] n8023_o;
  wire n8024_o;
  wire [7:0] n8025_o;
  wire n8026_o;
  wire n8027_o;
  wire [7:0] n8028_o;
  wire n8029_o;
  wire n8030_o;
  wire [7:0] n8031_o;
  wire n8032_o;
  wire n8033_o;
  wire [7:0] n8034_o;
  wire n8035_o;
  wire n8036_o;
  wire [7:0] n8037_o;
  wire n8038_o;
  wire n8039_o;
  wire [7:0] n8040_o;
  wire n8041_o;
  wire n8042_o;
  wire [7:0] n8043_o;
  wire n8044_o;
  wire n8045_o;
  wire n8046_o;
  wire n8048_o;
  wire n8050_o;
  wire n8051_o;
  wire [7:0] n8052_o;
  wire n8053_o;
  wire n8055_o;
  wire n8060_o;
  wire n8061_o;
  wire [2:0] n8064_o;
  wire n8066_o;
  wire n8068_o;
  wire n8069_o;
  wire n8070_o;
  wire n8071_o;
  wire n8072_o;
  wire n8073_o;
  wire n8074_o;
  wire n8075_o;
  wire n8077_o;
  wire [7:0] n8078_o;
  wire n8080_o;
  wire [7:0] n8081_o;
  wire [7:0] n8082_o;
  wire n8084_o;
  wire [2:0] n8085_o;
  wire [6:0] n8086_o;
  wire n8087_o;
  wire n8088_o;
  wire n8090_o;
  wire [6:0] n8091_o;
  wire n8092_o;
  wire n8093_o;
  wire n8095_o;
  wire [6:0] n8096_o;
  wire n8097_o;
  wire n8098_o;
  wire n8100_o;
  wire [6:0] n8101_o;
  wire n8102_o;
  wire n8103_o;
  wire n8105_o;
  wire [6:0] n8106_o;
  wire n8108_o;
  wire n8110_o;
  wire [6:0] n8111_o;
  wire n8113_o;
  wire n8115_o;
  wire [6:0] n8116_o;
  wire n8117_o;
  wire n8118_o;
  wire n8120_o;
  wire [6:0] n8121_o;
  wire n8123_o;
  wire [6:0] n8124_o;
  reg n8125_o;
  wire n8126_o;
  wire n8127_o;
  wire n8128_o;
  wire n8129_o;
  reg n8130_o;
  wire [5:0] n8131_o;
  wire [5:0] n8132_o;
  wire [5:0] n8133_o;
  wire [5:0] n8134_o;
  wire [5:0] n8135_o;
  wire [5:0] n8136_o;
  wire [5:0] n8137_o;
  wire [5:0] n8138_o;
  reg [5:0] n8139_o;
  wire n8140_o;
  wire n8141_o;
  wire n8142_o;
  wire n8143_o;
  reg n8144_o;
  wire [7:0] n8147_o;
  wire n8148_o;
  wire [7:0] n8149_o;
  wire n8150_o;
  wire [7:0] n8151_o;
  wire n8152_o;
  wire [7:0] n8153_o;
  wire n8155_o;
  wire n8158_o;
  wire [7:0] n8159_o;
  wire n8160_o;
  wire [7:0] n8161_o;
  wire n8162_o;
  wire n8163_o;
  wire [7:0] n8164_o;
  wire n8165_o;
  wire n8166_o;
  wire [7:0] n8167_o;
  wire n8168_o;
  wire n8169_o;
  wire [7:0] n8170_o;
  wire n8171_o;
  wire n8172_o;
  wire [7:0] n8173_o;
  wire n8174_o;
  wire n8175_o;
  wire [7:0] n8176_o;
  wire n8177_o;
  wire n8178_o;
  wire [7:0] n8179_o;
  wire n8180_o;
  wire n8181_o;
  wire n8182_o;
  wire n8184_o;
  wire n8185_o;
  wire n8186_o;
  wire n8187_o;
  wire [1:0] n8188_o;
  wire n8189_o;
  wire [1:0] n8190_o;
  wire [1:0] n8191_o;
  wire n8193_o;
  wire [6:0] n8194_o;
  wire n8195_o;
  reg n8196_o;
  wire n8197_o;
  reg n8198_o;
  wire n8199_o;
  reg n8200_o;
  wire n8201_o;
  reg n8202_o;
  wire n8203_o;
  reg n8204_o;
  wire n8205_o;
  reg n8206_o;
  wire n8207_o;
  wire n8208_o;
  wire n8209_o;
  reg n8210_o;
  wire n8211_o;
  wire n8212_o;
  wire n8213_o;
  reg n8214_o;
  wire n8222_o;
  wire n8223_o;
  wire n8224_o;
  wire n8225_o;
  wire n8226_o;
  wire n8227_o;
  reg n8229_o;
  wire [2:0] n8230_o;
  wire [2:0] n8231_o;
  wire [2:0] n8232_o;
  wire [2:0] n8233_o;
  wire [2:0] n8234_o;
  wire [2:0] n8235_o;
  wire [2:0] n8236_o;
  reg [2:0] n8238_o;
  wire [2:0] n8239_o;
  wire [2:0] n8240_o;
  wire [2:0] n8241_o;
  wire [2:0] n8242_o;
  wire [2:0] n8243_o;
  wire [2:0] n8244_o;
  wire [2:0] n8245_o;
  reg [2:0] n8247_o;
  wire n8248_o;
  wire n8249_o;
  wire n8250_o;
  wire n8251_o;
  wire n8252_o;
  wire n8253_o;
  reg n8255_o;
  wire [7:0] n8263_o;
  wire [7:0] n8266_o;
  wire [7:0] n8267_o;
  assign q = n8263_o; //(module output)
  assign f_out = n8267_o; //(module output)
  /* T80_ALU.vhd:127:16  */
  assign usecarry = n7718_o; // (signal)
  /* T80_ALU.vhd:128:16  */
  assign carry7_v = n7765_o; // (signal)
  /* T80_ALU.vhd:129:16  */
  assign overflow_v = n7791_o; // (signal)
  /* T80_ALU.vhd:130:16  */
  assign halfcarry_v = n7743_o; // (signal)
  /* T80_ALU.vhd:131:16  */
  assign carry_v = n7787_o; // (signal)
  /* T80_ALU.vhd:132:16  */
  assign q_v = n8266_o; // (signal)
  /* T80_ALU.vhd:134:16  */
  assign bitmask = n7714_o; // (signal)
  /* T80_ALU.vhd:138:16  */
  assign n7690_o = ir[5:3]; // extract
  /* T80_ALU.vhd:138:58  */
  assign n7693_o = n7690_o == 3'b000;
  /* T80_ALU.vhd:139:94  */
  assign n7696_o = n7690_o == 3'b001;
  /* T80_ALU.vhd:140:94  */
  assign n7699_o = n7690_o == 3'b010;
  /* T80_ALU.vhd:141:94  */
  assign n7702_o = n7690_o == 3'b011;
  /* T80_ALU.vhd:142:94  */
  assign n7705_o = n7690_o == 3'b100;
  /* T80_ALU.vhd:143:94  */
  assign n7708_o = n7690_o == 3'b101;
  /* T80_ALU.vhd:144:94  */
  assign n7711_o = n7690_o == 3'b110;
  /* T80_MCode.vhd:122:7  */
  assign n7713_o = {n7711_o, n7708_o, n7705_o, n7702_o, n7699_o, n7696_o, n7693_o};
  /* T80_ALU.vhd:138:9  */
  always @*
    case (n7713_o)
      7'b1000000: n7714_o = 8'b01000000;
      7'b0100000: n7714_o = 8'b00100000;
      7'b0010000: n7714_o = 8'b00010000;
      7'b0001000: n7714_o = 8'b00001000;
      7'b0000100: n7714_o = 8'b00000100;
      7'b0000010: n7714_o = 8'b00000010;
      7'b0000001: n7714_o = 8'b00000001;
      default: n7714_o = 8'b10000000;
    endcase
  /* T80_ALU.vhd:147:31  */
  assign n7715_o = alu_op[2]; // extract
  /* T80_ALU.vhd:147:21  */
  assign n7716_o = ~n7715_o;
  /* T80_ALU.vhd:147:45  */
  assign n7717_o = alu_op[0]; // extract
  /* T80_ALU.vhd:147:35  */
  assign n7718_o = n7716_o & n7717_o;
  /* T80_ALU.vhd:148:20  */
  assign n7720_o = busa[3:0]; // extract
  /* T80_ALU.vhd:148:38  */
  assign n7721_o = busb[3:0]; // extract
  /* T80_ALU.vhd:148:58  */
  assign n7722_o = alu_op[1]; // extract
  /* T80_ALU.vhd:148:69  */
  assign n7723_o = alu_op[1]; // extract
  /* T80_ALU.vhd:148:95  */
  assign n7724_o = f_in[0]; // extract
  /* T80_ALU.vhd:148:87  */
  assign n7725_o = usecarry & n7724_o;
  /* T80_ALU.vhd:148:73  */
  assign n7726_o = n7723_o ^ n7725_o;
  /* T80_ALU.vhd:116:32  */
  assign n7731_o = ~n7721_o;
  /* T80_ALU.vhd:115:17  */
  assign n7732_o = n7722_o ? n7731_o : n7721_o;
  /* T80_ALU.vhd:121:39  */
  assign n7735_o = {1'b0, n7720_o};
  /* T80_ALU.vhd:121:43  */
  assign n7736_o = {n7735_o, n7726_o};
  /* T80_ALU.vhd:121:70  */
  assign n7738_o = {1'b0, n7732_o};
  /* T80_ALU.vhd:121:76  */
  assign n7740_o = {n7738_o, 1'b1};
  /* T80_ALU.vhd:121:55  */
  assign n7741_o = n7736_o + n7740_o;
  /* T80_ALU.vhd:122:31  */
  assign n7743_o = n7741_o[5]; // extract
  /* T80_ALU.vhd:123:46  */
  assign n7744_o = n7741_o[4:1]; // extract
  /* T80_ALU.vhd:149:20  */
  assign n7746_o = busa[6:4]; // extract
  /* T80_ALU.vhd:149:38  */
  assign n7747_o = busb[6:4]; // extract
  /* T80_ALU.vhd:149:58  */
  assign n7748_o = alu_op[1]; // extract
  /* T80_ALU.vhd:116:32  */
  assign n7753_o = ~n7747_o;
  /* T80_ALU.vhd:115:17  */
  assign n7754_o = n7748_o ? n7753_o : n7747_o;
  /* T80_ALU.vhd:121:39  */
  assign n7757_o = {1'b0, n7746_o};
  /* T80_ALU.vhd:121:43  */
  assign n7758_o = {n7757_o, halfcarry_v};
  /* T80_ALU.vhd:121:70  */
  assign n7760_o = {1'b0, n7754_o};
  /* T80_ALU.vhd:121:76  */
  assign n7762_o = {n7760_o, 1'b1};
  /* T80_ALU.vhd:121:55  */
  assign n7763_o = n7758_o + n7762_o;
  /* T80_ALU.vhd:122:31  */
  assign n7765_o = n7763_o[4]; // extract
  /* T80_ALU.vhd:123:46  */
  assign n7766_o = n7763_o[3:1]; // extract
  /* T80_ALU.vhd:150:20  */
  assign n7768_o = busa[7]; // extract
  /* T80_ALU.vhd:150:38  */
  assign n7769_o = busb[7]; // extract
  /* T80_ALU.vhd:150:58  */
  assign n7770_o = alu_op[1]; // extract
  /* T80_ALU.vhd:116:32  */
  assign n7775_o = ~n7769_o;
  /* T80_ALU.vhd:115:17  */
  assign n7776_o = n7770_o ? n7775_o : n7769_o;
  /* T80_ALU.vhd:121:39  */
  assign n7779_o = {1'b0, n7768_o};
  /* T80_ALU.vhd:121:43  */
  assign n7780_o = {n7779_o, carry7_v};
  /* T80_ALU.vhd:121:70  */
  assign n7782_o = {1'b0, n7776_o};
  /* T80_ALU.vhd:121:76  */
  assign n7784_o = {n7782_o, 1'b1};
  /* T80_ALU.vhd:121:55  */
  assign n7785_o = n7780_o + n7784_o;
  /* T80_ALU.vhd:122:31  */
  assign n7787_o = n7785_o[2]; // extract
  /* T80_ALU.vhd:123:46  */
  assign n7788_o = n7785_o[1]; // extract
  /* T80_ALU.vhd:158:47  */
  assign n7791_o = carry_v ^ carry7_v;
  /* T80_ALU.vhd:173:36  */
  assign n7799_o = alu_op[2:0]; // extract
  /* T80_ALU.vhd:174:25  */
  assign n7801_o = n7799_o == 3'b000;
  /* T80_ALU.vhd:174:36  */
  assign n7803_o = n7799_o == 3'b001;
  /* T80_ALU.vhd:174:36  */
  assign n7804_o = n7801_o | n7803_o;
  /* T80_ALU.vhd:182:50  */
  assign n7806_o = ~carry_v;
  /* T80_ALU.vhd:183:50  */
  assign n7807_o = ~halfcarry_v;
  /* T80_ALU.vhd:179:25  */
  assign n7809_o = n7799_o == 3'b010;
  /* T80_ALU.vhd:179:36  */
  assign n7811_o = n7799_o == 3'b011;
  /* T80_ALU.vhd:179:36  */
  assign n7812_o = n7809_o | n7811_o;
  /* T80_ALU.vhd:179:44  */
  assign n7814_o = n7799_o == 3'b111;
  /* T80_ALU.vhd:179:44  */
  assign n7815_o = n7812_o | n7814_o;
  /* T80_ALU.vhd:186:57  */
  assign n7816_o = busa & busb;
  /* T80_ALU.vhd:185:25  */
  assign n7819_o = n7799_o == 3'b100;
  /* T80_ALU.vhd:189:57  */
  assign n7820_o = busa ^ busb;
  /* T80_ALU.vhd:188:25  */
  assign n7823_o = n7799_o == 3'b101;
  /* T80_ALU.vhd:192:57  */
  assign n7824_o = busa | busb;
  assign n7826_o = {n7823_o, n7819_o, n7815_o, n7804_o};
  /* T80_ALU.vhd:173:25  */
  always @*
    case (n7826_o)
      4'b1000: n7827_o = 1'b0;
      4'b0100: n7827_o = 1'b0;
      4'b0010: n7827_o = n7806_o;
      4'b0001: n7827_o = carry_v;
      default: n7827_o = 1'b0;
    endcase
  /* T80_ALU.vhd:173:25  */
  always @*
    case (n7826_o)
      4'b1000: n7828_o = 1'b0;
      4'b0100: n7828_o = 1'b0;
      4'b0010: n7828_o = 1'b1;
      4'b0001: n7828_o = 1'b0;
      default: n7828_o = 1'b0;
    endcase
  assign n7829_o = f_in[2]; // extract
  /* T80_ALU.vhd:173:25  */
  always @*
    case (n7826_o)
      4'b1000: n7830_o = n7829_o;
      4'b0100: n7830_o = n7829_o;
      4'b0010: n7830_o = overflow_v;
      4'b0001: n7830_o = overflow_v;
      default: n7830_o = n7829_o;
    endcase
  /* T80_ALU.vhd:173:25  */
  always @*
    case (n7826_o)
      4'b1000: n7831_o = 1'b0;
      4'b0100: n7831_o = 1'b1;
      4'b0010: n7831_o = n7807_o;
      4'b0001: n7831_o = halfcarry_v;
      default: n7831_o = 1'b0;
    endcase
  /* T80_ALU.vhd:173:25  */
  always @*
    case (n7826_o)
      4'b1000: n7832_o = n7820_o;
      4'b0100: n7832_o = n7816_o;
      4'b0010: n7832_o = q_v;
      4'b0001: n7832_o = q_v;
      default: n7832_o = n7824_o;
    endcase
  /* T80_ALU.vhd:195:34  */
  assign n7833_o = alu_op[2:0]; // extract
  /* T80_ALU.vhd:195:47  */
  assign n7835_o = n7833_o == 3'b111;
  /* T80_ALU.vhd:196:54  */
  assign n7836_o = busb[3]; // extract
  /* T80_ALU.vhd:197:54  */
  assign n7837_o = busb[5]; // extract
  /* T80_ALU.vhd:199:53  */
  assign n7838_o = n7832_o[3]; // extract
  /* T80_ALU.vhd:200:53  */
  assign n7839_o = n7832_o[5]; // extract
  /* T80_ALU.vhd:195:25  */
  assign n7840_o = n7835_o ? n7836_o : n7838_o;
  /* T80_ALU.vhd:195:25  */
  assign n7841_o = n7835_o ? n7837_o : n7839_o;
  /* T80_ALU.vhd:202:44  */
  assign n7843_o = n7832_o == 8'b00000000;
  /* T80_ALU.vhd:205:62  */
  assign n7845_o = f_in[6]; // extract
  /* T80_ALU.vhd:204:33  */
  assign n7846_o = z16 ? n7845_o : 1'b1;
  /* T80_ALU.vhd:202:25  */
  assign n7848_o = n7843_o ? n7846_o : 1'b0;
  /* T80_ALU.vhd:210:45  */
  assign n7849_o = n7832_o[7]; // extract
  /* T80_ALU.vhd:211:36  */
  assign n7850_o = alu_op[2:0]; // extract
  /* T80_ALU.vhd:212:25  */
  assign n7852_o = n7850_o == 3'b000;
  /* T80_ALU.vhd:212:36  */
  assign n7854_o = n7850_o == 3'b001;
  /* T80_ALU.vhd:212:36  */
  assign n7855_o = n7852_o | n7854_o;
  /* T80_ALU.vhd:212:44  */
  assign n7857_o = n7850_o == 3'b010;
  /* T80_ALU.vhd:212:44  */
  assign n7858_o = n7855_o | n7857_o;
  /* T80_ALU.vhd:212:52  */
  assign n7860_o = n7850_o == 3'b011;
  /* T80_ALU.vhd:212:52  */
  assign n7861_o = n7858_o | n7860_o;
  /* T80_ALU.vhd:212:60  */
  assign n7863_o = n7850_o == 3'b111;
  /* T80_ALU.vhd:212:60  */
  assign n7864_o = n7861_o | n7863_o;
  /* T80_ALU.vhd:214:58  */
  assign n7865_o = n7832_o[0]; // extract
  /* T80_ALU.vhd:214:69  */
  assign n7866_o = n7832_o[1]; // extract
  /* T80_ALU.vhd:214:62  */
  assign n7867_o = n7865_o ^ n7866_o;
  /* T80_ALU.vhd:214:80  */
  assign n7868_o = n7832_o[2]; // extract
  /* T80_ALU.vhd:214:73  */
  assign n7869_o = n7867_o ^ n7868_o;
  /* T80_ALU.vhd:214:91  */
  assign n7870_o = n7832_o[3]; // extract
  /* T80_ALU.vhd:214:84  */
  assign n7871_o = n7869_o ^ n7870_o;
  /* T80_ALU.vhd:215:44  */
  assign n7872_o = n7832_o[4]; // extract
  /* T80_ALU.vhd:214:95  */
  assign n7873_o = n7871_o ^ n7872_o;
  /* T80_ALU.vhd:215:55  */
  assign n7874_o = n7832_o[5]; // extract
  /* T80_ALU.vhd:215:48  */
  assign n7875_o = n7873_o ^ n7874_o;
  /* T80_ALU.vhd:215:66  */
  assign n7876_o = n7832_o[6]; // extract
  /* T80_ALU.vhd:215:59  */
  assign n7877_o = n7875_o ^ n7876_o;
  /* T80_ALU.vhd:215:77  */
  assign n7878_o = n7832_o[7]; // extract
  /* T80_ALU.vhd:215:70  */
  assign n7879_o = n7877_o ^ n7878_o;
  /* T80_ALU.vhd:214:50  */
  assign n7880_o = ~n7879_o;
  /* T80_ALU.vhd:211:25  */
  always @*
    case (n7864_o)
      1'b1: n7881_o = n7830_o;
      default: n7881_o = n7880_o;
    endcase
  /* T80_ALU.vhd:218:54  */
  assign n7882_o = f_in[7]; // extract
  /* T80_ALU.vhd:219:54  */
  assign n7883_o = f_in[6]; // extract
  /* T80_ALU.vhd:220:54  */
  assign n7884_o = f_in[2]; // extract
  assign n7885_o = {n7882_o, n7883_o};
  /* T80_ALU.vhd:217:25  */
  assign n7886_o = arith16 ? n7884_o : n7881_o;
  assign n7887_o = {n7849_o, n7848_o};
  /* T80_ALU.vhd:217:25  */
  assign n7888_o = arith16 ? n7885_o : n7887_o;
  /* T80_ALU.vhd:170:17  */
  assign n7890_o = alu_op == 4'b0000;
  /* T80_ALU.vhd:170:29  */
  assign n7892_o = alu_op == 4'b0001;
  /* T80_ALU.vhd:170:29  */
  assign n7893_o = n7890_o | n7892_o;
  /* T80_ALU.vhd:170:38  */
  assign n7895_o = alu_op == 4'b0010;
  /* T80_ALU.vhd:170:38  */
  assign n7896_o = n7893_o | n7895_o;
  /* T80_ALU.vhd:170:48  */
  assign n7898_o = alu_op == 4'b0011;
  /* T80_ALU.vhd:170:48  */
  assign n7899_o = n7896_o | n7898_o;
  /* T80_ALU.vhd:170:57  */
  assign n7901_o = alu_op == 4'b0100;
  /* T80_ALU.vhd:170:57  */
  assign n7902_o = n7899_o | n7901_o;
  /* T80_ALU.vhd:170:66  */
  assign n7904_o = alu_op == 4'b0101;
  /* T80_ALU.vhd:170:66  */
  assign n7905_o = n7902_o | n7904_o;
  /* T80_ALU.vhd:170:75  */
  assign n7907_o = alu_op == 4'b0110;
  /* T80_ALU.vhd:170:75  */
  assign n7908_o = n7905_o | n7907_o;
  /* T80_ALU.vhd:170:84  */
  assign n7910_o = alu_op == 4'b0111;
  /* T80_ALU.vhd:170:84  */
  assign n7911_o = n7908_o | n7910_o;
  /* T80_ALU.vhd:252:54  */
  assign n7912_o = f_in[4]; // extract
  /* T80_ALU.vhd:256:40  */
  assign n7915_o = f_in[1]; // extract
  /* T80_ALU.vhd:256:49  */
  assign n7916_o = ~n7915_o;
  assign n7917_o = {1'b0, busa};
  /* T80_ALU.vhd:259:49  */
  assign n7918_o = n7917_o[3:0]; // extract
  /* T80_ALU.vhd:259:62  */
  assign n7920_o = $unsigned(n7918_o) > $unsigned(4'b1001);
  /* T80_ALU.vhd:259:73  */
  assign n7921_o = f_in[4]; // extract
  /* T80_ALU.vhd:259:66  */
  assign n7922_o = n7920_o | n7921_o;
  assign n7923_o = {1'b0, busa};
  /* T80_ALU.vhd:260:58  */
  assign n7924_o = n7923_o[3:0]; // extract
  /* T80_ALU.vhd:260:71  */
  assign n7926_o = $unsigned(n7924_o) > $unsigned(4'b1001);
  /* T80_ALU.vhd:260:49  */
  assign n7929_o = n7926_o ? 1'b1 : 1'b0;
  assign n7930_o = {1'b0, busa};
  /* T80_ALU.vhd:265:64  */
  assign n7932_o = n7930_o + 9'b000000110;
  /* T80_ALU.vhd:259:41  */
  assign n7933_o = n7922_o ? n7929_o : n7912_o;
  assign n7934_o = {1'b0, busa};
  /* T80_ALU.vhd:259:41  */
  assign n7935_o = n7922_o ? n7932_o : n7934_o;
  /* T80_ALU.vhd:268:49  */
  assign n7936_o = n7935_o[8:4]; // extract
  /* T80_ALU.vhd:268:62  */
  assign n7938_o = $unsigned(n7936_o) > $unsigned(5'b01001);
  /* T80_ALU.vhd:268:73  */
  assign n7939_o = f_in[0]; // extract
  /* T80_ALU.vhd:268:66  */
  assign n7940_o = n7938_o | n7939_o;
  /* T80_ALU.vhd:269:64  */
  assign n7942_o = n7935_o + 9'b001100000;
  /* T80_ALU.vhd:268:41  */
  assign n7943_o = n7940_o ? n7942_o : n7935_o;
  assign n7944_o = {1'b0, busa};
  /* T80_ALU.vhd:273:49  */
  assign n7945_o = n7944_o[3:0]; // extract
  /* T80_ALU.vhd:273:62  */
  assign n7947_o = $unsigned(n7945_o) > $unsigned(4'b1001);
  /* T80_ALU.vhd:273:73  */
  assign n7948_o = f_in[4]; // extract
  /* T80_ALU.vhd:273:66  */
  assign n7949_o = n7947_o | n7948_o;
  assign n7950_o = {1'b0, busa};
  /* T80_ALU.vhd:274:57  */
  assign n7951_o = n7950_o[3:0]; // extract
  /* T80_ALU.vhd:274:70  */
  assign n7953_o = $unsigned(n7951_o) > $unsigned(4'b0101);
  /* T80_ALU.vhd:273:41  */
  assign n7955_o = n7960_o ? 1'b0 : n7912_o;
  assign n7956_o = {1'b0, busa};
  /* T80_ALU.vhd:277:75  */
  assign n7957_o = n7956_o[7:0]; // extract
  /* T80_ALU.vhd:277:88  */
  assign n7959_o = n7957_o - 8'b00000110;
  /* T80_ALU.vhd:273:41  */
  assign n7960_o = n7953_o & n7949_o;
  /* T80_ALU.vhd:273:41  */
  assign n7961_o = n7949_o ? n7959_o : busa;
  /* T80_ALU.vhd:279:59  */
  assign n7963_o = $unsigned(busa) > $unsigned(8'b10011001);
  /* T80_ALU.vhd:279:72  */
  assign n7964_o = f_in[0]; // extract
  /* T80_ALU.vhd:279:65  */
  assign n7965_o = n7963_o | n7964_o;
  assign n7966_o = {1'b0, n7961_o};
  /* T80_ALU.vhd:280:64  */
  assign n7968_o = n7966_o - 9'b101100000;
  assign n7969_o = {1'b0, n7961_o};
  /* T80_ALU.vhd:279:41  */
  assign n7970_o = n7965_o ? n7968_o : n7969_o;
  /* T80_ALU.vhd:256:33  */
  assign n7971_o = n7916_o ? n7933_o : n7955_o;
  /* T80_ALU.vhd:256:33  */
  assign n7972_o = n7916_o ? n7943_o : n7970_o;
  /* T80_ALU.vhd:284:47  */
  assign n7973_o = n7972_o[3]; // extract
  /* T80_ALU.vhd:285:47  */
  assign n7974_o = n7972_o[5]; // extract
  /* T80_ALU.vhd:286:46  */
  assign n7975_o = f_in[0]; // extract
  /* T80_ALU.vhd:286:63  */
  assign n7976_o = n7972_o[8]; // extract
  /* T80_ALU.vhd:286:55  */
  assign n7977_o = n7975_o | n7976_o;
  /* T80_ALU.vhd:287:54  */
  assign n7978_o = n7972_o[7:0]; // extract
  /* T80_ALU.vhd:288:33  */
  assign n7979_o = n7972_o[7:0]; // extract
  /* T80_ALU.vhd:288:46  */
  assign n7981_o = n7979_o == 8'b00000000;
  /* T80_ALU.vhd:288:25  */
  assign n7984_o = n7981_o ? 1'b1 : 1'b0;
  /* T80_ALU.vhd:293:47  */
  assign n7985_o = n7972_o[7]; // extract
  /* T80_ALU.vhd:294:52  */
  assign n7986_o = n7972_o[0]; // extract
  /* T80_ALU.vhd:294:65  */
  assign n7987_o = n7972_o[1]; // extract
  /* T80_ALU.vhd:294:56  */
  assign n7988_o = n7986_o ^ n7987_o;
  /* T80_ALU.vhd:294:78  */
  assign n7989_o = n7972_o[2]; // extract
  /* T80_ALU.vhd:294:69  */
  assign n7990_o = n7988_o ^ n7989_o;
  /* T80_ALU.vhd:294:91  */
  assign n7991_o = n7972_o[3]; // extract
  /* T80_ALU.vhd:294:82  */
  assign n7992_o = n7990_o ^ n7991_o;
  /* T80_ALU.vhd:295:38  */
  assign n7993_o = n7972_o[4]; // extract
  /* T80_ALU.vhd:294:95  */
  assign n7994_o = n7992_o ^ n7993_o;
  /* T80_ALU.vhd:295:51  */
  assign n7995_o = n7972_o[5]; // extract
  /* T80_ALU.vhd:295:42  */
  assign n7996_o = n7994_o ^ n7995_o;
  /* T80_ALU.vhd:295:64  */
  assign n7997_o = n7972_o[6]; // extract
  /* T80_ALU.vhd:295:55  */
  assign n7998_o = n7996_o ^ n7997_o;
  /* T80_ALU.vhd:295:77  */
  assign n7999_o = n7972_o[7]; // extract
  /* T80_ALU.vhd:295:68  */
  assign n8000_o = n7998_o ^ n7999_o;
  /* T80_ALU.vhd:294:42  */
  assign n8001_o = ~n8000_o;
  /* T80_ALU.vhd:222:17  */
  assign n8003_o = alu_op == 4'b1100;
  /* T80_ALU.vhd:298:48  */
  assign n8004_o = busa[7:4]; // extract
  /* T80_ALU.vhd:299:34  */
  assign n8005_o = alu_op[0]; // extract
  /* T80_ALU.vhd:300:56  */
  assign n8006_o = busb[7:4]; // extract
  /* T80_ALU.vhd:302:56  */
  assign n8007_o = busb[3:0]; // extract
  /* T80_ALU.vhd:299:25  */
  assign n8008_o = n8005_o ? n8006_o : n8007_o;
  assign n8011_o = {n8004_o, n8008_o};
  /* T80_ALU.vhd:306:45  */
  assign n8012_o = n8011_o[3]; // extract
  assign n8013_o = {n8004_o, n8008_o};
  /* T80_ALU.vhd:307:45  */
  assign n8014_o = n8013_o[5]; // extract
  assign n8015_o = {n8004_o, n8008_o};
  /* T80_ALU.vhd:308:44  */
  assign n8017_o = n8015_o == 8'b00000000;
  /* T80_ALU.vhd:308:25  */
  assign n8020_o = n8017_o ? 1'b1 : 1'b0;
  assign n8021_o = {n8004_o, n8008_o};
  /* T80_ALU.vhd:313:45  */
  assign n8022_o = n8021_o[7]; // extract
  assign n8023_o = {n8004_o, n8008_o};
  /* T80_ALU.vhd:314:50  */
  assign n8024_o = n8023_o[0]; // extract
  assign n8025_o = {n8004_o, n8008_o};
  /* T80_ALU.vhd:314:61  */
  assign n8026_o = n8025_o[1]; // extract
  /* T80_ALU.vhd:314:54  */
  assign n8027_o = n8024_o ^ n8026_o;
  assign n8028_o = {n8004_o, n8008_o};
  /* T80_ALU.vhd:314:72  */
  assign n8029_o = n8028_o[2]; // extract
  /* T80_ALU.vhd:314:65  */
  assign n8030_o = n8027_o ^ n8029_o;
  assign n8031_o = {n8004_o, n8008_o};
  /* T80_ALU.vhd:314:83  */
  assign n8032_o = n8031_o[3]; // extract
  /* T80_ALU.vhd:314:76  */
  assign n8033_o = n8030_o ^ n8032_o;
  assign n8034_o = {n8004_o, n8008_o};
  /* T80_ALU.vhd:315:36  */
  assign n8035_o = n8034_o[4]; // extract
  /* T80_ALU.vhd:314:87  */
  assign n8036_o = n8033_o ^ n8035_o;
  assign n8037_o = {n8004_o, n8008_o};
  /* T80_ALU.vhd:315:47  */
  assign n8038_o = n8037_o[5]; // extract
  /* T80_ALU.vhd:315:40  */
  assign n8039_o = n8036_o ^ n8038_o;
  assign n8040_o = {n8004_o, n8008_o};
  /* T80_ALU.vhd:315:58  */
  assign n8041_o = n8040_o[6]; // extract
  /* T80_ALU.vhd:315:51  */
  assign n8042_o = n8039_o ^ n8041_o;
  assign n8043_o = {n8004_o, n8008_o};
  /* T80_ALU.vhd:315:69  */
  assign n8044_o = n8043_o[7]; // extract
  /* T80_ALU.vhd:315:62  */
  assign n8045_o = n8042_o ^ n8044_o;
  /* T80_ALU.vhd:314:42  */
  assign n8046_o = ~n8045_o;
  /* T80_ALU.vhd:296:17  */
  assign n8048_o = alu_op == 4'b1101;
  /* T80_ALU.vhd:296:29  */
  assign n8050_o = alu_op == 4'b1110;
  /* T80_ALU.vhd:296:29  */
  assign n8051_o = n8048_o | n8050_o;
  /* T80_ALU.vhd:318:49  */
  assign n8052_o = busb & bitmask;
  /* T80_ALU.vhd:319:45  */
  assign n8053_o = n8052_o[7]; // extract
  /* T80_ALU.vhd:320:44  */
  assign n8055_o = n8052_o == 8'b00000000;
  /* T80_ALU.vhd:320:25  */
  assign n8060_o = n8055_o ? 1'b1 : 1'b0;
  /* T80_ALU.vhd:320:25  */
  assign n8061_o = n8055_o ? 1'b1 : 1'b0;
  /* T80_ALU.vhd:329:30  */
  assign n8064_o = ir[2:0]; // extract
  /* T80_ALU.vhd:329:43  */
  assign n8066_o = n8064_o == 3'b110;
  /* T80_ALU.vhd:329:63  */
  assign n8068_o = xy_state != 2'b00;
  /* T80_ALU.vhd:329:51  */
  assign n8069_o = n8066_o | n8068_o;
  /* T80_ALU.vhd:330:52  */
  assign n8070_o = wz[11]; // extract
  /* T80_ALU.vhd:331:52  */
  assign n8071_o = wz[13]; // extract
  /* T80_ALU.vhd:333:54  */
  assign n8072_o = busb[3]; // extract
  /* T80_ALU.vhd:334:54  */
  assign n8073_o = busb[5]; // extract
  /* T80_ALU.vhd:329:25  */
  assign n8074_o = n8069_o ? n8070_o : n8072_o;
  /* T80_ALU.vhd:329:25  */
  assign n8075_o = n8069_o ? n8071_o : n8073_o;
  /* T80_ALU.vhd:316:17  */
  assign n8077_o = alu_op == 4'b1001;
  /* T80_ALU.vhd:338:49  */
  assign n8078_o = busb | bitmask;
  /* T80_ALU.vhd:336:17  */
  assign n8080_o = alu_op == 4'b1010;
  /* T80_ALU.vhd:341:53  */
  assign n8081_o = ~bitmask;
  /* T80_ALU.vhd:341:49  */
  assign n8082_o = busb & n8081_o;
  /* T80_ALU.vhd:339:17  */
  assign n8084_o = alu_op == 4'b1011;
  /* T80_ALU.vhd:344:32  */
  assign n8085_o = ir[5:3]; // extract
  /* T80_ALU.vhd:346:56  */
  assign n8086_o = busa[6:0]; // extract
  /* T80_ALU.vhd:347:47  */
  assign n8087_o = busa[7]; // extract
  /* T80_ALU.vhd:348:54  */
  assign n8088_o = busa[7]; // extract
  /* T80_ALU.vhd:345:25  */
  assign n8090_o = n8085_o == 3'b000;
  /* T80_ALU.vhd:350:56  */
  assign n8091_o = busa[6:0]; // extract
  /* T80_ALU.vhd:351:47  */
  assign n8092_o = f_in[0]; // extract
  /* T80_ALU.vhd:352:54  */
  assign n8093_o = busa[7]; // extract
  /* T80_ALU.vhd:349:25  */
  assign n8095_o = n8085_o == 3'b010;
  /* T80_ALU.vhd:354:56  */
  assign n8096_o = busa[7:1]; // extract
  /* T80_ALU.vhd:355:47  */
  assign n8097_o = busa[0]; // extract
  /* T80_ALU.vhd:356:54  */
  assign n8098_o = busa[0]; // extract
  /* T80_ALU.vhd:353:25  */
  assign n8100_o = n8085_o == 3'b001;
  /* T80_ALU.vhd:358:56  */
  assign n8101_o = busa[7:1]; // extract
  /* T80_ALU.vhd:359:47  */
  assign n8102_o = f_in[0]; // extract
  /* T80_ALU.vhd:360:54  */
  assign n8103_o = busa[0]; // extract
  /* T80_ALU.vhd:357:25  */
  assign n8105_o = n8085_o == 3'b011;
  /* T80_ALU.vhd:362:56  */
  assign n8106_o = busa[6:0]; // extract
  /* T80_ALU.vhd:364:54  */
  assign n8108_o = busa[7]; // extract
  /* T80_ALU.vhd:361:25  */
  assign n8110_o = n8085_o == 3'b100;
  /* T80_ALU.vhd:371:64  */
  assign n8111_o = busa[6:0]; // extract
  /* T80_ALU.vhd:373:62  */
  assign n8113_o = busa[7]; // extract
  /* T80_ALU.vhd:365:25  */
  assign n8115_o = n8085_o == 3'b110;
  /* T80_ALU.vhd:376:56  */
  assign n8116_o = busa[7:1]; // extract
  /* T80_ALU.vhd:377:47  */
  assign n8117_o = busa[7]; // extract
  /* T80_ALU.vhd:378:54  */
  assign n8118_o = busa[0]; // extract
  /* T80_ALU.vhd:375:25  */
  assign n8120_o = n8085_o == 3'b101;
  /* T80_ALU.vhd:380:56  */
  assign n8121_o = busa[7:1]; // extract
  /* T80_ALU.vhd:382:54  */
  assign n8123_o = busa[0]; // extract
  assign n8124_o = {n8120_o, n8115_o, n8110_o, n8105_o, n8100_o, n8095_o, n8090_o};
  /* T80_ALU.vhd:344:25  */
  always @*
    case (n8124_o)
      7'b1000000: n8125_o = n8118_o;
      7'b0100000: n8125_o = n8113_o;
      7'b0010000: n8125_o = n8108_o;
      7'b0001000: n8125_o = n8103_o;
      7'b0000100: n8125_o = n8098_o;
      7'b0000010: n8125_o = n8093_o;
      7'b0000001: n8125_o = n8088_o;
      default: n8125_o = n8123_o;
    endcase
  assign n8126_o = n8096_o[0]; // extract
  assign n8127_o = n8101_o[0]; // extract
  assign n8128_o = n8116_o[0]; // extract
  assign n8129_o = n8121_o[0]; // extract
  /* T80_ALU.vhd:344:25  */
  always @*
    case (n8124_o)
      7'b1000000: n8130_o = n8128_o;
      7'b0100000: n8130_o = 1'b1;
      7'b0010000: n8130_o = 1'b0;
      7'b0001000: n8130_o = n8127_o;
      7'b0000100: n8130_o = n8126_o;
      7'b0000010: n8130_o = n8092_o;
      7'b0000001: n8130_o = n8087_o;
      default: n8130_o = n8129_o;
    endcase
  assign n8131_o = n8086_o[5:0]; // extract
  assign n8132_o = n8091_o[5:0]; // extract
  assign n8133_o = n8096_o[6:1]; // extract
  assign n8134_o = n8101_o[6:1]; // extract
  assign n8135_o = n8106_o[5:0]; // extract
  assign n8136_o = n8111_o[5:0]; // extract
  assign n8137_o = n8116_o[6:1]; // extract
  assign n8138_o = n8121_o[6:1]; // extract
  /* T80_ALU.vhd:344:25  */
  always @*
    case (n8124_o)
      7'b1000000: n8139_o = n8137_o;
      7'b0100000: n8139_o = n8136_o;
      7'b0010000: n8139_o = n8135_o;
      7'b0001000: n8139_o = n8134_o;
      7'b0000100: n8139_o = n8133_o;
      7'b0000010: n8139_o = n8132_o;
      7'b0000001: n8139_o = n8131_o;
      default: n8139_o = n8138_o;
    endcase
  assign n8140_o = n8086_o[6]; // extract
  assign n8141_o = n8091_o[6]; // extract
  assign n8142_o = n8106_o[6]; // extract
  assign n8143_o = n8111_o[6]; // extract
  /* T80_ALU.vhd:344:25  */
  always @*
    case (n8124_o)
      7'b1000000: n8144_o = n8117_o;
      7'b0100000: n8144_o = n8143_o;
      7'b0010000: n8144_o = n8142_o;
      7'b0001000: n8144_o = n8102_o;
      7'b0000100: n8144_o = n8097_o;
      7'b0000010: n8144_o = n8141_o;
      7'b0000001: n8144_o = n8140_o;
      default: n8144_o = 1'b0;
    endcase
  assign n8147_o = {n8144_o, n8139_o, n8130_o};
  /* T80_ALU.vhd:386:45  */
  assign n8148_o = n8147_o[3]; // extract
  assign n8149_o = {n8144_o, n8139_o, n8130_o};
  /* T80_ALU.vhd:387:45  */
  assign n8150_o = n8149_o[5]; // extract
  assign n8151_o = {n8144_o, n8139_o, n8130_o};
  /* T80_ALU.vhd:388:45  */
  assign n8152_o = n8151_o[7]; // extract
  assign n8153_o = {n8144_o, n8139_o, n8130_o};
  /* T80_ALU.vhd:389:44  */
  assign n8155_o = n8153_o == 8'b00000000;
  /* T80_ALU.vhd:389:25  */
  assign n8158_o = n8155_o ? 1'b1 : 1'b0;
  assign n8159_o = {n8144_o, n8139_o, n8130_o};
  /* T80_ALU.vhd:394:50  */
  assign n8160_o = n8159_o[0]; // extract
  assign n8161_o = {n8144_o, n8139_o, n8130_o};
  /* T80_ALU.vhd:394:61  */
  assign n8162_o = n8161_o[1]; // extract
  /* T80_ALU.vhd:394:54  */
  assign n8163_o = n8160_o ^ n8162_o;
  assign n8164_o = {n8144_o, n8139_o, n8130_o};
  /* T80_ALU.vhd:394:72  */
  assign n8165_o = n8164_o[2]; // extract
  /* T80_ALU.vhd:394:65  */
  assign n8166_o = n8163_o ^ n8165_o;
  assign n8167_o = {n8144_o, n8139_o, n8130_o};
  /* T80_ALU.vhd:394:83  */
  assign n8168_o = n8167_o[3]; // extract
  /* T80_ALU.vhd:394:76  */
  assign n8169_o = n8166_o ^ n8168_o;
  assign n8170_o = {n8144_o, n8139_o, n8130_o};
  /* T80_ALU.vhd:395:36  */
  assign n8171_o = n8170_o[4]; // extract
  /* T80_ALU.vhd:394:87  */
  assign n8172_o = n8169_o ^ n8171_o;
  assign n8173_o = {n8144_o, n8139_o, n8130_o};
  /* T80_ALU.vhd:395:47  */
  assign n8174_o = n8173_o[5]; // extract
  /* T80_ALU.vhd:395:40  */
  assign n8175_o = n8172_o ^ n8174_o;
  assign n8176_o = {n8144_o, n8139_o, n8130_o};
  /* T80_ALU.vhd:395:58  */
  assign n8177_o = n8176_o[6]; // extract
  /* T80_ALU.vhd:395:51  */
  assign n8178_o = n8175_o ^ n8177_o;
  assign n8179_o = {n8144_o, n8139_o, n8130_o};
  /* T80_ALU.vhd:395:69  */
  assign n8180_o = n8179_o[7]; // extract
  /* T80_ALU.vhd:395:62  */
  assign n8181_o = n8178_o ^ n8180_o;
  /* T80_ALU.vhd:394:42  */
  assign n8182_o = ~n8181_o;
  /* T80_ALU.vhd:396:33  */
  assign n8184_o = iset == 2'b00;
  /* T80_ALU.vhd:397:54  */
  assign n8185_o = f_in[2]; // extract
  /* T80_ALU.vhd:398:54  */
  assign n8186_o = f_in[7]; // extract
  /* T80_ALU.vhd:399:54  */
  assign n8187_o = f_in[6]; // extract
  assign n8188_o = {n8186_o, n8187_o};
  /* T80_ALU.vhd:396:25  */
  assign n8189_o = n8184_o ? n8185_o : n8182_o;
  assign n8190_o = {n8152_o, n8158_o};
  /* T80_ALU.vhd:396:25  */
  assign n8191_o = n8184_o ? n8188_o : n8190_o;
  /* T80_ALU.vhd:342:17  */
  assign n8193_o = alu_op == 4'b1000;
  assign n8194_o = {n8193_o, n8084_o, n8080_o, n8077_o, n8051_o, n8003_o, n7911_o};
  assign n8195_o = f_in[0]; // extract
  /* T80_ALU.vhd:169:17  */
  always @*
    case (n8194_o)
      7'b1000000: n8196_o = n8125_o;
      7'b0100000: n8196_o = n8195_o;
      7'b0010000: n8196_o = n8195_o;
      7'b0001000: n8196_o = n8195_o;
      7'b0000100: n8196_o = n8195_o;
      7'b0000010: n8196_o = n7977_o;
      7'b0000001: n8196_o = n7827_o;
      default: n8196_o = n8195_o;
    endcase
  assign n8197_o = f_in[1]; // extract
  /* T80_ALU.vhd:169:17  */
  always @*
    case (n8194_o)
      7'b1000000: n8198_o = 1'b0;
      7'b0100000: n8198_o = n8197_o;
      7'b0010000: n8198_o = n8197_o;
      7'b0001000: n8198_o = 1'b0;
      7'b0000100: n8198_o = 1'b0;
      7'b0000010: n8198_o = n8197_o;
      7'b0000001: n8198_o = n7828_o;
      default: n8198_o = n8197_o;
    endcase
  assign n8199_o = f_in[2]; // extract
  /* T80_ALU.vhd:169:17  */
  always @*
    case (n8194_o)
      7'b1000000: n8200_o = n8189_o;
      7'b0100000: n8200_o = n8199_o;
      7'b0010000: n8200_o = n8199_o;
      7'b0001000: n8200_o = n8060_o;
      7'b0000100: n8200_o = n8046_o;
      7'b0000010: n8200_o = n8001_o;
      7'b0000001: n8200_o = n7886_o;
      default: n8200_o = n8199_o;
    endcase
  assign n8201_o = f_in[3]; // extract
  /* T80_ALU.vhd:169:17  */
  always @*
    case (n8194_o)
      7'b1000000: n8202_o = n8148_o;
      7'b0100000: n8202_o = n8201_o;
      7'b0010000: n8202_o = n8201_o;
      7'b0001000: n8202_o = n8074_o;
      7'b0000100: n8202_o = n8012_o;
      7'b0000010: n8202_o = n7973_o;
      7'b0000001: n8202_o = n7840_o;
      default: n8202_o = n8201_o;
    endcase
  assign n8203_o = f_in[4]; // extract
  /* T80_ALU.vhd:169:17  */
  always @*
    case (n8194_o)
      7'b1000000: n8204_o = 1'b0;
      7'b0100000: n8204_o = n8203_o;
      7'b0010000: n8204_o = n8203_o;
      7'b0001000: n8204_o = 1'b1;
      7'b0000100: n8204_o = 1'b0;
      7'b0000010: n8204_o = n7971_o;
      7'b0000001: n8204_o = n7831_o;
      default: n8204_o = n8203_o;
    endcase
  assign n8205_o = f_in[5]; // extract
  /* T80_ALU.vhd:169:17  */
  always @*
    case (n8194_o)
      7'b1000000: n8206_o = n8150_o;
      7'b0100000: n8206_o = n8205_o;
      7'b0010000: n8206_o = n8205_o;
      7'b0001000: n8206_o = n8075_o;
      7'b0000100: n8206_o = n8014_o;
      7'b0000010: n8206_o = n7974_o;
      7'b0000001: n8206_o = n7841_o;
      default: n8206_o = n8205_o;
    endcase
  assign n8207_o = n7888_o[0]; // extract
  assign n8208_o = n8191_o[0]; // extract
  assign n8209_o = f_in[6]; // extract
  /* T80_ALU.vhd:169:17  */
  always @*
    case (n8194_o)
      7'b1000000: n8210_o = n8208_o;
      7'b0100000: n8210_o = n8209_o;
      7'b0010000: n8210_o = n8209_o;
      7'b0001000: n8210_o = n8061_o;
      7'b0000100: n8210_o = n8020_o;
      7'b0000010: n8210_o = n7984_o;
      7'b0000001: n8210_o = n8207_o;
      default: n8210_o = n8209_o;
    endcase
  assign n8211_o = n7888_o[1]; // extract
  assign n8212_o = n8191_o[1]; // extract
  assign n8213_o = f_in[7]; // extract
  /* T80_ALU.vhd:169:17  */
  always @*
    case (n8194_o)
      7'b1000000: n8214_o = n8212_o;
      7'b0100000: n8214_o = n8213_o;
      7'b0010000: n8214_o = n8213_o;
      7'b0001000: n8214_o = n8053_o;
      7'b0000100: n8214_o = n8022_o;
      7'b0000010: n8214_o = n7985_o;
      7'b0000001: n8214_o = n8211_o;
      default: n8214_o = n8213_o;
    endcase
  assign n8222_o = n7832_o[0]; // extract
  assign n8223_o = n7978_o[0]; // extract
  assign n8224_o = n8008_o[0]; // extract
  assign n8225_o = n8052_o[0]; // extract
  assign n8226_o = n8078_o[0]; // extract
  assign n8227_o = n8082_o[0]; // extract
  /* T80_ALU.vhd:169:17  */
  always @*
    case (n8194_o)
      7'b1000000: n8229_o = n8130_o;
      7'b0100000: n8229_o = n8227_o;
      7'b0010000: n8229_o = n8226_o;
      7'b0001000: n8229_o = n8225_o;
      7'b0000100: n8229_o = n8224_o;
      7'b0000010: n8229_o = n8223_o;
      7'b0000001: n8229_o = n8222_o;
      default: n8229_o = 1'bX;
    endcase
  assign n8230_o = n7832_o[3:1]; // extract
  assign n8231_o = n7978_o[3:1]; // extract
  assign n8232_o = n8008_o[3:1]; // extract
  assign n8233_o = n8052_o[3:1]; // extract
  assign n8234_o = n8078_o[3:1]; // extract
  assign n8235_o = n8082_o[3:1]; // extract
  assign n8236_o = n8139_o[2:0]; // extract
  /* T80_ALU.vhd:169:17  */
  always @*
    case (n8194_o)
      7'b1000000: n8238_o = n8236_o;
      7'b0100000: n8238_o = n8235_o;
      7'b0010000: n8238_o = n8234_o;
      7'b0001000: n8238_o = n8233_o;
      7'b0000100: n8238_o = n8232_o;
      7'b0000010: n8238_o = n8231_o;
      7'b0000001: n8238_o = n8230_o;
      default: n8238_o = 3'bX;
    endcase
  assign n8239_o = n7832_o[6:4]; // extract
  assign n8240_o = n7978_o[6:4]; // extract
  assign n8241_o = n8004_o[2:0]; // extract
  assign n8242_o = n8052_o[6:4]; // extract
  assign n8243_o = n8078_o[6:4]; // extract
  assign n8244_o = n8082_o[6:4]; // extract
  assign n8245_o = n8139_o[5:3]; // extract
  /* T80_ALU.vhd:169:17  */
  always @*
    case (n8194_o)
      7'b1000000: n8247_o = n8245_o;
      7'b0100000: n8247_o = n8244_o;
      7'b0010000: n8247_o = n8243_o;
      7'b0001000: n8247_o = n8242_o;
      7'b0000100: n8247_o = n8241_o;
      7'b0000010: n8247_o = n8240_o;
      7'b0000001: n8247_o = n8239_o;
      default: n8247_o = 3'bX;
    endcase
  assign n8248_o = n7832_o[7]; // extract
  assign n8249_o = n7978_o[7]; // extract
  assign n8250_o = n8004_o[3]; // extract
  assign n8251_o = n8052_o[7]; // extract
  assign n8252_o = n8078_o[7]; // extract
  assign n8253_o = n8082_o[7]; // extract
  /* T80_ALU.vhd:169:17  */
  always @*
    case (n8194_o)
      7'b1000000: n8255_o = n8144_o;
      7'b0100000: n8255_o = n8253_o;
      7'b0010000: n8255_o = n8252_o;
      7'b0001000: n8255_o = n8251_o;
      7'b0000100: n8255_o = n8250_o;
      7'b0000010: n8255_o = n8249_o;
      7'b0000001: n8255_o = n8248_o;
      default: n8255_o = 1'bX;
    endcase
  assign n8263_o = {n8255_o, n8247_o, n8238_o, n8229_o};
  assign n8266_o = {n7788_o, n7766_o, n7744_o};
  assign n8267_o = {n8214_o, n8210_o, n8206_o, n8204_o, n8202_o, n8200_o, n8198_o, n8196_o};
endmodule

module t80_mcode_0_0_1_2_3_4_5_6_7
  (input  [7:0] ir,
   input  [1:0] iset,
   input  [2:0] mcycle,
   input  [7:0] f,
   input  nmicycle,
   input  intcycle,
   input  [1:0] xy_state,
   input  r800_mode,
   output [2:0] mcycles,
   output [2:0] tstates,
   output [1:0] prefix,
   output inc_pc,
   output inc_wz,
   output [3:0] incdec_16,
   output read_to_reg,
   output read_to_acc,
   output [3:0] set_busa_to,
   output [3:0] set_busb_to,
   output [3:0] alu_op,
   output save_alu,
   output rot_akku,
   output preservec,
   output arith16,
   output [2:0] set_addr_to,
   output iorq,
   output jump,
   output jumpe,
   output jumpxy,
   output call,
   output rstp,
   output ldz,
   output ldw,
   output ldsphl,
   output ldhlsp,
   output addspdd,
   output [2:0] special_ld,
   output exchangedh,
   output exchangerp,
   output exchangeaf,
   output exchangers,
   output exchangewh,
   output i_djnz,
   output i_cpl,
   output i_ccf,
   output i_scf,
   output i_retn,
   output i_bt,
   output i_bc,
   output i_btr,
   output i_rld,
   output i_rrd,
   output i_inrc,
   output i_mulub,
   output i_mulu,
   output [1:0] setwz,
   output setdi,
   output setei,
   output [1:0] imode,
   output halt,
   output noread,
   output write,
   output no_pc,
   output xybit_undoc);
  wire [2:0] n2490_o;
  wire [2:0] n2491_o;
  wire [1:0] n2492_o;
  wire n2494_o;
  wire [2:0] n2497_o;
  wire [2:0] n2498_o;
  wire [3:0] n2500_o;
  wire n2502_o;
  wire n2504_o;
  wire n2505_o;
  wire n2507_o;
  wire n2508_o;
  wire n2510_o;
  wire n2511_o;
  wire n2513_o;
  wire n2514_o;
  wire n2516_o;
  wire n2517_o;
  wire n2519_o;
  wire n2520_o;
  wire n2522_o;
  wire n2523_o;
  wire n2525_o;
  wire n2526_o;
  wire n2528_o;
  wire n2529_o;
  wire n2531_o;
  wire n2532_o;
  wire n2534_o;
  wire n2535_o;
  wire n2537_o;
  wire n2538_o;
  wire n2540_o;
  wire n2541_o;
  wire n2543_o;
  wire n2544_o;
  wire n2546_o;
  wire n2547_o;
  wire n2549_o;
  wire n2550_o;
  wire n2552_o;
  wire n2553_o;
  wire n2555_o;
  wire n2556_o;
  wire n2558_o;
  wire n2559_o;
  wire n2561_o;
  wire n2562_o;
  wire n2564_o;
  wire n2565_o;
  wire n2567_o;
  wire n2568_o;
  wire n2570_o;
  wire n2571_o;
  wire n2573_o;
  wire n2574_o;
  wire n2576_o;
  wire n2577_o;
  wire n2579_o;
  wire n2580_o;
  wire n2582_o;
  wire n2583_o;
  wire n2585_o;
  wire n2586_o;
  wire n2588_o;
  wire n2589_o;
  wire n2591_o;
  wire n2592_o;
  wire n2594_o;
  wire n2595_o;
  wire n2597_o;
  wire n2598_o;
  wire n2600_o;
  wire n2601_o;
  wire n2603_o;
  wire n2604_o;
  wire n2606_o;
  wire n2607_o;
  wire n2609_o;
  wire n2610_o;
  wire n2612_o;
  wire n2613_o;
  wire n2615_o;
  wire n2616_o;
  wire n2618_o;
  wire n2619_o;
  wire n2621_o;
  wire n2622_o;
  wire n2624_o;
  wire n2625_o;
  wire n2627_o;
  wire n2628_o;
  wire n2630_o;
  wire n2631_o;
  wire n2633_o;
  wire n2634_o;
  wire n2636_o;
  wire n2637_o;
  wire n2639_o;
  wire n2640_o;
  wire n2642_o;
  wire n2643_o;
  wire n2645_o;
  wire n2646_o;
  wire [30:0] n2647_o;
  wire n2649_o;
  reg n2652_o;
  reg n2655_o;
  reg [2:0] n2657_o;
  wire n2659_o;
  wire n2661_o;
  wire n2662_o;
  wire n2664_o;
  wire n2665_o;
  wire n2667_o;
  wire n2668_o;
  wire n2670_o;
  wire n2671_o;
  wire n2673_o;
  wire n2674_o;
  wire n2676_o;
  wire n2677_o;
  wire [30:0] n2678_o;
  wire n2680_o;
  wire n2682_o;
  wire [1:0] n2683_o;
  reg n2686_o;
  reg [2:0] n2688_o;
  reg [2:0] n2691_o;
  wire n2693_o;
  wire n2695_o;
  wire n2696_o;
  wire n2698_o;
  wire n2699_o;
  wire n2701_o;
  wire n2702_o;
  wire n2704_o;
  wire n2705_o;
  wire n2707_o;
  wire n2708_o;
  wire n2710_o;
  wire n2711_o;
  wire [30:0] n2712_o;
  wire n2715_o;
  wire n2717_o;
  wire [1:0] n2718_o;
  reg [2:0] n2720_o;
  reg n2722_o;
  reg [2:0] n2725_o;
  reg n2728_o;
  wire n2730_o;
  wire n2732_o;
  wire n2733_o;
  wire n2735_o;
  wire n2736_o;
  wire n2738_o;
  wire n2739_o;
  wire n2741_o;
  wire n2742_o;
  wire n2744_o;
  wire n2745_o;
  wire n2747_o;
  wire n2748_o;
  wire [30:0] n2749_o;
  wire n2752_o;
  wire n2754_o;
  wire [1:0] n2755_o;
  reg n2758_o;
  reg [2:0] n2760_o;
  reg n2762_o;
  reg [2:0] n2765_o;
  reg n2768_o;
  wire n2770_o;
  wire [30:0] n2771_o;
  wire n2773_o;
  wire n2775_o;
  wire [1:0] n2776_o;
  reg n2779_o;
  reg [2:0] n2782_o;
  wire n2784_o;
  wire [30:0] n2785_o;
  wire n2787_o;
  wire n2789_o;
  wire [1:0] n2790_o;
  reg n2793_o;
  reg [2:0] n2796_o;
  wire n2798_o;
  wire [30:0] n2799_o;
  wire n2801_o;
  wire n2803_o;
  wire n2805_o;
  wire [2:0] n2806_o;
  reg n2810_o;
  reg n2813_o;
  reg [2:0] n2816_o;
  reg n2819_o;
  wire n2821_o;
  wire [30:0] n2822_o;
  wire n2824_o;
  wire n2826_o;
  wire [1:0] n2827_o;
  reg [3:0] n2830_o;
  reg [2:0] n2833_o;
  reg [1:0] n2836_o;
  reg n2839_o;
  wire n2841_o;
  wire [30:0] n2842_o;
  wire n2844_o;
  wire n2846_o;
  wire [1:0] n2847_o;
  reg [3:0] n2850_o;
  reg [2:0] n2853_o;
  reg [1:0] n2856_o;
  reg n2859_o;
  wire n2861_o;
  wire [30:0] n2862_o;
  wire n2864_o;
  wire n2866_o;
  wire n2868_o;
  wire [2:0] n2869_o;
  reg n2873_o;
  reg [3:0] n2876_o;
  reg [2:0] n2879_o;
  reg n2882_o;
  reg [1:0] n2885_o;
  reg n2888_o;
  wire n2890_o;
  wire [30:0] n2891_o;
  wire n2893_o;
  wire [2:0] n2895_o;
  wire [2:0] n2897_o;
  wire n2900_o;
  wire n2902_o;
  wire n2904_o;
  wire [2:0] n2906_o;
  wire [2:0] n2908_o;
  wire n2911_o;
  wire n2913_o;
  wire [1:0] n2914_o;
  reg n2918_o;
  reg n2922_o;
  reg [2:0] n2924_o;
  reg n2926_o;
  wire n2928_o;
  wire n2930_o;
  wire n2931_o;
  wire n2933_o;
  wire n2934_o;
  wire n2936_o;
  wire n2937_o;
  wire [30:0] n2938_o;
  wire n2940_o;
  wire n2942_o;
  wire n2945_o;
  wire n2948_o;
  wire [3:0] n2949_o;
  reg n2953_o;
  reg n2956_o;
  reg n2960_o;
  reg [2:0] n2962_o;
  reg [2:0] n2966_o;
  reg n2969_o;
  reg n2972_o;
  wire n2974_o;
  wire [30:0] n2975_o;
  wire n2977_o;
  wire n2979_o;
  wire n2981_o;
  wire n2983_o;
  wire [3:0] n2984_o;
  reg n2988_o;
  reg n2991_o;
  reg [3:0] n2995_o;
  reg [2:0] n2999_o;
  reg n3002_o;
  reg n3005_o;
  reg n3009_o;
  wire n3011_o;
  wire n3013_o;
  wire [30:0] n3014_o;
  wire n3016_o;
  wire [3:0] n3019_o;
  wire [3:0] n3021_o;
  wire n3023_o;
  wire n3025_o;
  wire [3:0] n3028_o;
  wire [3:0] n3030_o;
  wire n3032_o;
  wire n3034_o;
  wire [2:0] n3035_o;
  reg [2:0] n3037_o;
  reg [3:0] n3041_o;
  reg [3:0] n3043_o;
  reg [2:0] n3047_o;
  reg n3051_o;
  wire n3053_o;
  wire n3055_o;
  wire n3056_o;
  wire n3058_o;
  wire n3059_o;
  wire n3061_o;
  wire n3062_o;
  wire [30:0] n3063_o;
  wire n3065_o;
  wire n3067_o;
  wire [2:0] n3069_o;
  wire [2:0] n3071_o;
  wire n3074_o;
  wire n3076_o;
  wire n3078_o;
  wire [2:0] n3080_o;
  wire [2:0] n3082_o;
  wire n3085_o;
  wire n3087_o;
  wire [2:0] n3088_o;
  reg [3:0] n3092_o;
  reg n3096_o;
  reg [2:0] n3098_o;
  reg n3100_o;
  reg [2:0] n3104_o;
  wire n3106_o;
  wire n3108_o;
  wire n3109_o;
  wire n3111_o;
  wire n3112_o;
  wire n3114_o;
  wire n3115_o;
  wire n3117_o;
  wire n3119_o;
  wire n3121_o;
  wire [30:0] n3122_o;
  wire n3124_o;
  wire n3126_o;
  wire n3128_o;
  wire n3130_o;
  wire n3132_o;
  wire [4:0] n3133_o;
  reg [2:0] n3136_o;
  reg [3:0] n3140_o;
  reg [3:0] n3144_o;
  reg [2:0] n3150_o;
  reg n3153_o;
  reg n3156_o;
  reg n3159_o;
  reg n3163_o;
  wire n3165_o;
  wire n3168_o;
  wire n3170_o;
  wire n3171_o;
  wire n3173_o;
  wire n3174_o;
  wire n3176_o;
  wire n3177_o;
  wire n3179_o;
  wire n3180_o;
  wire n3182_o;
  wire n3183_o;
  wire n3185_o;
  wire n3186_o;
  wire n3188_o;
  wire n3189_o;
  wire n3191_o;
  wire n3192_o;
  wire n3194_o;
  wire n3195_o;
  wire n3197_o;
  wire n3198_o;
  wire n3200_o;
  wire n3201_o;
  wire n3203_o;
  wire n3204_o;
  wire n3206_o;
  wire n3207_o;
  wire n3209_o;
  wire n3210_o;
  wire n3212_o;
  wire n3213_o;
  wire n3215_o;
  wire n3216_o;
  wire n3218_o;
  wire n3219_o;
  wire n3221_o;
  wire n3222_o;
  wire n3224_o;
  wire n3225_o;
  wire n3227_o;
  wire n3228_o;
  wire n3230_o;
  wire n3231_o;
  wire n3233_o;
  wire n3234_o;
  wire n3236_o;
  wire n3237_o;
  wire n3239_o;
  wire n3240_o;
  wire n3242_o;
  wire n3243_o;
  wire n3245_o;
  wire n3246_o;
  wire n3248_o;
  wire n3249_o;
  wire n3251_o;
  wire n3252_o;
  wire n3254_o;
  wire n3255_o;
  wire n3257_o;
  wire n3258_o;
  wire n3260_o;
  wire n3261_o;
  wire n3263_o;
  wire n3264_o;
  wire n3266_o;
  wire n3267_o;
  wire n3269_o;
  wire n3270_o;
  wire n3272_o;
  wire n3273_o;
  wire n3275_o;
  wire n3276_o;
  wire n3278_o;
  wire n3279_o;
  wire n3281_o;
  wire n3282_o;
  wire n3284_o;
  wire n3285_o;
  wire n3287_o;
  wire n3288_o;
  wire n3290_o;
  wire n3291_o;
  wire n3293_o;
  wire n3294_o;
  wire n3296_o;
  wire n3297_o;
  wire n3299_o;
  wire n3300_o;
  wire n3302_o;
  wire n3303_o;
  wire n3305_o;
  wire n3306_o;
  wire n3308_o;
  wire n3309_o;
  wire n3311_o;
  wire n3312_o;
  wire n3314_o;
  wire n3315_o;
  wire n3317_o;
  wire n3318_o;
  wire n3320_o;
  wire n3321_o;
  wire n3323_o;
  wire n3324_o;
  wire n3326_o;
  wire n3327_o;
  wire n3329_o;
  wire n3330_o;
  wire n3332_o;
  wire n3333_o;
  wire [30:0] n3334_o;
  wire n3336_o;
  wire n3339_o;
  wire [1:0] n3340_o;
  reg n3343_o;
  reg [2:0] n3345_o;
  reg [2:0] n3347_o;
  reg n3350_o;
  reg [2:0] n3353_o;
  wire n3355_o;
  wire n3357_o;
  wire n3358_o;
  wire n3360_o;
  wire n3361_o;
  wire n3363_o;
  wire n3364_o;
  wire n3366_o;
  wire n3367_o;
  wire n3369_o;
  wire n3370_o;
  wire n3372_o;
  wire n3373_o;
  wire n3375_o;
  wire n3376_o;
  wire n3378_o;
  wire n3382_o;
  wire n3385_o;
  wire [2:0] n3387_o;
  wire [2:0] n3389_o;
  wire n3392_o;
  wire n3394_o;
  wire n3396_o;
  wire n3397_o;
  wire n3399_o;
  wire n3400_o;
  wire n3402_o;
  wire n3403_o;
  wire n3405_o;
  wire n3406_o;
  wire n3408_o;
  wire n3409_o;
  wire n3411_o;
  wire n3412_o;
  wire n3414_o;
  wire n3415_o;
  wire n3417_o;
  wire n3419_o;
  wire n3420_o;
  wire n3422_o;
  wire n3423_o;
  wire n3425_o;
  wire n3426_o;
  wire n3428_o;
  wire n3429_o;
  wire n3431_o;
  wire n3432_o;
  wire n3434_o;
  wire n3435_o;
  wire [30:0] n3436_o;
  wire n3438_o;
  wire n3440_o;
  wire n3442_o;
  wire [2:0] n3443_o;
  reg [2:0] n3445_o;
  reg n3448_o;
  reg [2:0] n3450_o;
  reg [3:0] n3453_o;
  reg [3:0] n3455_o;
  reg n3458_o;
  reg n3461_o;
  reg [2:0] n3465_o;
  reg n3468_o;
  wire n3470_o;
  wire n3472_o;
  wire n3474_o;
  wire n3475_o;
  wire n3477_o;
  wire n3478_o;
  wire n3480_o;
  wire n3481_o;
  wire n3483_o;
  wire n3484_o;
  wire n3486_o;
  wire n3487_o;
  wire n3489_o;
  wire n3490_o;
  wire [30:0] n3491_o;
  wire n3493_o;
  wire n3495_o;
  wire n3497_o;
  wire [2:0] n3498_o;
  reg [2:0] n3500_o;
  reg n3503_o;
  reg [2:0] n3505_o;
  reg [3:0] n3508_o;
  reg [3:0] n3510_o;
  reg n3513_o;
  reg n3516_o;
  reg [2:0] n3520_o;
  reg n3523_o;
  wire n3525_o;
  wire n3528_o;
  wire n3530_o;
  wire n3532_o;
  wire n3534_o;
  wire [30:0] n3535_o;
  wire n3537_o;
  wire n3539_o;
  wire n3541_o;
  wire [2:0] n3542_o;
  reg [2:0] n3544_o;
  reg [3:0] n3548_o;
  reg [3:0] n3552_o;
  reg [2:0] n3556_o;
  reg n3560_o;
  wire [30:0] n3561_o;
  wire n3563_o;
  wire n3565_o;
  wire n3567_o;
  wire n3569_o;
  wire n3571_o;
  wire [4:0] n3572_o;
  reg [2:0] n3574_o;
  reg n3577_o;
  reg [3:0] n3581_o;
  reg [3:0] n3585_o;
  reg [2:0] n3589_o;
  reg n3592_o;
  reg n3595_o;
  reg n3599_o;
  wire [2:0] n3602_o;
  wire [2:0] n3603_o;
  wire n3605_o;
  wire [3:0] n3607_o;
  wire [3:0] n3609_o;
  wire [2:0] n3611_o;
  wire n3613_o;
  wire n3615_o;
  wire n3617_o;
  wire [2:0] n3619_o;
  wire [2:0] n3620_o;
  wire n3622_o;
  wire [3:0] n3623_o;
  wire [3:0] n3624_o;
  wire [2:0] n3625_o;
  wire n3627_o;
  wire n3629_o;
  wire n3630_o;
  wire n3632_o;
  wire n3634_o;
  wire n3636_o;
  wire n3638_o;
  wire [30:0] n3639_o;
  wire n3641_o;
  wire [1:0] n3643_o;
  wire [30:0] n3644_o;
  wire [1:0] n3645_o;
  wire n3648_o;
  wire n3650_o;
  wire n3651_o;
  wire n3653_o;
  wire n3654_o;
  reg n3656_o;
  reg [1:0] n3658_o;
  reg n3661_o;
  wire n3663_o;
  wire [1:0] n3665_o;
  wire [30:0] n3666_o;
  wire [1:0] n3667_o;
  wire n3669_o;
  wire n3671_o;
  wire n3672_o;
  wire n3674_o;
  wire n3675_o;
  reg n3678_o;
  reg [1:0] n3680_o;
  reg n3683_o;
  wire n3685_o;
  wire [2:0] n3686_o;
  reg [2:0] n3688_o;
  reg n3692_o;
  reg [2:0] n3694_o;
  reg n3696_o;
  reg [1:0] n3698_o;
  reg n3700_o;
  reg [3:0] n3703_o;
  reg n3707_o;
  reg n3711_o;
  reg [1:0] n3714_o;
  reg n3718_o;
  reg n3722_o;
  wire n3724_o;
  wire n3726_o;
  wire n3727_o;
  wire n3729_o;
  wire n3730_o;
  wire n3732_o;
  wire n3733_o;
  wire n3736_o;
  wire n3738_o;
  wire n3739_o;
  wire n3741_o;
  wire n3742_o;
  wire n3744_o;
  wire n3745_o;
  wire n3748_o;
  wire n3750_o;
  wire n3751_o;
  wire n3753_o;
  wire n3754_o;
  wire n3756_o;
  wire n3757_o;
  wire n3760_o;
  wire n3762_o;
  wire n3763_o;
  wire n3765_o;
  wire n3766_o;
  wire n3768_o;
  wire n3769_o;
  wire [30:0] n3770_o;
  wire n3772_o;
  wire n3774_o;
  wire [1:0] n3775_o;
  reg n3779_o;
  reg n3782_o;
  reg n3785_o;
  reg n3788_o;
  wire n3790_o;
  wire [30:0] n3792_o;
  wire n3794_o;
  wire [2:0] n3796_o;
  wire n3801_o;
  wire n3802_o;
  wire n3804_o;
  wire n3805_o;
  wire n3807_o;
  wire n3808_o;
  wire n3809_o;
  wire n3811_o;
  wire n3812_o;
  wire n3814_o;
  wire n3815_o;
  wire n3816_o;
  wire n3818_o;
  wire n3819_o;
  wire n3821_o;
  wire n3822_o;
  wire n3823_o;
  wire n3825_o;
  wire n3826_o;
  wire n3828_o;
  wire [7:0] n3829_o;
  reg n3831_o;
  wire n3834_o;
  wire n3836_o;
  wire [1:0] n3837_o;
  reg n3841_o;
  reg n3843_o;
  reg n3846_o;
  reg n3849_o;
  wire n3851_o;
  wire n3853_o;
  wire n3854_o;
  wire n3856_o;
  wire n3857_o;
  wire n3859_o;
  wire n3860_o;
  wire n3862_o;
  wire n3863_o;
  wire n3865_o;
  wire n3866_o;
  wire n3868_o;
  wire n3869_o;
  wire n3871_o;
  wire n3872_o;
  wire [30:0] n3873_o;
  wire n3875_o;
  wire n3877_o;
  wire [1:0] n3878_o;
  reg [2:0] n3880_o;
  reg n3883_o;
  reg n3886_o;
  reg n3889_o;
  reg n3892_o;
  wire n3894_o;
  wire [30:0] n3895_o;
  wire n3896_o;
  wire n3897_o;
  wire [2:0] n3900_o;
  wire n3903_o;
  wire n3905_o;
  wire n3907_o;
  wire [1:0] n3908_o;
  reg [2:0] n3910_o;
  reg [2:0] n3913_o;
  reg n3916_o;
  reg n3919_o;
  reg n3922_o;
  reg n3924_o;
  wire n3926_o;
  wire [30:0] n3927_o;
  wire n3928_o;
  wire [2:0] n3931_o;
  wire n3934_o;
  wire n3936_o;
  wire n3938_o;
  wire [1:0] n3939_o;
  reg [2:0] n3941_o;
  reg [2:0] n3944_o;
  reg n3947_o;
  reg n3950_o;
  reg n3953_o;
  reg n3955_o;
  wire n3957_o;
  wire [30:0] n3958_o;
  wire n3959_o;
  wire n3960_o;
  wire [2:0] n3963_o;
  wire n3966_o;
  wire n3968_o;
  wire n3970_o;
  wire [1:0] n3971_o;
  reg [2:0] n3973_o;
  reg [2:0] n3976_o;
  reg n3979_o;
  reg n3982_o;
  reg n3985_o;
  reg n3987_o;
  wire n3989_o;
  wire [30:0] n3990_o;
  wire n3991_o;
  wire [2:0] n3994_o;
  wire n3997_o;
  wire n3999_o;
  wire n4001_o;
  wire [1:0] n4002_o;
  reg [2:0] n4004_o;
  reg [2:0] n4007_o;
  reg n4010_o;
  reg n4013_o;
  reg n4016_o;
  reg n4018_o;
  wire n4020_o;
  wire n4022_o;
  wire [30:0] n4023_o;
  wire n4026_o;
  wire n4028_o;
  wire n4030_o;
  wire [2:0] n4031_o;
  reg [2:0] n4034_o;
  reg n4037_o;
  reg n4040_o;
  reg [2:0] n4042_o;
  reg [3:0] n4045_o;
  reg [3:0] n4047_o;
  reg n4050_o;
  reg n4053_o;
  reg n4057_o;
  reg n4060_o;
  reg n4063_o;
  wire n4065_o;
  wire [30:0] n4066_o;
  wire n4068_o;
  wire n4070_o;
  wire n4072_o;
  wire n4074_o;
  wire [3:0] n4075_o;
  reg [2:0] n4077_o;
  reg n4081_o;
  reg [3:0] n4085_o;
  reg [3:0] n4089_o;
  reg [2:0] n4093_o;
  reg n4096_o;
  reg n4099_o;
  reg n4102_o;
  reg n4106_o;
  wire n4108_o;
  wire [30:0] n4111_o;
  wire n4113_o;
  wire [2:0] n4115_o;
  wire n4120_o;
  wire n4121_o;
  wire n4123_o;
  wire n4124_o;
  wire n4126_o;
  wire n4127_o;
  wire n4128_o;
  wire n4130_o;
  wire n4131_o;
  wire n4133_o;
  wire n4134_o;
  wire n4135_o;
  wire n4137_o;
  wire n4138_o;
  wire n4140_o;
  wire n4141_o;
  wire n4142_o;
  wire n4144_o;
  wire n4145_o;
  wire n4147_o;
  wire [7:0] n4148_o;
  reg n4150_o;
  wire [2:0] n4153_o;
  wire [2:0] n4155_o;
  wire [3:0] n4158_o;
  wire [3:0] n4161_o;
  wire [2:0] n4164_o;
  wire n4166_o;
  wire n4168_o;
  wire n4170_o;
  wire [3:0] n4171_o;
  reg [2:0] n4173_o;
  reg [2:0] n4175_o;
  reg n4179_o;
  reg [3:0] n4182_o;
  reg [3:0] n4185_o;
  reg [2:0] n4188_o;
  reg n4191_o;
  reg n4194_o;
  reg n4197_o;
  reg n4201_o;
  wire n4203_o;
  wire n4205_o;
  wire n4206_o;
  wire n4208_o;
  wire n4209_o;
  wire n4211_o;
  wire n4212_o;
  wire n4214_o;
  wire n4215_o;
  wire n4217_o;
  wire n4218_o;
  wire n4220_o;
  wire n4221_o;
  wire n4223_o;
  wire n4224_o;
  wire [30:0] n4225_o;
  wire n4227_o;
  wire n4229_o;
  wire n4231_o;
  wire [2:0] n4232_o;
  reg [3:0] n4236_o;
  reg [2:0] n4240_o;
  reg n4243_o;
  reg n4246_o;
  wire n4248_o;
  wire [30:0] n4250_o;
  wire [2:0] n4252_o;
  wire n4257_o;
  wire n4258_o;
  wire n4260_o;
  wire n4261_o;
  wire n4263_o;
  wire n4264_o;
  wire n4265_o;
  wire n4267_o;
  wire n4268_o;
  wire n4270_o;
  wire n4271_o;
  wire n4272_o;
  wire n4274_o;
  wire n4275_o;
  wire n4277_o;
  wire n4278_o;
  wire n4279_o;
  wire n4281_o;
  wire n4282_o;
  wire n4284_o;
  wire [7:0] n4285_o;
  reg n4287_o;
  wire [2:0] n4290_o;
  wire [2:0] n4293_o;
  wire n4295_o;
  wire n4297_o;
  wire n4299_o;
  wire [2:0] n4300_o;
  reg [2:0] n4302_o;
  reg [2:0] n4305_o;
  reg [3:0] n4309_o;
  reg [2:0] n4312_o;
  reg n4315_o;
  reg n4318_o;
  wire n4320_o;
  wire n4322_o;
  wire n4323_o;
  wire n4325_o;
  wire n4326_o;
  wire n4328_o;
  wire n4329_o;
  wire n4331_o;
  wire n4332_o;
  wire n4334_o;
  wire n4335_o;
  wire n4337_o;
  wire n4338_o;
  wire n4340_o;
  wire n4341_o;
  wire [30:0] n4342_o;
  wire n4344_o;
  wire n4346_o;
  wire n4348_o;
  wire [2:0] n4349_o;
  reg [2:0] n4351_o;
  reg [3:0] n4355_o;
  reg [3:0] n4359_o;
  reg [2:0] n4363_o;
  reg n4366_o;
  reg n4370_o;
  wire n4372_o;
  wire n4374_o;
  wire n4375_o;
  wire n4377_o;
  wire n4378_o;
  wire n4380_o;
  wire n4381_o;
  wire n4383_o;
  wire n4384_o;
  wire n4386_o;
  wire n4387_o;
  wire n4389_o;
  wire n4390_o;
  wire n4392_o;
  wire n4393_o;
  wire [30:0] n4394_o;
  wire n4396_o;
  wire n4398_o;
  wire [1:0] n4399_o;
  reg n4402_o;
  reg n4405_o;
  reg [2:0] n4408_o;
  reg n4411_o;
  wire n4413_o;
  wire [30:0] n4414_o;
  wire n4416_o;
  wire n4418_o;
  wire [1:0] n4419_o;
  reg n4422_o;
  reg [3:0] n4425_o;
  reg [2:0] n4428_o;
  reg n4431_o;
  reg n4434_o;
  wire n4436_o;
  wire n4438_o;
  wire n4440_o;
  wire n4442_o;
  wire n4444_o;
  wire n4445_o;
  wire [58:0] n4446_o;
  reg [2:0] n4479_o;
  reg [2:0] n4484_o;
  reg [1:0] n4490_o;
  reg n4493_o;
  reg n4496_o;
  wire [1:0] n4497_o;
  wire [1:0] n4498_o;
  wire [1:0] n4499_o;
  wire [1:0] n4500_o;
  wire [1:0] n4501_o;
  wire [1:0] n4502_o;
  wire [1:0] n4503_o;
  wire [1:0] n4504_o;
  wire [1:0] n4505_o;
  reg [1:0] n4508_o;
  wire [1:0] n4509_o;
  wire [1:0] n4510_o;
  wire [1:0] n4511_o;
  wire [1:0] n4512_o;
  wire [1:0] n4513_o;
  wire [1:0] n4514_o;
  wire [1:0] n4515_o;
  wire [1:0] n4516_o;
  wire [1:0] n4517_o;
  reg [1:0] n4520_o;
  reg n4529_o;
  reg n4532_o;
  reg [2:0] n4535_o;
  reg n4538_o;
  wire n4539_o;
  wire n4540_o;
  wire n4541_o;
  wire n4542_o;
  wire n4543_o;
  wire n4544_o;
  wire n4545_o;
  wire n4546_o;
  wire n4547_o;
  wire n4548_o;
  wire n4549_o;
  wire n4550_o;
  wire n4552_o;
  wire n4554_o;
  wire n4555_o;
  wire n4556_o;
  wire n4557_o;
  wire n4558_o;
  wire n4559_o;
  wire n4560_o;
  reg n4563_o;
  wire [1:0] n4564_o;
  wire [1:0] n4565_o;
  wire [1:0] n4566_o;
  wire [1:0] n4567_o;
  wire [1:0] n4568_o;
  wire [1:0] n4569_o;
  wire [1:0] n4570_o;
  wire [1:0] n4571_o;
  wire [1:0] n4572_o;
  wire [1:0] n4573_o;
  wire [1:0] n4574_o;
  wire [1:0] n4575_o;
  wire [1:0] n4577_o;
  wire [1:0] n4579_o;
  wire [1:0] n4580_o;
  wire [1:0] n4581_o;
  wire [1:0] n4582_o;
  wire [1:0] n4583_o;
  wire [1:0] n4584_o;
  wire [1:0] n4585_o;
  reg [1:0] n4588_o;
  wire n4589_o;
  wire n4590_o;
  wire n4591_o;
  wire n4592_o;
  wire n4593_o;
  wire n4594_o;
  wire n4596_o;
  wire n4598_o;
  wire n4599_o;
  wire n4600_o;
  wire n4601_o;
  wire n4602_o;
  wire n4603_o;
  wire n4604_o;
  reg n4607_o;
  reg [3:0] n4613_o;
  reg n4621_o;
  reg n4625_o;
  reg n4630_o;
  reg n4633_o;
  reg [2:0] n4636_o;
  reg n4639_o;
  reg n4642_o;
  reg n4645_o;
  reg n4649_o;
  reg n4652_o;
  reg n4655_o;
  reg n4658_o;
  reg n4661_o;
  reg n4665_o;
  reg n4669_o;
  reg n4673_o;
  reg n4677_o;
  reg n4681_o;
  reg n4684_o;
  reg n4687_o;
  reg n4691_o;
  reg n4695_o;
  reg n4699_o;
  reg [1:0] n4702_o;
  reg n4706_o;
  reg n4710_o;
  reg n4714_o;
  reg n4717_o;
  reg n4720_o;
  reg n4723_o;
  wire n4725_o;
  wire [2:0] n4726_o;
  wire [2:0] n4727_o;
  wire n4729_o;
  wire n4731_o;
  wire n4734_o;
  wire [3:0] n4736_o;
  wire n4739_o;
  wire [30:0] n4740_o;
  wire n4742_o;
  wire n4744_o;
  wire n4745_o;
  wire n4747_o;
  wire n4749_o;
  wire [2:0] n4750_o;
  reg [2:0] n4752_o;
  reg n4755_o;
  reg [3:0] n4757_o;
  reg n4760_o;
  reg [2:0] n4764_o;
  reg n4767_o;
  wire [2:0] n4770_o;
  wire [2:0] n4771_o;
  wire n4772_o;
  wire [3:0] n4773_o;
  wire n4774_o;
  wire [2:0] n4776_o;
  wire n4778_o;
  wire n4781_o;
  wire n4783_o;
  wire n4785_o;
  wire n4786_o;
  wire n4788_o;
  wire n4789_o;
  wire n4791_o;
  wire n4792_o;
  wire n4794_o;
  wire n4795_o;
  wire n4797_o;
  wire n4798_o;
  wire n4800_o;
  wire n4801_o;
  wire n4803_o;
  wire n4804_o;
  wire n4806_o;
  wire n4807_o;
  wire n4809_o;
  wire n4810_o;
  wire n4812_o;
  wire n4813_o;
  wire n4815_o;
  wire n4816_o;
  wire n4818_o;
  wire n4819_o;
  wire n4821_o;
  wire n4822_o;
  wire n4824_o;
  wire n4825_o;
  wire n4827_o;
  wire n4828_o;
  wire n4830_o;
  wire n4831_o;
  wire n4833_o;
  wire n4834_o;
  wire n4836_o;
  wire n4837_o;
  wire n4839_o;
  wire n4840_o;
  wire n4842_o;
  wire n4843_o;
  wire n4845_o;
  wire n4846_o;
  wire n4848_o;
  wire n4849_o;
  wire n4851_o;
  wire n4852_o;
  wire n4854_o;
  wire n4855_o;
  wire n4857_o;
  wire n4858_o;
  wire n4860_o;
  wire n4861_o;
  wire n4863_o;
  wire n4864_o;
  wire n4866_o;
  wire n4867_o;
  wire n4869_o;
  wire n4870_o;
  wire n4872_o;
  wire n4873_o;
  wire n4875_o;
  wire n4876_o;
  wire n4878_o;
  wire n4879_o;
  wire n4881_o;
  wire n4882_o;
  wire n4884_o;
  wire n4885_o;
  wire n4887_o;
  wire n4888_o;
  wire n4890_o;
  wire n4891_o;
  wire n4893_o;
  wire n4894_o;
  wire n4896_o;
  wire n4897_o;
  wire n4899_o;
  wire n4900_o;
  wire n4902_o;
  wire n4903_o;
  wire n4905_o;
  wire n4906_o;
  wire n4908_o;
  wire n4909_o;
  wire n4911_o;
  wire n4912_o;
  wire n4914_o;
  wire n4915_o;
  wire n4917_o;
  wire n4918_o;
  wire n4920_o;
  wire n4921_o;
  wire n4923_o;
  wire n4924_o;
  wire n4926_o;
  wire n4927_o;
  wire n4929_o;
  wire n4930_o;
  wire n4932_o;
  wire n4933_o;
  wire n4935_o;
  wire n4936_o;
  wire n4938_o;
  wire n4939_o;
  wire n4941_o;
  wire n4942_o;
  wire n4944_o;
  wire n4945_o;
  wire n4947_o;
  wire n4948_o;
  wire [30:0] n4949_o;
  wire n4951_o;
  wire n4953_o;
  wire n4954_o;
  wire n4956_o;
  wire n4958_o;
  wire [2:0] n4959_o;
  reg [2:0] n4961_o;
  reg n4964_o;
  reg [3:0] n4966_o;
  reg n4969_o;
  reg [2:0] n4973_o;
  reg n4976_o;
  wire n4978_o;
  wire n4980_o;
  wire n4981_o;
  wire n4983_o;
  wire n4984_o;
  wire n4986_o;
  wire n4987_o;
  wire n4989_o;
  wire n4990_o;
  wire n4992_o;
  wire n4993_o;
  wire n4995_o;
  wire n4996_o;
  wire n4998_o;
  wire n4999_o;
  wire n5001_o;
  wire n5003_o;
  wire [2:0] n5004_o;
  wire [2:0] n5005_o;
  wire [3:0] n5007_o;
  wire [30:0] n5008_o;
  wire n5010_o;
  wire n5012_o;
  wire n5013_o;
  wire n5015_o;
  wire [1:0] n5016_o;
  reg [2:0] n5018_o;
  reg [3:0] n5020_o;
  reg [2:0] n5023_o;
  wire [2:0] n5026_o;
  wire [2:0] n5027_o;
  wire n5028_o;
  wire [3:0] n5029_o;
  wire [2:0] n5031_o;
  wire n5034_o;
  wire n5036_o;
  wire n5038_o;
  wire n5039_o;
  wire n5041_o;
  wire n5042_o;
  wire n5044_o;
  wire n5045_o;
  wire n5047_o;
  wire n5048_o;
  wire n5050_o;
  wire n5051_o;
  wire n5053_o;
  wire n5054_o;
  wire n5056_o;
  wire n5057_o;
  wire n5059_o;
  wire n5060_o;
  wire n5062_o;
  wire n5063_o;
  wire n5065_o;
  wire n5066_o;
  wire n5068_o;
  wire n5069_o;
  wire n5071_o;
  wire n5072_o;
  wire n5074_o;
  wire n5075_o;
  wire n5077_o;
  wire n5078_o;
  wire n5080_o;
  wire n5081_o;
  wire n5083_o;
  wire n5084_o;
  wire n5086_o;
  wire n5087_o;
  wire n5089_o;
  wire n5090_o;
  wire n5092_o;
  wire n5093_o;
  wire n5095_o;
  wire n5096_o;
  wire n5098_o;
  wire n5099_o;
  wire n5101_o;
  wire n5102_o;
  wire n5104_o;
  wire n5105_o;
  wire n5107_o;
  wire n5108_o;
  wire n5110_o;
  wire n5111_o;
  wire n5113_o;
  wire n5114_o;
  wire n5116_o;
  wire n5117_o;
  wire n5119_o;
  wire n5120_o;
  wire n5122_o;
  wire n5123_o;
  wire n5125_o;
  wire n5126_o;
  wire n5128_o;
  wire n5129_o;
  wire n5131_o;
  wire n5132_o;
  wire n5134_o;
  wire n5135_o;
  wire n5137_o;
  wire n5138_o;
  wire n5140_o;
  wire n5141_o;
  wire n5143_o;
  wire n5144_o;
  wire n5146_o;
  wire n5147_o;
  wire n5149_o;
  wire n5150_o;
  wire n5152_o;
  wire n5153_o;
  wire n5155_o;
  wire n5156_o;
  wire n5158_o;
  wire n5159_o;
  wire n5161_o;
  wire n5162_o;
  wire n5164_o;
  wire n5165_o;
  wire n5167_o;
  wire n5168_o;
  wire n5170_o;
  wire n5171_o;
  wire n5173_o;
  wire n5174_o;
  wire n5176_o;
  wire n5177_o;
  wire n5179_o;
  wire n5180_o;
  wire n5182_o;
  wire n5183_o;
  wire n5185_o;
  wire n5186_o;
  wire n5188_o;
  wire n5189_o;
  wire n5191_o;
  wire n5192_o;
  wire n5194_o;
  wire n5195_o;
  wire n5197_o;
  wire n5198_o;
  wire n5200_o;
  wire n5201_o;
  wire [30:0] n5202_o;
  wire n5204_o;
  wire n5206_o;
  wire n5207_o;
  wire n5209_o;
  wire [1:0] n5210_o;
  reg [2:0] n5212_o;
  reg [3:0] n5214_o;
  reg [2:0] n5217_o;
  wire n5219_o;
  wire n5221_o;
  wire n5222_o;
  wire n5224_o;
  wire n5225_o;
  wire n5227_o;
  wire n5228_o;
  wire n5230_o;
  wire n5231_o;
  wire n5233_o;
  wire n5234_o;
  wire n5236_o;
  wire n5237_o;
  wire n5239_o;
  wire n5240_o;
  wire n5242_o;
  wire n5244_o;
  wire n5247_o;
  wire [3:0] n5249_o;
  wire n5252_o;
  wire [30:0] n5253_o;
  wire n5255_o;
  wire n5257_o;
  wire n5258_o;
  wire n5260_o;
  wire n5262_o;
  wire [2:0] n5263_o;
  reg [2:0] n5265_o;
  reg n5268_o;
  reg [3:0] n5270_o;
  reg n5273_o;
  reg [2:0] n5277_o;
  reg n5280_o;
  wire [2:0] n5283_o;
  wire [2:0] n5284_o;
  wire n5285_o;
  wire [3:0] n5286_o;
  wire n5287_o;
  wire [2:0] n5289_o;
  wire n5291_o;
  wire n5294_o;
  wire n5296_o;
  wire n5298_o;
  wire n5299_o;
  wire n5301_o;
  wire n5302_o;
  wire n5304_o;
  wire n5305_o;
  wire n5307_o;
  wire n5308_o;
  wire n5310_o;
  wire n5311_o;
  wire n5313_o;
  wire n5314_o;
  wire n5316_o;
  wire n5317_o;
  wire n5319_o;
  wire n5320_o;
  wire n5322_o;
  wire n5323_o;
  wire n5325_o;
  wire n5326_o;
  wire n5328_o;
  wire n5329_o;
  wire n5331_o;
  wire n5332_o;
  wire n5334_o;
  wire n5335_o;
  wire n5337_o;
  wire n5338_o;
  wire n5340_o;
  wire n5341_o;
  wire n5343_o;
  wire n5344_o;
  wire n5346_o;
  wire n5347_o;
  wire n5349_o;
  wire n5350_o;
  wire n5352_o;
  wire n5353_o;
  wire n5355_o;
  wire n5356_o;
  wire n5358_o;
  wire n5359_o;
  wire n5361_o;
  wire n5362_o;
  wire n5364_o;
  wire n5365_o;
  wire n5367_o;
  wire n5368_o;
  wire n5370_o;
  wire n5371_o;
  wire n5373_o;
  wire n5374_o;
  wire n5376_o;
  wire n5377_o;
  wire n5379_o;
  wire n5380_o;
  wire n5382_o;
  wire n5383_o;
  wire n5385_o;
  wire n5386_o;
  wire n5388_o;
  wire n5389_o;
  wire n5391_o;
  wire n5392_o;
  wire n5394_o;
  wire n5395_o;
  wire n5397_o;
  wire n5398_o;
  wire n5400_o;
  wire n5401_o;
  wire n5403_o;
  wire n5404_o;
  wire n5406_o;
  wire n5407_o;
  wire n5409_o;
  wire n5410_o;
  wire n5412_o;
  wire n5413_o;
  wire n5415_o;
  wire n5416_o;
  wire n5418_o;
  wire n5419_o;
  wire n5421_o;
  wire n5422_o;
  wire n5424_o;
  wire n5425_o;
  wire n5427_o;
  wire n5428_o;
  wire n5430_o;
  wire n5431_o;
  wire n5433_o;
  wire n5434_o;
  wire n5436_o;
  wire n5437_o;
  wire n5439_o;
  wire n5440_o;
  wire n5442_o;
  wire n5443_o;
  wire n5445_o;
  wire n5446_o;
  wire n5448_o;
  wire n5449_o;
  wire n5451_o;
  wire n5452_o;
  wire n5454_o;
  wire n5455_o;
  wire n5457_o;
  wire n5458_o;
  wire n5460_o;
  wire n5461_o;
  wire [30:0] n5462_o;
  wire n5464_o;
  wire n5466_o;
  wire n5467_o;
  wire n5469_o;
  wire n5471_o;
  wire [2:0] n5472_o;
  reg [2:0] n5474_o;
  reg n5477_o;
  reg [3:0] n5479_o;
  reg n5482_o;
  reg [2:0] n5486_o;
  reg n5489_o;
  wire n5491_o;
  wire n5493_o;
  wire n5494_o;
  wire n5496_o;
  wire n5497_o;
  wire n5499_o;
  wire n5500_o;
  wire n5502_o;
  wire n5503_o;
  wire n5505_o;
  wire n5506_o;
  wire n5508_o;
  wire n5509_o;
  wire n5511_o;
  wire n5512_o;
  wire n5514_o;
  wire n5516_o;
  wire n5519_o;
  wire [3:0] n5521_o;
  wire n5524_o;
  wire [30:0] n5525_o;
  wire n5527_o;
  wire n5529_o;
  wire n5530_o;
  wire n5532_o;
  wire n5534_o;
  wire [2:0] n5535_o;
  reg [2:0] n5537_o;
  reg n5540_o;
  reg [3:0] n5542_o;
  reg n5545_o;
  reg [2:0] n5549_o;
  reg n5552_o;
  wire [2:0] n5555_o;
  wire [2:0] n5556_o;
  wire n5557_o;
  wire [3:0] n5558_o;
  wire n5559_o;
  wire [2:0] n5561_o;
  wire n5563_o;
  wire n5566_o;
  wire n5568_o;
  wire n5570_o;
  wire n5571_o;
  wire n5573_o;
  wire n5574_o;
  wire n5576_o;
  wire n5577_o;
  wire n5579_o;
  wire n5580_o;
  wire n5582_o;
  wire n5583_o;
  wire n5585_o;
  wire n5586_o;
  wire n5588_o;
  wire n5589_o;
  wire n5591_o;
  wire n5592_o;
  wire n5594_o;
  wire n5595_o;
  wire n5597_o;
  wire n5598_o;
  wire n5600_o;
  wire n5601_o;
  wire n5603_o;
  wire n5604_o;
  wire n5606_o;
  wire n5607_o;
  wire n5609_o;
  wire n5610_o;
  wire n5612_o;
  wire n5613_o;
  wire n5615_o;
  wire n5616_o;
  wire n5618_o;
  wire n5619_o;
  wire n5621_o;
  wire n5622_o;
  wire n5624_o;
  wire n5625_o;
  wire n5627_o;
  wire n5628_o;
  wire n5630_o;
  wire n5631_o;
  wire n5633_o;
  wire n5634_o;
  wire n5636_o;
  wire n5637_o;
  wire n5639_o;
  wire n5640_o;
  wire n5642_o;
  wire n5643_o;
  wire n5645_o;
  wire n5646_o;
  wire n5648_o;
  wire n5649_o;
  wire n5651_o;
  wire n5652_o;
  wire n5654_o;
  wire n5655_o;
  wire n5657_o;
  wire n5658_o;
  wire n5660_o;
  wire n5661_o;
  wire n5663_o;
  wire n5664_o;
  wire n5666_o;
  wire n5667_o;
  wire n5669_o;
  wire n5670_o;
  wire n5672_o;
  wire n5673_o;
  wire n5675_o;
  wire n5676_o;
  wire n5678_o;
  wire n5679_o;
  wire n5681_o;
  wire n5682_o;
  wire n5684_o;
  wire n5685_o;
  wire n5687_o;
  wire n5688_o;
  wire n5690_o;
  wire n5691_o;
  wire n5693_o;
  wire n5694_o;
  wire n5696_o;
  wire n5697_o;
  wire n5699_o;
  wire n5700_o;
  wire n5702_o;
  wire n5703_o;
  wire n5705_o;
  wire n5706_o;
  wire n5708_o;
  wire n5709_o;
  wire n5711_o;
  wire n5712_o;
  wire n5714_o;
  wire n5715_o;
  wire n5717_o;
  wire n5718_o;
  wire n5720_o;
  wire n5721_o;
  wire n5723_o;
  wire n5724_o;
  wire n5726_o;
  wire n5727_o;
  wire n5729_o;
  wire n5730_o;
  wire n5732_o;
  wire n5733_o;
  wire [30:0] n5734_o;
  wire n5736_o;
  wire n5738_o;
  wire n5739_o;
  wire n5741_o;
  wire n5743_o;
  wire [2:0] n5744_o;
  reg [2:0] n5746_o;
  reg n5749_o;
  reg [3:0] n5751_o;
  reg n5754_o;
  reg [2:0] n5758_o;
  reg n5761_o;
  wire n5763_o;
  wire n5765_o;
  wire n5766_o;
  wire n5768_o;
  wire n5769_o;
  wire n5771_o;
  wire n5772_o;
  wire n5774_o;
  wire n5775_o;
  wire n5777_o;
  wire n5778_o;
  wire n5780_o;
  wire n5781_o;
  wire n5783_o;
  wire n5784_o;
  wire [7:0] n5785_o;
  reg [2:0] n5791_o;
  reg [2:0] n5793_o;
  reg n5796_o;
  reg [2:0] n5798_o;
  reg [3:0] n5800_o;
  reg n5803_o;
  reg [2:0] n5805_o;
  reg n5808_o;
  reg n5811_o;
  wire n5813_o;
  wire n5815_o;
  wire n5817_o;
  wire n5818_o;
  wire n5820_o;
  wire n5821_o;
  wire n5823_o;
  wire n5824_o;
  wire n5826_o;
  wire n5827_o;
  wire n5829_o;
  wire n5830_o;
  wire n5832_o;
  wire n5833_o;
  wire n5835_o;
  wire n5836_o;
  wire n5838_o;
  wire n5839_o;
  wire n5841_o;
  wire n5842_o;
  wire n5844_o;
  wire n5845_o;
  wire n5847_o;
  wire n5848_o;
  wire n5850_o;
  wire n5851_o;
  wire n5853_o;
  wire n5854_o;
  wire n5856_o;
  wire n5857_o;
  wire n5859_o;
  wire n5860_o;
  wire n5862_o;
  wire n5863_o;
  wire n5865_o;
  wire n5866_o;
  wire n5868_o;
  wire n5869_o;
  wire n5871_o;
  wire n5872_o;
  wire n5874_o;
  wire n5875_o;
  wire n5877_o;
  wire n5878_o;
  wire n5880_o;
  wire n5881_o;
  wire n5883_o;
  wire n5884_o;
  wire n5886_o;
  wire n5887_o;
  wire n5889_o;
  wire n5890_o;
  wire n5892_o;
  wire n5893_o;
  wire n5895_o;
  wire n5896_o;
  wire n5898_o;
  wire n5899_o;
  wire n5901_o;
  wire n5902_o;
  wire n5904_o;
  wire n5905_o;
  wire n5907_o;
  wire n5908_o;
  wire n5910_o;
  wire n5911_o;
  wire n5913_o;
  wire n5914_o;
  wire n5916_o;
  wire n5917_o;
  wire n5919_o;
  wire n5920_o;
  wire n5922_o;
  wire n5923_o;
  wire n5925_o;
  wire n5926_o;
  wire n5928_o;
  wire n5929_o;
  wire n5931_o;
  wire n5932_o;
  wire n5934_o;
  wire n5935_o;
  wire n5937_o;
  wire n5938_o;
  wire n5940_o;
  wire n5941_o;
  wire n5943_o;
  wire n5944_o;
  wire n5946_o;
  wire n5947_o;
  wire n5949_o;
  wire n5950_o;
  wire n5952_o;
  wire n5953_o;
  wire n5955_o;
  wire n5956_o;
  wire n5958_o;
  wire n5959_o;
  wire n5961_o;
  wire n5962_o;
  wire n5964_o;
  wire n5965_o;
  wire n5967_o;
  wire n5968_o;
  wire n5970_o;
  wire n5971_o;
  wire n5973_o;
  wire n5974_o;
  wire n5976_o;
  wire n5977_o;
  wire n5979_o;
  wire n5980_o;
  wire n5982_o;
  wire n5983_o;
  wire n5985_o;
  wire n5986_o;
  wire n5988_o;
  wire n5989_o;
  wire n5991_o;
  wire n5992_o;
  wire n5994_o;
  wire n5995_o;
  wire n5997_o;
  wire n5998_o;
  wire n6000_o;
  wire n6001_o;
  wire n6003_o;
  wire n6004_o;
  wire n6006_o;
  wire n6007_o;
  wire n6009_o;
  wire n6010_o;
  wire n6012_o;
  wire n6013_o;
  wire n6015_o;
  wire n6016_o;
  wire n6018_o;
  wire n6019_o;
  wire n6021_o;
  wire n6022_o;
  wire n6024_o;
  wire n6025_o;
  wire n6027_o;
  wire n6028_o;
  wire n6030_o;
  wire n6031_o;
  wire n6033_o;
  wire n6034_o;
  wire n6036_o;
  wire n6037_o;
  wire n6039_o;
  wire n6040_o;
  wire n6042_o;
  wire n6043_o;
  wire n6045_o;
  wire n6046_o;
  wire n6048_o;
  wire n6049_o;
  wire n6051_o;
  wire n6052_o;
  wire n6054_o;
  wire n6055_o;
  wire n6057_o;
  wire n6058_o;
  wire n6060_o;
  wire n6061_o;
  wire n6063_o;
  wire n6064_o;
  wire n6066_o;
  wire n6067_o;
  wire n6069_o;
  wire n6070_o;
  wire n6072_o;
  wire n6073_o;
  wire n6075_o;
  wire n6076_o;
  wire n6078_o;
  wire n6079_o;
  wire n6081_o;
  wire n6082_o;
  wire n6084_o;
  wire n6085_o;
  wire n6087_o;
  wire n6088_o;
  wire n6090_o;
  wire n6091_o;
  wire n6093_o;
  wire n6094_o;
  wire n6096_o;
  wire n6097_o;
  wire n6099_o;
  wire n6100_o;
  wire n6102_o;
  wire n6103_o;
  wire n6105_o;
  wire n6106_o;
  wire n6108_o;
  wire n6109_o;
  wire n6111_o;
  wire n6112_o;
  wire n6114_o;
  wire n6115_o;
  wire n6117_o;
  wire n6118_o;
  wire n6120_o;
  wire n6121_o;
  wire n6123_o;
  wire n6124_o;
  wire n6126_o;
  wire n6127_o;
  wire n6129_o;
  wire n6130_o;
  wire n6132_o;
  wire n6133_o;
  wire n6135_o;
  wire n6136_o;
  wire n6138_o;
  wire n6139_o;
  wire n6141_o;
  wire n6142_o;
  wire n6144_o;
  wire n6145_o;
  wire n6147_o;
  wire n6148_o;
  wire n6150_o;
  wire n6151_o;
  wire n6153_o;
  wire n6154_o;
  wire n6156_o;
  wire n6157_o;
  wire n6159_o;
  wire n6160_o;
  wire n6162_o;
  wire n6163_o;
  wire n6165_o;
  wire n6166_o;
  wire n6168_o;
  wire n6169_o;
  wire n6171_o;
  wire n6172_o;
  wire n6174_o;
  wire n6175_o;
  wire n6177_o;
  wire n6178_o;
  wire n6180_o;
  wire n6181_o;
  wire n6183_o;
  wire n6184_o;
  wire n6186_o;
  wire n6187_o;
  wire n6189_o;
  wire n6190_o;
  wire n6192_o;
  wire n6193_o;
  wire n6195_o;
  wire n6196_o;
  wire n6198_o;
  wire n6199_o;
  wire n6201_o;
  wire n6202_o;
  wire n6204_o;
  wire n6205_o;
  wire n6207_o;
  wire n6208_o;
  wire n6210_o;
  wire n6211_o;
  wire n6213_o;
  wire n6214_o;
  wire n6216_o;
  wire n6217_o;
  wire n6219_o;
  wire n6220_o;
  wire n6222_o;
  wire n6223_o;
  wire n6225_o;
  wire n6226_o;
  wire n6228_o;
  wire n6229_o;
  wire n6231_o;
  wire n6232_o;
  wire n6234_o;
  wire n6235_o;
  wire n6237_o;
  wire n6238_o;
  wire n6240_o;
  wire n6241_o;
  wire n6243_o;
  wire n6244_o;
  wire n6246_o;
  wire n6247_o;
  wire n6249_o;
  wire n6250_o;
  wire n6252_o;
  wire n6253_o;
  wire n6255_o;
  wire n6256_o;
  wire n6258_o;
  wire n6259_o;
  wire n6261_o;
  wire n6262_o;
  wire n6264_o;
  wire n6265_o;
  wire n6267_o;
  wire n6268_o;
  wire n6270_o;
  wire n6271_o;
  wire n6273_o;
  wire n6274_o;
  wire n6276_o;
  wire n6277_o;
  wire n6279_o;
  wire n6280_o;
  wire n6282_o;
  wire n6283_o;
  wire n6285_o;
  wire n6286_o;
  wire n6288_o;
  wire n6289_o;
  wire n6291_o;
  wire n6292_o;
  wire n6294_o;
  wire n6295_o;
  wire n6297_o;
  wire n6298_o;
  wire n6300_o;
  wire n6301_o;
  wire n6303_o;
  wire n6304_o;
  wire n6306_o;
  wire n6307_o;
  wire n6309_o;
  wire n6310_o;
  wire n6312_o;
  wire n6313_o;
  wire n6315_o;
  wire n6316_o;
  wire n6318_o;
  wire n6319_o;
  wire n6321_o;
  wire n6322_o;
  wire n6324_o;
  wire n6326_o;
  wire n6327_o;
  wire n6329_o;
  wire n6331_o;
  wire n6333_o;
  wire n6335_o;
  wire [30:0] n6336_o;
  wire n6338_o;
  wire n6340_o;
  wire [1:0] n6341_o;
  wire n6343_o;
  wire [1:0] n6344_o;
  wire [2:0] n6346_o;
  wire [2:0] n6348_o;
  wire n6351_o;
  wire n6353_o;
  wire [1:0] n6354_o;
  wire n6356_o;
  wire [1:0] n6357_o;
  wire [2:0] n6359_o;
  wire [2:0] n6361_o;
  wire n6364_o;
  wire n6366_o;
  wire [3:0] n6367_o;
  reg n6371_o;
  reg n6374_o;
  reg n6378_o;
  reg [2:0] n6380_o;
  reg n6382_o;
  reg [2:0] n6386_o;
  reg n6389_o;
  reg n6392_o;
  wire n6394_o;
  wire n6396_o;
  wire n6397_o;
  wire n6399_o;
  wire n6400_o;
  wire n6402_o;
  wire n6403_o;
  wire [30:0] n6404_o;
  wire n6406_o;
  wire [1:0] n6407_o;
  wire n6409_o;
  wire [1:0] n6410_o;
  wire [3:0] n6413_o;
  wire [3:0] n6415_o;
  wire n6417_o;
  wire [1:0] n6418_o;
  wire n6420_o;
  wire [1:0] n6421_o;
  wire [3:0] n6424_o;
  wire [3:0] n6426_o;
  wire n6428_o;
  wire n6430_o;
  wire [3:0] n6431_o;
  reg n6435_o;
  reg n6438_o;
  reg [3:0] n6440_o;
  reg [2:0] n6444_o;
  reg n6447_o;
  reg n6450_o;
  reg n6454_o;
  wire n6456_o;
  wire n6458_o;
  wire n6459_o;
  wire n6461_o;
  wire n6462_o;
  wire n6464_o;
  wire n6465_o;
  wire [30:0] n6466_o;
  wire n6468_o;
  wire n6470_o;
  wire n6471_o;
  wire [3:0] n6474_o;
  wire n6476_o;
  wire n6477_o;
  wire n6478_o;
  wire [3:0] n6481_o;
  wire n6483_o;
  wire n6485_o;
  wire [3:0] n6486_o;
  reg [2:0] n6489_o;
  reg [3:0] n6492_o;
  reg [2:0] n6494_o;
  reg [3:0] n6497_o;
  reg [3:0] n6499_o;
  reg [2:0] n6503_o;
  reg n6506_o;
  reg n6509_o;
  reg n6512_o;
  reg n6515_o;
  wire n6517_o;
  wire n6519_o;
  wire n6520_o;
  wire n6522_o;
  wire n6523_o;
  wire n6525_o;
  wire n6526_o;
  wire [30:0] n6527_o;
  wire n6529_o;
  wire n6531_o;
  wire n6532_o;
  wire [3:0] n6535_o;
  wire n6537_o;
  wire n6539_o;
  wire n6541_o;
  wire [3:0] n6542_o;
  reg [2:0] n6545_o;
  reg [3:0] n6548_o;
  reg [2:0] n6550_o;
  reg [3:0] n6553_o;
  reg [3:0] n6555_o;
  reg n6558_o;
  reg n6561_o;
  reg [2:0] n6564_o;
  reg n6567_o;
  reg n6571_o;
  reg n6575_o;
  wire n6577_o;
  wire n6579_o;
  wire n6580_o;
  wire n6582_o;
  wire n6583_o;
  wire n6585_o;
  wire n6586_o;
  wire n6588_o;
  wire n6590_o;
  wire n6591_o;
  wire n6593_o;
  wire n6594_o;
  wire n6596_o;
  wire n6597_o;
  wire n6599_o;
  wire n6600_o;
  wire n6602_o;
  wire n6603_o;
  wire n6605_o;
  wire n6606_o;
  wire n6608_o;
  wire n6609_o;
  wire n6611_o;
  wire n6613_o;
  wire n6614_o;
  wire n6616_o;
  wire n6617_o;
  wire n6619_o;
  wire n6620_o;
  wire n6622_o;
  wire n6624_o;
  wire n6625_o;
  wire n6627_o;
  wire n6629_o;
  wire n6630_o;
  wire [30:0] n6631_o;
  wire n6633_o;
  wire [1:0] n6635_o;
  wire [30:0] n6636_o;
  wire [1:0] n6637_o;
  wire n6640_o;
  wire n6642_o;
  wire n6643_o;
  wire n6645_o;
  wire n6646_o;
  reg n6648_o;
  reg [1:0] n6650_o;
  reg n6653_o;
  wire n6655_o;
  wire [1:0] n6657_o;
  wire [30:0] n6658_o;
  wire [1:0] n6659_o;
  wire n6662_o;
  wire n6664_o;
  wire n6665_o;
  wire n6667_o;
  wire n6668_o;
  reg n6670_o;
  reg [1:0] n6672_o;
  reg n6675_o;
  wire n6677_o;
  wire [2:0] n6678_o;
  reg [2:0] n6680_o;
  reg n6684_o;
  reg [2:0] n6686_o;
  reg n6688_o;
  reg [1:0] n6690_o;
  reg n6692_o;
  reg [3:0] n6695_o;
  reg n6699_o;
  reg [1:0] n6702_o;
  reg n6706_o;
  reg n6710_o;
  wire n6712_o;
  wire n6714_o;
  wire n6715_o;
  wire n6717_o;
  wire n6718_o;
  wire n6720_o;
  wire n6721_o;
  wire [30:0] n6722_o;
  wire n6724_o;
  wire [1:0] n6726_o;
  wire [30:0] n6727_o;
  wire [1:0] n6728_o;
  wire n6731_o;
  wire n6733_o;
  wire n6734_o;
  wire n6736_o;
  wire n6737_o;
  reg n6739_o;
  reg [1:0] n6741_o;
  reg n6744_o;
  wire n6746_o;
  wire [1:0] n6748_o;
  wire [30:0] n6749_o;
  wire [1:0] n6750_o;
  wire n6752_o;
  wire n6754_o;
  wire n6755_o;
  wire n6757_o;
  wire n6758_o;
  reg n6761_o;
  reg [1:0] n6763_o;
  reg n6766_o;
  wire n6768_o;
  wire [2:0] n6769_o;
  reg [2:0] n6771_o;
  reg n6775_o;
  reg [2:0] n6777_o;
  reg n6779_o;
  reg [1:0] n6781_o;
  reg n6783_o;
  reg [3:0] n6786_o;
  reg n6790_o;
  reg [1:0] n6793_o;
  reg n6797_o;
  reg n6801_o;
  wire n6803_o;
  wire n6805_o;
  wire n6806_o;
  wire n6808_o;
  wire n6809_o;
  wire n6811_o;
  wire n6812_o;
  wire [30:0] n6813_o;
  wire n6815_o;
  wire n6819_o;
  wire n6821_o;
  wire n6823_o;
  wire [3:0] n6824_o;
  reg [2:0] n6826_o;
  reg n6829_o;
  reg [2:0] n6831_o;
  reg [2:0] n6833_o;
  reg [3:0] n6835_o;
  reg n6838_o;
  reg [2:0] n6842_o;
  reg n6845_o;
  reg n6848_o;
  reg n6851_o;
  reg n6854_o;
  wire n6856_o;
  wire [30:0] n6857_o;
  wire n6859_o;
  wire n6863_o;
  wire n6865_o;
  wire n6867_o;
  wire [3:0] n6868_o;
  reg [2:0] n6870_o;
  reg n6873_o;
  reg [2:0] n6875_o;
  reg [2:0] n6877_o;
  reg [3:0] n6879_o;
  reg n6882_o;
  reg [2:0] n6886_o;
  reg n6889_o;
  reg n6892_o;
  reg n6895_o;
  reg n6898_o;
  wire n6900_o;
  wire [30:0] n6901_o;
  wire n6903_o;
  wire n6905_o;
  wire n6907_o;
  wire [2:0] n6908_o;
  reg [3:0] n6912_o;
  reg [2:0] n6916_o;
  reg n6919_o;
  reg n6922_o;
  reg n6925_o;
  reg n6928_o;
  wire n6930_o;
  wire n6932_o;
  wire n6933_o;
  wire n6935_o;
  wire n6936_o;
  wire n6938_o;
  wire n6939_o;
  wire n6941_o;
  wire n6942_o;
  wire n6944_o;
  wire n6945_o;
  wire n6947_o;
  wire n6948_o;
  wire n6950_o;
  wire n6951_o;
  wire [30:0] n6952_o;
  wire n6954_o;
  wire [2:0] n6955_o;
  wire n6957_o;
  wire [2:0] n6958_o;
  wire n6961_o;
  wire [2:0] n6963_o;
  wire n6965_o;
  wire [1:0] n6966_o;
  reg n6968_o;
  reg [2:0] n6970_o;
  reg [2:0] n6973_o;
  reg n6976_o;
  reg n6979_o;
  reg [1:0] n6982_o;
  wire n6984_o;
  wire n6986_o;
  wire n6987_o;
  wire n6989_o;
  wire n6990_o;
  wire n6992_o;
  wire n6993_o;
  wire n6995_o;
  wire n6996_o;
  wire n6998_o;
  wire n6999_o;
  wire n7001_o;
  wire n7002_o;
  wire n7004_o;
  wire n7005_o;
  wire [30:0] n7006_o;
  wire [2:0] n7007_o;
  wire [2:0] n7008_o;
  wire n7010_o;
  wire n7013_o;
  wire n7015_o;
  wire n7017_o;
  wire [1:0] n7018_o;
  reg [2:0] n7020_o;
  reg n7022_o;
  reg [2:0] n7025_o;
  reg n7028_o;
  reg [1:0] n7031_o;
  reg n7034_o;
  wire n7036_o;
  wire n7038_o;
  wire n7039_o;
  wire n7041_o;
  wire n7042_o;
  wire n7044_o;
  wire n7045_o;
  wire n7047_o;
  wire n7048_o;
  wire n7050_o;
  wire n7051_o;
  wire n7053_o;
  wire n7054_o;
  wire n7056_o;
  wire n7057_o;
  wire [30:0] n7058_o;
  wire n7059_o;
  wire n7061_o;
  wire n7063_o;
  wire n7064_o;
  wire n7065_o;
  wire [3:0] n7068_o;
  wire n7070_o;
  wire n7072_o;
  wire [3:0] n7073_o;
  reg [2:0] n7076_o;
  wire [2:0] n7077_o;
  reg [2:0] n7079_o;
  wire n7080_o;
  reg n7082_o;
  reg n7085_o;
  reg [3:0] n7088_o;
  reg [3:0] n7092_o;
  reg [3:0] n7094_o;
  reg n7097_o;
  reg [2:0] n7101_o;
  reg n7104_o;
  reg n7107_o;
  reg [1:0] n7110_o;
  reg n7113_o;
  reg n7116_o;
  wire n7118_o;
  wire n7120_o;
  wire n7121_o;
  wire n7123_o;
  wire n7124_o;
  wire n7126_o;
  wire n7127_o;
  wire [30:0] n7128_o;
  wire n7130_o;
  wire n7131_o;
  wire n7133_o;
  wire n7134_o;
  wire n7135_o;
  wire [3:0] n7138_o;
  wire n7140_o;
  wire n7142_o;
  wire [3:0] n7143_o;
  reg [2:0] n7146_o;
  wire [2:0] n7147_o;
  reg [2:0] n7149_o;
  wire n7150_o;
  reg n7152_o;
  reg n7155_o;
  reg [3:0] n7158_o;
  reg [3:0] n7162_o;
  reg [3:0] n7164_o;
  reg n7167_o;
  reg [2:0] n7171_o;
  reg n7174_o;
  reg n7177_o;
  reg [1:0] n7180_o;
  reg n7183_o;
  reg n7186_o;
  wire n7188_o;
  wire n7190_o;
  wire n7191_o;
  wire n7193_o;
  wire n7194_o;
  wire n7196_o;
  wire n7197_o;
  wire [30:0] n7198_o;
  wire [2:0] n7199_o;
  wire n7202_o;
  wire n7205_o;
  wire [1:0] n7206_o;
  reg [2:0] n7208_o;
  reg [2:0] n7210_o;
  reg n7212_o;
  reg n7215_o;
  reg n7218_o;
  reg n7222_o;
  wire [2:0] n7225_o;
  wire [2:0] n7227_o;
  wire [3:0] n7228_o;
  wire [3:0] n7230_o;
  wire n7232_o;
  wire n7234_o;
  wire n7236_o;
  wire n7238_o;
  wire n7240_o;
  wire n7241_o;
  wire n7243_o;
  wire n7244_o;
  wire n7246_o;
  wire n7247_o;
  wire [30:0] n7248_o;
  wire n7250_o;
  wire [3:0] n7253_o;
  wire [3:0] n7255_o;
  wire n7258_o;
  wire n7261_o;
  wire [1:0] n7262_o;
  reg [2:0] n7264_o;
  reg [2:0] n7266_o;
  reg [3:0] n7268_o;
  reg n7271_o;
  reg n7275_o;
  wire [2:0] n7278_o;
  wire [2:0] n7279_o;
  wire [2:0] n7281_o;
  wire [3:0] n7283_o;
  wire n7285_o;
  wire n7287_o;
  wire n7289_o;
  wire n7291_o;
  wire n7292_o;
  wire [24:0] n7293_o;
  reg [2:0] n7309_o;
  reg [2:0] n7315_o;
  reg n7318_o;
  reg n7321_o;
  wire [2:0] n7322_o;
  wire [2:0] n7323_o;
  wire [2:0] n7324_o;
  reg [2:0] n7327_o;
  wire n7328_o;
  wire n7329_o;
  wire n7330_o;
  reg n7333_o;
  reg n7336_o;
  reg n7340_o;
  wire [2:0] n7342_o;
  wire [2:0] n7343_o;
  reg [2:0] n7346_o;
  wire n7348_o;
  wire n7349_o;
  reg n7352_o;
  wire n7353_o;
  wire n7354_o;
  wire n7355_o;
  wire n7357_o;
  wire n7358_o;
  wire n7359_o;
  wire n7360_o;
  wire n7361_o;
  wire n7362_o;
  wire n7363_o;
  reg n7366_o;
  wire [1:0] n7367_o;
  wire [1:0] n7368_o;
  wire [1:0] n7369_o;
  wire [1:0] n7371_o;
  wire [1:0] n7372_o;
  wire [1:0] n7373_o;
  wire [1:0] n7374_o;
  wire [1:0] n7375_o;
  wire [1:0] n7376_o;
  wire [1:0] n7377_o;
  reg [1:0] n7380_o;
  wire n7381_o;
  wire n7382_o;
  wire n7383_o;
  wire n7385_o;
  wire n7386_o;
  wire n7387_o;
  wire n7388_o;
  reg n7391_o;
  reg [3:0] n7394_o;
  reg n7398_o;
  reg n7401_o;
  reg [2:0] n7404_o;
  reg n7407_o;
  reg n7410_o;
  reg n7413_o;
  reg n7416_o;
  reg [2:0] n7423_o;
  reg n7426_o;
  reg n7429_o;
  reg n7432_o;
  reg n7435_o;
  reg n7438_o;
  reg n7441_o;
  reg n7444_o;
  reg n7447_o;
  reg n7450_o;
  reg [1:0] n7453_o;
  reg [1:0] n7459_o;
  reg n7462_o;
  reg n7465_o;
  reg n7468_o;
  wire [1:0] n7469_o;
  reg [2:0] n7470_o;
  reg [2:0] n7472_o;
  reg [1:0] n7474_o;
  reg n7477_o;
  reg n7480_o;
  wire [1:0] n7482_o;
  reg [1:0] n7484_o;
  wire n7485_o;
  wire n7486_o;
  reg n7488_o;
  wire n7489_o;
  reg n7491_o;
  reg n7495_o;
  reg n7498_o;
  reg [2:0] n7500_o;
  reg n7502_o;
  wire n7505_o;
  reg n7506_o;
  wire [1:0] n7507_o;
  reg [1:0] n7508_o;
  reg n7510_o;
  reg [3:0] n7514_o;
  reg n7515_o;
  reg n7518_o;
  reg n7521_o;
  reg n7524_o;
  reg [2:0] n7526_o;
  reg n7529_o;
  reg n7532_o;
  reg n7535_o;
  reg n7538_o;
  reg n7541_o;
  reg n7544_o;
  reg n7547_o;
  reg n7550_o;
  reg n7553_o;
  reg [2:0] n7556_o;
  reg n7559_o;
  reg n7562_o;
  reg n7565_o;
  reg n7568_o;
  reg n7571_o;
  reg n7574_o;
  reg n7577_o;
  reg n7580_o;
  reg n7583_o;
  reg n7586_o;
  reg n7589_o;
  reg n7592_o;
  reg n7595_o;
  reg n7598_o;
  reg n7601_o;
  reg n7604_o;
  reg n7607_o;
  reg n7610_o;
  reg [1:0] n7613_o;
  reg n7616_o;
  reg n7619_o;
  reg [1:0] n7622_o;
  reg n7625_o;
  reg n7628_o;
  reg n7630_o;
  reg n7633_o;
  reg n7636_o;
  wire n7639_o;
  wire n7641_o;
  wire n7643_o;
  wire n7644_o;
  wire [2:0] n7646_o;
  wire n7648_o;
  wire n7650_o;
  wire n7651_o;
  wire n7652_o;
  wire n7654_o;
  wire n7656_o;
  wire n7657_o;
  wire n7658_o;
  wire n7660_o;
  wire n7662_o;
  wire [2:0] n7664_o;
  wire n7667_o;
  wire n7669_o;
  wire n7670_o;
  wire n7672_o;
  wire n7674_o;
  wire [2:0] n7676_o;
  wire n7677_o;
  wire [3:0] n7678_o;
  wire [3:0] n7679_o;
  wire [3:0] n7680_o;
  wire n7681_o;
  wire n7682_o;
  localparam n7684_o = 1'b0;
  localparam n7685_o = 1'b0;
  wire [3:0] n7686_o;
  wire [3:0] n7687_o;
  assign mcycles = n7470_o; //(module output)
  assign tstates = n7676_o; //(module output)
  assign prefix = n7474_o; //(module output)
  assign inc_pc = n7672_o; //(module output)
  assign inc_wz = n7480_o; //(module output)
  assign incdec_16 = n7686_o; //(module output)
  assign read_to_reg = n7495_o; //(module output)
  assign read_to_acc = n7498_o; //(module output)
  assign set_busa_to = n7687_o; //(module output)
  assign set_busb_to = n7680_o; //(module output)
  assign alu_op = n7514_o; //(module output)
  assign save_alu = n7515_o; //(module output)
  assign rot_akku = n7518_o; //(module output)
  assign preservec = n7521_o; //(module output)
  assign arith16 = n7524_o; //(module output)
  assign set_addr_to = n7664_o; //(module output)
  assign iorq = n7529_o; //(module output)
  assign jump = n7532_o; //(module output)
  assign jumpe = n7535_o; //(module output)
  assign jumpxy = n7538_o; //(module output)
  assign call = n7541_o; //(module output)
  assign rstp = n7544_o; //(module output)
  assign ldz = n7547_o; //(module output)
  assign ldw = n7550_o; //(module output)
  assign ldsphl = n7553_o; //(module output)
  assign ldhlsp = n7684_o; //(module output)
  assign addspdd = n7685_o; //(module output)
  assign special_ld = n7556_o; //(module output)
  assign exchangedh = n7559_o; //(module output)
  assign exchangerp = n7562_o; //(module output)
  assign exchangeaf = n7565_o; //(module output)
  assign exchangers = n7568_o; //(module output)
  assign exchangewh = n7571_o; //(module output)
  assign i_djnz = n7574_o; //(module output)
  assign i_cpl = n7577_o; //(module output)
  assign i_ccf = n7580_o; //(module output)
  assign i_scf = n7583_o; //(module output)
  assign i_retn = n7586_o; //(module output)
  assign i_bt = n7589_o; //(module output)
  assign i_bc = n7592_o; //(module output)
  assign i_btr = n7595_o; //(module output)
  assign i_rld = n7598_o; //(module output)
  assign i_rrd = n7601_o; //(module output)
  assign i_inrc = n7604_o; //(module output)
  assign i_mulub = n7607_o; //(module output)
  assign i_mulu = n7610_o; //(module output)
  assign setwz = n7613_o; //(module output)
  assign setdi = n7616_o; //(module output)
  assign setei = n7619_o; //(module output)
  assign imode = n7622_o; //(module output)
  assign halt = n7625_o; //(module output)
  assign noread = n7682_o; //(module output)
  assign write = n7630_o; //(module output)
  assign no_pc = n7654_o; //(module output)
  assign xybit_undoc = n7636_o; //(module output)
  /* T80_MCode.vhd:200:26  */
  assign n2490_o = ir[5:3]; // extract
  /* T80_MCode.vhd:201:26  */
  assign n2491_o = ir[2:0]; // extract
  /* T80_MCode.vhd:202:28  */
  assign n2492_o = ir[5:4]; // extract
  /* T80_MCode.vhd:206:27  */
  assign n2494_o = mcycle == 3'b001;
  /* T80_MCode.vhd:206:17  */
  assign n2497_o = n2494_o ? 3'b100 : 3'b011;
  /* T80_MCode.vhd:219:35  */
  assign n2498_o = ir[5:3]; // extract
  /* T80_MCode.vhd:219:31  */
  assign n2500_o = {1'b0, n2498_o};
  /* T80_MCode.vhd:276:17  */
  assign n2502_o = ir == 8'b01000000;
  /* T80_MCode.vhd:276:32  */
  assign n2504_o = ir == 8'b01000001;
  /* T80_MCode.vhd:276:32  */
  assign n2505_o = n2502_o | n2504_o;
  /* T80_MCode.vhd:276:43  */
  assign n2507_o = ir == 8'b01000010;
  /* T80_MCode.vhd:276:43  */
  assign n2508_o = n2505_o | n2507_o;
  /* T80_MCode.vhd:276:54  */
  assign n2510_o = ir == 8'b01000011;
  /* T80_MCode.vhd:276:54  */
  assign n2511_o = n2508_o | n2510_o;
  /* T80_MCode.vhd:276:65  */
  assign n2513_o = ir == 8'b01000100;
  /* T80_MCode.vhd:276:65  */
  assign n2514_o = n2511_o | n2513_o;
  /* T80_MCode.vhd:276:76  */
  assign n2516_o = ir == 8'b01000101;
  /* T80_MCode.vhd:276:76  */
  assign n2517_o = n2514_o | n2516_o;
  /* T80_MCode.vhd:276:87  */
  assign n2519_o = ir == 8'b01000111;
  /* T80_MCode.vhd:276:87  */
  assign n2520_o = n2517_o | n2519_o;
  /* T80_MCode.vhd:277:25  */
  assign n2522_o = ir == 8'b01001000;
  /* T80_MCode.vhd:277:25  */
  assign n2523_o = n2520_o | n2522_o;
  /* T80_MCode.vhd:277:36  */
  assign n2525_o = ir == 8'b01001001;
  /* T80_MCode.vhd:277:36  */
  assign n2526_o = n2523_o | n2525_o;
  /* T80_MCode.vhd:277:47  */
  assign n2528_o = ir == 8'b01001010;
  /* T80_MCode.vhd:277:47  */
  assign n2529_o = n2526_o | n2528_o;
  /* T80_MCode.vhd:277:58  */
  assign n2531_o = ir == 8'b01001011;
  /* T80_MCode.vhd:277:58  */
  assign n2532_o = n2529_o | n2531_o;
  /* T80_MCode.vhd:277:69  */
  assign n2534_o = ir == 8'b01001100;
  /* T80_MCode.vhd:277:69  */
  assign n2535_o = n2532_o | n2534_o;
  /* T80_MCode.vhd:277:80  */
  assign n2537_o = ir == 8'b01001101;
  /* T80_MCode.vhd:277:80  */
  assign n2538_o = n2535_o | n2537_o;
  /* T80_MCode.vhd:277:91  */
  assign n2540_o = ir == 8'b01001111;
  /* T80_MCode.vhd:277:91  */
  assign n2541_o = n2538_o | n2540_o;
  /* T80_MCode.vhd:278:25  */
  assign n2543_o = ir == 8'b01010000;
  /* T80_MCode.vhd:278:25  */
  assign n2544_o = n2541_o | n2543_o;
  /* T80_MCode.vhd:278:36  */
  assign n2546_o = ir == 8'b01010001;
  /* T80_MCode.vhd:278:36  */
  assign n2547_o = n2544_o | n2546_o;
  /* T80_MCode.vhd:278:47  */
  assign n2549_o = ir == 8'b01010010;
  /* T80_MCode.vhd:278:47  */
  assign n2550_o = n2547_o | n2549_o;
  /* T80_MCode.vhd:278:58  */
  assign n2552_o = ir == 8'b01010011;
  /* T80_MCode.vhd:278:58  */
  assign n2553_o = n2550_o | n2552_o;
  /* T80_MCode.vhd:278:69  */
  assign n2555_o = ir == 8'b01010100;
  /* T80_MCode.vhd:278:69  */
  assign n2556_o = n2553_o | n2555_o;
  /* T80_MCode.vhd:278:80  */
  assign n2558_o = ir == 8'b01010101;
  /* T80_MCode.vhd:278:80  */
  assign n2559_o = n2556_o | n2558_o;
  /* T80_MCode.vhd:278:91  */
  assign n2561_o = ir == 8'b01010111;
  /* T80_MCode.vhd:278:91  */
  assign n2562_o = n2559_o | n2561_o;
  /* T80_MCode.vhd:279:25  */
  assign n2564_o = ir == 8'b01011000;
  /* T80_MCode.vhd:279:25  */
  assign n2565_o = n2562_o | n2564_o;
  /* T80_MCode.vhd:279:36  */
  assign n2567_o = ir == 8'b01011001;
  /* T80_MCode.vhd:279:36  */
  assign n2568_o = n2565_o | n2567_o;
  /* T80_MCode.vhd:279:47  */
  assign n2570_o = ir == 8'b01011010;
  /* T80_MCode.vhd:279:47  */
  assign n2571_o = n2568_o | n2570_o;
  /* T80_MCode.vhd:279:58  */
  assign n2573_o = ir == 8'b01011011;
  /* T80_MCode.vhd:279:58  */
  assign n2574_o = n2571_o | n2573_o;
  /* T80_MCode.vhd:279:69  */
  assign n2576_o = ir == 8'b01011100;
  /* T80_MCode.vhd:279:69  */
  assign n2577_o = n2574_o | n2576_o;
  /* T80_MCode.vhd:279:80  */
  assign n2579_o = ir == 8'b01011101;
  /* T80_MCode.vhd:279:80  */
  assign n2580_o = n2577_o | n2579_o;
  /* T80_MCode.vhd:279:91  */
  assign n2582_o = ir == 8'b01011111;
  /* T80_MCode.vhd:279:91  */
  assign n2583_o = n2580_o | n2582_o;
  /* T80_MCode.vhd:280:25  */
  assign n2585_o = ir == 8'b01100000;
  /* T80_MCode.vhd:280:25  */
  assign n2586_o = n2583_o | n2585_o;
  /* T80_MCode.vhd:280:36  */
  assign n2588_o = ir == 8'b01100001;
  /* T80_MCode.vhd:280:36  */
  assign n2589_o = n2586_o | n2588_o;
  /* T80_MCode.vhd:280:47  */
  assign n2591_o = ir == 8'b01100010;
  /* T80_MCode.vhd:280:47  */
  assign n2592_o = n2589_o | n2591_o;
  /* T80_MCode.vhd:280:58  */
  assign n2594_o = ir == 8'b01100011;
  /* T80_MCode.vhd:280:58  */
  assign n2595_o = n2592_o | n2594_o;
  /* T80_MCode.vhd:280:69  */
  assign n2597_o = ir == 8'b01100100;
  /* T80_MCode.vhd:280:69  */
  assign n2598_o = n2595_o | n2597_o;
  /* T80_MCode.vhd:280:80  */
  assign n2600_o = ir == 8'b01100101;
  /* T80_MCode.vhd:280:80  */
  assign n2601_o = n2598_o | n2600_o;
  /* T80_MCode.vhd:280:91  */
  assign n2603_o = ir == 8'b01100111;
  /* T80_MCode.vhd:280:91  */
  assign n2604_o = n2601_o | n2603_o;
  /* T80_MCode.vhd:281:25  */
  assign n2606_o = ir == 8'b01101000;
  /* T80_MCode.vhd:281:25  */
  assign n2607_o = n2604_o | n2606_o;
  /* T80_MCode.vhd:281:36  */
  assign n2609_o = ir == 8'b01101001;
  /* T80_MCode.vhd:281:36  */
  assign n2610_o = n2607_o | n2609_o;
  /* T80_MCode.vhd:281:47  */
  assign n2612_o = ir == 8'b01101010;
  /* T80_MCode.vhd:281:47  */
  assign n2613_o = n2610_o | n2612_o;
  /* T80_MCode.vhd:281:58  */
  assign n2615_o = ir == 8'b01101011;
  /* T80_MCode.vhd:281:58  */
  assign n2616_o = n2613_o | n2615_o;
  /* T80_MCode.vhd:281:69  */
  assign n2618_o = ir == 8'b01101100;
  /* T80_MCode.vhd:281:69  */
  assign n2619_o = n2616_o | n2618_o;
  /* T80_MCode.vhd:281:80  */
  assign n2621_o = ir == 8'b01101101;
  /* T80_MCode.vhd:281:80  */
  assign n2622_o = n2619_o | n2621_o;
  /* T80_MCode.vhd:281:91  */
  assign n2624_o = ir == 8'b01101111;
  /* T80_MCode.vhd:281:91  */
  assign n2625_o = n2622_o | n2624_o;
  /* T80_MCode.vhd:282:25  */
  assign n2627_o = ir == 8'b01111000;
  /* T80_MCode.vhd:282:25  */
  assign n2628_o = n2625_o | n2627_o;
  /* T80_MCode.vhd:282:36  */
  assign n2630_o = ir == 8'b01111001;
  /* T80_MCode.vhd:282:36  */
  assign n2631_o = n2628_o | n2630_o;
  /* T80_MCode.vhd:282:47  */
  assign n2633_o = ir == 8'b01111010;
  /* T80_MCode.vhd:282:47  */
  assign n2634_o = n2631_o | n2633_o;
  /* T80_MCode.vhd:282:58  */
  assign n2636_o = ir == 8'b01111011;
  /* T80_MCode.vhd:282:58  */
  assign n2637_o = n2634_o | n2636_o;
  /* T80_MCode.vhd:282:69  */
  assign n2639_o = ir == 8'b01111100;
  /* T80_MCode.vhd:282:69  */
  assign n2640_o = n2637_o | n2639_o;
  /* T80_MCode.vhd:282:80  */
  assign n2642_o = ir == 8'b01111101;
  /* T80_MCode.vhd:282:80  */
  assign n2643_o = n2640_o | n2642_o;
  /* T80_MCode.vhd:282:91  */
  assign n2645_o = ir == 8'b01111111;
  /* T80_MCode.vhd:282:91  */
  assign n2646_o = n2643_o | n2645_o;
  /* T80_MCode.vhd:291:30  */
  assign n2647_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:292:25  */
  assign n2649_o = n2647_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:291:25  */
  always @*
    case (n2649_o)
      1'b1: n2652_o = 1'b1;
      default: n2652_o = 1'b0;
    endcase
  /* T80_MCode.vhd:291:25  */
  always @*
    case (n2649_o)
      1'b1: n2655_o = 1'b1;
      default: n2655_o = 1'b0;
    endcase
  /* T80_MCode.vhd:291:25  */
  always @*
    case (n2649_o)
      1'b1: n2657_o = n2490_o;
      default: n2657_o = 3'b000;
    endcase
  /* T80_MCode.vhd:288:17  */
  assign n2659_o = ir == 8'b00000110;
  /* T80_MCode.vhd:288:32  */
  assign n2661_o = ir == 8'b00001110;
  /* T80_MCode.vhd:288:32  */
  assign n2662_o = n2659_o | n2661_o;
  /* T80_MCode.vhd:288:43  */
  assign n2664_o = ir == 8'b00010110;
  /* T80_MCode.vhd:288:43  */
  assign n2665_o = n2662_o | n2664_o;
  /* T80_MCode.vhd:288:54  */
  assign n2667_o = ir == 8'b00011110;
  /* T80_MCode.vhd:288:54  */
  assign n2668_o = n2665_o | n2667_o;
  /* T80_MCode.vhd:288:65  */
  assign n2670_o = ir == 8'b00100110;
  /* T80_MCode.vhd:288:65  */
  assign n2671_o = n2668_o | n2670_o;
  /* T80_MCode.vhd:288:76  */
  assign n2673_o = ir == 8'b00101110;
  /* T80_MCode.vhd:288:76  */
  assign n2674_o = n2671_o | n2673_o;
  /* T80_MCode.vhd:288:87  */
  assign n2676_o = ir == 8'b00111110;
  /* T80_MCode.vhd:288:87  */
  assign n2677_o = n2674_o | n2676_o;
  /* T80_MCode.vhd:301:30  */
  assign n2678_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:302:25  */
  assign n2680_o = n2678_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:304:25  */
  assign n2682_o = n2678_o == 31'b0000000000000000000000000000010;
  /* T80.vhd:400:17  */
  assign n2683_o = {n2682_o, n2680_o};
  /* T80_MCode.vhd:301:25  */
  always @*
    case (n2683_o)
      2'b10: n2686_o = 1'b1;
      2'b01: n2686_o = 1'b0;
      default: n2686_o = 1'b0;
    endcase
  /* T80_MCode.vhd:301:25  */
  always @*
    case (n2683_o)
      2'b10: n2688_o = n2490_o;
      2'b01: n2688_o = 3'b000;
      default: n2688_o = 3'b000;
    endcase
  /* T80_MCode.vhd:301:25  */
  always @*
    case (n2683_o)
      2'b10: n2691_o = 3'b111;
      2'b01: n2691_o = 3'b010;
      default: n2691_o = 3'b111;
    endcase
  /* T80_MCode.vhd:298:17  */
  assign n2693_o = ir == 8'b01000110;
  /* T80_MCode.vhd:298:32  */
  assign n2695_o = ir == 8'b01001110;
  /* T80_MCode.vhd:298:32  */
  assign n2696_o = n2693_o | n2695_o;
  /* T80_MCode.vhd:298:43  */
  assign n2698_o = ir == 8'b01010110;
  /* T80_MCode.vhd:298:43  */
  assign n2699_o = n2696_o | n2698_o;
  /* T80_MCode.vhd:298:54  */
  assign n2701_o = ir == 8'b01011110;
  /* T80_MCode.vhd:298:54  */
  assign n2702_o = n2699_o | n2701_o;
  /* T80_MCode.vhd:298:65  */
  assign n2704_o = ir == 8'b01100110;
  /* T80_MCode.vhd:298:65  */
  assign n2705_o = n2702_o | n2704_o;
  /* T80_MCode.vhd:298:76  */
  assign n2707_o = ir == 8'b01101110;
  /* T80_MCode.vhd:298:76  */
  assign n2708_o = n2705_o | n2707_o;
  /* T80_MCode.vhd:298:87  */
  assign n2710_o = ir == 8'b01111110;
  /* T80_MCode.vhd:298:87  */
  assign n2711_o = n2708_o | n2710_o;
  /* T80_MCode.vhd:312:30  */
  assign n2712_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:313:25  */
  assign n2715_o = n2712_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:317:25  */
  assign n2717_o = n2712_o == 31'b0000000000000000000000000000010;
  assign n2718_o = {n2717_o, n2715_o};
  /* T80_MCode.vhd:312:25  */
  always @*
    case (n2718_o)
      2'b10: n2720_o = 3'b000;
      2'b01: n2720_o = n2491_o;
      default: n2720_o = 3'b000;
    endcase
  /* T80_MCode.vhd:312:25  */
  always @*
    case (n2718_o)
      2'b10: n2722_o = 1'b0;
      2'b01: n2722_o = 1'b0;
      default: n2722_o = 1'b0;
    endcase
  /* T80_MCode.vhd:312:25  */
  always @*
    case (n2718_o)
      2'b10: n2725_o = 3'b111;
      2'b01: n2725_o = 3'b010;
      default: n2725_o = 3'b111;
    endcase
  /* T80_MCode.vhd:312:25  */
  always @*
    case (n2718_o)
      2'b10: n2728_o = 1'b1;
      2'b01: n2728_o = 1'b0;
      default: n2728_o = 1'b0;
    endcase
  /* T80_MCode.vhd:309:17  */
  assign n2730_o = ir == 8'b01110000;
  /* T80_MCode.vhd:309:32  */
  assign n2732_o = ir == 8'b01110001;
  /* T80_MCode.vhd:309:32  */
  assign n2733_o = n2730_o | n2732_o;
  /* T80_MCode.vhd:309:43  */
  assign n2735_o = ir == 8'b01110010;
  /* T80_MCode.vhd:309:43  */
  assign n2736_o = n2733_o | n2735_o;
  /* T80_MCode.vhd:309:54  */
  assign n2738_o = ir == 8'b01110011;
  /* T80_MCode.vhd:309:54  */
  assign n2739_o = n2736_o | n2738_o;
  /* T80_MCode.vhd:309:65  */
  assign n2741_o = ir == 8'b01110100;
  /* T80_MCode.vhd:309:65  */
  assign n2742_o = n2739_o | n2741_o;
  /* T80_MCode.vhd:309:76  */
  assign n2744_o = ir == 8'b01110101;
  /* T80_MCode.vhd:309:76  */
  assign n2745_o = n2742_o | n2744_o;
  /* T80_MCode.vhd:309:87  */
  assign n2747_o = ir == 8'b01110111;
  /* T80_MCode.vhd:309:87  */
  assign n2748_o = n2745_o | n2747_o;
  /* T80_MCode.vhd:324:30  */
  assign n2749_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:325:25  */
  assign n2752_o = n2749_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:330:25  */
  assign n2754_o = n2749_o == 31'b0000000000000000000000000000011;
  assign n2755_o = {n2754_o, n2752_o};
  /* T80_MCode.vhd:324:25  */
  always @*
    case (n2755_o)
      2'b10: n2758_o = 1'b0;
      2'b01: n2758_o = 1'b1;
      default: n2758_o = 1'b0;
    endcase
  /* T80_MCode.vhd:324:25  */
  always @*
    case (n2755_o)
      2'b10: n2760_o = 3'b000;
      2'b01: n2760_o = n2491_o;
      default: n2760_o = 3'b000;
    endcase
  /* T80_MCode.vhd:324:25  */
  always @*
    case (n2755_o)
      2'b10: n2762_o = 1'b0;
      2'b01: n2762_o = 1'b0;
      default: n2762_o = 1'b0;
    endcase
  /* T80_MCode.vhd:324:25  */
  always @*
    case (n2755_o)
      2'b10: n2765_o = 3'b111;
      2'b01: n2765_o = 3'b010;
      default: n2765_o = 3'b111;
    endcase
  /* T80_MCode.vhd:324:25  */
  always @*
    case (n2755_o)
      2'b10: n2768_o = 1'b1;
      2'b01: n2768_o = 1'b0;
      default: n2768_o = 1'b0;
    endcase
  /* T80_MCode.vhd:321:17  */
  assign n2770_o = ir == 8'b00110110;
  /* T80_MCode.vhd:337:30  */
  assign n2771_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:338:25  */
  assign n2773_o = n2771_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:340:25  */
  assign n2775_o = n2771_o == 31'b0000000000000000000000000000010;
  assign n2776_o = {n2775_o, n2773_o};
  /* T80_MCode.vhd:337:25  */
  always @*
    case (n2776_o)
      2'b10: n2779_o = 1'b1;
      2'b01: n2779_o = 1'b0;
      default: n2779_o = 1'b0;
    endcase
  /* T80_MCode.vhd:337:25  */
  always @*
    case (n2776_o)
      2'b10: n2782_o = 3'b111;
      2'b01: n2782_o = 3'b000;
      default: n2782_o = 3'b111;
    endcase
  /* T80_MCode.vhd:334:17  */
  assign n2784_o = ir == 8'b00001010;
  /* T80_MCode.vhd:347:30  */
  assign n2785_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:348:25  */
  assign n2787_o = n2785_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:350:25  */
  assign n2789_o = n2785_o == 31'b0000000000000000000000000000010;
  assign n2790_o = {n2789_o, n2787_o};
  /* T80_MCode.vhd:347:25  */
  always @*
    case (n2790_o)
      2'b10: n2793_o = 1'b1;
      2'b01: n2793_o = 1'b0;
      default: n2793_o = 1'b0;
    endcase
  /* T80_MCode.vhd:347:25  */
  always @*
    case (n2790_o)
      2'b10: n2796_o = 3'b111;
      2'b01: n2796_o = 3'b001;
      default: n2796_o = 3'b111;
    endcase
  /* T80_MCode.vhd:344:17  */
  assign n2798_o = ir == 8'b00011010;
  /* T80_MCode.vhd:369:38  */
  assign n2799_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:370:33  */
  assign n2801_o = n2799_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:373:33  */
  assign n2803_o = n2799_o == 31'b0000000000000000000000000000011;
  /* T80_MCode.vhd:376:33  */
  assign n2805_o = n2799_o == 31'b0000000000000000000000000000100;
  assign n2806_o = {n2805_o, n2803_o, n2801_o};
  /* T80_MCode.vhd:369:33  */
  always @*
    case (n2806_o)
      3'b100: n2810_o = 1'b0;
      3'b010: n2810_o = 1'b1;
      3'b001: n2810_o = 1'b1;
      default: n2810_o = 1'b0;
    endcase
  /* T80_MCode.vhd:369:33  */
  always @*
    case (n2806_o)
      3'b100: n2813_o = 1'b1;
      3'b010: n2813_o = 1'b0;
      3'b001: n2813_o = 1'b0;
      default: n2813_o = 1'b0;
    endcase
  /* T80_MCode.vhd:369:33  */
  always @*
    case (n2806_o)
      3'b100: n2816_o = 3'b111;
      3'b010: n2816_o = 3'b110;
      3'b001: n2816_o = 3'b111;
      default: n2816_o = 3'b111;
    endcase
  /* T80_MCode.vhd:369:33  */
  always @*
    case (n2806_o)
      3'b100: n2819_o = 1'b0;
      3'b010: n2819_o = 1'b0;
      3'b001: n2819_o = 1'b1;
      default: n2819_o = 1'b0;
    endcase
  /* T80_MCode.vhd:354:17  */
  assign n2821_o = ir == 8'b00111010;
  /* T80_MCode.vhd:384:30  */
  assign n2822_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:385:25  */
  assign n2824_o = n2822_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:389:25  */
  assign n2826_o = n2822_o == 31'b0000000000000000000000000000010;
  assign n2827_o = {n2826_o, n2824_o};
  /* T80_MCode.vhd:384:25  */
  always @*
    case (n2827_o)
      2'b10: n2830_o = 4'b0000;
      2'b01: n2830_o = 4'b0111;
      default: n2830_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:384:25  */
  always @*
    case (n2827_o)
      2'b10: n2833_o = 3'b111;
      2'b01: n2833_o = 3'b000;
      default: n2833_o = 3'b111;
    endcase
  /* T80_MCode.vhd:384:25  */
  always @*
    case (n2827_o)
      2'b10: n2836_o = 2'b00;
      2'b01: n2836_o = 2'b10;
      default: n2836_o = 2'b00;
    endcase
  /* T80_MCode.vhd:384:25  */
  always @*
    case (n2827_o)
      2'b10: n2839_o = 1'b1;
      2'b01: n2839_o = 1'b0;
      default: n2839_o = 1'b0;
    endcase
  /* T80_MCode.vhd:381:17  */
  assign n2841_o = ir == 8'b00000010;
  /* T80_MCode.vhd:396:30  */
  assign n2842_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:397:25  */
  assign n2844_o = n2842_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:401:25  */
  assign n2846_o = n2842_o == 31'b0000000000000000000000000000010;
  assign n2847_o = {n2846_o, n2844_o};
  /* T80_MCode.vhd:396:25  */
  always @*
    case (n2847_o)
      2'b10: n2850_o = 4'b0000;
      2'b01: n2850_o = 4'b0111;
      default: n2850_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:396:25  */
  always @*
    case (n2847_o)
      2'b10: n2853_o = 3'b111;
      2'b01: n2853_o = 3'b001;
      default: n2853_o = 3'b111;
    endcase
  /* T80_MCode.vhd:396:25  */
  always @*
    case (n2847_o)
      2'b10: n2856_o = 2'b00;
      2'b01: n2856_o = 2'b10;
      default: n2856_o = 2'b00;
    endcase
  /* T80_MCode.vhd:396:25  */
  always @*
    case (n2847_o)
      2'b10: n2859_o = 1'b1;
      2'b01: n2859_o = 1'b0;
      default: n2859_o = 1'b0;
    endcase
  /* T80_MCode.vhd:393:17  */
  assign n2861_o = ir == 8'b00010010;
  /* T80_MCode.vhd:421:38  */
  assign n2862_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:422:33  */
  assign n2864_o = n2862_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:425:33  */
  assign n2866_o = n2862_o == 31'b0000000000000000000000000000011;
  /* T80_MCode.vhd:430:33  */
  assign n2868_o = n2862_o == 31'b0000000000000000000000000000100;
  assign n2869_o = {n2868_o, n2866_o, n2864_o};
  /* T80_MCode.vhd:421:33  */
  always @*
    case (n2869_o)
      3'b100: n2873_o = 1'b0;
      3'b010: n2873_o = 1'b1;
      3'b001: n2873_o = 1'b1;
      default: n2873_o = 1'b0;
    endcase
  /* T80_MCode.vhd:421:33  */
  always @*
    case (n2869_o)
      3'b100: n2876_o = 4'b0000;
      3'b010: n2876_o = 4'b0111;
      3'b001: n2876_o = 4'b0000;
      default: n2876_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:421:33  */
  always @*
    case (n2869_o)
      3'b100: n2879_o = 3'b111;
      3'b010: n2879_o = 3'b110;
      3'b001: n2879_o = 3'b111;
      default: n2879_o = 3'b111;
    endcase
  /* T80_MCode.vhd:421:33  */
  always @*
    case (n2869_o)
      3'b100: n2882_o = 1'b0;
      3'b010: n2882_o = 1'b0;
      3'b001: n2882_o = 1'b1;
      default: n2882_o = 1'b0;
    endcase
  /* T80_MCode.vhd:421:33  */
  always @*
    case (n2869_o)
      3'b100: n2885_o = 2'b00;
      3'b010: n2885_o = 2'b10;
      3'b001: n2885_o = 2'b00;
      default: n2885_o = 2'b00;
    endcase
  /* T80_MCode.vhd:421:33  */
  always @*
    case (n2869_o)
      3'b100: n2888_o = 1'b1;
      3'b010: n2888_o = 1'b0;
      3'b001: n2888_o = 1'b0;
      default: n2888_o = 1'b0;
    endcase
  /* T80_MCode.vhd:405:17  */
  assign n2890_o = ir == 8'b00110010;
  /* T80_MCode.vhd:440:30  */
  assign n2891_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:444:42  */
  assign n2893_o = n2492_o == 2'b11;
  assign n2895_o = {n2492_o, 1'b1};
  /* T80_MCode.vhd:444:33  */
  assign n2897_o = n2893_o ? 3'b000 : n2895_o;
  /* T80_MCode.vhd:444:33  */
  assign n2900_o = n2893_o ? 1'b1 : 1'b0;
  /* T80_MCode.vhd:441:25  */
  assign n2902_o = n2891_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:453:42  */
  assign n2904_o = n2492_o == 2'b11;
  assign n2906_o = {n2492_o, 1'b0};
  /* T80_MCode.vhd:453:33  */
  assign n2908_o = n2904_o ? 3'b001 : n2906_o;
  /* T80_MCode.vhd:453:33  */
  assign n2911_o = n2904_o ? 1'b1 : 1'b0;
  /* T80_MCode.vhd:450:25  */
  assign n2913_o = n2891_o == 31'b0000000000000000000000000000011;
  assign n2914_o = {n2913_o, n2902_o};
  /* T80_MCode.vhd:440:25  */
  always @*
    case (n2914_o)
      2'b10: n2918_o = 1'b1;
      2'b01: n2918_o = 1'b1;
      default: n2918_o = 1'b0;
    endcase
  /* T80_MCode.vhd:440:25  */
  always @*
    case (n2914_o)
      2'b10: n2922_o = 1'b1;
      2'b01: n2922_o = 1'b1;
      default: n2922_o = 1'b0;
    endcase
  /* T80_MCode.vhd:440:25  */
  always @*
    case (n2914_o)
      2'b10: n2924_o = n2908_o;
      2'b01: n2924_o = n2897_o;
      default: n2924_o = 3'b000;
    endcase
  /* T80_MCode.vhd:440:25  */
  always @*
    case (n2914_o)
      2'b10: n2926_o = n2911_o;
      2'b01: n2926_o = n2900_o;
      default: n2926_o = 1'b0;
    endcase
  /* T80_MCode.vhd:437:17  */
  assign n2928_o = ir == 8'b00000001;
  /* T80_MCode.vhd:437:32  */
  assign n2930_o = ir == 8'b00010001;
  /* T80_MCode.vhd:437:32  */
  assign n2931_o = n2928_o | n2930_o;
  /* T80_MCode.vhd:437:43  */
  assign n2933_o = ir == 8'b00100001;
  /* T80_MCode.vhd:437:43  */
  assign n2934_o = n2931_o | n2933_o;
  /* T80_MCode.vhd:437:54  */
  assign n2936_o = ir == 8'b00110001;
  /* T80_MCode.vhd:437:54  */
  assign n2937_o = n2934_o | n2936_o;
  /* T80_MCode.vhd:476:38  */
  assign n2938_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:477:33  */
  assign n2940_o = n2938_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:480:33  */
  assign n2942_o = n2938_o == 31'b0000000000000000000000000000011;
  /* T80_MCode.vhd:484:33  */
  assign n2945_o = n2938_o == 31'b0000000000000000000000000000100;
  /* T80_MCode.vhd:489:33  */
  assign n2948_o = n2938_o == 31'b0000000000000000000000000000101;
  assign n2949_o = {n2948_o, n2945_o, n2942_o, n2940_o};
  /* T80_MCode.vhd:476:33  */
  always @*
    case (n2949_o)
      4'b1000: n2953_o = 1'b0;
      4'b0100: n2953_o = 1'b0;
      4'b0010: n2953_o = 1'b1;
      4'b0001: n2953_o = 1'b1;
      default: n2953_o = 1'b0;
    endcase
  /* T80_MCode.vhd:476:33  */
  always @*
    case (n2949_o)
      4'b1000: n2956_o = 1'b0;
      4'b0100: n2956_o = 1'b1;
      4'b0010: n2956_o = 1'b0;
      4'b0001: n2956_o = 1'b0;
      default: n2956_o = 1'b0;
    endcase
  /* T80_MCode.vhd:476:33  */
  always @*
    case (n2949_o)
      4'b1000: n2960_o = 1'b1;
      4'b0100: n2960_o = 1'b1;
      4'b0010: n2960_o = 1'b0;
      4'b0001: n2960_o = 1'b0;
      default: n2960_o = 1'b0;
    endcase
  /* T80_MCode.vhd:476:33  */
  always @*
    case (n2949_o)
      4'b1000: n2962_o = 3'b100;
      4'b0100: n2962_o = 3'b101;
      4'b0010: n2962_o = 3'b000;
      4'b0001: n2962_o = 3'b000;
      default: n2962_o = 3'b000;
    endcase
  /* T80_MCode.vhd:476:33  */
  always @*
    case (n2949_o)
      4'b1000: n2966_o = 3'b111;
      4'b0100: n2966_o = 3'b110;
      4'b0010: n2966_o = 3'b110;
      4'b0001: n2966_o = 3'b111;
      default: n2966_o = 3'b111;
    endcase
  /* T80_MCode.vhd:476:33  */
  always @*
    case (n2949_o)
      4'b1000: n2969_o = 1'b0;
      4'b0100: n2969_o = 1'b0;
      4'b0010: n2969_o = 1'b0;
      4'b0001: n2969_o = 1'b1;
      default: n2969_o = 1'b0;
    endcase
  /* T80_MCode.vhd:476:33  */
  always @*
    case (n2949_o)
      4'b1000: n2972_o = 1'b0;
      4'b0100: n2972_o = 1'b0;
      4'b0010: n2972_o = 1'b1;
      4'b0001: n2972_o = 1'b0;
      default: n2972_o = 1'b0;
    endcase
  /* T80_MCode.vhd:461:17  */
  assign n2974_o = ir == 8'b00101010;
  /* T80_MCode.vhd:511:38  */
  assign n2975_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:512:33  */
  assign n2977_o = n2975_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:515:33  */
  assign n2979_o = n2975_o == 31'b0000000000000000000000000000011;
  /* T80_MCode.vhd:520:33  */
  assign n2981_o = n2975_o == 31'b0000000000000000000000000000100;
  /* T80_MCode.vhd:525:33  */
  assign n2983_o = n2975_o == 31'b0000000000000000000000000000101;
  assign n2984_o = {n2983_o, n2981_o, n2979_o, n2977_o};
  /* T80_MCode.vhd:511:33  */
  always @*
    case (n2984_o)
      4'b1000: n2988_o = 1'b0;
      4'b0100: n2988_o = 1'b0;
      4'b0010: n2988_o = 1'b1;
      4'b0001: n2988_o = 1'b1;
      default: n2988_o = 1'b0;
    endcase
  /* T80_MCode.vhd:511:33  */
  always @*
    case (n2984_o)
      4'b1000: n2991_o = 1'b0;
      4'b0100: n2991_o = 1'b1;
      4'b0010: n2991_o = 1'b0;
      4'b0001: n2991_o = 1'b0;
      default: n2991_o = 1'b0;
    endcase
  /* T80_MCode.vhd:511:33  */
  always @*
    case (n2984_o)
      4'b1000: n2995_o = 4'b0000;
      4'b0100: n2995_o = 4'b0100;
      4'b0010: n2995_o = 4'b0101;
      4'b0001: n2995_o = 4'b0000;
      default: n2995_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:511:33  */
  always @*
    case (n2984_o)
      4'b1000: n2999_o = 3'b111;
      4'b0100: n2999_o = 3'b110;
      4'b0010: n2999_o = 3'b110;
      4'b0001: n2999_o = 3'b111;
      default: n2999_o = 3'b111;
    endcase
  /* T80_MCode.vhd:511:33  */
  always @*
    case (n2984_o)
      4'b1000: n3002_o = 1'b0;
      4'b0100: n3002_o = 1'b0;
      4'b0010: n3002_o = 1'b0;
      4'b0001: n3002_o = 1'b1;
      default: n3002_o = 1'b0;
    endcase
  /* T80_MCode.vhd:511:33  */
  always @*
    case (n2984_o)
      4'b1000: n3005_o = 1'b0;
      4'b0100: n3005_o = 1'b0;
      4'b0010: n3005_o = 1'b1;
      4'b0001: n3005_o = 1'b0;
      default: n3005_o = 1'b0;
    endcase
  /* T80_MCode.vhd:511:33  */
  always @*
    case (n2984_o)
      4'b1000: n3009_o = 1'b1;
      4'b0100: n3009_o = 1'b1;
      4'b0010: n3009_o = 1'b0;
      4'b0001: n3009_o = 1'b0;
      default: n3009_o = 1'b0;
    endcase
  /* T80_MCode.vhd:495:17  */
  assign n3011_o = ir == 8'b00100010;
  /* T80_MCode.vhd:530:17  */
  assign n3013_o = ir == 8'b11111001;
  /* T80_MCode.vhd:574:38  */
  assign n3014_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:579:50  */
  assign n3016_o = n2492_o == 2'b11;
  assign n3019_o = {1'b0, n2492_o, 1'b0};
  /* T80_MCode.vhd:579:41  */
  assign n3021_o = n3016_o ? 4'b0111 : n3019_o;
  /* T80_MCode.vhd:575:33  */
  assign n3023_o = n3014_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:589:50  */
  assign n3025_o = n2492_o == 2'b11;
  assign n3028_o = {1'b0, n2492_o, 1'b1};
  /* T80_MCode.vhd:589:41  */
  assign n3030_o = n3025_o ? 4'b1011 : n3028_o;
  /* T80_MCode.vhd:586:33  */
  assign n3032_o = n3014_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:597:33  */
  assign n3034_o = n3014_o == 31'b0000000000000000000000000000011;
  assign n3035_o = {n3034_o, n3032_o, n3023_o};
  /* T80_MCode.vhd:574:33  */
  always @*
    case (n3035_o)
      3'b100: n3037_o = n2497_o;
      3'b010: n3037_o = n2497_o;
      3'b001: n3037_o = 3'b101;
      default: n3037_o = n2497_o;
    endcase
  /* T80_MCode.vhd:574:33  */
  always @*
    case (n3035_o)
      3'b100: n3041_o = 4'b0000;
      3'b010: n3041_o = 4'b1111;
      3'b001: n3041_o = 4'b1111;
      default: n3041_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:574:33  */
  always @*
    case (n3035_o)
      3'b100: n3043_o = 4'b0000;
      3'b010: n3043_o = n3030_o;
      3'b001: n3043_o = n3021_o;
      default: n3043_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:574:33  */
  always @*
    case (n3035_o)
      3'b100: n3047_o = 3'b111;
      3'b010: n3047_o = 3'b101;
      3'b001: n3047_o = 3'b101;
      default: n3047_o = 3'b111;
    endcase
  /* T80_MCode.vhd:574:33  */
  always @*
    case (n3035_o)
      3'b100: n3051_o = 1'b1;
      3'b010: n3051_o = 1'b1;
      3'b001: n3051_o = 1'b0;
      default: n3051_o = 1'b0;
    endcase
  /* T80_MCode.vhd:541:17  */
  assign n3053_o = ir == 8'b11000101;
  /* T80_MCode.vhd:541:32  */
  assign n3055_o = ir == 8'b11010101;
  /* T80_MCode.vhd:541:32  */
  assign n3056_o = n3053_o | n3055_o;
  /* T80_MCode.vhd:541:43  */
  assign n3058_o = ir == 8'b11100101;
  /* T80_MCode.vhd:541:43  */
  assign n3059_o = n3056_o | n3058_o;
  /* T80_MCode.vhd:541:54  */
  assign n3061_o = ir == 8'b11110101;
  /* T80_MCode.vhd:541:54  */
  assign n3062_o = n3059_o | n3061_o;
  /* T80_MCode.vhd:605:30  */
  assign n3063_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:606:25  */
  assign n3065_o = n3063_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:612:42  */
  assign n3067_o = n2492_o == 2'b11;
  assign n3069_o = {n2492_o, 1'b1};
  /* T80_MCode.vhd:612:33  */
  assign n3071_o = n3067_o ? 3'b011 : n3069_o;
  /* T80_MCode.vhd:612:33  */
  assign n3074_o = n3067_o ? 1'b1 : 1'b0;
  /* T80_MCode.vhd:608:25  */
  assign n3076_o = n3063_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:621:42  */
  assign n3078_o = n2492_o == 2'b11;
  assign n3080_o = {n2492_o, 1'b0};
  /* T80_MCode.vhd:621:33  */
  assign n3082_o = n3078_o ? 3'b111 : n3080_o;
  /* T80_MCode.vhd:621:33  */
  assign n3085_o = n3078_o ? 1'b0 : 1'b0;
  /* T80_MCode.vhd:618:25  */
  assign n3087_o = n3063_o == 31'b0000000000000000000000000000011;
  assign n3088_o = {n3087_o, n3076_o, n3065_o};
  /* T80_MCode.vhd:605:25  */
  always @*
    case (n3088_o)
      3'b100: n3092_o = 4'b0111;
      3'b010: n3092_o = 4'b0111;
      3'b001: n3092_o = 4'b0000;
      default: n3092_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:605:25  */
  always @*
    case (n3088_o)
      3'b100: n3096_o = 1'b1;
      3'b010: n3096_o = 1'b1;
      3'b001: n3096_o = 1'b0;
      default: n3096_o = 1'b0;
    endcase
  /* T80_MCode.vhd:605:25  */
  always @*
    case (n3088_o)
      3'b100: n3098_o = n3082_o;
      3'b010: n3098_o = n3071_o;
      3'b001: n3098_o = 3'b000;
      default: n3098_o = 3'b000;
    endcase
  /* T80_MCode.vhd:605:25  */
  always @*
    case (n3088_o)
      3'b100: n3100_o = n3085_o;
      3'b010: n3100_o = n3074_o;
      3'b001: n3100_o = 1'b0;
      default: n3100_o = 1'b0;
    endcase
  /* T80_MCode.vhd:605:25  */
  always @*
    case (n3088_o)
      3'b100: n3104_o = 3'b111;
      3'b010: n3104_o = 3'b101;
      3'b001: n3104_o = 3'b101;
      default: n3104_o = 3'b111;
    endcase
  /* T80_MCode.vhd:602:17  */
  assign n3106_o = ir == 8'b11000001;
  /* T80_MCode.vhd:602:32  */
  assign n3108_o = ir == 8'b11010001;
  /* T80_MCode.vhd:602:32  */
  assign n3109_o = n3106_o | n3108_o;
  /* T80_MCode.vhd:602:43  */
  assign n3111_o = ir == 8'b11100001;
  /* T80_MCode.vhd:602:43  */
  assign n3112_o = n3109_o | n3111_o;
  /* T80_MCode.vhd:602:54  */
  assign n3114_o = ir == 8'b11110001;
  /* T80_MCode.vhd:602:54  */
  assign n3115_o = n3112_o | n3114_o;
  /* T80_MCode.vhd:631:17  */
  assign n3117_o = ir == 8'b11101011;
  /* T80_MCode.vhd:636:17  */
  assign n3119_o = ir == 8'b00001000;
  /* T80_MCode.vhd:662:17  */
  assign n3121_o = ir == 8'b11011001;
  /* T80_MCode.vhd:688:38  */
  assign n3122_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:689:33  */
  assign n3124_o = n3122_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:691:33  */
  assign n3126_o = n3122_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:695:33  */
  assign n3128_o = n3122_o == 31'b0000000000000000000000000000011;
  /* T80_MCode.vhd:700:33  */
  assign n3130_o = n3122_o == 31'b0000000000000000000000000000100;
  /* T80_MCode.vhd:705:33  */
  assign n3132_o = n3122_o == 31'b0000000000000000000000000000101;
  assign n3133_o = {n3132_o, n3130_o, n3128_o, n3126_o, n3124_o};
  /* T80_MCode.vhd:688:33  */
  always @*
    case (n3133_o)
      5'b10000: n3136_o = 3'b101;
      5'b01000: n3136_o = n2497_o;
      5'b00100: n3136_o = 3'b100;
      5'b00010: n3136_o = n2497_o;
      5'b00001: n3136_o = n2497_o;
      default: n3136_o = n2497_o;
    endcase
  /* T80_MCode.vhd:688:33  */
  always @*
    case (n3133_o)
      5'b10000: n3140_o = 4'b0000;
      5'b01000: n3140_o = 4'b1111;
      5'b00100: n3140_o = 4'b0000;
      5'b00010: n3140_o = 4'b0111;
      5'b00001: n3140_o = 4'b0000;
      default: n3140_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:688:33  */
  always @*
    case (n3133_o)
      5'b10000: n3144_o = 4'b0000;
      5'b01000: n3144_o = 4'b0101;
      5'b00100: n3144_o = 4'b0100;
      5'b00010: n3144_o = 4'b0000;
      5'b00001: n3144_o = 4'b0000;
      default: n3144_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:688:33  */
  always @*
    case (n3133_o)
      5'b10000: n3150_o = 3'b111;
      5'b01000: n3150_o = 3'b101;
      5'b00100: n3150_o = 3'b101;
      5'b00010: n3150_o = 3'b101;
      5'b00001: n3150_o = 3'b101;
      default: n3150_o = 3'b111;
    endcase
  /* T80_MCode.vhd:688:33  */
  always @*
    case (n3133_o)
      5'b10000: n3153_o = 1'b0;
      5'b01000: n3153_o = 1'b0;
      5'b00100: n3153_o = 1'b0;
      5'b00010: n3153_o = 1'b1;
      5'b00001: n3153_o = 1'b0;
      default: n3153_o = 1'b0;
    endcase
  /* T80_MCode.vhd:688:33  */
  always @*
    case (n3133_o)
      5'b10000: n3156_o = 1'b0;
      5'b01000: n3156_o = 1'b0;
      5'b00100: n3156_o = 1'b1;
      5'b00010: n3156_o = 1'b0;
      5'b00001: n3156_o = 1'b0;
      default: n3156_o = 1'b0;
    endcase
  /* T80_MCode.vhd:688:33  */
  always @*
    case (n3133_o)
      5'b10000: n3159_o = 1'b1;
      5'b01000: n3159_o = 1'b0;
      5'b00100: n3159_o = 1'b0;
      5'b00010: n3159_o = 1'b0;
      5'b00001: n3159_o = 1'b0;
      default: n3159_o = 1'b0;
    endcase
  /* T80_MCode.vhd:688:33  */
  always @*
    case (n3133_o)
      5'b10000: n3163_o = 1'b1;
      5'b01000: n3163_o = 1'b1;
      5'b00100: n3163_o = 1'b0;
      5'b00010: n3163_o = 1'b0;
      5'b00001: n3163_o = 1'b0;
      default: n3163_o = 1'b0;
    endcase
  /* T80_MCode.vhd:684:17  */
  assign n3165_o = ir == 8'b11100011;
  /* T80_MCode.vhd:714:17  */
  assign n3168_o = ir == 8'b10000000;
  /* T80_MCode.vhd:714:32  */
  assign n3170_o = ir == 8'b10000001;
  /* T80_MCode.vhd:714:32  */
  assign n3171_o = n3168_o | n3170_o;
  /* T80_MCode.vhd:714:43  */
  assign n3173_o = ir == 8'b10000010;
  /* T80_MCode.vhd:714:43  */
  assign n3174_o = n3171_o | n3173_o;
  /* T80_MCode.vhd:714:54  */
  assign n3176_o = ir == 8'b10000011;
  /* T80_MCode.vhd:714:54  */
  assign n3177_o = n3174_o | n3176_o;
  /* T80_MCode.vhd:714:65  */
  assign n3179_o = ir == 8'b10000100;
  /* T80_MCode.vhd:714:65  */
  assign n3180_o = n3177_o | n3179_o;
  /* T80_MCode.vhd:714:76  */
  assign n3182_o = ir == 8'b10000101;
  /* T80_MCode.vhd:714:76  */
  assign n3183_o = n3180_o | n3182_o;
  /* T80_MCode.vhd:714:87  */
  assign n3185_o = ir == 8'b10000111;
  /* T80_MCode.vhd:714:87  */
  assign n3186_o = n3183_o | n3185_o;
  /* T80_MCode.vhd:715:25  */
  assign n3188_o = ir == 8'b10001000;
  /* T80_MCode.vhd:715:25  */
  assign n3189_o = n3186_o | n3188_o;
  /* T80_MCode.vhd:715:36  */
  assign n3191_o = ir == 8'b10001001;
  /* T80_MCode.vhd:715:36  */
  assign n3192_o = n3189_o | n3191_o;
  /* T80_MCode.vhd:715:47  */
  assign n3194_o = ir == 8'b10001010;
  /* T80_MCode.vhd:715:47  */
  assign n3195_o = n3192_o | n3194_o;
  /* T80_MCode.vhd:715:58  */
  assign n3197_o = ir == 8'b10001011;
  /* T80_MCode.vhd:715:58  */
  assign n3198_o = n3195_o | n3197_o;
  /* T80_MCode.vhd:715:69  */
  assign n3200_o = ir == 8'b10001100;
  /* T80_MCode.vhd:715:69  */
  assign n3201_o = n3198_o | n3200_o;
  /* T80_MCode.vhd:715:80  */
  assign n3203_o = ir == 8'b10001101;
  /* T80_MCode.vhd:715:80  */
  assign n3204_o = n3201_o | n3203_o;
  /* T80_MCode.vhd:715:91  */
  assign n3206_o = ir == 8'b10001111;
  /* T80_MCode.vhd:715:91  */
  assign n3207_o = n3204_o | n3206_o;
  /* T80_MCode.vhd:716:25  */
  assign n3209_o = ir == 8'b10010000;
  /* T80_MCode.vhd:716:25  */
  assign n3210_o = n3207_o | n3209_o;
  /* T80_MCode.vhd:716:36  */
  assign n3212_o = ir == 8'b10010001;
  /* T80_MCode.vhd:716:36  */
  assign n3213_o = n3210_o | n3212_o;
  /* T80_MCode.vhd:716:47  */
  assign n3215_o = ir == 8'b10010010;
  /* T80_MCode.vhd:716:47  */
  assign n3216_o = n3213_o | n3215_o;
  /* T80_MCode.vhd:716:58  */
  assign n3218_o = ir == 8'b10010011;
  /* T80_MCode.vhd:716:58  */
  assign n3219_o = n3216_o | n3218_o;
  /* T80_MCode.vhd:716:69  */
  assign n3221_o = ir == 8'b10010100;
  /* T80_MCode.vhd:716:69  */
  assign n3222_o = n3219_o | n3221_o;
  /* T80_MCode.vhd:716:80  */
  assign n3224_o = ir == 8'b10010101;
  /* T80_MCode.vhd:716:80  */
  assign n3225_o = n3222_o | n3224_o;
  /* T80_MCode.vhd:716:91  */
  assign n3227_o = ir == 8'b10010111;
  /* T80_MCode.vhd:716:91  */
  assign n3228_o = n3225_o | n3227_o;
  /* T80_MCode.vhd:717:25  */
  assign n3230_o = ir == 8'b10011000;
  /* T80_MCode.vhd:717:25  */
  assign n3231_o = n3228_o | n3230_o;
  /* T80_MCode.vhd:717:36  */
  assign n3233_o = ir == 8'b10011001;
  /* T80_MCode.vhd:717:36  */
  assign n3234_o = n3231_o | n3233_o;
  /* T80_MCode.vhd:717:47  */
  assign n3236_o = ir == 8'b10011010;
  /* T80_MCode.vhd:717:47  */
  assign n3237_o = n3234_o | n3236_o;
  /* T80_MCode.vhd:717:58  */
  assign n3239_o = ir == 8'b10011011;
  /* T80_MCode.vhd:717:58  */
  assign n3240_o = n3237_o | n3239_o;
  /* T80_MCode.vhd:717:69  */
  assign n3242_o = ir == 8'b10011100;
  /* T80_MCode.vhd:717:69  */
  assign n3243_o = n3240_o | n3242_o;
  /* T80_MCode.vhd:717:80  */
  assign n3245_o = ir == 8'b10011101;
  /* T80_MCode.vhd:717:80  */
  assign n3246_o = n3243_o | n3245_o;
  /* T80_MCode.vhd:717:91  */
  assign n3248_o = ir == 8'b10011111;
  /* T80_MCode.vhd:717:91  */
  assign n3249_o = n3246_o | n3248_o;
  /* T80_MCode.vhd:718:25  */
  assign n3251_o = ir == 8'b10100000;
  /* T80_MCode.vhd:718:25  */
  assign n3252_o = n3249_o | n3251_o;
  /* T80_MCode.vhd:718:36  */
  assign n3254_o = ir == 8'b10100001;
  /* T80_MCode.vhd:718:36  */
  assign n3255_o = n3252_o | n3254_o;
  /* T80_MCode.vhd:718:47  */
  assign n3257_o = ir == 8'b10100010;
  /* T80_MCode.vhd:718:47  */
  assign n3258_o = n3255_o | n3257_o;
  /* T80_MCode.vhd:718:58  */
  assign n3260_o = ir == 8'b10100011;
  /* T80_MCode.vhd:718:58  */
  assign n3261_o = n3258_o | n3260_o;
  /* T80_MCode.vhd:718:69  */
  assign n3263_o = ir == 8'b10100100;
  /* T80_MCode.vhd:718:69  */
  assign n3264_o = n3261_o | n3263_o;
  /* T80_MCode.vhd:718:80  */
  assign n3266_o = ir == 8'b10100101;
  /* T80_MCode.vhd:718:80  */
  assign n3267_o = n3264_o | n3266_o;
  /* T80_MCode.vhd:718:91  */
  assign n3269_o = ir == 8'b10100111;
  /* T80_MCode.vhd:718:91  */
  assign n3270_o = n3267_o | n3269_o;
  /* T80_MCode.vhd:719:25  */
  assign n3272_o = ir == 8'b10101000;
  /* T80_MCode.vhd:719:25  */
  assign n3273_o = n3270_o | n3272_o;
  /* T80_MCode.vhd:719:36  */
  assign n3275_o = ir == 8'b10101001;
  /* T80_MCode.vhd:719:36  */
  assign n3276_o = n3273_o | n3275_o;
  /* T80_MCode.vhd:719:47  */
  assign n3278_o = ir == 8'b10101010;
  /* T80_MCode.vhd:719:47  */
  assign n3279_o = n3276_o | n3278_o;
  /* T80_MCode.vhd:719:58  */
  assign n3281_o = ir == 8'b10101011;
  /* T80_MCode.vhd:719:58  */
  assign n3282_o = n3279_o | n3281_o;
  /* T80_MCode.vhd:719:69  */
  assign n3284_o = ir == 8'b10101100;
  /* T80_MCode.vhd:719:69  */
  assign n3285_o = n3282_o | n3284_o;
  /* T80_MCode.vhd:719:80  */
  assign n3287_o = ir == 8'b10101101;
  /* T80_MCode.vhd:719:80  */
  assign n3288_o = n3285_o | n3287_o;
  /* T80_MCode.vhd:719:91  */
  assign n3290_o = ir == 8'b10101111;
  /* T80_MCode.vhd:719:91  */
  assign n3291_o = n3288_o | n3290_o;
  /* T80_MCode.vhd:720:25  */
  assign n3293_o = ir == 8'b10110000;
  /* T80_MCode.vhd:720:25  */
  assign n3294_o = n3291_o | n3293_o;
  /* T80_MCode.vhd:720:36  */
  assign n3296_o = ir == 8'b10110001;
  /* T80_MCode.vhd:720:36  */
  assign n3297_o = n3294_o | n3296_o;
  /* T80_MCode.vhd:720:47  */
  assign n3299_o = ir == 8'b10110010;
  /* T80_MCode.vhd:720:47  */
  assign n3300_o = n3297_o | n3299_o;
  /* T80_MCode.vhd:720:58  */
  assign n3302_o = ir == 8'b10110011;
  /* T80_MCode.vhd:720:58  */
  assign n3303_o = n3300_o | n3302_o;
  /* T80_MCode.vhd:720:69  */
  assign n3305_o = ir == 8'b10110100;
  /* T80_MCode.vhd:720:69  */
  assign n3306_o = n3303_o | n3305_o;
  /* T80_MCode.vhd:720:80  */
  assign n3308_o = ir == 8'b10110101;
  /* T80_MCode.vhd:720:80  */
  assign n3309_o = n3306_o | n3308_o;
  /* T80_MCode.vhd:720:91  */
  assign n3311_o = ir == 8'b10110111;
  /* T80_MCode.vhd:720:91  */
  assign n3312_o = n3309_o | n3311_o;
  /* T80_MCode.vhd:721:25  */
  assign n3314_o = ir == 8'b10111000;
  /* T80_MCode.vhd:721:25  */
  assign n3315_o = n3312_o | n3314_o;
  /* T80_MCode.vhd:721:36  */
  assign n3317_o = ir == 8'b10111001;
  /* T80_MCode.vhd:721:36  */
  assign n3318_o = n3315_o | n3317_o;
  /* T80_MCode.vhd:721:47  */
  assign n3320_o = ir == 8'b10111010;
  /* T80_MCode.vhd:721:47  */
  assign n3321_o = n3318_o | n3320_o;
  /* T80_MCode.vhd:721:58  */
  assign n3323_o = ir == 8'b10111011;
  /* T80_MCode.vhd:721:58  */
  assign n3324_o = n3321_o | n3323_o;
  /* T80_MCode.vhd:721:69  */
  assign n3326_o = ir == 8'b10111100;
  /* T80_MCode.vhd:721:69  */
  assign n3327_o = n3324_o | n3326_o;
  /* T80_MCode.vhd:721:80  */
  assign n3329_o = ir == 8'b10111101;
  /* T80_MCode.vhd:721:80  */
  assign n3330_o = n3327_o | n3329_o;
  /* T80_MCode.vhd:721:91  */
  assign n3332_o = ir == 8'b10111111;
  /* T80_MCode.vhd:721:91  */
  assign n3333_o = n3330_o | n3332_o;
  /* T80_MCode.vhd:744:30  */
  assign n3334_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:745:25  */
  assign n3336_o = n3334_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:747:25  */
  assign n3339_o = n3334_o == 31'b0000000000000000000000000000010;
  assign n3340_o = {n3339_o, n3336_o};
  /* T80_MCode.vhd:744:25  */
  always @*
    case (n3340_o)
      2'b10: n3343_o = 1'b1;
      2'b01: n3343_o = 1'b0;
      default: n3343_o = 1'b0;
    endcase
  /* T80_MCode.vhd:744:25  */
  always @*
    case (n3340_o)
      2'b10: n3345_o = 3'b111;
      2'b01: n3345_o = 3'b000;
      default: n3345_o = 3'b000;
    endcase
  /* T80_MCode.vhd:744:25  */
  always @*
    case (n3340_o)
      2'b10: n3347_o = n2491_o;
      2'b01: n3347_o = 3'b000;
      default: n3347_o = 3'b000;
    endcase
  /* T80_MCode.vhd:744:25  */
  always @*
    case (n3340_o)
      2'b10: n3350_o = 1'b1;
      2'b01: n3350_o = 1'b0;
      default: n3350_o = 1'b0;
    endcase
  /* T80_MCode.vhd:744:25  */
  always @*
    case (n3340_o)
      2'b10: n3353_o = 3'b111;
      2'b01: n3353_o = 3'b010;
      default: n3353_o = 3'b111;
    endcase
  /* T80_MCode.vhd:734:17  */
  assign n3355_o = ir == 8'b10000110;
  /* T80_MCode.vhd:734:32  */
  assign n3357_o = ir == 8'b10001110;
  /* T80_MCode.vhd:734:32  */
  assign n3358_o = n3355_o | n3357_o;
  /* T80_MCode.vhd:734:43  */
  assign n3360_o = ir == 8'b10010110;
  /* T80_MCode.vhd:734:43  */
  assign n3361_o = n3358_o | n3360_o;
  /* T80_MCode.vhd:734:54  */
  assign n3363_o = ir == 8'b10011110;
  /* T80_MCode.vhd:734:54  */
  assign n3364_o = n3361_o | n3363_o;
  /* T80_MCode.vhd:734:65  */
  assign n3366_o = ir == 8'b10100110;
  /* T80_MCode.vhd:734:65  */
  assign n3367_o = n3364_o | n3366_o;
  /* T80_MCode.vhd:734:76  */
  assign n3369_o = ir == 8'b10101110;
  /* T80_MCode.vhd:734:76  */
  assign n3370_o = n3367_o | n3369_o;
  /* T80_MCode.vhd:734:87  */
  assign n3372_o = ir == 8'b10110110;
  /* T80_MCode.vhd:734:87  */
  assign n3373_o = n3370_o | n3372_o;
  /* T80_MCode.vhd:734:98  */
  assign n3375_o = ir == 8'b10111110;
  /* T80_MCode.vhd:734:98  */
  assign n3376_o = n3373_o | n3375_o;
  /* T80_MCode.vhd:764:35  */
  assign n3378_o = mcycle == 3'b010;
  /* T80_MCode.vhd:764:25  */
  assign n3382_o = n3378_o ? 1'b1 : 1'b0;
  /* T80_MCode.vhd:764:25  */
  assign n3385_o = n3378_o ? 1'b1 : 1'b0;
  /* T80_MCode.vhd:764:25  */
  assign n3387_o = n3378_o ? 3'b111 : 3'b000;
  /* T80_MCode.vhd:764:25  */
  assign n3389_o = n3378_o ? n2491_o : 3'b000;
  /* T80_MCode.vhd:764:25  */
  assign n3392_o = n3378_o ? 1'b1 : 1'b0;
  /* T80_MCode.vhd:754:17  */
  assign n3394_o = ir == 8'b11000110;
  /* T80_MCode.vhd:754:32  */
  assign n3396_o = ir == 8'b11001110;
  /* T80_MCode.vhd:754:32  */
  assign n3397_o = n3394_o | n3396_o;
  /* T80_MCode.vhd:754:43  */
  assign n3399_o = ir == 8'b11010110;
  /* T80_MCode.vhd:754:43  */
  assign n3400_o = n3397_o | n3399_o;
  /* T80_MCode.vhd:754:54  */
  assign n3402_o = ir == 8'b11011110;
  /* T80_MCode.vhd:754:54  */
  assign n3403_o = n3400_o | n3402_o;
  /* T80_MCode.vhd:754:65  */
  assign n3405_o = ir == 8'b11100110;
  /* T80_MCode.vhd:754:65  */
  assign n3406_o = n3403_o | n3405_o;
  /* T80_MCode.vhd:754:76  */
  assign n3408_o = ir == 8'b11101110;
  /* T80_MCode.vhd:754:76  */
  assign n3409_o = n3406_o | n3408_o;
  /* T80_MCode.vhd:754:87  */
  assign n3411_o = ir == 8'b11110110;
  /* T80_MCode.vhd:754:87  */
  assign n3412_o = n3409_o | n3411_o;
  /* T80_MCode.vhd:754:98  */
  assign n3414_o = ir == 8'b11111110;
  /* T80_MCode.vhd:754:98  */
  assign n3415_o = n3412_o | n3414_o;
  /* T80_MCode.vhd:771:17  */
  assign n3417_o = ir == 8'b00000100;
  /* T80_MCode.vhd:771:32  */
  assign n3419_o = ir == 8'b00001100;
  /* T80_MCode.vhd:771:32  */
  assign n3420_o = n3417_o | n3419_o;
  /* T80_MCode.vhd:771:43  */
  assign n3422_o = ir == 8'b00010100;
  /* T80_MCode.vhd:771:43  */
  assign n3423_o = n3420_o | n3422_o;
  /* T80_MCode.vhd:771:54  */
  assign n3425_o = ir == 8'b00011100;
  /* T80_MCode.vhd:771:54  */
  assign n3426_o = n3423_o | n3425_o;
  /* T80_MCode.vhd:771:65  */
  assign n3428_o = ir == 8'b00100100;
  /* T80_MCode.vhd:771:65  */
  assign n3429_o = n3426_o | n3428_o;
  /* T80_MCode.vhd:771:76  */
  assign n3431_o = ir == 8'b00101100;
  /* T80_MCode.vhd:771:76  */
  assign n3432_o = n3429_o | n3431_o;
  /* T80_MCode.vhd:771:87  */
  assign n3434_o = ir == 8'b00111100;
  /* T80_MCode.vhd:771:87  */
  assign n3435_o = n3432_o | n3434_o;
  /* T80_MCode.vhd:782:30  */
  assign n3436_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:783:25  */
  assign n3438_o = n3436_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:785:25  */
  assign n3440_o = n3436_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:794:25  */
  assign n3442_o = n3436_o == 31'b0000000000000000000000000000011;
  assign n3443_o = {n3442_o, n3440_o, n3438_o};
  /* T80_MCode.vhd:782:25  */
  always @*
    case (n3443_o)
      3'b100: n3445_o = n2497_o;
      3'b010: n3445_o = 3'b100;
      3'b001: n3445_o = n2497_o;
      default: n3445_o = n2497_o;
    endcase
  /* T80_MCode.vhd:782:25  */
  always @*
    case (n3443_o)
      3'b100: n3448_o = 1'b0;
      3'b010: n3448_o = 1'b1;
      3'b001: n3448_o = 1'b0;
      default: n3448_o = 1'b0;
    endcase
  /* T80_MCode.vhd:782:25  */
  always @*
    case (n3443_o)
      3'b100: n3450_o = 3'b000;
      3'b010: n3450_o = n2490_o;
      3'b001: n3450_o = 3'b000;
      default: n3450_o = 3'b000;
    endcase
  /* T80_MCode.vhd:782:25  */
  always @*
    case (n3443_o)
      3'b100: n3453_o = 4'b0000;
      3'b010: n3453_o = 4'b1010;
      3'b001: n3453_o = 4'b0000;
      default: n3453_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:782:25  */
  always @*
    case (n3443_o)
      3'b100: n3455_o = n2500_o;
      3'b010: n3455_o = 4'b0000;
      3'b001: n3455_o = n2500_o;
      default: n3455_o = n2500_o;
    endcase
  /* T80_MCode.vhd:782:25  */
  always @*
    case (n3443_o)
      3'b100: n3458_o = 1'b0;
      3'b010: n3458_o = 1'b1;
      3'b001: n3458_o = 1'b0;
      default: n3458_o = 1'b0;
    endcase
  /* T80_MCode.vhd:782:25  */
  always @*
    case (n3443_o)
      3'b100: n3461_o = 1'b0;
      3'b010: n3461_o = 1'b1;
      3'b001: n3461_o = 1'b0;
      default: n3461_o = 1'b0;
    endcase
  /* T80_MCode.vhd:782:25  */
  always @*
    case (n3443_o)
      3'b100: n3465_o = 3'b111;
      3'b010: n3465_o = 3'b010;
      3'b001: n3465_o = 3'b010;
      default: n3465_o = 3'b111;
    endcase
  /* T80_MCode.vhd:782:25  */
  always @*
    case (n3443_o)
      3'b100: n3468_o = 1'b1;
      3'b010: n3468_o = 1'b0;
      3'b001: n3468_o = 1'b0;
      default: n3468_o = 1'b0;
    endcase
  /* T80_MCode.vhd:779:17  */
  assign n3470_o = ir == 8'b00110100;
  /* T80_MCode.vhd:798:17  */
  assign n3472_o = ir == 8'b00000101;
  /* T80_MCode.vhd:798:32  */
  assign n3474_o = ir == 8'b00001101;
  /* T80_MCode.vhd:798:32  */
  assign n3475_o = n3472_o | n3474_o;
  /* T80_MCode.vhd:798:43  */
  assign n3477_o = ir == 8'b00010101;
  /* T80_MCode.vhd:798:43  */
  assign n3478_o = n3475_o | n3477_o;
  /* T80_MCode.vhd:798:54  */
  assign n3480_o = ir == 8'b00011101;
  /* T80_MCode.vhd:798:54  */
  assign n3481_o = n3478_o | n3480_o;
  /* T80_MCode.vhd:798:65  */
  assign n3483_o = ir == 8'b00100101;
  /* T80_MCode.vhd:798:65  */
  assign n3484_o = n3481_o | n3483_o;
  /* T80_MCode.vhd:798:76  */
  assign n3486_o = ir == 8'b00101101;
  /* T80_MCode.vhd:798:76  */
  assign n3487_o = n3484_o | n3486_o;
  /* T80_MCode.vhd:798:87  */
  assign n3489_o = ir == 8'b00111101;
  /* T80_MCode.vhd:798:87  */
  assign n3490_o = n3487_o | n3489_o;
  /* T80_MCode.vhd:809:30  */
  assign n3491_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:810:25  */
  assign n3493_o = n3491_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:812:25  */
  assign n3495_o = n3491_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:821:25  */
  assign n3497_o = n3491_o == 31'b0000000000000000000000000000011;
  assign n3498_o = {n3497_o, n3495_o, n3493_o};
  /* T80_MCode.vhd:809:25  */
  always @*
    case (n3498_o)
      3'b100: n3500_o = n2497_o;
      3'b010: n3500_o = 3'b100;
      3'b001: n3500_o = n2497_o;
      default: n3500_o = n2497_o;
    endcase
  /* T80_MCode.vhd:809:25  */
  always @*
    case (n3498_o)
      3'b100: n3503_o = 1'b0;
      3'b010: n3503_o = 1'b1;
      3'b001: n3503_o = 1'b0;
      default: n3503_o = 1'b0;
    endcase
  /* T80_MCode.vhd:809:25  */
  always @*
    case (n3498_o)
      3'b100: n3505_o = 3'b000;
      3'b010: n3505_o = n2490_o;
      3'b001: n3505_o = 3'b000;
      default: n3505_o = 3'b000;
    endcase
  /* T80_MCode.vhd:809:25  */
  always @*
    case (n3498_o)
      3'b100: n3508_o = 4'b0000;
      3'b010: n3508_o = 4'b1010;
      3'b001: n3508_o = 4'b0000;
      default: n3508_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:809:25  */
  always @*
    case (n3498_o)
      3'b100: n3510_o = n2500_o;
      3'b010: n3510_o = 4'b0010;
      3'b001: n3510_o = n2500_o;
      default: n3510_o = n2500_o;
    endcase
  /* T80_MCode.vhd:809:25  */
  always @*
    case (n3498_o)
      3'b100: n3513_o = 1'b0;
      3'b010: n3513_o = 1'b1;
      3'b001: n3513_o = 1'b0;
      default: n3513_o = 1'b0;
    endcase
  /* T80_MCode.vhd:809:25  */
  always @*
    case (n3498_o)
      3'b100: n3516_o = 1'b0;
      3'b010: n3516_o = 1'b1;
      3'b001: n3516_o = 1'b0;
      default: n3516_o = 1'b0;
    endcase
  /* T80_MCode.vhd:809:25  */
  always @*
    case (n3498_o)
      3'b100: n3520_o = 3'b111;
      3'b010: n3520_o = 3'b010;
      3'b001: n3520_o = 3'b010;
      default: n3520_o = 3'b111;
    endcase
  /* T80_MCode.vhd:809:25  */
  always @*
    case (n3498_o)
      3'b100: n3523_o = 1'b1;
      3'b010: n3523_o = 1'b0;
      3'b001: n3523_o = 1'b0;
      default: n3523_o = 1'b0;
    endcase
  /* T80_MCode.vhd:806:17  */
  assign n3525_o = ir == 8'b00110101;
  /* T80_MCode.vhd:827:17  */
  assign n3528_o = ir == 8'b00100111;
  /* T80_MCode.vhd:833:17  */
  assign n3530_o = ir == 8'b00101111;
  /* T80_MCode.vhd:836:17  */
  assign n3532_o = ir == 8'b00111111;
  /* T80_MCode.vhd:839:17  */
  assign n3534_o = ir == 8'b00110111;
  /* T80_MCode.vhd:846:38  */
  assign n3535_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:847:33  */
  assign n3537_o = n3535_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:852:33  */
  assign n3539_o = n3535_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:857:33  */
  assign n3541_o = n3535_o == 31'b0000000000000000000000000000011;
  assign n3542_o = {n3541_o, n3539_o, n3537_o};
  /* T80_MCode.vhd:846:33  */
  always @*
    case (n3542_o)
      3'b100: n3544_o = n2497_o;
      3'b010: n3544_o = n2497_o;
      3'b001: n3544_o = 3'b101;
      default: n3544_o = n2497_o;
    endcase
  /* T80_MCode.vhd:846:33  */
  always @*
    case (n3542_o)
      3'b100: n3548_o = 4'b0000;
      3'b010: n3548_o = 4'b1111;
      3'b001: n3548_o = 4'b1111;
      default: n3548_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:846:33  */
  always @*
    case (n3542_o)
      3'b100: n3552_o = 4'b0000;
      3'b010: n3552_o = 4'b1100;
      3'b001: n3552_o = 4'b1101;
      default: n3552_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:846:33  */
  always @*
    case (n3542_o)
      3'b100: n3556_o = 3'b111;
      3'b010: n3556_o = 3'b101;
      3'b001: n3556_o = 3'b101;
      default: n3556_o = 3'b111;
    endcase
  /* T80_MCode.vhd:846:33  */
  always @*
    case (n3542_o)
      3'b100: n3560_o = 1'b1;
      3'b010: n3560_o = 1'b1;
      3'b001: n3560_o = 1'b0;
      default: n3560_o = 1'b0;
    endcase
  /* T80_MCode.vhd:884:46  */
  assign n3561_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:885:41  */
  assign n3563_o = n3561_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:890:41  */
  assign n3565_o = n3561_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:896:41  */
  assign n3567_o = n3561_o == 31'b0000000000000000000000000000011;
  /* T80_MCode.vhd:899:41  */
  assign n3569_o = n3561_o == 31'b0000000000000000000000000000100;
  /* T80_MCode.vhd:902:41  */
  assign n3571_o = n3561_o == 31'b0000000000000000000000000000101;
  assign n3572_o = {n3571_o, n3569_o, n3567_o, n3565_o, n3563_o};
  /* T80_MCode.vhd:884:41  */
  always @*
    case (n3572_o)
      5'b10000: n3574_o = n2497_o;
      5'b01000: n3574_o = n2497_o;
      5'b00100: n3574_o = n2497_o;
      5'b00010: n3574_o = n2497_o;
      5'b00001: n3574_o = 3'b101;
      default: n3574_o = n2497_o;
    endcase
  /* T80_MCode.vhd:884:41  */
  always @*
    case (n3572_o)
      5'b10000: n3577_o = 1'b0;
      5'b01000: n3577_o = 1'b1;
      5'b00100: n3577_o = 1'b0;
      5'b00010: n3577_o = 1'b0;
      5'b00001: n3577_o = 1'b0;
      default: n3577_o = 1'b0;
    endcase
  /* T80_MCode.vhd:884:41  */
  always @*
    case (n3572_o)
      5'b10000: n3581_o = 4'b0000;
      5'b01000: n3581_o = 4'b0000;
      5'b00100: n3581_o = 4'b0000;
      5'b00010: n3581_o = 4'b1111;
      5'b00001: n3581_o = 4'b1111;
      default: n3581_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:884:41  */
  always @*
    case (n3572_o)
      5'b10000: n3585_o = 4'b0000;
      5'b01000: n3585_o = 4'b0000;
      5'b00100: n3585_o = 4'b0000;
      5'b00010: n3585_o = 4'b1100;
      5'b00001: n3585_o = 4'b1101;
      default: n3585_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:884:41  */
  always @*
    case (n3572_o)
      5'b10000: n3589_o = 3'b111;
      5'b01000: n3589_o = 3'b111;
      5'b00100: n3589_o = 3'b111;
      5'b00010: n3589_o = 3'b101;
      5'b00001: n3589_o = 3'b101;
      default: n3589_o = 3'b111;
    endcase
  /* T80_MCode.vhd:884:41  */
  always @*
    case (n3572_o)
      5'b10000: n3592_o = 1'b1;
      5'b01000: n3592_o = 1'b0;
      5'b00100: n3592_o = 1'b0;
      5'b00010: n3592_o = 1'b0;
      5'b00001: n3592_o = 1'b0;
      default: n3592_o = 1'b0;
    endcase
  /* T80_MCode.vhd:884:41  */
  always @*
    case (n3572_o)
      5'b10000: n3595_o = 1'b0;
      5'b01000: n3595_o = 1'b1;
      5'b00100: n3595_o = 1'b0;
      5'b00010: n3595_o = 1'b0;
      5'b00001: n3595_o = 1'b0;
      default: n3595_o = 1'b0;
    endcase
  /* T80_MCode.vhd:884:41  */
  always @*
    case (n3572_o)
      5'b10000: n3599_o = 1'b0;
      5'b01000: n3599_o = 1'b0;
      5'b00100: n3599_o = 1'b1;
      5'b00010: n3599_o = 1'b1;
      5'b00001: n3599_o = 1'b0;
      default: n3599_o = 1'b0;
    endcase
  /* T80_MCode.vhd:861:25  */
  assign n3602_o = intcycle ? 3'b101 : 3'b001;
  /* T80_MCode.vhd:861:25  */
  assign n3603_o = intcycle ? n3574_o : n2497_o;
  /* T80_MCode.vhd:861:25  */
  assign n3605_o = intcycle ? n3577_o : 1'b0;
  /* T80_MCode.vhd:861:25  */
  assign n3607_o = intcycle ? n3581_o : 4'b0000;
  /* T80_MCode.vhd:861:25  */
  assign n3609_o = intcycle ? n3585_o : 4'b0000;
  /* T80_MCode.vhd:861:25  */
  assign n3611_o = intcycle ? n3589_o : 3'b111;
  /* T80_MCode.vhd:861:25  */
  assign n3613_o = intcycle ? n3592_o : 1'b0;
  /* T80_MCode.vhd:861:25  */
  assign n3615_o = intcycle ? n3595_o : 1'b0;
  /* T80_MCode.vhd:861:25  */
  assign n3617_o = intcycle ? n3599_o : 1'b0;
  /* T80_MCode.vhd:843:25  */
  assign n3619_o = nmicycle ? 3'b011 : n3602_o;
  /* T80_MCode.vhd:843:25  */
  assign n3620_o = nmicycle ? n3544_o : n3603_o;
  /* T80_MCode.vhd:843:25  */
  assign n3622_o = nmicycle ? 1'b0 : n3605_o;
  /* T80_MCode.vhd:843:25  */
  assign n3623_o = nmicycle ? n3548_o : n3607_o;
  /* T80_MCode.vhd:843:25  */
  assign n3624_o = nmicycle ? n3552_o : n3609_o;
  /* T80_MCode.vhd:843:25  */
  assign n3625_o = nmicycle ? n3556_o : n3611_o;
  /* T80_MCode.vhd:843:25  */
  assign n3627_o = nmicycle ? 1'b0 : n3613_o;
  /* T80_MCode.vhd:843:25  */
  assign n3629_o = nmicycle ? 1'b0 : n3615_o;
  /* T80_MCode.vhd:843:25  */
  assign n3630_o = nmicycle ? n3560_o : n3617_o;
  /* T80_MCode.vhd:842:17  */
  assign n3632_o = ir == 8'b00000000;
  /* T80_MCode.vhd:910:17  */
  assign n3634_o = ir == 8'b01110110;
  /* T80_MCode.vhd:913:17  */
  assign n3636_o = ir == 8'b11110011;
  /* T80_MCode.vhd:916:17  */
  assign n3638_o = ir == 8'b11111011;
  /* T80_MCode.vhd:959:38  */
  assign n3639_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:960:33  */
  assign n3641_o = n3639_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:968:68  */
  assign n3643_o = ir[5:4]; // extract
  /* T80_MCode.vhd:968:46  */
  assign n3644_o = {29'b0, n3643_o};  //  uext
  /* T80_MCode.vhd:970:78  */
  assign n3645_o = ir[5:4]; // extract
  /* T80_MCode.vhd:969:41  */
  assign n3648_o = n3644_o == 31'b0000000000000000000000000000000;
  /* T80_MCode.vhd:969:47  */
  assign n3650_o = n3644_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:969:47  */
  assign n3651_o = n3648_o | n3650_o;
  /* T80_MCode.vhd:969:49  */
  assign n3653_o = n3644_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:969:49  */
  assign n3654_o = n3651_o | n3653_o;
  /* T80_MCode.vhd:968:41  */
  always @*
    case (n3654_o)
      1'b1: n3656_o = 1'b1;
      default: n3656_o = 1'b0;
    endcase
  /* T80_MCode.vhd:968:41  */
  always @*
    case (n3654_o)
      1'b1: n3658_o = n3645_o;
      default: n3658_o = 2'b00;
    endcase
  /* T80_MCode.vhd:968:41  */
  always @*
    case (n3654_o)
      1'b1: n3661_o = 1'b0;
      default: n3661_o = 1'b1;
    endcase
  /* T80_MCode.vhd:962:33  */
  assign n3663_o = n3639_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:985:68  */
  assign n3665_o = ir[5:4]; // extract
  /* T80_MCode.vhd:985:46  */
  assign n3666_o = {29'b0, n3665_o};  //  uext
  /* T80_MCode.vhd:987:78  */
  assign n3667_o = ir[5:4]; // extract
  /* T80_MCode.vhd:986:41  */
  assign n3669_o = n3666_o == 31'b0000000000000000000000000000000;
  /* T80_MCode.vhd:986:47  */
  assign n3671_o = n3666_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:986:47  */
  assign n3672_o = n3669_o | n3671_o;
  /* T80_MCode.vhd:986:49  */
  assign n3674_o = n3666_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:986:49  */
  assign n3675_o = n3672_o | n3674_o;
  /* T80_MCode.vhd:985:41  */
  always @*
    case (n3675_o)
      1'b1: n3678_o = 1'b0;
      default: n3678_o = 1'b1;
    endcase
  /* T80_MCode.vhd:985:41  */
  always @*
    case (n3675_o)
      1'b1: n3680_o = n3667_o;
      default: n3680_o = 2'b00;
    endcase
  /* T80_MCode.vhd:985:41  */
  always @*
    case (n3675_o)
      1'b1: n3683_o = 1'b0;
      default: n3683_o = 1'b1;
    endcase
  /* T80_MCode.vhd:979:33  */
  assign n3685_o = n3639_o == 31'b0000000000000000000000000000011;
  assign n3686_o = {n3685_o, n3663_o, n3641_o};
  /* T80_MCode.vhd:959:33  */
  always @*
    case (n3686_o)
      3'b100: n3688_o = n2497_o;
      3'b010: n3688_o = 3'b100;
      3'b001: n3688_o = n2497_o;
      default: n3688_o = n2497_o;
    endcase
  /* T80_MCode.vhd:959:33  */
  always @*
    case (n3686_o)
      3'b100: n3692_o = 1'b1;
      3'b010: n3692_o = 1'b1;
      3'b001: n3692_o = 1'b0;
      default: n3692_o = 1'b0;
    endcase
  /* T80_MCode.vhd:959:33  */
  always @*
    case (n3686_o)
      3'b100: n3694_o = 3'b100;
      3'b010: n3694_o = 3'b101;
      3'b001: n3694_o = 3'b000;
      default: n3694_o = 3'b000;
    endcase
  /* T80_MCode.vhd:959:33  */
  always @*
    case (n3686_o)
      3'b100: n3696_o = n3678_o;
      3'b010: n3696_o = n3656_o;
      3'b001: n3696_o = 1'b0;
      default: n3696_o = 1'b0;
    endcase
  /* T80_MCode.vhd:959:33  */
  always @*
    case (n3686_o)
      3'b100: n3698_o = n3680_o;
      3'b010: n3698_o = n3658_o;
      3'b001: n3698_o = 2'b00;
      default: n3698_o = 2'b00;
    endcase
  /* T80_MCode.vhd:959:33  */
  always @*
    case (n3686_o)
      3'b100: n3700_o = n3683_o;
      3'b010: n3700_o = n3661_o;
      3'b001: n3700_o = 1'b0;
      default: n3700_o = 1'b0;
    endcase
  /* T80_MCode.vhd:959:33  */
  always @*
    case (n3686_o)
      3'b100: n3703_o = 4'b0001;
      3'b010: n3703_o = 4'b0000;
      3'b001: n3703_o = n2500_o;
      default: n3703_o = n2500_o;
    endcase
  /* T80_MCode.vhd:959:33  */
  always @*
    case (n3686_o)
      3'b100: n3707_o = 1'b1;
      3'b010: n3707_o = 1'b1;
      3'b001: n3707_o = 1'b0;
      default: n3707_o = 1'b0;
    endcase
  /* T80_MCode.vhd:959:33  */
  always @*
    case (n3686_o)
      3'b100: n3711_o = 1'b1;
      3'b010: n3711_o = 1'b1;
      3'b001: n3711_o = 1'b0;
      default: n3711_o = 1'b0;
    endcase
  /* T80_MCode.vhd:959:33  */
  always @*
    case (n3686_o)
      3'b100: n3714_o = 2'b00;
      3'b010: n3714_o = 2'b11;
      3'b001: n3714_o = 2'b00;
      default: n3714_o = 2'b00;
    endcase
  /* T80_MCode.vhd:959:33  */
  always @*
    case (n3686_o)
      3'b100: n3718_o = 1'b1;
      3'b010: n3718_o = 1'b1;
      3'b001: n3718_o = 1'b0;
      default: n3718_o = 1'b0;
    endcase
  /* T80_MCode.vhd:959:33  */
  always @*
    case (n3686_o)
      3'b100: n3722_o = 1'b0;
      3'b010: n3722_o = 1'b1;
      3'b001: n3722_o = 1'b1;
      default: n3722_o = 1'b0;
    endcase
  /* T80_MCode.vhd:921:17  */
  assign n3724_o = ir == 8'b00001001;
  /* T80_MCode.vhd:921:32  */
  assign n3726_o = ir == 8'b00011001;
  /* T80_MCode.vhd:921:32  */
  assign n3727_o = n3724_o | n3726_o;
  /* T80_MCode.vhd:921:43  */
  assign n3729_o = ir == 8'b00101001;
  /* T80_MCode.vhd:921:43  */
  assign n3730_o = n3727_o | n3729_o;
  /* T80_MCode.vhd:921:54  */
  assign n3732_o = ir == 8'b00111001;
  /* T80_MCode.vhd:921:54  */
  assign n3733_o = n3730_o | n3732_o;
  /* T80_MCode.vhd:995:17  */
  assign n3736_o = ir == 8'b00000011;
  /* T80_MCode.vhd:995:32  */
  assign n3738_o = ir == 8'b00010011;
  /* T80_MCode.vhd:995:32  */
  assign n3739_o = n3736_o | n3738_o;
  /* T80_MCode.vhd:995:43  */
  assign n3741_o = ir == 8'b00100011;
  /* T80_MCode.vhd:995:43  */
  assign n3742_o = n3739_o | n3741_o;
  /* T80_MCode.vhd:995:54  */
  assign n3744_o = ir == 8'b00110011;
  /* T80_MCode.vhd:995:54  */
  assign n3745_o = n3742_o | n3744_o;
  /* T80_MCode.vhd:1010:17  */
  assign n3748_o = ir == 8'b00001011;
  /* T80_MCode.vhd:1010:32  */
  assign n3750_o = ir == 8'b00011011;
  /* T80_MCode.vhd:1010:32  */
  assign n3751_o = n3748_o | n3750_o;
  /* T80_MCode.vhd:1010:43  */
  assign n3753_o = ir == 8'b00101011;
  /* T80_MCode.vhd:1010:43  */
  assign n3754_o = n3751_o | n3753_o;
  /* T80_MCode.vhd:1010:54  */
  assign n3756_o = ir == 8'b00111011;
  /* T80_MCode.vhd:1010:54  */
  assign n3757_o = n3754_o | n3756_o;
  /* T80_MCode.vhd:1027:17  */
  assign n3760_o = ir == 8'b00000111;
  /* T80_MCode.vhd:1029:25  */
  assign n3762_o = ir == 8'b00010111;
  /* T80_MCode.vhd:1029:25  */
  assign n3763_o = n3760_o | n3762_o;
  /* T80_MCode.vhd:1031:25  */
  assign n3765_o = ir == 8'b00001111;
  /* T80_MCode.vhd:1031:25  */
  assign n3766_o = n3763_o | n3765_o;
  /* T80_MCode.vhd:1033:25  */
  assign n3768_o = ir == 8'b00011111;
  /* T80_MCode.vhd:1033:25  */
  assign n3769_o = n3766_o | n3768_o;
  /* T80_MCode.vhd:1049:30  */
  assign n3770_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:1050:25  */
  assign n3772_o = n3770_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:1053:25  */
  assign n3774_o = n3770_o == 31'b0000000000000000000000000000011;
  assign n3775_o = {n3774_o, n3772_o};
  /* T80_MCode.vhd:1049:25  */
  always @*
    case (n3775_o)
      2'b10: n3779_o = 1'b1;
      2'b01: n3779_o = 1'b1;
      default: n3779_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1049:25  */
  always @*
    case (n3775_o)
      2'b10: n3782_o = 1'b1;
      2'b01: n3782_o = 1'b0;
      default: n3782_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1049:25  */
  always @*
    case (n3775_o)
      2'b10: n3785_o = 1'b0;
      2'b01: n3785_o = 1'b1;
      default: n3785_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1049:25  */
  always @*
    case (n3775_o)
      2'b10: n3788_o = 1'b1;
      2'b01: n3788_o = 1'b0;
      default: n3788_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1042:17  */
  assign n3790_o = ir == 8'b11000011;
  /* T80_MCode.vhd:1124:38  */
  assign n3792_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:1125:33  */
  assign n3794_o = n3792_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:1133:73  */
  assign n3796_o = ir[5:3]; // extract
  /* T80_MCode.vhd:180:47  */
  assign n3801_o = f[6]; // extract
  /* T80_MCode.vhd:180:56  */
  assign n3802_o = ~n3801_o;
  /* T80_MCode.vhd:180:25  */
  assign n3804_o = n3796_o == 3'b000;
  /* T80_MCode.vhd:181:47  */
  assign n3805_o = f[6]; // extract
  /* T80_MCode.vhd:181:25  */
  assign n3807_o = n3796_o == 3'b001;
  /* T80_MCode.vhd:182:47  */
  assign n3808_o = f[0]; // extract
  /* T80_MCode.vhd:182:56  */
  assign n3809_o = ~n3808_o;
  /* T80_MCode.vhd:182:25  */
  assign n3811_o = n3796_o == 3'b010;
  /* T80_MCode.vhd:183:47  */
  assign n3812_o = f[0]; // extract
  /* T80_MCode.vhd:183:25  */
  assign n3814_o = n3796_o == 3'b011;
  /* T80_MCode.vhd:184:47  */
  assign n3815_o = f[2]; // extract
  /* T80_MCode.vhd:184:56  */
  assign n3816_o = ~n3815_o;
  /* T80_MCode.vhd:184:25  */
  assign n3818_o = n3796_o == 3'b100;
  /* T80_MCode.vhd:185:47  */
  assign n3819_o = f[2]; // extract
  /* T80_MCode.vhd:185:25  */
  assign n3821_o = n3796_o == 3'b101;
  /* T80_MCode.vhd:186:47  */
  assign n3822_o = f[7]; // extract
  /* T80_MCode.vhd:186:56  */
  assign n3823_o = ~n3822_o;
  /* T80_MCode.vhd:186:25  */
  assign n3825_o = n3796_o == 3'b110;
  /* T80_MCode.vhd:187:47  */
  assign n3826_o = f[7]; // extract
  /* T80_MCode.vhd:187:25  */
  assign n3828_o = n3796_o == 3'b111;
  assign n3829_o = {n3828_o, n3825_o, n3821_o, n3818_o, n3814_o, n3811_o, n3807_o, n3804_o};
  /* T80_MCode.vhd:179:25  */
  always @*
    case (n3829_o)
      8'b10000000: n3831_o = n3826_o;
      8'b01000000: n3831_o = n3823_o;
      8'b00100000: n3831_o = n3819_o;
      8'b00010000: n3831_o = n3816_o;
      8'b00001000: n3831_o = n3812_o;
      8'b00000100: n3831_o = n3809_o;
      8'b00000010: n3831_o = n3805_o;
      8'b00000001: n3831_o = n3802_o;
      default: n3831_o = 1'bX;
    endcase
  /* T80_MCode.vhd:1133:41  */
  assign n3834_o = n3831_o ? 1'b1 : 1'b0;
  /* T80_MCode.vhd:1128:33  */
  assign n3836_o = n3792_o == 31'b0000000000000000000000000000011;
  assign n3837_o = {n3836_o, n3794_o};
  /* T80_MCode.vhd:1124:33  */
  always @*
    case (n3837_o)
      2'b10: n3841_o = 1'b1;
      2'b01: n3841_o = 1'b1;
      default: n3841_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1124:33  */
  always @*
    case (n3837_o)
      2'b10: n3843_o = n3834_o;
      2'b01: n3843_o = 1'b0;
      default: n3843_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1124:33  */
  always @*
    case (n3837_o)
      2'b10: n3846_o = 1'b0;
      2'b01: n3846_o = 1'b1;
      default: n3846_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1124:33  */
  always @*
    case (n3837_o)
      2'b10: n3849_o = 1'b1;
      2'b01: n3849_o = 1'b0;
      default: n3849_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1061:17  */
  assign n3851_o = ir == 8'b11000010;
  /* T80_MCode.vhd:1061:32  */
  assign n3853_o = ir == 8'b11001010;
  /* T80_MCode.vhd:1061:32  */
  assign n3854_o = n3851_o | n3853_o;
  /* T80_MCode.vhd:1061:43  */
  assign n3856_o = ir == 8'b11010010;
  /* T80_MCode.vhd:1061:43  */
  assign n3857_o = n3854_o | n3856_o;
  /* T80_MCode.vhd:1061:54  */
  assign n3859_o = ir == 8'b11011010;
  /* T80_MCode.vhd:1061:54  */
  assign n3860_o = n3857_o | n3859_o;
  /* T80_MCode.vhd:1061:65  */
  assign n3862_o = ir == 8'b11100010;
  /* T80_MCode.vhd:1061:65  */
  assign n3863_o = n3860_o | n3862_o;
  /* T80_MCode.vhd:1061:76  */
  assign n3865_o = ir == 8'b11101010;
  /* T80_MCode.vhd:1061:76  */
  assign n3866_o = n3863_o | n3865_o;
  /* T80_MCode.vhd:1061:87  */
  assign n3868_o = ir == 8'b11110010;
  /* T80_MCode.vhd:1061:87  */
  assign n3869_o = n3866_o | n3868_o;
  /* T80_MCode.vhd:1061:98  */
  assign n3871_o = ir == 8'b11111010;
  /* T80_MCode.vhd:1061:98  */
  assign n3872_o = n3869_o | n3871_o;
  /* T80_MCode.vhd:1145:38  */
  assign n3873_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:1146:33  */
  assign n3875_o = n3873_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:1149:33  */
  assign n3877_o = n3873_o == 31'b0000000000000000000000000000011;
  assign n3878_o = {n3877_o, n3875_o};
  /* T80_MCode.vhd:1145:33  */
  always @*
    case (n3878_o)
      2'b10: n3880_o = 3'b101;
      2'b01: n3880_o = n2497_o;
      default: n3880_o = n2497_o;
    endcase
  /* T80_MCode.vhd:1145:33  */
  always @*
    case (n3878_o)
      2'b10: n3883_o = 1'b0;
      2'b01: n3883_o = 1'b1;
      default: n3883_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1145:33  */
  always @*
    case (n3878_o)
      2'b10: n3886_o = 1'b1;
      2'b01: n3886_o = 1'b0;
      default: n3886_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1145:33  */
  always @*
    case (n3878_o)
      2'b10: n3889_o = 1'b1;
      2'b01: n3889_o = 1'b0;
      default: n3889_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1145:33  */
  always @*
    case (n3878_o)
      2'b10: n3892_o = 1'b0;
      2'b01: n3892_o = 1'b1;
      default: n3892_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1141:17  */
  assign n3894_o = ir == 8'b00011000;
  /* T80_MCode.vhd:1160:38  */
  assign n3895_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:1163:45  */
  assign n3896_o = f[0]; // extract
  /* T80_MCode.vhd:1163:54  */
  assign n3897_o = ~n3896_o;
  /* T80_MCode.vhd:1163:41  */
  assign n3900_o = n3897_o ? 3'b010 : 3'b011;
  /* T80_MCode.vhd:1163:41  */
  assign n3903_o = n3897_o ? 1'b0 : 1'b1;
  /* T80_MCode.vhd:1161:33  */
  assign n3905_o = n3895_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:1168:33  */
  assign n3907_o = n3895_o == 31'b0000000000000000000000000000011;
  assign n3908_o = {n3907_o, n3905_o};
  /* T80_MCode.vhd:1160:33  */
  always @*
    case (n3908_o)
      2'b10: n3910_o = 3'b011;
      2'b01: n3910_o = n3900_o;
      default: n3910_o = 3'b011;
    endcase
  /* T80_MCode.vhd:1160:33  */
  always @*
    case (n3908_o)
      2'b10: n3913_o = 3'b101;
      2'b01: n3913_o = n2497_o;
      default: n3913_o = n2497_o;
    endcase
  /* T80_MCode.vhd:1160:33  */
  always @*
    case (n3908_o)
      2'b10: n3916_o = 1'b0;
      2'b01: n3916_o = 1'b1;
      default: n3916_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1160:33  */
  always @*
    case (n3908_o)
      2'b10: n3919_o = 1'b1;
      2'b01: n3919_o = 1'b0;
      default: n3919_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1160:33  */
  always @*
    case (n3908_o)
      2'b10: n3922_o = 1'b1;
      2'b01: n3922_o = 1'b0;
      default: n3922_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1160:33  */
  always @*
    case (n3908_o)
      2'b10: n3924_o = 1'b0;
      2'b01: n3924_o = n3903_o;
      default: n3924_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1156:17  */
  assign n3926_o = ir == 8'b00111000;
  /* T80_MCode.vhd:1179:38  */
  assign n3927_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:1182:45  */
  assign n3928_o = f[0]; // extract
  /* T80_MCode.vhd:1182:41  */
  assign n3931_o = n3928_o ? 3'b010 : 3'b011;
  /* T80_MCode.vhd:1182:41  */
  assign n3934_o = n3928_o ? 1'b0 : 1'b1;
  /* T80_MCode.vhd:1180:33  */
  assign n3936_o = n3927_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:1187:33  */
  assign n3938_o = n3927_o == 31'b0000000000000000000000000000011;
  assign n3939_o = {n3938_o, n3936_o};
  /* T80_MCode.vhd:1179:33  */
  always @*
    case (n3939_o)
      2'b10: n3941_o = 3'b011;
      2'b01: n3941_o = n3931_o;
      default: n3941_o = 3'b011;
    endcase
  /* T80_MCode.vhd:1179:33  */
  always @*
    case (n3939_o)
      2'b10: n3944_o = 3'b101;
      2'b01: n3944_o = n2497_o;
      default: n3944_o = n2497_o;
    endcase
  /* T80_MCode.vhd:1179:33  */
  always @*
    case (n3939_o)
      2'b10: n3947_o = 1'b0;
      2'b01: n3947_o = 1'b1;
      default: n3947_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1179:33  */
  always @*
    case (n3939_o)
      2'b10: n3950_o = 1'b1;
      2'b01: n3950_o = 1'b0;
      default: n3950_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1179:33  */
  always @*
    case (n3939_o)
      2'b10: n3953_o = 1'b1;
      2'b01: n3953_o = 1'b0;
      default: n3953_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1179:33  */
  always @*
    case (n3939_o)
      2'b10: n3955_o = 1'b0;
      2'b01: n3955_o = n3934_o;
      default: n3955_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1175:17  */
  assign n3957_o = ir == 8'b00110000;
  /* T80_MCode.vhd:1198:38  */
  assign n3958_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:1201:45  */
  assign n3959_o = f[6]; // extract
  /* T80_MCode.vhd:1201:54  */
  assign n3960_o = ~n3959_o;
  /* T80_MCode.vhd:1201:41  */
  assign n3963_o = n3960_o ? 3'b010 : 3'b011;
  /* T80_MCode.vhd:1201:41  */
  assign n3966_o = n3960_o ? 1'b0 : 1'b1;
  /* T80_MCode.vhd:1199:33  */
  assign n3968_o = n3958_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:1206:33  */
  assign n3970_o = n3958_o == 31'b0000000000000000000000000000011;
  assign n3971_o = {n3970_o, n3968_o};
  /* T80_MCode.vhd:1198:33  */
  always @*
    case (n3971_o)
      2'b10: n3973_o = 3'b011;
      2'b01: n3973_o = n3963_o;
      default: n3973_o = 3'b011;
    endcase
  /* T80_MCode.vhd:1198:33  */
  always @*
    case (n3971_o)
      2'b10: n3976_o = 3'b101;
      2'b01: n3976_o = n2497_o;
      default: n3976_o = n2497_o;
    endcase
  /* T80_MCode.vhd:1198:33  */
  always @*
    case (n3971_o)
      2'b10: n3979_o = 1'b0;
      2'b01: n3979_o = 1'b1;
      default: n3979_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1198:33  */
  always @*
    case (n3971_o)
      2'b10: n3982_o = 1'b1;
      2'b01: n3982_o = 1'b0;
      default: n3982_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1198:33  */
  always @*
    case (n3971_o)
      2'b10: n3985_o = 1'b1;
      2'b01: n3985_o = 1'b0;
      default: n3985_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1198:33  */
  always @*
    case (n3971_o)
      2'b10: n3987_o = 1'b0;
      2'b01: n3987_o = n3966_o;
      default: n3987_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1194:17  */
  assign n3989_o = ir == 8'b00101000;
  /* T80_MCode.vhd:1217:38  */
  assign n3990_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:1220:45  */
  assign n3991_o = f[6]; // extract
  /* T80_MCode.vhd:1220:41  */
  assign n3994_o = n3991_o ? 3'b010 : 3'b011;
  /* T80_MCode.vhd:1220:41  */
  assign n3997_o = n3991_o ? 1'b0 : 1'b1;
  /* T80_MCode.vhd:1218:33  */
  assign n3999_o = n3990_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:1225:33  */
  assign n4001_o = n3990_o == 31'b0000000000000000000000000000011;
  assign n4002_o = {n4001_o, n3999_o};
  /* T80_MCode.vhd:1217:33  */
  always @*
    case (n4002_o)
      2'b10: n4004_o = 3'b011;
      2'b01: n4004_o = n3994_o;
      default: n4004_o = 3'b011;
    endcase
  /* T80_MCode.vhd:1217:33  */
  always @*
    case (n4002_o)
      2'b10: n4007_o = 3'b101;
      2'b01: n4007_o = n2497_o;
      default: n4007_o = n2497_o;
    endcase
  /* T80_MCode.vhd:1217:33  */
  always @*
    case (n4002_o)
      2'b10: n4010_o = 1'b0;
      2'b01: n4010_o = 1'b1;
      default: n4010_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1217:33  */
  always @*
    case (n4002_o)
      2'b10: n4013_o = 1'b1;
      2'b01: n4013_o = 1'b0;
      default: n4013_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1217:33  */
  always @*
    case (n4002_o)
      2'b10: n4016_o = 1'b1;
      2'b01: n4016_o = 1'b0;
      default: n4016_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1217:33  */
  always @*
    case (n4002_o)
      2'b10: n4018_o = 1'b0;
      2'b01: n4018_o = n3997_o;
      default: n4018_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1213:17  */
  assign n4020_o = ir == 8'b00100000;
  /* T80_MCode.vhd:1232:17  */
  assign n4022_o = ir == 8'b11101001;
  /* T80_MCode.vhd:1247:38  */
  assign n4023_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:1248:33  */
  assign n4026_o = n4023_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:1256:33  */
  assign n4028_o = n4023_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:1260:33  */
  assign n4030_o = n4023_o == 31'b0000000000000000000000000000011;
  assign n4031_o = {n4030_o, n4028_o, n4026_o};
  /* T80_MCode.vhd:1247:33  */
  always @*
    case (n4031_o)
      3'b100: n4034_o = 3'b101;
      3'b010: n4034_o = n2497_o;
      3'b001: n4034_o = 3'b101;
      default: n4034_o = n2497_o;
    endcase
  /* T80_MCode.vhd:1247:33  */
  always @*
    case (n4031_o)
      3'b100: n4037_o = 1'b0;
      3'b010: n4037_o = 1'b1;
      3'b001: n4037_o = 1'b0;
      default: n4037_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1247:33  */
  always @*
    case (n4031_o)
      3'b100: n4040_o = 1'b0;
      3'b010: n4040_o = 1'b0;
      3'b001: n4040_o = 1'b1;
      default: n4040_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1247:33  */
  always @*
    case (n4031_o)
      3'b100: n4042_o = 3'b000;
      3'b010: n4042_o = 3'b000;
      3'b001: n4042_o = 3'b000;
      default: n4042_o = 3'b000;
    endcase
  /* T80_MCode.vhd:1247:33  */
  always @*
    case (n4031_o)
      3'b100: n4045_o = 4'b0000;
      3'b010: n4045_o = 4'b0000;
      3'b001: n4045_o = 4'b1010;
      default: n4045_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:1247:33  */
  always @*
    case (n4031_o)
      3'b100: n4047_o = n2500_o;
      3'b010: n4047_o = n2500_o;
      3'b001: n4047_o = 4'b0010;
      default: n4047_o = n2500_o;
    endcase
  /* T80_MCode.vhd:1247:33  */
  always @*
    case (n4031_o)
      3'b100: n4050_o = 1'b0;
      3'b010: n4050_o = 1'b0;
      3'b001: n4050_o = 1'b1;
      default: n4050_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1247:33  */
  always @*
    case (n4031_o)
      3'b100: n4053_o = 1'b1;
      3'b010: n4053_o = 1'b0;
      3'b001: n4053_o = 1'b0;
      default: n4053_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1247:33  */
  always @*
    case (n4031_o)
      3'b100: n4057_o = 1'b0;
      3'b010: n4057_o = 1'b1;
      3'b001: n4057_o = 1'b1;
      default: n4057_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1247:33  */
  always @*
    case (n4031_o)
      3'b100: n4060_o = 1'b1;
      3'b010: n4060_o = 1'b0;
      3'b001: n4060_o = 1'b0;
      default: n4060_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1247:33  */
  always @*
    case (n4031_o)
      3'b100: n4063_o = 1'b0;
      3'b010: n4063_o = 1'b1;
      3'b001: n4063_o = 1'b0;
      default: n4063_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1235:17  */
  assign n4065_o = ir == 8'b00010000;
  /* T80_MCode.vhd:1276:30  */
  assign n4066_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:1277:25  */
  assign n4068_o = n4066_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:1280:25  */
  assign n4070_o = n4066_o == 31'b0000000000000000000000000000011;
  /* T80_MCode.vhd:1287:25  */
  assign n4072_o = n4066_o == 31'b0000000000000000000000000000100;
  /* T80_MCode.vhd:1292:25  */
  assign n4074_o = n4066_o == 31'b0000000000000000000000000000101;
  assign n4075_o = {n4074_o, n4072_o, n4070_o, n4068_o};
  /* T80_MCode.vhd:1276:25  */
  always @*
    case (n4075_o)
      4'b1000: n4077_o = n2497_o;
      4'b0100: n4077_o = n2497_o;
      4'b0010: n4077_o = 3'b100;
      4'b0001: n4077_o = n2497_o;
      default: n4077_o = n2497_o;
    endcase
  /* T80_MCode.vhd:1276:25  */
  always @*
    case (n4075_o)
      4'b1000: n4081_o = 1'b0;
      4'b0100: n4081_o = 1'b0;
      4'b0010: n4081_o = 1'b1;
      4'b0001: n4081_o = 1'b1;
      default: n4081_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1276:25  */
  always @*
    case (n4075_o)
      4'b1000: n4085_o = 4'b0000;
      4'b0100: n4085_o = 4'b1111;
      4'b0010: n4085_o = 4'b1111;
      4'b0001: n4085_o = 4'b0000;
      default: n4085_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:1276:25  */
  always @*
    case (n4075_o)
      4'b1000: n4089_o = 4'b0000;
      4'b0100: n4089_o = 4'b1100;
      4'b0010: n4089_o = 4'b1101;
      4'b0001: n4089_o = 4'b0000;
      default: n4089_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:1276:25  */
  always @*
    case (n4075_o)
      4'b1000: n4093_o = 3'b111;
      4'b0100: n4093_o = 3'b101;
      4'b0010: n4093_o = 3'b101;
      4'b0001: n4093_o = 3'b111;
      default: n4093_o = 3'b111;
    endcase
  /* T80_MCode.vhd:1276:25  */
  always @*
    case (n4075_o)
      4'b1000: n4096_o = 1'b1;
      4'b0100: n4096_o = 1'b0;
      4'b0010: n4096_o = 1'b0;
      4'b0001: n4096_o = 1'b0;
      default: n4096_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1276:25  */
  always @*
    case (n4075_o)
      4'b1000: n4099_o = 1'b0;
      4'b0100: n4099_o = 1'b0;
      4'b0010: n4099_o = 1'b0;
      4'b0001: n4099_o = 1'b1;
      default: n4099_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1276:25  */
  always @*
    case (n4075_o)
      4'b1000: n4102_o = 1'b0;
      4'b0100: n4102_o = 1'b0;
      4'b0010: n4102_o = 1'b1;
      4'b0001: n4102_o = 1'b0;
      default: n4102_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1276:25  */
  always @*
    case (n4075_o)
      4'b1000: n4106_o = 1'b1;
      4'b0100: n4106_o = 1'b1;
      4'b0010: n4106_o = 1'b0;
      4'b0001: n4106_o = 1'b0;
      default: n4106_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1269:17  */
  assign n4108_o = ir == 8'b11001101;
  /* T80_MCode.vhd:1305:38  */
  assign n4111_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:1306:33  */
  assign n4113_o = n4111_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:1312:73  */
  assign n4115_o = ir[5:3]; // extract
  /* T80_MCode.vhd:180:47  */
  assign n4120_o = f[6]; // extract
  /* T80_MCode.vhd:180:56  */
  assign n4121_o = ~n4120_o;
  /* T80_MCode.vhd:180:25  */
  assign n4123_o = n4115_o == 3'b000;
  /* T80_MCode.vhd:181:47  */
  assign n4124_o = f[6]; // extract
  /* T80_MCode.vhd:181:25  */
  assign n4126_o = n4115_o == 3'b001;
  /* T80_MCode.vhd:182:47  */
  assign n4127_o = f[0]; // extract
  /* T80_MCode.vhd:182:56  */
  assign n4128_o = ~n4127_o;
  /* T80_MCode.vhd:182:25  */
  assign n4130_o = n4115_o == 3'b010;
  /* T80_MCode.vhd:183:47  */
  assign n4131_o = f[0]; // extract
  /* T80_MCode.vhd:183:25  */
  assign n4133_o = n4115_o == 3'b011;
  /* T80_MCode.vhd:184:47  */
  assign n4134_o = f[2]; // extract
  /* T80_MCode.vhd:184:56  */
  assign n4135_o = ~n4134_o;
  /* T80_MCode.vhd:184:25  */
  assign n4137_o = n4115_o == 3'b100;
  /* T80_MCode.vhd:185:47  */
  assign n4138_o = f[2]; // extract
  /* T80_MCode.vhd:185:25  */
  assign n4140_o = n4115_o == 3'b101;
  /* T80_MCode.vhd:186:47  */
  assign n4141_o = f[7]; // extract
  /* T80_MCode.vhd:186:56  */
  assign n4142_o = ~n4141_o;
  /* T80_MCode.vhd:186:25  */
  assign n4144_o = n4115_o == 3'b110;
  /* T80_MCode.vhd:187:47  */
  assign n4145_o = f[7]; // extract
  /* T80_MCode.vhd:187:25  */
  assign n4147_o = n4115_o == 3'b111;
  assign n4148_o = {n4147_o, n4144_o, n4140_o, n4137_o, n4133_o, n4130_o, n4126_o, n4123_o};
  /* T80_MCode.vhd:179:25  */
  always @*
    case (n4148_o)
      8'b10000000: n4150_o = n4145_o;
      8'b01000000: n4150_o = n4142_o;
      8'b00100000: n4150_o = n4138_o;
      8'b00010000: n4150_o = n4135_o;
      8'b00001000: n4150_o = n4131_o;
      8'b00000100: n4150_o = n4128_o;
      8'b00000010: n4150_o = n4124_o;
      8'b00000001: n4150_o = n4121_o;
      default: n4150_o = 1'bX;
    endcase
  /* T80_MCode.vhd:1312:41  */
  assign n4153_o = n4150_o ? 3'b101 : 3'b011;
  /* T80_MCode.vhd:1312:41  */
  assign n4155_o = n4150_o ? 3'b100 : n2497_o;
  /* T80_MCode.vhd:1312:41  */
  assign n4158_o = n4150_o ? 4'b1111 : 4'b0000;
  /* T80_MCode.vhd:1312:41  */
  assign n4161_o = n4150_o ? 4'b1101 : 4'b0000;
  /* T80_MCode.vhd:1312:41  */
  assign n4164_o = n4150_o ? 3'b101 : 3'b111;
  /* T80_MCode.vhd:1309:33  */
  assign n4166_o = n4111_o == 31'b0000000000000000000000000000011;
  /* T80_MCode.vhd:1320:33  */
  assign n4168_o = n4111_o == 31'b0000000000000000000000000000100;
  /* T80_MCode.vhd:1325:33  */
  assign n4170_o = n4111_o == 31'b0000000000000000000000000000101;
  assign n4171_o = {n4170_o, n4168_o, n4166_o, n4113_o};
  /* T80_MCode.vhd:1305:33  */
  always @*
    case (n4171_o)
      4'b1000: n4173_o = 3'b101;
      4'b0100: n4173_o = 3'b101;
      4'b0010: n4173_o = n4153_o;
      4'b0001: n4173_o = 3'b101;
      default: n4173_o = 3'b101;
    endcase
  /* T80_MCode.vhd:1305:33  */
  always @*
    case (n4171_o)
      4'b1000: n4175_o = n2497_o;
      4'b0100: n4175_o = n2497_o;
      4'b0010: n4175_o = n4155_o;
      4'b0001: n4175_o = n2497_o;
      default: n4175_o = n2497_o;
    endcase
  /* T80_MCode.vhd:1305:33  */
  always @*
    case (n4171_o)
      4'b1000: n4179_o = 1'b0;
      4'b0100: n4179_o = 1'b0;
      4'b0010: n4179_o = 1'b1;
      4'b0001: n4179_o = 1'b1;
      default: n4179_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1305:33  */
  always @*
    case (n4171_o)
      4'b1000: n4182_o = 4'b0000;
      4'b0100: n4182_o = 4'b1111;
      4'b0010: n4182_o = n4158_o;
      4'b0001: n4182_o = 4'b0000;
      default: n4182_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:1305:33  */
  always @*
    case (n4171_o)
      4'b1000: n4185_o = 4'b0000;
      4'b0100: n4185_o = 4'b1100;
      4'b0010: n4185_o = n4161_o;
      4'b0001: n4185_o = 4'b0000;
      default: n4185_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:1305:33  */
  always @*
    case (n4171_o)
      4'b1000: n4188_o = 3'b111;
      4'b0100: n4188_o = 3'b101;
      4'b0010: n4188_o = n4164_o;
      4'b0001: n4188_o = 3'b111;
      default: n4188_o = 3'b111;
    endcase
  /* T80_MCode.vhd:1305:33  */
  always @*
    case (n4171_o)
      4'b1000: n4191_o = 1'b1;
      4'b0100: n4191_o = 1'b0;
      4'b0010: n4191_o = 1'b0;
      4'b0001: n4191_o = 1'b0;
      default: n4191_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1305:33  */
  always @*
    case (n4171_o)
      4'b1000: n4194_o = 1'b0;
      4'b0100: n4194_o = 1'b0;
      4'b0010: n4194_o = 1'b0;
      4'b0001: n4194_o = 1'b1;
      default: n4194_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1305:33  */
  always @*
    case (n4171_o)
      4'b1000: n4197_o = 1'b0;
      4'b0100: n4197_o = 1'b0;
      4'b0010: n4197_o = 1'b1;
      4'b0001: n4197_o = 1'b0;
      default: n4197_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1305:33  */
  always @*
    case (n4171_o)
      4'b1000: n4201_o = 1'b1;
      4'b0100: n4201_o = 1'b1;
      4'b0010: n4201_o = 1'b0;
      4'b0001: n4201_o = 1'b0;
      default: n4201_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1297:17  */
  assign n4203_o = ir == 8'b11000100;
  /* T80_MCode.vhd:1297:32  */
  assign n4205_o = ir == 8'b11001100;
  /* T80_MCode.vhd:1297:32  */
  assign n4206_o = n4203_o | n4205_o;
  /* T80_MCode.vhd:1297:43  */
  assign n4208_o = ir == 8'b11010100;
  /* T80_MCode.vhd:1297:43  */
  assign n4209_o = n4206_o | n4208_o;
  /* T80_MCode.vhd:1297:54  */
  assign n4211_o = ir == 8'b11011100;
  /* T80_MCode.vhd:1297:54  */
  assign n4212_o = n4209_o | n4211_o;
  /* T80_MCode.vhd:1297:65  */
  assign n4214_o = ir == 8'b11100100;
  /* T80_MCode.vhd:1297:65  */
  assign n4215_o = n4212_o | n4214_o;
  /* T80_MCode.vhd:1297:76  */
  assign n4217_o = ir == 8'b11101100;
  /* T80_MCode.vhd:1297:76  */
  assign n4218_o = n4215_o | n4217_o;
  /* T80_MCode.vhd:1297:87  */
  assign n4220_o = ir == 8'b11110100;
  /* T80_MCode.vhd:1297:87  */
  assign n4221_o = n4218_o | n4220_o;
  /* T80_MCode.vhd:1297:98  */
  assign n4223_o = ir == 8'b11111100;
  /* T80_MCode.vhd:1297:98  */
  assign n4224_o = n4221_o | n4223_o;
  /* T80_MCode.vhd:1349:38  */
  assign n4225_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:1350:33  */
  assign n4227_o = n4225_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:1353:33  */
  assign n4229_o = n4225_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:1357:33  */
  assign n4231_o = n4225_o == 31'b0000000000000000000000000000011;
  assign n4232_o = {n4231_o, n4229_o, n4227_o};
  /* T80_MCode.vhd:1349:33  */
  always @*
    case (n4232_o)
      3'b100: n4236_o = 4'b0111;
      3'b010: n4236_o = 4'b0111;
      3'b001: n4236_o = 4'b0000;
      default: n4236_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:1349:33  */
  always @*
    case (n4232_o)
      3'b100: n4240_o = 3'b111;
      3'b010: n4240_o = 3'b101;
      3'b001: n4240_o = 3'b101;
      default: n4240_o = 3'b111;
    endcase
  /* T80_MCode.vhd:1349:33  */
  always @*
    case (n4232_o)
      3'b100: n4243_o = 1'b1;
      3'b010: n4243_o = 1'b0;
      3'b001: n4243_o = 1'b0;
      default: n4243_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1349:33  */
  always @*
    case (n4232_o)
      3'b100: n4246_o = 1'b0;
      3'b010: n4246_o = 1'b1;
      3'b001: n4246_o = 1'b0;
      default: n4246_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1331:17  */
  assign n4248_o = ir == 8'b11001001;
  /* T80_MCode.vhd:1435:46  */
  assign n4250_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:1437:81  */
  assign n4252_o = ir[5:3]; // extract
  /* T80_MCode.vhd:180:47  */
  assign n4257_o = f[6]; // extract
  /* T80_MCode.vhd:180:56  */
  assign n4258_o = ~n4257_o;
  /* T80_MCode.vhd:180:25  */
  assign n4260_o = n4252_o == 3'b000;
  /* T80_MCode.vhd:181:47  */
  assign n4261_o = f[6]; // extract
  /* T80_MCode.vhd:181:25  */
  assign n4263_o = n4252_o == 3'b001;
  /* T80_MCode.vhd:182:47  */
  assign n4264_o = f[0]; // extract
  /* T80_MCode.vhd:182:56  */
  assign n4265_o = ~n4264_o;
  /* T80_MCode.vhd:182:25  */
  assign n4267_o = n4252_o == 3'b010;
  /* T80_MCode.vhd:183:47  */
  assign n4268_o = f[0]; // extract
  /* T80_MCode.vhd:183:25  */
  assign n4270_o = n4252_o == 3'b011;
  /* T80_MCode.vhd:184:47  */
  assign n4271_o = f[2]; // extract
  /* T80_MCode.vhd:184:56  */
  assign n4272_o = ~n4271_o;
  /* T80_MCode.vhd:184:25  */
  assign n4274_o = n4252_o == 3'b100;
  /* T80_MCode.vhd:185:47  */
  assign n4275_o = f[2]; // extract
  /* T80_MCode.vhd:185:25  */
  assign n4277_o = n4252_o == 3'b101;
  /* T80_MCode.vhd:186:47  */
  assign n4278_o = f[7]; // extract
  /* T80_MCode.vhd:186:56  */
  assign n4279_o = ~n4278_o;
  /* T80_MCode.vhd:186:25  */
  assign n4281_o = n4252_o == 3'b110;
  /* T80_MCode.vhd:187:47  */
  assign n4282_o = f[7]; // extract
  /* T80_MCode.vhd:187:25  */
  assign n4284_o = n4252_o == 3'b111;
  assign n4285_o = {n4284_o, n4281_o, n4277_o, n4274_o, n4270_o, n4267_o, n4263_o, n4260_o};
  /* T80_MCode.vhd:179:25  */
  always @*
    case (n4285_o)
      8'b10000000: n4287_o = n4282_o;
      8'b01000000: n4287_o = n4279_o;
      8'b00100000: n4287_o = n4275_o;
      8'b00010000: n4287_o = n4272_o;
      8'b00001000: n4287_o = n4268_o;
      8'b00000100: n4287_o = n4265_o;
      8'b00000010: n4287_o = n4261_o;
      8'b00000001: n4287_o = n4258_o;
      default: n4287_o = 1'bX;
    endcase
  /* T80_MCode.vhd:1437:49  */
  assign n4290_o = n4287_o ? 3'b011 : 3'b001;
  /* T80_MCode.vhd:1437:49  */
  assign n4293_o = n4287_o ? 3'b101 : 3'b111;
  /* T80_MCode.vhd:1436:41  */
  assign n4295_o = n4250_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:1443:41  */
  assign n4297_o = n4250_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:1447:41  */
  assign n4299_o = n4250_o == 31'b0000000000000000000000000000011;
  assign n4300_o = {n4299_o, n4297_o, n4295_o};
  /* T80_MCode.vhd:1435:41  */
  always @*
    case (n4300_o)
      3'b100: n4302_o = 3'b011;
      3'b010: n4302_o = 3'b011;
      3'b001: n4302_o = n4290_o;
      default: n4302_o = 3'b011;
    endcase
  /* T80_MCode.vhd:1435:41  */
  always @*
    case (n4300_o)
      3'b100: n4305_o = n2497_o;
      3'b010: n4305_o = n2497_o;
      3'b001: n4305_o = 3'b101;
      default: n4305_o = n2497_o;
    endcase
  /* T80_MCode.vhd:1435:41  */
  always @*
    case (n4300_o)
      3'b100: n4309_o = 4'b0111;
      3'b010: n4309_o = 4'b0111;
      3'b001: n4309_o = 4'b0000;
      default: n4309_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:1435:41  */
  always @*
    case (n4300_o)
      3'b100: n4312_o = 3'b111;
      3'b010: n4312_o = 3'b101;
      3'b001: n4312_o = n4293_o;
      default: n4312_o = 3'b111;
    endcase
  /* T80_MCode.vhd:1435:41  */
  always @*
    case (n4300_o)
      3'b100: n4315_o = 1'b1;
      3'b010: n4315_o = 1'b0;
      3'b001: n4315_o = 1'b0;
      default: n4315_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1435:41  */
  always @*
    case (n4300_o)
      3'b100: n4318_o = 1'b0;
      3'b010: n4318_o = 1'b1;
      3'b001: n4318_o = 1'b0;
      default: n4318_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1363:17  */
  assign n4320_o = ir == 8'b11000000;
  /* T80_MCode.vhd:1363:32  */
  assign n4322_o = ir == 8'b11001000;
  /* T80_MCode.vhd:1363:32  */
  assign n4323_o = n4320_o | n4322_o;
  /* T80_MCode.vhd:1363:43  */
  assign n4325_o = ir == 8'b11010000;
  /* T80_MCode.vhd:1363:43  */
  assign n4326_o = n4323_o | n4325_o;
  /* T80_MCode.vhd:1363:54  */
  assign n4328_o = ir == 8'b11011000;
  /* T80_MCode.vhd:1363:54  */
  assign n4329_o = n4326_o | n4328_o;
  /* T80_MCode.vhd:1363:65  */
  assign n4331_o = ir == 8'b11100000;
  /* T80_MCode.vhd:1363:65  */
  assign n4332_o = n4329_o | n4331_o;
  /* T80_MCode.vhd:1363:76  */
  assign n4334_o = ir == 8'b11101000;
  /* T80_MCode.vhd:1363:76  */
  assign n4335_o = n4332_o | n4334_o;
  /* T80_MCode.vhd:1363:87  */
  assign n4337_o = ir == 8'b11110000;
  /* T80_MCode.vhd:1363:87  */
  assign n4338_o = n4335_o | n4337_o;
  /* T80_MCode.vhd:1363:98  */
  assign n4340_o = ir == 8'b11111000;
  /* T80_MCode.vhd:1363:98  */
  assign n4341_o = n4338_o | n4340_o;
  /* T80_MCode.vhd:1476:38  */
  assign n4342_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:1477:33  */
  assign n4344_o = n4342_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:1482:33  */
  assign n4346_o = n4342_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:1487:33  */
  assign n4348_o = n4342_o == 31'b0000000000000000000000000000011;
  assign n4349_o = {n4348_o, n4346_o, n4344_o};
  /* T80_MCode.vhd:1476:33  */
  always @*
    case (n4349_o)
      3'b100: n4351_o = n2497_o;
      3'b010: n4351_o = n2497_o;
      3'b001: n4351_o = 3'b101;
      default: n4351_o = n2497_o;
    endcase
  /* T80_MCode.vhd:1476:33  */
  always @*
    case (n4349_o)
      3'b100: n4355_o = 4'b0000;
      3'b010: n4355_o = 4'b1111;
      3'b001: n4355_o = 4'b1111;
      default: n4355_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:1476:33  */
  always @*
    case (n4349_o)
      3'b100: n4359_o = 4'b0000;
      3'b010: n4359_o = 4'b1100;
      3'b001: n4359_o = 4'b1101;
      default: n4359_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:1476:33  */
  always @*
    case (n4349_o)
      3'b100: n4363_o = 3'b111;
      3'b010: n4363_o = 3'b101;
      3'b001: n4363_o = 3'b101;
      default: n4363_o = 3'b111;
    endcase
  /* T80_MCode.vhd:1476:33  */
  always @*
    case (n4349_o)
      3'b100: n4366_o = 1'b1;
      3'b010: n4366_o = 1'b0;
      3'b001: n4366_o = 1'b0;
      default: n4366_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1476:33  */
  always @*
    case (n4349_o)
      3'b100: n4370_o = 1'b1;
      3'b010: n4370_o = 1'b1;
      3'b001: n4370_o = 1'b0;
      default: n4370_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1454:17  */
  assign n4372_o = ir == 8'b11000111;
  /* T80_MCode.vhd:1454:32  */
  assign n4374_o = ir == 8'b11001111;
  /* T80_MCode.vhd:1454:32  */
  assign n4375_o = n4372_o | n4374_o;
  /* T80_MCode.vhd:1454:43  */
  assign n4377_o = ir == 8'b11010111;
  /* T80_MCode.vhd:1454:43  */
  assign n4378_o = n4375_o | n4377_o;
  /* T80_MCode.vhd:1454:54  */
  assign n4380_o = ir == 8'b11011111;
  /* T80_MCode.vhd:1454:54  */
  assign n4381_o = n4378_o | n4380_o;
  /* T80_MCode.vhd:1454:65  */
  assign n4383_o = ir == 8'b11100111;
  /* T80_MCode.vhd:1454:65  */
  assign n4384_o = n4381_o | n4383_o;
  /* T80_MCode.vhd:1454:76  */
  assign n4386_o = ir == 8'b11101111;
  /* T80_MCode.vhd:1454:76  */
  assign n4387_o = n4384_o | n4386_o;
  /* T80_MCode.vhd:1454:87  */
  assign n4389_o = ir == 8'b11110111;
  /* T80_MCode.vhd:1454:87  */
  assign n4390_o = n4387_o | n4389_o;
  /* T80_MCode.vhd:1454:98  */
  assign n4392_o = ir == 8'b11111111;
  /* T80_MCode.vhd:1454:98  */
  assign n4393_o = n4390_o | n4392_o;
  /* T80_MCode.vhd:1499:38  */
  assign n4394_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:1500:33  */
  assign n4396_o = n4394_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:1503:33  */
  assign n4398_o = n4394_o == 31'b0000000000000000000000000000011;
  assign n4399_o = {n4398_o, n4396_o};
  /* T80_MCode.vhd:1499:33  */
  always @*
    case (n4399_o)
      2'b10: n4402_o = 1'b0;
      2'b01: n4402_o = 1'b1;
      default: n4402_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1499:33  */
  always @*
    case (n4399_o)
      2'b10: n4405_o = 1'b1;
      2'b01: n4405_o = 1'b0;
      default: n4405_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1499:33  */
  always @*
    case (n4399_o)
      2'b10: n4408_o = 3'b111;
      2'b01: n4408_o = 3'b100;
      default: n4408_o = 3'b111;
    endcase
  /* T80_MCode.vhd:1499:33  */
  always @*
    case (n4399_o)
      2'b10: n4411_o = 1'b1;
      2'b01: n4411_o = 1'b0;
      default: n4411_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1495:17  */
  assign n4413_o = ir == 8'b11011011;
  /* T80_MCode.vhd:1513:38  */
  assign n4414_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:1514:33  */
  assign n4416_o = n4414_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:1518:33  */
  assign n4418_o = n4414_o == 31'b0000000000000000000000000000011;
  assign n4419_o = {n4418_o, n4416_o};
  /* T80_MCode.vhd:1513:33  */
  always @*
    case (n4419_o)
      2'b10: n4422_o = 1'b0;
      2'b01: n4422_o = 1'b1;
      default: n4422_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1513:33  */
  always @*
    case (n4419_o)
      2'b10: n4425_o = 4'b0000;
      2'b01: n4425_o = 4'b0111;
      default: n4425_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:1513:33  */
  always @*
    case (n4419_o)
      2'b10: n4428_o = 3'b111;
      2'b01: n4428_o = 3'b100;
      default: n4428_o = 3'b111;
    endcase
  /* T80_MCode.vhd:1513:33  */
  always @*
    case (n4419_o)
      2'b10: n4431_o = 1'b1;
      2'b01: n4431_o = 1'b0;
      default: n4431_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1513:33  */
  always @*
    case (n4419_o)
      2'b10: n4434_o = 1'b1;
      2'b01: n4434_o = 1'b0;
      default: n4434_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1509:17  */
  assign n4436_o = ir == 8'b11010011;
  /* T80_MCode.vhd:1531:17  */
  assign n4438_o = ir == 8'b11001011;
  /* T80_MCode.vhd:1536:17  */
  assign n4440_o = ir == 8'b11101101;
  /* T80_MCode.vhd:1541:17  */
  assign n4442_o = ir == 8'b11011101;
  /* T80_MCode.vhd:1541:32  */
  assign n4444_o = ir == 8'b11111101;
  /* T80_MCode.vhd:1541:32  */
  assign n4445_o = n4442_o | n4444_o;
  assign n4446_o = {n4445_o, n4440_o, n4438_o, n4436_o, n4413_o, n4393_o, n4341_o, n4248_o, n4224_o, n4108_o, n4065_o, n4022_o, n4020_o, n3989_o, n3957_o, n3926_o, n3894_o, n3872_o, n3790_o, n3769_o, n3757_o, n3745_o, n3733_o, n3638_o, n3636_o, n3634_o, n3632_o, n3534_o, n3532_o, n3530_o, n3528_o, n3525_o, n3490_o, n3470_o, n3435_o, n3415_o, n3376_o, n3333_o, n3165_o, n3121_o, n3119_o, n3117_o, n3115_o, n3062_o, n3013_o, n3011_o, n2974_o, n2937_o, n2890_o, n2861_o, n2841_o, n2821_o, n2798_o, n2784_o, n2770_o, n2748_o, n2711_o, n2677_o, n2646_o};
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4479_o = 3'b001;
      59'b01000000000000000000000000000000000000000000000000000000000: n4479_o = 3'b001;
      59'b00100000000000000000000000000000000000000000000000000000000: n4479_o = 3'b001;
      59'b00010000000000000000000000000000000000000000000000000000000: n4479_o = 3'b011;
      59'b00001000000000000000000000000000000000000000000000000000000: n4479_o = 3'b011;
      59'b00000100000000000000000000000000000000000000000000000000000: n4479_o = 3'b011;
      59'b00000010000000000000000000000000000000000000000000000000000: n4479_o = n4302_o;
      59'b00000001000000000000000000000000000000000000000000000000000: n4479_o = 3'b011;
      59'b00000000100000000000000000000000000000000000000000000000000: n4479_o = n4173_o;
      59'b00000000010000000000000000000000000000000000000000000000000: n4479_o = 3'b101;
      59'b00000000001000000000000000000000000000000000000000000000000: n4479_o = 3'b011;
      59'b00000000000100000000000000000000000000000000000000000000000: n4479_o = 3'b001;
      59'b00000000000010000000000000000000000000000000000000000000000: n4479_o = n4004_o;
      59'b00000000000001000000000000000000000000000000000000000000000: n4479_o = n3973_o;
      59'b00000000000000100000000000000000000000000000000000000000000: n4479_o = n3941_o;
      59'b00000000000000010000000000000000000000000000000000000000000: n4479_o = n3910_o;
      59'b00000000000000001000000000000000000000000000000000000000000: n4479_o = 3'b011;
      59'b00000000000000000100000000000000000000000000000000000000000: n4479_o = 3'b011;
      59'b00000000000000000010000000000000000000000000000000000000000: n4479_o = 3'b011;
      59'b00000000000000000001000000000000000000000000000000000000000: n4479_o = 3'b001;
      59'b00000000000000000000100000000000000000000000000000000000000: n4479_o = 3'b001;
      59'b00000000000000000000010000000000000000000000000000000000000: n4479_o = 3'b001;
      59'b00000000000000000000001000000000000000000000000000000000000: n4479_o = 3'b011;
      59'b00000000000000000000000100000000000000000000000000000000000: n4479_o = 3'b001;
      59'b00000000000000000000000010000000000000000000000000000000000: n4479_o = 3'b001;
      59'b00000000000000000000000001000000000000000000000000000000000: n4479_o = 3'b001;
      59'b00000000000000000000000000100000000000000000000000000000000: n4479_o = n3619_o;
      59'b00000000000000000000000000010000000000000000000000000000000: n4479_o = 3'b001;
      59'b00000000000000000000000000001000000000000000000000000000000: n4479_o = 3'b001;
      59'b00000000000000000000000000000100000000000000000000000000000: n4479_o = 3'b001;
      59'b00000000000000000000000000000010000000000000000000000000000: n4479_o = 3'b001;
      59'b00000000000000000000000000000001000000000000000000000000000: n4479_o = 3'b011;
      59'b00000000000000000000000000000000100000000000000000000000000: n4479_o = 3'b001;
      59'b00000000000000000000000000000000010000000000000000000000000: n4479_o = 3'b011;
      59'b00000000000000000000000000000000001000000000000000000000000: n4479_o = 3'b001;
      59'b00000000000000000000000000000000000100000000000000000000000: n4479_o = 3'b010;
      59'b00000000000000000000000000000000000010000000000000000000000: n4479_o = 3'b010;
      59'b00000000000000000000000000000000000001000000000000000000000: n4479_o = 3'b001;
      59'b00000000000000000000000000000000000000100000000000000000000: n4479_o = 3'b101;
      59'b00000000000000000000000000000000000000010000000000000000000: n4479_o = 3'b001;
      59'b00000000000000000000000000000000000000001000000000000000000: n4479_o = 3'b001;
      59'b00000000000000000000000000000000000000000100000000000000000: n4479_o = 3'b001;
      59'b00000000000000000000000000000000000000000010000000000000000: n4479_o = 3'b011;
      59'b00000000000000000000000000000000000000000001000000000000000: n4479_o = 3'b011;
      59'b00000000000000000000000000000000000000000000100000000000000: n4479_o = 3'b001;
      59'b00000000000000000000000000000000000000000000010000000000000: n4479_o = 3'b101;
      59'b00000000000000000000000000000000000000000000001000000000000: n4479_o = 3'b101;
      59'b00000000000000000000000000000000000000000000000100000000000: n4479_o = 3'b011;
      59'b00000000000000000000000000000000000000000000000010000000000: n4479_o = 3'b100;
      59'b00000000000000000000000000000000000000000000000001000000000: n4479_o = 3'b010;
      59'b00000000000000000000000000000000000000000000000000100000000: n4479_o = 3'b010;
      59'b00000000000000000000000000000000000000000000000000010000000: n4479_o = 3'b100;
      59'b00000000000000000000000000000000000000000000000000001000000: n4479_o = 3'b010;
      59'b00000000000000000000000000000000000000000000000000000100000: n4479_o = 3'b010;
      59'b00000000000000000000000000000000000000000000000000000010000: n4479_o = 3'b011;
      59'b00000000000000000000000000000000000000000000000000000001000: n4479_o = 3'b010;
      59'b00000000000000000000000000000000000000000000000000000000100: n4479_o = 3'b010;
      59'b00000000000000000000000000000000000000000000000000000000010: n4479_o = 3'b010;
      59'b00000000000000000000000000000000000000000000000000000000001: n4479_o = 3'b001;
      default: n4479_o = 3'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4484_o = n2497_o;
      59'b01000000000000000000000000000000000000000000000000000000000: n4484_o = n2497_o;
      59'b00100000000000000000000000000000000000000000000000000000000: n4484_o = n2497_o;
      59'b00010000000000000000000000000000000000000000000000000000000: n4484_o = n2497_o;
      59'b00001000000000000000000000000000000000000000000000000000000: n4484_o = n2497_o;
      59'b00000100000000000000000000000000000000000000000000000000000: n4484_o = n4351_o;
      59'b00000010000000000000000000000000000000000000000000000000000: n4484_o = n4305_o;
      59'b00000001000000000000000000000000000000000000000000000000000: n4484_o = n2497_o;
      59'b00000000100000000000000000000000000000000000000000000000000: n4484_o = n4175_o;
      59'b00000000010000000000000000000000000000000000000000000000000: n4484_o = n4077_o;
      59'b00000000001000000000000000000000000000000000000000000000000: n4484_o = n4034_o;
      59'b00000000000100000000000000000000000000000000000000000000000: n4484_o = n2497_o;
      59'b00000000000010000000000000000000000000000000000000000000000: n4484_o = n4007_o;
      59'b00000000000001000000000000000000000000000000000000000000000: n4484_o = n3976_o;
      59'b00000000000000100000000000000000000000000000000000000000000: n4484_o = n3944_o;
      59'b00000000000000010000000000000000000000000000000000000000000: n4484_o = n3913_o;
      59'b00000000000000001000000000000000000000000000000000000000000: n4484_o = n3880_o;
      59'b00000000000000000100000000000000000000000000000000000000000: n4484_o = n2497_o;
      59'b00000000000000000010000000000000000000000000000000000000000: n4484_o = n2497_o;
      59'b00000000000000000001000000000000000000000000000000000000000: n4484_o = n2497_o;
      59'b00000000000000000000100000000000000000000000000000000000000: n4484_o = 3'b110;
      59'b00000000000000000000010000000000000000000000000000000000000: n4484_o = 3'b110;
      59'b00000000000000000000001000000000000000000000000000000000000: n4484_o = n3688_o;
      59'b00000000000000000000000100000000000000000000000000000000000: n4484_o = n2497_o;
      59'b00000000000000000000000010000000000000000000000000000000000: n4484_o = n2497_o;
      59'b00000000000000000000000001000000000000000000000000000000000: n4484_o = n2497_o;
      59'b00000000000000000000000000100000000000000000000000000000000: n4484_o = n3620_o;
      59'b00000000000000000000000000010000000000000000000000000000000: n4484_o = n2497_o;
      59'b00000000000000000000000000001000000000000000000000000000000: n4484_o = n2497_o;
      59'b00000000000000000000000000000100000000000000000000000000000: n4484_o = n2497_o;
      59'b00000000000000000000000000000010000000000000000000000000000: n4484_o = n2497_o;
      59'b00000000000000000000000000000001000000000000000000000000000: n4484_o = n3500_o;
      59'b00000000000000000000000000000000100000000000000000000000000: n4484_o = n2497_o;
      59'b00000000000000000000000000000000010000000000000000000000000: n4484_o = n3445_o;
      59'b00000000000000000000000000000000001000000000000000000000000: n4484_o = n2497_o;
      59'b00000000000000000000000000000000000100000000000000000000000: n4484_o = n2497_o;
      59'b00000000000000000000000000000000000010000000000000000000000: n4484_o = n2497_o;
      59'b00000000000000000000000000000000000001000000000000000000000: n4484_o = n2497_o;
      59'b00000000000000000000000000000000000000100000000000000000000: n4484_o = n3136_o;
      59'b00000000000000000000000000000000000000010000000000000000000: n4484_o = n2497_o;
      59'b00000000000000000000000000000000000000001000000000000000000: n4484_o = n2497_o;
      59'b00000000000000000000000000000000000000000100000000000000000: n4484_o = n2497_o;
      59'b00000000000000000000000000000000000000000010000000000000000: n4484_o = n2497_o;
      59'b00000000000000000000000000000000000000000001000000000000000: n4484_o = n3037_o;
      59'b00000000000000000000000000000000000000000000100000000000000: n4484_o = 3'b110;
      59'b00000000000000000000000000000000000000000000010000000000000: n4484_o = n2497_o;
      59'b00000000000000000000000000000000000000000000001000000000000: n4484_o = n2497_o;
      59'b00000000000000000000000000000000000000000000000100000000000: n4484_o = n2497_o;
      59'b00000000000000000000000000000000000000000000000010000000000: n4484_o = n2497_o;
      59'b00000000000000000000000000000000000000000000000001000000000: n4484_o = n2497_o;
      59'b00000000000000000000000000000000000000000000000000100000000: n4484_o = n2497_o;
      59'b00000000000000000000000000000000000000000000000000010000000: n4484_o = n2497_o;
      59'b00000000000000000000000000000000000000000000000000001000000: n4484_o = n2497_o;
      59'b00000000000000000000000000000000000000000000000000000100000: n4484_o = n2497_o;
      59'b00000000000000000000000000000000000000000000000000000010000: n4484_o = n2497_o;
      59'b00000000000000000000000000000000000000000000000000000001000: n4484_o = n2497_o;
      59'b00000000000000000000000000000000000000000000000000000000100: n4484_o = n2497_o;
      59'b00000000000000000000000000000000000000000000000000000000010: n4484_o = n2497_o;
      59'b00000000000000000000000000000000000000000000000000000000001: n4484_o = n2497_o;
      default: n4484_o = 3'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4490_o = 2'b11;
      59'b01000000000000000000000000000000000000000000000000000000000: n4490_o = 2'b10;
      59'b00100000000000000000000000000000000000000000000000000000000: n4490_o = 2'b01;
      59'b00010000000000000000000000000000000000000000000000000000000: n4490_o = 2'b00;
      59'b00001000000000000000000000000000000000000000000000000000000: n4490_o = 2'b00;
      59'b00000100000000000000000000000000000000000000000000000000000: n4490_o = 2'b00;
      59'b00000010000000000000000000000000000000000000000000000000000: n4490_o = 2'b00;
      59'b00000001000000000000000000000000000000000000000000000000000: n4490_o = 2'b00;
      59'b00000000100000000000000000000000000000000000000000000000000: n4490_o = 2'b00;
      59'b00000000010000000000000000000000000000000000000000000000000: n4490_o = 2'b00;
      59'b00000000001000000000000000000000000000000000000000000000000: n4490_o = 2'b00;
      59'b00000000000100000000000000000000000000000000000000000000000: n4490_o = 2'b00;
      59'b00000000000010000000000000000000000000000000000000000000000: n4490_o = 2'b00;
      59'b00000000000001000000000000000000000000000000000000000000000: n4490_o = 2'b00;
      59'b00000000000000100000000000000000000000000000000000000000000: n4490_o = 2'b00;
      59'b00000000000000010000000000000000000000000000000000000000000: n4490_o = 2'b00;
      59'b00000000000000001000000000000000000000000000000000000000000: n4490_o = 2'b00;
      59'b00000000000000000100000000000000000000000000000000000000000: n4490_o = 2'b00;
      59'b00000000000000000010000000000000000000000000000000000000000: n4490_o = 2'b00;
      59'b00000000000000000001000000000000000000000000000000000000000: n4490_o = 2'b00;
      59'b00000000000000000000100000000000000000000000000000000000000: n4490_o = 2'b00;
      59'b00000000000000000000010000000000000000000000000000000000000: n4490_o = 2'b00;
      59'b00000000000000000000001000000000000000000000000000000000000: n4490_o = 2'b00;
      59'b00000000000000000000000100000000000000000000000000000000000: n4490_o = 2'b00;
      59'b00000000000000000000000010000000000000000000000000000000000: n4490_o = 2'b00;
      59'b00000000000000000000000001000000000000000000000000000000000: n4490_o = 2'b00;
      59'b00000000000000000000000000100000000000000000000000000000000: n4490_o = 2'b00;
      59'b00000000000000000000000000010000000000000000000000000000000: n4490_o = 2'b00;
      59'b00000000000000000000000000001000000000000000000000000000000: n4490_o = 2'b00;
      59'b00000000000000000000000000000100000000000000000000000000000: n4490_o = 2'b00;
      59'b00000000000000000000000000000010000000000000000000000000000: n4490_o = 2'b00;
      59'b00000000000000000000000000000001000000000000000000000000000: n4490_o = 2'b00;
      59'b00000000000000000000000000000000100000000000000000000000000: n4490_o = 2'b00;
      59'b00000000000000000000000000000000010000000000000000000000000: n4490_o = 2'b00;
      59'b00000000000000000000000000000000001000000000000000000000000: n4490_o = 2'b00;
      59'b00000000000000000000000000000000000100000000000000000000000: n4490_o = 2'b00;
      59'b00000000000000000000000000000000000010000000000000000000000: n4490_o = 2'b00;
      59'b00000000000000000000000000000000000001000000000000000000000: n4490_o = 2'b00;
      59'b00000000000000000000000000000000000000100000000000000000000: n4490_o = 2'b00;
      59'b00000000000000000000000000000000000000010000000000000000000: n4490_o = 2'b00;
      59'b00000000000000000000000000000000000000001000000000000000000: n4490_o = 2'b00;
      59'b00000000000000000000000000000000000000000100000000000000000: n4490_o = 2'b00;
      59'b00000000000000000000000000000000000000000010000000000000000: n4490_o = 2'b00;
      59'b00000000000000000000000000000000000000000001000000000000000: n4490_o = 2'b00;
      59'b00000000000000000000000000000000000000000000100000000000000: n4490_o = 2'b00;
      59'b00000000000000000000000000000000000000000000010000000000000: n4490_o = 2'b00;
      59'b00000000000000000000000000000000000000000000001000000000000: n4490_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000100000000000: n4490_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000010000000000: n4490_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000001000000000: n4490_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000100000000: n4490_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000010000000: n4490_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000001000000: n4490_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000000100000: n4490_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000000010000: n4490_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000000001000: n4490_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000000000100: n4490_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000000000010: n4490_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000000000001: n4490_o = 2'b00;
      default: n4490_o = 2'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4493_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4493_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4493_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4493_o = n4422_o;
      59'b00001000000000000000000000000000000000000000000000000000000: n4493_o = n4402_o;
      59'b00000100000000000000000000000000000000000000000000000000000: n4493_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4493_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4493_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4493_o = n4179_o;
      59'b00000000010000000000000000000000000000000000000000000000000: n4493_o = n4081_o;
      59'b00000000001000000000000000000000000000000000000000000000000: n4493_o = n4037_o;
      59'b00000000000100000000000000000000000000000000000000000000000: n4493_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4493_o = n4010_o;
      59'b00000000000001000000000000000000000000000000000000000000000: n4493_o = n3979_o;
      59'b00000000000000100000000000000000000000000000000000000000000: n4493_o = n3947_o;
      59'b00000000000000010000000000000000000000000000000000000000000: n4493_o = n3916_o;
      59'b00000000000000001000000000000000000000000000000000000000000: n4493_o = n3883_o;
      59'b00000000000000000100000000000000000000000000000000000000000: n4493_o = n3841_o;
      59'b00000000000000000010000000000000000000000000000000000000000: n4493_o = n3779_o;
      59'b00000000000000000001000000000000000000000000000000000000000: n4493_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4493_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4493_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4493_o = 1'b0;
      59'b00000000000000000000000100000000000000000000000000000000000: n4493_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4493_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4493_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4493_o = n3622_o;
      59'b00000000000000000000000000010000000000000000000000000000000: n4493_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4493_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4493_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4493_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4493_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4493_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4493_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4493_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4493_o = n3382_o;
      59'b00000000000000000000000000000000000010000000000000000000000: n4493_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4493_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4493_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4493_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4493_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4493_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4493_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4493_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4493_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4493_o = n2988_o;
      59'b00000000000000000000000000000000000000000000001000000000000: n4493_o = n2953_o;
      59'b00000000000000000000000000000000000000000000000100000000000: n4493_o = n2918_o;
      59'b00000000000000000000000000000000000000000000000010000000000: n4493_o = n2873_o;
      59'b00000000000000000000000000000000000000000000000001000000000: n4493_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4493_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4493_o = n2810_o;
      59'b00000000000000000000000000000000000000000000000000001000000: n4493_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4493_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4493_o = n2758_o;
      59'b00000000000000000000000000000000000000000000000000000001000: n4493_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4493_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4493_o = n2652_o;
      59'b00000000000000000000000000000000000000000000000000000000001: n4493_o = 1'b0;
      default: n4493_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4496_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4496_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4496_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4496_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4496_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4496_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4496_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4496_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4496_o = 1'b0;
      59'b00000000010000000000000000000000000000000000000000000000000: n4496_o = 1'b0;
      59'b00000000001000000000000000000000000000000000000000000000000: n4496_o = 1'b0;
      59'b00000000000100000000000000000000000000000000000000000000000: n4496_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4496_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4496_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4496_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4496_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4496_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4496_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4496_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4496_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4496_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4496_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4496_o = 1'b0;
      59'b00000000000000000000000100000000000000000000000000000000000: n4496_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4496_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4496_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4496_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4496_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4496_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4496_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4496_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4496_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4496_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4496_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4496_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4496_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4496_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4496_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4496_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4496_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4496_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4496_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4496_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4496_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4496_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4496_o = n2991_o;
      59'b00000000000000000000000000000000000000000000001000000000000: n4496_o = n2956_o;
      59'b00000000000000000000000000000000000000000000000100000000000: n4496_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4496_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4496_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4496_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4496_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4496_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4496_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4496_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4496_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4496_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4496_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4496_o = 1'b0;
      default: n4496_o = 1'bX;
    endcase
  assign n4497_o = n3041_o[1:0]; // extract
  assign n4498_o = n3092_o[1:0]; // extract
  assign n4499_o = n3140_o[1:0]; // extract
  assign n4500_o = n3623_o[1:0]; // extract
  assign n4501_o = n4085_o[1:0]; // extract
  assign n4502_o = n4182_o[1:0]; // extract
  assign n4503_o = n4236_o[1:0]; // extract
  assign n4504_o = n4309_o[1:0]; // extract
  assign n4505_o = n4355_o[1:0]; // extract
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4508_o = 2'b00;
      59'b01000000000000000000000000000000000000000000000000000000000: n4508_o = 2'b00;
      59'b00100000000000000000000000000000000000000000000000000000000: n4508_o = 2'b00;
      59'b00010000000000000000000000000000000000000000000000000000000: n4508_o = 2'b00;
      59'b00001000000000000000000000000000000000000000000000000000000: n4508_o = 2'b00;
      59'b00000100000000000000000000000000000000000000000000000000000: n4508_o = n4505_o;
      59'b00000010000000000000000000000000000000000000000000000000000: n4508_o = n4504_o;
      59'b00000001000000000000000000000000000000000000000000000000000: n4508_o = n4503_o;
      59'b00000000100000000000000000000000000000000000000000000000000: n4508_o = n4502_o;
      59'b00000000010000000000000000000000000000000000000000000000000: n4508_o = n4501_o;
      59'b00000000001000000000000000000000000000000000000000000000000: n4508_o = 2'b00;
      59'b00000000000100000000000000000000000000000000000000000000000: n4508_o = 2'b00;
      59'b00000000000010000000000000000000000000000000000000000000000: n4508_o = 2'b00;
      59'b00000000000001000000000000000000000000000000000000000000000: n4508_o = 2'b00;
      59'b00000000000000100000000000000000000000000000000000000000000: n4508_o = 2'b00;
      59'b00000000000000010000000000000000000000000000000000000000000: n4508_o = 2'b00;
      59'b00000000000000001000000000000000000000000000000000000000000: n4508_o = 2'b00;
      59'b00000000000000000100000000000000000000000000000000000000000: n4508_o = 2'b00;
      59'b00000000000000000010000000000000000000000000000000000000000: n4508_o = 2'b00;
      59'b00000000000000000001000000000000000000000000000000000000000: n4508_o = 2'b00;
      59'b00000000000000000000100000000000000000000000000000000000000: n4508_o = n2492_o;
      59'b00000000000000000000010000000000000000000000000000000000000: n4508_o = n2492_o;
      59'b00000000000000000000001000000000000000000000000000000000000: n4508_o = 2'b00;
      59'b00000000000000000000000100000000000000000000000000000000000: n4508_o = 2'b00;
      59'b00000000000000000000000010000000000000000000000000000000000: n4508_o = 2'b00;
      59'b00000000000000000000000001000000000000000000000000000000000: n4508_o = 2'b00;
      59'b00000000000000000000000000100000000000000000000000000000000: n4508_o = n4500_o;
      59'b00000000000000000000000000010000000000000000000000000000000: n4508_o = 2'b00;
      59'b00000000000000000000000000001000000000000000000000000000000: n4508_o = 2'b00;
      59'b00000000000000000000000000000100000000000000000000000000000: n4508_o = 2'b00;
      59'b00000000000000000000000000000010000000000000000000000000000: n4508_o = 2'b00;
      59'b00000000000000000000000000000001000000000000000000000000000: n4508_o = 2'b00;
      59'b00000000000000000000000000000000100000000000000000000000000: n4508_o = 2'b00;
      59'b00000000000000000000000000000000010000000000000000000000000: n4508_o = 2'b00;
      59'b00000000000000000000000000000000001000000000000000000000000: n4508_o = 2'b00;
      59'b00000000000000000000000000000000000100000000000000000000000: n4508_o = 2'b00;
      59'b00000000000000000000000000000000000010000000000000000000000: n4508_o = 2'b00;
      59'b00000000000000000000000000000000000001000000000000000000000: n4508_o = 2'b00;
      59'b00000000000000000000000000000000000000100000000000000000000: n4508_o = n4499_o;
      59'b00000000000000000000000000000000000000010000000000000000000: n4508_o = 2'b00;
      59'b00000000000000000000000000000000000000001000000000000000000: n4508_o = 2'b00;
      59'b00000000000000000000000000000000000000000100000000000000000: n4508_o = 2'b00;
      59'b00000000000000000000000000000000000000000010000000000000000: n4508_o = n4498_o;
      59'b00000000000000000000000000000000000000000001000000000000000: n4508_o = n4497_o;
      59'b00000000000000000000000000000000000000000000100000000000000: n4508_o = 2'b00;
      59'b00000000000000000000000000000000000000000000010000000000000: n4508_o = 2'b00;
      59'b00000000000000000000000000000000000000000000001000000000000: n4508_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000100000000000: n4508_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000010000000000: n4508_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000001000000000: n4508_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000100000000: n4508_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000010000000: n4508_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000001000000: n4508_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000000100000: n4508_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000000010000: n4508_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000000001000: n4508_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000000000100: n4508_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000000000010: n4508_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000000000001: n4508_o = 2'b00;
      default: n4508_o = 2'bX;
    endcase
  assign n4509_o = n3041_o[3:2]; // extract
  assign n4510_o = n3092_o[3:2]; // extract
  assign n4511_o = n3140_o[3:2]; // extract
  assign n4512_o = n3623_o[3:2]; // extract
  assign n4513_o = n4085_o[3:2]; // extract
  assign n4514_o = n4182_o[3:2]; // extract
  assign n4515_o = n4236_o[3:2]; // extract
  assign n4516_o = n4309_o[3:2]; // extract
  assign n4517_o = n4355_o[3:2]; // extract
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4520_o = 2'b00;
      59'b01000000000000000000000000000000000000000000000000000000000: n4520_o = 2'b00;
      59'b00100000000000000000000000000000000000000000000000000000000: n4520_o = 2'b00;
      59'b00010000000000000000000000000000000000000000000000000000000: n4520_o = 2'b00;
      59'b00001000000000000000000000000000000000000000000000000000000: n4520_o = 2'b00;
      59'b00000100000000000000000000000000000000000000000000000000000: n4520_o = n4517_o;
      59'b00000010000000000000000000000000000000000000000000000000000: n4520_o = n4516_o;
      59'b00000001000000000000000000000000000000000000000000000000000: n4520_o = n4515_o;
      59'b00000000100000000000000000000000000000000000000000000000000: n4520_o = n4514_o;
      59'b00000000010000000000000000000000000000000000000000000000000: n4520_o = n4513_o;
      59'b00000000001000000000000000000000000000000000000000000000000: n4520_o = 2'b00;
      59'b00000000000100000000000000000000000000000000000000000000000: n4520_o = 2'b00;
      59'b00000000000010000000000000000000000000000000000000000000000: n4520_o = 2'b00;
      59'b00000000000001000000000000000000000000000000000000000000000: n4520_o = 2'b00;
      59'b00000000000000100000000000000000000000000000000000000000000: n4520_o = 2'b00;
      59'b00000000000000010000000000000000000000000000000000000000000: n4520_o = 2'b00;
      59'b00000000000000001000000000000000000000000000000000000000000: n4520_o = 2'b00;
      59'b00000000000000000100000000000000000000000000000000000000000: n4520_o = 2'b00;
      59'b00000000000000000010000000000000000000000000000000000000000: n4520_o = 2'b00;
      59'b00000000000000000001000000000000000000000000000000000000000: n4520_o = 2'b00;
      59'b00000000000000000000100000000000000000000000000000000000000: n4520_o = 2'b11;
      59'b00000000000000000000010000000000000000000000000000000000000: n4520_o = 2'b01;
      59'b00000000000000000000001000000000000000000000000000000000000: n4520_o = 2'b00;
      59'b00000000000000000000000100000000000000000000000000000000000: n4520_o = 2'b00;
      59'b00000000000000000000000010000000000000000000000000000000000: n4520_o = 2'b00;
      59'b00000000000000000000000001000000000000000000000000000000000: n4520_o = 2'b00;
      59'b00000000000000000000000000100000000000000000000000000000000: n4520_o = n4512_o;
      59'b00000000000000000000000000010000000000000000000000000000000: n4520_o = 2'b00;
      59'b00000000000000000000000000001000000000000000000000000000000: n4520_o = 2'b00;
      59'b00000000000000000000000000000100000000000000000000000000000: n4520_o = 2'b00;
      59'b00000000000000000000000000000010000000000000000000000000000: n4520_o = 2'b00;
      59'b00000000000000000000000000000001000000000000000000000000000: n4520_o = 2'b00;
      59'b00000000000000000000000000000000100000000000000000000000000: n4520_o = 2'b00;
      59'b00000000000000000000000000000000010000000000000000000000000: n4520_o = 2'b00;
      59'b00000000000000000000000000000000001000000000000000000000000: n4520_o = 2'b00;
      59'b00000000000000000000000000000000000100000000000000000000000: n4520_o = 2'b00;
      59'b00000000000000000000000000000000000010000000000000000000000: n4520_o = 2'b00;
      59'b00000000000000000000000000000000000001000000000000000000000: n4520_o = 2'b00;
      59'b00000000000000000000000000000000000000100000000000000000000: n4520_o = n4511_o;
      59'b00000000000000000000000000000000000000010000000000000000000: n4520_o = 2'b00;
      59'b00000000000000000000000000000000000000001000000000000000000: n4520_o = 2'b00;
      59'b00000000000000000000000000000000000000000100000000000000000: n4520_o = 2'b00;
      59'b00000000000000000000000000000000000000000010000000000000000: n4520_o = n4510_o;
      59'b00000000000000000000000000000000000000000001000000000000000: n4520_o = n4509_o;
      59'b00000000000000000000000000000000000000000000100000000000000: n4520_o = 2'b00;
      59'b00000000000000000000000000000000000000000000010000000000000: n4520_o = 2'b00;
      59'b00000000000000000000000000000000000000000000001000000000000: n4520_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000100000000000: n4520_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000010000000000: n4520_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000001000000000: n4520_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000100000000: n4520_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000010000000: n4520_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000001000000: n4520_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000000100000: n4520_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000000010000: n4520_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000000001000: n4520_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000000000100: n4520_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000000000010: n4520_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000000000001: n4520_o = 2'b00;
      default: n4520_o = 2'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4529_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4529_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4529_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4529_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4529_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4529_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4529_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4529_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4529_o = 1'b0;
      59'b00000000010000000000000000000000000000000000000000000000000: n4529_o = 1'b0;
      59'b00000000001000000000000000000000000000000000000000000000000: n4529_o = n4040_o;
      59'b00000000000100000000000000000000000000000000000000000000000: n4529_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4529_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4529_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4529_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4529_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4529_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4529_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4529_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4529_o = 1'b1;
      59'b00000000000000000000100000000000000000000000000000000000000: n4529_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4529_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4529_o = n3692_o;
      59'b00000000000000000000000100000000000000000000000000000000000: n4529_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4529_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4529_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4529_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4529_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4529_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4529_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4529_o = 1'b1;
      59'b00000000000000000000000000000001000000000000000000000000000: n4529_o = n3503_o;
      59'b00000000000000000000000000000000100000000000000000000000000: n4529_o = 1'b1;
      59'b00000000000000000000000000000000010000000000000000000000000: n4529_o = n3448_o;
      59'b00000000000000000000000000000000001000000000000000000000000: n4529_o = 1'b1;
      59'b00000000000000000000000000000000000100000000000000000000000: n4529_o = n3385_o;
      59'b00000000000000000000000000000000000010000000000000000000000: n4529_o = n3343_o;
      59'b00000000000000000000000000000000000001000000000000000000000: n4529_o = 1'b1;
      59'b00000000000000000000000000000000000000100000000000000000000: n4529_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4529_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4529_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4529_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4529_o = n3096_o;
      59'b00000000000000000000000000000000000000000001000000000000000: n4529_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4529_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4529_o = 1'b0;
      59'b00000000000000000000000000000000000000000000001000000000000: n4529_o = n2960_o;
      59'b00000000000000000000000000000000000000000000000100000000000: n4529_o = n2922_o;
      59'b00000000000000000000000000000000000000000000000010000000000: n4529_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4529_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4529_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4529_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4529_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4529_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4529_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4529_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4529_o = n2686_o;
      59'b00000000000000000000000000000000000000000000000000000000010: n4529_o = n2655_o;
      59'b00000000000000000000000000000000000000000000000000000000001: n4529_o = 1'b1;
      default: n4529_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4532_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4532_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4532_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4532_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4532_o = n4405_o;
      59'b00000100000000000000000000000000000000000000000000000000000: n4532_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4532_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4532_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4532_o = 1'b0;
      59'b00000000010000000000000000000000000000000000000000000000000: n4532_o = 1'b0;
      59'b00000000001000000000000000000000000000000000000000000000000: n4532_o = 1'b0;
      59'b00000000000100000000000000000000000000000000000000000000000: n4532_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4532_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4532_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4532_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4532_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4532_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4532_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4532_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4532_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4532_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4532_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4532_o = 1'b0;
      59'b00000000000000000000000100000000000000000000000000000000000: n4532_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4532_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4532_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4532_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4532_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4532_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4532_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4532_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4532_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4532_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4532_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4532_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4532_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4532_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4532_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4532_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4532_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4532_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4532_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4532_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4532_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4532_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4532_o = 1'b0;
      59'b00000000000000000000000000000000000000000000001000000000000: n4532_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4532_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4532_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4532_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4532_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4532_o = n2813_o;
      59'b00000000000000000000000000000000000000000000000000001000000: n4532_o = n2793_o;
      59'b00000000000000000000000000000000000000000000000000000100000: n4532_o = n2779_o;
      59'b00000000000000000000000000000000000000000000000000000010000: n4532_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4532_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4532_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4532_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4532_o = 1'b0;
      default: n4532_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4535_o = 3'b000;
      59'b01000000000000000000000000000000000000000000000000000000000: n4535_o = 3'b000;
      59'b00100000000000000000000000000000000000000000000000000000000: n4535_o = 3'b000;
      59'b00010000000000000000000000000000000000000000000000000000000: n4535_o = 3'b000;
      59'b00001000000000000000000000000000000000000000000000000000000: n4535_o = 3'b000;
      59'b00000100000000000000000000000000000000000000000000000000000: n4535_o = 3'b000;
      59'b00000010000000000000000000000000000000000000000000000000000: n4535_o = 3'b000;
      59'b00000001000000000000000000000000000000000000000000000000000: n4535_o = 3'b000;
      59'b00000000100000000000000000000000000000000000000000000000000: n4535_o = 3'b000;
      59'b00000000010000000000000000000000000000000000000000000000000: n4535_o = 3'b000;
      59'b00000000001000000000000000000000000000000000000000000000000: n4535_o = n4042_o;
      59'b00000000000100000000000000000000000000000000000000000000000: n4535_o = 3'b000;
      59'b00000000000010000000000000000000000000000000000000000000000: n4535_o = 3'b000;
      59'b00000000000001000000000000000000000000000000000000000000000: n4535_o = 3'b000;
      59'b00000000000000100000000000000000000000000000000000000000000: n4535_o = 3'b000;
      59'b00000000000000010000000000000000000000000000000000000000000: n4535_o = 3'b000;
      59'b00000000000000001000000000000000000000000000000000000000000: n4535_o = 3'b000;
      59'b00000000000000000100000000000000000000000000000000000000000: n4535_o = 3'b000;
      59'b00000000000000000010000000000000000000000000000000000000000: n4535_o = 3'b000;
      59'b00000000000000000001000000000000000000000000000000000000000: n4535_o = 3'b111;
      59'b00000000000000000000100000000000000000000000000000000000000: n4535_o = 3'b000;
      59'b00000000000000000000010000000000000000000000000000000000000: n4535_o = 3'b000;
      59'b00000000000000000000001000000000000000000000000000000000000: n4535_o = n3694_o;
      59'b00000000000000000000000100000000000000000000000000000000000: n4535_o = 3'b000;
      59'b00000000000000000000000010000000000000000000000000000000000: n4535_o = 3'b000;
      59'b00000000000000000000000001000000000000000000000000000000000: n4535_o = 3'b000;
      59'b00000000000000000000000000100000000000000000000000000000000: n4535_o = 3'b000;
      59'b00000000000000000000000000010000000000000000000000000000000: n4535_o = 3'b000;
      59'b00000000000000000000000000001000000000000000000000000000000: n4535_o = 3'b000;
      59'b00000000000000000000000000000100000000000000000000000000000: n4535_o = 3'b000;
      59'b00000000000000000000000000000010000000000000000000000000000: n4535_o = 3'b111;
      59'b00000000000000000000000000000001000000000000000000000000000: n4535_o = n3505_o;
      59'b00000000000000000000000000000000100000000000000000000000000: n4535_o = n2490_o;
      59'b00000000000000000000000000000000010000000000000000000000000: n4535_o = n3450_o;
      59'b00000000000000000000000000000000001000000000000000000000000: n4535_o = n2490_o;
      59'b00000000000000000000000000000000000100000000000000000000000: n4535_o = n3387_o;
      59'b00000000000000000000000000000000000010000000000000000000000: n4535_o = n3345_o;
      59'b00000000000000000000000000000000000001000000000000000000000: n4535_o = 3'b111;
      59'b00000000000000000000000000000000000000100000000000000000000: n4535_o = 3'b000;
      59'b00000000000000000000000000000000000000010000000000000000000: n4535_o = 3'b000;
      59'b00000000000000000000000000000000000000001000000000000000000: n4535_o = 3'b000;
      59'b00000000000000000000000000000000000000000100000000000000000: n4535_o = 3'b000;
      59'b00000000000000000000000000000000000000000010000000000000000: n4535_o = n3098_o;
      59'b00000000000000000000000000000000000000000001000000000000000: n4535_o = 3'b000;
      59'b00000000000000000000000000000000000000000000100000000000000: n4535_o = 3'b000;
      59'b00000000000000000000000000000000000000000000010000000000000: n4535_o = 3'b000;
      59'b00000000000000000000000000000000000000000000001000000000000: n4535_o = n2962_o;
      59'b00000000000000000000000000000000000000000000000100000000000: n4535_o = n2924_o;
      59'b00000000000000000000000000000000000000000000000010000000000: n4535_o = 3'b000;
      59'b00000000000000000000000000000000000000000000000001000000000: n4535_o = 3'b000;
      59'b00000000000000000000000000000000000000000000000000100000000: n4535_o = 3'b000;
      59'b00000000000000000000000000000000000000000000000000010000000: n4535_o = 3'b000;
      59'b00000000000000000000000000000000000000000000000000001000000: n4535_o = 3'b000;
      59'b00000000000000000000000000000000000000000000000000000100000: n4535_o = 3'b000;
      59'b00000000000000000000000000000000000000000000000000000010000: n4535_o = 3'b000;
      59'b00000000000000000000000000000000000000000000000000000001000: n4535_o = 3'b000;
      59'b00000000000000000000000000000000000000000000000000000000100: n4535_o = n2688_o;
      59'b00000000000000000000000000000000000000000000000000000000010: n4535_o = n2657_o;
      59'b00000000000000000000000000000000000000000000000000000000001: n4535_o = n2490_o;
      default: n4535_o = 3'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4538_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4538_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4538_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4538_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4538_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4538_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4538_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4538_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4538_o = 1'b0;
      59'b00000000010000000000000000000000000000000000000000000000000: n4538_o = 1'b0;
      59'b00000000001000000000000000000000000000000000000000000000000: n4538_o = 1'b0;
      59'b00000000000100000000000000000000000000000000000000000000000: n4538_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4538_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4538_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4538_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4538_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4538_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4538_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4538_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4538_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4538_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4538_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4538_o = 1'b0;
      59'b00000000000000000000000100000000000000000000000000000000000: n4538_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4538_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4538_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4538_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4538_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4538_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4538_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4538_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4538_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4538_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4538_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4538_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4538_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4538_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4538_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4538_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4538_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4538_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4538_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4538_o = n3100_o;
      59'b00000000000000000000000000000000000000000001000000000000000: n4538_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4538_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4538_o = 1'b0;
      59'b00000000000000000000000000000000000000000000001000000000000: n4538_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4538_o = n2926_o;
      59'b00000000000000000000000000000000000000000000000010000000000: n4538_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4538_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4538_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4538_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4538_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4538_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4538_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4538_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4538_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4538_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4538_o = 1'b0;
      default: n4538_o = 1'bX;
    endcase
  assign n4539_o = n2491_o[0]; // extract
  assign n4540_o = n2720_o[0]; // extract
  assign n4541_o = n2760_o[0]; // extract
  assign n4542_o = n2830_o[0]; // extract
  assign n4543_o = n2850_o[0]; // extract
  assign n4544_o = n2876_o[0]; // extract
  assign n4545_o = n2995_o[0]; // extract
  assign n4546_o = n3043_o[0]; // extract
  assign n4547_o = n3144_o[0]; // extract
  assign n4548_o = n2491_o[0]; // extract
  assign n4549_o = n3347_o[0]; // extract
  assign n4550_o = n3389_o[0]; // extract
  assign n4552_o = n3453_o[0]; // extract
  assign n4554_o = n3508_o[0]; // extract
  assign n4555_o = n3624_o[0]; // extract
  assign n4556_o = n4045_o[0]; // extract
  assign n4557_o = n4089_o[0]; // extract
  assign n4558_o = n4185_o[0]; // extract
  assign n4559_o = n4359_o[0]; // extract
  assign n4560_o = n4425_o[0]; // extract
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4563_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4563_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4563_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4563_o = n4560_o;
      59'b00001000000000000000000000000000000000000000000000000000000: n4563_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4563_o = n4559_o;
      59'b00000010000000000000000000000000000000000000000000000000000: n4563_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4563_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4563_o = n4558_o;
      59'b00000000010000000000000000000000000000000000000000000000000: n4563_o = n4557_o;
      59'b00000000001000000000000000000000000000000000000000000000000: n4563_o = n4556_o;
      59'b00000000000100000000000000000000000000000000000000000000000: n4563_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4563_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4563_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4563_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4563_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4563_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4563_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4563_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4563_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4563_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4563_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4563_o = n3696_o;
      59'b00000000000000000000000100000000000000000000000000000000000: n4563_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4563_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4563_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4563_o = n4555_o;
      59'b00000000000000000000000000010000000000000000000000000000000: n4563_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4563_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4563_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4563_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4563_o = n4554_o;
      59'b00000000000000000000000000000000100000000000000000000000000: n4563_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4563_o = n4552_o;
      59'b00000000000000000000000000000000001000000000000000000000000: n4563_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4563_o = n4550_o;
      59'b00000000000000000000000000000000000010000000000000000000000: n4563_o = n4549_o;
      59'b00000000000000000000000000000000000001000000000000000000000: n4563_o = n4548_o;
      59'b00000000000000000000000000000000000000100000000000000000000: n4563_o = n4547_o;
      59'b00000000000000000000000000000000000000010000000000000000000: n4563_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4563_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4563_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4563_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4563_o = n4546_o;
      59'b00000000000000000000000000000000000000000000100000000000000: n4563_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4563_o = n4545_o;
      59'b00000000000000000000000000000000000000000000001000000000000: n4563_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4563_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4563_o = n4544_o;
      59'b00000000000000000000000000000000000000000000000001000000000: n4563_o = n4543_o;
      59'b00000000000000000000000000000000000000000000000000100000000: n4563_o = n4542_o;
      59'b00000000000000000000000000000000000000000000000000010000000: n4563_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4563_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4563_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4563_o = n4541_o;
      59'b00000000000000000000000000000000000000000000000000000001000: n4563_o = n4540_o;
      59'b00000000000000000000000000000000000000000000000000000000100: n4563_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4563_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4563_o = n4539_o;
      default: n4563_o = 1'bX;
    endcase
  assign n4564_o = n2491_o[2:1]; // extract
  assign n4565_o = n2720_o[2:1]; // extract
  assign n4566_o = n2760_o[2:1]; // extract
  assign n4567_o = n2830_o[2:1]; // extract
  assign n4568_o = n2850_o[2:1]; // extract
  assign n4569_o = n2876_o[2:1]; // extract
  assign n4570_o = n2995_o[2:1]; // extract
  assign n4571_o = n3043_o[2:1]; // extract
  assign n4572_o = n3144_o[2:1]; // extract
  assign n4573_o = n2491_o[2:1]; // extract
  assign n4574_o = n3347_o[2:1]; // extract
  assign n4575_o = n3389_o[2:1]; // extract
  assign n4577_o = n3453_o[2:1]; // extract
  assign n4579_o = n3508_o[2:1]; // extract
  assign n4580_o = n3624_o[2:1]; // extract
  assign n4581_o = n4045_o[2:1]; // extract
  assign n4582_o = n4089_o[2:1]; // extract
  assign n4583_o = n4185_o[2:1]; // extract
  assign n4584_o = n4359_o[2:1]; // extract
  assign n4585_o = n4425_o[2:1]; // extract
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4588_o = 2'b00;
      59'b01000000000000000000000000000000000000000000000000000000000: n4588_o = 2'b00;
      59'b00100000000000000000000000000000000000000000000000000000000: n4588_o = 2'b00;
      59'b00010000000000000000000000000000000000000000000000000000000: n4588_o = n4585_o;
      59'b00001000000000000000000000000000000000000000000000000000000: n4588_o = 2'b00;
      59'b00000100000000000000000000000000000000000000000000000000000: n4588_o = n4584_o;
      59'b00000010000000000000000000000000000000000000000000000000000: n4588_o = 2'b00;
      59'b00000001000000000000000000000000000000000000000000000000000: n4588_o = 2'b00;
      59'b00000000100000000000000000000000000000000000000000000000000: n4588_o = n4583_o;
      59'b00000000010000000000000000000000000000000000000000000000000: n4588_o = n4582_o;
      59'b00000000001000000000000000000000000000000000000000000000000: n4588_o = n4581_o;
      59'b00000000000100000000000000000000000000000000000000000000000: n4588_o = 2'b00;
      59'b00000000000010000000000000000000000000000000000000000000000: n4588_o = 2'b00;
      59'b00000000000001000000000000000000000000000000000000000000000: n4588_o = 2'b00;
      59'b00000000000000100000000000000000000000000000000000000000000: n4588_o = 2'b00;
      59'b00000000000000010000000000000000000000000000000000000000000: n4588_o = 2'b00;
      59'b00000000000000001000000000000000000000000000000000000000000: n4588_o = 2'b00;
      59'b00000000000000000100000000000000000000000000000000000000000: n4588_o = 2'b00;
      59'b00000000000000000010000000000000000000000000000000000000000: n4588_o = 2'b00;
      59'b00000000000000000001000000000000000000000000000000000000000: n4588_o = 2'b00;
      59'b00000000000000000000100000000000000000000000000000000000000: n4588_o = 2'b00;
      59'b00000000000000000000010000000000000000000000000000000000000: n4588_o = 2'b00;
      59'b00000000000000000000001000000000000000000000000000000000000: n4588_o = n3698_o;
      59'b00000000000000000000000100000000000000000000000000000000000: n4588_o = 2'b00;
      59'b00000000000000000000000010000000000000000000000000000000000: n4588_o = 2'b00;
      59'b00000000000000000000000001000000000000000000000000000000000: n4588_o = 2'b00;
      59'b00000000000000000000000000100000000000000000000000000000000: n4588_o = n4580_o;
      59'b00000000000000000000000000010000000000000000000000000000000: n4588_o = 2'b00;
      59'b00000000000000000000000000001000000000000000000000000000000: n4588_o = 2'b00;
      59'b00000000000000000000000000000100000000000000000000000000000: n4588_o = 2'b00;
      59'b00000000000000000000000000000010000000000000000000000000000: n4588_o = 2'b00;
      59'b00000000000000000000000000000001000000000000000000000000000: n4588_o = n4579_o;
      59'b00000000000000000000000000000000100000000000000000000000000: n4588_o = 2'b01;
      59'b00000000000000000000000000000000010000000000000000000000000: n4588_o = n4577_o;
      59'b00000000000000000000000000000000001000000000000000000000000: n4588_o = 2'b01;
      59'b00000000000000000000000000000000000100000000000000000000000: n4588_o = n4575_o;
      59'b00000000000000000000000000000000000010000000000000000000000: n4588_o = n4574_o;
      59'b00000000000000000000000000000000000001000000000000000000000: n4588_o = n4573_o;
      59'b00000000000000000000000000000000000000100000000000000000000: n4588_o = n4572_o;
      59'b00000000000000000000000000000000000000010000000000000000000: n4588_o = 2'b00;
      59'b00000000000000000000000000000000000000001000000000000000000: n4588_o = 2'b00;
      59'b00000000000000000000000000000000000000000100000000000000000: n4588_o = 2'b00;
      59'b00000000000000000000000000000000000000000010000000000000000: n4588_o = 2'b00;
      59'b00000000000000000000000000000000000000000001000000000000000: n4588_o = n4571_o;
      59'b00000000000000000000000000000000000000000000100000000000000: n4588_o = 2'b00;
      59'b00000000000000000000000000000000000000000000010000000000000: n4588_o = n4570_o;
      59'b00000000000000000000000000000000000000000000001000000000000: n4588_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000100000000000: n4588_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000010000000000: n4588_o = n4569_o;
      59'b00000000000000000000000000000000000000000000000001000000000: n4588_o = n4568_o;
      59'b00000000000000000000000000000000000000000000000000100000000: n4588_o = n4567_o;
      59'b00000000000000000000000000000000000000000000000000010000000: n4588_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000001000000: n4588_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000000100000: n4588_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000000010000: n4588_o = n4566_o;
      59'b00000000000000000000000000000000000000000000000000000001000: n4588_o = n4565_o;
      59'b00000000000000000000000000000000000000000000000000000000100: n4588_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000000000010: n4588_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000000000001: n4588_o = n4564_o;
      default: n4588_o = 2'bX;
    endcase
  assign n4589_o = n2830_o[3]; // extract
  assign n4590_o = n2850_o[3]; // extract
  assign n4591_o = n2876_o[3]; // extract
  assign n4592_o = n2995_o[3]; // extract
  assign n4593_o = n3043_o[3]; // extract
  assign n4594_o = n3144_o[3]; // extract
  assign n4596_o = n3453_o[3]; // extract
  assign n4598_o = n3508_o[3]; // extract
  assign n4599_o = n3624_o[3]; // extract
  assign n4600_o = n4045_o[3]; // extract
  assign n4601_o = n4089_o[3]; // extract
  assign n4602_o = n4185_o[3]; // extract
  assign n4603_o = n4359_o[3]; // extract
  assign n4604_o = n4425_o[3]; // extract
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4607_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4607_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4607_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4607_o = n4604_o;
      59'b00001000000000000000000000000000000000000000000000000000000: n4607_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4607_o = n4603_o;
      59'b00000010000000000000000000000000000000000000000000000000000: n4607_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4607_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4607_o = n4602_o;
      59'b00000000010000000000000000000000000000000000000000000000000: n4607_o = n4601_o;
      59'b00000000001000000000000000000000000000000000000000000000000: n4607_o = n4600_o;
      59'b00000000000100000000000000000000000000000000000000000000000: n4607_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4607_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4607_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4607_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4607_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4607_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4607_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4607_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4607_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4607_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4607_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4607_o = n3700_o;
      59'b00000000000000000000000100000000000000000000000000000000000: n4607_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4607_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4607_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4607_o = n4599_o;
      59'b00000000000000000000000000010000000000000000000000000000000: n4607_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4607_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4607_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4607_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4607_o = n4598_o;
      59'b00000000000000000000000000000000100000000000000000000000000: n4607_o = 1'b1;
      59'b00000000000000000000000000000000010000000000000000000000000: n4607_o = n4596_o;
      59'b00000000000000000000000000000000001000000000000000000000000: n4607_o = 1'b1;
      59'b00000000000000000000000000000000000100000000000000000000000: n4607_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4607_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4607_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4607_o = n4594_o;
      59'b00000000000000000000000000000000000000010000000000000000000: n4607_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4607_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4607_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4607_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4607_o = n4593_o;
      59'b00000000000000000000000000000000000000000000100000000000000: n4607_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4607_o = n4592_o;
      59'b00000000000000000000000000000000000000000000001000000000000: n4607_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4607_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4607_o = n4591_o;
      59'b00000000000000000000000000000000000000000000000001000000000: n4607_o = n4590_o;
      59'b00000000000000000000000000000000000000000000000000100000000: n4607_o = n4589_o;
      59'b00000000000000000000000000000000000000000000000000010000000: n4607_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4607_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4607_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4607_o = n2762_o;
      59'b00000000000000000000000000000000000000000000000000000001000: n4607_o = n2722_o;
      59'b00000000000000000000000000000000000000000000000000000000100: n4607_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4607_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4607_o = 1'b0;
      default: n4607_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4613_o = n2500_o;
      59'b01000000000000000000000000000000000000000000000000000000000: n4613_o = n2500_o;
      59'b00100000000000000000000000000000000000000000000000000000000: n4613_o = n2500_o;
      59'b00010000000000000000000000000000000000000000000000000000000: n4613_o = n2500_o;
      59'b00001000000000000000000000000000000000000000000000000000000: n4613_o = n2500_o;
      59'b00000100000000000000000000000000000000000000000000000000000: n4613_o = n2500_o;
      59'b00000010000000000000000000000000000000000000000000000000000: n4613_o = n2500_o;
      59'b00000001000000000000000000000000000000000000000000000000000: n4613_o = n2500_o;
      59'b00000000100000000000000000000000000000000000000000000000000: n4613_o = n2500_o;
      59'b00000000010000000000000000000000000000000000000000000000000: n4613_o = n2500_o;
      59'b00000000001000000000000000000000000000000000000000000000000: n4613_o = n4047_o;
      59'b00000000000100000000000000000000000000000000000000000000000: n4613_o = n2500_o;
      59'b00000000000010000000000000000000000000000000000000000000000: n4613_o = n2500_o;
      59'b00000000000001000000000000000000000000000000000000000000000: n4613_o = n2500_o;
      59'b00000000000000100000000000000000000000000000000000000000000: n4613_o = n2500_o;
      59'b00000000000000010000000000000000000000000000000000000000000: n4613_o = n2500_o;
      59'b00000000000000001000000000000000000000000000000000000000000: n4613_o = n2500_o;
      59'b00000000000000000100000000000000000000000000000000000000000: n4613_o = n2500_o;
      59'b00000000000000000010000000000000000000000000000000000000000: n4613_o = n2500_o;
      59'b00000000000000000001000000000000000000000000000000000000000: n4613_o = 4'b1000;
      59'b00000000000000000000100000000000000000000000000000000000000: n4613_o = n2500_o;
      59'b00000000000000000000010000000000000000000000000000000000000: n4613_o = n2500_o;
      59'b00000000000000000000001000000000000000000000000000000000000: n4613_o = n3703_o;
      59'b00000000000000000000000100000000000000000000000000000000000: n4613_o = n2500_o;
      59'b00000000000000000000000010000000000000000000000000000000000: n4613_o = n2500_o;
      59'b00000000000000000000000001000000000000000000000000000000000: n4613_o = n2500_o;
      59'b00000000000000000000000000100000000000000000000000000000000: n4613_o = n2500_o;
      59'b00000000000000000000000000010000000000000000000000000000000: n4613_o = n2500_o;
      59'b00000000000000000000000000001000000000000000000000000000000: n4613_o = n2500_o;
      59'b00000000000000000000000000000100000000000000000000000000000: n4613_o = n2500_o;
      59'b00000000000000000000000000000010000000000000000000000000000: n4613_o = 4'b1100;
      59'b00000000000000000000000000000001000000000000000000000000000: n4613_o = n3510_o;
      59'b00000000000000000000000000000000100000000000000000000000000: n4613_o = 4'b0010;
      59'b00000000000000000000000000000000010000000000000000000000000: n4613_o = n3455_o;
      59'b00000000000000000000000000000000001000000000000000000000000: n4613_o = 4'b0000;
      59'b00000000000000000000000000000000000100000000000000000000000: n4613_o = n2500_o;
      59'b00000000000000000000000000000000000010000000000000000000000: n4613_o = n2500_o;
      59'b00000000000000000000000000000000000001000000000000000000000: n4613_o = n2500_o;
      59'b00000000000000000000000000000000000000100000000000000000000: n4613_o = n2500_o;
      59'b00000000000000000000000000000000000000010000000000000000000: n4613_o = n2500_o;
      59'b00000000000000000000000000000000000000001000000000000000000: n4613_o = n2500_o;
      59'b00000000000000000000000000000000000000000100000000000000000: n4613_o = n2500_o;
      59'b00000000000000000000000000000000000000000010000000000000000: n4613_o = n2500_o;
      59'b00000000000000000000000000000000000000000001000000000000000: n4613_o = n2500_o;
      59'b00000000000000000000000000000000000000000000100000000000000: n4613_o = n2500_o;
      59'b00000000000000000000000000000000000000000000010000000000000: n4613_o = n2500_o;
      59'b00000000000000000000000000000000000000000000001000000000000: n4613_o = n2500_o;
      59'b00000000000000000000000000000000000000000000000100000000000: n4613_o = n2500_o;
      59'b00000000000000000000000000000000000000000000000010000000000: n4613_o = n2500_o;
      59'b00000000000000000000000000000000000000000000000001000000000: n4613_o = n2500_o;
      59'b00000000000000000000000000000000000000000000000000100000000: n4613_o = n2500_o;
      59'b00000000000000000000000000000000000000000000000000010000000: n4613_o = n2500_o;
      59'b00000000000000000000000000000000000000000000000000001000000: n4613_o = n2500_o;
      59'b00000000000000000000000000000000000000000000000000000100000: n4613_o = n2500_o;
      59'b00000000000000000000000000000000000000000000000000000010000: n4613_o = n2500_o;
      59'b00000000000000000000000000000000000000000000000000000001000: n4613_o = n2500_o;
      59'b00000000000000000000000000000000000000000000000000000000100: n4613_o = n2500_o;
      59'b00000000000000000000000000000000000000000000000000000000010: n4613_o = n2500_o;
      59'b00000000000000000000000000000000000000000000000000000000001: n4613_o = n2500_o;
      default: n4613_o = 4'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4621_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4621_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4621_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4621_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4621_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4621_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4621_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4621_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4621_o = 1'b0;
      59'b00000000010000000000000000000000000000000000000000000000000: n4621_o = 1'b0;
      59'b00000000001000000000000000000000000000000000000000000000000: n4621_o = n4050_o;
      59'b00000000000100000000000000000000000000000000000000000000000: n4621_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4621_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4621_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4621_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4621_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4621_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4621_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4621_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4621_o = 1'b1;
      59'b00000000000000000000100000000000000000000000000000000000000: n4621_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4621_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4621_o = n3707_o;
      59'b00000000000000000000000100000000000000000000000000000000000: n4621_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4621_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4621_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4621_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4621_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4621_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4621_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4621_o = 1'b1;
      59'b00000000000000000000000000000001000000000000000000000000000: n4621_o = n3513_o;
      59'b00000000000000000000000000000000100000000000000000000000000: n4621_o = 1'b1;
      59'b00000000000000000000000000000000010000000000000000000000000: n4621_o = n3458_o;
      59'b00000000000000000000000000000000001000000000000000000000000: n4621_o = 1'b1;
      59'b00000000000000000000000000000000000100000000000000000000000: n4621_o = n3392_o;
      59'b00000000000000000000000000000000000010000000000000000000000: n4621_o = n3350_o;
      59'b00000000000000000000000000000000000001000000000000000000000: n4621_o = 1'b1;
      59'b00000000000000000000000000000000000000100000000000000000000: n4621_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4621_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4621_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4621_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4621_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4621_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4621_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4621_o = 1'b0;
      59'b00000000000000000000000000000000000000000000001000000000000: n4621_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4621_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4621_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4621_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4621_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4621_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4621_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4621_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4621_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4621_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4621_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4621_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4621_o = 1'b0;
      default: n4621_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4625_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4625_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4625_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4625_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4625_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4625_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4625_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4625_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4625_o = 1'b0;
      59'b00000000010000000000000000000000000000000000000000000000000: n4625_o = 1'b0;
      59'b00000000001000000000000000000000000000000000000000000000000: n4625_o = 1'b0;
      59'b00000000000100000000000000000000000000000000000000000000000: n4625_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4625_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4625_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4625_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4625_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4625_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4625_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4625_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4625_o = 1'b1;
      59'b00000000000000000000100000000000000000000000000000000000000: n4625_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4625_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4625_o = 1'b0;
      59'b00000000000000000000000100000000000000000000000000000000000: n4625_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4625_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4625_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4625_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4625_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4625_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4625_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4625_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4625_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4625_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4625_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4625_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4625_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4625_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4625_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4625_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4625_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4625_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4625_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4625_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4625_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4625_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4625_o = 1'b0;
      59'b00000000000000000000000000000000000000000000001000000000000: n4625_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4625_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4625_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4625_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4625_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4625_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4625_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4625_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4625_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4625_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4625_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4625_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4625_o = 1'b0;
      default: n4625_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4630_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4630_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4630_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4630_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4630_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4630_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4630_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4630_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4630_o = 1'b0;
      59'b00000000010000000000000000000000000000000000000000000000000: n4630_o = 1'b0;
      59'b00000000001000000000000000000000000000000000000000000000000: n4630_o = 1'b0;
      59'b00000000000100000000000000000000000000000000000000000000000: n4630_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4630_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4630_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4630_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4630_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4630_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4630_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4630_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4630_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4630_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4630_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4630_o = 1'b0;
      59'b00000000000000000000000100000000000000000000000000000000000: n4630_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4630_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4630_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4630_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4630_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4630_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4630_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4630_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4630_o = n3516_o;
      59'b00000000000000000000000000000000100000000000000000000000000: n4630_o = 1'b1;
      59'b00000000000000000000000000000000010000000000000000000000000: n4630_o = n3461_o;
      59'b00000000000000000000000000000000001000000000000000000000000: n4630_o = 1'b1;
      59'b00000000000000000000000000000000000100000000000000000000000: n4630_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4630_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4630_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4630_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4630_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4630_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4630_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4630_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4630_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4630_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4630_o = 1'b0;
      59'b00000000000000000000000000000000000000000000001000000000000: n4630_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4630_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4630_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4630_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4630_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4630_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4630_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4630_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4630_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4630_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4630_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4630_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4630_o = 1'b0;
      default: n4630_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4633_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4633_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4633_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4633_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4633_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4633_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4633_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4633_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4633_o = 1'b0;
      59'b00000000010000000000000000000000000000000000000000000000000: n4633_o = 1'b0;
      59'b00000000001000000000000000000000000000000000000000000000000: n4633_o = 1'b0;
      59'b00000000000100000000000000000000000000000000000000000000000: n4633_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4633_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4633_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4633_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4633_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4633_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4633_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4633_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4633_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4633_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4633_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4633_o = n3711_o;
      59'b00000000000000000000000100000000000000000000000000000000000: n4633_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4633_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4633_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4633_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4633_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4633_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4633_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4633_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4633_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4633_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4633_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4633_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4633_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4633_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4633_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4633_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4633_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4633_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4633_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4633_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4633_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4633_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4633_o = 1'b0;
      59'b00000000000000000000000000000000000000000000001000000000000: n4633_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4633_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4633_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4633_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4633_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4633_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4633_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4633_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4633_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4633_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4633_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4633_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4633_o = 1'b0;
      default: n4633_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4636_o = 3'b111;
      59'b01000000000000000000000000000000000000000000000000000000000: n4636_o = 3'b111;
      59'b00100000000000000000000000000000000000000000000000000000000: n4636_o = 3'b111;
      59'b00010000000000000000000000000000000000000000000000000000000: n4636_o = n4428_o;
      59'b00001000000000000000000000000000000000000000000000000000000: n4636_o = n4408_o;
      59'b00000100000000000000000000000000000000000000000000000000000: n4636_o = n4363_o;
      59'b00000010000000000000000000000000000000000000000000000000000: n4636_o = n4312_o;
      59'b00000001000000000000000000000000000000000000000000000000000: n4636_o = n4240_o;
      59'b00000000100000000000000000000000000000000000000000000000000: n4636_o = n4188_o;
      59'b00000000010000000000000000000000000000000000000000000000000: n4636_o = n4093_o;
      59'b00000000001000000000000000000000000000000000000000000000000: n4636_o = 3'b111;
      59'b00000000000100000000000000000000000000000000000000000000000: n4636_o = 3'b111;
      59'b00000000000010000000000000000000000000000000000000000000000: n4636_o = 3'b111;
      59'b00000000000001000000000000000000000000000000000000000000000: n4636_o = 3'b111;
      59'b00000000000000100000000000000000000000000000000000000000000: n4636_o = 3'b111;
      59'b00000000000000010000000000000000000000000000000000000000000: n4636_o = 3'b111;
      59'b00000000000000001000000000000000000000000000000000000000000: n4636_o = 3'b111;
      59'b00000000000000000100000000000000000000000000000000000000000: n4636_o = 3'b111;
      59'b00000000000000000010000000000000000000000000000000000000000: n4636_o = 3'b111;
      59'b00000000000000000001000000000000000000000000000000000000000: n4636_o = 3'b111;
      59'b00000000000000000000100000000000000000000000000000000000000: n4636_o = 3'b111;
      59'b00000000000000000000010000000000000000000000000000000000000: n4636_o = 3'b111;
      59'b00000000000000000000001000000000000000000000000000000000000: n4636_o = 3'b111;
      59'b00000000000000000000000100000000000000000000000000000000000: n4636_o = 3'b111;
      59'b00000000000000000000000010000000000000000000000000000000000: n4636_o = 3'b111;
      59'b00000000000000000000000001000000000000000000000000000000000: n4636_o = 3'b111;
      59'b00000000000000000000000000100000000000000000000000000000000: n4636_o = n3625_o;
      59'b00000000000000000000000000010000000000000000000000000000000: n4636_o = 3'b111;
      59'b00000000000000000000000000001000000000000000000000000000000: n4636_o = 3'b111;
      59'b00000000000000000000000000000100000000000000000000000000000: n4636_o = 3'b111;
      59'b00000000000000000000000000000010000000000000000000000000000: n4636_o = 3'b111;
      59'b00000000000000000000000000000001000000000000000000000000000: n4636_o = n3520_o;
      59'b00000000000000000000000000000000100000000000000000000000000: n4636_o = 3'b111;
      59'b00000000000000000000000000000000010000000000000000000000000: n4636_o = n3465_o;
      59'b00000000000000000000000000000000001000000000000000000000000: n4636_o = 3'b111;
      59'b00000000000000000000000000000000000100000000000000000000000: n4636_o = 3'b111;
      59'b00000000000000000000000000000000000010000000000000000000000: n4636_o = n3353_o;
      59'b00000000000000000000000000000000000001000000000000000000000: n4636_o = 3'b111;
      59'b00000000000000000000000000000000000000100000000000000000000: n4636_o = n3150_o;
      59'b00000000000000000000000000000000000000010000000000000000000: n4636_o = 3'b111;
      59'b00000000000000000000000000000000000000001000000000000000000: n4636_o = 3'b111;
      59'b00000000000000000000000000000000000000000100000000000000000: n4636_o = 3'b111;
      59'b00000000000000000000000000000000000000000010000000000000000: n4636_o = n3104_o;
      59'b00000000000000000000000000000000000000000001000000000000000: n4636_o = n3047_o;
      59'b00000000000000000000000000000000000000000000100000000000000: n4636_o = 3'b111;
      59'b00000000000000000000000000000000000000000000010000000000000: n4636_o = n2999_o;
      59'b00000000000000000000000000000000000000000000001000000000000: n4636_o = n2966_o;
      59'b00000000000000000000000000000000000000000000000100000000000: n4636_o = 3'b111;
      59'b00000000000000000000000000000000000000000000000010000000000: n4636_o = n2879_o;
      59'b00000000000000000000000000000000000000000000000001000000000: n4636_o = n2853_o;
      59'b00000000000000000000000000000000000000000000000000100000000: n4636_o = n2833_o;
      59'b00000000000000000000000000000000000000000000000000010000000: n4636_o = n2816_o;
      59'b00000000000000000000000000000000000000000000000000001000000: n4636_o = n2796_o;
      59'b00000000000000000000000000000000000000000000000000000100000: n4636_o = n2782_o;
      59'b00000000000000000000000000000000000000000000000000000010000: n4636_o = n2765_o;
      59'b00000000000000000000000000000000000000000000000000000001000: n4636_o = n2725_o;
      59'b00000000000000000000000000000000000000000000000000000000100: n4636_o = n2691_o;
      59'b00000000000000000000000000000000000000000000000000000000010: n4636_o = 3'b111;
      59'b00000000000000000000000000000000000000000000000000000000001: n4636_o = 3'b111;
      default: n4636_o = 3'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4639_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4639_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4639_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4639_o = n4431_o;
      59'b00001000000000000000000000000000000000000000000000000000000: n4639_o = n4411_o;
      59'b00000100000000000000000000000000000000000000000000000000000: n4639_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4639_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4639_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4639_o = 1'b0;
      59'b00000000010000000000000000000000000000000000000000000000000: n4639_o = 1'b0;
      59'b00000000001000000000000000000000000000000000000000000000000: n4639_o = 1'b0;
      59'b00000000000100000000000000000000000000000000000000000000000: n4639_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4639_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4639_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4639_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4639_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4639_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4639_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4639_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4639_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4639_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4639_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4639_o = 1'b0;
      59'b00000000000000000000000100000000000000000000000000000000000: n4639_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4639_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4639_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4639_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4639_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4639_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4639_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4639_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4639_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4639_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4639_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4639_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4639_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4639_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4639_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4639_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4639_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4639_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4639_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4639_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4639_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4639_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4639_o = 1'b0;
      59'b00000000000000000000000000000000000000000000001000000000000: n4639_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4639_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4639_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4639_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4639_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4639_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4639_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4639_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4639_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4639_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4639_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4639_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4639_o = 1'b0;
      default: n4639_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4642_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4642_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4642_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4642_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4642_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4642_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4642_o = n4315_o;
      59'b00000001000000000000000000000000000000000000000000000000000: n4642_o = n4243_o;
      59'b00000000100000000000000000000000000000000000000000000000000: n4642_o = 1'b0;
      59'b00000000010000000000000000000000000000000000000000000000000: n4642_o = 1'b0;
      59'b00000000001000000000000000000000000000000000000000000000000: n4642_o = 1'b0;
      59'b00000000000100000000000000000000000000000000000000000000000: n4642_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4642_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4642_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4642_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4642_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4642_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4642_o = n3843_o;
      59'b00000000000000000010000000000000000000000000000000000000000: n4642_o = n3782_o;
      59'b00000000000000000001000000000000000000000000000000000000000: n4642_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4642_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4642_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4642_o = 1'b0;
      59'b00000000000000000000000100000000000000000000000000000000000: n4642_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4642_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4642_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4642_o = n3627_o;
      59'b00000000000000000000000000010000000000000000000000000000000: n4642_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4642_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4642_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4642_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4642_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4642_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4642_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4642_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4642_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4642_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4642_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4642_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4642_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4642_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4642_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4642_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4642_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4642_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4642_o = 1'b0;
      59'b00000000000000000000000000000000000000000000001000000000000: n4642_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4642_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4642_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4642_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4642_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4642_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4642_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4642_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4642_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4642_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4642_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4642_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4642_o = 1'b0;
      default: n4642_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4645_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4645_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4645_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4645_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4645_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4645_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4645_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4645_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4645_o = 1'b0;
      59'b00000000010000000000000000000000000000000000000000000000000: n4645_o = 1'b0;
      59'b00000000001000000000000000000000000000000000000000000000000: n4645_o = n4053_o;
      59'b00000000000100000000000000000000000000000000000000000000000: n4645_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4645_o = n4013_o;
      59'b00000000000001000000000000000000000000000000000000000000000: n4645_o = n3982_o;
      59'b00000000000000100000000000000000000000000000000000000000000: n4645_o = n3950_o;
      59'b00000000000000010000000000000000000000000000000000000000000: n4645_o = n3919_o;
      59'b00000000000000001000000000000000000000000000000000000000000: n4645_o = n3886_o;
      59'b00000000000000000100000000000000000000000000000000000000000: n4645_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4645_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4645_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4645_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4645_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4645_o = 1'b0;
      59'b00000000000000000000000100000000000000000000000000000000000: n4645_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4645_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4645_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4645_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4645_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4645_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4645_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4645_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4645_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4645_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4645_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4645_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4645_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4645_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4645_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4645_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4645_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4645_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4645_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4645_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4645_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4645_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4645_o = 1'b0;
      59'b00000000000000000000000000000000000000000000001000000000000: n4645_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4645_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4645_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4645_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4645_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4645_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4645_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4645_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4645_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4645_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4645_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4645_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4645_o = 1'b0;
      default: n4645_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4649_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4649_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4649_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4649_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4649_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4649_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4649_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4649_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4649_o = 1'b0;
      59'b00000000010000000000000000000000000000000000000000000000000: n4649_o = 1'b0;
      59'b00000000001000000000000000000000000000000000000000000000000: n4649_o = 1'b0;
      59'b00000000000100000000000000000000000000000000000000000000000: n4649_o = 1'b1;
      59'b00000000000010000000000000000000000000000000000000000000000: n4649_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4649_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4649_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4649_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4649_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4649_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4649_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4649_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4649_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4649_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4649_o = 1'b0;
      59'b00000000000000000000000100000000000000000000000000000000000: n4649_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4649_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4649_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4649_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4649_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4649_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4649_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4649_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4649_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4649_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4649_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4649_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4649_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4649_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4649_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4649_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4649_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4649_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4649_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4649_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4649_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4649_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4649_o = 1'b0;
      59'b00000000000000000000000000000000000000000000001000000000000: n4649_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4649_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4649_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4649_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4649_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4649_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4649_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4649_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4649_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4649_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4649_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4649_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4649_o = 1'b0;
      default: n4649_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4652_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4652_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4652_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4652_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4652_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4652_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4652_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4652_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4652_o = n4191_o;
      59'b00000000010000000000000000000000000000000000000000000000000: n4652_o = n4096_o;
      59'b00000000001000000000000000000000000000000000000000000000000: n4652_o = 1'b0;
      59'b00000000000100000000000000000000000000000000000000000000000: n4652_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4652_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4652_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4652_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4652_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4652_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4652_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4652_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4652_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4652_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4652_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4652_o = 1'b0;
      59'b00000000000000000000000100000000000000000000000000000000000: n4652_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4652_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4652_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4652_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4652_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4652_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4652_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4652_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4652_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4652_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4652_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4652_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4652_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4652_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4652_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4652_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4652_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4652_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4652_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4652_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4652_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4652_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4652_o = 1'b0;
      59'b00000000000000000000000000000000000000000000001000000000000: n4652_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4652_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4652_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4652_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4652_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4652_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4652_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4652_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4652_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4652_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4652_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4652_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4652_o = 1'b0;
      default: n4652_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4655_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4655_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4655_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4655_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4655_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4655_o = n4366_o;
      59'b00000010000000000000000000000000000000000000000000000000000: n4655_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4655_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4655_o = 1'b0;
      59'b00000000010000000000000000000000000000000000000000000000000: n4655_o = 1'b0;
      59'b00000000001000000000000000000000000000000000000000000000000: n4655_o = 1'b0;
      59'b00000000000100000000000000000000000000000000000000000000000: n4655_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4655_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4655_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4655_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4655_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4655_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4655_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4655_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4655_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4655_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4655_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4655_o = 1'b0;
      59'b00000000000000000000000100000000000000000000000000000000000: n4655_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4655_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4655_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4655_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4655_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4655_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4655_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4655_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4655_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4655_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4655_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4655_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4655_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4655_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4655_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4655_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4655_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4655_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4655_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4655_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4655_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4655_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4655_o = 1'b0;
      59'b00000000000000000000000000000000000000000000001000000000000: n4655_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4655_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4655_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4655_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4655_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4655_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4655_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4655_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4655_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4655_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4655_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4655_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4655_o = 1'b0;
      default: n4655_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4658_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4658_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4658_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4658_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4658_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4658_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4658_o = n4318_o;
      59'b00000001000000000000000000000000000000000000000000000000000: n4658_o = n4246_o;
      59'b00000000100000000000000000000000000000000000000000000000000: n4658_o = n4194_o;
      59'b00000000010000000000000000000000000000000000000000000000000: n4658_o = n4099_o;
      59'b00000000001000000000000000000000000000000000000000000000000: n4658_o = 1'b0;
      59'b00000000000100000000000000000000000000000000000000000000000: n4658_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4658_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4658_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4658_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4658_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4658_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4658_o = n3846_o;
      59'b00000000000000000010000000000000000000000000000000000000000: n4658_o = n3785_o;
      59'b00000000000000000001000000000000000000000000000000000000000: n4658_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4658_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4658_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4658_o = 1'b0;
      59'b00000000000000000000000100000000000000000000000000000000000: n4658_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4658_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4658_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4658_o = n3629_o;
      59'b00000000000000000000000000010000000000000000000000000000000: n4658_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4658_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4658_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4658_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4658_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4658_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4658_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4658_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4658_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4658_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4658_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4658_o = n3153_o;
      59'b00000000000000000000000000000000000000010000000000000000000: n4658_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4658_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4658_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4658_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4658_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4658_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4658_o = n3002_o;
      59'b00000000000000000000000000000000000000000000001000000000000: n4658_o = n2969_o;
      59'b00000000000000000000000000000000000000000000000100000000000: n4658_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4658_o = n2882_o;
      59'b00000000000000000000000000000000000000000000000001000000000: n4658_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4658_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4658_o = n2819_o;
      59'b00000000000000000000000000000000000000000000000000001000000: n4658_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4658_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4658_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4658_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4658_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4658_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4658_o = 1'b0;
      default: n4658_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4661_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4661_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4661_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4661_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4661_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4661_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4661_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4661_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4661_o = n4197_o;
      59'b00000000010000000000000000000000000000000000000000000000000: n4661_o = n4102_o;
      59'b00000000001000000000000000000000000000000000000000000000000: n4661_o = 1'b0;
      59'b00000000000100000000000000000000000000000000000000000000000: n4661_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4661_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4661_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4661_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4661_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4661_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4661_o = n3849_o;
      59'b00000000000000000010000000000000000000000000000000000000000: n4661_o = n3788_o;
      59'b00000000000000000001000000000000000000000000000000000000000: n4661_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4661_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4661_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4661_o = 1'b0;
      59'b00000000000000000000000100000000000000000000000000000000000: n4661_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4661_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4661_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4661_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4661_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4661_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4661_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4661_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4661_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4661_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4661_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4661_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4661_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4661_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4661_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4661_o = n3156_o;
      59'b00000000000000000000000000000000000000010000000000000000000: n4661_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4661_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4661_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4661_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4661_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4661_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4661_o = n3005_o;
      59'b00000000000000000000000000000000000000000000001000000000000: n4661_o = n2972_o;
      59'b00000000000000000000000000000000000000000000000100000000000: n4661_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4661_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4661_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4661_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4661_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4661_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4661_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4661_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4661_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4661_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4661_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4661_o = 1'b0;
      default: n4661_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4665_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4665_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4665_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4665_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4665_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4665_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4665_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4665_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4665_o = 1'b0;
      59'b00000000010000000000000000000000000000000000000000000000000: n4665_o = 1'b0;
      59'b00000000001000000000000000000000000000000000000000000000000: n4665_o = 1'b0;
      59'b00000000000100000000000000000000000000000000000000000000000: n4665_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4665_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4665_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4665_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4665_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4665_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4665_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4665_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4665_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4665_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4665_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4665_o = 1'b0;
      59'b00000000000000000000000100000000000000000000000000000000000: n4665_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4665_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4665_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4665_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4665_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4665_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4665_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4665_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4665_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4665_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4665_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4665_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4665_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4665_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4665_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4665_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4665_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4665_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4665_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4665_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4665_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4665_o = 1'b1;
      59'b00000000000000000000000000000000000000000000010000000000000: n4665_o = 1'b0;
      59'b00000000000000000000000000000000000000000000001000000000000: n4665_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4665_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4665_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4665_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4665_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4665_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4665_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4665_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4665_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4665_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4665_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4665_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4665_o = 1'b0;
      default: n4665_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4669_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4669_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4669_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4669_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4669_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4669_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4669_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4669_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4669_o = 1'b0;
      59'b00000000010000000000000000000000000000000000000000000000000: n4669_o = 1'b0;
      59'b00000000001000000000000000000000000000000000000000000000000: n4669_o = 1'b0;
      59'b00000000000100000000000000000000000000000000000000000000000: n4669_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4669_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4669_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4669_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4669_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4669_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4669_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4669_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4669_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4669_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4669_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4669_o = 1'b0;
      59'b00000000000000000000000100000000000000000000000000000000000: n4669_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4669_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4669_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4669_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4669_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4669_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4669_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4669_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4669_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4669_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4669_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4669_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4669_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4669_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4669_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4669_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4669_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4669_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4669_o = 1'b1;
      59'b00000000000000000000000000000000000000000010000000000000000: n4669_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4669_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4669_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4669_o = 1'b0;
      59'b00000000000000000000000000000000000000000000001000000000000: n4669_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4669_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4669_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4669_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4669_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4669_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4669_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4669_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4669_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4669_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4669_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4669_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4669_o = 1'b0;
      default: n4669_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4673_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4673_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4673_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4673_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4673_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4673_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4673_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4673_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4673_o = 1'b0;
      59'b00000000010000000000000000000000000000000000000000000000000: n4673_o = 1'b0;
      59'b00000000001000000000000000000000000000000000000000000000000: n4673_o = 1'b0;
      59'b00000000000100000000000000000000000000000000000000000000000: n4673_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4673_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4673_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4673_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4673_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4673_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4673_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4673_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4673_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4673_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4673_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4673_o = 1'b0;
      59'b00000000000000000000000100000000000000000000000000000000000: n4673_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4673_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4673_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4673_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4673_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4673_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4673_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4673_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4673_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4673_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4673_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4673_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4673_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4673_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4673_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4673_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4673_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4673_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4673_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4673_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4673_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4673_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4673_o = 1'b0;
      59'b00000000000000000000000000000000000000000000001000000000000: n4673_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4673_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4673_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4673_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4673_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4673_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4673_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4673_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4673_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4673_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4673_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4673_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4673_o = 1'b1;
      default: n4673_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4677_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4677_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4677_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4677_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4677_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4677_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4677_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4677_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4677_o = 1'b0;
      59'b00000000010000000000000000000000000000000000000000000000000: n4677_o = 1'b0;
      59'b00000000001000000000000000000000000000000000000000000000000: n4677_o = 1'b0;
      59'b00000000000100000000000000000000000000000000000000000000000: n4677_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4677_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4677_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4677_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4677_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4677_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4677_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4677_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4677_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4677_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4677_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4677_o = 1'b0;
      59'b00000000000000000000000100000000000000000000000000000000000: n4677_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4677_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4677_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4677_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4677_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4677_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4677_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4677_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4677_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4677_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4677_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4677_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4677_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4677_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4677_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4677_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4677_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4677_o = 1'b1;
      59'b00000000000000000000000000000000000000000100000000000000000: n4677_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4677_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4677_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4677_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4677_o = 1'b0;
      59'b00000000000000000000000000000000000000000000001000000000000: n4677_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4677_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4677_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4677_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4677_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4677_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4677_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4677_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4677_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4677_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4677_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4677_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4677_o = 1'b0;
      default: n4677_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4681_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4681_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4681_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4681_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4681_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4681_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4681_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4681_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4681_o = 1'b0;
      59'b00000000010000000000000000000000000000000000000000000000000: n4681_o = 1'b0;
      59'b00000000001000000000000000000000000000000000000000000000000: n4681_o = 1'b0;
      59'b00000000000100000000000000000000000000000000000000000000000: n4681_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4681_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4681_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4681_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4681_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4681_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4681_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4681_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4681_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4681_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4681_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4681_o = 1'b0;
      59'b00000000000000000000000100000000000000000000000000000000000: n4681_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4681_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4681_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4681_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4681_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4681_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4681_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4681_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4681_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4681_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4681_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4681_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4681_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4681_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4681_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4681_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4681_o = 1'b1;
      59'b00000000000000000000000000000000000000001000000000000000000: n4681_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4681_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4681_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4681_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4681_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4681_o = 1'b0;
      59'b00000000000000000000000000000000000000000000001000000000000: n4681_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4681_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4681_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4681_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4681_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4681_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4681_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4681_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4681_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4681_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4681_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4681_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4681_o = 1'b0;
      default: n4681_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4684_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4684_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4684_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4684_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4684_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4684_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4684_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4684_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4684_o = 1'b0;
      59'b00000000010000000000000000000000000000000000000000000000000: n4684_o = 1'b0;
      59'b00000000001000000000000000000000000000000000000000000000000: n4684_o = 1'b0;
      59'b00000000000100000000000000000000000000000000000000000000000: n4684_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4684_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4684_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4684_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4684_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4684_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4684_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4684_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4684_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4684_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4684_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4684_o = 1'b0;
      59'b00000000000000000000000100000000000000000000000000000000000: n4684_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4684_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4684_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4684_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4684_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4684_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4684_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4684_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4684_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4684_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4684_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4684_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4684_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4684_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4684_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4684_o = n3159_o;
      59'b00000000000000000000000000000000000000010000000000000000000: n4684_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4684_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4684_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4684_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4684_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4684_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4684_o = 1'b0;
      59'b00000000000000000000000000000000000000000000001000000000000: n4684_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4684_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4684_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4684_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4684_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4684_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4684_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4684_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4684_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4684_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4684_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4684_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4684_o = 1'b0;
      default: n4684_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4687_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4687_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4687_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4687_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4687_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4687_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4687_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4687_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4687_o = 1'b0;
      59'b00000000010000000000000000000000000000000000000000000000000: n4687_o = 1'b0;
      59'b00000000001000000000000000000000000000000000000000000000000: n4687_o = n4057_o;
      59'b00000000000100000000000000000000000000000000000000000000000: n4687_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4687_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4687_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4687_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4687_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4687_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4687_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4687_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4687_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4687_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4687_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4687_o = 1'b0;
      59'b00000000000000000000000100000000000000000000000000000000000: n4687_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4687_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4687_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4687_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4687_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4687_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4687_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4687_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4687_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4687_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4687_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4687_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4687_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4687_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4687_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4687_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4687_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4687_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4687_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4687_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4687_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4687_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4687_o = 1'b0;
      59'b00000000000000000000000000000000000000000000001000000000000: n4687_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4687_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4687_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4687_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4687_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4687_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4687_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4687_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4687_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4687_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4687_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4687_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4687_o = 1'b0;
      default: n4687_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4691_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4691_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4691_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4691_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4691_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4691_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4691_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4691_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4691_o = 1'b0;
      59'b00000000010000000000000000000000000000000000000000000000000: n4691_o = 1'b0;
      59'b00000000001000000000000000000000000000000000000000000000000: n4691_o = 1'b0;
      59'b00000000000100000000000000000000000000000000000000000000000: n4691_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4691_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4691_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4691_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4691_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4691_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4691_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4691_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4691_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4691_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4691_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4691_o = 1'b0;
      59'b00000000000000000000000100000000000000000000000000000000000: n4691_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4691_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4691_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4691_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4691_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4691_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4691_o = 1'b1;
      59'b00000000000000000000000000000010000000000000000000000000000: n4691_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4691_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4691_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4691_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4691_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4691_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4691_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4691_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4691_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4691_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4691_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4691_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4691_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4691_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4691_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4691_o = 1'b0;
      59'b00000000000000000000000000000000000000000000001000000000000: n4691_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4691_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4691_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4691_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4691_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4691_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4691_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4691_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4691_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4691_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4691_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4691_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4691_o = 1'b0;
      default: n4691_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4695_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4695_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4695_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4695_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4695_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4695_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4695_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4695_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4695_o = 1'b0;
      59'b00000000010000000000000000000000000000000000000000000000000: n4695_o = 1'b0;
      59'b00000000001000000000000000000000000000000000000000000000000: n4695_o = 1'b0;
      59'b00000000000100000000000000000000000000000000000000000000000: n4695_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4695_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4695_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4695_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4695_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4695_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4695_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4695_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4695_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4695_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4695_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4695_o = 1'b0;
      59'b00000000000000000000000100000000000000000000000000000000000: n4695_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4695_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4695_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4695_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4695_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4695_o = 1'b1;
      59'b00000000000000000000000000000100000000000000000000000000000: n4695_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4695_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4695_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4695_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4695_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4695_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4695_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4695_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4695_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4695_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4695_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4695_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4695_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4695_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4695_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4695_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4695_o = 1'b0;
      59'b00000000000000000000000000000000000000000000001000000000000: n4695_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4695_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4695_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4695_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4695_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4695_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4695_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4695_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4695_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4695_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4695_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4695_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4695_o = 1'b0;
      default: n4695_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4699_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4699_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4699_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4699_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4699_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4699_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4699_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4699_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4699_o = 1'b0;
      59'b00000000010000000000000000000000000000000000000000000000000: n4699_o = 1'b0;
      59'b00000000001000000000000000000000000000000000000000000000000: n4699_o = 1'b0;
      59'b00000000000100000000000000000000000000000000000000000000000: n4699_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4699_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4699_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4699_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4699_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4699_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4699_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4699_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4699_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4699_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4699_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4699_o = 1'b0;
      59'b00000000000000000000000100000000000000000000000000000000000: n4699_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4699_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4699_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4699_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4699_o = 1'b1;
      59'b00000000000000000000000000001000000000000000000000000000000: n4699_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4699_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4699_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4699_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4699_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4699_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4699_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4699_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4699_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4699_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4699_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4699_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4699_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4699_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4699_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4699_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4699_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4699_o = 1'b0;
      59'b00000000000000000000000000000000000000000000001000000000000: n4699_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4699_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4699_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4699_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4699_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4699_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4699_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4699_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4699_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4699_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4699_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4699_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4699_o = 1'b0;
      default: n4699_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4702_o = 2'b00;
      59'b01000000000000000000000000000000000000000000000000000000000: n4702_o = 2'b00;
      59'b00100000000000000000000000000000000000000000000000000000000: n4702_o = 2'b00;
      59'b00010000000000000000000000000000000000000000000000000000000: n4702_o = 2'b00;
      59'b00001000000000000000000000000000000000000000000000000000000: n4702_o = 2'b00;
      59'b00000100000000000000000000000000000000000000000000000000000: n4702_o = 2'b00;
      59'b00000010000000000000000000000000000000000000000000000000000: n4702_o = 2'b00;
      59'b00000001000000000000000000000000000000000000000000000000000: n4702_o = 2'b00;
      59'b00000000100000000000000000000000000000000000000000000000000: n4702_o = 2'b00;
      59'b00000000010000000000000000000000000000000000000000000000000: n4702_o = 2'b00;
      59'b00000000001000000000000000000000000000000000000000000000000: n4702_o = 2'b00;
      59'b00000000000100000000000000000000000000000000000000000000000: n4702_o = 2'b00;
      59'b00000000000010000000000000000000000000000000000000000000000: n4702_o = 2'b00;
      59'b00000000000001000000000000000000000000000000000000000000000: n4702_o = 2'b00;
      59'b00000000000000100000000000000000000000000000000000000000000: n4702_o = 2'b00;
      59'b00000000000000010000000000000000000000000000000000000000000: n4702_o = 2'b00;
      59'b00000000000000001000000000000000000000000000000000000000000: n4702_o = 2'b00;
      59'b00000000000000000100000000000000000000000000000000000000000: n4702_o = 2'b00;
      59'b00000000000000000010000000000000000000000000000000000000000: n4702_o = 2'b00;
      59'b00000000000000000001000000000000000000000000000000000000000: n4702_o = 2'b00;
      59'b00000000000000000000100000000000000000000000000000000000000: n4702_o = 2'b00;
      59'b00000000000000000000010000000000000000000000000000000000000: n4702_o = 2'b00;
      59'b00000000000000000000001000000000000000000000000000000000000: n4702_o = n3714_o;
      59'b00000000000000000000000100000000000000000000000000000000000: n4702_o = 2'b00;
      59'b00000000000000000000000010000000000000000000000000000000000: n4702_o = 2'b00;
      59'b00000000000000000000000001000000000000000000000000000000000: n4702_o = 2'b00;
      59'b00000000000000000000000000100000000000000000000000000000000: n4702_o = 2'b00;
      59'b00000000000000000000000000010000000000000000000000000000000: n4702_o = 2'b00;
      59'b00000000000000000000000000001000000000000000000000000000000: n4702_o = 2'b00;
      59'b00000000000000000000000000000100000000000000000000000000000: n4702_o = 2'b00;
      59'b00000000000000000000000000000010000000000000000000000000000: n4702_o = 2'b00;
      59'b00000000000000000000000000000001000000000000000000000000000: n4702_o = 2'b00;
      59'b00000000000000000000000000000000100000000000000000000000000: n4702_o = 2'b00;
      59'b00000000000000000000000000000000010000000000000000000000000: n4702_o = 2'b00;
      59'b00000000000000000000000000000000001000000000000000000000000: n4702_o = 2'b00;
      59'b00000000000000000000000000000000000100000000000000000000000: n4702_o = 2'b00;
      59'b00000000000000000000000000000000000010000000000000000000000: n4702_o = 2'b00;
      59'b00000000000000000000000000000000000001000000000000000000000: n4702_o = 2'b00;
      59'b00000000000000000000000000000000000000100000000000000000000: n4702_o = 2'b00;
      59'b00000000000000000000000000000000000000010000000000000000000: n4702_o = 2'b00;
      59'b00000000000000000000000000000000000000001000000000000000000: n4702_o = 2'b00;
      59'b00000000000000000000000000000000000000000100000000000000000: n4702_o = 2'b00;
      59'b00000000000000000000000000000000000000000010000000000000000: n4702_o = 2'b00;
      59'b00000000000000000000000000000000000000000001000000000000000: n4702_o = 2'b00;
      59'b00000000000000000000000000000000000000000000100000000000000: n4702_o = 2'b00;
      59'b00000000000000000000000000000000000000000000010000000000000: n4702_o = 2'b00;
      59'b00000000000000000000000000000000000000000000001000000000000: n4702_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000100000000000: n4702_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000010000000000: n4702_o = n2885_o;
      59'b00000000000000000000000000000000000000000000000001000000000: n4702_o = n2856_o;
      59'b00000000000000000000000000000000000000000000000000100000000: n4702_o = n2836_o;
      59'b00000000000000000000000000000000000000000000000000010000000: n4702_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000001000000: n4702_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000000100000: n4702_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000000010000: n4702_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000000001000: n4702_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000000000100: n4702_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000000000010: n4702_o = 2'b00;
      59'b00000000000000000000000000000000000000000000000000000000001: n4702_o = 2'b00;
      default: n4702_o = 2'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4706_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4706_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4706_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4706_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4706_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4706_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4706_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4706_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4706_o = 1'b0;
      59'b00000000010000000000000000000000000000000000000000000000000: n4706_o = 1'b0;
      59'b00000000001000000000000000000000000000000000000000000000000: n4706_o = 1'b0;
      59'b00000000000100000000000000000000000000000000000000000000000: n4706_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4706_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4706_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4706_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4706_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4706_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4706_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4706_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4706_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4706_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4706_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4706_o = 1'b0;
      59'b00000000000000000000000100000000000000000000000000000000000: n4706_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4706_o = 1'b1;
      59'b00000000000000000000000001000000000000000000000000000000000: n4706_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4706_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4706_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4706_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4706_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4706_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4706_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4706_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4706_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4706_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4706_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4706_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4706_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4706_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4706_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4706_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4706_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4706_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4706_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4706_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4706_o = 1'b0;
      59'b00000000000000000000000000000000000000000000001000000000000: n4706_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4706_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4706_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4706_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4706_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4706_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4706_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4706_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4706_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4706_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4706_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4706_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4706_o = 1'b0;
      default: n4706_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4710_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4710_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4710_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4710_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4710_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4710_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4710_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4710_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4710_o = 1'b0;
      59'b00000000010000000000000000000000000000000000000000000000000: n4710_o = 1'b0;
      59'b00000000001000000000000000000000000000000000000000000000000: n4710_o = 1'b0;
      59'b00000000000100000000000000000000000000000000000000000000000: n4710_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4710_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4710_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4710_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4710_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4710_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4710_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4710_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4710_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4710_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4710_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4710_o = 1'b0;
      59'b00000000000000000000000100000000000000000000000000000000000: n4710_o = 1'b1;
      59'b00000000000000000000000010000000000000000000000000000000000: n4710_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4710_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4710_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4710_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4710_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4710_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4710_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4710_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4710_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4710_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4710_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4710_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4710_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4710_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4710_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4710_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4710_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4710_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4710_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4710_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4710_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4710_o = 1'b0;
      59'b00000000000000000000000000000000000000000000001000000000000: n4710_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4710_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4710_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4710_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4710_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4710_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4710_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4710_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4710_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4710_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4710_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4710_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4710_o = 1'b0;
      default: n4710_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4714_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4714_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4714_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4714_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4714_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4714_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4714_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4714_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4714_o = 1'b0;
      59'b00000000010000000000000000000000000000000000000000000000000: n4714_o = 1'b0;
      59'b00000000001000000000000000000000000000000000000000000000000: n4714_o = 1'b0;
      59'b00000000000100000000000000000000000000000000000000000000000: n4714_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4714_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4714_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4714_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4714_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4714_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4714_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4714_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4714_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4714_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4714_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4714_o = 1'b0;
      59'b00000000000000000000000100000000000000000000000000000000000: n4714_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4714_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4714_o = 1'b1;
      59'b00000000000000000000000000100000000000000000000000000000000: n4714_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4714_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4714_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4714_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4714_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4714_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4714_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4714_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4714_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4714_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4714_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4714_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4714_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4714_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4714_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4714_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4714_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4714_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4714_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4714_o = 1'b0;
      59'b00000000000000000000000000000000000000000000001000000000000: n4714_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4714_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4714_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4714_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4714_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4714_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4714_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4714_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4714_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4714_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4714_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4714_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4714_o = 1'b0;
      default: n4714_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4717_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4717_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4717_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4717_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4717_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4717_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4717_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4717_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4717_o = 1'b0;
      59'b00000000010000000000000000000000000000000000000000000000000: n4717_o = 1'b0;
      59'b00000000001000000000000000000000000000000000000000000000000: n4717_o = n4060_o;
      59'b00000000000100000000000000000000000000000000000000000000000: n4717_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4717_o = n4016_o;
      59'b00000000000001000000000000000000000000000000000000000000000: n4717_o = n3985_o;
      59'b00000000000000100000000000000000000000000000000000000000000: n4717_o = n3953_o;
      59'b00000000000000010000000000000000000000000000000000000000000: n4717_o = n3922_o;
      59'b00000000000000001000000000000000000000000000000000000000000: n4717_o = n3889_o;
      59'b00000000000000000100000000000000000000000000000000000000000: n4717_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4717_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4717_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4717_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4717_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4717_o = n3718_o;
      59'b00000000000000000000000100000000000000000000000000000000000: n4717_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4717_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4717_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4717_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4717_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4717_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4717_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4717_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4717_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4717_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4717_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4717_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4717_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4717_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4717_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4717_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4717_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4717_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4717_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4717_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4717_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4717_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4717_o = 1'b0;
      59'b00000000000000000000000000000000000000000000001000000000000: n4717_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4717_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4717_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4717_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4717_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4717_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4717_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4717_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4717_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4717_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4717_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4717_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4717_o = 1'b0;
      default: n4717_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4720_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4720_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4720_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4720_o = n4434_o;
      59'b00001000000000000000000000000000000000000000000000000000000: n4720_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4720_o = n4370_o;
      59'b00000010000000000000000000000000000000000000000000000000000: n4720_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4720_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4720_o = n4201_o;
      59'b00000000010000000000000000000000000000000000000000000000000: n4720_o = n4106_o;
      59'b00000000001000000000000000000000000000000000000000000000000: n4720_o = 1'b0;
      59'b00000000000100000000000000000000000000000000000000000000000: n4720_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4720_o = 1'b0;
      59'b00000000000001000000000000000000000000000000000000000000000: n4720_o = 1'b0;
      59'b00000000000000100000000000000000000000000000000000000000000: n4720_o = 1'b0;
      59'b00000000000000010000000000000000000000000000000000000000000: n4720_o = 1'b0;
      59'b00000000000000001000000000000000000000000000000000000000000: n4720_o = 1'b0;
      59'b00000000000000000100000000000000000000000000000000000000000: n4720_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4720_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4720_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4720_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4720_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4720_o = 1'b0;
      59'b00000000000000000000000100000000000000000000000000000000000: n4720_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4720_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4720_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4720_o = n3630_o;
      59'b00000000000000000000000000010000000000000000000000000000000: n4720_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4720_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4720_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4720_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4720_o = n3523_o;
      59'b00000000000000000000000000000000100000000000000000000000000: n4720_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4720_o = n3468_o;
      59'b00000000000000000000000000000000001000000000000000000000000: n4720_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4720_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4720_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4720_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4720_o = n3163_o;
      59'b00000000000000000000000000000000000000010000000000000000000: n4720_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4720_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4720_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4720_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4720_o = n3051_o;
      59'b00000000000000000000000000000000000000000000100000000000000: n4720_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4720_o = n3009_o;
      59'b00000000000000000000000000000000000000000000001000000000000: n4720_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4720_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4720_o = n2888_o;
      59'b00000000000000000000000000000000000000000000000001000000000: n4720_o = n2859_o;
      59'b00000000000000000000000000000000000000000000000000100000000: n4720_o = n2839_o;
      59'b00000000000000000000000000000000000000000000000000010000000: n4720_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4720_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4720_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4720_o = n2768_o;
      59'b00000000000000000000000000000000000000000000000000000001000: n4720_o = n2728_o;
      59'b00000000000000000000000000000000000000000000000000000000100: n4720_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4720_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4720_o = 1'b0;
      default: n4720_o = 1'bX;
    endcase
  /* T80_MCode.vhd:274:17  */
  always @*
    case (n4446_o)
      59'b10000000000000000000000000000000000000000000000000000000000: n4723_o = 1'b0;
      59'b01000000000000000000000000000000000000000000000000000000000: n4723_o = 1'b0;
      59'b00100000000000000000000000000000000000000000000000000000000: n4723_o = 1'b0;
      59'b00010000000000000000000000000000000000000000000000000000000: n4723_o = 1'b0;
      59'b00001000000000000000000000000000000000000000000000000000000: n4723_o = 1'b0;
      59'b00000100000000000000000000000000000000000000000000000000000: n4723_o = 1'b0;
      59'b00000010000000000000000000000000000000000000000000000000000: n4723_o = 1'b0;
      59'b00000001000000000000000000000000000000000000000000000000000: n4723_o = 1'b0;
      59'b00000000100000000000000000000000000000000000000000000000000: n4723_o = 1'b0;
      59'b00000000010000000000000000000000000000000000000000000000000: n4723_o = 1'b0;
      59'b00000000001000000000000000000000000000000000000000000000000: n4723_o = n4063_o;
      59'b00000000000100000000000000000000000000000000000000000000000: n4723_o = 1'b0;
      59'b00000000000010000000000000000000000000000000000000000000000: n4723_o = n4018_o;
      59'b00000000000001000000000000000000000000000000000000000000000: n4723_o = n3987_o;
      59'b00000000000000100000000000000000000000000000000000000000000: n4723_o = n3955_o;
      59'b00000000000000010000000000000000000000000000000000000000000: n4723_o = n3924_o;
      59'b00000000000000001000000000000000000000000000000000000000000: n4723_o = n3892_o;
      59'b00000000000000000100000000000000000000000000000000000000000: n4723_o = 1'b0;
      59'b00000000000000000010000000000000000000000000000000000000000: n4723_o = 1'b0;
      59'b00000000000000000001000000000000000000000000000000000000000: n4723_o = 1'b0;
      59'b00000000000000000000100000000000000000000000000000000000000: n4723_o = 1'b0;
      59'b00000000000000000000010000000000000000000000000000000000000: n4723_o = 1'b0;
      59'b00000000000000000000001000000000000000000000000000000000000: n4723_o = n3722_o;
      59'b00000000000000000000000100000000000000000000000000000000000: n4723_o = 1'b0;
      59'b00000000000000000000000010000000000000000000000000000000000: n4723_o = 1'b0;
      59'b00000000000000000000000001000000000000000000000000000000000: n4723_o = 1'b0;
      59'b00000000000000000000000000100000000000000000000000000000000: n4723_o = 1'b0;
      59'b00000000000000000000000000010000000000000000000000000000000: n4723_o = 1'b0;
      59'b00000000000000000000000000001000000000000000000000000000000: n4723_o = 1'b0;
      59'b00000000000000000000000000000100000000000000000000000000000: n4723_o = 1'b0;
      59'b00000000000000000000000000000010000000000000000000000000000: n4723_o = 1'b0;
      59'b00000000000000000000000000000001000000000000000000000000000: n4723_o = 1'b0;
      59'b00000000000000000000000000000000100000000000000000000000000: n4723_o = 1'b0;
      59'b00000000000000000000000000000000010000000000000000000000000: n4723_o = 1'b0;
      59'b00000000000000000000000000000000001000000000000000000000000: n4723_o = 1'b0;
      59'b00000000000000000000000000000000000100000000000000000000000: n4723_o = 1'b0;
      59'b00000000000000000000000000000000000010000000000000000000000: n4723_o = 1'b0;
      59'b00000000000000000000000000000000000001000000000000000000000: n4723_o = 1'b0;
      59'b00000000000000000000000000000000000000100000000000000000000: n4723_o = 1'b0;
      59'b00000000000000000000000000000000000000010000000000000000000: n4723_o = 1'b0;
      59'b00000000000000000000000000000000000000001000000000000000000: n4723_o = 1'b0;
      59'b00000000000000000000000000000000000000000100000000000000000: n4723_o = 1'b0;
      59'b00000000000000000000000000000000000000000010000000000000000: n4723_o = 1'b0;
      59'b00000000000000000000000000000000000000000001000000000000000: n4723_o = 1'b0;
      59'b00000000000000000000000000000000000000000000100000000000000: n4723_o = 1'b0;
      59'b00000000000000000000000000000000000000000000010000000000000: n4723_o = 1'b0;
      59'b00000000000000000000000000000000000000000000001000000000000: n4723_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000100000000000: n4723_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000010000000000: n4723_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000001000000000: n4723_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000100000000: n4723_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000010000000: n4723_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000001000000: n4723_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000100000: n4723_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000010000: n4723_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000001000: n4723_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000100: n4723_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000010: n4723_o = 1'b0;
      59'b00000000000000000000000000000000000000000000000000000000001: n4723_o = 1'b0;
      default: n4723_o = 1'bX;
    endcase
  /* T80_MCode.vhd:266:17  */
  assign n4725_o = iset == 2'b00;
  /* T80_MCode.vhd:1556:54  */
  assign n4726_o = ir[2:0]; // extract
  /* T80_MCode.vhd:1557:54  */
  assign n4727_o = ir[2:0]; // extract
  /* T80_MCode.vhd:1576:44  */
  assign n4729_o = xy_state == 2'b00;
  /* T80_MCode.vhd:1577:51  */
  assign n4731_o = mcycle == 3'b001;
  /* T80_MCode.vhd:1577:41  */
  assign n4734_o = n4731_o ? 1'b1 : 1'b0;
  /* T80_MCode.vhd:1577:41  */
  assign n4736_o = n4731_o ? 4'b1000 : n2500_o;
  /* T80_MCode.vhd:1577:41  */
  assign n4739_o = n4731_o ? 1'b1 : 1'b0;
  /* T80_MCode.vhd:1586:46  */
  assign n4740_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:1587:41  */
  assign n4742_o = n4740_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:1587:48  */
  assign n4744_o = n4740_o == 31'b0000000000000000000000000000111;
  /* T80_MCode.vhd:1587:48  */
  assign n4745_o = n4742_o | n4744_o;
  /* T80_MCode.vhd:1589:41  */
  assign n4747_o = n4740_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:1595:41  */
  assign n4749_o = n4740_o == 31'b0000000000000000000000000000011;
  assign n4750_o = {n4749_o, n4747_o, n4745_o};
  /* T80_MCode.vhd:1586:41  */
  always @*
    case (n4750_o)
      3'b100: n4752_o = n2497_o;
      3'b010: n4752_o = 3'b100;
      3'b001: n4752_o = n2497_o;
      default: n4752_o = n2497_o;
    endcase
  /* T80_MCode.vhd:1586:41  */
  always @*
    case (n4750_o)
      3'b100: n4755_o = 1'b0;
      3'b010: n4755_o = 1'b1;
      3'b001: n4755_o = 1'b0;
      default: n4755_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1586:41  */
  always @*
    case (n4750_o)
      3'b100: n4757_o = n2500_o;
      3'b010: n4757_o = 4'b1000;
      3'b001: n4757_o = n2500_o;
      default: n4757_o = n2500_o;
    endcase
  /* T80_MCode.vhd:1586:41  */
  always @*
    case (n4750_o)
      3'b100: n4760_o = 1'b0;
      3'b010: n4760_o = 1'b1;
      3'b001: n4760_o = 1'b0;
      default: n4760_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1586:41  */
  always @*
    case (n4750_o)
      3'b100: n4764_o = 3'b111;
      3'b010: n4764_o = 3'b010;
      3'b001: n4764_o = 3'b010;
      default: n4764_o = 3'b111;
    endcase
  /* T80_MCode.vhd:1586:41  */
  always @*
    case (n4750_o)
      3'b100: n4767_o = 1'b1;
      3'b010: n4767_o = 1'b0;
      3'b001: n4767_o = 1'b0;
      default: n4767_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1576:33  */
  assign n4770_o = n4729_o ? 3'b001 : 3'b011;
  /* T80_MCode.vhd:1576:33  */
  assign n4771_o = n4729_o ? n2497_o : n4752_o;
  /* T80_MCode.vhd:1576:33  */
  assign n4772_o = n4729_o ? n4734_o : n4755_o;
  /* T80_MCode.vhd:1576:33  */
  assign n4773_o = n4729_o ? n4736_o : n4757_o;
  /* T80_MCode.vhd:1576:33  */
  assign n4774_o = n4729_o ? n4739_o : n4760_o;
  /* T80_MCode.vhd:1576:33  */
  assign n4776_o = n4729_o ? 3'b111 : n4764_o;
  /* T80_MCode.vhd:1576:33  */
  assign n4778_o = n4729_o ? 1'b0 : n4767_o;
  /* T80_MCode.vhd:1576:33  */
  assign n4781_o = n4729_o ? 1'b0 : 1'b1;
  /* T80_MCode.vhd:1560:25  */
  assign n4783_o = ir == 8'b00000000;
  /* T80_MCode.vhd:1560:40  */
  assign n4785_o = ir == 8'b00000001;
  /* T80_MCode.vhd:1560:40  */
  assign n4786_o = n4783_o | n4785_o;
  /* T80_MCode.vhd:1560:51  */
  assign n4788_o = ir == 8'b00000010;
  /* T80_MCode.vhd:1560:51  */
  assign n4789_o = n4786_o | n4788_o;
  /* T80_MCode.vhd:1560:62  */
  assign n4791_o = ir == 8'b00000011;
  /* T80_MCode.vhd:1560:62  */
  assign n4792_o = n4789_o | n4791_o;
  /* T80_MCode.vhd:1560:73  */
  assign n4794_o = ir == 8'b00000100;
  /* T80_MCode.vhd:1560:73  */
  assign n4795_o = n4792_o | n4794_o;
  /* T80_MCode.vhd:1560:84  */
  assign n4797_o = ir == 8'b00000101;
  /* T80_MCode.vhd:1560:84  */
  assign n4798_o = n4795_o | n4797_o;
  /* T80_MCode.vhd:1560:95  */
  assign n4800_o = ir == 8'b00000111;
  /* T80_MCode.vhd:1560:95  */
  assign n4801_o = n4798_o | n4800_o;
  /* T80_MCode.vhd:1561:33  */
  assign n4803_o = ir == 8'b00010000;
  /* T80_MCode.vhd:1561:33  */
  assign n4804_o = n4801_o | n4803_o;
  /* T80_MCode.vhd:1561:44  */
  assign n4806_o = ir == 8'b00010001;
  /* T80_MCode.vhd:1561:44  */
  assign n4807_o = n4804_o | n4806_o;
  /* T80_MCode.vhd:1561:55  */
  assign n4809_o = ir == 8'b00010010;
  /* T80_MCode.vhd:1561:55  */
  assign n4810_o = n4807_o | n4809_o;
  /* T80_MCode.vhd:1561:66  */
  assign n4812_o = ir == 8'b00010011;
  /* T80_MCode.vhd:1561:66  */
  assign n4813_o = n4810_o | n4812_o;
  /* T80_MCode.vhd:1561:77  */
  assign n4815_o = ir == 8'b00010100;
  /* T80_MCode.vhd:1561:77  */
  assign n4816_o = n4813_o | n4815_o;
  /* T80_MCode.vhd:1561:88  */
  assign n4818_o = ir == 8'b00010101;
  /* T80_MCode.vhd:1561:88  */
  assign n4819_o = n4816_o | n4818_o;
  /* T80_MCode.vhd:1561:99  */
  assign n4821_o = ir == 8'b00010111;
  /* T80_MCode.vhd:1561:99  */
  assign n4822_o = n4819_o | n4821_o;
  /* T80_MCode.vhd:1562:33  */
  assign n4824_o = ir == 8'b00001000;
  /* T80_MCode.vhd:1562:33  */
  assign n4825_o = n4822_o | n4824_o;
  /* T80_MCode.vhd:1562:44  */
  assign n4827_o = ir == 8'b00001001;
  /* T80_MCode.vhd:1562:44  */
  assign n4828_o = n4825_o | n4827_o;
  /* T80_MCode.vhd:1562:55  */
  assign n4830_o = ir == 8'b00001010;
  /* T80_MCode.vhd:1562:55  */
  assign n4831_o = n4828_o | n4830_o;
  /* T80_MCode.vhd:1562:66  */
  assign n4833_o = ir == 8'b00001011;
  /* T80_MCode.vhd:1562:66  */
  assign n4834_o = n4831_o | n4833_o;
  /* T80_MCode.vhd:1562:77  */
  assign n4836_o = ir == 8'b00001100;
  /* T80_MCode.vhd:1562:77  */
  assign n4837_o = n4834_o | n4836_o;
  /* T80_MCode.vhd:1562:88  */
  assign n4839_o = ir == 8'b00001101;
  /* T80_MCode.vhd:1562:88  */
  assign n4840_o = n4837_o | n4839_o;
  /* T80_MCode.vhd:1562:99  */
  assign n4842_o = ir == 8'b00001111;
  /* T80_MCode.vhd:1562:99  */
  assign n4843_o = n4840_o | n4842_o;
  /* T80_MCode.vhd:1563:33  */
  assign n4845_o = ir == 8'b00011000;
  /* T80_MCode.vhd:1563:33  */
  assign n4846_o = n4843_o | n4845_o;
  /* T80_MCode.vhd:1563:44  */
  assign n4848_o = ir == 8'b00011001;
  /* T80_MCode.vhd:1563:44  */
  assign n4849_o = n4846_o | n4848_o;
  /* T80_MCode.vhd:1563:55  */
  assign n4851_o = ir == 8'b00011010;
  /* T80_MCode.vhd:1563:55  */
  assign n4852_o = n4849_o | n4851_o;
  /* T80_MCode.vhd:1563:66  */
  assign n4854_o = ir == 8'b00011011;
  /* T80_MCode.vhd:1563:66  */
  assign n4855_o = n4852_o | n4854_o;
  /* T80_MCode.vhd:1563:77  */
  assign n4857_o = ir == 8'b00011100;
  /* T80_MCode.vhd:1563:77  */
  assign n4858_o = n4855_o | n4857_o;
  /* T80_MCode.vhd:1563:88  */
  assign n4860_o = ir == 8'b00011101;
  /* T80_MCode.vhd:1563:88  */
  assign n4861_o = n4858_o | n4860_o;
  /* T80_MCode.vhd:1563:99  */
  assign n4863_o = ir == 8'b00011111;
  /* T80_MCode.vhd:1563:99  */
  assign n4864_o = n4861_o | n4863_o;
  /* T80_MCode.vhd:1564:33  */
  assign n4866_o = ir == 8'b00100000;
  /* T80_MCode.vhd:1564:33  */
  assign n4867_o = n4864_o | n4866_o;
  /* T80_MCode.vhd:1564:44  */
  assign n4869_o = ir == 8'b00100001;
  /* T80_MCode.vhd:1564:44  */
  assign n4870_o = n4867_o | n4869_o;
  /* T80_MCode.vhd:1564:55  */
  assign n4872_o = ir == 8'b00100010;
  /* T80_MCode.vhd:1564:55  */
  assign n4873_o = n4870_o | n4872_o;
  /* T80_MCode.vhd:1564:66  */
  assign n4875_o = ir == 8'b00100011;
  /* T80_MCode.vhd:1564:66  */
  assign n4876_o = n4873_o | n4875_o;
  /* T80_MCode.vhd:1564:77  */
  assign n4878_o = ir == 8'b00100100;
  /* T80_MCode.vhd:1564:77  */
  assign n4879_o = n4876_o | n4878_o;
  /* T80_MCode.vhd:1564:88  */
  assign n4881_o = ir == 8'b00100101;
  /* T80_MCode.vhd:1564:88  */
  assign n4882_o = n4879_o | n4881_o;
  /* T80_MCode.vhd:1564:99  */
  assign n4884_o = ir == 8'b00100111;
  /* T80_MCode.vhd:1564:99  */
  assign n4885_o = n4882_o | n4884_o;
  /* T80_MCode.vhd:1565:33  */
  assign n4887_o = ir == 8'b00101000;
  /* T80_MCode.vhd:1565:33  */
  assign n4888_o = n4885_o | n4887_o;
  /* T80_MCode.vhd:1565:44  */
  assign n4890_o = ir == 8'b00101001;
  /* T80_MCode.vhd:1565:44  */
  assign n4891_o = n4888_o | n4890_o;
  /* T80_MCode.vhd:1565:55  */
  assign n4893_o = ir == 8'b00101010;
  /* T80_MCode.vhd:1565:55  */
  assign n4894_o = n4891_o | n4893_o;
  /* T80_MCode.vhd:1565:66  */
  assign n4896_o = ir == 8'b00101011;
  /* T80_MCode.vhd:1565:66  */
  assign n4897_o = n4894_o | n4896_o;
  /* T80_MCode.vhd:1565:77  */
  assign n4899_o = ir == 8'b00101100;
  /* T80_MCode.vhd:1565:77  */
  assign n4900_o = n4897_o | n4899_o;
  /* T80_MCode.vhd:1565:88  */
  assign n4902_o = ir == 8'b00101101;
  /* T80_MCode.vhd:1565:88  */
  assign n4903_o = n4900_o | n4902_o;
  /* T80_MCode.vhd:1565:99  */
  assign n4905_o = ir == 8'b00101111;
  /* T80_MCode.vhd:1565:99  */
  assign n4906_o = n4903_o | n4905_o;
  /* T80_MCode.vhd:1566:33  */
  assign n4908_o = ir == 8'b00110000;
  /* T80_MCode.vhd:1566:33  */
  assign n4909_o = n4906_o | n4908_o;
  /* T80_MCode.vhd:1566:44  */
  assign n4911_o = ir == 8'b00110001;
  /* T80_MCode.vhd:1566:44  */
  assign n4912_o = n4909_o | n4911_o;
  /* T80_MCode.vhd:1566:55  */
  assign n4914_o = ir == 8'b00110010;
  /* T80_MCode.vhd:1566:55  */
  assign n4915_o = n4912_o | n4914_o;
  /* T80_MCode.vhd:1566:66  */
  assign n4917_o = ir == 8'b00110011;
  /* T80_MCode.vhd:1566:66  */
  assign n4918_o = n4915_o | n4917_o;
  /* T80_MCode.vhd:1566:77  */
  assign n4920_o = ir == 8'b00110100;
  /* T80_MCode.vhd:1566:77  */
  assign n4921_o = n4918_o | n4920_o;
  /* T80_MCode.vhd:1566:88  */
  assign n4923_o = ir == 8'b00110101;
  /* T80_MCode.vhd:1566:88  */
  assign n4924_o = n4921_o | n4923_o;
  /* T80_MCode.vhd:1566:99  */
  assign n4926_o = ir == 8'b00110111;
  /* T80_MCode.vhd:1566:99  */
  assign n4927_o = n4924_o | n4926_o;
  /* T80_MCode.vhd:1567:33  */
  assign n4929_o = ir == 8'b00111000;
  /* T80_MCode.vhd:1567:33  */
  assign n4930_o = n4927_o | n4929_o;
  /* T80_MCode.vhd:1567:44  */
  assign n4932_o = ir == 8'b00111001;
  /* T80_MCode.vhd:1567:44  */
  assign n4933_o = n4930_o | n4932_o;
  /* T80_MCode.vhd:1567:55  */
  assign n4935_o = ir == 8'b00111010;
  /* T80_MCode.vhd:1567:55  */
  assign n4936_o = n4933_o | n4935_o;
  /* T80_MCode.vhd:1567:66  */
  assign n4938_o = ir == 8'b00111011;
  /* T80_MCode.vhd:1567:66  */
  assign n4939_o = n4936_o | n4938_o;
  /* T80_MCode.vhd:1567:77  */
  assign n4941_o = ir == 8'b00111100;
  /* T80_MCode.vhd:1567:77  */
  assign n4942_o = n4939_o | n4941_o;
  /* T80_MCode.vhd:1567:88  */
  assign n4944_o = ir == 8'b00111101;
  /* T80_MCode.vhd:1567:88  */
  assign n4945_o = n4942_o | n4944_o;
  /* T80_MCode.vhd:1567:99  */
  assign n4947_o = ir == 8'b00111111;
  /* T80_MCode.vhd:1567:99  */
  assign n4948_o = n4945_o | n4947_o;
  /* T80_MCode.vhd:1611:38  */
  assign n4949_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:1612:33  */
  assign n4951_o = n4949_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:1612:40  */
  assign n4953_o = n4949_o == 31'b0000000000000000000000000000111;
  /* T80_MCode.vhd:1612:40  */
  assign n4954_o = n4951_o | n4953_o;
  /* T80_MCode.vhd:1614:33  */
  assign n4956_o = n4949_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:1620:33  */
  assign n4958_o = n4949_o == 31'b0000000000000000000000000000011;
  assign n4959_o = {n4958_o, n4956_o, n4954_o};
  /* T80_MCode.vhd:1611:33  */
  always @*
    case (n4959_o)
      3'b100: n4961_o = n2497_o;
      3'b010: n4961_o = 3'b100;
      3'b001: n4961_o = n2497_o;
      default: n4961_o = n2497_o;
    endcase
  /* T80_MCode.vhd:1611:33  */
  always @*
    case (n4959_o)
      3'b100: n4964_o = 1'b0;
      3'b010: n4964_o = 1'b1;
      3'b001: n4964_o = 1'b0;
      default: n4964_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1611:33  */
  always @*
    case (n4959_o)
      3'b100: n4966_o = n2500_o;
      3'b010: n4966_o = 4'b1000;
      3'b001: n4966_o = n2500_o;
      default: n4966_o = n2500_o;
    endcase
  /* T80_MCode.vhd:1611:33  */
  always @*
    case (n4959_o)
      3'b100: n4969_o = 1'b0;
      3'b010: n4969_o = 1'b1;
      3'b001: n4969_o = 1'b0;
      default: n4969_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1611:33  */
  always @*
    case (n4959_o)
      3'b100: n4973_o = 3'b111;
      3'b010: n4973_o = 3'b010;
      3'b001: n4973_o = 3'b010;
      default: n4973_o = 3'b111;
    endcase
  /* T80_MCode.vhd:1611:33  */
  always @*
    case (n4959_o)
      3'b100: n4976_o = 1'b1;
      3'b010: n4976_o = 1'b0;
      3'b001: n4976_o = 1'b0;
      default: n4976_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1601:25  */
  assign n4978_o = ir == 8'b00000110;
  /* T80_MCode.vhd:1601:40  */
  assign n4980_o = ir == 8'b00010110;
  /* T80_MCode.vhd:1601:40  */
  assign n4981_o = n4978_o | n4980_o;
  /* T80_MCode.vhd:1601:51  */
  assign n4983_o = ir == 8'b00001110;
  /* T80_MCode.vhd:1601:51  */
  assign n4984_o = n4981_o | n4983_o;
  /* T80_MCode.vhd:1601:62  */
  assign n4986_o = ir == 8'b00011110;
  /* T80_MCode.vhd:1601:62  */
  assign n4987_o = n4984_o | n4986_o;
  /* T80_MCode.vhd:1601:73  */
  assign n4989_o = ir == 8'b00101110;
  /* T80_MCode.vhd:1601:73  */
  assign n4990_o = n4987_o | n4989_o;
  /* T80_MCode.vhd:1601:84  */
  assign n4992_o = ir == 8'b00111110;
  /* T80_MCode.vhd:1601:84  */
  assign n4993_o = n4990_o | n4992_o;
  /* T80_MCode.vhd:1601:95  */
  assign n4995_o = ir == 8'b00100110;
  /* T80_MCode.vhd:1601:95  */
  assign n4996_o = n4993_o | n4995_o;
  /* T80_MCode.vhd:1601:106  */
  assign n4998_o = ir == 8'b00110110;
  /* T80_MCode.vhd:1601:106  */
  assign n4999_o = n4996_o | n4998_o;
  /* T80_MCode.vhd:1633:44  */
  assign n5001_o = xy_state == 2'b00;
  /* T80_MCode.vhd:1634:51  */
  assign n5003_o = mcycle == 3'b001;
  /* T80_MCode.vhd:1635:72  */
  assign n5004_o = ir[2:0]; // extract
  /* T80_MCode.vhd:1633:33  */
  assign n5005_o = n5028_o ? n5004_o : n4727_o;
  /* T80_MCode.vhd:1634:41  */
  assign n5007_o = n5003_o ? 4'b1001 : n2500_o;
  /* T80_MCode.vhd:1642:46  */
  assign n5008_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:1643:41  */
  assign n5010_o = n5008_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:1643:48  */
  assign n5012_o = n5008_o == 31'b0000000000000000000000000000111;
  /* T80_MCode.vhd:1643:48  */
  assign n5013_o = n5010_o | n5012_o;
  /* T80_MCode.vhd:1645:41  */
  assign n5015_o = n5008_o == 31'b0000000000000000000000000000010;
  assign n5016_o = {n5015_o, n5013_o};
  /* T80_MCode.vhd:1642:41  */
  always @*
    case (n5016_o)
      2'b10: n5018_o = 3'b100;
      2'b01: n5018_o = n2497_o;
      default: n5018_o = n2497_o;
    endcase
  /* T80_MCode.vhd:1642:41  */
  always @*
    case (n5016_o)
      2'b10: n5020_o = 4'b1001;
      2'b01: n5020_o = n2500_o;
      default: n5020_o = n2500_o;
    endcase
  /* T80_MCode.vhd:1642:41  */
  always @*
    case (n5016_o)
      2'b10: n5023_o = 3'b111;
      2'b01: n5023_o = 3'b010;
      default: n5023_o = 3'b111;
    endcase
  /* T80_MCode.vhd:1633:33  */
  assign n5026_o = n5001_o ? 3'b001 : 3'b010;
  /* T80_MCode.vhd:1633:33  */
  assign n5027_o = n5001_o ? n2497_o : n5018_o;
  /* T80_MCode.vhd:1633:33  */
  assign n5028_o = n5003_o & n5001_o;
  /* T80_MCode.vhd:1633:33  */
  assign n5029_o = n5001_o ? n5007_o : n5020_o;
  /* T80_MCode.vhd:1633:33  */
  assign n5031_o = n5001_o ? 3'b111 : n5023_o;
  /* T80_MCode.vhd:1633:33  */
  assign n5034_o = n5001_o ? 1'b0 : 1'b1;
  /* T80_MCode.vhd:1624:25  */
  assign n5036_o = ir == 8'b01000000;
  /* T80_MCode.vhd:1624:40  */
  assign n5038_o = ir == 8'b01000001;
  /* T80_MCode.vhd:1624:40  */
  assign n5039_o = n5036_o | n5038_o;
  /* T80_MCode.vhd:1624:51  */
  assign n5041_o = ir == 8'b01000010;
  /* T80_MCode.vhd:1624:51  */
  assign n5042_o = n5039_o | n5041_o;
  /* T80_MCode.vhd:1624:62  */
  assign n5044_o = ir == 8'b01000011;
  /* T80_MCode.vhd:1624:62  */
  assign n5045_o = n5042_o | n5044_o;
  /* T80_MCode.vhd:1624:73  */
  assign n5047_o = ir == 8'b01000100;
  /* T80_MCode.vhd:1624:73  */
  assign n5048_o = n5045_o | n5047_o;
  /* T80_MCode.vhd:1624:84  */
  assign n5050_o = ir == 8'b01000101;
  /* T80_MCode.vhd:1624:84  */
  assign n5051_o = n5048_o | n5050_o;
  /* T80_MCode.vhd:1624:95  */
  assign n5053_o = ir == 8'b01000111;
  /* T80_MCode.vhd:1624:95  */
  assign n5054_o = n5051_o | n5053_o;
  /* T80_MCode.vhd:1625:33  */
  assign n5056_o = ir == 8'b01001000;
  /* T80_MCode.vhd:1625:33  */
  assign n5057_o = n5054_o | n5056_o;
  /* T80_MCode.vhd:1625:44  */
  assign n5059_o = ir == 8'b01001001;
  /* T80_MCode.vhd:1625:44  */
  assign n5060_o = n5057_o | n5059_o;
  /* T80_MCode.vhd:1625:55  */
  assign n5062_o = ir == 8'b01001010;
  /* T80_MCode.vhd:1625:55  */
  assign n5063_o = n5060_o | n5062_o;
  /* T80_MCode.vhd:1625:66  */
  assign n5065_o = ir == 8'b01001011;
  /* T80_MCode.vhd:1625:66  */
  assign n5066_o = n5063_o | n5065_o;
  /* T80_MCode.vhd:1625:77  */
  assign n5068_o = ir == 8'b01001100;
  /* T80_MCode.vhd:1625:77  */
  assign n5069_o = n5066_o | n5068_o;
  /* T80_MCode.vhd:1625:88  */
  assign n5071_o = ir == 8'b01001101;
  /* T80_MCode.vhd:1625:88  */
  assign n5072_o = n5069_o | n5071_o;
  /* T80_MCode.vhd:1625:99  */
  assign n5074_o = ir == 8'b01001111;
  /* T80_MCode.vhd:1625:99  */
  assign n5075_o = n5072_o | n5074_o;
  /* T80_MCode.vhd:1626:33  */
  assign n5077_o = ir == 8'b01010000;
  /* T80_MCode.vhd:1626:33  */
  assign n5078_o = n5075_o | n5077_o;
  /* T80_MCode.vhd:1626:44  */
  assign n5080_o = ir == 8'b01010001;
  /* T80_MCode.vhd:1626:44  */
  assign n5081_o = n5078_o | n5080_o;
  /* T80_MCode.vhd:1626:55  */
  assign n5083_o = ir == 8'b01010010;
  /* T80_MCode.vhd:1626:55  */
  assign n5084_o = n5081_o | n5083_o;
  /* T80_MCode.vhd:1626:66  */
  assign n5086_o = ir == 8'b01010011;
  /* T80_MCode.vhd:1626:66  */
  assign n5087_o = n5084_o | n5086_o;
  /* T80_MCode.vhd:1626:77  */
  assign n5089_o = ir == 8'b01010100;
  /* T80_MCode.vhd:1626:77  */
  assign n5090_o = n5087_o | n5089_o;
  /* T80_MCode.vhd:1626:88  */
  assign n5092_o = ir == 8'b01010101;
  /* T80_MCode.vhd:1626:88  */
  assign n5093_o = n5090_o | n5092_o;
  /* T80_MCode.vhd:1626:99  */
  assign n5095_o = ir == 8'b01010111;
  /* T80_MCode.vhd:1626:99  */
  assign n5096_o = n5093_o | n5095_o;
  /* T80_MCode.vhd:1627:33  */
  assign n5098_o = ir == 8'b01011000;
  /* T80_MCode.vhd:1627:33  */
  assign n5099_o = n5096_o | n5098_o;
  /* T80_MCode.vhd:1627:44  */
  assign n5101_o = ir == 8'b01011001;
  /* T80_MCode.vhd:1627:44  */
  assign n5102_o = n5099_o | n5101_o;
  /* T80_MCode.vhd:1627:55  */
  assign n5104_o = ir == 8'b01011010;
  /* T80_MCode.vhd:1627:55  */
  assign n5105_o = n5102_o | n5104_o;
  /* T80_MCode.vhd:1627:66  */
  assign n5107_o = ir == 8'b01011011;
  /* T80_MCode.vhd:1627:66  */
  assign n5108_o = n5105_o | n5107_o;
  /* T80_MCode.vhd:1627:77  */
  assign n5110_o = ir == 8'b01011100;
  /* T80_MCode.vhd:1627:77  */
  assign n5111_o = n5108_o | n5110_o;
  /* T80_MCode.vhd:1627:88  */
  assign n5113_o = ir == 8'b01011101;
  /* T80_MCode.vhd:1627:88  */
  assign n5114_o = n5111_o | n5113_o;
  /* T80_MCode.vhd:1627:99  */
  assign n5116_o = ir == 8'b01011111;
  /* T80_MCode.vhd:1627:99  */
  assign n5117_o = n5114_o | n5116_o;
  /* T80_MCode.vhd:1628:33  */
  assign n5119_o = ir == 8'b01100000;
  /* T80_MCode.vhd:1628:33  */
  assign n5120_o = n5117_o | n5119_o;
  /* T80_MCode.vhd:1628:44  */
  assign n5122_o = ir == 8'b01100001;
  /* T80_MCode.vhd:1628:44  */
  assign n5123_o = n5120_o | n5122_o;
  /* T80_MCode.vhd:1628:55  */
  assign n5125_o = ir == 8'b01100010;
  /* T80_MCode.vhd:1628:55  */
  assign n5126_o = n5123_o | n5125_o;
  /* T80_MCode.vhd:1628:66  */
  assign n5128_o = ir == 8'b01100011;
  /* T80_MCode.vhd:1628:66  */
  assign n5129_o = n5126_o | n5128_o;
  /* T80_MCode.vhd:1628:77  */
  assign n5131_o = ir == 8'b01100100;
  /* T80_MCode.vhd:1628:77  */
  assign n5132_o = n5129_o | n5131_o;
  /* T80_MCode.vhd:1628:88  */
  assign n5134_o = ir == 8'b01100101;
  /* T80_MCode.vhd:1628:88  */
  assign n5135_o = n5132_o | n5134_o;
  /* T80_MCode.vhd:1628:99  */
  assign n5137_o = ir == 8'b01100111;
  /* T80_MCode.vhd:1628:99  */
  assign n5138_o = n5135_o | n5137_o;
  /* T80_MCode.vhd:1629:33  */
  assign n5140_o = ir == 8'b01101000;
  /* T80_MCode.vhd:1629:33  */
  assign n5141_o = n5138_o | n5140_o;
  /* T80_MCode.vhd:1629:44  */
  assign n5143_o = ir == 8'b01101001;
  /* T80_MCode.vhd:1629:44  */
  assign n5144_o = n5141_o | n5143_o;
  /* T80_MCode.vhd:1629:55  */
  assign n5146_o = ir == 8'b01101010;
  /* T80_MCode.vhd:1629:55  */
  assign n5147_o = n5144_o | n5146_o;
  /* T80_MCode.vhd:1629:66  */
  assign n5149_o = ir == 8'b01101011;
  /* T80_MCode.vhd:1629:66  */
  assign n5150_o = n5147_o | n5149_o;
  /* T80_MCode.vhd:1629:77  */
  assign n5152_o = ir == 8'b01101100;
  /* T80_MCode.vhd:1629:77  */
  assign n5153_o = n5150_o | n5152_o;
  /* T80_MCode.vhd:1629:88  */
  assign n5155_o = ir == 8'b01101101;
  /* T80_MCode.vhd:1629:88  */
  assign n5156_o = n5153_o | n5155_o;
  /* T80_MCode.vhd:1629:99  */
  assign n5158_o = ir == 8'b01101111;
  /* T80_MCode.vhd:1629:99  */
  assign n5159_o = n5156_o | n5158_o;
  /* T80_MCode.vhd:1630:33  */
  assign n5161_o = ir == 8'b01110000;
  /* T80_MCode.vhd:1630:33  */
  assign n5162_o = n5159_o | n5161_o;
  /* T80_MCode.vhd:1630:44  */
  assign n5164_o = ir == 8'b01110001;
  /* T80_MCode.vhd:1630:44  */
  assign n5165_o = n5162_o | n5164_o;
  /* T80_MCode.vhd:1630:55  */
  assign n5167_o = ir == 8'b01110010;
  /* T80_MCode.vhd:1630:55  */
  assign n5168_o = n5165_o | n5167_o;
  /* T80_MCode.vhd:1630:66  */
  assign n5170_o = ir == 8'b01110011;
  /* T80_MCode.vhd:1630:66  */
  assign n5171_o = n5168_o | n5170_o;
  /* T80_MCode.vhd:1630:77  */
  assign n5173_o = ir == 8'b01110100;
  /* T80_MCode.vhd:1630:77  */
  assign n5174_o = n5171_o | n5173_o;
  /* T80_MCode.vhd:1630:88  */
  assign n5176_o = ir == 8'b01110101;
  /* T80_MCode.vhd:1630:88  */
  assign n5177_o = n5174_o | n5176_o;
  /* T80_MCode.vhd:1630:99  */
  assign n5179_o = ir == 8'b01110111;
  /* T80_MCode.vhd:1630:99  */
  assign n5180_o = n5177_o | n5179_o;
  /* T80_MCode.vhd:1631:33  */
  assign n5182_o = ir == 8'b01111000;
  /* T80_MCode.vhd:1631:33  */
  assign n5183_o = n5180_o | n5182_o;
  /* T80_MCode.vhd:1631:44  */
  assign n5185_o = ir == 8'b01111001;
  /* T80_MCode.vhd:1631:44  */
  assign n5186_o = n5183_o | n5185_o;
  /* T80_MCode.vhd:1631:55  */
  assign n5188_o = ir == 8'b01111010;
  /* T80_MCode.vhd:1631:55  */
  assign n5189_o = n5186_o | n5188_o;
  /* T80_MCode.vhd:1631:66  */
  assign n5191_o = ir == 8'b01111011;
  /* T80_MCode.vhd:1631:66  */
  assign n5192_o = n5189_o | n5191_o;
  /* T80_MCode.vhd:1631:77  */
  assign n5194_o = ir == 8'b01111100;
  /* T80_MCode.vhd:1631:77  */
  assign n5195_o = n5192_o | n5194_o;
  /* T80_MCode.vhd:1631:88  */
  assign n5197_o = ir == 8'b01111101;
  /* T80_MCode.vhd:1631:88  */
  assign n5198_o = n5195_o | n5197_o;
  /* T80_MCode.vhd:1631:99  */
  assign n5200_o = ir == 8'b01111111;
  /* T80_MCode.vhd:1631:99  */
  assign n5201_o = n5198_o | n5200_o;
  /* T80_MCode.vhd:1655:38  */
  assign n5202_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:1656:33  */
  assign n5204_o = n5202_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:1656:40  */
  assign n5206_o = n5202_o == 31'b0000000000000000000000000000111;
  /* T80_MCode.vhd:1656:40  */
  assign n5207_o = n5204_o | n5206_o;
  /* T80_MCode.vhd:1658:33  */
  assign n5209_o = n5202_o == 31'b0000000000000000000000000000010;
  assign n5210_o = {n5209_o, n5207_o};
  /* T80_MCode.vhd:1655:33  */
  always @*
    case (n5210_o)
      2'b10: n5212_o = 3'b100;
      2'b01: n5212_o = n2497_o;
      default: n5212_o = n2497_o;
    endcase
  /* T80_MCode.vhd:1655:33  */
  always @*
    case (n5210_o)
      2'b10: n5214_o = 4'b1001;
      2'b01: n5214_o = n2500_o;
      default: n5214_o = n2500_o;
    endcase
  /* T80_MCode.vhd:1655:33  */
  always @*
    case (n5210_o)
      2'b10: n5217_o = 3'b111;
      2'b01: n5217_o = 3'b010;
      default: n5217_o = 3'b111;
    endcase
  /* T80_MCode.vhd:1652:25  */
  assign n5219_o = ir == 8'b01000110;
  /* T80_MCode.vhd:1652:40  */
  assign n5221_o = ir == 8'b01001110;
  /* T80_MCode.vhd:1652:40  */
  assign n5222_o = n5219_o | n5221_o;
  /* T80_MCode.vhd:1652:51  */
  assign n5224_o = ir == 8'b01010110;
  /* T80_MCode.vhd:1652:51  */
  assign n5225_o = n5222_o | n5224_o;
  /* T80_MCode.vhd:1652:62  */
  assign n5227_o = ir == 8'b01011110;
  /* T80_MCode.vhd:1652:62  */
  assign n5228_o = n5225_o | n5227_o;
  /* T80_MCode.vhd:1652:73  */
  assign n5230_o = ir == 8'b01100110;
  /* T80_MCode.vhd:1652:73  */
  assign n5231_o = n5228_o | n5230_o;
  /* T80_MCode.vhd:1652:84  */
  assign n5233_o = ir == 8'b01101110;
  /* T80_MCode.vhd:1652:84  */
  assign n5234_o = n5231_o | n5233_o;
  /* T80_MCode.vhd:1652:95  */
  assign n5236_o = ir == 8'b01110110;
  /* T80_MCode.vhd:1652:95  */
  assign n5237_o = n5234_o | n5236_o;
  /* T80_MCode.vhd:1652:106  */
  assign n5239_o = ir == 8'b01111110;
  /* T80_MCode.vhd:1652:106  */
  assign n5240_o = n5237_o | n5239_o;
  /* T80_MCode.vhd:1672:44  */
  assign n5242_o = xy_state == 2'b00;
  /* T80_MCode.vhd:1673:51  */
  assign n5244_o = mcycle == 3'b001;
  /* T80_MCode.vhd:1673:41  */
  assign n5247_o = n5244_o ? 1'b1 : 1'b0;
  /* T80_MCode.vhd:1673:41  */
  assign n5249_o = n5244_o ? 4'b1010 : n2500_o;
  /* T80_MCode.vhd:1673:41  */
  assign n5252_o = n5244_o ? 1'b1 : 1'b0;
  /* T80_MCode.vhd:1682:46  */
  assign n5253_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:1683:41  */
  assign n5255_o = n5253_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:1683:48  */
  assign n5257_o = n5253_o == 31'b0000000000000000000000000000111;
  /* T80_MCode.vhd:1683:48  */
  assign n5258_o = n5255_o | n5257_o;
  /* T80_MCode.vhd:1685:41  */
  assign n5260_o = n5253_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:1691:41  */
  assign n5262_o = n5253_o == 31'b0000000000000000000000000000011;
  assign n5263_o = {n5262_o, n5260_o, n5258_o};
  /* T80_MCode.vhd:1682:41  */
  always @*
    case (n5263_o)
      3'b100: n5265_o = n2497_o;
      3'b010: n5265_o = 3'b100;
      3'b001: n5265_o = n2497_o;
      default: n5265_o = n2497_o;
    endcase
  /* T80_MCode.vhd:1682:41  */
  always @*
    case (n5263_o)
      3'b100: n5268_o = 1'b0;
      3'b010: n5268_o = 1'b1;
      3'b001: n5268_o = 1'b0;
      default: n5268_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1682:41  */
  always @*
    case (n5263_o)
      3'b100: n5270_o = n2500_o;
      3'b010: n5270_o = 4'b1010;
      3'b001: n5270_o = n2500_o;
      default: n5270_o = n2500_o;
    endcase
  /* T80_MCode.vhd:1682:41  */
  always @*
    case (n5263_o)
      3'b100: n5273_o = 1'b0;
      3'b010: n5273_o = 1'b1;
      3'b001: n5273_o = 1'b0;
      default: n5273_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1682:41  */
  always @*
    case (n5263_o)
      3'b100: n5277_o = 3'b111;
      3'b010: n5277_o = 3'b010;
      3'b001: n5277_o = 3'b010;
      default: n5277_o = 3'b111;
    endcase
  /* T80_MCode.vhd:1682:41  */
  always @*
    case (n5263_o)
      3'b100: n5280_o = 1'b1;
      3'b010: n5280_o = 1'b0;
      3'b001: n5280_o = 1'b0;
      default: n5280_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1672:33  */
  assign n5283_o = n5242_o ? 3'b001 : 3'b011;
  /* T80_MCode.vhd:1672:33  */
  assign n5284_o = n5242_o ? n2497_o : n5265_o;
  /* T80_MCode.vhd:1672:33  */
  assign n5285_o = n5242_o ? n5247_o : n5268_o;
  /* T80_MCode.vhd:1672:33  */
  assign n5286_o = n5242_o ? n5249_o : n5270_o;
  /* T80_MCode.vhd:1672:33  */
  assign n5287_o = n5242_o ? n5252_o : n5273_o;
  /* T80_MCode.vhd:1672:33  */
  assign n5289_o = n5242_o ? 3'b111 : n5277_o;
  /* T80_MCode.vhd:1672:33  */
  assign n5291_o = n5242_o ? 1'b0 : n5280_o;
  /* T80_MCode.vhd:1672:33  */
  assign n5294_o = n5242_o ? 1'b0 : 1'b1;
  /* T80_MCode.vhd:1663:25  */
  assign n5296_o = ir == 8'b11000000;
  /* T80_MCode.vhd:1663:40  */
  assign n5298_o = ir == 8'b11000001;
  /* T80_MCode.vhd:1663:40  */
  assign n5299_o = n5296_o | n5298_o;
  /* T80_MCode.vhd:1663:51  */
  assign n5301_o = ir == 8'b11000010;
  /* T80_MCode.vhd:1663:51  */
  assign n5302_o = n5299_o | n5301_o;
  /* T80_MCode.vhd:1663:62  */
  assign n5304_o = ir == 8'b11000011;
  /* T80_MCode.vhd:1663:62  */
  assign n5305_o = n5302_o | n5304_o;
  /* T80_MCode.vhd:1663:73  */
  assign n5307_o = ir == 8'b11000100;
  /* T80_MCode.vhd:1663:73  */
  assign n5308_o = n5305_o | n5307_o;
  /* T80_MCode.vhd:1663:84  */
  assign n5310_o = ir == 8'b11000101;
  /* T80_MCode.vhd:1663:84  */
  assign n5311_o = n5308_o | n5310_o;
  /* T80_MCode.vhd:1663:95  */
  assign n5313_o = ir == 8'b11000111;
  /* T80_MCode.vhd:1663:95  */
  assign n5314_o = n5311_o | n5313_o;
  /* T80_MCode.vhd:1664:33  */
  assign n5316_o = ir == 8'b11001000;
  /* T80_MCode.vhd:1664:33  */
  assign n5317_o = n5314_o | n5316_o;
  /* T80_MCode.vhd:1664:44  */
  assign n5319_o = ir == 8'b11001001;
  /* T80_MCode.vhd:1664:44  */
  assign n5320_o = n5317_o | n5319_o;
  /* T80_MCode.vhd:1664:55  */
  assign n5322_o = ir == 8'b11001010;
  /* T80_MCode.vhd:1664:55  */
  assign n5323_o = n5320_o | n5322_o;
  /* T80_MCode.vhd:1664:66  */
  assign n5325_o = ir == 8'b11001011;
  /* T80_MCode.vhd:1664:66  */
  assign n5326_o = n5323_o | n5325_o;
  /* T80_MCode.vhd:1664:77  */
  assign n5328_o = ir == 8'b11001100;
  /* T80_MCode.vhd:1664:77  */
  assign n5329_o = n5326_o | n5328_o;
  /* T80_MCode.vhd:1664:88  */
  assign n5331_o = ir == 8'b11001101;
  /* T80_MCode.vhd:1664:88  */
  assign n5332_o = n5329_o | n5331_o;
  /* T80_MCode.vhd:1664:99  */
  assign n5334_o = ir == 8'b11001111;
  /* T80_MCode.vhd:1664:99  */
  assign n5335_o = n5332_o | n5334_o;
  /* T80_MCode.vhd:1665:33  */
  assign n5337_o = ir == 8'b11010000;
  /* T80_MCode.vhd:1665:33  */
  assign n5338_o = n5335_o | n5337_o;
  /* T80_MCode.vhd:1665:44  */
  assign n5340_o = ir == 8'b11010001;
  /* T80_MCode.vhd:1665:44  */
  assign n5341_o = n5338_o | n5340_o;
  /* T80_MCode.vhd:1665:55  */
  assign n5343_o = ir == 8'b11010010;
  /* T80_MCode.vhd:1665:55  */
  assign n5344_o = n5341_o | n5343_o;
  /* T80_MCode.vhd:1665:66  */
  assign n5346_o = ir == 8'b11010011;
  /* T80_MCode.vhd:1665:66  */
  assign n5347_o = n5344_o | n5346_o;
  /* T80_MCode.vhd:1665:77  */
  assign n5349_o = ir == 8'b11010100;
  /* T80_MCode.vhd:1665:77  */
  assign n5350_o = n5347_o | n5349_o;
  /* T80_MCode.vhd:1665:88  */
  assign n5352_o = ir == 8'b11010101;
  /* T80_MCode.vhd:1665:88  */
  assign n5353_o = n5350_o | n5352_o;
  /* T80_MCode.vhd:1665:99  */
  assign n5355_o = ir == 8'b11010111;
  /* T80_MCode.vhd:1665:99  */
  assign n5356_o = n5353_o | n5355_o;
  /* T80_MCode.vhd:1666:33  */
  assign n5358_o = ir == 8'b11011000;
  /* T80_MCode.vhd:1666:33  */
  assign n5359_o = n5356_o | n5358_o;
  /* T80_MCode.vhd:1666:44  */
  assign n5361_o = ir == 8'b11011001;
  /* T80_MCode.vhd:1666:44  */
  assign n5362_o = n5359_o | n5361_o;
  /* T80_MCode.vhd:1666:55  */
  assign n5364_o = ir == 8'b11011010;
  /* T80_MCode.vhd:1666:55  */
  assign n5365_o = n5362_o | n5364_o;
  /* T80_MCode.vhd:1666:66  */
  assign n5367_o = ir == 8'b11011011;
  /* T80_MCode.vhd:1666:66  */
  assign n5368_o = n5365_o | n5367_o;
  /* T80_MCode.vhd:1666:77  */
  assign n5370_o = ir == 8'b11011100;
  /* T80_MCode.vhd:1666:77  */
  assign n5371_o = n5368_o | n5370_o;
  /* T80_MCode.vhd:1666:88  */
  assign n5373_o = ir == 8'b11011101;
  /* T80_MCode.vhd:1666:88  */
  assign n5374_o = n5371_o | n5373_o;
  /* T80_MCode.vhd:1666:99  */
  assign n5376_o = ir == 8'b11011111;
  /* T80_MCode.vhd:1666:99  */
  assign n5377_o = n5374_o | n5376_o;
  /* T80_MCode.vhd:1667:33  */
  assign n5379_o = ir == 8'b11100000;
  /* T80_MCode.vhd:1667:33  */
  assign n5380_o = n5377_o | n5379_o;
  /* T80_MCode.vhd:1667:44  */
  assign n5382_o = ir == 8'b11100001;
  /* T80_MCode.vhd:1667:44  */
  assign n5383_o = n5380_o | n5382_o;
  /* T80_MCode.vhd:1667:55  */
  assign n5385_o = ir == 8'b11100010;
  /* T80_MCode.vhd:1667:55  */
  assign n5386_o = n5383_o | n5385_o;
  /* T80_MCode.vhd:1667:66  */
  assign n5388_o = ir == 8'b11100011;
  /* T80_MCode.vhd:1667:66  */
  assign n5389_o = n5386_o | n5388_o;
  /* T80_MCode.vhd:1667:77  */
  assign n5391_o = ir == 8'b11100100;
  /* T80_MCode.vhd:1667:77  */
  assign n5392_o = n5389_o | n5391_o;
  /* T80_MCode.vhd:1667:88  */
  assign n5394_o = ir == 8'b11100101;
  /* T80_MCode.vhd:1667:88  */
  assign n5395_o = n5392_o | n5394_o;
  /* T80_MCode.vhd:1667:99  */
  assign n5397_o = ir == 8'b11100111;
  /* T80_MCode.vhd:1667:99  */
  assign n5398_o = n5395_o | n5397_o;
  /* T80_MCode.vhd:1668:33  */
  assign n5400_o = ir == 8'b11101000;
  /* T80_MCode.vhd:1668:33  */
  assign n5401_o = n5398_o | n5400_o;
  /* T80_MCode.vhd:1668:44  */
  assign n5403_o = ir == 8'b11101001;
  /* T80_MCode.vhd:1668:44  */
  assign n5404_o = n5401_o | n5403_o;
  /* T80_MCode.vhd:1668:55  */
  assign n5406_o = ir == 8'b11101010;
  /* T80_MCode.vhd:1668:55  */
  assign n5407_o = n5404_o | n5406_o;
  /* T80_MCode.vhd:1668:66  */
  assign n5409_o = ir == 8'b11101011;
  /* T80_MCode.vhd:1668:66  */
  assign n5410_o = n5407_o | n5409_o;
  /* T80_MCode.vhd:1668:77  */
  assign n5412_o = ir == 8'b11101100;
  /* T80_MCode.vhd:1668:77  */
  assign n5413_o = n5410_o | n5412_o;
  /* T80_MCode.vhd:1668:88  */
  assign n5415_o = ir == 8'b11101101;
  /* T80_MCode.vhd:1668:88  */
  assign n5416_o = n5413_o | n5415_o;
  /* T80_MCode.vhd:1668:99  */
  assign n5418_o = ir == 8'b11101111;
  /* T80_MCode.vhd:1668:99  */
  assign n5419_o = n5416_o | n5418_o;
  /* T80_MCode.vhd:1669:33  */
  assign n5421_o = ir == 8'b11110000;
  /* T80_MCode.vhd:1669:33  */
  assign n5422_o = n5419_o | n5421_o;
  /* T80_MCode.vhd:1669:44  */
  assign n5424_o = ir == 8'b11110001;
  /* T80_MCode.vhd:1669:44  */
  assign n5425_o = n5422_o | n5424_o;
  /* T80_MCode.vhd:1669:55  */
  assign n5427_o = ir == 8'b11110010;
  /* T80_MCode.vhd:1669:55  */
  assign n5428_o = n5425_o | n5427_o;
  /* T80_MCode.vhd:1669:66  */
  assign n5430_o = ir == 8'b11110011;
  /* T80_MCode.vhd:1669:66  */
  assign n5431_o = n5428_o | n5430_o;
  /* T80_MCode.vhd:1669:77  */
  assign n5433_o = ir == 8'b11110100;
  /* T80_MCode.vhd:1669:77  */
  assign n5434_o = n5431_o | n5433_o;
  /* T80_MCode.vhd:1669:88  */
  assign n5436_o = ir == 8'b11110101;
  /* T80_MCode.vhd:1669:88  */
  assign n5437_o = n5434_o | n5436_o;
  /* T80_MCode.vhd:1669:99  */
  assign n5439_o = ir == 8'b11110111;
  /* T80_MCode.vhd:1669:99  */
  assign n5440_o = n5437_o | n5439_o;
  /* T80_MCode.vhd:1670:33  */
  assign n5442_o = ir == 8'b11111000;
  /* T80_MCode.vhd:1670:33  */
  assign n5443_o = n5440_o | n5442_o;
  /* T80_MCode.vhd:1670:44  */
  assign n5445_o = ir == 8'b11111001;
  /* T80_MCode.vhd:1670:44  */
  assign n5446_o = n5443_o | n5445_o;
  /* T80_MCode.vhd:1670:55  */
  assign n5448_o = ir == 8'b11111010;
  /* T80_MCode.vhd:1670:55  */
  assign n5449_o = n5446_o | n5448_o;
  /* T80_MCode.vhd:1670:66  */
  assign n5451_o = ir == 8'b11111011;
  /* T80_MCode.vhd:1670:66  */
  assign n5452_o = n5449_o | n5451_o;
  /* T80_MCode.vhd:1670:77  */
  assign n5454_o = ir == 8'b11111100;
  /* T80_MCode.vhd:1670:77  */
  assign n5455_o = n5452_o | n5454_o;
  /* T80_MCode.vhd:1670:88  */
  assign n5457_o = ir == 8'b11111101;
  /* T80_MCode.vhd:1670:88  */
  assign n5458_o = n5455_o | n5457_o;
  /* T80_MCode.vhd:1670:99  */
  assign n5460_o = ir == 8'b11111111;
  /* T80_MCode.vhd:1670:99  */
  assign n5461_o = n5458_o | n5460_o;
  /* T80_MCode.vhd:1700:38  */
  assign n5462_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:1701:33  */
  assign n5464_o = n5462_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:1701:40  */
  assign n5466_o = n5462_o == 31'b0000000000000000000000000000111;
  /* T80_MCode.vhd:1701:40  */
  assign n5467_o = n5464_o | n5466_o;
  /* T80_MCode.vhd:1703:33  */
  assign n5469_o = n5462_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:1709:33  */
  assign n5471_o = n5462_o == 31'b0000000000000000000000000000011;
  assign n5472_o = {n5471_o, n5469_o, n5467_o};
  /* T80_MCode.vhd:1700:33  */
  always @*
    case (n5472_o)
      3'b100: n5474_o = n2497_o;
      3'b010: n5474_o = 3'b100;
      3'b001: n5474_o = n2497_o;
      default: n5474_o = n2497_o;
    endcase
  /* T80_MCode.vhd:1700:33  */
  always @*
    case (n5472_o)
      3'b100: n5477_o = 1'b0;
      3'b010: n5477_o = 1'b1;
      3'b001: n5477_o = 1'b0;
      default: n5477_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1700:33  */
  always @*
    case (n5472_o)
      3'b100: n5479_o = n2500_o;
      3'b010: n5479_o = 4'b1010;
      3'b001: n5479_o = n2500_o;
      default: n5479_o = n2500_o;
    endcase
  /* T80_MCode.vhd:1700:33  */
  always @*
    case (n5472_o)
      3'b100: n5482_o = 1'b0;
      3'b010: n5482_o = 1'b1;
      3'b001: n5482_o = 1'b0;
      default: n5482_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1700:33  */
  always @*
    case (n5472_o)
      3'b100: n5486_o = 3'b111;
      3'b010: n5486_o = 3'b010;
      3'b001: n5486_o = 3'b010;
      default: n5486_o = 3'b111;
    endcase
  /* T80_MCode.vhd:1700:33  */
  always @*
    case (n5472_o)
      3'b100: n5489_o = 1'b1;
      3'b010: n5489_o = 1'b0;
      3'b001: n5489_o = 1'b0;
      default: n5489_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1697:25  */
  assign n5491_o = ir == 8'b11000110;
  /* T80_MCode.vhd:1697:40  */
  assign n5493_o = ir == 8'b11001110;
  /* T80_MCode.vhd:1697:40  */
  assign n5494_o = n5491_o | n5493_o;
  /* T80_MCode.vhd:1697:51  */
  assign n5496_o = ir == 8'b11010110;
  /* T80_MCode.vhd:1697:51  */
  assign n5497_o = n5494_o | n5496_o;
  /* T80_MCode.vhd:1697:62  */
  assign n5499_o = ir == 8'b11011110;
  /* T80_MCode.vhd:1697:62  */
  assign n5500_o = n5497_o | n5499_o;
  /* T80_MCode.vhd:1697:73  */
  assign n5502_o = ir == 8'b11100110;
  /* T80_MCode.vhd:1697:73  */
  assign n5503_o = n5500_o | n5502_o;
  /* T80_MCode.vhd:1697:84  */
  assign n5505_o = ir == 8'b11101110;
  /* T80_MCode.vhd:1697:84  */
  assign n5506_o = n5503_o | n5505_o;
  /* T80_MCode.vhd:1697:95  */
  assign n5508_o = ir == 8'b11110110;
  /* T80_MCode.vhd:1697:95  */
  assign n5509_o = n5506_o | n5508_o;
  /* T80_MCode.vhd:1697:106  */
  assign n5511_o = ir == 8'b11111110;
  /* T80_MCode.vhd:1697:106  */
  assign n5512_o = n5509_o | n5511_o;
  /* T80_MCode.vhd:1722:44  */
  assign n5514_o = xy_state == 2'b00;
  /* T80_MCode.vhd:1723:51  */
  assign n5516_o = mcycle == 3'b001;
  /* T80_MCode.vhd:1723:41  */
  assign n5519_o = n5516_o ? 1'b1 : 1'b0;
  /* T80_MCode.vhd:1723:41  */
  assign n5521_o = n5516_o ? 4'b1011 : n2500_o;
  /* T80_MCode.vhd:1723:41  */
  assign n5524_o = n5516_o ? 1'b1 : 1'b0;
  /* T80_MCode.vhd:1732:46  */
  assign n5525_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:1733:41  */
  assign n5527_o = n5525_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:1733:48  */
  assign n5529_o = n5525_o == 31'b0000000000000000000000000000111;
  /* T80_MCode.vhd:1733:48  */
  assign n5530_o = n5527_o | n5529_o;
  /* T80_MCode.vhd:1735:41  */
  assign n5532_o = n5525_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:1741:41  */
  assign n5534_o = n5525_o == 31'b0000000000000000000000000000011;
  assign n5535_o = {n5534_o, n5532_o, n5530_o};
  /* T80_MCode.vhd:1732:41  */
  always @*
    case (n5535_o)
      3'b100: n5537_o = n2497_o;
      3'b010: n5537_o = 3'b100;
      3'b001: n5537_o = n2497_o;
      default: n5537_o = n2497_o;
    endcase
  /* T80_MCode.vhd:1732:41  */
  always @*
    case (n5535_o)
      3'b100: n5540_o = 1'b0;
      3'b010: n5540_o = 1'b1;
      3'b001: n5540_o = 1'b0;
      default: n5540_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1732:41  */
  always @*
    case (n5535_o)
      3'b100: n5542_o = n2500_o;
      3'b010: n5542_o = 4'b1011;
      3'b001: n5542_o = n2500_o;
      default: n5542_o = n2500_o;
    endcase
  /* T80_MCode.vhd:1732:41  */
  always @*
    case (n5535_o)
      3'b100: n5545_o = 1'b0;
      3'b010: n5545_o = 1'b1;
      3'b001: n5545_o = 1'b0;
      default: n5545_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1732:41  */
  always @*
    case (n5535_o)
      3'b100: n5549_o = 3'b111;
      3'b010: n5549_o = 3'b010;
      3'b001: n5549_o = 3'b010;
      default: n5549_o = 3'b111;
    endcase
  /* T80_MCode.vhd:1732:41  */
  always @*
    case (n5535_o)
      3'b100: n5552_o = 1'b1;
      3'b010: n5552_o = 1'b0;
      3'b001: n5552_o = 1'b0;
      default: n5552_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1722:33  */
  assign n5555_o = n5514_o ? 3'b001 : 3'b011;
  /* T80_MCode.vhd:1722:33  */
  assign n5556_o = n5514_o ? n2497_o : n5537_o;
  /* T80_MCode.vhd:1722:33  */
  assign n5557_o = n5514_o ? n5519_o : n5540_o;
  /* T80_MCode.vhd:1722:33  */
  assign n5558_o = n5514_o ? n5521_o : n5542_o;
  /* T80_MCode.vhd:1722:33  */
  assign n5559_o = n5514_o ? n5524_o : n5545_o;
  /* T80_MCode.vhd:1722:33  */
  assign n5561_o = n5514_o ? 3'b111 : n5549_o;
  /* T80_MCode.vhd:1722:33  */
  assign n5563_o = n5514_o ? 1'b0 : n5552_o;
  /* T80_MCode.vhd:1722:33  */
  assign n5566_o = n5514_o ? 1'b0 : 1'b1;
  /* T80_MCode.vhd:1713:25  */
  assign n5568_o = ir == 8'b10000000;
  /* T80_MCode.vhd:1713:40  */
  assign n5570_o = ir == 8'b10000001;
  /* T80_MCode.vhd:1713:40  */
  assign n5571_o = n5568_o | n5570_o;
  /* T80_MCode.vhd:1713:51  */
  assign n5573_o = ir == 8'b10000010;
  /* T80_MCode.vhd:1713:51  */
  assign n5574_o = n5571_o | n5573_o;
  /* T80_MCode.vhd:1713:62  */
  assign n5576_o = ir == 8'b10000011;
  /* T80_MCode.vhd:1713:62  */
  assign n5577_o = n5574_o | n5576_o;
  /* T80_MCode.vhd:1713:73  */
  assign n5579_o = ir == 8'b10000100;
  /* T80_MCode.vhd:1713:73  */
  assign n5580_o = n5577_o | n5579_o;
  /* T80_MCode.vhd:1713:84  */
  assign n5582_o = ir == 8'b10000101;
  /* T80_MCode.vhd:1713:84  */
  assign n5583_o = n5580_o | n5582_o;
  /* T80_MCode.vhd:1713:95  */
  assign n5585_o = ir == 8'b10000111;
  /* T80_MCode.vhd:1713:95  */
  assign n5586_o = n5583_o | n5585_o;
  /* T80_MCode.vhd:1714:33  */
  assign n5588_o = ir == 8'b10001000;
  /* T80_MCode.vhd:1714:33  */
  assign n5589_o = n5586_o | n5588_o;
  /* T80_MCode.vhd:1714:44  */
  assign n5591_o = ir == 8'b10001001;
  /* T80_MCode.vhd:1714:44  */
  assign n5592_o = n5589_o | n5591_o;
  /* T80_MCode.vhd:1714:55  */
  assign n5594_o = ir == 8'b10001010;
  /* T80_MCode.vhd:1714:55  */
  assign n5595_o = n5592_o | n5594_o;
  /* T80_MCode.vhd:1714:66  */
  assign n5597_o = ir == 8'b10001011;
  /* T80_MCode.vhd:1714:66  */
  assign n5598_o = n5595_o | n5597_o;
  /* T80_MCode.vhd:1714:77  */
  assign n5600_o = ir == 8'b10001100;
  /* T80_MCode.vhd:1714:77  */
  assign n5601_o = n5598_o | n5600_o;
  /* T80_MCode.vhd:1714:88  */
  assign n5603_o = ir == 8'b10001101;
  /* T80_MCode.vhd:1714:88  */
  assign n5604_o = n5601_o | n5603_o;
  /* T80_MCode.vhd:1714:99  */
  assign n5606_o = ir == 8'b10001111;
  /* T80_MCode.vhd:1714:99  */
  assign n5607_o = n5604_o | n5606_o;
  /* T80_MCode.vhd:1715:33  */
  assign n5609_o = ir == 8'b10010000;
  /* T80_MCode.vhd:1715:33  */
  assign n5610_o = n5607_o | n5609_o;
  /* T80_MCode.vhd:1715:44  */
  assign n5612_o = ir == 8'b10010001;
  /* T80_MCode.vhd:1715:44  */
  assign n5613_o = n5610_o | n5612_o;
  /* T80_MCode.vhd:1715:55  */
  assign n5615_o = ir == 8'b10010010;
  /* T80_MCode.vhd:1715:55  */
  assign n5616_o = n5613_o | n5615_o;
  /* T80_MCode.vhd:1715:66  */
  assign n5618_o = ir == 8'b10010011;
  /* T80_MCode.vhd:1715:66  */
  assign n5619_o = n5616_o | n5618_o;
  /* T80_MCode.vhd:1715:77  */
  assign n5621_o = ir == 8'b10010100;
  /* T80_MCode.vhd:1715:77  */
  assign n5622_o = n5619_o | n5621_o;
  /* T80_MCode.vhd:1715:88  */
  assign n5624_o = ir == 8'b10010101;
  /* T80_MCode.vhd:1715:88  */
  assign n5625_o = n5622_o | n5624_o;
  /* T80_MCode.vhd:1715:99  */
  assign n5627_o = ir == 8'b10010111;
  /* T80_MCode.vhd:1715:99  */
  assign n5628_o = n5625_o | n5627_o;
  /* T80_MCode.vhd:1716:33  */
  assign n5630_o = ir == 8'b10011000;
  /* T80_MCode.vhd:1716:33  */
  assign n5631_o = n5628_o | n5630_o;
  /* T80_MCode.vhd:1716:44  */
  assign n5633_o = ir == 8'b10011001;
  /* T80_MCode.vhd:1716:44  */
  assign n5634_o = n5631_o | n5633_o;
  /* T80_MCode.vhd:1716:55  */
  assign n5636_o = ir == 8'b10011010;
  /* T80_MCode.vhd:1716:55  */
  assign n5637_o = n5634_o | n5636_o;
  /* T80_MCode.vhd:1716:66  */
  assign n5639_o = ir == 8'b10011011;
  /* T80_MCode.vhd:1716:66  */
  assign n5640_o = n5637_o | n5639_o;
  /* T80_MCode.vhd:1716:77  */
  assign n5642_o = ir == 8'b10011100;
  /* T80_MCode.vhd:1716:77  */
  assign n5643_o = n5640_o | n5642_o;
  /* T80_MCode.vhd:1716:88  */
  assign n5645_o = ir == 8'b10011101;
  /* T80_MCode.vhd:1716:88  */
  assign n5646_o = n5643_o | n5645_o;
  /* T80_MCode.vhd:1716:99  */
  assign n5648_o = ir == 8'b10011111;
  /* T80_MCode.vhd:1716:99  */
  assign n5649_o = n5646_o | n5648_o;
  /* T80_MCode.vhd:1717:33  */
  assign n5651_o = ir == 8'b10100000;
  /* T80_MCode.vhd:1717:33  */
  assign n5652_o = n5649_o | n5651_o;
  /* T80_MCode.vhd:1717:44  */
  assign n5654_o = ir == 8'b10100001;
  /* T80_MCode.vhd:1717:44  */
  assign n5655_o = n5652_o | n5654_o;
  /* T80_MCode.vhd:1717:55  */
  assign n5657_o = ir == 8'b10100010;
  /* T80_MCode.vhd:1717:55  */
  assign n5658_o = n5655_o | n5657_o;
  /* T80_MCode.vhd:1717:66  */
  assign n5660_o = ir == 8'b10100011;
  /* T80_MCode.vhd:1717:66  */
  assign n5661_o = n5658_o | n5660_o;
  /* T80_MCode.vhd:1717:77  */
  assign n5663_o = ir == 8'b10100100;
  /* T80_MCode.vhd:1717:77  */
  assign n5664_o = n5661_o | n5663_o;
  /* T80_MCode.vhd:1717:88  */
  assign n5666_o = ir == 8'b10100101;
  /* T80_MCode.vhd:1717:88  */
  assign n5667_o = n5664_o | n5666_o;
  /* T80_MCode.vhd:1717:99  */
  assign n5669_o = ir == 8'b10100111;
  /* T80_MCode.vhd:1717:99  */
  assign n5670_o = n5667_o | n5669_o;
  /* T80_MCode.vhd:1718:33  */
  assign n5672_o = ir == 8'b10101000;
  /* T80_MCode.vhd:1718:33  */
  assign n5673_o = n5670_o | n5672_o;
  /* T80_MCode.vhd:1718:44  */
  assign n5675_o = ir == 8'b10101001;
  /* T80_MCode.vhd:1718:44  */
  assign n5676_o = n5673_o | n5675_o;
  /* T80_MCode.vhd:1718:55  */
  assign n5678_o = ir == 8'b10101010;
  /* T80_MCode.vhd:1718:55  */
  assign n5679_o = n5676_o | n5678_o;
  /* T80_MCode.vhd:1718:66  */
  assign n5681_o = ir == 8'b10101011;
  /* T80_MCode.vhd:1718:66  */
  assign n5682_o = n5679_o | n5681_o;
  /* T80_MCode.vhd:1718:77  */
  assign n5684_o = ir == 8'b10101100;
  /* T80_MCode.vhd:1718:77  */
  assign n5685_o = n5682_o | n5684_o;
  /* T80_MCode.vhd:1718:88  */
  assign n5687_o = ir == 8'b10101101;
  /* T80_MCode.vhd:1718:88  */
  assign n5688_o = n5685_o | n5687_o;
  /* T80_MCode.vhd:1718:99  */
  assign n5690_o = ir == 8'b10101111;
  /* T80_MCode.vhd:1718:99  */
  assign n5691_o = n5688_o | n5690_o;
  /* T80_MCode.vhd:1719:33  */
  assign n5693_o = ir == 8'b10110000;
  /* T80_MCode.vhd:1719:33  */
  assign n5694_o = n5691_o | n5693_o;
  /* T80_MCode.vhd:1719:44  */
  assign n5696_o = ir == 8'b10110001;
  /* T80_MCode.vhd:1719:44  */
  assign n5697_o = n5694_o | n5696_o;
  /* T80_MCode.vhd:1719:55  */
  assign n5699_o = ir == 8'b10110010;
  /* T80_MCode.vhd:1719:55  */
  assign n5700_o = n5697_o | n5699_o;
  /* T80_MCode.vhd:1719:66  */
  assign n5702_o = ir == 8'b10110011;
  /* T80_MCode.vhd:1719:66  */
  assign n5703_o = n5700_o | n5702_o;
  /* T80_MCode.vhd:1719:77  */
  assign n5705_o = ir == 8'b10110100;
  /* T80_MCode.vhd:1719:77  */
  assign n5706_o = n5703_o | n5705_o;
  /* T80_MCode.vhd:1719:88  */
  assign n5708_o = ir == 8'b10110101;
  /* T80_MCode.vhd:1719:88  */
  assign n5709_o = n5706_o | n5708_o;
  /* T80_MCode.vhd:1719:99  */
  assign n5711_o = ir == 8'b10110111;
  /* T80_MCode.vhd:1719:99  */
  assign n5712_o = n5709_o | n5711_o;
  /* T80_MCode.vhd:1720:33  */
  assign n5714_o = ir == 8'b10111000;
  /* T80_MCode.vhd:1720:33  */
  assign n5715_o = n5712_o | n5714_o;
  /* T80_MCode.vhd:1720:44  */
  assign n5717_o = ir == 8'b10111001;
  /* T80_MCode.vhd:1720:44  */
  assign n5718_o = n5715_o | n5717_o;
  /* T80_MCode.vhd:1720:55  */
  assign n5720_o = ir == 8'b10111010;
  /* T80_MCode.vhd:1720:55  */
  assign n5721_o = n5718_o | n5720_o;
  /* T80_MCode.vhd:1720:66  */
  assign n5723_o = ir == 8'b10111011;
  /* T80_MCode.vhd:1720:66  */
  assign n5724_o = n5721_o | n5723_o;
  /* T80_MCode.vhd:1720:77  */
  assign n5726_o = ir == 8'b10111100;
  /* T80_MCode.vhd:1720:77  */
  assign n5727_o = n5724_o | n5726_o;
  /* T80_MCode.vhd:1720:88  */
  assign n5729_o = ir == 8'b10111101;
  /* T80_MCode.vhd:1720:88  */
  assign n5730_o = n5727_o | n5729_o;
  /* T80_MCode.vhd:1720:99  */
  assign n5732_o = ir == 8'b10111111;
  /* T80_MCode.vhd:1720:99  */
  assign n5733_o = n5730_o | n5732_o;
  /* T80_MCode.vhd:1750:38  */
  assign n5734_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:1751:33  */
  assign n5736_o = n5734_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:1751:40  */
  assign n5738_o = n5734_o == 31'b0000000000000000000000000000111;
  /* T80_MCode.vhd:1751:40  */
  assign n5739_o = n5736_o | n5738_o;
  /* T80_MCode.vhd:1753:33  */
  assign n5741_o = n5734_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:1759:33  */
  assign n5743_o = n5734_o == 31'b0000000000000000000000000000011;
  assign n5744_o = {n5743_o, n5741_o, n5739_o};
  /* T80_MCode.vhd:1750:33  */
  always @*
    case (n5744_o)
      3'b100: n5746_o = n2497_o;
      3'b010: n5746_o = 3'b100;
      3'b001: n5746_o = n2497_o;
      default: n5746_o = n2497_o;
    endcase
  /* T80_MCode.vhd:1750:33  */
  always @*
    case (n5744_o)
      3'b100: n5749_o = 1'b0;
      3'b010: n5749_o = 1'b1;
      3'b001: n5749_o = 1'b0;
      default: n5749_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1750:33  */
  always @*
    case (n5744_o)
      3'b100: n5751_o = n2500_o;
      3'b010: n5751_o = 4'b1011;
      3'b001: n5751_o = n2500_o;
      default: n5751_o = n2500_o;
    endcase
  /* T80_MCode.vhd:1750:33  */
  always @*
    case (n5744_o)
      3'b100: n5754_o = 1'b0;
      3'b010: n5754_o = 1'b1;
      3'b001: n5754_o = 1'b0;
      default: n5754_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1750:33  */
  always @*
    case (n5744_o)
      3'b100: n5758_o = 3'b111;
      3'b010: n5758_o = 3'b010;
      3'b001: n5758_o = 3'b010;
      default: n5758_o = 3'b111;
    endcase
  /* T80_MCode.vhd:1750:33  */
  always @*
    case (n5744_o)
      3'b100: n5761_o = 1'b1;
      3'b010: n5761_o = 1'b0;
      3'b001: n5761_o = 1'b0;
      default: n5761_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1747:25  */
  assign n5763_o = ir == 8'b10000110;
  /* T80_MCode.vhd:1747:40  */
  assign n5765_o = ir == 8'b10001110;
  /* T80_MCode.vhd:1747:40  */
  assign n5766_o = n5763_o | n5765_o;
  /* T80_MCode.vhd:1747:51  */
  assign n5768_o = ir == 8'b10010110;
  /* T80_MCode.vhd:1747:51  */
  assign n5769_o = n5766_o | n5768_o;
  /* T80_MCode.vhd:1747:62  */
  assign n5771_o = ir == 8'b10011110;
  /* T80_MCode.vhd:1747:62  */
  assign n5772_o = n5769_o | n5771_o;
  /* T80_MCode.vhd:1747:73  */
  assign n5774_o = ir == 8'b10100110;
  /* T80_MCode.vhd:1747:73  */
  assign n5775_o = n5772_o | n5774_o;
  /* T80_MCode.vhd:1747:84  */
  assign n5777_o = ir == 8'b10101110;
  /* T80_MCode.vhd:1747:84  */
  assign n5778_o = n5775_o | n5777_o;
  /* T80_MCode.vhd:1747:95  */
  assign n5780_o = ir == 8'b10110110;
  /* T80_MCode.vhd:1747:95  */
  assign n5781_o = n5778_o | n5780_o;
  /* T80_MCode.vhd:1747:106  */
  assign n5783_o = ir == 8'b10111110;
  /* T80_MCode.vhd:1747:106  */
  assign n5784_o = n5781_o | n5783_o;
  assign n5785_o = {n5784_o, n5733_o, n5512_o, n5461_o, n5240_o, n5201_o, n4999_o, n4948_o};
  /* T80_MCode.vhd:1559:25  */
  always @*
    case (n5785_o)
      8'b10000000: n5791_o = 3'b011;
      8'b01000000: n5791_o = n5555_o;
      8'b00100000: n5791_o = 3'b011;
      8'b00010000: n5791_o = n5283_o;
      8'b00001000: n5791_o = 3'b010;
      8'b00000100: n5791_o = n5026_o;
      8'b00000010: n5791_o = 3'b011;
      8'b00000001: n5791_o = n4770_o;
      default: n5791_o = 3'bX;
    endcase
  /* T80_MCode.vhd:1559:25  */
  always @*
    case (n5785_o)
      8'b10000000: n5793_o = n5746_o;
      8'b01000000: n5793_o = n5556_o;
      8'b00100000: n5793_o = n5474_o;
      8'b00010000: n5793_o = n5284_o;
      8'b00001000: n5793_o = n5212_o;
      8'b00000100: n5793_o = n5027_o;
      8'b00000010: n5793_o = n4961_o;
      8'b00000001: n5793_o = n4771_o;
      default: n5793_o = 3'bX;
    endcase
  /* T80_MCode.vhd:1559:25  */
  always @*
    case (n5785_o)
      8'b10000000: n5796_o = n5749_o;
      8'b01000000: n5796_o = n5557_o;
      8'b00100000: n5796_o = n5477_o;
      8'b00010000: n5796_o = n5285_o;
      8'b00001000: n5796_o = 1'b0;
      8'b00000100: n5796_o = 1'b0;
      8'b00000010: n5796_o = n4964_o;
      8'b00000001: n5796_o = n4772_o;
      default: n5796_o = 1'bX;
    endcase
  /* T80_MCode.vhd:1559:25  */
  always @*
    case (n5785_o)
      8'b10000000: n5798_o = n4727_o;
      8'b01000000: n5798_o = n4727_o;
      8'b00100000: n5798_o = n4727_o;
      8'b00010000: n5798_o = n4727_o;
      8'b00001000: n5798_o = n4727_o;
      8'b00000100: n5798_o = n5005_o;
      8'b00000010: n5798_o = n4727_o;
      8'b00000001: n5798_o = n4727_o;
      default: n5798_o = 3'bX;
    endcase
  /* T80_MCode.vhd:1559:25  */
  always @*
    case (n5785_o)
      8'b10000000: n5800_o = n5751_o;
      8'b01000000: n5800_o = n5558_o;
      8'b00100000: n5800_o = n5479_o;
      8'b00010000: n5800_o = n5286_o;
      8'b00001000: n5800_o = n5214_o;
      8'b00000100: n5800_o = n5029_o;
      8'b00000010: n5800_o = n4966_o;
      8'b00000001: n5800_o = n4773_o;
      default: n5800_o = 4'bX;
    endcase
  /* T80_MCode.vhd:1559:25  */
  always @*
    case (n5785_o)
      8'b10000000: n5803_o = n5754_o;
      8'b01000000: n5803_o = n5559_o;
      8'b00100000: n5803_o = n5482_o;
      8'b00010000: n5803_o = n5287_o;
      8'b00001000: n5803_o = 1'b0;
      8'b00000100: n5803_o = 1'b0;
      8'b00000010: n5803_o = n4969_o;
      8'b00000001: n5803_o = n4774_o;
      default: n5803_o = 1'bX;
    endcase
  /* T80_MCode.vhd:1559:25  */
  always @*
    case (n5785_o)
      8'b10000000: n5805_o = n5758_o;
      8'b01000000: n5805_o = n5561_o;
      8'b00100000: n5805_o = n5486_o;
      8'b00010000: n5805_o = n5289_o;
      8'b00001000: n5805_o = n5217_o;
      8'b00000100: n5805_o = n5031_o;
      8'b00000010: n5805_o = n4973_o;
      8'b00000001: n5805_o = n4776_o;
      default: n5805_o = 3'bX;
    endcase
  /* T80_MCode.vhd:1559:25  */
  always @*
    case (n5785_o)
      8'b10000000: n5808_o = n5761_o;
      8'b01000000: n5808_o = n5563_o;
      8'b00100000: n5808_o = n5489_o;
      8'b00010000: n5808_o = n5291_o;
      8'b00001000: n5808_o = 1'b0;
      8'b00000100: n5808_o = 1'b0;
      8'b00000010: n5808_o = n4976_o;
      8'b00000001: n5808_o = n4778_o;
      default: n5808_o = 1'bX;
    endcase
  /* T80_MCode.vhd:1559:25  */
  always @*
    case (n5785_o)
      8'b10000000: n5811_o = 1'b0;
      8'b01000000: n5811_o = n5566_o;
      8'b00100000: n5811_o = 1'b0;
      8'b00010000: n5811_o = n5294_o;
      8'b00001000: n5811_o = 1'b0;
      8'b00000100: n5811_o = n5034_o;
      8'b00000010: n5811_o = 1'b0;
      8'b00000001: n5811_o = n4781_o;
      default: n5811_o = 1'bX;
    endcase
  /* T80_MCode.vhd:1548:17  */
  assign n5813_o = iset == 2'b01;
  /* T80_MCode.vhd:1774:25  */
  assign n5815_o = ir == 8'b00000000;
  /* T80_MCode.vhd:1774:40  */
  assign n5817_o = ir == 8'b00000001;
  /* T80_MCode.vhd:1774:40  */
  assign n5818_o = n5815_o | n5817_o;
  /* T80_MCode.vhd:1774:51  */
  assign n5820_o = ir == 8'b00000010;
  /* T80_MCode.vhd:1774:51  */
  assign n5821_o = n5818_o | n5820_o;
  /* T80_MCode.vhd:1774:62  */
  assign n5823_o = ir == 8'b00000011;
  /* T80_MCode.vhd:1774:62  */
  assign n5824_o = n5821_o | n5823_o;
  /* T80_MCode.vhd:1774:73  */
  assign n5826_o = ir == 8'b00000100;
  /* T80_MCode.vhd:1774:73  */
  assign n5827_o = n5824_o | n5826_o;
  /* T80_MCode.vhd:1774:84  */
  assign n5829_o = ir == 8'b00000101;
  /* T80_MCode.vhd:1774:84  */
  assign n5830_o = n5827_o | n5829_o;
  /* T80_MCode.vhd:1774:95  */
  assign n5832_o = ir == 8'b00000110;
  /* T80_MCode.vhd:1774:95  */
  assign n5833_o = n5830_o | n5832_o;
  /* T80_MCode.vhd:1774:106  */
  assign n5835_o = ir == 8'b00000111;
  /* T80_MCode.vhd:1774:106  */
  assign n5836_o = n5833_o | n5835_o;
  /* T80_MCode.vhd:1775:33  */
  assign n5838_o = ir == 8'b00001000;
  /* T80_MCode.vhd:1775:33  */
  assign n5839_o = n5836_o | n5838_o;
  /* T80_MCode.vhd:1775:44  */
  assign n5841_o = ir == 8'b00001001;
  /* T80_MCode.vhd:1775:44  */
  assign n5842_o = n5839_o | n5841_o;
  /* T80_MCode.vhd:1775:55  */
  assign n5844_o = ir == 8'b00001010;
  /* T80_MCode.vhd:1775:55  */
  assign n5845_o = n5842_o | n5844_o;
  /* T80_MCode.vhd:1775:66  */
  assign n5847_o = ir == 8'b00001011;
  /* T80_MCode.vhd:1775:66  */
  assign n5848_o = n5845_o | n5847_o;
  /* T80_MCode.vhd:1775:77  */
  assign n5850_o = ir == 8'b00001100;
  /* T80_MCode.vhd:1775:77  */
  assign n5851_o = n5848_o | n5850_o;
  /* T80_MCode.vhd:1775:88  */
  assign n5853_o = ir == 8'b00001101;
  /* T80_MCode.vhd:1775:88  */
  assign n5854_o = n5851_o | n5853_o;
  /* T80_MCode.vhd:1775:99  */
  assign n5856_o = ir == 8'b00001110;
  /* T80_MCode.vhd:1775:99  */
  assign n5857_o = n5854_o | n5856_o;
  /* T80_MCode.vhd:1775:110  */
  assign n5859_o = ir == 8'b00001111;
  /* T80_MCode.vhd:1775:110  */
  assign n5860_o = n5857_o | n5859_o;
  /* T80_MCode.vhd:1776:33  */
  assign n5862_o = ir == 8'b00010000;
  /* T80_MCode.vhd:1776:33  */
  assign n5863_o = n5860_o | n5862_o;
  /* T80_MCode.vhd:1776:44  */
  assign n5865_o = ir == 8'b00010001;
  /* T80_MCode.vhd:1776:44  */
  assign n5866_o = n5863_o | n5865_o;
  /* T80_MCode.vhd:1776:55  */
  assign n5868_o = ir == 8'b00010010;
  /* T80_MCode.vhd:1776:55  */
  assign n5869_o = n5866_o | n5868_o;
  /* T80_MCode.vhd:1776:66  */
  assign n5871_o = ir == 8'b00010011;
  /* T80_MCode.vhd:1776:66  */
  assign n5872_o = n5869_o | n5871_o;
  /* T80_MCode.vhd:1776:77  */
  assign n5874_o = ir == 8'b00010100;
  /* T80_MCode.vhd:1776:77  */
  assign n5875_o = n5872_o | n5874_o;
  /* T80_MCode.vhd:1776:88  */
  assign n5877_o = ir == 8'b00010101;
  /* T80_MCode.vhd:1776:88  */
  assign n5878_o = n5875_o | n5877_o;
  /* T80_MCode.vhd:1776:99  */
  assign n5880_o = ir == 8'b00010110;
  /* T80_MCode.vhd:1776:99  */
  assign n5881_o = n5878_o | n5880_o;
  /* T80_MCode.vhd:1776:110  */
  assign n5883_o = ir == 8'b00010111;
  /* T80_MCode.vhd:1776:110  */
  assign n5884_o = n5881_o | n5883_o;
  /* T80_MCode.vhd:1777:33  */
  assign n5886_o = ir == 8'b00011000;
  /* T80_MCode.vhd:1777:33  */
  assign n5887_o = n5884_o | n5886_o;
  /* T80_MCode.vhd:1777:44  */
  assign n5889_o = ir == 8'b00011001;
  /* T80_MCode.vhd:1777:44  */
  assign n5890_o = n5887_o | n5889_o;
  /* T80_MCode.vhd:1777:55  */
  assign n5892_o = ir == 8'b00011010;
  /* T80_MCode.vhd:1777:55  */
  assign n5893_o = n5890_o | n5892_o;
  /* T80_MCode.vhd:1777:66  */
  assign n5895_o = ir == 8'b00011011;
  /* T80_MCode.vhd:1777:66  */
  assign n5896_o = n5893_o | n5895_o;
  /* T80_MCode.vhd:1777:77  */
  assign n5898_o = ir == 8'b00011100;
  /* T80_MCode.vhd:1777:77  */
  assign n5899_o = n5896_o | n5898_o;
  /* T80_MCode.vhd:1777:88  */
  assign n5901_o = ir == 8'b00011101;
  /* T80_MCode.vhd:1777:88  */
  assign n5902_o = n5899_o | n5901_o;
  /* T80_MCode.vhd:1777:99  */
  assign n5904_o = ir == 8'b00011110;
  /* T80_MCode.vhd:1777:99  */
  assign n5905_o = n5902_o | n5904_o;
  /* T80_MCode.vhd:1777:110  */
  assign n5907_o = ir == 8'b00011111;
  /* T80_MCode.vhd:1777:110  */
  assign n5908_o = n5905_o | n5907_o;
  /* T80_MCode.vhd:1778:33  */
  assign n5910_o = ir == 8'b00100000;
  /* T80_MCode.vhd:1778:33  */
  assign n5911_o = n5908_o | n5910_o;
  /* T80_MCode.vhd:1778:44  */
  assign n5913_o = ir == 8'b00100001;
  /* T80_MCode.vhd:1778:44  */
  assign n5914_o = n5911_o | n5913_o;
  /* T80_MCode.vhd:1778:55  */
  assign n5916_o = ir == 8'b00100010;
  /* T80_MCode.vhd:1778:55  */
  assign n5917_o = n5914_o | n5916_o;
  /* T80_MCode.vhd:1778:66  */
  assign n5919_o = ir == 8'b00100011;
  /* T80_MCode.vhd:1778:66  */
  assign n5920_o = n5917_o | n5919_o;
  /* T80_MCode.vhd:1778:77  */
  assign n5922_o = ir == 8'b00100100;
  /* T80_MCode.vhd:1778:77  */
  assign n5923_o = n5920_o | n5922_o;
  /* T80_MCode.vhd:1778:88  */
  assign n5925_o = ir == 8'b00100101;
  /* T80_MCode.vhd:1778:88  */
  assign n5926_o = n5923_o | n5925_o;
  /* T80_MCode.vhd:1778:99  */
  assign n5928_o = ir == 8'b00100110;
  /* T80_MCode.vhd:1778:99  */
  assign n5929_o = n5926_o | n5928_o;
  /* T80_MCode.vhd:1778:110  */
  assign n5931_o = ir == 8'b00100111;
  /* T80_MCode.vhd:1778:110  */
  assign n5932_o = n5929_o | n5931_o;
  /* T80_MCode.vhd:1779:33  */
  assign n5934_o = ir == 8'b00101000;
  /* T80_MCode.vhd:1779:33  */
  assign n5935_o = n5932_o | n5934_o;
  /* T80_MCode.vhd:1779:44  */
  assign n5937_o = ir == 8'b00101001;
  /* T80_MCode.vhd:1779:44  */
  assign n5938_o = n5935_o | n5937_o;
  /* T80_MCode.vhd:1779:55  */
  assign n5940_o = ir == 8'b00101010;
  /* T80_MCode.vhd:1779:55  */
  assign n5941_o = n5938_o | n5940_o;
  /* T80_MCode.vhd:1779:66  */
  assign n5943_o = ir == 8'b00101011;
  /* T80_MCode.vhd:1779:66  */
  assign n5944_o = n5941_o | n5943_o;
  /* T80_MCode.vhd:1779:77  */
  assign n5946_o = ir == 8'b00101100;
  /* T80_MCode.vhd:1779:77  */
  assign n5947_o = n5944_o | n5946_o;
  /* T80_MCode.vhd:1779:88  */
  assign n5949_o = ir == 8'b00101101;
  /* T80_MCode.vhd:1779:88  */
  assign n5950_o = n5947_o | n5949_o;
  /* T80_MCode.vhd:1779:99  */
  assign n5952_o = ir == 8'b00101110;
  /* T80_MCode.vhd:1779:99  */
  assign n5953_o = n5950_o | n5952_o;
  /* T80_MCode.vhd:1779:110  */
  assign n5955_o = ir == 8'b00101111;
  /* T80_MCode.vhd:1779:110  */
  assign n5956_o = n5953_o | n5955_o;
  /* T80_MCode.vhd:1780:33  */
  assign n5958_o = ir == 8'b00110000;
  /* T80_MCode.vhd:1780:33  */
  assign n5959_o = n5956_o | n5958_o;
  /* T80_MCode.vhd:1780:44  */
  assign n5961_o = ir == 8'b00110001;
  /* T80_MCode.vhd:1780:44  */
  assign n5962_o = n5959_o | n5961_o;
  /* T80_MCode.vhd:1780:55  */
  assign n5964_o = ir == 8'b00110010;
  /* T80_MCode.vhd:1780:55  */
  assign n5965_o = n5962_o | n5964_o;
  /* T80_MCode.vhd:1780:66  */
  assign n5967_o = ir == 8'b00110011;
  /* T80_MCode.vhd:1780:66  */
  assign n5968_o = n5965_o | n5967_o;
  /* T80_MCode.vhd:1780:77  */
  assign n5970_o = ir == 8'b00110100;
  /* T80_MCode.vhd:1780:77  */
  assign n5971_o = n5968_o | n5970_o;
  /* T80_MCode.vhd:1780:88  */
  assign n5973_o = ir == 8'b00110101;
  /* T80_MCode.vhd:1780:88  */
  assign n5974_o = n5971_o | n5973_o;
  /* T80_MCode.vhd:1780:99  */
  assign n5976_o = ir == 8'b00110110;
  /* T80_MCode.vhd:1780:99  */
  assign n5977_o = n5974_o | n5976_o;
  /* T80_MCode.vhd:1780:110  */
  assign n5979_o = ir == 8'b00110111;
  /* T80_MCode.vhd:1780:110  */
  assign n5980_o = n5977_o | n5979_o;
  /* T80_MCode.vhd:1781:33  */
  assign n5982_o = ir == 8'b00111000;
  /* T80_MCode.vhd:1781:33  */
  assign n5983_o = n5980_o | n5982_o;
  /* T80_MCode.vhd:1781:44  */
  assign n5985_o = ir == 8'b00111001;
  /* T80_MCode.vhd:1781:44  */
  assign n5986_o = n5983_o | n5985_o;
  /* T80_MCode.vhd:1781:55  */
  assign n5988_o = ir == 8'b00111010;
  /* T80_MCode.vhd:1781:55  */
  assign n5989_o = n5986_o | n5988_o;
  /* T80_MCode.vhd:1781:66  */
  assign n5991_o = ir == 8'b00111011;
  /* T80_MCode.vhd:1781:66  */
  assign n5992_o = n5989_o | n5991_o;
  /* T80_MCode.vhd:1781:77  */
  assign n5994_o = ir == 8'b00111100;
  /* T80_MCode.vhd:1781:77  */
  assign n5995_o = n5992_o | n5994_o;
  /* T80_MCode.vhd:1781:88  */
  assign n5997_o = ir == 8'b00111101;
  /* T80_MCode.vhd:1781:88  */
  assign n5998_o = n5995_o | n5997_o;
  /* T80_MCode.vhd:1781:99  */
  assign n6000_o = ir == 8'b00111110;
  /* T80_MCode.vhd:1781:99  */
  assign n6001_o = n5998_o | n6000_o;
  /* T80_MCode.vhd:1781:110  */
  assign n6003_o = ir == 8'b00111111;
  /* T80_MCode.vhd:1781:110  */
  assign n6004_o = n6001_o | n6003_o;
  /* T80_MCode.vhd:1784:33  */
  assign n6006_o = ir == 8'b10000000;
  /* T80_MCode.vhd:1784:33  */
  assign n6007_o = n6004_o | n6006_o;
  /* T80_MCode.vhd:1784:44  */
  assign n6009_o = ir == 8'b10000001;
  /* T80_MCode.vhd:1784:44  */
  assign n6010_o = n6007_o | n6009_o;
  /* T80_MCode.vhd:1784:55  */
  assign n6012_o = ir == 8'b10000010;
  /* T80_MCode.vhd:1784:55  */
  assign n6013_o = n6010_o | n6012_o;
  /* T80_MCode.vhd:1784:66  */
  assign n6015_o = ir == 8'b10000011;
  /* T80_MCode.vhd:1784:66  */
  assign n6016_o = n6013_o | n6015_o;
  /* T80_MCode.vhd:1784:77  */
  assign n6018_o = ir == 8'b10000100;
  /* T80_MCode.vhd:1784:77  */
  assign n6019_o = n6016_o | n6018_o;
  /* T80_MCode.vhd:1784:88  */
  assign n6021_o = ir == 8'b10000101;
  /* T80_MCode.vhd:1784:88  */
  assign n6022_o = n6019_o | n6021_o;
  /* T80_MCode.vhd:1784:99  */
  assign n6024_o = ir == 8'b10000110;
  /* T80_MCode.vhd:1784:99  */
  assign n6025_o = n6022_o | n6024_o;
  /* T80_MCode.vhd:1784:110  */
  assign n6027_o = ir == 8'b10000111;
  /* T80_MCode.vhd:1784:110  */
  assign n6028_o = n6025_o | n6027_o;
  /* T80_MCode.vhd:1785:33  */
  assign n6030_o = ir == 8'b10001000;
  /* T80_MCode.vhd:1785:33  */
  assign n6031_o = n6028_o | n6030_o;
  /* T80_MCode.vhd:1785:44  */
  assign n6033_o = ir == 8'b10001001;
  /* T80_MCode.vhd:1785:44  */
  assign n6034_o = n6031_o | n6033_o;
  /* T80_MCode.vhd:1785:55  */
  assign n6036_o = ir == 8'b10001010;
  /* T80_MCode.vhd:1785:55  */
  assign n6037_o = n6034_o | n6036_o;
  /* T80_MCode.vhd:1785:66  */
  assign n6039_o = ir == 8'b10001011;
  /* T80_MCode.vhd:1785:66  */
  assign n6040_o = n6037_o | n6039_o;
  /* T80_MCode.vhd:1785:77  */
  assign n6042_o = ir == 8'b10001100;
  /* T80_MCode.vhd:1785:77  */
  assign n6043_o = n6040_o | n6042_o;
  /* T80_MCode.vhd:1785:88  */
  assign n6045_o = ir == 8'b10001101;
  /* T80_MCode.vhd:1785:88  */
  assign n6046_o = n6043_o | n6045_o;
  /* T80_MCode.vhd:1785:99  */
  assign n6048_o = ir == 8'b10001110;
  /* T80_MCode.vhd:1785:99  */
  assign n6049_o = n6046_o | n6048_o;
  /* T80_MCode.vhd:1785:110  */
  assign n6051_o = ir == 8'b10001111;
  /* T80_MCode.vhd:1785:110  */
  assign n6052_o = n6049_o | n6051_o;
  /* T80_MCode.vhd:1786:33  */
  assign n6054_o = ir == 8'b10010000;
  /* T80_MCode.vhd:1786:33  */
  assign n6055_o = n6052_o | n6054_o;
  /* T80_MCode.vhd:1786:44  */
  assign n6057_o = ir == 8'b10010001;
  /* T80_MCode.vhd:1786:44  */
  assign n6058_o = n6055_o | n6057_o;
  /* T80_MCode.vhd:1786:55  */
  assign n6060_o = ir == 8'b10010010;
  /* T80_MCode.vhd:1786:55  */
  assign n6061_o = n6058_o | n6060_o;
  /* T80_MCode.vhd:1786:66  */
  assign n6063_o = ir == 8'b10010011;
  /* T80_MCode.vhd:1786:66  */
  assign n6064_o = n6061_o | n6063_o;
  /* T80_MCode.vhd:1786:77  */
  assign n6066_o = ir == 8'b10010100;
  /* T80_MCode.vhd:1786:77  */
  assign n6067_o = n6064_o | n6066_o;
  /* T80_MCode.vhd:1786:88  */
  assign n6069_o = ir == 8'b10010101;
  /* T80_MCode.vhd:1786:88  */
  assign n6070_o = n6067_o | n6069_o;
  /* T80_MCode.vhd:1786:99  */
  assign n6072_o = ir == 8'b10010110;
  /* T80_MCode.vhd:1786:99  */
  assign n6073_o = n6070_o | n6072_o;
  /* T80_MCode.vhd:1786:110  */
  assign n6075_o = ir == 8'b10010111;
  /* T80_MCode.vhd:1786:110  */
  assign n6076_o = n6073_o | n6075_o;
  /* T80_MCode.vhd:1787:33  */
  assign n6078_o = ir == 8'b10011000;
  /* T80_MCode.vhd:1787:33  */
  assign n6079_o = n6076_o | n6078_o;
  /* T80_MCode.vhd:1787:44  */
  assign n6081_o = ir == 8'b10011001;
  /* T80_MCode.vhd:1787:44  */
  assign n6082_o = n6079_o | n6081_o;
  /* T80_MCode.vhd:1787:55  */
  assign n6084_o = ir == 8'b10011010;
  /* T80_MCode.vhd:1787:55  */
  assign n6085_o = n6082_o | n6084_o;
  /* T80_MCode.vhd:1787:66  */
  assign n6087_o = ir == 8'b10011011;
  /* T80_MCode.vhd:1787:66  */
  assign n6088_o = n6085_o | n6087_o;
  /* T80_MCode.vhd:1787:77  */
  assign n6090_o = ir == 8'b10011100;
  /* T80_MCode.vhd:1787:77  */
  assign n6091_o = n6088_o | n6090_o;
  /* T80_MCode.vhd:1787:88  */
  assign n6093_o = ir == 8'b10011101;
  /* T80_MCode.vhd:1787:88  */
  assign n6094_o = n6091_o | n6093_o;
  /* T80_MCode.vhd:1787:99  */
  assign n6096_o = ir == 8'b10011110;
  /* T80_MCode.vhd:1787:99  */
  assign n6097_o = n6094_o | n6096_o;
  /* T80_MCode.vhd:1787:110  */
  assign n6099_o = ir == 8'b10011111;
  /* T80_MCode.vhd:1787:110  */
  assign n6100_o = n6097_o | n6099_o;
  /* T80_MCode.vhd:1788:33  */
  assign n6102_o = ir == 8'b10100100;
  /* T80_MCode.vhd:1788:33  */
  assign n6103_o = n6100_o | n6102_o;
  /* T80_MCode.vhd:1788:88  */
  assign n6105_o = ir == 8'b10100101;
  /* T80_MCode.vhd:1788:88  */
  assign n6106_o = n6103_o | n6105_o;
  /* T80_MCode.vhd:1788:99  */
  assign n6108_o = ir == 8'b10100110;
  /* T80_MCode.vhd:1788:99  */
  assign n6109_o = n6106_o | n6108_o;
  /* T80_MCode.vhd:1788:110  */
  assign n6111_o = ir == 8'b10100111;
  /* T80_MCode.vhd:1788:110  */
  assign n6112_o = n6109_o | n6111_o;
  /* T80_MCode.vhd:1789:33  */
  assign n6114_o = ir == 8'b10101100;
  /* T80_MCode.vhd:1789:33  */
  assign n6115_o = n6112_o | n6114_o;
  /* T80_MCode.vhd:1789:88  */
  assign n6117_o = ir == 8'b10101101;
  /* T80_MCode.vhd:1789:88  */
  assign n6118_o = n6115_o | n6117_o;
  /* T80_MCode.vhd:1789:99  */
  assign n6120_o = ir == 8'b10101110;
  /* T80_MCode.vhd:1789:99  */
  assign n6121_o = n6118_o | n6120_o;
  /* T80_MCode.vhd:1789:110  */
  assign n6123_o = ir == 8'b10101111;
  /* T80_MCode.vhd:1789:110  */
  assign n6124_o = n6121_o | n6123_o;
  /* T80_MCode.vhd:1790:33  */
  assign n6126_o = ir == 8'b10110100;
  /* T80_MCode.vhd:1790:33  */
  assign n6127_o = n6124_o | n6126_o;
  /* T80_MCode.vhd:1790:88  */
  assign n6129_o = ir == 8'b10110101;
  /* T80_MCode.vhd:1790:88  */
  assign n6130_o = n6127_o | n6129_o;
  /* T80_MCode.vhd:1790:99  */
  assign n6132_o = ir == 8'b10110110;
  /* T80_MCode.vhd:1790:99  */
  assign n6133_o = n6130_o | n6132_o;
  /* T80_MCode.vhd:1790:110  */
  assign n6135_o = ir == 8'b10110111;
  /* T80_MCode.vhd:1790:110  */
  assign n6136_o = n6133_o | n6135_o;
  /* T80_MCode.vhd:1791:33  */
  assign n6138_o = ir == 8'b10111100;
  /* T80_MCode.vhd:1791:33  */
  assign n6139_o = n6136_o | n6138_o;
  /* T80_MCode.vhd:1791:88  */
  assign n6141_o = ir == 8'b10111101;
  /* T80_MCode.vhd:1791:88  */
  assign n6142_o = n6139_o | n6141_o;
  /* T80_MCode.vhd:1791:99  */
  assign n6144_o = ir == 8'b10111110;
  /* T80_MCode.vhd:1791:99  */
  assign n6145_o = n6142_o | n6144_o;
  /* T80_MCode.vhd:1791:110  */
  assign n6147_o = ir == 8'b10111111;
  /* T80_MCode.vhd:1791:110  */
  assign n6148_o = n6145_o | n6147_o;
  /* T80_MCode.vhd:1792:33  */
  assign n6150_o = ir == 8'b11000000;
  /* T80_MCode.vhd:1792:33  */
  assign n6151_o = n6148_o | n6150_o;
  /* T80_MCode.vhd:1792:44  */
  assign n6153_o = ir == 8'b11000010;
  /* T80_MCode.vhd:1792:44  */
  assign n6154_o = n6151_o | n6153_o;
  /* T80_MCode.vhd:1792:77  */
  assign n6156_o = ir == 8'b11000100;
  /* T80_MCode.vhd:1792:77  */
  assign n6157_o = n6154_o | n6156_o;
  /* T80_MCode.vhd:1792:88  */
  assign n6159_o = ir == 8'b11000101;
  /* T80_MCode.vhd:1792:88  */
  assign n6160_o = n6157_o | n6159_o;
  /* T80_MCode.vhd:1792:99  */
  assign n6162_o = ir == 8'b11000110;
  /* T80_MCode.vhd:1792:99  */
  assign n6163_o = n6160_o | n6162_o;
  /* T80_MCode.vhd:1792:110  */
  assign n6165_o = ir == 8'b11000111;
  /* T80_MCode.vhd:1792:110  */
  assign n6166_o = n6163_o | n6165_o;
  /* T80_MCode.vhd:1793:33  */
  assign n6168_o = ir == 8'b11001000;
  /* T80_MCode.vhd:1793:33  */
  assign n6169_o = n6166_o | n6168_o;
  /* T80_MCode.vhd:1793:44  */
  assign n6171_o = ir == 8'b11001010;
  /* T80_MCode.vhd:1793:44  */
  assign n6172_o = n6169_o | n6171_o;
  /* T80_MCode.vhd:1793:66  */
  assign n6174_o = ir == 8'b11001011;
  /* T80_MCode.vhd:1793:66  */
  assign n6175_o = n6172_o | n6174_o;
  /* T80_MCode.vhd:1793:77  */
  assign n6177_o = ir == 8'b11001100;
  /* T80_MCode.vhd:1793:77  */
  assign n6178_o = n6175_o | n6177_o;
  /* T80_MCode.vhd:1793:88  */
  assign n6180_o = ir == 8'b11001101;
  /* T80_MCode.vhd:1793:88  */
  assign n6181_o = n6178_o | n6180_o;
  /* T80_MCode.vhd:1793:99  */
  assign n6183_o = ir == 8'b11001110;
  /* T80_MCode.vhd:1793:99  */
  assign n6184_o = n6181_o | n6183_o;
  /* T80_MCode.vhd:1793:110  */
  assign n6186_o = ir == 8'b11001111;
  /* T80_MCode.vhd:1793:110  */
  assign n6187_o = n6184_o | n6186_o;
  /* T80_MCode.vhd:1794:33  */
  assign n6189_o = ir == 8'b11010000;
  /* T80_MCode.vhd:1794:33  */
  assign n6190_o = n6187_o | n6189_o;
  /* T80_MCode.vhd:1794:44  */
  assign n6192_o = ir == 8'b11010010;
  /* T80_MCode.vhd:1794:44  */
  assign n6193_o = n6190_o | n6192_o;
  /* T80_MCode.vhd:1794:66  */
  assign n6195_o = ir == 8'b11010011;
  /* T80_MCode.vhd:1794:66  */
  assign n6196_o = n6193_o | n6195_o;
  /* T80_MCode.vhd:1794:77  */
  assign n6198_o = ir == 8'b11010100;
  /* T80_MCode.vhd:1794:77  */
  assign n6199_o = n6196_o | n6198_o;
  /* T80_MCode.vhd:1794:88  */
  assign n6201_o = ir == 8'b11010101;
  /* T80_MCode.vhd:1794:88  */
  assign n6202_o = n6199_o | n6201_o;
  /* T80_MCode.vhd:1794:99  */
  assign n6204_o = ir == 8'b11010110;
  /* T80_MCode.vhd:1794:99  */
  assign n6205_o = n6202_o | n6204_o;
  /* T80_MCode.vhd:1794:110  */
  assign n6207_o = ir == 8'b11010111;
  /* T80_MCode.vhd:1794:110  */
  assign n6208_o = n6205_o | n6207_o;
  /* T80_MCode.vhd:1795:33  */
  assign n6210_o = ir == 8'b11011000;
  /* T80_MCode.vhd:1795:33  */
  assign n6211_o = n6208_o | n6210_o;
  /* T80_MCode.vhd:1795:44  */
  assign n6213_o = ir == 8'b11011010;
  /* T80_MCode.vhd:1795:44  */
  assign n6214_o = n6211_o | n6213_o;
  /* T80_MCode.vhd:1795:66  */
  assign n6216_o = ir == 8'b11011011;
  /* T80_MCode.vhd:1795:66  */
  assign n6217_o = n6214_o | n6216_o;
  /* T80_MCode.vhd:1795:77  */
  assign n6219_o = ir == 8'b11011100;
  /* T80_MCode.vhd:1795:77  */
  assign n6220_o = n6217_o | n6219_o;
  /* T80_MCode.vhd:1795:88  */
  assign n6222_o = ir == 8'b11011101;
  /* T80_MCode.vhd:1795:88  */
  assign n6223_o = n6220_o | n6222_o;
  /* T80_MCode.vhd:1795:99  */
  assign n6225_o = ir == 8'b11011110;
  /* T80_MCode.vhd:1795:99  */
  assign n6226_o = n6223_o | n6225_o;
  /* T80_MCode.vhd:1795:110  */
  assign n6228_o = ir == 8'b11011111;
  /* T80_MCode.vhd:1795:110  */
  assign n6229_o = n6226_o | n6228_o;
  /* T80_MCode.vhd:1796:33  */
  assign n6231_o = ir == 8'b11100000;
  /* T80_MCode.vhd:1796:33  */
  assign n6232_o = n6229_o | n6231_o;
  /* T80_MCode.vhd:1796:44  */
  assign n6234_o = ir == 8'b11100001;
  /* T80_MCode.vhd:1796:44  */
  assign n6235_o = n6232_o | n6234_o;
  /* T80_MCode.vhd:1796:55  */
  assign n6237_o = ir == 8'b11100010;
  /* T80_MCode.vhd:1796:55  */
  assign n6238_o = n6235_o | n6237_o;
  /* T80_MCode.vhd:1796:66  */
  assign n6240_o = ir == 8'b11100011;
  /* T80_MCode.vhd:1796:66  */
  assign n6241_o = n6238_o | n6240_o;
  /* T80_MCode.vhd:1796:77  */
  assign n6243_o = ir == 8'b11100100;
  /* T80_MCode.vhd:1796:77  */
  assign n6244_o = n6241_o | n6243_o;
  /* T80_MCode.vhd:1796:88  */
  assign n6246_o = ir == 8'b11100101;
  /* T80_MCode.vhd:1796:88  */
  assign n6247_o = n6244_o | n6246_o;
  /* T80_MCode.vhd:1796:99  */
  assign n6249_o = ir == 8'b11100110;
  /* T80_MCode.vhd:1796:99  */
  assign n6250_o = n6247_o | n6249_o;
  /* T80_MCode.vhd:1796:110  */
  assign n6252_o = ir == 8'b11100111;
  /* T80_MCode.vhd:1796:110  */
  assign n6253_o = n6250_o | n6252_o;
  /* T80_MCode.vhd:1797:33  */
  assign n6255_o = ir == 8'b11101000;
  /* T80_MCode.vhd:1797:33  */
  assign n6256_o = n6253_o | n6255_o;
  /* T80_MCode.vhd:1797:44  */
  assign n6258_o = ir == 8'b11101001;
  /* T80_MCode.vhd:1797:44  */
  assign n6259_o = n6256_o | n6258_o;
  /* T80_MCode.vhd:1797:55  */
  assign n6261_o = ir == 8'b11101010;
  /* T80_MCode.vhd:1797:55  */
  assign n6262_o = n6259_o | n6261_o;
  /* T80_MCode.vhd:1797:66  */
  assign n6264_o = ir == 8'b11101011;
  /* T80_MCode.vhd:1797:66  */
  assign n6265_o = n6262_o | n6264_o;
  /* T80_MCode.vhd:1797:77  */
  assign n6267_o = ir == 8'b11101100;
  /* T80_MCode.vhd:1797:77  */
  assign n6268_o = n6265_o | n6267_o;
  /* T80_MCode.vhd:1797:88  */
  assign n6270_o = ir == 8'b11101101;
  /* T80_MCode.vhd:1797:88  */
  assign n6271_o = n6268_o | n6270_o;
  /* T80_MCode.vhd:1797:99  */
  assign n6273_o = ir == 8'b11101110;
  /* T80_MCode.vhd:1797:99  */
  assign n6274_o = n6271_o | n6273_o;
  /* T80_MCode.vhd:1797:110  */
  assign n6276_o = ir == 8'b11101111;
  /* T80_MCode.vhd:1797:110  */
  assign n6277_o = n6274_o | n6276_o;
  /* T80_MCode.vhd:1798:33  */
  assign n6279_o = ir == 8'b11110000;
  /* T80_MCode.vhd:1798:33  */
  assign n6280_o = n6277_o | n6279_o;
  /* T80_MCode.vhd:1798:44  */
  assign n6282_o = ir == 8'b11110001;
  /* T80_MCode.vhd:1798:44  */
  assign n6283_o = n6280_o | n6282_o;
  /* T80_MCode.vhd:1798:55  */
  assign n6285_o = ir == 8'b11110010;
  /* T80_MCode.vhd:1798:55  */
  assign n6286_o = n6283_o | n6285_o;
  /* T80_MCode.vhd:1798:77  */
  assign n6288_o = ir == 8'b11110100;
  /* T80_MCode.vhd:1798:77  */
  assign n6289_o = n6286_o | n6288_o;
  /* T80_MCode.vhd:1798:88  */
  assign n6291_o = ir == 8'b11110101;
  /* T80_MCode.vhd:1798:88  */
  assign n6292_o = n6289_o | n6291_o;
  /* T80_MCode.vhd:1798:99  */
  assign n6294_o = ir == 8'b11110110;
  /* T80_MCode.vhd:1798:99  */
  assign n6295_o = n6292_o | n6294_o;
  /* T80_MCode.vhd:1798:110  */
  assign n6297_o = ir == 8'b11110111;
  /* T80_MCode.vhd:1798:110  */
  assign n6298_o = n6295_o | n6297_o;
  /* T80_MCode.vhd:1799:33  */
  assign n6300_o = ir == 8'b11111000;
  /* T80_MCode.vhd:1799:33  */
  assign n6301_o = n6298_o | n6300_o;
  /* T80_MCode.vhd:1799:44  */
  assign n6303_o = ir == 8'b11111001;
  /* T80_MCode.vhd:1799:44  */
  assign n6304_o = n6301_o | n6303_o;
  /* T80_MCode.vhd:1799:55  */
  assign n6306_o = ir == 8'b11111010;
  /* T80_MCode.vhd:1799:55  */
  assign n6307_o = n6304_o | n6306_o;
  /* T80_MCode.vhd:1799:66  */
  assign n6309_o = ir == 8'b11111011;
  /* T80_MCode.vhd:1799:66  */
  assign n6310_o = n6307_o | n6309_o;
  /* T80_MCode.vhd:1799:77  */
  assign n6312_o = ir == 8'b11111100;
  /* T80_MCode.vhd:1799:77  */
  assign n6313_o = n6310_o | n6312_o;
  /* T80_MCode.vhd:1799:88  */
  assign n6315_o = ir == 8'b11111101;
  /* T80_MCode.vhd:1799:88  */
  assign n6316_o = n6313_o | n6315_o;
  /* T80_MCode.vhd:1799:99  */
  assign n6318_o = ir == 8'b11111110;
  /* T80_MCode.vhd:1799:99  */
  assign n6319_o = n6316_o | n6318_o;
  /* T80_MCode.vhd:1799:110  */
  assign n6321_o = ir == 8'b11111111;
  /* T80_MCode.vhd:1799:110  */
  assign n6322_o = n6319_o | n6321_o;
  /* T80_MCode.vhd:1801:25  */
  assign n6324_o = ir == 8'b01110111;
  /* T80_MCode.vhd:1801:40  */
  assign n6326_o = ir == 8'b01111111;
  /* T80_MCode.vhd:1801:40  */
  assign n6327_o = n6324_o | n6326_o;
  /* T80_MCode.vhd:1805:25  */
  assign n6329_o = ir == 8'b01010111;
  /* T80_MCode.vhd:1809:25  */
  assign n6331_o = ir == 8'b01011111;
  /* T80_MCode.vhd:1813:25  */
  assign n6333_o = ir == 8'b01000111;
  /* T80_MCode.vhd:1817:25  */
  assign n6335_o = ir == 8'b01001111;
  /* T80_MCode.vhd:1825:38  */
  assign n6336_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:1826:33  */
  assign n6338_o = n6336_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:1829:33  */
  assign n6340_o = n6336_o == 31'b0000000000000000000000000000011;
  /* T80_MCode.vhd:1835:46  */
  assign n6341_o = ir[5:4]; // extract
  /* T80_MCode.vhd:1835:59  */
  assign n6343_o = n6341_o == 2'b11;
  /* T80_MCode.vhd:1838:78  */
  assign n6344_o = ir[5:4]; // extract
  assign n6346_o = {n6344_o, 1'b1};
  /* T80_MCode.vhd:1835:41  */
  assign n6348_o = n6343_o ? 3'b000 : n6346_o;
  /* T80_MCode.vhd:1835:41  */
  assign n6351_o = n6343_o ? 1'b1 : 1'b0;
  /* T80_MCode.vhd:1833:33  */
  assign n6353_o = n6336_o == 31'b0000000000000000000000000000100;
  /* T80_MCode.vhd:1845:46  */
  assign n6354_o = ir[5:4]; // extract
  /* T80_MCode.vhd:1845:59  */
  assign n6356_o = n6354_o == 2'b11;
  /* T80_MCode.vhd:1848:78  */
  assign n6357_o = ir[5:4]; // extract
  assign n6359_o = {n6357_o, 1'b0};
  /* T80_MCode.vhd:1845:41  */
  assign n6361_o = n6356_o ? 3'b001 : n6359_o;
  /* T80_MCode.vhd:1845:41  */
  assign n6364_o = n6356_o ? 1'b1 : 1'b0;
  /* T80_MCode.vhd:1843:33  */
  assign n6366_o = n6336_o == 31'b0000000000000000000000000000101;
  assign n6367_o = {n6366_o, n6353_o, n6340_o, n6338_o};
  /* T80_MCode.vhd:1825:33  */
  always @*
    case (n6367_o)
      4'b1000: n6371_o = 1'b0;
      4'b0100: n6371_o = 1'b0;
      4'b0010: n6371_o = 1'b1;
      4'b0001: n6371_o = 1'b1;
      default: n6371_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1825:33  */
  always @*
    case (n6367_o)
      4'b1000: n6374_o = 1'b0;
      4'b0100: n6374_o = 1'b1;
      4'b0010: n6374_o = 1'b0;
      4'b0001: n6374_o = 1'b0;
      default: n6374_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1825:33  */
  always @*
    case (n6367_o)
      4'b1000: n6378_o = 1'b1;
      4'b0100: n6378_o = 1'b1;
      4'b0010: n6378_o = 1'b0;
      4'b0001: n6378_o = 1'b0;
      default: n6378_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1825:33  */
  always @*
    case (n6367_o)
      4'b1000: n6380_o = n6361_o;
      4'b0100: n6380_o = n6348_o;
      4'b0010: n6380_o = 3'b000;
      4'b0001: n6380_o = 3'b000;
      default: n6380_o = 3'b000;
    endcase
  /* T80_MCode.vhd:1825:33  */
  always @*
    case (n6367_o)
      4'b1000: n6382_o = n6364_o;
      4'b0100: n6382_o = n6351_o;
      4'b0010: n6382_o = 1'b0;
      4'b0001: n6382_o = 1'b0;
      default: n6382_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1825:33  */
  always @*
    case (n6367_o)
      4'b1000: n6386_o = 3'b111;
      4'b0100: n6386_o = 3'b110;
      4'b0010: n6386_o = 3'b110;
      4'b0001: n6386_o = 3'b111;
      default: n6386_o = 3'b111;
    endcase
  /* T80_MCode.vhd:1825:33  */
  always @*
    case (n6367_o)
      4'b1000: n6389_o = 1'b0;
      4'b0100: n6389_o = 1'b0;
      4'b0010: n6389_o = 1'b0;
      4'b0001: n6389_o = 1'b1;
      default: n6389_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1825:33  */
  always @*
    case (n6367_o)
      4'b1000: n6392_o = 1'b0;
      4'b0100: n6392_o = 1'b0;
      4'b0010: n6392_o = 1'b1;
      4'b0001: n6392_o = 1'b0;
      default: n6392_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1822:25  */
  assign n6394_o = ir == 8'b01001011;
  /* T80_MCode.vhd:1822:40  */
  assign n6396_o = ir == 8'b01011011;
  /* T80_MCode.vhd:1822:40  */
  assign n6397_o = n6394_o | n6396_o;
  /* T80_MCode.vhd:1822:51  */
  assign n6399_o = ir == 8'b01101011;
  /* T80_MCode.vhd:1822:51  */
  assign n6400_o = n6397_o | n6399_o;
  /* T80_MCode.vhd:1822:62  */
  assign n6402_o = ir == 8'b01111011;
  /* T80_MCode.vhd:1822:62  */
  assign n6403_o = n6400_o | n6402_o;
  /* T80_MCode.vhd:1856:38  */
  assign n6404_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:1857:33  */
  assign n6406_o = n6404_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:1864:46  */
  assign n6407_o = ir[5:4]; // extract
  /* T80_MCode.vhd:1864:59  */
  assign n6409_o = n6407_o == 2'b11;
  /* T80_MCode.vhd:1867:78  */
  assign n6410_o = ir[5:4]; // extract
  assign n6413_o = {1'b0, n6410_o, 1'b1};
  /* T80_MCode.vhd:1864:41  */
  assign n6415_o = n6409_o ? 4'b1000 : n6413_o;
  /* T80_MCode.vhd:1860:33  */
  assign n6417_o = n6404_o == 31'b0000000000000000000000000000011;
  /* T80_MCode.vhd:1875:46  */
  assign n6418_o = ir[5:4]; // extract
  /* T80_MCode.vhd:1875:59  */
  assign n6420_o = n6418_o == 2'b11;
  /* T80_MCode.vhd:1878:78  */
  assign n6421_o = ir[5:4]; // extract
  assign n6424_o = {1'b0, n6421_o, 1'b0};
  /* T80_MCode.vhd:1875:41  */
  assign n6426_o = n6420_o ? 4'b1001 : n6424_o;
  /* T80_MCode.vhd:1871:33  */
  assign n6428_o = n6404_o == 31'b0000000000000000000000000000100;
  /* T80_MCode.vhd:1882:33  */
  assign n6430_o = n6404_o == 31'b0000000000000000000000000000101;
  assign n6431_o = {n6430_o, n6428_o, n6417_o, n6406_o};
  /* T80_MCode.vhd:1856:33  */
  always @*
    case (n6431_o)
      4'b1000: n6435_o = 1'b0;
      4'b0100: n6435_o = 1'b0;
      4'b0010: n6435_o = 1'b1;
      4'b0001: n6435_o = 1'b1;
      default: n6435_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1856:33  */
  always @*
    case (n6431_o)
      4'b1000: n6438_o = 1'b0;
      4'b0100: n6438_o = 1'b1;
      4'b0010: n6438_o = 1'b0;
      4'b0001: n6438_o = 1'b0;
      default: n6438_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1856:33  */
  always @*
    case (n6431_o)
      4'b1000: n6440_o = 4'b0000;
      4'b0100: n6440_o = n6426_o;
      4'b0010: n6440_o = n6415_o;
      4'b0001: n6440_o = 4'b0000;
      default: n6440_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:1856:33  */
  always @*
    case (n6431_o)
      4'b1000: n6444_o = 3'b111;
      4'b0100: n6444_o = 3'b110;
      4'b0010: n6444_o = 3'b110;
      4'b0001: n6444_o = 3'b111;
      default: n6444_o = 3'b111;
    endcase
  /* T80_MCode.vhd:1856:33  */
  always @*
    case (n6431_o)
      4'b1000: n6447_o = 1'b0;
      4'b0100: n6447_o = 1'b0;
      4'b0010: n6447_o = 1'b0;
      4'b0001: n6447_o = 1'b1;
      default: n6447_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1856:33  */
  always @*
    case (n6431_o)
      4'b1000: n6450_o = 1'b0;
      4'b0100: n6450_o = 1'b0;
      4'b0010: n6450_o = 1'b1;
      4'b0001: n6450_o = 1'b0;
      default: n6450_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1856:33  */
  always @*
    case (n6431_o)
      4'b1000: n6454_o = 1'b1;
      4'b0100: n6454_o = 1'b1;
      4'b0010: n6454_o = 1'b0;
      4'b0001: n6454_o = 1'b0;
      default: n6454_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1853:25  */
  assign n6456_o = ir == 8'b01000011;
  /* T80_MCode.vhd:1853:40  */
  assign n6458_o = ir == 8'b01010011;
  /* T80_MCode.vhd:1853:40  */
  assign n6459_o = n6456_o | n6458_o;
  /* T80_MCode.vhd:1853:51  */
  assign n6461_o = ir == 8'b01100011;
  /* T80_MCode.vhd:1853:51  */
  assign n6462_o = n6459_o | n6461_o;
  /* T80_MCode.vhd:1853:62  */
  assign n6464_o = ir == 8'b01110011;
  /* T80_MCode.vhd:1853:62  */
  assign n6465_o = n6462_o | n6464_o;
  /* T80_MCode.vhd:1889:38  */
  assign n6466_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:1890:33  */
  assign n6468_o = n6466_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:1898:46  */
  assign n6470_o = ir[3]; // extract
  /* T80_MCode.vhd:1898:50  */
  assign n6471_o = ~n6470_o;
  /* T80_MCode.vhd:1898:41  */
  assign n6474_o = n6471_o ? 4'b0110 : 4'b1110;
  /* T80_MCode.vhd:1893:33  */
  assign n6476_o = n6466_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:1907:46  */
  assign n6477_o = ir[3]; // extract
  /* T80_MCode.vhd:1907:50  */
  assign n6478_o = ~n6477_o;
  /* T80_MCode.vhd:1907:41  */
  assign n6481_o = n6478_o ? 4'b0101 : 4'b1101;
  /* T80_MCode.vhd:1903:33  */
  assign n6483_o = n6466_o == 31'b0000000000000000000000000000011;
  /* T80_MCode.vhd:1913:33  */
  assign n6485_o = n6466_o == 31'b0000000000000000000000000000100;
  assign n6486_o = {n6485_o, n6483_o, n6476_o, n6468_o};
  /* T80_MCode.vhd:1889:33  */
  always @*
    case (n6486_o)
      4'b1000: n6489_o = 3'b101;
      4'b0100: n6489_o = 3'b101;
      4'b0010: n6489_o = n2497_o;
      4'b0001: n6489_o = n2497_o;
      default: n6489_o = n2497_o;
    endcase
  /* T80_MCode.vhd:1889:33  */
  always @*
    case (n6486_o)
      4'b1000: n6492_o = 4'b0000;
      4'b0100: n6492_o = n6481_o;
      4'b0010: n6492_o = n6474_o;
      4'b0001: n6492_o = 4'b1100;
      default: n6492_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:1889:33  */
  always @*
    case (n6486_o)
      4'b1000: n6494_o = 3'b000;
      4'b0100: n6494_o = 3'b000;
      4'b0010: n6494_o = 3'b111;
      4'b0001: n6494_o = 3'b000;
      default: n6494_o = 3'b000;
    endcase
  /* T80_MCode.vhd:1889:33  */
  always @*
    case (n6486_o)
      4'b1000: n6497_o = 4'b0000;
      4'b0100: n6497_o = 4'b0000;
      4'b0010: n6497_o = 4'b0110;
      4'b0001: n6497_o = 4'b0000;
      default: n6497_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:1889:33  */
  always @*
    case (n6486_o)
      4'b1000: n6499_o = n2500_o;
      4'b0100: n6499_o = n2500_o;
      4'b0010: n6499_o = 4'b0000;
      4'b0001: n6499_o = n2500_o;
      default: n6499_o = n2500_o;
    endcase
  /* T80_MCode.vhd:1889:33  */
  always @*
    case (n6486_o)
      4'b1000: n6503_o = 3'b111;
      4'b0100: n6503_o = 3'b111;
      4'b0010: n6503_o = 3'b001;
      4'b0001: n6503_o = 3'b010;
      default: n6503_o = 3'b111;
    endcase
  /* T80_MCode.vhd:1889:33  */
  always @*
    case (n6486_o)
      4'b1000: n6506_o = 1'b0;
      4'b0100: n6506_o = 1'b1;
      4'b0010: n6506_o = 1'b0;
      4'b0001: n6506_o = 1'b0;
      default: n6506_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1889:33  */
  always @*
    case (n6486_o)
      4'b1000: n6509_o = 1'b1;
      4'b0100: n6509_o = 1'b0;
      4'b0010: n6509_o = 1'b0;
      4'b0001: n6509_o = 1'b0;
      default: n6509_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1889:33  */
  always @*
    case (n6486_o)
      4'b1000: n6512_o = 1'b0;
      4'b0100: n6512_o = 1'b1;
      4'b0010: n6512_o = 1'b0;
      4'b0001: n6512_o = 1'b0;
      default: n6512_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1889:33  */
  always @*
    case (n6486_o)
      4'b1000: n6515_o = 1'b0;
      4'b0100: n6515_o = 1'b1;
      4'b0010: n6515_o = 1'b0;
      4'b0001: n6515_o = 1'b0;
      default: n6515_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1886:25  */
  assign n6517_o = ir == 8'b10100000;
  /* T80_MCode.vhd:1886:41  */
  assign n6519_o = ir == 8'b10101000;
  /* T80_MCode.vhd:1886:41  */
  assign n6520_o = n6517_o | n6519_o;
  /* T80_MCode.vhd:1886:54  */
  assign n6522_o = ir == 8'b10110000;
  /* T80_MCode.vhd:1886:54  */
  assign n6523_o = n6520_o | n6522_o;
  /* T80_MCode.vhd:1886:67  */
  assign n6525_o = ir == 8'b10111000;
  /* T80_MCode.vhd:1886:67  */
  assign n6526_o = n6523_o | n6525_o;
  /* T80_MCode.vhd:1921:38  */
  assign n6527_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:1922:33  */
  assign n6529_o = n6527_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:1931:46  */
  assign n6531_o = ir[3]; // extract
  /* T80_MCode.vhd:1931:50  */
  assign n6532_o = ~n6531_o;
  /* T80_MCode.vhd:1931:41  */
  assign n6535_o = n6532_o ? 4'b0110 : 4'b1110;
  /* T80_MCode.vhd:1925:33  */
  assign n6537_o = n6527_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:1937:33  */
  assign n6539_o = n6527_o == 31'b0000000000000000000000000000011;
  /* T80_MCode.vhd:1942:33  */
  assign n6541_o = n6527_o == 31'b0000000000000000000000000000100;
  assign n6542_o = {n6541_o, n6539_o, n6537_o, n6529_o};
  /* T80_MCode.vhd:1921:33  */
  always @*
    case (n6542_o)
      4'b1000: n6545_o = 3'b101;
      4'b0100: n6545_o = 3'b101;
      4'b0010: n6545_o = n2497_o;
      4'b0001: n6545_o = n2497_o;
      default: n6545_o = n2497_o;
    endcase
  /* T80_MCode.vhd:1921:33  */
  always @*
    case (n6542_o)
      4'b1000: n6548_o = 4'b0000;
      4'b0100: n6548_o = 4'b0000;
      4'b0010: n6548_o = n6535_o;
      4'b0001: n6548_o = 4'b1100;
      default: n6548_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:1921:33  */
  always @*
    case (n6542_o)
      4'b1000: n6550_o = 3'b000;
      4'b0100: n6550_o = 3'b000;
      4'b0010: n6550_o = 3'b111;
      4'b0001: n6550_o = 3'b000;
      default: n6550_o = 3'b000;
    endcase
  /* T80_MCode.vhd:1921:33  */
  always @*
    case (n6542_o)
      4'b1000: n6553_o = 4'b0000;
      4'b0100: n6553_o = 4'b0000;
      4'b0010: n6553_o = 4'b0110;
      4'b0001: n6553_o = 4'b0000;
      default: n6553_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:1921:33  */
  always @*
    case (n6542_o)
      4'b1000: n6555_o = n2500_o;
      4'b0100: n6555_o = n2500_o;
      4'b0010: n6555_o = 4'b0111;
      4'b0001: n6555_o = n2500_o;
      default: n6555_o = n2500_o;
    endcase
  /* T80_MCode.vhd:1921:33  */
  always @*
    case (n6542_o)
      4'b1000: n6558_o = 1'b0;
      4'b0100: n6558_o = 1'b0;
      4'b0010: n6558_o = 1'b1;
      4'b0001: n6558_o = 1'b0;
      default: n6558_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1921:33  */
  always @*
    case (n6542_o)
      4'b1000: n6561_o = 1'b0;
      4'b0100: n6561_o = 1'b0;
      4'b0010: n6561_o = 1'b1;
      4'b0001: n6561_o = 1'b0;
      default: n6561_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1921:33  */
  always @*
    case (n6542_o)
      4'b1000: n6564_o = 3'b111;
      4'b0100: n6564_o = 3'b111;
      4'b0010: n6564_o = 3'b111;
      4'b0001: n6564_o = 3'b010;
      default: n6564_o = 3'b111;
    endcase
  /* T80_MCode.vhd:1921:33  */
  always @*
    case (n6542_o)
      4'b1000: n6567_o = 1'b0;
      4'b0100: n6567_o = 1'b1;
      4'b0010: n6567_o = 1'b0;
      4'b0001: n6567_o = 1'b0;
      default: n6567_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1921:33  */
  always @*
    case (n6542_o)
      4'b1000: n6571_o = 1'b1;
      4'b0100: n6571_o = 1'b1;
      4'b0010: n6571_o = 1'b0;
      4'b0001: n6571_o = 1'b0;
      default: n6571_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1921:33  */
  always @*
    case (n6542_o)
      4'b1000: n6575_o = 1'b0;
      4'b0100: n6575_o = 1'b1;
      4'b0010: n6575_o = 1'b1;
      4'b0001: n6575_o = 1'b0;
      default: n6575_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1918:25  */
  assign n6577_o = ir == 8'b10100001;
  /* T80_MCode.vhd:1918:41  */
  assign n6579_o = ir == 8'b10101001;
  /* T80_MCode.vhd:1918:41  */
  assign n6580_o = n6577_o | n6579_o;
  /* T80_MCode.vhd:1918:54  */
  assign n6582_o = ir == 8'b10110001;
  /* T80_MCode.vhd:1918:54  */
  assign n6583_o = n6580_o | n6582_o;
  /* T80_MCode.vhd:1918:67  */
  assign n6585_o = ir == 8'b10111001;
  /* T80_MCode.vhd:1918:67  */
  assign n6586_o = n6583_o | n6585_o;
  /* T80_MCode.vhd:1947:25  */
  assign n6588_o = ir == 8'b01000100;
  /* T80_MCode.vhd:1947:40  */
  assign n6590_o = ir == 8'b01001100;
  /* T80_MCode.vhd:1947:40  */
  assign n6591_o = n6588_o | n6590_o;
  /* T80_MCode.vhd:1947:51  */
  assign n6593_o = ir == 8'b01010100;
  /* T80_MCode.vhd:1947:51  */
  assign n6594_o = n6591_o | n6593_o;
  /* T80_MCode.vhd:1947:62  */
  assign n6596_o = ir == 8'b01011100;
  /* T80_MCode.vhd:1947:62  */
  assign n6597_o = n6594_o | n6596_o;
  /* T80_MCode.vhd:1947:73  */
  assign n6599_o = ir == 8'b01100100;
  /* T80_MCode.vhd:1947:73  */
  assign n6600_o = n6597_o | n6599_o;
  /* T80_MCode.vhd:1947:84  */
  assign n6602_o = ir == 8'b01101100;
  /* T80_MCode.vhd:1947:84  */
  assign n6603_o = n6600_o | n6602_o;
  /* T80_MCode.vhd:1947:95  */
  assign n6605_o = ir == 8'b01110100;
  /* T80_MCode.vhd:1947:95  */
  assign n6606_o = n6603_o | n6605_o;
  /* T80_MCode.vhd:1947:106  */
  assign n6608_o = ir == 8'b01111100;
  /* T80_MCode.vhd:1947:106  */
  assign n6609_o = n6606_o | n6608_o;
  /* T80_MCode.vhd:1954:25  */
  assign n6611_o = ir == 8'b01000110;
  /* T80_MCode.vhd:1954:40  */
  assign n6613_o = ir == 8'b01001110;
  /* T80_MCode.vhd:1954:40  */
  assign n6614_o = n6611_o | n6613_o;
  /* T80_MCode.vhd:1954:51  */
  assign n6616_o = ir == 8'b01100110;
  /* T80_MCode.vhd:1954:51  */
  assign n6617_o = n6614_o | n6616_o;
  /* T80_MCode.vhd:1954:62  */
  assign n6619_o = ir == 8'b01101110;
  /* T80_MCode.vhd:1954:62  */
  assign n6620_o = n6617_o | n6619_o;
  /* T80_MCode.vhd:1957:25  */
  assign n6622_o = ir == 8'b01010110;
  /* T80_MCode.vhd:1957:40  */
  assign n6624_o = ir == 8'b01110110;
  /* T80_MCode.vhd:1957:40  */
  assign n6625_o = n6622_o | n6624_o;
  /* T80_MCode.vhd:1960:25  */
  assign n6627_o = ir == 8'b01011110;
  /* T80_MCode.vhd:1960:40  */
  assign n6629_o = ir == 8'b01111110;
  /* T80_MCode.vhd:1960:40  */
  assign n6630_o = n6627_o | n6629_o;
  /* T80_MCode.vhd:1967:38  */
  assign n6631_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:1968:33  */
  assign n6633_o = n6631_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:1976:68  */
  assign n6635_o = ir[5:4]; // extract
  /* T80_MCode.vhd:1976:46  */
  assign n6636_o = {29'b0, n6635_o};  //  uext
  /* T80_MCode.vhd:1978:78  */
  assign n6637_o = ir[5:4]; // extract
  /* T80_MCode.vhd:1977:41  */
  assign n6640_o = n6636_o == 31'b0000000000000000000000000000000;
  /* T80_MCode.vhd:1977:47  */
  assign n6642_o = n6636_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:1977:47  */
  assign n6643_o = n6640_o | n6642_o;
  /* T80_MCode.vhd:1977:49  */
  assign n6645_o = n6636_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:1977:49  */
  assign n6646_o = n6643_o | n6645_o;
  /* T80_MCode.vhd:1976:41  */
  always @*
    case (n6646_o)
      1'b1: n6648_o = 1'b1;
      default: n6648_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1976:41  */
  always @*
    case (n6646_o)
      1'b1: n6650_o = n6637_o;
      default: n6650_o = 2'b00;
    endcase
  /* T80_MCode.vhd:1976:41  */
  always @*
    case (n6646_o)
      1'b1: n6653_o = 1'b0;
      default: n6653_o = 1'b1;
    endcase
  /* T80_MCode.vhd:1970:33  */
  assign n6655_o = n6631_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:1992:68  */
  assign n6657_o = ir[5:4]; // extract
  /* T80_MCode.vhd:1992:46  */
  assign n6658_o = {29'b0, n6657_o};  //  uext
  /* T80_MCode.vhd:1994:78  */
  assign n6659_o = ir[5:4]; // extract
  /* T80_MCode.vhd:1993:41  */
  assign n6662_o = n6658_o == 31'b0000000000000000000000000000000;
  /* T80_MCode.vhd:1993:47  */
  assign n6664_o = n6658_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:1993:47  */
  assign n6665_o = n6662_o | n6664_o;
  /* T80_MCode.vhd:1993:49  */
  assign n6667_o = n6658_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:1993:49  */
  assign n6668_o = n6665_o | n6667_o;
  /* T80_MCode.vhd:1992:41  */
  always @*
    case (n6668_o)
      1'b1: n6670_o = 1'b0;
      default: n6670_o = 1'b1;
    endcase
  /* T80_MCode.vhd:1992:41  */
  always @*
    case (n6668_o)
      1'b1: n6672_o = n6659_o;
      default: n6672_o = 2'b00;
    endcase
  /* T80_MCode.vhd:1992:41  */
  always @*
    case (n6668_o)
      1'b1: n6675_o = 1'b0;
      default: n6675_o = 1'b1;
    endcase
  /* T80_MCode.vhd:1986:33  */
  assign n6677_o = n6631_o == 31'b0000000000000000000000000000011;
  assign n6678_o = {n6677_o, n6655_o, n6633_o};
  /* T80_MCode.vhd:1967:33  */
  always @*
    case (n6678_o)
      3'b100: n6680_o = n2497_o;
      3'b010: n6680_o = 3'b100;
      3'b001: n6680_o = n2497_o;
      default: n6680_o = n2497_o;
    endcase
  /* T80_MCode.vhd:1967:33  */
  always @*
    case (n6678_o)
      3'b100: n6684_o = 1'b1;
      3'b010: n6684_o = 1'b1;
      3'b001: n6684_o = 1'b0;
      default: n6684_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1967:33  */
  always @*
    case (n6678_o)
      3'b100: n6686_o = 3'b100;
      3'b010: n6686_o = 3'b101;
      3'b001: n6686_o = 3'b000;
      default: n6686_o = 3'b000;
    endcase
  /* T80_MCode.vhd:1967:33  */
  always @*
    case (n6678_o)
      3'b100: n6688_o = n6670_o;
      3'b010: n6688_o = n6648_o;
      3'b001: n6688_o = 1'b0;
      default: n6688_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1967:33  */
  always @*
    case (n6678_o)
      3'b100: n6690_o = n6672_o;
      3'b010: n6690_o = n6650_o;
      3'b001: n6690_o = 2'b00;
      default: n6690_o = 2'b00;
    endcase
  /* T80_MCode.vhd:1967:33  */
  always @*
    case (n6678_o)
      3'b100: n6692_o = n6675_o;
      3'b010: n6692_o = n6653_o;
      3'b001: n6692_o = 1'b0;
      default: n6692_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1967:33  */
  always @*
    case (n6678_o)
      3'b100: n6695_o = 4'b0001;
      3'b010: n6695_o = 4'b0001;
      3'b001: n6695_o = n2500_o;
      default: n6695_o = n2500_o;
    endcase
  /* T80_MCode.vhd:1967:33  */
  always @*
    case (n6678_o)
      3'b100: n6699_o = 1'b1;
      3'b010: n6699_o = 1'b1;
      3'b001: n6699_o = 1'b0;
      default: n6699_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1967:33  */
  always @*
    case (n6678_o)
      3'b100: n6702_o = 2'b00;
      3'b010: n6702_o = 2'b11;
      3'b001: n6702_o = 2'b00;
      default: n6702_o = 2'b00;
    endcase
  /* T80_MCode.vhd:1967:33  */
  always @*
    case (n6678_o)
      3'b100: n6706_o = 1'b1;
      3'b010: n6706_o = 1'b1;
      3'b001: n6706_o = 1'b0;
      default: n6706_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1967:33  */
  always @*
    case (n6678_o)
      3'b100: n6710_o = 1'b0;
      3'b010: n6710_o = 1'b1;
      3'b001: n6710_o = 1'b1;
      default: n6710_o = 1'b0;
    endcase
  /* T80_MCode.vhd:1964:25  */
  assign n6712_o = ir == 8'b01001010;
  /* T80_MCode.vhd:1964:40  */
  assign n6714_o = ir == 8'b01011010;
  /* T80_MCode.vhd:1964:40  */
  assign n6715_o = n6712_o | n6714_o;
  /* T80_MCode.vhd:1964:51  */
  assign n6717_o = ir == 8'b01101010;
  /* T80_MCode.vhd:1964:51  */
  assign n6718_o = n6715_o | n6717_o;
  /* T80_MCode.vhd:1964:62  */
  assign n6720_o = ir == 8'b01111010;
  /* T80_MCode.vhd:1964:62  */
  assign n6721_o = n6718_o | n6720_o;
  /* T80_MCode.vhd:2004:38  */
  assign n6722_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:2005:33  */
  assign n6724_o = n6722_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:2013:68  */
  assign n6726_o = ir[5:4]; // extract
  /* T80_MCode.vhd:2013:46  */
  assign n6727_o = {29'b0, n6726_o};  //  uext
  /* T80_MCode.vhd:2015:78  */
  assign n6728_o = ir[5:4]; // extract
  /* T80_MCode.vhd:2014:41  */
  assign n6731_o = n6727_o == 31'b0000000000000000000000000000000;
  /* T80_MCode.vhd:2014:47  */
  assign n6733_o = n6727_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:2014:47  */
  assign n6734_o = n6731_o | n6733_o;
  /* T80_MCode.vhd:2014:49  */
  assign n6736_o = n6727_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:2014:49  */
  assign n6737_o = n6734_o | n6736_o;
  /* T80_MCode.vhd:2013:41  */
  always @*
    case (n6737_o)
      1'b1: n6739_o = 1'b1;
      default: n6739_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2013:41  */
  always @*
    case (n6737_o)
      1'b1: n6741_o = n6728_o;
      default: n6741_o = 2'b00;
    endcase
  /* T80_MCode.vhd:2013:41  */
  always @*
    case (n6737_o)
      1'b1: n6744_o = 1'b0;
      default: n6744_o = 1'b1;
    endcase
  /* T80_MCode.vhd:2007:33  */
  assign n6746_o = n6722_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:2029:68  */
  assign n6748_o = ir[5:4]; // extract
  /* T80_MCode.vhd:2029:46  */
  assign n6749_o = {29'b0, n6748_o};  //  uext
  /* T80_MCode.vhd:2031:78  */
  assign n6750_o = ir[5:4]; // extract
  /* T80_MCode.vhd:2030:41  */
  assign n6752_o = n6749_o == 31'b0000000000000000000000000000000;
  /* T80_MCode.vhd:2030:47  */
  assign n6754_o = n6749_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:2030:47  */
  assign n6755_o = n6752_o | n6754_o;
  /* T80_MCode.vhd:2030:49  */
  assign n6757_o = n6749_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:2030:49  */
  assign n6758_o = n6755_o | n6757_o;
  /* T80_MCode.vhd:2029:41  */
  always @*
    case (n6758_o)
      1'b1: n6761_o = 1'b0;
      default: n6761_o = 1'b1;
    endcase
  /* T80_MCode.vhd:2029:41  */
  always @*
    case (n6758_o)
      1'b1: n6763_o = n6750_o;
      default: n6763_o = 2'b00;
    endcase
  /* T80_MCode.vhd:2029:41  */
  always @*
    case (n6758_o)
      1'b1: n6766_o = 1'b0;
      default: n6766_o = 1'b1;
    endcase
  /* T80_MCode.vhd:2023:33  */
  assign n6768_o = n6722_o == 31'b0000000000000000000000000000011;
  assign n6769_o = {n6768_o, n6746_o, n6724_o};
  /* T80_MCode.vhd:2004:33  */
  always @*
    case (n6769_o)
      3'b100: n6771_o = n2497_o;
      3'b010: n6771_o = 3'b100;
      3'b001: n6771_o = n2497_o;
      default: n6771_o = n2497_o;
    endcase
  /* T80_MCode.vhd:2004:33  */
  always @*
    case (n6769_o)
      3'b100: n6775_o = 1'b1;
      3'b010: n6775_o = 1'b1;
      3'b001: n6775_o = 1'b0;
      default: n6775_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2004:33  */
  always @*
    case (n6769_o)
      3'b100: n6777_o = 3'b100;
      3'b010: n6777_o = 3'b101;
      3'b001: n6777_o = 3'b000;
      default: n6777_o = 3'b000;
    endcase
  /* T80_MCode.vhd:2004:33  */
  always @*
    case (n6769_o)
      3'b100: n6779_o = n6761_o;
      3'b010: n6779_o = n6739_o;
      3'b001: n6779_o = 1'b0;
      default: n6779_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2004:33  */
  always @*
    case (n6769_o)
      3'b100: n6781_o = n6763_o;
      3'b010: n6781_o = n6741_o;
      3'b001: n6781_o = 2'b00;
      default: n6781_o = 2'b00;
    endcase
  /* T80_MCode.vhd:2004:33  */
  always @*
    case (n6769_o)
      3'b100: n6783_o = n6766_o;
      3'b010: n6783_o = n6744_o;
      3'b001: n6783_o = 1'b0;
      default: n6783_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2004:33  */
  always @*
    case (n6769_o)
      3'b100: n6786_o = 4'b0011;
      3'b010: n6786_o = 4'b0011;
      3'b001: n6786_o = n2500_o;
      default: n6786_o = n2500_o;
    endcase
  /* T80_MCode.vhd:2004:33  */
  always @*
    case (n6769_o)
      3'b100: n6790_o = 1'b1;
      3'b010: n6790_o = 1'b1;
      3'b001: n6790_o = 1'b0;
      default: n6790_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2004:33  */
  always @*
    case (n6769_o)
      3'b100: n6793_o = 2'b00;
      3'b010: n6793_o = 2'b11;
      3'b001: n6793_o = 2'b00;
      default: n6793_o = 2'b00;
    endcase
  /* T80_MCode.vhd:2004:33  */
  always @*
    case (n6769_o)
      3'b100: n6797_o = 1'b1;
      3'b010: n6797_o = 1'b1;
      3'b001: n6797_o = 1'b0;
      default: n6797_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2004:33  */
  always @*
    case (n6769_o)
      3'b100: n6801_o = 1'b0;
      3'b010: n6801_o = 1'b1;
      3'b001: n6801_o = 1'b1;
      default: n6801_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2001:25  */
  assign n6803_o = ir == 8'b01000010;
  /* T80_MCode.vhd:2001:40  */
  assign n6805_o = ir == 8'b01010010;
  /* T80_MCode.vhd:2001:40  */
  assign n6806_o = n6803_o | n6805_o;
  /* T80_MCode.vhd:2001:51  */
  assign n6808_o = ir == 8'b01100010;
  /* T80_MCode.vhd:2001:51  */
  assign n6809_o = n6806_o | n6808_o;
  /* T80_MCode.vhd:2001:62  */
  assign n6811_o = ir == 8'b01110010;
  /* T80_MCode.vhd:2001:62  */
  assign n6812_o = n6809_o | n6811_o;
  /* T80_MCode.vhd:2040:38  */
  assign n6813_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:2041:33  */
  assign n6815_o = n6813_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:2043:33  */
  assign n6819_o = n6813_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:2050:33  */
  assign n6821_o = n6813_o == 31'b0000000000000000000000000000011;
  /* T80_MCode.vhd:2055:33  */
  assign n6823_o = n6813_o == 31'b0000000000000000000000000000100;
  assign n6824_o = {n6823_o, n6821_o, n6819_o, n6815_o};
  /* T80_MCode.vhd:2040:33  */
  always @*
    case (n6824_o)
      4'b1000: n6826_o = n2497_o;
      4'b0100: n6826_o = 3'b100;
      4'b0010: n6826_o = n2497_o;
      4'b0001: n6826_o = n2497_o;
      default: n6826_o = n2497_o;
    endcase
  /* T80_MCode.vhd:2040:33  */
  always @*
    case (n6824_o)
      4'b1000: n6829_o = 1'b0;
      4'b0100: n6829_o = 1'b0;
      4'b0010: n6829_o = 1'b1;
      4'b0001: n6829_o = 1'b0;
      default: n6829_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2040:33  */
  always @*
    case (n6824_o)
      4'b1000: n6831_o = 3'b000;
      4'b0100: n6831_o = 3'b000;
      4'b0010: n6831_o = 3'b111;
      4'b0001: n6831_o = 3'b000;
      default: n6831_o = 3'b000;
    endcase
  /* T80_MCode.vhd:2040:33  */
  always @*
    case (n6824_o)
      4'b1000: n6833_o = 3'b000;
      4'b0100: n6833_o = 3'b000;
      4'b0010: n6833_o = 3'b110;
      4'b0001: n6833_o = 3'b000;
      default: n6833_o = 3'b000;
    endcase
  /* T80_MCode.vhd:2040:33  */
  always @*
    case (n6824_o)
      4'b1000: n6835_o = n2500_o;
      4'b0100: n6835_o = n2500_o;
      4'b0010: n6835_o = 4'b1101;
      4'b0001: n6835_o = n2500_o;
      default: n6835_o = n2500_o;
    endcase
  /* T80_MCode.vhd:2040:33  */
  always @*
    case (n6824_o)
      4'b1000: n6838_o = 1'b0;
      4'b0100: n6838_o = 1'b0;
      4'b0010: n6838_o = 1'b1;
      4'b0001: n6838_o = 1'b0;
      default: n6838_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2040:33  */
  always @*
    case (n6824_o)
      4'b1000: n6842_o = 3'b111;
      4'b0100: n6842_o = 3'b010;
      4'b0010: n6842_o = 3'b111;
      4'b0001: n6842_o = 3'b010;
      default: n6842_o = 3'b111;
    endcase
  /* T80_MCode.vhd:2040:33  */
  always @*
    case (n6824_o)
      4'b1000: n6845_o = 1'b0;
      4'b0100: n6845_o = 1'b1;
      4'b0010: n6845_o = 1'b0;
      4'b0001: n6845_o = 1'b0;
      default: n6845_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2040:33  */
  always @*
    case (n6824_o)
      4'b1000: n6848_o = 1'b0;
      4'b0100: n6848_o = 1'b1;
      4'b0010: n6848_o = 1'b0;
      4'b0001: n6848_o = 1'b0;
      default: n6848_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2040:33  */
  always @*
    case (n6824_o)
      4'b1000: n6851_o = 1'b1;
      4'b0100: n6851_o = 1'b0;
      4'b0010: n6851_o = 1'b0;
      4'b0001: n6851_o = 1'b0;
      default: n6851_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2040:33  */
  always @*
    case (n6824_o)
      4'b1000: n6854_o = 1'b0;
      4'b0100: n6854_o = 1'b0;
      4'b0010: n6854_o = 1'b1;
      4'b0001: n6854_o = 1'b0;
      default: n6854_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2037:25  */
  assign n6856_o = ir == 8'b01101111;
  /* T80_MCode.vhd:2062:38  */
  assign n6857_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:2063:33  */
  assign n6859_o = n6857_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:2065:33  */
  assign n6863_o = n6857_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:2072:33  */
  assign n6865_o = n6857_o == 31'b0000000000000000000000000000011;
  /* T80_MCode.vhd:2077:33  */
  assign n6867_o = n6857_o == 31'b0000000000000000000000000000100;
  assign n6868_o = {n6867_o, n6865_o, n6863_o, n6859_o};
  /* T80_MCode.vhd:2062:33  */
  always @*
    case (n6868_o)
      4'b1000: n6870_o = n2497_o;
      4'b0100: n6870_o = 3'b100;
      4'b0010: n6870_o = n2497_o;
      4'b0001: n6870_o = n2497_o;
      default: n6870_o = n2497_o;
    endcase
  /* T80_MCode.vhd:2062:33  */
  always @*
    case (n6868_o)
      4'b1000: n6873_o = 1'b0;
      4'b0100: n6873_o = 1'b0;
      4'b0010: n6873_o = 1'b1;
      4'b0001: n6873_o = 1'b0;
      default: n6873_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2062:33  */
  always @*
    case (n6868_o)
      4'b1000: n6875_o = 3'b000;
      4'b0100: n6875_o = 3'b000;
      4'b0010: n6875_o = 3'b111;
      4'b0001: n6875_o = 3'b000;
      default: n6875_o = 3'b000;
    endcase
  /* T80_MCode.vhd:2062:33  */
  always @*
    case (n6868_o)
      4'b1000: n6877_o = 3'b000;
      4'b0100: n6877_o = 3'b000;
      4'b0010: n6877_o = 3'b110;
      4'b0001: n6877_o = 3'b000;
      default: n6877_o = 3'b000;
    endcase
  /* T80_MCode.vhd:2062:33  */
  always @*
    case (n6868_o)
      4'b1000: n6879_o = n2500_o;
      4'b0100: n6879_o = n2500_o;
      4'b0010: n6879_o = 4'b1110;
      4'b0001: n6879_o = n2500_o;
      default: n6879_o = n2500_o;
    endcase
  /* T80_MCode.vhd:2062:33  */
  always @*
    case (n6868_o)
      4'b1000: n6882_o = 1'b0;
      4'b0100: n6882_o = 1'b0;
      4'b0010: n6882_o = 1'b1;
      4'b0001: n6882_o = 1'b0;
      default: n6882_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2062:33  */
  always @*
    case (n6868_o)
      4'b1000: n6886_o = 3'b111;
      4'b0100: n6886_o = 3'b010;
      4'b0010: n6886_o = 3'b111;
      4'b0001: n6886_o = 3'b010;
      default: n6886_o = 3'b111;
    endcase
  /* T80_MCode.vhd:2062:33  */
  always @*
    case (n6868_o)
      4'b1000: n6889_o = 1'b0;
      4'b0100: n6889_o = 1'b1;
      4'b0010: n6889_o = 1'b0;
      4'b0001: n6889_o = 1'b0;
      default: n6889_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2062:33  */
  always @*
    case (n6868_o)
      4'b1000: n6892_o = 1'b0;
      4'b0100: n6892_o = 1'b1;
      4'b0010: n6892_o = 1'b0;
      4'b0001: n6892_o = 1'b0;
      default: n6892_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2062:33  */
  always @*
    case (n6868_o)
      4'b1000: n6895_o = 1'b1;
      4'b0100: n6895_o = 1'b0;
      4'b0010: n6895_o = 1'b0;
      4'b0001: n6895_o = 1'b0;
      default: n6895_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2062:33  */
  always @*
    case (n6868_o)
      4'b1000: n6898_o = 1'b0;
      4'b0100: n6898_o = 1'b0;
      4'b0010: n6898_o = 1'b1;
      4'b0001: n6898_o = 1'b0;
      default: n6898_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2059:25  */
  assign n6900_o = ir == 8'b01100111;
  /* T80_MCode.vhd:2084:38  */
  assign n6901_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:2085:33  */
  assign n6903_o = n6901_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:2087:33  */
  assign n6905_o = n6901_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:2091:33  */
  assign n6907_o = n6901_o == 31'b0000000000000000000000000000011;
  assign n6908_o = {n6907_o, n6905_o, n6903_o};
  /* T80_MCode.vhd:2084:33  */
  always @*
    case (n6908_o)
      3'b100: n6912_o = 4'b0111;
      3'b010: n6912_o = 4'b0111;
      3'b001: n6912_o = 4'b0000;
      default: n6912_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:2084:33  */
  always @*
    case (n6908_o)
      3'b100: n6916_o = 3'b111;
      3'b010: n6916_o = 3'b101;
      3'b001: n6916_o = 3'b101;
      default: n6916_o = 3'b111;
    endcase
  /* T80_MCode.vhd:2084:33  */
  always @*
    case (n6908_o)
      3'b100: n6919_o = 1'b1;
      3'b010: n6919_o = 1'b0;
      3'b001: n6919_o = 1'b0;
      default: n6919_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2084:33  */
  always @*
    case (n6908_o)
      3'b100: n6922_o = 1'b0;
      3'b010: n6922_o = 1'b1;
      3'b001: n6922_o = 1'b0;
      default: n6922_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2084:33  */
  always @*
    case (n6908_o)
      3'b100: n6925_o = 1'b1;
      3'b010: n6925_o = 1'b0;
      3'b001: n6925_o = 1'b0;
      default: n6925_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2084:33  */
  always @*
    case (n6908_o)
      3'b100: n6928_o = 1'b1;
      3'b010: n6928_o = 1'b0;
      3'b001: n6928_o = 1'b0;
      default: n6928_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2081:25  */
  assign n6930_o = ir == 8'b01000101;
  /* T80_MCode.vhd:2081:40  */
  assign n6932_o = ir == 8'b01001101;
  /* T80_MCode.vhd:2081:40  */
  assign n6933_o = n6930_o | n6932_o;
  /* T80_MCode.vhd:2081:51  */
  assign n6935_o = ir == 8'b01010101;
  /* T80_MCode.vhd:2081:51  */
  assign n6936_o = n6933_o | n6935_o;
  /* T80_MCode.vhd:2081:62  */
  assign n6938_o = ir == 8'b01011101;
  /* T80_MCode.vhd:2081:62  */
  assign n6939_o = n6936_o | n6938_o;
  /* T80_MCode.vhd:2081:73  */
  assign n6941_o = ir == 8'b01100101;
  /* T80_MCode.vhd:2081:73  */
  assign n6942_o = n6939_o | n6941_o;
  /* T80_MCode.vhd:2081:84  */
  assign n6944_o = ir == 8'b01101101;
  /* T80_MCode.vhd:2081:84  */
  assign n6945_o = n6942_o | n6944_o;
  /* T80_MCode.vhd:2081:95  */
  assign n6947_o = ir == 8'b01110101;
  /* T80_MCode.vhd:2081:95  */
  assign n6948_o = n6945_o | n6947_o;
  /* T80_MCode.vhd:2081:106  */
  assign n6950_o = ir == 8'b01111101;
  /* T80_MCode.vhd:2081:106  */
  assign n6951_o = n6948_o | n6950_o;
  /* T80_MCode.vhd:2101:38  */
  assign n6952_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:2102:33  */
  assign n6954_o = n6952_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:2107:46  */
  assign n6955_o = ir[5:3]; // extract
  /* T80_MCode.vhd:2107:59  */
  assign n6957_o = n6955_o != 3'b110;
  /* T80_MCode.vhd:2109:78  */
  assign n6958_o = ir[5:3]; // extract
  /* T80_MCode.vhd:2107:41  */
  assign n6961_o = n6957_o ? 1'b1 : 1'b0;
  /* T80_MCode.vhd:2107:41  */
  assign n6963_o = n6957_o ? n6958_o : 3'b000;
  /* T80_MCode.vhd:2105:33  */
  assign n6965_o = n6952_o == 31'b0000000000000000000000000000010;
  assign n6966_o = {n6965_o, n6954_o};
  /* T80_MCode.vhd:2101:33  */
  always @*
    case (n6966_o)
      2'b10: n6968_o = n6961_o;
      2'b01: n6968_o = 1'b0;
      default: n6968_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2101:33  */
  always @*
    case (n6966_o)
      2'b10: n6970_o = n6963_o;
      2'b01: n6970_o = 3'b000;
      default: n6970_o = 3'b000;
    endcase
  /* T80_MCode.vhd:2101:33  */
  always @*
    case (n6966_o)
      2'b10: n6973_o = 3'b111;
      2'b01: n6973_o = 3'b000;
      default: n6973_o = 3'b111;
    endcase
  /* T80_MCode.vhd:2101:33  */
  always @*
    case (n6966_o)
      2'b10: n6976_o = 1'b1;
      2'b01: n6976_o = 1'b0;
      default: n6976_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2101:33  */
  always @*
    case (n6966_o)
      2'b10: n6979_o = 1'b1;
      2'b01: n6979_o = 1'b0;
      default: n6979_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2101:33  */
  always @*
    case (n6966_o)
      2'b10: n6982_o = 2'b00;
      2'b01: n6982_o = 2'b01;
      default: n6982_o = 2'b00;
    endcase
  /* T80_MCode.vhd:2098:25  */
  assign n6984_o = ir == 8'b01000000;
  /* T80_MCode.vhd:2098:40  */
  assign n6986_o = ir == 8'b01001000;
  /* T80_MCode.vhd:2098:40  */
  assign n6987_o = n6984_o | n6986_o;
  /* T80_MCode.vhd:2098:51  */
  assign n6989_o = ir == 8'b01010000;
  /* T80_MCode.vhd:2098:51  */
  assign n6990_o = n6987_o | n6989_o;
  /* T80_MCode.vhd:2098:62  */
  assign n6992_o = ir == 8'b01011000;
  /* T80_MCode.vhd:2098:62  */
  assign n6993_o = n6990_o | n6992_o;
  /* T80_MCode.vhd:2098:73  */
  assign n6995_o = ir == 8'b01100000;
  /* T80_MCode.vhd:2098:73  */
  assign n6996_o = n6993_o | n6995_o;
  /* T80_MCode.vhd:2098:84  */
  assign n6998_o = ir == 8'b01101000;
  /* T80_MCode.vhd:2098:84  */
  assign n6999_o = n6996_o | n6998_o;
  /* T80_MCode.vhd:2098:95  */
  assign n7001_o = ir == 8'b01110000;
  /* T80_MCode.vhd:2098:95  */
  assign n7002_o = n6999_o | n7001_o;
  /* T80_MCode.vhd:2098:106  */
  assign n7004_o = ir == 8'b01111000;
  /* T80_MCode.vhd:2098:106  */
  assign n7005_o = n7002_o | n7004_o;
  /* T80_MCode.vhd:2118:38  */
  assign n7006_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:2122:78  */
  assign n7007_o = ir[5:3]; // extract
  /* T80_MCode.vhd:2123:46  */
  assign n7008_o = ir[5:3]; // extract
  /* T80_MCode.vhd:2123:59  */
  assign n7010_o = n7008_o == 3'b110;
  /* T80_MCode.vhd:2123:41  */
  assign n7013_o = n7010_o ? 1'b1 : 1'b0;
  /* T80_MCode.vhd:2119:33  */
  assign n7015_o = n7006_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:2126:33  */
  assign n7017_o = n7006_o == 31'b0000000000000000000000000000010;
  assign n7018_o = {n7017_o, n7015_o};
  /* T80_MCode.vhd:2118:33  */
  always @*
    case (n7018_o)
      2'b10: n7020_o = 3'b000;
      2'b01: n7020_o = n7007_o;
      default: n7020_o = 3'b000;
    endcase
  /* T80_MCode.vhd:2118:33  */
  always @*
    case (n7018_o)
      2'b10: n7022_o = 1'b0;
      2'b01: n7022_o = n7013_o;
      default: n7022_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2118:33  */
  always @*
    case (n7018_o)
      2'b10: n7025_o = 3'b111;
      2'b01: n7025_o = 3'b000;
      default: n7025_o = 3'b111;
    endcase
  /* T80_MCode.vhd:2118:33  */
  always @*
    case (n7018_o)
      2'b10: n7028_o = 1'b1;
      2'b01: n7028_o = 1'b0;
      default: n7028_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2118:33  */
  always @*
    case (n7018_o)
      2'b10: n7031_o = 2'b00;
      2'b01: n7031_o = 2'b01;
      default: n7031_o = 2'b00;
    endcase
  /* T80_MCode.vhd:2118:33  */
  always @*
    case (n7018_o)
      2'b10: n7034_o = 1'b1;
      2'b01: n7034_o = 1'b0;
      default: n7034_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2114:25  */
  assign n7036_o = ir == 8'b01000001;
  /* T80_MCode.vhd:2114:40  */
  assign n7038_o = ir == 8'b01001001;
  /* T80_MCode.vhd:2114:40  */
  assign n7039_o = n7036_o | n7038_o;
  /* T80_MCode.vhd:2114:51  */
  assign n7041_o = ir == 8'b01010001;
  /* T80_MCode.vhd:2114:51  */
  assign n7042_o = n7039_o | n7041_o;
  /* T80_MCode.vhd:2114:62  */
  assign n7044_o = ir == 8'b01011001;
  /* T80_MCode.vhd:2114:62  */
  assign n7045_o = n7042_o | n7044_o;
  /* T80_MCode.vhd:2114:73  */
  assign n7047_o = ir == 8'b01100001;
  /* T80_MCode.vhd:2114:73  */
  assign n7048_o = n7045_o | n7047_o;
  /* T80_MCode.vhd:2114:84  */
  assign n7050_o = ir == 8'b01101001;
  /* T80_MCode.vhd:2114:84  */
  assign n7051_o = n7048_o | n7050_o;
  /* T80_MCode.vhd:2114:95  */
  assign n7053_o = ir == 8'b01110001;
  /* T80_MCode.vhd:2114:95  */
  assign n7054_o = n7051_o | n7053_o;
  /* T80_MCode.vhd:2114:106  */
  assign n7056_o = ir == 8'b01111001;
  /* T80_MCode.vhd:2114:106  */
  assign n7057_o = n7054_o | n7056_o;
  /* T80_MCode.vhd:2134:38  */
  assign n7058_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:2144:59  */
  assign n7059_o = ir[3]; // extract
  /* T80_MCode.vhd:2135:33  */
  assign n7061_o = n7058_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:2145:33  */
  assign n7063_o = n7058_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:2150:46  */
  assign n7064_o = ir[3]; // extract
  /* T80_MCode.vhd:2150:50  */
  assign n7065_o = ~n7064_o;
  /* T80_MCode.vhd:2150:41  */
  assign n7068_o = n7065_o ? 4'b0110 : 4'b1110;
  /* T80_MCode.vhd:2149:33  */
  assign n7070_o = n7058_o == 31'b0000000000000000000000000000011;
  /* T80_MCode.vhd:2157:33  */
  assign n7072_o = n7058_o == 31'b0000000000000000000000000000100;
  assign n7073_o = {n7072_o, n7070_o, n7063_o, n7061_o};
  /* T80_MCode.vhd:2134:33  */
  always @*
    case (n7073_o)
      4'b1000: n7076_o = 3'b101;
      4'b0100: n7076_o = n2497_o;
      4'b0010: n7076_o = n2497_o;
      4'b0001: n7076_o = 3'b101;
      default: n7076_o = n2497_o;
    endcase
  assign n7077_o = n7068_o[2:0]; // extract
  /* T80_MCode.vhd:2134:33  */
  always @*
    case (n7073_o)
      4'b1000: n7079_o = 3'b000;
      4'b0100: n7079_o = n7077_o;
      4'b0010: n7079_o = 3'b000;
      4'b0001: n7079_o = 3'b000;
      default: n7079_o = 3'b000;
    endcase
  assign n7080_o = n7068_o[3]; // extract
  /* T80_MCode.vhd:2134:33  */
  always @*
    case (n7073_o)
      4'b1000: n7082_o = 1'b0;
      4'b0100: n7082_o = n7080_o;
      4'b0010: n7082_o = 1'b0;
      4'b0001: n7082_o = n7059_o;
      default: n7082_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2134:33  */
  always @*
    case (n7073_o)
      4'b1000: n7085_o = 1'b0;
      4'b0100: n7085_o = 1'b0;
      4'b0010: n7085_o = 1'b0;
      4'b0001: n7085_o = 1'b1;
      default: n7085_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2134:33  */
  always @*
    case (n7073_o)
      4'b1000: n7088_o = 4'b0000;
      4'b0100: n7088_o = 4'b0000;
      4'b0010: n7088_o = 4'b0000;
      4'b0001: n7088_o = 4'b0000;
      default: n7088_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:2134:33  */
  always @*
    case (n7073_o)
      4'b1000: n7092_o = 4'b0000;
      4'b0100: n7092_o = 4'b0000;
      4'b0010: n7092_o = 4'b0110;
      4'b0001: n7092_o = 4'b1010;
      default: n7092_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:2134:33  */
  always @*
    case (n7073_o)
      4'b1000: n7094_o = n2500_o;
      4'b0100: n7094_o = n2500_o;
      4'b0010: n7094_o = n2500_o;
      4'b0001: n7094_o = 4'b0010;
      default: n7094_o = n2500_o;
    endcase
  /* T80_MCode.vhd:2134:33  */
  always @*
    case (n7073_o)
      4'b1000: n7097_o = 1'b0;
      4'b0100: n7097_o = 1'b0;
      4'b0010: n7097_o = 1'b0;
      4'b0001: n7097_o = 1'b1;
      default: n7097_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2134:33  */
  always @*
    case (n7073_o)
      4'b1000: n7101_o = 3'b111;
      4'b0100: n7101_o = 3'b111;
      4'b0010: n7101_o = 3'b010;
      4'b0001: n7101_o = 3'b000;
      default: n7101_o = 3'b111;
    endcase
  /* T80_MCode.vhd:2134:33  */
  always @*
    case (n7073_o)
      4'b1000: n7104_o = 1'b0;
      4'b0100: n7104_o = 1'b0;
      4'b0010: n7104_o = 1'b1;
      4'b0001: n7104_o = 1'b0;
      default: n7104_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2134:33  */
  always @*
    case (n7073_o)
      4'b1000: n7107_o = 1'b0;
      4'b0100: n7107_o = 1'b1;
      4'b0010: n7107_o = 1'b0;
      4'b0001: n7107_o = 1'b0;
      default: n7107_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2134:33  */
  always @*
    case (n7073_o)
      4'b1000: n7110_o = 2'b00;
      4'b0100: n7110_o = 2'b00;
      4'b0010: n7110_o = 2'b00;
      4'b0001: n7110_o = 2'b11;
      default: n7110_o = 2'b00;
    endcase
  /* T80_MCode.vhd:2134:33  */
  always @*
    case (n7073_o)
      4'b1000: n7113_o = 1'b1;
      4'b0100: n7113_o = 1'b0;
      4'b0010: n7113_o = 1'b0;
      4'b0001: n7113_o = 1'b0;
      default: n7113_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2134:33  */
  always @*
    case (n7073_o)
      4'b1000: n7116_o = 1'b0;
      4'b0100: n7116_o = 1'b1;
      4'b0010: n7116_o = 1'b0;
      4'b0001: n7116_o = 1'b0;
      default: n7116_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2131:25  */
  assign n7118_o = ir == 8'b10100010;
  /* T80_MCode.vhd:2131:41  */
  assign n7120_o = ir == 8'b10101010;
  /* T80_MCode.vhd:2131:41  */
  assign n7121_o = n7118_o | n7120_o;
  /* T80_MCode.vhd:2131:54  */
  assign n7123_o = ir == 8'b10110010;
  /* T80_MCode.vhd:2131:54  */
  assign n7124_o = n7121_o | n7123_o;
  /* T80_MCode.vhd:2131:67  */
  assign n7126_o = ir == 8'b10111010;
  /* T80_MCode.vhd:2131:67  */
  assign n7127_o = n7124_o | n7126_o;
  /* T80_MCode.vhd:2165:38  */
  assign n7128_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:2166:33  */
  assign n7130_o = n7128_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:2178:59  */
  assign n7131_o = ir[3]; // extract
  /* T80_MCode.vhd:2174:33  */
  assign n7133_o = n7128_o == 31'b0000000000000000000000000000010;
  /* T80_MCode.vhd:2180:46  */
  assign n7134_o = ir[3]; // extract
  /* T80_MCode.vhd:2180:50  */
  assign n7135_o = ~n7134_o;
  /* T80_MCode.vhd:2180:41  */
  assign n7138_o = n7135_o ? 4'b0110 : 4'b1110;
  /* T80_MCode.vhd:2179:33  */
  assign n7140_o = n7128_o == 31'b0000000000000000000000000000011;
  /* T80_MCode.vhd:2188:33  */
  assign n7142_o = n7128_o == 31'b0000000000000000000000000000100;
  assign n7143_o = {n7142_o, n7140_o, n7133_o, n7130_o};
  /* T80_MCode.vhd:2165:33  */
  always @*
    case (n7143_o)
      4'b1000: n7146_o = 3'b101;
      4'b0100: n7146_o = n2497_o;
      4'b0010: n7146_o = n2497_o;
      4'b0001: n7146_o = 3'b101;
      default: n7146_o = n2497_o;
    endcase
  assign n7147_o = n7138_o[2:0]; // extract
  /* T80_MCode.vhd:2165:33  */
  always @*
    case (n7143_o)
      4'b1000: n7149_o = 3'b000;
      4'b0100: n7149_o = n7147_o;
      4'b0010: n7149_o = 3'b000;
      4'b0001: n7149_o = 3'b000;
      default: n7149_o = 3'b000;
    endcase
  assign n7150_o = n7138_o[3]; // extract
  /* T80_MCode.vhd:2165:33  */
  always @*
    case (n7143_o)
      4'b1000: n7152_o = 1'b0;
      4'b0100: n7152_o = n7150_o;
      4'b0010: n7152_o = n7131_o;
      4'b0001: n7152_o = 1'b0;
      default: n7152_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2165:33  */
  always @*
    case (n7143_o)
      4'b1000: n7155_o = 1'b0;
      4'b0100: n7155_o = 1'b0;
      4'b0010: n7155_o = 1'b0;
      4'b0001: n7155_o = 1'b1;
      default: n7155_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2165:33  */
  always @*
    case (n7143_o)
      4'b1000: n7158_o = 4'b0000;
      4'b0100: n7158_o = 4'b0000;
      4'b0010: n7158_o = 4'b0000;
      4'b0001: n7158_o = 4'b0000;
      default: n7158_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:2165:33  */
  always @*
    case (n7143_o)
      4'b1000: n7162_o = 4'b0000;
      4'b0100: n7162_o = 4'b0000;
      4'b0010: n7162_o = 4'b0110;
      4'b0001: n7162_o = 4'b1010;
      default: n7162_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:2165:33  */
  always @*
    case (n7143_o)
      4'b1000: n7164_o = n2500_o;
      4'b0100: n7164_o = n2500_o;
      4'b0010: n7164_o = n2500_o;
      4'b0001: n7164_o = 4'b0010;
      default: n7164_o = n2500_o;
    endcase
  /* T80_MCode.vhd:2165:33  */
  always @*
    case (n7143_o)
      4'b1000: n7167_o = 1'b0;
      4'b0100: n7167_o = 1'b0;
      4'b0010: n7167_o = 1'b0;
      4'b0001: n7167_o = 1'b1;
      default: n7167_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2165:33  */
  always @*
    case (n7143_o)
      4'b1000: n7171_o = 3'b111;
      4'b0100: n7171_o = 3'b111;
      4'b0010: n7171_o = 3'b000;
      4'b0001: n7171_o = 3'b010;
      default: n7171_o = 3'b111;
    endcase
  /* T80_MCode.vhd:2165:33  */
  always @*
    case (n7143_o)
      4'b1000: n7174_o = 1'b0;
      4'b0100: n7174_o = 1'b1;
      4'b0010: n7174_o = 1'b0;
      4'b0001: n7174_o = 1'b0;
      default: n7174_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2165:33  */
  always @*
    case (n7143_o)
      4'b1000: n7177_o = 1'b0;
      4'b0100: n7177_o = 1'b1;
      4'b0010: n7177_o = 1'b0;
      4'b0001: n7177_o = 1'b0;
      default: n7177_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2165:33  */
  always @*
    case (n7143_o)
      4'b1000: n7180_o = 2'b00;
      4'b0100: n7180_o = 2'b00;
      4'b0010: n7180_o = 2'b11;
      4'b0001: n7180_o = 2'b00;
      default: n7180_o = 2'b00;
    endcase
  /* T80_MCode.vhd:2165:33  */
  always @*
    case (n7143_o)
      4'b1000: n7183_o = 1'b1;
      4'b0100: n7183_o = 1'b0;
      4'b0010: n7183_o = 1'b0;
      4'b0001: n7183_o = 1'b0;
      default: n7183_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2165:33  */
  always @*
    case (n7143_o)
      4'b1000: n7186_o = 1'b0;
      4'b0100: n7186_o = 1'b1;
      4'b0010: n7186_o = 1'b0;
      4'b0001: n7186_o = 1'b0;
      default: n7186_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2162:25  */
  assign n7188_o = ir == 8'b10100011;
  /* T80_MCode.vhd:2162:41  */
  assign n7190_o = ir == 8'b10101011;
  /* T80_MCode.vhd:2162:41  */
  assign n7191_o = n7188_o | n7190_o;
  /* T80_MCode.vhd:2162:54  */
  assign n7193_o = ir == 8'b10110011;
  /* T80_MCode.vhd:2162:54  */
  assign n7194_o = n7191_o | n7193_o;
  /* T80_MCode.vhd:2162:67  */
  assign n7196_o = ir == 8'b10111011;
  /* T80_MCode.vhd:2162:67  */
  assign n7197_o = n7194_o | n7196_o;
  /* T80_MCode.vhd:2197:46  */
  assign n7198_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:2201:78  */
  assign n7199_o = ir[5:3]; // extract
  /* T80_MCode.vhd:2198:41  */
  assign n7202_o = n7198_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:2203:41  */
  assign n7205_o = n7198_o == 31'b0000000000000000000000000000010;
  assign n7206_o = {n7205_o, n7202_o};
  /* T80_MCode.vhd:2197:41  */
  always @*
    case (n7206_o)
      2'b10: n7208_o = 3'b100;
      2'b01: n7208_o = 3'b000;
      default: n7208_o = 3'b000;
    endcase
  /* T80_MCode.vhd:2197:41  */
  always @*
    case (n7206_o)
      2'b10: n7210_o = 3'b000;
      2'b01: n7210_o = n7199_o;
      default: n7210_o = 3'b000;
    endcase
  /* T80_MCode.vhd:2197:41  */
  always @*
    case (n7206_o)
      2'b10: n7212_o = 1'b0;
      2'b01: n7212_o = 1'b0;
      default: n7212_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2197:41  */
  always @*
    case (n7206_o)
      2'b10: n7215_o = 1'b0;
      2'b01: n7215_o = 1'b1;
      default: n7215_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2197:41  */
  always @*
    case (n7206_o)
      2'b10: n7218_o = 1'b1;
      2'b01: n7218_o = 1'b0;
      default: n7218_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2197:41  */
  always @*
    case (n7206_o)
      2'b10: n7222_o = 1'b1;
      2'b01: n7222_o = 1'b1;
      default: n7222_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2195:33  */
  assign n7225_o = r800_mode ? 3'b010 : 3'b001;
  /* T80_MCode.vhd:2195:33  */
  assign n7227_o = r800_mode ? n7208_o : 3'b000;
  assign n7228_o = {n7212_o, n7210_o};
  /* T80_MCode.vhd:2195:33  */
  assign n7230_o = r800_mode ? n7228_o : 4'b0000;
  /* T80_MCode.vhd:2195:33  */
  assign n7232_o = r800_mode ? n7215_o : 1'b0;
  /* T80_MCode.vhd:2195:33  */
  assign n7234_o = r800_mode ? n7218_o : 1'b0;
  /* T80_MCode.vhd:2195:33  */
  assign n7236_o = r800_mode ? n7222_o : 1'b0;
  /* T80_MCode.vhd:2193:25  */
  assign n7238_o = ir == 8'b11000001;
  /* T80_MCode.vhd:2193:40  */
  assign n7240_o = ir == 8'b11001001;
  /* T80_MCode.vhd:2193:40  */
  assign n7241_o = n7238_o | n7240_o;
  /* T80_MCode.vhd:2193:51  */
  assign n7243_o = ir == 8'b11010001;
  /* T80_MCode.vhd:2193:51  */
  assign n7244_o = n7241_o | n7243_o;
  /* T80_MCode.vhd:2193:62  */
  assign n7246_o = ir == 8'b11011001;
  /* T80_MCode.vhd:2193:62  */
  assign n7247_o = n7244_o | n7246_o;
  /* T80_MCode.vhd:2214:46  */
  assign n7248_o = {28'b0, mcycle};  //  uext
  /* T80_MCode.vhd:2217:58  */
  assign n7250_o = n2492_o == 2'b11;
  assign n7253_o = {1'b0, n2492_o, 1'b0};
  /* T80_MCode.vhd:2217:49  */
  assign n7255_o = n7250_o ? 4'b1000 : n7253_o;
  /* T80_MCode.vhd:2215:41  */
  assign n7258_o = n7248_o == 31'b0000000000000000000000000000001;
  /* T80_MCode.vhd:2225:41  */
  assign n7261_o = n7248_o == 31'b0000000000000000000000000000010;
  assign n7262_o = {n7261_o, n7258_o};
  /* T80_MCode.vhd:2214:41  */
  always @*
    case (n7262_o)
      2'b10: n7264_o = 3'b101;
      2'b01: n7264_o = n2497_o;
      default: n7264_o = n2497_o;
    endcase
  /* T80_MCode.vhd:2214:41  */
  always @*
    case (n7262_o)
      2'b10: n7266_o = 3'b100;
      2'b01: n7266_o = 3'b100;
      default: n7266_o = 3'b000;
    endcase
  /* T80_MCode.vhd:2214:41  */
  always @*
    case (n7262_o)
      2'b10: n7268_o = 4'b0000;
      2'b01: n7268_o = n7255_o;
      default: n7268_o = 4'b0000;
    endcase
  /* T80_MCode.vhd:2214:41  */
  always @*
    case (n7262_o)
      2'b10: n7271_o = 1'b1;
      2'b01: n7271_o = 1'b0;
      default: n7271_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2214:41  */
  always @*
    case (n7262_o)
      2'b10: n7275_o = 1'b1;
      2'b01: n7275_o = 1'b1;
      default: n7275_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2212:33  */
  assign n7278_o = r800_mode ? 3'b010 : 3'b001;
  /* T80_MCode.vhd:2212:33  */
  assign n7279_o = r800_mode ? n7264_o : n2497_o;
  /* T80_MCode.vhd:2212:33  */
  assign n7281_o = r800_mode ? n7266_o : 3'b000;
  /* T80_MCode.vhd:2212:33  */
  assign n7283_o = r800_mode ? n7268_o : 4'b0000;
  /* T80_MCode.vhd:2212:33  */
  assign n7285_o = r800_mode ? n7271_o : 1'b0;
  /* T80_MCode.vhd:2212:33  */
  assign n7287_o = r800_mode ? n7275_o : 1'b0;
  /* T80_MCode.vhd:2210:25  */
  assign n7289_o = ir == 8'b11000011;
  /* T80_MCode.vhd:2210:40  */
  assign n7291_o = ir == 8'b11110011;
  /* T80_MCode.vhd:2210:40  */
  assign n7292_o = n7289_o | n7291_o;
  assign n7293_o = {n7292_o, n7247_o, n7197_o, n7127_o, n7057_o, n7005_o, n6951_o, n6900_o, n6856_o, n6812_o, n6721_o, n6630_o, n6625_o, n6620_o, n6609_o, n6586_o, n6526_o, n6465_o, n6403_o, n6335_o, n6333_o, n6331_o, n6329_o, n6327_o, n6322_o};
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7309_o = n7278_o;
      25'b0100000000000000000000000: n7309_o = n7225_o;
      25'b0010000000000000000000000: n7309_o = 3'b100;
      25'b0001000000000000000000000: n7309_o = 3'b100;
      25'b0000100000000000000000000: n7309_o = 3'b010;
      25'b0000010000000000000000000: n7309_o = 3'b010;
      25'b0000001000000000000000000: n7309_o = 3'b011;
      25'b0000000100000000000000000: n7309_o = 3'b100;
      25'b0000000010000000000000000: n7309_o = 3'b100;
      25'b0000000001000000000000000: n7309_o = 3'b011;
      25'b0000000000100000000000000: n7309_o = 3'b011;
      25'b0000000000010000000000000: n7309_o = 3'b001;
      25'b0000000000001000000000000: n7309_o = 3'b001;
      25'b0000000000000100000000000: n7309_o = 3'b001;
      25'b0000000000000010000000000: n7309_o = 3'b001;
      25'b0000000000000001000000000: n7309_o = 3'b100;
      25'b0000000000000000100000000: n7309_o = 3'b100;
      25'b0000000000000000010000000: n7309_o = 3'b101;
      25'b0000000000000000001000000: n7309_o = 3'b101;
      25'b0000000000000000000100000: n7309_o = 3'b001;
      25'b0000000000000000000010000: n7309_o = 3'b001;
      25'b0000000000000000000001000: n7309_o = 3'b001;
      25'b0000000000000000000000100: n7309_o = 3'b001;
      25'b0000000000000000000000010: n7309_o = 3'b001;
      25'b0000000000000000000000001: n7309_o = 3'b001;
      default: n7309_o = 3'bX;
    endcase
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7315_o = n7279_o;
      25'b0100000000000000000000000: n7315_o = n2497_o;
      25'b0010000000000000000000000: n7315_o = n7146_o;
      25'b0001000000000000000000000: n7315_o = n7076_o;
      25'b0000100000000000000000000: n7315_o = n2497_o;
      25'b0000010000000000000000000: n7315_o = n2497_o;
      25'b0000001000000000000000000: n7315_o = n2497_o;
      25'b0000000100000000000000000: n7315_o = n6870_o;
      25'b0000000010000000000000000: n7315_o = n6826_o;
      25'b0000000001000000000000000: n7315_o = n6771_o;
      25'b0000000000100000000000000: n7315_o = n6680_o;
      25'b0000000000010000000000000: n7315_o = n2497_o;
      25'b0000000000001000000000000: n7315_o = n2497_o;
      25'b0000000000000100000000000: n7315_o = n2497_o;
      25'b0000000000000010000000000: n7315_o = n2497_o;
      25'b0000000000000001000000000: n7315_o = n6545_o;
      25'b0000000000000000100000000: n7315_o = n6489_o;
      25'b0000000000000000010000000: n7315_o = n2497_o;
      25'b0000000000000000001000000: n7315_o = n2497_o;
      25'b0000000000000000000100000: n7315_o = 3'b101;
      25'b0000000000000000000010000: n7315_o = 3'b101;
      25'b0000000000000000000001000: n7315_o = 3'b101;
      25'b0000000000000000000000100: n7315_o = 3'b101;
      25'b0000000000000000000000010: n7315_o = n2497_o;
      25'b0000000000000000000000001: n7315_o = n2497_o;
      default: n7315_o = 3'bX;
    endcase
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7318_o = 1'b0;
      25'b0100000000000000000000000: n7318_o = 1'b0;
      25'b0010000000000000000000000: n7318_o = 1'b0;
      25'b0001000000000000000000000: n7318_o = 1'b0;
      25'b0000100000000000000000000: n7318_o = 1'b0;
      25'b0000010000000000000000000: n7318_o = 1'b0;
      25'b0000001000000000000000000: n7318_o = 1'b0;
      25'b0000000100000000000000000: n7318_o = 1'b0;
      25'b0000000010000000000000000: n7318_o = 1'b0;
      25'b0000000001000000000000000: n7318_o = 1'b0;
      25'b0000000000100000000000000: n7318_o = 1'b0;
      25'b0000000000010000000000000: n7318_o = 1'b0;
      25'b0000000000001000000000000: n7318_o = 1'b0;
      25'b0000000000000100000000000: n7318_o = 1'b0;
      25'b0000000000000010000000000: n7318_o = 1'b0;
      25'b0000000000000001000000000: n7318_o = 1'b0;
      25'b0000000000000000100000000: n7318_o = 1'b0;
      25'b0000000000000000010000000: n7318_o = n6435_o;
      25'b0000000000000000001000000: n7318_o = n6371_o;
      25'b0000000000000000000100000: n7318_o = 1'b0;
      25'b0000000000000000000010000: n7318_o = 1'b0;
      25'b0000000000000000000001000: n7318_o = 1'b0;
      25'b0000000000000000000000100: n7318_o = 1'b0;
      25'b0000000000000000000000010: n7318_o = 1'b0;
      25'b0000000000000000000000001: n7318_o = 1'b0;
      default: n7318_o = 1'bX;
    endcase
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7321_o = 1'b0;
      25'b0100000000000000000000000: n7321_o = 1'b0;
      25'b0010000000000000000000000: n7321_o = 1'b0;
      25'b0001000000000000000000000: n7321_o = 1'b0;
      25'b0000100000000000000000000: n7321_o = 1'b0;
      25'b0000010000000000000000000: n7321_o = 1'b0;
      25'b0000001000000000000000000: n7321_o = 1'b0;
      25'b0000000100000000000000000: n7321_o = 1'b0;
      25'b0000000010000000000000000: n7321_o = 1'b0;
      25'b0000000001000000000000000: n7321_o = 1'b0;
      25'b0000000000100000000000000: n7321_o = 1'b0;
      25'b0000000000010000000000000: n7321_o = 1'b0;
      25'b0000000000001000000000000: n7321_o = 1'b0;
      25'b0000000000000100000000000: n7321_o = 1'b0;
      25'b0000000000000010000000000: n7321_o = 1'b0;
      25'b0000000000000001000000000: n7321_o = 1'b0;
      25'b0000000000000000100000000: n7321_o = 1'b0;
      25'b0000000000000000010000000: n7321_o = n6438_o;
      25'b0000000000000000001000000: n7321_o = n6374_o;
      25'b0000000000000000000100000: n7321_o = 1'b0;
      25'b0000000000000000000010000: n7321_o = 1'b0;
      25'b0000000000000000000001000: n7321_o = 1'b0;
      25'b0000000000000000000000100: n7321_o = 1'b0;
      25'b0000000000000000000000010: n7321_o = 1'b0;
      25'b0000000000000000000000001: n7321_o = 1'b0;
      default: n7321_o = 1'bX;
    endcase
  assign n7322_o = n6492_o[2:0]; // extract
  assign n7323_o = n6548_o[2:0]; // extract
  assign n7324_o = n6912_o[2:0]; // extract
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7327_o = 3'b000;
      25'b0100000000000000000000000: n7327_o = 3'b000;
      25'b0010000000000000000000000: n7327_o = n7149_o;
      25'b0001000000000000000000000: n7327_o = n7079_o;
      25'b0000100000000000000000000: n7327_o = 3'b000;
      25'b0000010000000000000000000: n7327_o = 3'b000;
      25'b0000001000000000000000000: n7327_o = n7324_o;
      25'b0000000100000000000000000: n7327_o = 3'b000;
      25'b0000000010000000000000000: n7327_o = 3'b000;
      25'b0000000001000000000000000: n7327_o = 3'b000;
      25'b0000000000100000000000000: n7327_o = 3'b000;
      25'b0000000000010000000000000: n7327_o = 3'b000;
      25'b0000000000001000000000000: n7327_o = 3'b000;
      25'b0000000000000100000000000: n7327_o = 3'b000;
      25'b0000000000000010000000000: n7327_o = 3'b000;
      25'b0000000000000001000000000: n7327_o = n7323_o;
      25'b0000000000000000100000000: n7327_o = n7322_o;
      25'b0000000000000000010000000: n7327_o = 3'b000;
      25'b0000000000000000001000000: n7327_o = 3'b000;
      25'b0000000000000000000100000: n7327_o = 3'b000;
      25'b0000000000000000000010000: n7327_o = 3'b000;
      25'b0000000000000000000001000: n7327_o = 3'b000;
      25'b0000000000000000000000100: n7327_o = 3'b000;
      25'b0000000000000000000000010: n7327_o = 3'b000;
      25'b0000000000000000000000001: n7327_o = 3'b000;
      default: n7327_o = 3'bX;
    endcase
  assign n7328_o = n6492_o[3]; // extract
  assign n7329_o = n6548_o[3]; // extract
  assign n7330_o = n6912_o[3]; // extract
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7333_o = 1'b0;
      25'b0100000000000000000000000: n7333_o = 1'b0;
      25'b0010000000000000000000000: n7333_o = n7152_o;
      25'b0001000000000000000000000: n7333_o = n7082_o;
      25'b0000100000000000000000000: n7333_o = 1'b0;
      25'b0000010000000000000000000: n7333_o = 1'b0;
      25'b0000001000000000000000000: n7333_o = n7330_o;
      25'b0000000100000000000000000: n7333_o = 1'b0;
      25'b0000000010000000000000000: n7333_o = 1'b0;
      25'b0000000001000000000000000: n7333_o = 1'b0;
      25'b0000000000100000000000000: n7333_o = 1'b0;
      25'b0000000000010000000000000: n7333_o = 1'b0;
      25'b0000000000001000000000000: n7333_o = 1'b0;
      25'b0000000000000100000000000: n7333_o = 1'b0;
      25'b0000000000000010000000000: n7333_o = 1'b0;
      25'b0000000000000001000000000: n7333_o = n7329_o;
      25'b0000000000000000100000000: n7333_o = n7328_o;
      25'b0000000000000000010000000: n7333_o = 1'b0;
      25'b0000000000000000001000000: n7333_o = 1'b0;
      25'b0000000000000000000100000: n7333_o = 1'b0;
      25'b0000000000000000000010000: n7333_o = 1'b0;
      25'b0000000000000000000001000: n7333_o = 1'b0;
      25'b0000000000000000000000100: n7333_o = 1'b0;
      25'b0000000000000000000000010: n7333_o = 1'b0;
      25'b0000000000000000000000001: n7333_o = 1'b0;
      default: n7333_o = 1'bX;
    endcase
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7336_o = 1'b0;
      25'b0100000000000000000000000: n7336_o = 1'b0;
      25'b0010000000000000000000000: n7336_o = n7155_o;
      25'b0001000000000000000000000: n7336_o = n7085_o;
      25'b0000100000000000000000000: n7336_o = 1'b0;
      25'b0000010000000000000000000: n7336_o = n6968_o;
      25'b0000001000000000000000000: n7336_o = 1'b0;
      25'b0000000100000000000000000: n7336_o = n6873_o;
      25'b0000000010000000000000000: n7336_o = n6829_o;
      25'b0000000001000000000000000: n7336_o = n6775_o;
      25'b0000000000100000000000000: n7336_o = n6684_o;
      25'b0000000000010000000000000: n7336_o = 1'b0;
      25'b0000000000001000000000000: n7336_o = 1'b0;
      25'b0000000000000100000000000: n7336_o = 1'b0;
      25'b0000000000000010000000000: n7336_o = 1'b0;
      25'b0000000000000001000000000: n7336_o = 1'b0;
      25'b0000000000000000100000000: n7336_o = 1'b0;
      25'b0000000000000000010000000: n7336_o = 1'b0;
      25'b0000000000000000001000000: n7336_o = n6378_o;
      25'b0000000000000000000100000: n7336_o = 1'b0;
      25'b0000000000000000000010000: n7336_o = 1'b0;
      25'b0000000000000000000001000: n7336_o = 1'b0;
      25'b0000000000000000000000100: n7336_o = 1'b0;
      25'b0000000000000000000000010: n7336_o = 1'b0;
      25'b0000000000000000000000001: n7336_o = 1'b0;
      default: n7336_o = 1'bX;
    endcase
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7340_o = 1'b0;
      25'b0100000000000000000000000: n7340_o = 1'b0;
      25'b0010000000000000000000000: n7340_o = 1'b0;
      25'b0001000000000000000000000: n7340_o = 1'b0;
      25'b0000100000000000000000000: n7340_o = 1'b0;
      25'b0000010000000000000000000: n7340_o = 1'b0;
      25'b0000001000000000000000000: n7340_o = 1'b0;
      25'b0000000100000000000000000: n7340_o = 1'b0;
      25'b0000000010000000000000000: n7340_o = 1'b0;
      25'b0000000001000000000000000: n7340_o = 1'b0;
      25'b0000000000100000000000000: n7340_o = 1'b0;
      25'b0000000000010000000000000: n7340_o = 1'b0;
      25'b0000000000001000000000000: n7340_o = 1'b0;
      25'b0000000000000100000000000: n7340_o = 1'b0;
      25'b0000000000000010000000000: n7340_o = 1'b1;
      25'b0000000000000001000000000: n7340_o = 1'b0;
      25'b0000000000000000100000000: n7340_o = 1'b0;
      25'b0000000000000000010000000: n7340_o = 1'b0;
      25'b0000000000000000001000000: n7340_o = 1'b0;
      25'b0000000000000000000100000: n7340_o = 1'b0;
      25'b0000000000000000000010000: n7340_o = 1'b0;
      25'b0000000000000000000001000: n7340_o = 1'b0;
      25'b0000000000000000000000100: n7340_o = 1'b0;
      25'b0000000000000000000000010: n7340_o = 1'b0;
      25'b0000000000000000000000001: n7340_o = 1'b0;
      default: n7340_o = 1'bX;
    endcase
  assign n7342_o = n7088_o[2:0]; // extract
  assign n7343_o = n7158_o[2:0]; // extract
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7346_o = n7281_o;
      25'b0100000000000000000000000: n7346_o = n7227_o;
      25'b0010000000000000000000000: n7346_o = n7343_o;
      25'b0001000000000000000000000: n7346_o = n7342_o;
      25'b0000100000000000000000000: n7346_o = 3'b000;
      25'b0000010000000000000000000: n7346_o = n6970_o;
      25'b0000001000000000000000000: n7346_o = 3'b000;
      25'b0000000100000000000000000: n7346_o = n6875_o;
      25'b0000000010000000000000000: n7346_o = n6831_o;
      25'b0000000001000000000000000: n7346_o = n6777_o;
      25'b0000000000100000000000000: n7346_o = n6686_o;
      25'b0000000000010000000000000: n7346_o = 3'b000;
      25'b0000000000001000000000000: n7346_o = 3'b000;
      25'b0000000000000100000000000: n7346_o = 3'b000;
      25'b0000000000000010000000000: n7346_o = 3'b010;
      25'b0000000000000001000000000: n7346_o = n6550_o;
      25'b0000000000000000100000000: n7346_o = n6494_o;
      25'b0000000000000000010000000: n7346_o = 3'b000;
      25'b0000000000000000001000000: n7346_o = n6380_o;
      25'b0000000000000000000100000: n7346_o = 3'b000;
      25'b0000000000000000000010000: n7346_o = 3'b000;
      25'b0000000000000000000001000: n7346_o = 3'b000;
      25'b0000000000000000000000100: n7346_o = 3'b000;
      25'b0000000000000000000000010: n7346_o = 3'b000;
      25'b0000000000000000000000001: n7346_o = 3'b000;
      default: n7346_o = 3'bX;
    endcase
  assign n7348_o = n7088_o[3]; // extract
  assign n7349_o = n7158_o[3]; // extract
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7352_o = 1'b0;
      25'b0100000000000000000000000: n7352_o = 1'b0;
      25'b0010000000000000000000000: n7352_o = n7349_o;
      25'b0001000000000000000000000: n7352_o = n7348_o;
      25'b0000100000000000000000000: n7352_o = 1'b0;
      25'b0000010000000000000000000: n7352_o = 1'b0;
      25'b0000001000000000000000000: n7352_o = 1'b0;
      25'b0000000100000000000000000: n7352_o = 1'b0;
      25'b0000000010000000000000000: n7352_o = 1'b0;
      25'b0000000001000000000000000: n7352_o = 1'b0;
      25'b0000000000100000000000000: n7352_o = 1'b0;
      25'b0000000000010000000000000: n7352_o = 1'b0;
      25'b0000000000001000000000000: n7352_o = 1'b0;
      25'b0000000000000100000000000: n7352_o = 1'b0;
      25'b0000000000000010000000000: n7352_o = 1'b1;
      25'b0000000000000001000000000: n7352_o = 1'b0;
      25'b0000000000000000100000000: n7352_o = 1'b0;
      25'b0000000000000000010000000: n7352_o = 1'b0;
      25'b0000000000000000001000000: n7352_o = n6382_o;
      25'b0000000000000000000100000: n7352_o = 1'b0;
      25'b0000000000000000000010000: n7352_o = 1'b0;
      25'b0000000000000000000001000: n7352_o = 1'b0;
      25'b0000000000000000000000100: n7352_o = 1'b0;
      25'b0000000000000000000000010: n7352_o = 1'b0;
      25'b0000000000000000000000001: n7352_o = 1'b0;
      default: n7352_o = 1'bX;
    endcase
  assign n7353_o = n6440_o[0]; // extract
  assign n7354_o = n6497_o[0]; // extract
  assign n7355_o = n6553_o[0]; // extract
  assign n7357_o = n6833_o[0]; // extract
  assign n7358_o = n6877_o[0]; // extract
  assign n7359_o = n7020_o[0]; // extract
  assign n7360_o = n7092_o[0]; // extract
  assign n7361_o = n7162_o[0]; // extract
  assign n7362_o = n7230_o[0]; // extract
  assign n7363_o = n7283_o[0]; // extract
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7366_o = n7363_o;
      25'b0100000000000000000000000: n7366_o = n7362_o;
      25'b0010000000000000000000000: n7366_o = n7361_o;
      25'b0001000000000000000000000: n7366_o = n7360_o;
      25'b0000100000000000000000000: n7366_o = n7359_o;
      25'b0000010000000000000000000: n7366_o = 1'b0;
      25'b0000001000000000000000000: n7366_o = 1'b0;
      25'b0000000100000000000000000: n7366_o = n7358_o;
      25'b0000000010000000000000000: n7366_o = n7357_o;
      25'b0000000001000000000000000: n7366_o = n6779_o;
      25'b0000000000100000000000000: n7366_o = n6688_o;
      25'b0000000000010000000000000: n7366_o = 1'b0;
      25'b0000000000001000000000000: n7366_o = 1'b0;
      25'b0000000000000100000000000: n7366_o = 1'b0;
      25'b0000000000000010000000000: n7366_o = 1'b1;
      25'b0000000000000001000000000: n7366_o = n7355_o;
      25'b0000000000000000100000000: n7366_o = n7354_o;
      25'b0000000000000000010000000: n7366_o = n7353_o;
      25'b0000000000000000001000000: n7366_o = 1'b0;
      25'b0000000000000000000100000: n7366_o = 1'b0;
      25'b0000000000000000000010000: n7366_o = 1'b0;
      25'b0000000000000000000001000: n7366_o = 1'b0;
      25'b0000000000000000000000100: n7366_o = 1'b0;
      25'b0000000000000000000000010: n7366_o = 1'b0;
      25'b0000000000000000000000001: n7366_o = 1'b0;
      default: n7366_o = 1'bX;
    endcase
  assign n7367_o = n6440_o[2:1]; // extract
  assign n7368_o = n6497_o[2:1]; // extract
  assign n7369_o = n6553_o[2:1]; // extract
  assign n7371_o = n6833_o[2:1]; // extract
  assign n7372_o = n6877_o[2:1]; // extract
  assign n7373_o = n7020_o[2:1]; // extract
  assign n7374_o = n7092_o[2:1]; // extract
  assign n7375_o = n7162_o[2:1]; // extract
  assign n7376_o = n7230_o[2:1]; // extract
  assign n7377_o = n7283_o[2:1]; // extract
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7380_o = n7377_o;
      25'b0100000000000000000000000: n7380_o = n7376_o;
      25'b0010000000000000000000000: n7380_o = n7375_o;
      25'b0001000000000000000000000: n7380_o = n7374_o;
      25'b0000100000000000000000000: n7380_o = n7373_o;
      25'b0000010000000000000000000: n7380_o = 2'b00;
      25'b0000001000000000000000000: n7380_o = 2'b00;
      25'b0000000100000000000000000: n7380_o = n7372_o;
      25'b0000000010000000000000000: n7380_o = n7371_o;
      25'b0000000001000000000000000: n7380_o = n6781_o;
      25'b0000000000100000000000000: n7380_o = n6690_o;
      25'b0000000000010000000000000: n7380_o = 2'b00;
      25'b0000000000001000000000000: n7380_o = 2'b00;
      25'b0000000000000100000000000: n7380_o = 2'b00;
      25'b0000000000000010000000000: n7380_o = 2'b11;
      25'b0000000000000001000000000: n7380_o = n7369_o;
      25'b0000000000000000100000000: n7380_o = n7368_o;
      25'b0000000000000000010000000: n7380_o = n7367_o;
      25'b0000000000000000001000000: n7380_o = 2'b00;
      25'b0000000000000000000100000: n7380_o = 2'b00;
      25'b0000000000000000000010000: n7380_o = 2'b00;
      25'b0000000000000000000001000: n7380_o = 2'b00;
      25'b0000000000000000000000100: n7380_o = 2'b00;
      25'b0000000000000000000000010: n7380_o = 2'b00;
      25'b0000000000000000000000001: n7380_o = 2'b00;
      default: n7380_o = 2'bX;
    endcase
  assign n7381_o = n6440_o[3]; // extract
  assign n7382_o = n6497_o[3]; // extract
  assign n7383_o = n6553_o[3]; // extract
  assign n7385_o = n7092_o[3]; // extract
  assign n7386_o = n7162_o[3]; // extract
  assign n7387_o = n7230_o[3]; // extract
  assign n7388_o = n7283_o[3]; // extract
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7391_o = n7388_o;
      25'b0100000000000000000000000: n7391_o = n7387_o;
      25'b0010000000000000000000000: n7391_o = n7386_o;
      25'b0001000000000000000000000: n7391_o = n7385_o;
      25'b0000100000000000000000000: n7391_o = n7022_o;
      25'b0000010000000000000000000: n7391_o = 1'b0;
      25'b0000001000000000000000000: n7391_o = 1'b0;
      25'b0000000100000000000000000: n7391_o = 1'b0;
      25'b0000000010000000000000000: n7391_o = 1'b0;
      25'b0000000001000000000000000: n7391_o = n6783_o;
      25'b0000000000100000000000000: n7391_o = n6692_o;
      25'b0000000000010000000000000: n7391_o = 1'b0;
      25'b0000000000001000000000000: n7391_o = 1'b0;
      25'b0000000000000100000000000: n7391_o = 1'b0;
      25'b0000000000000010000000000: n7391_o = 1'b0;
      25'b0000000000000001000000000: n7391_o = n7383_o;
      25'b0000000000000000100000000: n7391_o = n7382_o;
      25'b0000000000000000010000000: n7391_o = n7381_o;
      25'b0000000000000000001000000: n7391_o = 1'b0;
      25'b0000000000000000000100000: n7391_o = 1'b0;
      25'b0000000000000000000010000: n7391_o = 1'b0;
      25'b0000000000000000000001000: n7391_o = 1'b0;
      25'b0000000000000000000000100: n7391_o = 1'b0;
      25'b0000000000000000000000010: n7391_o = 1'b0;
      25'b0000000000000000000000001: n7391_o = 1'b0;
      default: n7391_o = 1'bX;
    endcase
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7394_o = n2500_o;
      25'b0100000000000000000000000: n7394_o = n2500_o;
      25'b0010000000000000000000000: n7394_o = n7164_o;
      25'b0001000000000000000000000: n7394_o = n7094_o;
      25'b0000100000000000000000000: n7394_o = n2500_o;
      25'b0000010000000000000000000: n7394_o = n2500_o;
      25'b0000001000000000000000000: n7394_o = n2500_o;
      25'b0000000100000000000000000: n7394_o = n6879_o;
      25'b0000000010000000000000000: n7394_o = n6835_o;
      25'b0000000001000000000000000: n7394_o = n6786_o;
      25'b0000000000100000000000000: n7394_o = n6695_o;
      25'b0000000000010000000000000: n7394_o = n2500_o;
      25'b0000000000001000000000000: n7394_o = n2500_o;
      25'b0000000000000100000000000: n7394_o = n2500_o;
      25'b0000000000000010000000000: n7394_o = 4'b0010;
      25'b0000000000000001000000000: n7394_o = n6555_o;
      25'b0000000000000000100000000: n7394_o = n6499_o;
      25'b0000000000000000010000000: n7394_o = n2500_o;
      25'b0000000000000000001000000: n7394_o = n2500_o;
      25'b0000000000000000000100000: n7394_o = n2500_o;
      25'b0000000000000000000010000: n7394_o = n2500_o;
      25'b0000000000000000000001000: n7394_o = n2500_o;
      25'b0000000000000000000000100: n7394_o = n2500_o;
      25'b0000000000000000000000010: n7394_o = n2500_o;
      25'b0000000000000000000000001: n7394_o = n2500_o;
      default: n7394_o = 4'bX;
    endcase
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7398_o = 1'b0;
      25'b0100000000000000000000000: n7398_o = 1'b0;
      25'b0010000000000000000000000: n7398_o = n7167_o;
      25'b0001000000000000000000000: n7398_o = n7097_o;
      25'b0000100000000000000000000: n7398_o = 1'b0;
      25'b0000010000000000000000000: n7398_o = 1'b0;
      25'b0000001000000000000000000: n7398_o = 1'b0;
      25'b0000000100000000000000000: n7398_o = n6882_o;
      25'b0000000010000000000000000: n7398_o = n6838_o;
      25'b0000000001000000000000000: n7398_o = n6790_o;
      25'b0000000000100000000000000: n7398_o = n6699_o;
      25'b0000000000010000000000000: n7398_o = 1'b0;
      25'b0000000000001000000000000: n7398_o = 1'b0;
      25'b0000000000000100000000000: n7398_o = 1'b0;
      25'b0000000000000010000000000: n7398_o = 1'b1;
      25'b0000000000000001000000000: n7398_o = n6558_o;
      25'b0000000000000000100000000: n7398_o = 1'b0;
      25'b0000000000000000010000000: n7398_o = 1'b0;
      25'b0000000000000000001000000: n7398_o = 1'b0;
      25'b0000000000000000000100000: n7398_o = 1'b0;
      25'b0000000000000000000010000: n7398_o = 1'b0;
      25'b0000000000000000000001000: n7398_o = 1'b0;
      25'b0000000000000000000000100: n7398_o = 1'b0;
      25'b0000000000000000000000010: n7398_o = 1'b0;
      25'b0000000000000000000000001: n7398_o = 1'b0;
      default: n7398_o = 1'bX;
    endcase
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7401_o = 1'b0;
      25'b0100000000000000000000000: n7401_o = 1'b0;
      25'b0010000000000000000000000: n7401_o = 1'b0;
      25'b0001000000000000000000000: n7401_o = 1'b0;
      25'b0000100000000000000000000: n7401_o = 1'b0;
      25'b0000010000000000000000000: n7401_o = 1'b0;
      25'b0000001000000000000000000: n7401_o = 1'b0;
      25'b0000000100000000000000000: n7401_o = 1'b0;
      25'b0000000010000000000000000: n7401_o = 1'b0;
      25'b0000000001000000000000000: n7401_o = 1'b0;
      25'b0000000000100000000000000: n7401_o = 1'b0;
      25'b0000000000010000000000000: n7401_o = 1'b0;
      25'b0000000000001000000000000: n7401_o = 1'b0;
      25'b0000000000000100000000000: n7401_o = 1'b0;
      25'b0000000000000010000000000: n7401_o = 1'b0;
      25'b0000000000000001000000000: n7401_o = n6561_o;
      25'b0000000000000000100000000: n7401_o = 1'b0;
      25'b0000000000000000010000000: n7401_o = 1'b0;
      25'b0000000000000000001000000: n7401_o = 1'b0;
      25'b0000000000000000000100000: n7401_o = 1'b0;
      25'b0000000000000000000010000: n7401_o = 1'b0;
      25'b0000000000000000000001000: n7401_o = 1'b0;
      25'b0000000000000000000000100: n7401_o = 1'b0;
      25'b0000000000000000000000010: n7401_o = 1'b0;
      25'b0000000000000000000000001: n7401_o = 1'b0;
      default: n7401_o = 1'bX;
    endcase
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7404_o = 3'b111;
      25'b0100000000000000000000000: n7404_o = 3'b111;
      25'b0010000000000000000000000: n7404_o = n7171_o;
      25'b0001000000000000000000000: n7404_o = n7101_o;
      25'b0000100000000000000000000: n7404_o = n7025_o;
      25'b0000010000000000000000000: n7404_o = n6973_o;
      25'b0000001000000000000000000: n7404_o = n6916_o;
      25'b0000000100000000000000000: n7404_o = n6886_o;
      25'b0000000010000000000000000: n7404_o = n6842_o;
      25'b0000000001000000000000000: n7404_o = 3'b111;
      25'b0000000000100000000000000: n7404_o = 3'b111;
      25'b0000000000010000000000000: n7404_o = 3'b111;
      25'b0000000000001000000000000: n7404_o = 3'b111;
      25'b0000000000000100000000000: n7404_o = 3'b111;
      25'b0000000000000010000000000: n7404_o = 3'b111;
      25'b0000000000000001000000000: n7404_o = n6564_o;
      25'b0000000000000000100000000: n7404_o = n6503_o;
      25'b0000000000000000010000000: n7404_o = n6444_o;
      25'b0000000000000000001000000: n7404_o = n6386_o;
      25'b0000000000000000000100000: n7404_o = 3'b111;
      25'b0000000000000000000010000: n7404_o = 3'b111;
      25'b0000000000000000000001000: n7404_o = 3'b111;
      25'b0000000000000000000000100: n7404_o = 3'b111;
      25'b0000000000000000000000010: n7404_o = 3'b111;
      25'b0000000000000000000000001: n7404_o = 3'b111;
      default: n7404_o = 3'bX;
    endcase
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7407_o = 1'b0;
      25'b0100000000000000000000000: n7407_o = 1'b0;
      25'b0010000000000000000000000: n7407_o = n7174_o;
      25'b0001000000000000000000000: n7407_o = n7104_o;
      25'b0000100000000000000000000: n7407_o = n7028_o;
      25'b0000010000000000000000000: n7407_o = n6976_o;
      25'b0000001000000000000000000: n7407_o = 1'b0;
      25'b0000000100000000000000000: n7407_o = 1'b0;
      25'b0000000010000000000000000: n7407_o = 1'b0;
      25'b0000000001000000000000000: n7407_o = 1'b0;
      25'b0000000000100000000000000: n7407_o = 1'b0;
      25'b0000000000010000000000000: n7407_o = 1'b0;
      25'b0000000000001000000000000: n7407_o = 1'b0;
      25'b0000000000000100000000000: n7407_o = 1'b0;
      25'b0000000000000010000000000: n7407_o = 1'b0;
      25'b0000000000000001000000000: n7407_o = 1'b0;
      25'b0000000000000000100000000: n7407_o = 1'b0;
      25'b0000000000000000010000000: n7407_o = 1'b0;
      25'b0000000000000000001000000: n7407_o = 1'b0;
      25'b0000000000000000000100000: n7407_o = 1'b0;
      25'b0000000000000000000010000: n7407_o = 1'b0;
      25'b0000000000000000000001000: n7407_o = 1'b0;
      25'b0000000000000000000000100: n7407_o = 1'b0;
      25'b0000000000000000000000010: n7407_o = 1'b0;
      25'b0000000000000000000000001: n7407_o = 1'b0;
      default: n7407_o = 1'bX;
    endcase
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7410_o = 1'b0;
      25'b0100000000000000000000000: n7410_o = 1'b0;
      25'b0010000000000000000000000: n7410_o = 1'b0;
      25'b0001000000000000000000000: n7410_o = 1'b0;
      25'b0000100000000000000000000: n7410_o = 1'b0;
      25'b0000010000000000000000000: n7410_o = 1'b0;
      25'b0000001000000000000000000: n7410_o = n6919_o;
      25'b0000000100000000000000000: n7410_o = 1'b0;
      25'b0000000010000000000000000: n7410_o = 1'b0;
      25'b0000000001000000000000000: n7410_o = 1'b0;
      25'b0000000000100000000000000: n7410_o = 1'b0;
      25'b0000000000010000000000000: n7410_o = 1'b0;
      25'b0000000000001000000000000: n7410_o = 1'b0;
      25'b0000000000000100000000000: n7410_o = 1'b0;
      25'b0000000000000010000000000: n7410_o = 1'b0;
      25'b0000000000000001000000000: n7410_o = 1'b0;
      25'b0000000000000000100000000: n7410_o = 1'b0;
      25'b0000000000000000010000000: n7410_o = 1'b0;
      25'b0000000000000000001000000: n7410_o = 1'b0;
      25'b0000000000000000000100000: n7410_o = 1'b0;
      25'b0000000000000000000010000: n7410_o = 1'b0;
      25'b0000000000000000000001000: n7410_o = 1'b0;
      25'b0000000000000000000000100: n7410_o = 1'b0;
      25'b0000000000000000000000010: n7410_o = 1'b0;
      25'b0000000000000000000000001: n7410_o = 1'b0;
      default: n7410_o = 1'bX;
    endcase
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7413_o = 1'b0;
      25'b0100000000000000000000000: n7413_o = 1'b0;
      25'b0010000000000000000000000: n7413_o = 1'b0;
      25'b0001000000000000000000000: n7413_o = 1'b0;
      25'b0000100000000000000000000: n7413_o = 1'b0;
      25'b0000010000000000000000000: n7413_o = 1'b0;
      25'b0000001000000000000000000: n7413_o = n6922_o;
      25'b0000000100000000000000000: n7413_o = 1'b0;
      25'b0000000010000000000000000: n7413_o = 1'b0;
      25'b0000000001000000000000000: n7413_o = 1'b0;
      25'b0000000000100000000000000: n7413_o = 1'b0;
      25'b0000000000010000000000000: n7413_o = 1'b0;
      25'b0000000000001000000000000: n7413_o = 1'b0;
      25'b0000000000000100000000000: n7413_o = 1'b0;
      25'b0000000000000010000000000: n7413_o = 1'b0;
      25'b0000000000000001000000000: n7413_o = 1'b0;
      25'b0000000000000000100000000: n7413_o = 1'b0;
      25'b0000000000000000010000000: n7413_o = n6447_o;
      25'b0000000000000000001000000: n7413_o = n6389_o;
      25'b0000000000000000000100000: n7413_o = 1'b0;
      25'b0000000000000000000010000: n7413_o = 1'b0;
      25'b0000000000000000000001000: n7413_o = 1'b0;
      25'b0000000000000000000000100: n7413_o = 1'b0;
      25'b0000000000000000000000010: n7413_o = 1'b0;
      25'b0000000000000000000000001: n7413_o = 1'b0;
      default: n7413_o = 1'bX;
    endcase
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7416_o = 1'b0;
      25'b0100000000000000000000000: n7416_o = 1'b0;
      25'b0010000000000000000000000: n7416_o = 1'b0;
      25'b0001000000000000000000000: n7416_o = 1'b0;
      25'b0000100000000000000000000: n7416_o = 1'b0;
      25'b0000010000000000000000000: n7416_o = 1'b0;
      25'b0000001000000000000000000: n7416_o = n6925_o;
      25'b0000000100000000000000000: n7416_o = 1'b0;
      25'b0000000010000000000000000: n7416_o = 1'b0;
      25'b0000000001000000000000000: n7416_o = 1'b0;
      25'b0000000000100000000000000: n7416_o = 1'b0;
      25'b0000000000010000000000000: n7416_o = 1'b0;
      25'b0000000000001000000000000: n7416_o = 1'b0;
      25'b0000000000000100000000000: n7416_o = 1'b0;
      25'b0000000000000010000000000: n7416_o = 1'b0;
      25'b0000000000000001000000000: n7416_o = 1'b0;
      25'b0000000000000000100000000: n7416_o = 1'b0;
      25'b0000000000000000010000000: n7416_o = n6450_o;
      25'b0000000000000000001000000: n7416_o = n6392_o;
      25'b0000000000000000000100000: n7416_o = 1'b0;
      25'b0000000000000000000010000: n7416_o = 1'b0;
      25'b0000000000000000000001000: n7416_o = 1'b0;
      25'b0000000000000000000000100: n7416_o = 1'b0;
      25'b0000000000000000000000010: n7416_o = 1'b0;
      25'b0000000000000000000000001: n7416_o = 1'b0;
      default: n7416_o = 1'bX;
    endcase
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7423_o = 3'b000;
      25'b0100000000000000000000000: n7423_o = 3'b000;
      25'b0010000000000000000000000: n7423_o = 3'b000;
      25'b0001000000000000000000000: n7423_o = 3'b000;
      25'b0000100000000000000000000: n7423_o = 3'b000;
      25'b0000010000000000000000000: n7423_o = 3'b000;
      25'b0000001000000000000000000: n7423_o = 3'b000;
      25'b0000000100000000000000000: n7423_o = 3'b000;
      25'b0000000010000000000000000: n7423_o = 3'b000;
      25'b0000000001000000000000000: n7423_o = 3'b000;
      25'b0000000000100000000000000: n7423_o = 3'b000;
      25'b0000000000010000000000000: n7423_o = 3'b000;
      25'b0000000000001000000000000: n7423_o = 3'b000;
      25'b0000000000000100000000000: n7423_o = 3'b000;
      25'b0000000000000010000000000: n7423_o = 3'b000;
      25'b0000000000000001000000000: n7423_o = 3'b000;
      25'b0000000000000000100000000: n7423_o = 3'b000;
      25'b0000000000000000010000000: n7423_o = 3'b000;
      25'b0000000000000000001000000: n7423_o = 3'b000;
      25'b0000000000000000000100000: n7423_o = 3'b111;
      25'b0000000000000000000010000: n7423_o = 3'b110;
      25'b0000000000000000000001000: n7423_o = 3'b101;
      25'b0000000000000000000000100: n7423_o = 3'b100;
      25'b0000000000000000000000010: n7423_o = 3'b000;
      25'b0000000000000000000000001: n7423_o = 3'b000;
      default: n7423_o = 3'bX;
    endcase
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7426_o = 1'b0;
      25'b0100000000000000000000000: n7426_o = 1'b0;
      25'b0010000000000000000000000: n7426_o = 1'b0;
      25'b0001000000000000000000000: n7426_o = 1'b0;
      25'b0000100000000000000000000: n7426_o = 1'b0;
      25'b0000010000000000000000000: n7426_o = 1'b0;
      25'b0000001000000000000000000: n7426_o = n6928_o;
      25'b0000000100000000000000000: n7426_o = 1'b0;
      25'b0000000010000000000000000: n7426_o = 1'b0;
      25'b0000000001000000000000000: n7426_o = 1'b0;
      25'b0000000000100000000000000: n7426_o = 1'b0;
      25'b0000000000010000000000000: n7426_o = 1'b0;
      25'b0000000000001000000000000: n7426_o = 1'b0;
      25'b0000000000000100000000000: n7426_o = 1'b0;
      25'b0000000000000010000000000: n7426_o = 1'b0;
      25'b0000000000000001000000000: n7426_o = 1'b0;
      25'b0000000000000000100000000: n7426_o = 1'b0;
      25'b0000000000000000010000000: n7426_o = 1'b0;
      25'b0000000000000000001000000: n7426_o = 1'b0;
      25'b0000000000000000000100000: n7426_o = 1'b0;
      25'b0000000000000000000010000: n7426_o = 1'b0;
      25'b0000000000000000000001000: n7426_o = 1'b0;
      25'b0000000000000000000000100: n7426_o = 1'b0;
      25'b0000000000000000000000010: n7426_o = 1'b0;
      25'b0000000000000000000000001: n7426_o = 1'b0;
      default: n7426_o = 1'bX;
    endcase
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7429_o = 1'b0;
      25'b0100000000000000000000000: n7429_o = 1'b0;
      25'b0010000000000000000000000: n7429_o = 1'b0;
      25'b0001000000000000000000000: n7429_o = 1'b0;
      25'b0000100000000000000000000: n7429_o = 1'b0;
      25'b0000010000000000000000000: n7429_o = 1'b0;
      25'b0000001000000000000000000: n7429_o = 1'b0;
      25'b0000000100000000000000000: n7429_o = 1'b0;
      25'b0000000010000000000000000: n7429_o = 1'b0;
      25'b0000000001000000000000000: n7429_o = 1'b0;
      25'b0000000000100000000000000: n7429_o = 1'b0;
      25'b0000000000010000000000000: n7429_o = 1'b0;
      25'b0000000000001000000000000: n7429_o = 1'b0;
      25'b0000000000000100000000000: n7429_o = 1'b0;
      25'b0000000000000010000000000: n7429_o = 1'b0;
      25'b0000000000000001000000000: n7429_o = 1'b0;
      25'b0000000000000000100000000: n7429_o = n6506_o;
      25'b0000000000000000010000000: n7429_o = 1'b0;
      25'b0000000000000000001000000: n7429_o = 1'b0;
      25'b0000000000000000000100000: n7429_o = 1'b0;
      25'b0000000000000000000010000: n7429_o = 1'b0;
      25'b0000000000000000000001000: n7429_o = 1'b0;
      25'b0000000000000000000000100: n7429_o = 1'b0;
      25'b0000000000000000000000010: n7429_o = 1'b0;
      25'b0000000000000000000000001: n7429_o = 1'b0;
      default: n7429_o = 1'bX;
    endcase
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7432_o = 1'b0;
      25'b0100000000000000000000000: n7432_o = 1'b0;
      25'b0010000000000000000000000: n7432_o = 1'b0;
      25'b0001000000000000000000000: n7432_o = 1'b0;
      25'b0000100000000000000000000: n7432_o = 1'b0;
      25'b0000010000000000000000000: n7432_o = 1'b0;
      25'b0000001000000000000000000: n7432_o = 1'b0;
      25'b0000000100000000000000000: n7432_o = 1'b0;
      25'b0000000010000000000000000: n7432_o = 1'b0;
      25'b0000000001000000000000000: n7432_o = 1'b0;
      25'b0000000000100000000000000: n7432_o = 1'b0;
      25'b0000000000010000000000000: n7432_o = 1'b0;
      25'b0000000000001000000000000: n7432_o = 1'b0;
      25'b0000000000000100000000000: n7432_o = 1'b0;
      25'b0000000000000010000000000: n7432_o = 1'b0;
      25'b0000000000000001000000000: n7432_o = n6567_o;
      25'b0000000000000000100000000: n7432_o = 1'b0;
      25'b0000000000000000010000000: n7432_o = 1'b0;
      25'b0000000000000000001000000: n7432_o = 1'b0;
      25'b0000000000000000000100000: n7432_o = 1'b0;
      25'b0000000000000000000010000: n7432_o = 1'b0;
      25'b0000000000000000000001000: n7432_o = 1'b0;
      25'b0000000000000000000000100: n7432_o = 1'b0;
      25'b0000000000000000000000010: n7432_o = 1'b0;
      25'b0000000000000000000000001: n7432_o = 1'b0;
      default: n7432_o = 1'bX;
    endcase
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7435_o = 1'b0;
      25'b0100000000000000000000000: n7435_o = 1'b0;
      25'b0010000000000000000000000: n7435_o = n7177_o;
      25'b0001000000000000000000000: n7435_o = n7107_o;
      25'b0000100000000000000000000: n7435_o = 1'b0;
      25'b0000010000000000000000000: n7435_o = 1'b0;
      25'b0000001000000000000000000: n7435_o = 1'b0;
      25'b0000000100000000000000000: n7435_o = 1'b0;
      25'b0000000010000000000000000: n7435_o = 1'b0;
      25'b0000000001000000000000000: n7435_o = 1'b0;
      25'b0000000000100000000000000: n7435_o = 1'b0;
      25'b0000000000010000000000000: n7435_o = 1'b0;
      25'b0000000000001000000000000: n7435_o = 1'b0;
      25'b0000000000000100000000000: n7435_o = 1'b0;
      25'b0000000000000010000000000: n7435_o = 1'b0;
      25'b0000000000000001000000000: n7435_o = 1'b0;
      25'b0000000000000000100000000: n7435_o = 1'b0;
      25'b0000000000000000010000000: n7435_o = 1'b0;
      25'b0000000000000000001000000: n7435_o = 1'b0;
      25'b0000000000000000000100000: n7435_o = 1'b0;
      25'b0000000000000000000010000: n7435_o = 1'b0;
      25'b0000000000000000000001000: n7435_o = 1'b0;
      25'b0000000000000000000000100: n7435_o = 1'b0;
      25'b0000000000000000000000010: n7435_o = 1'b0;
      25'b0000000000000000000000001: n7435_o = 1'b0;
      default: n7435_o = 1'bX;
    endcase
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7438_o = 1'b0;
      25'b0100000000000000000000000: n7438_o = 1'b0;
      25'b0010000000000000000000000: n7438_o = 1'b0;
      25'b0001000000000000000000000: n7438_o = 1'b0;
      25'b0000100000000000000000000: n7438_o = 1'b0;
      25'b0000010000000000000000000: n7438_o = 1'b0;
      25'b0000001000000000000000000: n7438_o = 1'b0;
      25'b0000000100000000000000000: n7438_o = 1'b0;
      25'b0000000010000000000000000: n7438_o = n6845_o;
      25'b0000000001000000000000000: n7438_o = 1'b0;
      25'b0000000000100000000000000: n7438_o = 1'b0;
      25'b0000000000010000000000000: n7438_o = 1'b0;
      25'b0000000000001000000000000: n7438_o = 1'b0;
      25'b0000000000000100000000000: n7438_o = 1'b0;
      25'b0000000000000010000000000: n7438_o = 1'b0;
      25'b0000000000000001000000000: n7438_o = 1'b0;
      25'b0000000000000000100000000: n7438_o = 1'b0;
      25'b0000000000000000010000000: n7438_o = 1'b0;
      25'b0000000000000000001000000: n7438_o = 1'b0;
      25'b0000000000000000000100000: n7438_o = 1'b0;
      25'b0000000000000000000010000: n7438_o = 1'b0;
      25'b0000000000000000000001000: n7438_o = 1'b0;
      25'b0000000000000000000000100: n7438_o = 1'b0;
      25'b0000000000000000000000010: n7438_o = 1'b0;
      25'b0000000000000000000000001: n7438_o = 1'b0;
      default: n7438_o = 1'bX;
    endcase
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7441_o = 1'b0;
      25'b0100000000000000000000000: n7441_o = 1'b0;
      25'b0010000000000000000000000: n7441_o = 1'b0;
      25'b0001000000000000000000000: n7441_o = 1'b0;
      25'b0000100000000000000000000: n7441_o = 1'b0;
      25'b0000010000000000000000000: n7441_o = 1'b0;
      25'b0000001000000000000000000: n7441_o = 1'b0;
      25'b0000000100000000000000000: n7441_o = n6889_o;
      25'b0000000010000000000000000: n7441_o = 1'b0;
      25'b0000000001000000000000000: n7441_o = 1'b0;
      25'b0000000000100000000000000: n7441_o = 1'b0;
      25'b0000000000010000000000000: n7441_o = 1'b0;
      25'b0000000000001000000000000: n7441_o = 1'b0;
      25'b0000000000000100000000000: n7441_o = 1'b0;
      25'b0000000000000010000000000: n7441_o = 1'b0;
      25'b0000000000000001000000000: n7441_o = 1'b0;
      25'b0000000000000000100000000: n7441_o = 1'b0;
      25'b0000000000000000010000000: n7441_o = 1'b0;
      25'b0000000000000000001000000: n7441_o = 1'b0;
      25'b0000000000000000000100000: n7441_o = 1'b0;
      25'b0000000000000000000010000: n7441_o = 1'b0;
      25'b0000000000000000000001000: n7441_o = 1'b0;
      25'b0000000000000000000000100: n7441_o = 1'b0;
      25'b0000000000000000000000010: n7441_o = 1'b0;
      25'b0000000000000000000000001: n7441_o = 1'b0;
      default: n7441_o = 1'bX;
    endcase
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7444_o = 1'b0;
      25'b0100000000000000000000000: n7444_o = 1'b0;
      25'b0010000000000000000000000: n7444_o = 1'b0;
      25'b0001000000000000000000000: n7444_o = 1'b0;
      25'b0000100000000000000000000: n7444_o = 1'b0;
      25'b0000010000000000000000000: n7444_o = n6979_o;
      25'b0000001000000000000000000: n7444_o = 1'b0;
      25'b0000000100000000000000000: n7444_o = 1'b0;
      25'b0000000010000000000000000: n7444_o = 1'b0;
      25'b0000000001000000000000000: n7444_o = 1'b0;
      25'b0000000000100000000000000: n7444_o = 1'b0;
      25'b0000000000010000000000000: n7444_o = 1'b0;
      25'b0000000000001000000000000: n7444_o = 1'b0;
      25'b0000000000000100000000000: n7444_o = 1'b0;
      25'b0000000000000010000000000: n7444_o = 1'b0;
      25'b0000000000000001000000000: n7444_o = 1'b0;
      25'b0000000000000000100000000: n7444_o = 1'b0;
      25'b0000000000000000010000000: n7444_o = 1'b0;
      25'b0000000000000000001000000: n7444_o = 1'b0;
      25'b0000000000000000000100000: n7444_o = 1'b0;
      25'b0000000000000000000010000: n7444_o = 1'b0;
      25'b0000000000000000000001000: n7444_o = 1'b0;
      25'b0000000000000000000000100: n7444_o = 1'b0;
      25'b0000000000000000000000010: n7444_o = 1'b0;
      25'b0000000000000000000000001: n7444_o = 1'b0;
      default: n7444_o = 1'bX;
    endcase
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7447_o = 1'b0;
      25'b0100000000000000000000000: n7447_o = n7232_o;
      25'b0010000000000000000000000: n7447_o = 1'b0;
      25'b0001000000000000000000000: n7447_o = 1'b0;
      25'b0000100000000000000000000: n7447_o = 1'b0;
      25'b0000010000000000000000000: n7447_o = 1'b0;
      25'b0000001000000000000000000: n7447_o = 1'b0;
      25'b0000000100000000000000000: n7447_o = 1'b0;
      25'b0000000010000000000000000: n7447_o = 1'b0;
      25'b0000000001000000000000000: n7447_o = 1'b0;
      25'b0000000000100000000000000: n7447_o = 1'b0;
      25'b0000000000010000000000000: n7447_o = 1'b0;
      25'b0000000000001000000000000: n7447_o = 1'b0;
      25'b0000000000000100000000000: n7447_o = 1'b0;
      25'b0000000000000010000000000: n7447_o = 1'b0;
      25'b0000000000000001000000000: n7447_o = 1'b0;
      25'b0000000000000000100000000: n7447_o = 1'b0;
      25'b0000000000000000010000000: n7447_o = 1'b0;
      25'b0000000000000000001000000: n7447_o = 1'b0;
      25'b0000000000000000000100000: n7447_o = 1'b0;
      25'b0000000000000000000010000: n7447_o = 1'b0;
      25'b0000000000000000000001000: n7447_o = 1'b0;
      25'b0000000000000000000000100: n7447_o = 1'b0;
      25'b0000000000000000000000010: n7447_o = 1'b0;
      25'b0000000000000000000000001: n7447_o = 1'b0;
      default: n7447_o = 1'bX;
    endcase
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7450_o = n7285_o;
      25'b0100000000000000000000000: n7450_o = n7234_o;
      25'b0010000000000000000000000: n7450_o = 1'b0;
      25'b0001000000000000000000000: n7450_o = 1'b0;
      25'b0000100000000000000000000: n7450_o = 1'b0;
      25'b0000010000000000000000000: n7450_o = 1'b0;
      25'b0000001000000000000000000: n7450_o = 1'b0;
      25'b0000000100000000000000000: n7450_o = 1'b0;
      25'b0000000010000000000000000: n7450_o = 1'b0;
      25'b0000000001000000000000000: n7450_o = 1'b0;
      25'b0000000000100000000000000: n7450_o = 1'b0;
      25'b0000000000010000000000000: n7450_o = 1'b0;
      25'b0000000000001000000000000: n7450_o = 1'b0;
      25'b0000000000000100000000000: n7450_o = 1'b0;
      25'b0000000000000010000000000: n7450_o = 1'b0;
      25'b0000000000000001000000000: n7450_o = 1'b0;
      25'b0000000000000000100000000: n7450_o = 1'b0;
      25'b0000000000000000010000000: n7450_o = 1'b0;
      25'b0000000000000000001000000: n7450_o = 1'b0;
      25'b0000000000000000000100000: n7450_o = 1'b0;
      25'b0000000000000000000010000: n7450_o = 1'b0;
      25'b0000000000000000000001000: n7450_o = 1'b0;
      25'b0000000000000000000000100: n7450_o = 1'b0;
      25'b0000000000000000000000010: n7450_o = 1'b0;
      25'b0000000000000000000000001: n7450_o = 1'b0;
      default: n7450_o = 1'bX;
    endcase
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7453_o = 2'b00;
      25'b0100000000000000000000000: n7453_o = 2'b00;
      25'b0010000000000000000000000: n7453_o = n7180_o;
      25'b0001000000000000000000000: n7453_o = n7110_o;
      25'b0000100000000000000000000: n7453_o = n7031_o;
      25'b0000010000000000000000000: n7453_o = n6982_o;
      25'b0000001000000000000000000: n7453_o = 2'b00;
      25'b0000000100000000000000000: n7453_o = 2'b00;
      25'b0000000010000000000000000: n7453_o = 2'b00;
      25'b0000000001000000000000000: n7453_o = n6793_o;
      25'b0000000000100000000000000: n7453_o = n6702_o;
      25'b0000000000010000000000000: n7453_o = 2'b00;
      25'b0000000000001000000000000: n7453_o = 2'b00;
      25'b0000000000000100000000000: n7453_o = 2'b00;
      25'b0000000000000010000000000: n7453_o = 2'b00;
      25'b0000000000000001000000000: n7453_o = 2'b00;
      25'b0000000000000000100000000: n7453_o = 2'b00;
      25'b0000000000000000010000000: n7453_o = 2'b00;
      25'b0000000000000000001000000: n7453_o = 2'b00;
      25'b0000000000000000000100000: n7453_o = 2'b00;
      25'b0000000000000000000010000: n7453_o = 2'b00;
      25'b0000000000000000000001000: n7453_o = 2'b00;
      25'b0000000000000000000000100: n7453_o = 2'b00;
      25'b0000000000000000000000010: n7453_o = 2'b00;
      25'b0000000000000000000000001: n7453_o = 2'b00;
      default: n7453_o = 2'bX;
    endcase
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7459_o = 2'b11;
      25'b0100000000000000000000000: n7459_o = 2'b11;
      25'b0010000000000000000000000: n7459_o = 2'b11;
      25'b0001000000000000000000000: n7459_o = 2'b11;
      25'b0000100000000000000000000: n7459_o = 2'b11;
      25'b0000010000000000000000000: n7459_o = 2'b11;
      25'b0000001000000000000000000: n7459_o = 2'b11;
      25'b0000000100000000000000000: n7459_o = 2'b11;
      25'b0000000010000000000000000: n7459_o = 2'b11;
      25'b0000000001000000000000000: n7459_o = 2'b11;
      25'b0000000000100000000000000: n7459_o = 2'b11;
      25'b0000000000010000000000000: n7459_o = 2'b10;
      25'b0000000000001000000000000: n7459_o = 2'b01;
      25'b0000000000000100000000000: n7459_o = 2'b00;
      25'b0000000000000010000000000: n7459_o = 2'b11;
      25'b0000000000000001000000000: n7459_o = 2'b11;
      25'b0000000000000000100000000: n7459_o = 2'b11;
      25'b0000000000000000010000000: n7459_o = 2'b11;
      25'b0000000000000000001000000: n7459_o = 2'b11;
      25'b0000000000000000000100000: n7459_o = 2'b11;
      25'b0000000000000000000010000: n7459_o = 2'b11;
      25'b0000000000000000000001000: n7459_o = 2'b11;
      25'b0000000000000000000000100: n7459_o = 2'b11;
      25'b0000000000000000000000010: n7459_o = 2'b11;
      25'b0000000000000000000000001: n7459_o = 2'b11;
      default: n7459_o = 2'bX;
    endcase
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7462_o = n7287_o;
      25'b0100000000000000000000000: n7462_o = n7236_o;
      25'b0010000000000000000000000: n7462_o = n7183_o;
      25'b0001000000000000000000000: n7462_o = n7113_o;
      25'b0000100000000000000000000: n7462_o = 1'b0;
      25'b0000010000000000000000000: n7462_o = 1'b0;
      25'b0000001000000000000000000: n7462_o = 1'b0;
      25'b0000000100000000000000000: n7462_o = n6892_o;
      25'b0000000010000000000000000: n7462_o = n6848_o;
      25'b0000000001000000000000000: n7462_o = n6797_o;
      25'b0000000000100000000000000: n7462_o = n6706_o;
      25'b0000000000010000000000000: n7462_o = 1'b0;
      25'b0000000000001000000000000: n7462_o = 1'b0;
      25'b0000000000000100000000000: n7462_o = 1'b0;
      25'b0000000000000010000000000: n7462_o = 1'b0;
      25'b0000000000000001000000000: n7462_o = n6571_o;
      25'b0000000000000000100000000: n7462_o = n6509_o;
      25'b0000000000000000010000000: n7462_o = 1'b0;
      25'b0000000000000000001000000: n7462_o = 1'b0;
      25'b0000000000000000000100000: n7462_o = 1'b0;
      25'b0000000000000000000010000: n7462_o = 1'b0;
      25'b0000000000000000000001000: n7462_o = 1'b0;
      25'b0000000000000000000000100: n7462_o = 1'b0;
      25'b0000000000000000000000010: n7462_o = 1'b0;
      25'b0000000000000000000000001: n7462_o = 1'b0;
      default: n7462_o = 1'bX;
    endcase
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7465_o = 1'b0;
      25'b0100000000000000000000000: n7465_o = 1'b0;
      25'b0010000000000000000000000: n7465_o = n7186_o;
      25'b0001000000000000000000000: n7465_o = n7116_o;
      25'b0000100000000000000000000: n7465_o = n7034_o;
      25'b0000010000000000000000000: n7465_o = 1'b0;
      25'b0000001000000000000000000: n7465_o = 1'b0;
      25'b0000000100000000000000000: n7465_o = n6895_o;
      25'b0000000010000000000000000: n7465_o = n6851_o;
      25'b0000000001000000000000000: n7465_o = 1'b0;
      25'b0000000000100000000000000: n7465_o = 1'b0;
      25'b0000000000010000000000000: n7465_o = 1'b0;
      25'b0000000000001000000000000: n7465_o = 1'b0;
      25'b0000000000000100000000000: n7465_o = 1'b0;
      25'b0000000000000010000000000: n7465_o = 1'b0;
      25'b0000000000000001000000000: n7465_o = 1'b0;
      25'b0000000000000000100000000: n7465_o = n6512_o;
      25'b0000000000000000010000000: n7465_o = n6454_o;
      25'b0000000000000000001000000: n7465_o = 1'b0;
      25'b0000000000000000000100000: n7465_o = 1'b0;
      25'b0000000000000000000010000: n7465_o = 1'b0;
      25'b0000000000000000000001000: n7465_o = 1'b0;
      25'b0000000000000000000000100: n7465_o = 1'b0;
      25'b0000000000000000000000010: n7465_o = 1'b0;
      25'b0000000000000000000000001: n7465_o = 1'b0;
      default: n7465_o = 1'bX;
    endcase
  /* T80_MCode.vhd:1773:25  */
  always @*
    case (n7293_o)
      25'b1000000000000000000000000: n7468_o = 1'b0;
      25'b0100000000000000000000000: n7468_o = 1'b0;
      25'b0010000000000000000000000: n7468_o = 1'b0;
      25'b0001000000000000000000000: n7468_o = 1'b0;
      25'b0000100000000000000000000: n7468_o = 1'b0;
      25'b0000010000000000000000000: n7468_o = 1'b0;
      25'b0000001000000000000000000: n7468_o = 1'b0;
      25'b0000000100000000000000000: n7468_o = n6898_o;
      25'b0000000010000000000000000: n7468_o = n6854_o;
      25'b0000000001000000000000000: n7468_o = n6801_o;
      25'b0000000000100000000000000: n7468_o = n6710_o;
      25'b0000000000010000000000000: n7468_o = 1'b0;
      25'b0000000000001000000000000: n7468_o = 1'b0;
      25'b0000000000000100000000000: n7468_o = 1'b0;
      25'b0000000000000010000000000: n7468_o = 1'b0;
      25'b0000000000000001000000000: n7468_o = n6575_o;
      25'b0000000000000000100000000: n7468_o = n6515_o;
      25'b0000000000000000010000000: n7468_o = 1'b0;
      25'b0000000000000000001000000: n7468_o = 1'b0;
      25'b0000000000000000000100000: n7468_o = 1'b0;
      25'b0000000000000000000010000: n7468_o = 1'b0;
      25'b0000000000000000000001000: n7468_o = 1'b0;
      25'b0000000000000000000000100: n7468_o = 1'b0;
      25'b0000000000000000000000010: n7468_o = 1'b0;
      25'b0000000000000000000000001: n7468_o = 1'b0;
      default: n7468_o = 1'bX;
    endcase
  assign n7469_o = {n5813_o, n4725_o};
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7470_o = n5791_o;
      2'b01: n7470_o = n4479_o;
      default: n7470_o = n7309_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7472_o = n5793_o;
      2'b01: n7472_o = n4484_o;
      default: n7472_o = n7315_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7474_o = 2'b00;
      2'b01: n7474_o = n4490_o;
      default: n7474_o = 2'b00;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7477_o = 1'b0;
      2'b01: n7477_o = n4493_o;
      default: n7477_o = n7318_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7480_o = 1'b0;
      2'b01: n7480_o = n4496_o;
      default: n7480_o = n7321_o;
    endcase
  assign n7482_o = n7327_o[1:0]; // extract
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7484_o = 2'b00;
      2'b01: n7484_o = n4508_o;
      default: n7484_o = n7482_o;
    endcase
  assign n7485_o = n4520_o[0]; // extract
  assign n7486_o = n7327_o[2]; // extract
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7488_o = 1'b0;
      2'b01: n7488_o = n7485_o;
      default: n7488_o = n7486_o;
    endcase
  assign n7489_o = n4520_o[1]; // extract
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7491_o = 1'b0;
      2'b01: n7491_o = n7489_o;
      default: n7491_o = n7333_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7495_o = n5796_o;
      2'b01: n7495_o = n4529_o;
      default: n7495_o = n7336_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7498_o = 1'b0;
      2'b01: n7498_o = n4532_o;
      default: n7498_o = n7340_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7500_o = n4726_o;
      2'b01: n7500_o = n4535_o;
      default: n7500_o = n7346_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7502_o = 1'b0;
      2'b01: n7502_o = n4538_o;
      default: n7502_o = n7352_o;
    endcase
  assign n7505_o = n5798_o[0]; // extract
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7506_o = n7505_o;
      2'b01: n7506_o = n4563_o;
      default: n7506_o = n7366_o;
    endcase
  assign n7507_o = n5798_o[2:1]; // extract
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7508_o = n7507_o;
      2'b01: n7508_o = n4588_o;
      default: n7508_o = n7380_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7510_o = 1'b0;
      2'b01: n7510_o = n4607_o;
      default: n7510_o = n7391_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7514_o = n5800_o;
      2'b01: n7514_o = n4613_o;
      default: n7514_o = n7394_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7515_o = n5803_o;
      2'b01: n7515_o = n4621_o;
      default: n7515_o = n7398_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7518_o = 1'b0;
      2'b01: n7518_o = n4625_o;
      default: n7518_o = 1'b0;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7521_o = 1'b0;
      2'b01: n7521_o = n4630_o;
      default: n7521_o = n7401_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7524_o = 1'b0;
      2'b01: n7524_o = n4633_o;
      default: n7524_o = 1'b0;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7526_o = n5805_o;
      2'b01: n7526_o = n4636_o;
      default: n7526_o = n7404_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7529_o = 1'b0;
      2'b01: n7529_o = n4639_o;
      default: n7529_o = n7407_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7532_o = 1'b0;
      2'b01: n7532_o = n4642_o;
      default: n7532_o = n7410_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7535_o = 1'b0;
      2'b01: n7535_o = n4645_o;
      default: n7535_o = 1'b0;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7538_o = 1'b0;
      2'b01: n7538_o = n4649_o;
      default: n7538_o = 1'b0;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7541_o = 1'b0;
      2'b01: n7541_o = n4652_o;
      default: n7541_o = 1'b0;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7544_o = 1'b0;
      2'b01: n7544_o = n4655_o;
      default: n7544_o = 1'b0;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7547_o = 1'b0;
      2'b01: n7547_o = n4658_o;
      default: n7547_o = n7413_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7550_o = 1'b0;
      2'b01: n7550_o = n4661_o;
      default: n7550_o = n7416_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7553_o = 1'b0;
      2'b01: n7553_o = n4665_o;
      default: n7553_o = 1'b0;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7556_o = 3'b000;
      2'b01: n7556_o = 3'b000;
      default: n7556_o = n7423_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7559_o = 1'b0;
      2'b01: n7559_o = n4669_o;
      default: n7559_o = 1'b0;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7562_o = 1'b0;
      2'b01: n7562_o = n4673_o;
      default: n7562_o = 1'b0;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7565_o = 1'b0;
      2'b01: n7565_o = n4677_o;
      default: n7565_o = 1'b0;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7568_o = 1'b0;
      2'b01: n7568_o = n4681_o;
      default: n7568_o = 1'b0;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7571_o = 1'b0;
      2'b01: n7571_o = n4684_o;
      default: n7571_o = 1'b0;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7574_o = 1'b0;
      2'b01: n7574_o = n4687_o;
      default: n7574_o = 1'b0;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7577_o = 1'b0;
      2'b01: n7577_o = n4691_o;
      default: n7577_o = 1'b0;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7580_o = 1'b0;
      2'b01: n7580_o = n4695_o;
      default: n7580_o = 1'b0;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7583_o = 1'b0;
      2'b01: n7583_o = n4699_o;
      default: n7583_o = 1'b0;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7586_o = 1'b0;
      2'b01: n7586_o = 1'b0;
      default: n7586_o = n7426_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7589_o = 1'b0;
      2'b01: n7589_o = 1'b0;
      default: n7589_o = n7429_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7592_o = 1'b0;
      2'b01: n7592_o = 1'b0;
      default: n7592_o = n7432_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7595_o = 1'b0;
      2'b01: n7595_o = 1'b0;
      default: n7595_o = n7435_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7598_o = 1'b0;
      2'b01: n7598_o = 1'b0;
      default: n7598_o = n7438_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7601_o = 1'b0;
      2'b01: n7601_o = 1'b0;
      default: n7601_o = n7441_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7604_o = 1'b0;
      2'b01: n7604_o = 1'b0;
      default: n7604_o = n7444_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7607_o = 1'b0;
      2'b01: n7607_o = 1'b0;
      default: n7607_o = n7447_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7610_o = 1'b0;
      2'b01: n7610_o = 1'b0;
      default: n7610_o = n7450_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7613_o = 2'b00;
      2'b01: n7613_o = n4702_o;
      default: n7613_o = n7453_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7616_o = 1'b0;
      2'b01: n7616_o = n4706_o;
      default: n7616_o = 1'b0;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7619_o = 1'b0;
      2'b01: n7619_o = n4710_o;
      default: n7619_o = 1'b0;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7622_o = 2'b11;
      2'b01: n7622_o = 2'b11;
      default: n7622_o = n7459_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7625_o = 1'b0;
      2'b01: n7625_o = n4714_o;
      default: n7625_o = 1'b0;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7628_o = 1'b0;
      2'b01: n7628_o = n4717_o;
      default: n7628_o = n7462_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7630_o = n5808_o;
      2'b01: n7630_o = n4720_o;
      default: n7630_o = n7465_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7633_o = 1'b0;
      2'b01: n7633_o = n4723_o;
      default: n7633_o = n7468_o;
    endcase
  /* T80_MCode.vhd:265:17  */
  always @*
    case (n7469_o)
      2'b10: n7636_o = n5811_o;
      2'b01: n7636_o = 1'b0;
      default: n7636_o = 1'b0;
    endcase
  /* T80_MCode.vhd:2254:35  */
  assign n7639_o = mcycle == 3'b110;
  /* T80_MCode.vhd:2262:40  */
  assign n7641_o = ir == 8'b00110110;
  /* T80_MCode.vhd:2262:60  */
  assign n7643_o = ir == 8'b11001011;
  /* T80_MCode.vhd:2262:53  */
  assign n7644_o = n7641_o | n7643_o;
  /* T80_MCode.vhd:2254:25  */
  assign n7646_o = n7657_o ? 3'b111 : n7526_o;
  /* T80_MCode.vhd:2265:45  */
  assign n7648_o = ir == 8'b00110110;
  /* T80_MCode.vhd:2265:66  */
  assign n7650_o = iset == 2'b01;
  /* T80_MCode.vhd:2265:58  */
  assign n7651_o = n7648_o | n7650_o;
  /* T80_MCode.vhd:2265:36  */
  assign n7652_o = ~n7651_o;
  /* T80_MCode.vhd:2254:25  */
  assign n7654_o = n7658_o ? 1'b1 : n7633_o;
  /* T80_MCode.vhd:2254:25  */
  assign n7656_o = n7639_o ? 1'b1 : n7477_o;
  /* T80_MCode.vhd:2254:25  */
  assign n7657_o = n7644_o & n7639_o;
  /* T80_MCode.vhd:2254:25  */
  assign n7658_o = n7652_o & n7639_o;
  /* T80_MCode.vhd:2269:35  */
  assign n7660_o = mcycle == 3'b111;
  /* T80_MCode.vhd:2273:41  */
  assign n7662_o = iset != 2'b01;
  /* T80_MCode.vhd:2269:25  */
  assign n7664_o = n7681_o ? 3'b010 : n7646_o;
  /* T80_MCode.vhd:2278:40  */
  assign n7667_o = ir == 8'b00110110;
  /* T80_MCode.vhd:2278:61  */
  assign n7669_o = iset == 2'b01;
  /* T80_MCode.vhd:2278:53  */
  assign n7670_o = n7667_o | n7669_o;
  /* T80_MCode.vhd:2269:25  */
  assign n7672_o = n7677_o ? 1'b1 : n7656_o;
  /* T80_MCode.vhd:2278:33  */
  assign n7674_o = n7670_o ? n7628_o : 1'b1;
  /* T80_MCode.vhd:2269:25  */
  assign n7676_o = n7660_o ? 3'b101 : n7472_o;
  /* T80_MCode.vhd:2269:25  */
  assign n7677_o = n7670_o & n7660_o;
  assign n7678_o = {1'b0, n2491_o};
  assign n7679_o = {n7510_o, n7508_o, n7506_o};
  /* T80_MCode.vhd:2269:25  */
  assign n7680_o = n7660_o ? n7678_o : n7679_o;
  /* T80_MCode.vhd:2269:25  */
  assign n7681_o = n7662_o & n7660_o;
  /* T80_MCode.vhd:2269:25  */
  assign n7682_o = n7660_o ? n7674_o : n7628_o;
  assign n7686_o = {n7491_o, n7488_o, n7484_o};
  assign n7687_o = {n7502_o, n7500_o};
endmodule

module t80_0_1_0_1_2_3_4_5_6_7
  (input  reset_n,
   input  clk_n,
   input  cen,
   input  wait_n,
   input  int_n,
   input  nmi_n,
   input  busrq_n,
   input  [7:0] dinst,
   input  [7:0] di,
   input  r800_mode,
   input  out0,
   input  dirset,
   input  [211:0] dir,
   output m1_n,
   output iorq,
   output noread,
   output write,
   output rfsh_n,
   output halt_n,
   output busak_n,
   output [15:0] a,
   output [7:0] do_,
   output [2:0] mc,
   output [2:0] ts,
   output intcycle_n,
   output inte,
   output stop,
   output [211:0] \reg );
  wire [7:0] acc;
  wire [7:0] f;
  wire [7:0] ap;
  wire [7:0] fp;
  wire [7:0] i;
  wire [7:0] r;
  wire [15:0] sp;
  wire [15:0] pc;
  wire [7:0] regdih;
  wire [7:0] regdil;
  wire [15:0] regbusa;
  wire [15:0] regbusb;
  wire [15:0] regbusc;
  wire [2:0] regaddra_r;
  wire [2:0] regaddra;
  wire [2:0] regaddrb_r;
  wire [2:0] regaddrb;
  wire [2:0] regaddrc;
  wire regweh;
  wire regwel;
  wire alternate;
  wire [15:0] wz;
  wire [15:0] tmpaddr2;
  wire [7:0] ir;
  wire [1:0] iset;
  wire [15:0] regbusa_r;
  wire [31:0] mulu_prod32;
  wire [31:0] mulu_tmp;
  wire [15:0] mulu_fakt1;
  wire [15:0] id16;
  wire [7:0] save_mux;
  wire [2:0] tstate;
  wire [2:0] mcycle;
  wire inte_ff1;
  wire inte_ff2;
  wire halt_ff;
  reg busreq_s;
  reg busack;
  wire clken;
  wire nmi_s;
  wire [1:0] istatus;
  wire [7:0] di_reg;
  wire t_res;
  wire [1:0] xy_state;
  wire [2:0] pre_xy_f_m;
  wire nextis_xy_fetch;
  wire xy_ind;
  wire no_btr;
  wire btr_r;
  wire btr_r2;
  wire auto_wait;
  wire auto_wait_t1;
  wire auto_wait_t2;
  wire incdecz;
  wire [7:0] last_b;
  wire [7:0] q_reg;
  wire [7:0] f_last;
  wire [7:0] busb;
  wire [7:0] busa;
  wire [7:0] aluqs;
  wire [7:0] f_out;
  wire [4:0] read_to_reg_r;
  wire arith16_r;
  wire z16_r;
  wire [3:0] alu_op_r;
  wire save_alu_r;
  wire preservec_r;
  wire [2:0] mcycles;
  wire [2:0] mcycles_d;
  wire [2:0] tstates;
  wire intcycle;
  wire nmicycle;
  wire inc_pc;
  wire inc_wz;
  wire [3:0] incdec_16;
  wire [1:0] prefix;
  wire read_to_acc;
  wire read_to_reg;
  wire [3:0] set_busb_to;
  wire [3:0] set_busa_to;
  wire [3:0] alu_op;
  wire save_alu;
  wire rot_akku;
  wire preservec;
  wire arith16;
  wire [2:0] set_addr_to;
  wire jump;
  wire jumpe;
  wire jumpxy;
  wire call;
  wire rstp;
  wire ldz;
  wire ldw;
  wire ldsphl;
  wire ldhlsp;
  wire addspdd;
  wire iorq_i;
  wire [2:0] special_ld;
  wire exchangedh;
  wire exchangerp;
  wire exchangeaf;
  wire exchangers;
  wire exchangewh;
  wire i_djnz;
  wire i_cpl;
  wire i_ccf;
  wire i_scf;
  wire i_retn;
  wire i_bt;
  wire i_bc;
  wire i_btr;
  wire i_rld;
  wire i_rrd;
  wire i_rxdd;
  wire i_inrc;
  wire i_mulub;
  wire i_mulu;
  wire [1:0] setwz;
  wire setdi;
  wire setei;
  wire [1:0] imode;
  wire halt;
  wire xybit_undoc;
  wire no_pc;
  wire [127:0] dor;
  wire [1:0] n209_o;
  wire [3:0] n210_o;
  wire [131:0] n211_o;
  wire [147:0] n212_o;
  wire [163:0] n213_o;
  wire [171:0] n214_o;
  wire [179:0] n215_o;
  wire [187:0] n216_o;
  wire [195:0] n217_o;
  wire [203:0] n218_o;
  wire [211:0] n219_o;
  wire n220_o;
  wire [211:0] n221_o;
  wire [1:0] n222_o;
  wire [3:0] n223_o;
  wire [15:0] n224_o;
  wire [19:0] n225_o;
  wire [47:0] n226_o;
  wire [67:0] n227_o;
  wire [15:0] n228_o;
  wire [83:0] n229_o;
  wire [47:0] n230_o;
  wire [131:0] n231_o;
  wire [147:0] n232_o;
  wire [163:0] n233_o;
  wire [171:0] n234_o;
  wire [179:0] n235_o;
  wire [187:0] n236_o;
  wire [195:0] n237_o;
  wire [203:0] n238_o;
  wire [211:0] n239_o;
  wire [2:0] mcode_n240;
  wire [2:0] mcode_n241;
  wire [1:0] mcode_n242;
  wire mcode_n243;
  wire mcode_n244;
  wire [3:0] mcode_n245;
  wire mcode_n246;
  wire mcode_n247;
  wire [3:0] mcode_n248;
  wire [3:0] mcode_n249;
  wire [3:0] mcode_n250;
  wire mcode_n251;
  wire mcode_n252;
  wire mcode_n253;
  wire mcode_n254;
  wire [2:0] mcode_n255;
  wire mcode_n256;
  wire mcode_n257;
  wire mcode_n258;
  wire mcode_n259;
  wire mcode_n260;
  wire mcode_n261;
  wire mcode_n262;
  wire mcode_n263;
  wire mcode_n264;
  wire mcode_n265;
  wire mcode_n266;
  wire [2:0] mcode_n267;
  wire mcode_n268;
  wire mcode_n269;
  wire mcode_n270;
  wire mcode_n271;
  wire mcode_n272;
  wire mcode_n273;
  wire mcode_n274;
  wire mcode_n275;
  wire mcode_n276;
  wire mcode_n277;
  wire mcode_n278;
  wire mcode_n279;
  wire mcode_n280;
  wire mcode_n281;
  wire mcode_n282;
  wire mcode_n283;
  wire mcode_n284;
  wire mcode_n285;
  wire [1:0] mcode_n286;
  wire mcode_n287;
  wire mcode_n288;
  wire [1:0] mcode_n289;
  wire mcode_n290;
  wire mcode_n291;
  wire mcode_n292;
  wire mcode_n293;
  wire mcode_n294;
  wire [2:0] mcode_mcycles;
  wire [2:0] mcode_tstates;
  wire [1:0] mcode_prefix;
  wire mcode_inc_pc;
  wire mcode_inc_wz;
  wire [3:0] mcode_incdec_16;
  wire mcode_read_to_reg;
  wire mcode_read_to_acc;
  wire [3:0] mcode_set_busa_to;
  wire [3:0] mcode_set_busb_to;
  wire [3:0] mcode_alu_op;
  wire mcode_save_alu;
  wire mcode_rot_akku;
  wire mcode_preservec;
  wire mcode_arith16;
  wire [2:0] mcode_set_addr_to;
  wire mcode_iorq;
  wire mcode_jump;
  wire mcode_jumpe;
  wire mcode_jumpxy;
  wire mcode_call;
  wire mcode_rstp;
  wire mcode_ldz;
  wire mcode_ldw;
  wire mcode_ldsphl;
  wire mcode_ldhlsp;
  wire mcode_addspdd;
  wire [2:0] mcode_special_ld;
  wire mcode_exchangedh;
  wire mcode_exchangerp;
  wire mcode_exchangeaf;
  wire mcode_exchangers;
  wire mcode_exchangewh;
  wire mcode_i_djnz;
  wire mcode_i_cpl;
  wire mcode_i_ccf;
  wire mcode_i_scf;
  wire mcode_i_retn;
  wire mcode_i_bt;
  wire mcode_i_bc;
  wire mcode_i_btr;
  wire mcode_i_rld;
  wire mcode_i_rrd;
  wire mcode_i_inrc;
  wire mcode_i_mulub;
  wire mcode_i_mulu;
  wire [1:0] mcode_setwz;
  wire mcode_setdi;
  wire mcode_setei;
  wire [1:0] mcode_imode;
  wire mcode_halt;
  wire mcode_noread;
  wire mcode_write;
  wire mcode_no_pc;
  wire mcode_xybit_undoc;
  wire [5:0] n405_o;
  wire [7:0] alu_n406;
  wire [7:0] alu_n407;
  wire [7:0] alu_q;
  wire [7:0] alu_f_out;
  wire n412_o;
  wire n413_o;
  wire n415_o;
  wire n416_o;
  wire n420_o;
  wire n421_o;
  wire n422_o;
  wire n424_o;
  wire n426_o;
  wire n428_o;
  wire n429_o;
  wire n430_o;
  wire n432_o;
  wire n434_o;
  wire n435_o;
  wire n436_o;
  wire n437_o;
  wire n438_o;
  wire [7:0] n440_o;
  wire n441_o;
  wire [7:0] n442_o;
  wire n450_o;
  wire [7:0] n452_o;
  wire [7:0] n453_o;
  wire [7:0] n454_o;
  wire [7:0] n455_o;
  wire [7:0] n456_o;
  wire [7:0] n457_o;
  wire [15:0] n458_o;
  wire [15:0] n459_o;
  wire [15:0] n460_o;
  wire [1:0] n461_o;
  wire n463_o;
  wire n464_o;
  wire n466_o;
  wire n467_o;
  wire [7:0] n468_o;
  wire [8:0] n470_o;
  wire [8:0] n472_o;
  wire [8:0] n473_o;
  wire [3:0] n474_o;
  wire [4:0] n476_o;
  wire [3:0] n477_o;
  wire [4:0] n479_o;
  wire [4:0] n480_o;
  wire n483_o;
  wire n484_o;
  wire [1:0] n485_o;
  wire [1:0] n486_o;
  wire [1:0] n487_o;
  wire n488_o;
  wire n489_o;
  wire n490_o;
  wire n491_o;
  wire n495_o;
  wire n496_o;
  wire [7:0] n497_o;
  wire [8:0] n499_o;
  wire [8:0] n501_o;
  wire [8:0] n502_o;
  wire [3:0] n503_o;
  wire [4:0] n505_o;
  wire [3:0] n506_o;
  wire [4:0] n508_o;
  wire [4:0] n509_o;
  wire n512_o;
  wire n513_o;
  wire [1:0] n514_o;
  wire [1:0] n515_o;
  wire n516_o;
  wire n517_o;
  wire n521_o;
  wire [1:0] n522_o;
  wire n524_o;
  wire n525_o;
  wire n526_o;
  wire n527_o;
  wire n528_o;
  wire n529_o;
  wire n531_o;
  wire n532_o;
  wire n535_o;
  wire n537_o;
  wire n538_o;
  wire n539_o;
  wire n540_o;
  wire n542_o;
  wire n543_o;
  wire [6:0] n544_o;
  wire [6:0] n546_o;
  wire n547_o;
  wire n548_o;
  wire n549_o;
  wire n550_o;
  wire n551_o;
  wire n552_o;
  wire n553_o;
  wire n554_o;
  wire n555_o;
  wire n556_o;
  wire [15:0] n558_o;
  wire [15:0] n559_o;
  wire n561_o;
  wire n562_o;
  wire n564_o;
  wire n565_o;
  wire n566_o;
  wire n567_o;
  wire [7:0] n569_o;
  wire [7:0] n571_o;
  wire n573_o;
  wire n575_o;
  wire n576_o;
  wire [7:0] n577_o;
  wire [7:0] n578_o;
  wire n580_o;
  wire n582_o;
  wire n583_o;
  wire [1:0] n586_o;
  wire n588_o;
  wire [1:0] n590_o;
  wire n592_o;
  wire [1:0] n594_o;
  wire [1:0] n595_o;
  wire n596_o;
  wire [1:0] n598_o;
  wire [1:0] n601_o;
  wire n603_o;
  wire [15:0] n604_o;
  wire [15:0] n605_o;
  wire [6:0] n606_o;
  wire [6:0] n607_o;
  wire n608_o;
  wire n610_o;
  wire [7:0] n611_o;
  wire [1:0] n612_o;
  wire [1:0] n613_o;
  wire n614_o;
  wire n616_o;
  wire n618_o;
  wire [1:0] n620_o;
  wire n621_o;
  wire n623_o;
  wire n624_o;
  wire n625_o;
  wire n626_o;
  wire n627_o;
  wire [7:0] n628_o;
  wire [7:0] n629_o;
  wire n630_o;
  wire n631_o;
  wire n632_o;
  wire n634_o;
  wire n636_o;
  wire n638_o;
  wire n639_o;
  wire n641_o;
  wire n642_o;
  wire [7:0] n643_o;
  wire [7:0] n644_o;
  wire n646_o;
  wire [15:0] n647_o;
  wire [15:0] n648_o;
  wire n650_o;
  wire [15:0] n651_o;
  wire [15:0] n654_o;
  wire n656_o;
  wire n658_o;
  wire n660_o;
  wire [15:0] n663_o;
  wire [15:0] n664_o;
  wire n666_o;
  wire [7:0] n667_o;
  wire [7:0] n670_o;
  wire [15:0] n671_o;
  wire [15:0] n672_o;
  wire n674_o;
  wire n676_o;
  wire [7:0] n677_o;
  wire [7:0] n680_o;
  wire [15:0] n681_o;
  wire [15:0] n682_o;
  wire n684_o;
  wire [15:0] n686_o;
  wire [7:0] n687_o;
  wire n689_o;
  wire [7:0] n690_o;
  wire [7:0] n693_o;
  wire [15:0] n694_o;
  wire [15:0] n695_o;
  wire [15:0] n696_o;
  wire [15:0] n697_o;
  wire [15:0] n698_o;
  wire n700_o;
  wire n702_o;
  wire [3:0] n703_o;
  wire n705_o;
  wire n706_o;
  wire [1:0] n707_o;
  wire n709_o;
  wire n710_o;
  wire n712_o;
  wire n713_o;
  wire n714_o;
  wire n715_o;
  wire n716_o;
  wire n717_o;
  wire n718_o;
  wire n719_o;
  wire n721_o;
  wire [15:0] n722_o;
  wire [15:0] n723_o;
  wire [5:0] n724_o;
  wire [7:0] n725_o;
  wire [7:0] n726_o;
  wire [7:0] n727_o;
  wire [7:0] n728_o;
  wire [7:0] n729_o;
  wire [7:0] n730_o;
  reg [7:0] n731_o;
  wire [7:0] n732_o;
  wire [7:0] n733_o;
  wire [7:0] n734_o;
  wire [7:0] n735_o;
  wire [7:0] n736_o;
  wire [7:0] n737_o;
  reg [7:0] n738_o;
  reg [15:0] n739_o;
  wire [15:0] n740_o;
  wire [15:0] n741_o;
  wire [15:0] n742_o;
  wire [15:0] n743_o;
  wire [15:0] n744_o;
  wire [15:0] n745_o;
  wire [15:0] n747_o;
  wire [15:0] n749_o;
  wire [15:0] n750_o;
  wire [15:0] n751_o;
  wire [15:0] n752_o;
  wire [15:0] n753_o;
  wire [15:0] n754_o;
  wire [15:0] n755_o;
  wire [15:0] n756_o;
  wire [15:0] n757_o;
  wire [15:0] n758_o;
  wire [15:0] n759_o;
  wire [15:0] n760_o;
  wire [15:0] n761_o;
  wire n763_o;
  wire [15:0] n764_o;
  wire [7:0] n765_o;
  wire n766_o;
  wire n767_o;
  wire n769_o;
  wire n770_o;
  wire [7:0] n772_o;
  wire [2:0] n773_o;
  wire n774_o;
  wire n775_o;
  wire n776_o;
  wire n777_o;
  wire [2:0] n778_o;
  wire [2:0] n779_o;
  wire n780_o;
  wire n781_o;
  wire n782_o;
  wire n783_o;
  wire n784_o;
  wire n785_o;
  wire n786_o;
  wire n787_o;
  wire n788_o;
  wire n789_o;
  wire n790_o;
  wire n791_o;
  wire n792_o;
  wire n793_o;
  wire n794_o;
  wire n796_o;
  wire n797_o;
  wire n798_o;
  wire n799_o;
  wire n800_o;
  wire n801_o;
  wire n802_o;
  wire n803_o;
  wire n804_o;
  wire n805_o;
  wire n806_o;
  wire n807_o;
  wire [1:0] n808_o;
  wire [2:0] n809_o;
  wire n810_o;
  wire [1:0] n811_o;
  wire [1:0] n812_o;
  wire [2:0] n813_o;
  wire n814_o;
  wire n815_o;
  wire n816_o;
  wire n817_o;
  wire n819_o;
  wire n820_o;
  wire n821_o;
  wire n822_o;
  wire n823_o;
  wire n824_o;
  wire n826_o;
  wire n827_o;
  wire n828_o;
  wire n829_o;
  wire n830_o;
  wire n831_o;
  wire n833_o;
  wire n834_o;
  wire n835_o;
  wire n836_o;
  wire n837_o;
  wire n838_o;
  wire n839_o;
  wire n840_o;
  wire n841_o;
  wire n842_o;
  wire n843_o;
  wire n844_o;
  wire [1:0] n845_o;
  wire [2:0] n846_o;
  wire [1:0] n847_o;
  wire [2:0] n848_o;
  wire n849_o;
  wire n850_o;
  wire [15:0] n851_o;
  wire n852_o;
  wire [1:0] n853_o;
  wire n854_o;
  wire n855_o;
  wire [2:0] n856_o;
  wire [2:0] n857_o;
  wire [15:0] n858_o;
  wire [15:0] n859_o;
  wire n860_o;
  wire n861_o;
  wire n862_o;
  wire n863_o;
  wire n864_o;
  wire n865_o;
  wire [3:0] n867_o;
  wire n869_o;
  wire n871_o;
  wire n872_o;
  wire n873_o;
  wire n874_o;
  wire n876_o;
  wire n877_o;
  wire n878_o;
  wire n879_o;
  wire n880_o;
  wire n881_o;
  wire n882_o;
  wire n883_o;
  wire [8:0] n885_o;
  wire [7:0] n886_o;
  wire [8:0] n888_o;
  wire [8:0] n889_o;
  wire [8:0] n891_o;
  wire [7:0] n892_o;
  wire [8:0] n894_o;
  wire [8:0] n895_o;
  wire [8:0] n896_o;
  wire n897_o;
  wire n898_o;
  wire n899_o;
  wire [8:0] n901_o;
  wire [8:0] n903_o;
  wire [8:0] n904_o;
  wire n905_o;
  wire n906_o;
  wire n907_o;
  wire n908_o;
  wire n909_o;
  wire n910_o;
  wire n911_o;
  wire n912_o;
  wire n913_o;
  wire n914_o;
  wire n915_o;
  wire n916_o;
  wire n917_o;
  wire n918_o;
  wire n919_o;
  wire n920_o;
  wire [2:0] n921_o;
  wire n922_o;
  wire [2:0] n923_o;
  wire [2:0] n924_o;
  wire n925_o;
  wire n926_o;
  wire n927_o;
  wire n928_o;
  wire [7:0] n929_o;
  wire n932_o;
  wire n933_o;
  wire n935_o;
  wire n937_o;
  wire n938_o;
  wire [7:0] n939_o;
  wire [15:0] n940_o;
  wire [15:0] n941_o;
  wire [15:0] n942_o;
  wire [15:0] n943_o;
  wire [15:0] n945_o;
  wire [15:0] n946_o;
  wire [15:0] n947_o;
  wire [15:0] n948_o;
  wire [15:0] n950_o;
  wire [15:0] n953_o;
  wire n954_o;
  wire n955_o;
  wire n956_o;
  wire n957_o;
  wire n958_o;
  wire n959_o;
  wire n960_o;
  wire n961_o;
  wire n962_o;
  wire n963_o;
  wire n964_o;
  wire n965_o;
  wire [7:0] n968_o;
  wire n969_o;
  wire n970_o;
  wire n971_o;
  wire n972_o;
  wire n973_o;
  wire n974_o;
  wire n975_o;
  wire [7:0] n978_o;
  wire n979_o;
  wire [7:0] n980_o;
  wire n981_o;
  wire [7:0] n982_o;
  wire n983_o;
  wire n984_o;
  wire n985_o;
  wire n986_o;
  wire n987_o;
  wire n988_o;
  wire n989_o;
  wire n990_o;
  wire n991_o;
  wire n992_o;
  wire [3:0] n994_o;
  wire n995_o;
  wire [3:0] n996_o;
  wire [3:0] n997_o;
  wire [15:0] n998_o;
  wire [15:0] n999_o;
  wire [2:0] n1002_o;
  localparam [15:0] n1003_o = 16'b0000000000000000;
  wire [9:0] n1004_o;
  wire [2:0] n1005_o;
  wire [15:0] n1006_o;
  wire [15:0] n1007_o;
  wire n1008_o;
  wire [3:0] n1009_o;
  wire [3:0] n1010_o;
  wire [1:0] n1011_o;
  wire [15:0] n1012_o;
  wire [15:0] n1013_o;
  wire n1014_o;
  wire n1018_o;
  wire n1020_o;
  wire n1021_o;
  wire [15:0] n1022_o;
  wire [15:0] n1023_o;
  wire [15:0] n1024_o;
  wire n1026_o;
  wire n1028_o;
  wire n1029_o;
  wire n1030_o;
  wire n1031_o;
  wire n1032_o;
  wire [15:0] n1035_o;
  wire [15:0] n1036_o;
  wire n1037_o;
  wire n1039_o;
  wire n1040_o;
  wire n1042_o;
  wire n1044_o;
  wire n1045_o;
  wire n1046_o;
  wire [2:0] n1047_o;
  wire n1049_o;
  wire n1050_o;
  wire [15:0] n1052_o;
  wire [15:0] n1054_o;
  wire [15:0] n1055_o;
  wire [15:0] n1056_o;
  wire n1057_o;
  wire n1059_o;
  wire n1060_o;
  wire [15:0] n1061_o;
  wire [15:0] n1062_o;
  wire [15:0] n1063_o;
  wire [15:0] n1064_o;
  wire [15:0] n1065_o;
  wire [7:0] n1066_o;
  wire n1067_o;
  wire [7:0] n1068_o;
  wire [7:0] n1069_o;
  wire [7:0] n1070_o;
  wire [7:0] n1071_o;
  wire n1072_o;
  wire n1073_o;
  wire [15:0] n1074_o;
  wire [7:0] n1075_o;
  wire [1:0] n1076_o;
  wire n1077_o;
  wire n1078_o;
  wire [7:0] n1079_o;
  wire [7:0] n1080_o;
  wire [7:0] n1081_o;
  wire [7:0] n1082_o;
  wire n1084_o;
  wire [15:0] n1085_o;
  wire [15:0] n1086_o;
  wire n1087_o;
  wire [7:0] n1088_o;
  wire [7:0] n1089_o;
  wire [7:0] n1090_o;
  wire [7:0] n1091_o;
  wire [7:0] n1092_o;
  wire [7:0] n1093_o;
  wire [1:0] n1094_o;
  wire n1095_o;
  wire n1096_o;
  wire n1097_o;
  wire n1098_o;
  wire [7:0] n1099_o;
  wire n1100_o;
  wire n1101_o;
  wire n1102_o;
  wire n1103_o;
  wire [3:0] n1105_o;
  wire n1108_o;
  wire n1114_o;
  wire [7:0] n1115_o;
  wire [7:0] n1116_o;
  wire n1117_o;
  wire [1:0] n1118_o;
  wire n1119_o;
  wire n1121_o;
  wire n1124_o;
  wire n1125_o;
  wire n1127_o;
  wire n1130_o;
  wire n1131_o;
  wire n1133_o;
  wire n1136_o;
  wire n1137_o;
  wire n1139_o;
  wire n1142_o;
  wire n1144_o;
  wire [2:0] n1145_o;
  reg [7:0] n1146_o;
  wire n1147_o;
  reg n1148_o;
  wire n1149_o;
  reg n1150_o;
  wire n1151_o;
  reg n1152_o;
  wire n1153_o;
  reg n1154_o;
  wire n1155_o;
  reg n1156_o;
  wire n1157_o;
  reg n1158_o;
  wire n1159_o;
  reg n1160_o;
  reg [7:0] n1161_o;
  wire n1162_o;
  wire [7:0] n1163_o;
  reg [7:0] n1164_o;
  wire [7:0] n1165_o;
  wire [6:0] n1166_o;
  wire [6:0] n1167_o;
  wire [6:0] n1168_o;
  wire [7:0] n1169_o;
  wire n1170_o;
  wire [7:0] n1171_o;
  wire [7:0] n1172_o;
  wire n1173_o;
  wire n1175_o;
  wire n1176_o;
  wire n1177_o;
  wire n1178_o;
  wire [7:0] n1179_o;
  wire [7:0] n1180_o;
  wire [15:0] n1181_o;
  wire [15:0] n1182_o;
  wire [15:0] n1183_o;
  wire n1184_o;
  wire n1185_o;
  wire n1187_o;
  wire n1188_o;
  wire [6:0] n1189_o;
  wire n1190_o;
  wire n1191_o;
  wire n1192_o;
  wire [7:0] n1193_o;
  wire [7:0] n1194_o;
  wire [7:0] n1195_o;
  wire n1196_o;
  wire n1199_o;
  wire n1200_o;
  wire n1202_o;
  wire n1205_o;
  wire n1206_o;
  wire n1207_o;
  wire n1208_o;
  wire n1209_o;
  wire n1210_o;
  wire n1211_o;
  wire n1212_o;
  wire n1213_o;
  wire n1214_o;
  wire n1215_o;
  wire n1216_o;
  wire n1217_o;
  wire n1218_o;
  wire n1219_o;
  wire n1220_o;
  wire n1221_o;
  wire n1222_o;
  wire [6:0] n1223_o;
  wire [6:0] n1224_o;
  wire [6:0] n1225_o;
  wire n1226_o;
  wire n1228_o;
  wire n1229_o;
  wire n1230_o;
  wire n1231_o;
  wire n1232_o;
  wire [7:0] n1233_o;
  wire [3:0] n1234_o;
  wire [3:0] n1235_o;
  wire [7:0] n1236_o;
  wire [7:0] n1237_o;
  wire [3:0] n1238_o;
  wire [3:0] n1239_o;
  wire [7:0] n1240_o;
  wire [7:0] n1241_o;
  wire [7:0] n1242_o;
  wire n1243_o;
  wire [4:0] n1246_o;
  wire [4:0] n1247_o;
  wire [4:0] n1248_o;
  wire [4:0] n1250_o;
  wire n1253_o;
  wire n1254_o;
  wire n1255_o;
  wire n1256_o;
  wire [2:0] n1259_o;
  wire n1260_o;
  wire n1261_o;
  wire [2:0] n1262_o;
  wire [2:0] n1263_o;
  wire [1:0] n1265_o;
  wire n1266_o;
  wire n1268_o;
  wire n1269_o;
  wire n1270_o;
  wire [7:0] n1272_o;
  wire [7:0] n1273_o;
  wire n1274_o;
  wire n1275_o;
  wire n1276_o;
  wire n1277_o;
  wire n1278_o;
  wire n1279_o;
  wire n1281_o;
  wire n1283_o;
  wire n1284_o;
  wire n1286_o;
  wire n1287_o;
  wire n1288_o;
  wire n1289_o;
  wire n1290_o;
  wire n1292_o;
  wire n1293_o;
  wire n1294_o;
  wire n1296_o;
  wire n1298_o;
  wire n1300_o;
  wire n1302_o;
  wire n1304_o;
  wire [4:0] n1305_o;
  reg [7:0] n1306_o;
  reg [7:0] n1307_o;
  wire [7:0] n1308_o;
  reg [7:0] n1309_o;
  wire [7:0] n1310_o;
  reg [7:0] n1311_o;
  wire [7:0] n1312_o;
  reg [7:0] n1313_o;
  wire [7:0] n1314_o;
  wire [7:0] n1315_o;
  wire [7:0] n1316_o;
  wire [7:0] n1317_o;
  wire [7:0] n1318_o;
  wire [15:0] n1319_o;
  wire [15:0] n1320_o;
  wire n1322_o;
  wire n1323_o;
  wire n1324_o;
  wire n1325_o;
  wire n1326_o;
  wire n1327_o;
  wire n1328_o;
  wire n1331_o;
  wire n1332_o;
  wire n1333_o;
  wire n1334_o;
  wire n1335_o;
  wire n1336_o;
  wire [7:0] n1337_o;
  wire [15:0] n1338_o;
  wire [7:0] n1339_o;
  wire [7:0] n1340_o;
  wire [7:0] n1341_o;
  wire [7:0] n1342_o;
  wire [7:0] n1343_o;
  wire n1344_o;
  wire [7:0] n1345_o;
  wire [15:0] n1346_o;
  wire [15:0] n1347_o;
  wire n1348_o;
  wire [15:0] n1349_o;
  wire [7:0] n1350_o;
  wire [1:0] n1351_o;
  wire n1352_o;
  wire n1353_o;
  wire n1354_o;
  wire n1355_o;
  wire n1356_o;
  wire [7:0] n1357_o;
  wire n1358_o;
  wire n1359_o;
  wire n1360_o;
  wire n1361_o;
  wire n1362_o;
  wire [4:0] n1363_o;
  wire n1364_o;
  wire n1365_o;
  wire [3:0] n1366_o;
  wire n1367_o;
  wire n1368_o;
  wire [2:0] n1369_o;
  wire n1370_o;
  wire [15:0] n1376_o;
  wire [7:0] n1377_o;
  wire [7:0] n1378_o;
  wire [7:0] n1379_o;
  wire [7:0] n1380_o;
  wire [7:0] n1381_o;
  wire [7:0] n1382_o;
  wire [7:0] n1383_o;
  wire [15:0] n1384_o;
  wire [15:0] n1385_o;
  wire n1386_o;
  wire [15:0] n1387_o;
  wire [7:0] n1388_o;
  wire [1:0] n1389_o;
  wire [1:0] n1390_o;
  wire [1:0] n1391_o;
  wire n1392_o;
  wire n1393_o;
  wire n1394_o;
  wire [7:0] n1395_o;
  wire n1396_o;
  wire n1397_o;
  wire n1398_o;
  wire n1399_o;
  wire [7:0] n1400_o;
  wire [4:0] n1401_o;
  wire n1402_o;
  wire n1403_o;
  wire [3:0] n1404_o;
  wire n1405_o;
  wire n1406_o;
  wire [2:0] n1407_o;
  wire n1408_o;
  wire [2:0] n1492_o;
  wire [2:0] n1493_o;
  wire n1497_o;
  wire n1498_o;
  wire [1:0] n1502_o;
  wire [1:0] n1503_o;
  wire [3:0] n1545_o;
  wire [19:0] n1546_o;
  wire [19:0] n1547_o;
  wire [19:0] n1548_o;
  wire [15:0] n1549_o;
  wire [19:0] n1551_o;
  wire [19:0] n1552_o;
  wire [11:0] n1553_o;
  wire n1558_o;
  wire [7:0] n1559_o;
  wire [7:0] n1560_o;
  wire [7:0] n1561_o;
  wire [31:0] n1563_o;
  wire [31:0] n1564_o;
  wire [31:0] n1565_o;
  wire [15:0] n1566_o;
  wire [15:0] n1567_o;
  wire [31:0] n1568_o;
  wire n1571_o;
  wire [1:0] n1578_o;
  wire [2:0] n1579_o;
  wire n1580_o;
  wire n1582_o;
  wire n1583_o;
  wire [1:0] n1584_o;
  wire n1586_o;
  wire n1587_o;
  wire n1588_o;
  wire [2:0] n1590_o;
  wire [2:0] n1591_o;
  wire [1:0] n1592_o;
  wire [2:0] n1593_o;
  wire n1594_o;
  wire n1596_o;
  wire n1597_o;
  wire [1:0] n1598_o;
  wire n1600_o;
  wire n1601_o;
  wire n1602_o;
  wire [2:0] n1604_o;
  wire [2:0] n1605_o;
  wire [1:0] n1606_o;
  wire [2:0] n1607_o;
  wire n1608_o;
  wire [2:0] n1610_o;
  wire [2:0] n1611_o;
  wire n1612_o;
  wire n1614_o;
  wire n1615_o;
  wire n1617_o;
  wire n1618_o;
  wire n1619_o;
  wire [2:0] n1621_o;
  wire [2:0] n1622_o;
  wire n1623_o;
  wire n1625_o;
  wire n1626_o;
  wire n1627_o;
  wire n1629_o;
  wire n1631_o;
  wire n1633_o;
  wire n1634_o;
  wire n1635_o;
  wire [2:0] n1636_o;
  wire n1638_o;
  wire n1639_o;
  wire n1641_o;
  wire n1644_o;
  wire n1645_o;
  wire [1:0] n1657_o;
  wire [2:0] n1658_o;
  wire n1660_o;
  wire n1662_o;
  wire n1664_o;
  wire n1665_o;
  wire n1666_o;
  wire n1667_o;
  wire n1668_o;
  wire n1670_o;
  wire n1671_o;
  wire [2:0] n1672_o;
  wire n1673_o;
  wire [2:0] n1675_o;
  wire n1677_o;
  wire n1679_o;
  wire n1681_o;
  wire n1682_o;
  wire n1683_o;
  wire n1684_o;
  wire n1685_o;
  wire [1:0] n1686_o;
  wire n1688_o;
  wire n1689_o;
  wire [2:0] n1690_o;
  wire [2:0] n1692_o;
  wire n1694_o;
  wire n1695_o;
  wire [2:0] n1696_o;
  wire [2:0] n1698_o;
  wire n1699_o;
  wire n1701_o;
  wire n1702_o;
  wire [2:0] n1703_o;
  wire [2:0] n1705_o;
  wire n1707_o;
  wire n1708_o;
  wire n1710_o;
  wire n1711_o;
  wire [2:0] n1712_o;
  wire n1713_o;
  wire [2:0] n1715_o;
  wire n1717_o;
  wire n1718_o;
  wire [2:0] n1719_o;
  wire n1722_o;
  wire n1723_o;
  wire [2:0] n1724_o;
  wire [2:0] n1726_o;
  wire n1728_o;
  wire n1729_o;
  wire [2:0] n1730_o;
  wire [15:0] n1732_o;
  wire n1733_o;
  wire [15:0] n1734_o;
  wire [15:0] n1736_o;
  wire n1740_o;
  wire n1741_o;
  wire n1742_o;
  wire n1743_o;
  wire n1744_o;
  wire n1746_o;
  wire n1747_o;
  wire n1748_o;
  wire n1749_o;
  wire n1750_o;
  wire n1751_o;
  wire n1753_o;
  wire n1755_o;
  wire n1756_o;
  wire n1758_o;
  wire n1759_o;
  wire n1761_o;
  wire n1762_o;
  wire n1764_o;
  wire n1765_o;
  wire n1767_o;
  wire n1768_o;
  reg n1770_o;
  reg n1772_o;
  wire n1774_o;
  wire n1777_o;
  wire n1780_o;
  wire n1781_o;
  wire n1782_o;
  wire n1784_o;
  wire n1786_o;
  wire n1788_o;
  wire n1790_o;
  wire n1791_o;
  wire n1792_o;
  wire n1794_o;
  wire n1796_o;
  wire n1798_o;
  wire n1799_o;
  wire n1800_o;
  wire n1802_o;
  wire n1803_o;
  wire n1805_o;
  wire n1807_o;
  wire n1808_o;
  wire n1810_o;
  wire n1811_o;
  wire n1813_o;
  wire n1814_o;
  wire n1816_o;
  wire n1818_o;
  wire n1819_o;
  wire n1820_o;
  wire n1821_o;
  wire [1:0] n1822_o;
  wire n1824_o;
  wire n1826_o;
  wire n1827_o;
  wire n1829_o;
  wire n1830_o;
  reg n1832_o;
  reg n1834_o;
  wire n1835_o;
  wire n1836_o;
  wire [15:0] n1838_o;
  wire [15:0] n1839_o;
  wire [7:0] n1842_o;
  wire [7:0] n1843_o;
  wire [7:0] n1844_o;
  wire [7:0] n1845_o;
  wire [7:0] n1846_o;
  wire [7:0] n1847_o;
  wire [7:0] n1848_o;
  wire [7:0] n1849_o;
  wire n1851_o;
  wire n1852_o;
  wire n1854_o;
  wire n1855_o;
  wire [7:0] n1856_o;
  wire [7:0] n1857_o;
  wire [7:0] n1858_o;
  wire [7:0] n1859_o;
  wire n1861_o;
  wire n1862_o;
  wire [7:0] n1863_o;
  wire [7:0] n1864_o;
  wire [7:0] n1865_o;
  wire [7:0] n1866_o;
  wire n1868_o;
  wire n1869_o;
  wire [7:0] n1870_o;
  wire [7:0] n1871_o;
  wire [7:0] n1872_o;
  wire [7:0] n1873_o;
  wire n1875_o;
  wire n1876_o;
  wire [7:0] n1877_o;
  wire [7:0] n1878_o;
  wire [7:0] n1879_o;
  wire [7:0] n1880_o;
  wire n1881_o;
  wire n1883_o;
  wire n1885_o;
  wire n1886_o;
  wire n1888_o;
  wire n1890_o;
  wire n1891_o;
  wire n1892_o;
  wire n1893_o;
  wire [7:0] n1894_o;
  wire [7:0] n1895_o;
  wire [7:0] n1896_o;
  wire [7:0] n1897_o;
  wire [7:0] regs_n1899;
  wire [7:0] regs_n1900;
  wire [7:0] regs_n1901;
  wire [7:0] regs_n1902;
  wire [7:0] regs_n1903;
  wire [7:0] regs_n1904;
  wire [127:0] regs_n1905;
  wire [127:0] n1906_o;
  wire [7:0] regs_doah;
  wire [7:0] regs_doal;
  wire [7:0] regs_dobh;
  wire [7:0] regs_dobl;
  wire [7:0] regs_doch;
  wire [7:0] regs_docl;
  wire [127:0] regs_dor;
  wire n1925_o;
  wire n1926_o;
  wire [7:0] n1927_o;
  wire [7:0] n1928_o;
  wire [7:0] n1929_o;
  wire n1931_o;
  wire n1933_o;
  wire n1934_o;
  wire n1936_o;
  wire n1937_o;
  wire n1939_o;
  wire n1940_o;
  wire n1942_o;
  wire n1943_o;
  wire n1945_o;
  wire n1946_o;
  wire n1948_o;
  wire [7:0] n1949_o;
  wire n1951_o;
  wire [7:0] n1952_o;
  wire n1954_o;
  wire n1956_o;
  wire n1958_o;
  wire [7:0] n1959_o;
  wire n1961_o;
  wire [7:0] n1962_o;
  wire n1964_o;
  wire n1966_o;
  wire n1967_o;
  wire [7:0] n1970_o;
  wire n1972_o;
  wire [9:0] n1973_o;
  reg [7:0] n1976_o;
  wire n1978_o;
  wire n1979_o;
  wire [7:0] n1980_o;
  wire [7:0] n1981_o;
  wire [7:0] n1982_o;
  wire n1984_o;
  wire n1986_o;
  wire n1987_o;
  wire n1989_o;
  wire n1990_o;
  wire n1992_o;
  wire n1993_o;
  wire n1995_o;
  wire n1996_o;
  wire n1998_o;
  wire n1999_o;
  wire n2001_o;
  wire [7:0] n2002_o;
  wire n2004_o;
  wire [7:0] n2005_o;
  wire n2007_o;
  wire n2009_o;
  wire [5:0] n2010_o;
  reg [7:0] n2013_o;
  wire [7:0] n2014_o;
  wire [7:0] n2015_o;
  wire n2023_o;
  wire n2025_o;
  wire n2026_o;
  wire n2028_o;
  wire n2030_o;
  wire n2031_o;
  wire n2033_o;
  wire n2034_o;
  wire n2035_o;
  wire n2038_o;
  wire n2044_o;
  wire n2045_o;
  wire n2046_o;
  wire n2047_o;
  reg n2048_oldnmi_n;
  wire n2051_o;
  wire n2053_o;
  wire n2054_o;
  wire n2055_o;
  wire n2056_o;
  wire n2058_o;
  wire n2059_o;
  wire n2060_o;
  wire n2062_o;
  wire n2064_o;
  wire n2065_o;
  wire n2066_o;
  wire n2067_o;
  wire n2068_o;
  wire n2069_o;
  wire n2070_o;
  wire n2071_o;
  wire n2072_o;
  wire n2073_o;
  wire n2074_o;
  wire n2075_o;
  wire n2076_o;
  wire n2077_o;
  wire n2078_o;
  wire n2079_o;
  wire n2080_o;
  wire n2081_o;
  wire n2082_o;
  wire n2083_o;
  wire n2084_o;
  wire n2085_o;
  wire n2087_o;
  wire n2089_o;
  wire n2091_o;
  wire n2092_o;
  wire n2093_o;
  wire n2094_o;
  wire n2096_o;
  wire n2098_o;
  wire n2100_o;
  wire n2101_o;
  wire n2102_o;
  wire n2103_o;
  wire n2105_o;
  wire n2107_o;
  wire n2109_o;
  wire n2110_o;
  wire n2111_o;
  wire n2113_o;
  wire n2114_o;
  wire n2116_o;
  wire n2117_o;
  wire n2118_o;
  wire n2120_o;
  wire n2122_o;
  wire n2124_o;
  wire n2126_o;
  wire [2:0] n2128_o;
  wire n2130_o;
  wire n2134_o;
  wire [2:0] n2136_o;
  wire n2137_o;
  wire n2138_o;
  wire n2140_o;
  wire n2141_o;
  wire n2142_o;
  wire n2143_o;
  wire n2145_o;
  wire n2146_o;
  wire n2147_o;
  wire n2148_o;
  wire n2150_o;
  wire n2151_o;
  wire n2152_o;
  wire n2153_o;
  wire n2157_o;
  wire n2159_o;
  wire n2162_o;
  wire n2164_o;
  wire n2165_o;
  wire n2167_o;
  wire n2169_o;
  wire n2173_o;
  wire [2:0] n2176_o;
  wire n2178_o;
  wire [2:0] n2180_o;
  wire n2181_o;
  wire n2182_o;
  wire n2183_o;
  wire n2184_o;
  wire n2185_o;
  wire n2186_o;
  wire [2:0] n2187_o;
  wire n2188_o;
  wire n2189_o;
  wire n2190_o;
  wire n2191_o;
  wire n2192_o;
  wire n2193_o;
  wire [2:0] n2195_o;
  wire n2196_o;
  wire n2197_o;
  wire n2198_o;
  wire [2:0] n2199_o;
  wire n2200_o;
  wire n2201_o;
  wire n2202_o;
  wire [2:0] n2204_o;
  wire [2:0] n2205_o;
  wire n2206_o;
  wire n2207_o;
  wire n2210_o;
  wire n2211_o;
  wire [2:0] n2212_o;
  wire n2213_o;
  wire n2214_o;
  wire n2215_o;
  wire n2216_o;
  wire n2218_o;
  wire n2219_o;
  wire n2220_o;
  wire n2221_o;
  wire n2222_o;
  wire [2:0] n2224_o;
  wire [2:0] n2225_o;
  wire n2226_o;
  wire [2:0] n2227_o;
  wire [2:0] n2228_o;
  wire n2229_o;
  wire n2230_o;
  wire n2231_o;
  wire n2233_o;
  wire n2234_o;
  wire [2:0] n2235_o;
  wire n2236_o;
  wire n2237_o;
  wire n2238_o;
  wire [2:0] n2239_o;
  wire [2:0] n2240_o;
  wire n2241_o;
  wire n2242_o;
  wire n2243_o;
  wire n2245_o;
  wire n2247_o;
  wire [2:0] n2248_o;
  wire n2249_o;
  wire n2250_o;
  wire n2251_o;
  wire [2:0] n2252_o;
  wire [2:0] n2253_o;
  wire n2254_o;
  wire n2255_o;
  wire n2256_o;
  wire n2257_o;
  wire n2258_o;
  wire [2:0] n2259_o;
  wire n2260_o;
  wire n2261_o;
  wire n2263_o;
  wire n2265_o;
  wire n2266_o;
  wire [2:0] n2267_o;
  wire [2:0] n2268_o;
  wire n2269_o;
  wire n2270_o;
  wire n2271_o;
  wire n2272_o;
  wire n2273_o;
  wire n2274_o;
  wire [2:0] n2275_o;
  wire n2276_o;
  wire n2277_o;
  wire n2278_o;
  wire n2279_o;
  wire n2280_o;
  wire n2281_o;
  wire [2:0] n2282_o;
  wire [2:0] n2283_o;
  wire n2284_o;
  wire n2285_o;
  wire n2286_o;
  wire n2287_o;
  wire n2288_o;
  wire n2289_o;
  wire [2:0] n2290_o;
  wire n2291_o;
  wire n2292_o;
  wire n2293_o;
  wire n2294_o;
  wire n2295_o;
  wire n2296_o;
  wire n2343_o;
  wire n2344_o;
  reg n2345_q;
  wire n2348_o;
  wire n2349_o;
  wire n2350_o;
  reg [7:0] n2352_q;
  reg [7:0] n2353_q;
  reg [7:0] n2354_q;
  reg [7:0] n2355_q;
  reg [7:0] n2356_q;
  reg [7:0] n2357_q;
  reg [15:0] n2358_q;
  reg [15:0] n2359_q;
  wire [15:0] n2360_o;
  wire [15:0] n2361_o;
  wire [15:0] n2362_o;
  wire [2:0] n2363_o;
  reg [2:0] n2364_q;
  wire [2:0] n2365_o;
  reg [2:0] n2366_q;
  wire [2:0] n2367_o;
  reg [2:0] n2368_q;
  reg n2369_q;
  reg [15:0] n2370_q;
  reg [7:0] n2371_q;
  reg [1:0] n2372_q;
  wire [15:0] n2373_o;
  reg [15:0] n2374_q;
  wire [31:0] n2375_o;
  reg [31:0] n2376_q;
  wire [31:0] n2377_o;
  wire [15:0] n2378_o;
  reg [15:0] n2379_q;
  reg [2:0] n2380_q;
  reg [2:0] n2381_q;
  reg n2382_q;
  reg n2383_q;
  reg n2384_q;
  wire n2385_o;
  wire n2386_o;
  reg n2387_q;
  wire n2388_o;
  wire n2389_o;
  reg n2390_q;
  reg n2391_q;
  reg [1:0] n2392_q;
  reg [1:0] n2393_q;
  reg [2:0] n2394_q;
  reg n2395_q;
  reg n2396_q;
  reg n2397_q;
  reg n2398_q;
  reg n2399_q;
  reg n2400_q;
  wire n2401_o;
  reg n2402_q;
  wire n2403_o;
  wire [7:0] n2404_o;
  reg [7:0] n2405_q;
  reg n2406_q;
  reg n2407_q;
  wire [7:0] n2408_o;
  reg [7:0] n2409_q;
  wire [7:0] n2410_o;
  reg [7:0] n2411_q;
  wire [7:0] n2412_o;
  reg [7:0] n2413_q;
  reg [4:0] n2414_q;
  reg n2415_q;
  reg n2416_q;
  reg [3:0] n2417_q;
  reg n2418_q;
  reg n2419_q;
  reg [2:0] n2420_q;
  reg n2421_q;
  reg n2422_q;
  reg n2423_q;
  reg n2424_q;
  wire n2425_o;
  reg n2426_q;
  reg [15:0] n2427_q;
  reg [7:0] n2428_q;
  assign m1_n = n2424_q; //(module output)
  assign iorq = iorq_i; //(module output)
  assign noread = mcode_n291; //(module output)
  assign write = mcode_n292; //(module output)
  assign rfsh_n = n2426_q; //(module output)
  assign halt_n = n2044_o; //(module output)
  assign busak_n = n2046_o; //(module output)
  assign a = n2427_q; //(module output)
  assign do_ = n2428_q; //(module output)
  assign mc = mcycle; //(module output)
  assign ts = tstate; //(module output)
  assign intcycle_n = n2047_o; //(module output)
  assign inte = inte_ff1; //(module output)
  assign stop = i_djnz; //(module output)
  assign \reg  = n221_o; //(module output)
  /* T80.vhd:134:16  */
  assign acc = n2352_q; // (signal)
  /* T80.vhd:134:21  */
  assign f = n2353_q; // (signal)
  /* T80.vhd:135:16  */
  assign ap = n2354_q; // (signal)
  /* T80.vhd:135:20  */
  assign fp = n2355_q; // (signal)
  /* T80.vhd:136:16  */
  assign i = n2356_q; // (signal)
  /* T80.vhd:137:16  */
  assign r = n2357_q; // (signal)
  /* T80.vhd:138:16  */
  assign sp = n2358_q; // (signal)
  /* T80.vhd:138:20  */
  assign pc = n2359_q; // (signal)
  /* T80.vhd:140:16  */
  assign regdih = n1896_o; // (signal)
  /* T80.vhd:141:16  */
  assign regdil = n1897_o; // (signal)
  /* T80.vhd:142:16  */
  assign regbusa = n2360_o; // (signal)
  /* T80.vhd:143:16  */
  assign regbusb = n2361_o; // (signal)
  /* T80.vhd:144:16  */
  assign regbusc = n2362_o; // (signal)
  /* T80.vhd:145:16  */
  assign regaddra_r = n2364_q; // (signal)
  /* T80.vhd:146:16  */
  assign regaddra = n1672_o; // (signal)
  /* T80.vhd:147:16  */
  assign regaddrb_r = n2366_q; // (signal)
  /* T80.vhd:148:16  */
  assign regaddrb = n1730_o; // (signal)
  /* T80.vhd:149:16  */
  assign regaddrc = n2368_q; // (signal)
  /* T80.vhd:150:16  */
  assign regweh = n1835_o; // (signal)
  /* T80.vhd:151:16  */
  assign regwel = n1836_o; // (signal)
  /* T80.vhd:152:16  */
  assign alternate = n2369_q; // (signal)
  /* T80.vhd:155:16  */
  assign wz = n2370_q; // (signal)
  /* T80.vhd:156:16  */
  assign tmpaddr2 = n1839_o; // (signal)
  /* T80.vhd:157:16  */
  assign ir = n2371_q; // (signal)
  /* T80.vhd:158:16  */
  assign iset = n2372_q; // (signal)
  /* T80.vhd:159:16  */
  assign regbusa_r = n2374_q; // (signal)
  /* T80.vhd:160:16  */
  assign mulu_prod32 = n2376_q; // (signal)
  /* T80.vhd:161:16  */
  assign mulu_tmp = n2377_o; // (signal)
  /* T80.vhd:162:16  */
  assign mulu_fakt1 = n2379_q; // (signal)
  /* T80.vhd:164:16  */
  assign id16 = n1734_o; // (signal)
  /* T80.vhd:165:16  */
  assign save_mux = n440_o; // (signal)
  /* T80.vhd:167:16  */
  assign tstate = n2380_q; // (signal)
  /* T80.vhd:168:16  */
  assign mcycle = n2381_q; // (signal)
  /* T80.vhd:169:16  */
  assign inte_ff1 = n2382_q; // (signal)
  /* T80.vhd:170:16  */
  assign inte_ff2 = n2383_q; // (signal)
  /* T80.vhd:171:16  */
  assign halt_ff = n2384_q; // (signal)
  /* T80.vhd:172:16  */
  always @*
    busreq_s = n2387_q; // (isignal)
  initial
    busreq_s = 1'b0;
  /* T80.vhd:173:16  */
  always @*
    busack = n2390_q; // (isignal)
  initial
    busack = 1'b0;
  /* T80.vhd:174:16  */
  assign clken = n413_o; // (signal)
  /* T80.vhd:175:16  */
  assign nmi_s = n2391_q; // (signal)
  /* T80.vhd:176:16  */
  assign istatus = n2392_q; // (signal)
  /* T80.vhd:178:16  */
  assign di_reg = di; // (signal)
  /* T80.vhd:179:16  */
  assign t_res = n416_o; // (signal)
  /* T80.vhd:180:16  */
  assign xy_state = n2393_q; // (signal)
  /* T80.vhd:181:16  */
  assign pre_xy_f_m = n2394_q; // (signal)
  /* T80.vhd:182:16  */
  assign nextis_xy_fetch = n438_o; // (signal)
  /* T80.vhd:183:16  */
  assign xy_ind = n2395_q; // (signal)
  /* T80.vhd:184:16  */
  assign no_btr = n2396_q; // (signal)
  /* T80.vhd:185:16  */
  assign btr_r = n2397_q; // (signal)
  /* T80.vhd:186:16  */
  assign btr_r2 = n2398_q; // (signal)
  /* T80.vhd:187:16  */
  assign auto_wait = n2350_o; // (signal)
  /* T80.vhd:188:16  */
  assign auto_wait_t1 = n2399_q; // (signal)
  /* T80.vhd:189:16  */
  assign auto_wait_t2 = n2400_q; // (signal)
  /* T80.vhd:190:16  */
  assign incdecz = n2402_q; // (signal)
  /* T80.vhd:191:16  */
  assign last_b = n2405_q; // (signal)
  /* T80.vhd:195:16  */
  assign q_reg = n2408_o; // (signal)
  /* T80.vhd:196:16  */
  assign f_last = n2409_q; // (signal)
  /* T80.vhd:199:16  */
  assign busb = n2411_q; // (signal)
  /* T80.vhd:200:16  */
  assign busa = n2413_q; // (signal)
  /* T80.vhd:201:16  */
  assign aluqs = alu_n406; // (signal)
  /* T80.vhd:202:16  */
  assign f_out = alu_n407; // (signal)
  /* T80.vhd:205:16  */
  assign read_to_reg_r = n2414_q; // (signal)
  /* T80.vhd:206:16  */
  assign arith16_r = n2415_q; // (signal)
  /* T80.vhd:207:16  */
  assign z16_r = n2416_q; // (signal)
  /* T80.vhd:208:16  */
  assign alu_op_r = n2417_q; // (signal)
  /* T80.vhd:209:16  */
  assign save_alu_r = n2418_q; // (signal)
  /* T80.vhd:210:16  */
  assign preservec_r = n2419_q; // (signal)
  /* T80.vhd:211:16  */
  assign mcycles = n2420_q; // (signal)
  /* T80.vhd:214:16  */
  assign mcycles_d = mcode_n240; // (signal)
  /* T80.vhd:215:16  */
  assign tstates = mcode_n241; // (signal)
  /* T80.vhd:216:16  */
  assign intcycle = n2421_q; // (signal)
  /* T80.vhd:217:16  */
  assign nmicycle = n2422_q; // (signal)
  /* T80.vhd:218:16  */
  assign inc_pc = mcode_n243; // (signal)
  /* T80.vhd:219:16  */
  assign inc_wz = mcode_n244; // (signal)
  /* T80.vhd:220:16  */
  assign incdec_16 = mcode_n245; // (signal)
  /* T80.vhd:221:16  */
  assign prefix = mcode_n242; // (signal)
  /* T80.vhd:222:16  */
  assign read_to_acc = mcode_n247; // (signal)
  /* T80.vhd:223:16  */
  assign read_to_reg = mcode_n246; // (signal)
  /* T80.vhd:224:16  */
  assign set_busb_to = mcode_n249; // (signal)
  /* T80.vhd:225:16  */
  assign set_busa_to = mcode_n248; // (signal)
  /* T80.vhd:226:16  */
  assign alu_op = mcode_n250; // (signal)
  /* T80.vhd:227:16  */
  assign save_alu = mcode_n251; // (signal)
  /* T80.vhd:228:16  */
  assign rot_akku = mcode_n252; // (signal)
  /* T80.vhd:229:16  */
  assign preservec = mcode_n253; // (signal)
  /* T80.vhd:230:16  */
  assign arith16 = mcode_n254; // (signal)
  /* T80.vhd:231:16  */
  assign set_addr_to = mcode_n255; // (signal)
  /* T80.vhd:232:16  */
  assign jump = mcode_n257; // (signal)
  /* T80.vhd:233:16  */
  assign jumpe = mcode_n258; // (signal)
  /* T80.vhd:234:16  */
  assign jumpxy = mcode_n259; // (signal)
  /* T80.vhd:235:16  */
  assign call = mcode_n260; // (signal)
  /* T80.vhd:236:16  */
  assign rstp = mcode_n261; // (signal)
  /* T80.vhd:237:16  */
  assign ldz = mcode_n262; // (signal)
  /* T80.vhd:238:16  */
  assign ldw = mcode_n263; // (signal)
  /* T80.vhd:239:16  */
  assign ldsphl = mcode_n264; // (signal)
  /* T80.vhd:240:16  */
  assign ldhlsp = mcode_n265; // (signal)
  /* T80.vhd:241:16  */
  assign addspdd = mcode_n266; // (signal)
  /* T80.vhd:242:16  */
  assign iorq_i = mcode_n256; // (signal)
  /* T80.vhd:243:16  */
  assign special_ld = mcode_n267; // (signal)
  /* T80.vhd:244:16  */
  assign exchangedh = mcode_n268; // (signal)
  /* T80.vhd:245:16  */
  assign exchangerp = mcode_n269; // (signal)
  /* T80.vhd:246:16  */
  assign exchangeaf = mcode_n270; // (signal)
  /* T80.vhd:247:16  */
  assign exchangers = mcode_n271; // (signal)
  /* T80.vhd:248:16  */
  assign exchangewh = mcode_n272; // (signal)
  /* T80.vhd:249:16  */
  assign i_djnz = mcode_n273; // (signal)
  /* T80.vhd:250:16  */
  assign i_cpl = mcode_n274; // (signal)
  /* T80.vhd:251:16  */
  assign i_ccf = mcode_n275; // (signal)
  /* T80.vhd:252:16  */
  assign i_scf = mcode_n276; // (signal)
  /* T80.vhd:253:16  */
  assign i_retn = mcode_n277; // (signal)
  /* T80.vhd:254:16  */
  assign i_bt = mcode_n278; // (signal)
  /* T80.vhd:255:16  */
  assign i_bc = mcode_n279; // (signal)
  /* T80.vhd:256:16  */
  assign i_btr = mcode_n280; // (signal)
  /* T80.vhd:257:16  */
  assign i_rld = mcode_n281; // (signal)
  /* T80.vhd:258:16  */
  assign i_rrd = mcode_n282; // (signal)
  /* T80.vhd:259:16  */
  assign i_rxdd = n2423_q; // (signal)
  /* T80.vhd:260:16  */
  assign i_inrc = mcode_n283; // (signal)
  /* T80.vhd:261:16  */
  assign i_mulub = mcode_n284; // (signal)
  /* T80.vhd:262:16  */
  assign i_mulu = mcode_n285; // (signal)
  /* T80.vhd:263:16  */
  assign setwz = mcode_n286; // (signal)
  /* T80.vhd:264:16  */
  assign setdi = mcode_n287; // (signal)
  /* T80.vhd:265:16  */
  assign setei = mcode_n288; // (signal)
  /* T80.vhd:266:16  */
  assign imode = mcode_n289; // (signal)
  /* T80.vhd:267:16  */
  assign halt = mcode_n290; // (signal)
  /* T80.vhd:268:16  */
  assign xybit_undoc = mcode_n294; // (signal)
  /* T80.vhd:269:16  */
  assign no_pc = mcode_n293; // (signal)
  /* T80.vhd:270:16  */
  assign dor = regs_n1905; // (signal)
  /* T80.vhd:274:25  */
  assign n209_o = {inte_ff2, inte_ff1};
  /* T80.vhd:274:36  */
  assign n210_o = {n209_o, istatus};
  /* T80.vhd:274:46  */
  assign n211_o = {n210_o, dor};
  /* T80.vhd:274:52  */
  assign n212_o = {n211_o, pc};
  /* T80.vhd:274:75  */
  assign n213_o = {n212_o, sp};
  /* T80.vhd:274:98  */
  assign n214_o = {n213_o, r};
  /* T80.vhd:274:120  */
  assign n215_o = {n214_o, i};
  /* T80.vhd:274:124  */
  assign n216_o = {n215_o, fp};
  /* T80.vhd:274:129  */
  assign n217_o = {n216_o, ap};
  /* T80.vhd:274:134  */
  assign n218_o = {n217_o, f};
  /* T80.vhd:274:138  */
  assign n219_o = {n218_o, acc};
  /* T80.vhd:274:159  */
  assign n220_o = ~alternate;
  /* T80.vhd:274:144  */
  assign n221_o = n220_o ? n219_o : n239_o;
  /* T80.vhd:275:40  */
  assign n222_o = {inte_ff2, inte_ff1};
  /* T80.vhd:275:51  */
  assign n223_o = {n222_o, istatus};
  /* T80.vhd:275:66  */
  assign n224_o = dor[127:112]; // extract
  /* T80.vhd:275:61  */
  assign n225_o = {n223_o, n224_o};
  /* T80.vhd:275:88  */
  assign n226_o = dor[47:0]; // extract
  /* T80.vhd:275:83  */
  assign n227_o = {n225_o, n226_o};
  /* T80.vhd:275:107  */
  assign n228_o = dor[63:48]; // extract
  /* T80.vhd:275:102  */
  assign n229_o = {n227_o, n228_o};
  /* T80.vhd:275:127  */
  assign n230_o = dor[111:64]; // extract
  /* T80.vhd:275:122  */
  assign n231_o = {n229_o, n230_o};
  /* T80.vhd:275:143  */
  assign n232_o = {n231_o, pc};
  /* T80.vhd:276:70  */
  assign n233_o = {n232_o, sp};
  /* T80.vhd:276:93  */
  assign n234_o = {n233_o, r};
  /* T80.vhd:276:115  */
  assign n235_o = {n234_o, i};
  /* T80.vhd:276:119  */
  assign n236_o = {n235_o, fp};
  /* T80.vhd:276:124  */
  assign n237_o = {n236_o, ap};
  /* T80.vhd:276:129  */
  assign n238_o = {n237_o, f};
  /* T80.vhd:276:133  */
  assign n239_o = {n238_o, acc};
  /* T80.vhd:297:40  */
  assign mcode_n240 = mcode_mcycles; // (signal)
  /* T80.vhd:298:40  */
  assign mcode_n241 = mcode_tstates; // (signal)
  /* T80.vhd:299:40  */
  assign mcode_n242 = mcode_prefix; // (signal)
  /* T80.vhd:300:40  */
  assign mcode_n243 = mcode_inc_pc; // (signal)
  /* T80.vhd:301:40  */
  assign mcode_n244 = mcode_inc_wz; // (signal)
  /* T80.vhd:302:40  */
  assign mcode_n245 = mcode_incdec_16; // (signal)
  /* T80.vhd:304:40  */
  assign mcode_n246 = mcode_read_to_reg; // (signal)
  /* T80.vhd:303:40  */
  assign mcode_n247 = mcode_read_to_acc; // (signal)
  /* T80.vhd:306:40  */
  assign mcode_n248 = mcode_set_busa_to; // (signal)
  /* T80.vhd:305:40  */
  assign mcode_n249 = mcode_set_busb_to; // (signal)
  /* T80.vhd:307:40  */
  assign mcode_n250 = mcode_alu_op; // (signal)
  /* T80.vhd:308:40  */
  assign mcode_n251 = mcode_save_alu; // (signal)
  /* T80.vhd:309:40  */
  assign mcode_n252 = mcode_rot_akku; // (signal)
  /* T80.vhd:310:40  */
  assign mcode_n253 = mcode_preservec; // (signal)
  /* T80.vhd:311:40  */
  assign mcode_n254 = mcode_arith16; // (signal)
  /* T80.vhd:312:40  */
  assign mcode_n255 = mcode_set_addr_to; // (signal)
  /* T80.vhd:313:40  */
  assign mcode_n256 = mcode_iorq; // (signal)
  /* T80.vhd:314:40  */
  assign mcode_n257 = mcode_jump; // (signal)
  /* T80.vhd:315:40  */
  assign mcode_n258 = mcode_jumpe; // (signal)
  /* T80.vhd:316:40  */
  assign mcode_n259 = mcode_jumpxy; // (signal)
  /* T80.vhd:317:40  */
  assign mcode_n260 = mcode_call; // (signal)
  /* T80.vhd:318:40  */
  assign mcode_n261 = mcode_rstp; // (signal)
  /* T80.vhd:319:40  */
  assign mcode_n262 = mcode_ldz; // (signal)
  /* T80.vhd:320:40  */
  assign mcode_n263 = mcode_ldw; // (signal)
  /* T80.vhd:321:40  */
  assign mcode_n264 = mcode_ldsphl; // (signal)
  /* T80.vhd:322:40  */
  assign mcode_n265 = mcode_ldhlsp; // (signal)
  /* T80.vhd:323:40  */
  assign mcode_n266 = mcode_addspdd; // (signal)
  /* T80.vhd:324:40  */
  assign mcode_n267 = mcode_special_ld; // (signal)
  /* T80.vhd:325:40  */
  assign mcode_n268 = mcode_exchangedh; // (signal)
  /* T80.vhd:326:40  */
  assign mcode_n269 = mcode_exchangerp; // (signal)
  /* T80.vhd:327:40  */
  assign mcode_n270 = mcode_exchangeaf; // (signal)
  /* T80.vhd:328:40  */
  assign mcode_n271 = mcode_exchangers; // (signal)
  /* T80.vhd:329:40  */
  assign mcode_n272 = mcode_exchangewh; // (signal)
  /* T80.vhd:330:40  */
  assign mcode_n273 = mcode_i_djnz; // (signal)
  /* T80.vhd:331:40  */
  assign mcode_n274 = mcode_i_cpl; // (signal)
  /* T80.vhd:332:40  */
  assign mcode_n275 = mcode_i_ccf; // (signal)
  /* T80.vhd:333:40  */
  assign mcode_n276 = mcode_i_scf; // (signal)
  /* T80.vhd:334:40  */
  assign mcode_n277 = mcode_i_retn; // (signal)
  /* T80.vhd:335:40  */
  assign mcode_n278 = mcode_i_bt; // (signal)
  /* T80.vhd:336:40  */
  assign mcode_n279 = mcode_i_bc; // (signal)
  /* T80.vhd:337:40  */
  assign mcode_n280 = mcode_i_btr; // (signal)
  /* T80.vhd:338:40  */
  assign mcode_n281 = mcode_i_rld; // (signal)
  /* T80.vhd:339:40  */
  assign mcode_n282 = mcode_i_rrd; // (signal)
  /* T80.vhd:340:40  */
  assign mcode_n283 = mcode_i_inrc; // (signal)
  /* T80.vhd:341:40  */
  assign mcode_n284 = mcode_i_mulub; // (signal)
  /* T80.vhd:342:40  */
  assign mcode_n285 = mcode_i_mulu; // (signal)
  /* T80.vhd:343:40  */
  assign mcode_n286 = mcode_setwz; // (signal)
  /* T80.vhd:344:40  */
  assign mcode_n287 = mcode_setdi; // (signal)
  /* T80.vhd:345:40  */
  assign mcode_n288 = mcode_setei; // (signal)
  /* T80.vhd:346:40  */
  assign mcode_n289 = mcode_imode; // (signal)
  /* T80.vhd:347:40  */
  assign mcode_n290 = mcode_halt; // (signal)
  /* T80.vhd:348:40  */
  assign mcode_n291 = mcode_noread; // (signal)
  /* T80.vhd:349:40  */
  assign mcode_n292 = mcode_write; // (signal)
  /* T80.vhd:351:40  */
  assign mcode_n293 = mcode_no_pc; // (signal)
  /* T80.vhd:352:40  */
  assign mcode_n294 = mcode_xybit_undoc; // (signal)
  /* T80.vhd:278:9  */
  t80_mcode_0_0_1_2_3_4_5_6_7 mcode (
    .ir(ir),
    .iset(iset),
    .mcycle(mcycle),
    .f(f),
    .nmicycle(nmicycle),
    .intcycle(intcycle),
    .xy_state(xy_state),
    .r800_mode(r800_mode),
    .mcycles(mcode_mcycles),
    .tstates(mcode_tstates),
    .prefix(mcode_prefix),
    .inc_pc(mcode_inc_pc),
    .inc_wz(mcode_inc_wz),
    .incdec_16(mcode_incdec_16),
    .read_to_reg(mcode_read_to_reg),
    .read_to_acc(mcode_read_to_acc),
    .set_busa_to(mcode_set_busa_to),
    .set_busb_to(mcode_set_busb_to),
    .alu_op(mcode_alu_op),
    .save_alu(mcode_save_alu),
    .rot_akku(mcode_rot_akku),
    .preservec(mcode_preservec),
    .arith16(mcode_arith16),
    .set_addr_to(mcode_set_addr_to),
    .iorq(mcode_iorq),
    .jump(mcode_jump),
    .jumpe(mcode_jumpe),
    .jumpxy(mcode_jumpxy),
    .call(mcode_call),
    .rstp(mcode_rstp),
    .ldz(mcode_ldz),
    .ldw(mcode_ldw),
    .ldsphl(mcode_ldsphl),
    .ldhlsp(mcode_ldhlsp),
    .addspdd(mcode_addspdd),
    .special_ld(mcode_special_ld),
    .exchangedh(mcode_exchangedh),
    .exchangerp(mcode_exchangerp),
    .exchangeaf(mcode_exchangeaf),
    .exchangers(mcode_exchangers),
    .exchangewh(mcode_exchangewh),
    .i_djnz(mcode_i_djnz),
    .i_cpl(mcode_i_cpl),
    .i_ccf(mcode_i_ccf),
    .i_scf(mcode_i_scf),
    .i_retn(mcode_i_retn),
    .i_bt(mcode_i_bt),
    .i_bc(mcode_i_bc),
    .i_btr(mcode_i_btr),
    .i_rld(mcode_i_rld),
    .i_rrd(mcode_i_rrd),
    .i_inrc(mcode_i_inrc),
    .i_mulub(mcode_i_mulub),
    .i_mulu(mcode_i_mulu),
    .setwz(mcode_setwz),
    .setdi(mcode_setdi),
    .setei(mcode_setei),
    .imode(mcode_imode),
    .halt(mcode_halt),
    .noread(mcode_noread),
    .write(mcode_write),
    .no_pc(mcode_no_pc),
    .xybit_undoc(mcode_xybit_undoc));
  /* T80.vhd:372:38  */
  assign n405_o = ir[5:0]; // extract
  /* T80.vhd:377:36  */
  assign alu_n406 = alu_q; // (signal)
  /* T80.vhd:378:36  */
  assign alu_n407 = alu_f_out; // (signal)
  /* T80.vhd:354:9  */
  t80_alu_0_0_1_2_3_4_5_6_7 alu (
    .arith16(arith16_r),
    .z16(z16_r),
    .wz(wz),
    .xy_state(xy_state),
    .alu_op(alu_op_r),
    .rot_akku(rot_akku),
    .ir(n405_o),
    .iset(iset),
    .busa(busa),
    .busb(busb),
    .f_in(f),
    .q(alu_q),
    .f_out(alu_f_out));
  /* T80.vhd:380:26  */
  assign n412_o = ~busack;
  /* T80.vhd:380:22  */
  assign n413_o = cen & n412_o;
  /* T80.vhd:382:34  */
  assign n415_o = tstate == tstates;
  /* T80.vhd:382:22  */
  assign n416_o = n415_o ? 1'b1 : 1'b0;
  /* T80.vhd:384:46  */
  assign n420_o = xy_state != 2'b00;
  /* T80.vhd:384:65  */
  assign n421_o = ~xy_ind;
  /* T80.vhd:384:54  */
  assign n422_o = n421_o & n420_o;
  /* T80.vhd:385:71  */
  assign n424_o = set_addr_to == 3'b010;
  /* T80.vhd:386:65  */
  assign n426_o = mcycle == 3'b001;
  /* T80.vhd:386:80  */
  assign n428_o = ir == 8'b11001011;
  /* T80.vhd:386:73  */
  assign n429_o = n428_o & n426_o;
  /* T80.vhd:385:78  */
  assign n430_o = n424_o | n429_o;
  /* T80.vhd:387:65  */
  assign n432_o = mcycle == 3'b001;
  /* T80.vhd:387:80  */
  assign n434_o = ir == 8'b00110110;
  /* T80.vhd:387:73  */
  assign n435_o = n434_o & n432_o;
  /* T80.vhd:386:94  */
  assign n436_o = n430_o | n435_o;
  /* T80.vhd:384:71  */
  assign n437_o = n436_o & n422_o;
  /* T80.vhd:384:32  */
  assign n438_o = n437_o ? 1'b1 : 1'b0;
  /* T80.vhd:389:26  */
  assign n440_o = exchangerp ? busb : n442_o;
  /* T80.vhd:390:40  */
  assign n441_o = ~save_alu_r;
  /* T80.vhd:389:48  */
  assign n442_o = n441_o ? di_reg : aluqs;
  /* T80.vhd:400:28  */
  assign n450_o = ~reset_n;
  /* T80.vhd:441:43  */
  assign n452_o = dir[7:0]; // extract
  /* T80.vhd:442:43  */
  assign n453_o = dir[15:8]; // extract
  /* T80.vhd:443:43  */
  assign n454_o = dir[23:16]; // extract
  /* T80.vhd:444:43  */
  assign n455_o = dir[31:24]; // extract
  /* T80.vhd:445:43  */
  assign n456_o = dir[39:32]; // extract
  /* T80.vhd:446:52  */
  assign n457_o = dir[47:40]; // extract
  /* T80.vhd:447:52  */
  assign n458_o = dir[63:48]; // extract
  /* T80.vhd:448:52  */
  assign n459_o = dir[79:64]; // extract
  /* T80.vhd:449:41  */
  assign n460_o = dir[79:64]; // extract
  /* T80.vhd:450:47  */
  assign n461_o = dir[209:208]; // extract
  /* T80.vhd:459:60  */
  assign n463_o = mcycle == 3'b011;
  /* T80.vhd:459:49  */
  assign n464_o = n463_o & ldhlsp;
  /* T80.vhd:459:79  */
  assign n466_o = tstate == 3'b001;
  /* T80.vhd:459:68  */
  assign n467_o = n466_o & n464_o;
  /* T80.vhd:460:66  */
  assign n468_o = sp[7:0]; // extract
  /* T80.vhd:460:63  */
  assign n470_o = {1'b0, n468_o};
  /* T80.vhd:460:92  */
  assign n472_o = {1'b0, save_mux};
  /* T80.vhd:460:79  */
  assign n473_o = n470_o + n472_o;
  /* T80.vhd:461:66  */
  assign n474_o = sp[3:0]; // extract
  /* T80.vhd:461:63  */
  assign n476_o = {1'b0, n474_o};
  /* T80.vhd:461:101  */
  assign n477_o = save_mux[3:0]; // extract
  /* T80.vhd:461:92  */
  assign n479_o = {1'b0, n477_o};
  /* T80.vhd:461:79  */
  assign n480_o = n476_o + n479_o;
  /* T80.vhd:464:60  */
  assign n483_o = n480_o[4]; // extract
  /* T80.vhd:465:60  */
  assign n484_o = n473_o[8]; // extract
  assign n485_o = {1'b0, n484_o};
  assign n486_o = f[1:0]; // extract
  /* T80.vhd:459:33  */
  assign n487_o = n467_o ? n485_o : n486_o;
  assign n488_o = f[4]; // extract
  /* T80.vhd:459:33  */
  assign n489_o = n467_o ? n483_o : n488_o;
  assign n490_o = f[6]; // extract
  /* T80.vhd:459:33  */
  assign n491_o = n467_o ? 1'b0 : n490_o;
  /* T80.vhd:468:61  */
  assign n495_o = tstate == 3'b001;
  /* T80.vhd:468:50  */
  assign n496_o = n495_o & addspdd;
  /* T80.vhd:469:66  */
  assign n497_o = sp[7:0]; // extract
  /* T80.vhd:469:63  */
  assign n499_o = {1'b0, n497_o};
  /* T80.vhd:469:92  */
  assign n501_o = {1'b0, save_mux};
  /* T80.vhd:469:79  */
  assign n502_o = n499_o + n501_o;
  /* T80.vhd:470:66  */
  assign n503_o = sp[3:0]; // extract
  /* T80.vhd:470:63  */
  assign n505_o = {1'b0, n503_o};
  /* T80.vhd:470:101  */
  assign n506_o = save_mux[3:0]; // extract
  /* T80.vhd:470:92  */
  assign n508_o = {1'b0, n506_o};
  /* T80.vhd:470:79  */
  assign n509_o = n505_o + n508_o;
  /* T80.vhd:473:60  */
  assign n512_o = n509_o[4]; // extract
  /* T80.vhd:474:60  */
  assign n513_o = n502_o[8]; // extract
  assign n514_o = {1'b0, n513_o};
  /* T80.vhd:468:33  */
  assign n515_o = n496_o ? n514_o : n487_o;
  /* T80.vhd:468:33  */
  assign n516_o = n496_o ? n512_o : n489_o;
  /* T80.vhd:468:33  */
  assign n517_o = n496_o ? 1'b0 : n491_o;
  /* T80.vhd:479:45  */
  assign n521_o = imode != 2'b11;
  /* T80.vhd:452:25  */
  assign n522_o = n1352_o ? imode : istatus;
  /* T80.vhd:485:41  */
  assign n524_o = iset == 2'b10;
  /* T80.vhd:485:58  */
  assign n525_o = alu_op[2]; // extract
  /* T80.vhd:485:62  */
  assign n526_o = ~n525_o;
  /* T80.vhd:485:48  */
  assign n527_o = n526_o & n524_o;
  /* T80.vhd:485:78  */
  assign n528_o = alu_op[0]; // extract
  /* T80.vhd:485:68  */
  assign n529_o = n528_o & n527_o;
  /* T80.vhd:485:99  */
  assign n531_o = mcycle == 3'b011;
  /* T80.vhd:485:88  */
  assign n532_o = n531_o & n529_o;
  /* T80.vhd:485:33  */
  assign n535_o = n532_o ? 1'b1 : 1'b0;
  /* T80.vhd:491:44  */
  assign n537_o = mcycle == 3'b001;
  /* T80.vhd:491:62  */
  assign n538_o = tstate[2]; // extract
  /* T80.vhd:491:66  */
  assign n539_o = ~n538_o;
  /* T80.vhd:491:52  */
  assign n540_o = n539_o & n537_o;
  /* T80.vhd:494:51  */
  assign n542_o = tstate == 3'b010;
  /* T80.vhd:494:55  */
  assign n543_o = wait_n & n542_o;
  /* T80.vhd:498:75  */
  assign n544_o = r[6:0]; // extract
  /* T80.vhd:498:88  */
  assign n546_o = n544_o + 7'b0000001;
  /* T80.vhd:501:57  */
  assign n547_o = ~jump;
  /* T80.vhd:501:72  */
  assign n548_o = ~call;
  /* T80.vhd:501:63  */
  assign n549_o = n548_o & n547_o;
  /* T80.vhd:501:91  */
  assign n550_o = ~nmicycle;
  /* T80.vhd:501:78  */
  assign n551_o = n550_o & n549_o;
  /* T80.vhd:501:110  */
  assign n552_o = ~intcycle;
  /* T80.vhd:501:97  */
  assign n553_o = n552_o & n551_o;
  /* T80.vhd:501:139  */
  assign n554_o = halt_ff | halt;
  /* T80.vhd:501:120  */
  assign n555_o = ~n554_o;
  /* T80.vhd:501:116  */
  assign n556_o = n555_o & n553_o;
  /* T80.vhd:502:66  */
  assign n558_o = pc + 16'b0000000000000001;
  /* T80.vhd:494:41  */
  assign n559_o = n608_o ? n558_o : pc;
  /* T80.vhd:505:79  */
  assign n561_o = istatus == 2'b01;
  /* T80.vhd:505:67  */
  assign n562_o = n561_o & intcycle;
  /* T80.vhd:507:100  */
  assign n564_o = istatus == 2'b10;
  /* T80.vhd:507:88  */
  assign n565_o = n564_o & intcycle;
  /* T80.vhd:507:69  */
  assign n566_o = halt_ff | n565_o;
  /* T80.vhd:507:108  */
  assign n567_o = n566_o | nmicycle;
  /* T80.vhd:507:49  */
  assign n569_o = n567_o ? 8'b00000000 : dinst;
  /* T80.vhd:505:49  */
  assign n571_o = n562_o ? 8'b11111111 : n569_o;
  /* T80.vhd:513:62  */
  assign n573_o = intcycle & 1'b1;
  /* T80.vhd:513:93  */
  assign n575_o = istatus == 2'b10;
  /* T80.vhd:513:81  */
  assign n576_o = n575_o & n573_o;
  assign n577_o = wz[7:0]; // extract
  /* T80.vhd:494:41  */
  assign n578_o = n610_o ? dinst : n577_o;
  /* T80.vhd:519:59  */
  assign n580_o = prefix != 2'b00;
  /* T80.vhd:520:67  */
  assign n582_o = prefix == 2'b11;
  /* T80.vhd:521:70  */
  assign n583_o = ir[5]; // extract
  /* T80.vhd:521:65  */
  assign n586_o = n583_o ? 2'b10 : 2'b01;
  /* T80.vhd:527:75  */
  assign n588_o = prefix == 2'b10;
  /* T80.vhd:527:65  */
  assign n590_o = n588_o ? 2'b00 : xy_state;
  /* T80.vhd:527:65  */
  assign n592_o = n588_o ? 1'b0 : xy_ind;
  /* T80.vhd:520:57  */
  assign n594_o = n582_o ? 2'b00 : prefix;
  /* T80.vhd:520:57  */
  assign n595_o = n582_o ? n586_o : n590_o;
  /* T80.vhd:520:57  */
  assign n596_o = n582_o ? xy_ind : n592_o;
  /* T80.vhd:519:49  */
  assign n598_o = n580_o ? n594_o : 2'b00;
  /* T80.vhd:519:49  */
  assign n601_o = n580_o ? n595_o : 2'b00;
  /* T80.vhd:519:49  */
  assign n603_o = n580_o ? n596_o : 1'b0;
  assign n604_o = {i, r};
  /* T80.vhd:494:41  */
  assign n605_o = n543_o ? n604_o : n2427_q;
  assign n606_o = r[6:0]; // extract
  /* T80.vhd:491:33  */
  assign n607_o = n1084_o ? n546_o : n606_o;
  /* T80.vhd:494:41  */
  assign n608_o = n556_o & n543_o;
  /* T80.vhd:494:41  */
  assign n610_o = n576_o & n543_o;
  /* T80.vhd:494:41  */
  assign n611_o = n543_o ? n571_o : ir;
  /* T80.vhd:494:41  */
  assign n612_o = n543_o ? n598_o : iset;
  /* T80.vhd:452:25  */
  assign n613_o = n1353_o ? n601_o : xy_state;
  /* T80.vhd:494:41  */
  assign n614_o = n543_o ? n603_o : xy_ind;
  /* T80.vhd:542:51  */
  assign n616_o = mcycle == 3'b110;
  /* T80.vhd:544:59  */
  assign n618_o = prefix == 2'b01;
  /* T80.vhd:542:41  */
  assign n620_o = n621_o ? 2'b01 : iset;
  /* T80.vhd:542:41  */
  assign n621_o = n618_o & n616_o;
  /* T80.vhd:542:41  */
  assign n623_o = n616_o ? 1'b1 : xy_ind;
  /* T80.vhd:550:64  */
  assign n624_o = i_bt | i_bc;
  /* T80.vhd:550:72  */
  assign n625_o = n624_o | i_btr;
  /* T80.vhd:550:86  */
  assign n626_o = ~no_btr;
  /* T80.vhd:550:82  */
  assign n627_o = n625_o & n626_o;
  /* T80.vhd:554:76  */
  assign n628_o = wz[7:0]; // extract
  /* T80.vhd:556:86  */
  assign n629_o = wz[7:0]; // extract
  /* T80.vhd:560:66  */
  assign n630_o = call | rstp;
  /* T80.vhd:563:62  */
  assign n631_o = mcycle == mcycles;
  /* T80.vhd:563:72  */
  assign n632_o = nmicycle & n631_o;
  /* T80.vhd:566:78  */
  assign n634_o = mcycle == 3'b011;
  /* T80.vhd:566:67  */
  assign n636_o = n634_o & 1'b1;
  /* T80.vhd:566:87  */
  assign n638_o = n636_o | 1'b0;
  /* T80.vhd:567:57  */
  assign n639_o = intcycle & n638_o;
  /* T80.vhd:567:88  */
  assign n641_o = istatus == 2'b10;
  /* T80.vhd:567:76  */
  assign n642_o = n641_o & n639_o;
  /* T80.vhd:569:76  */
  assign n643_o = wz[7:0]; // extract
  /* T80.vhd:571:86  */
  assign n644_o = wz[7:0]; // extract
  /* T80.vhd:575:77  */
  assign n646_o = xy_state == 2'b00;
  /* T80.vhd:578:73  */
  assign n647_o = nextis_xy_fetch ? pc : wz;
  /* T80.vhd:575:65  */
  assign n648_o = n646_o ? regbusc : n647_o;
  /* T80.vhd:574:57  */
  assign n650_o = set_addr_to == 3'b010;
  /* T80.vhd:595:76  */
  assign n651_o = {acc, di_reg};
  /* T80.vhd:595:86  */
  assign n654_o = n651_o + 16'b0000000000000001;
  /* T80.vhd:584:57  */
  assign n656_o = set_addr_to == 3'b100;
  /* T80.vhd:596:57  */
  assign n658_o = set_addr_to == 3'b101;
  /* T80.vhd:605:82  */
  assign n660_o = setwz == 2'b01;
  /* T80.vhd:606:95  */
  assign n663_o = regbusc + 16'b0000000000000001;
  /* T80.vhd:605:73  */
  assign n664_o = n660_o ? n663_o : wz;
  /* T80.vhd:608:82  */
  assign n666_o = setwz == 2'b10;
  /* T80.vhd:609:106  */
  assign n667_o = regbusc[7:0]; // extract
  /* T80.vhd:609:119  */
  assign n670_o = n667_o + 8'b00000001;
  assign n671_o = {acc, n670_o};
  /* T80.vhd:608:73  */
  assign n672_o = n666_o ? n671_o : n664_o;
  /* T80.vhd:598:57  */
  assign n674_o = set_addr_to == 3'b000;
  /* T80.vhd:615:74  */
  assign n676_o = setwz == 2'b10;
  /* T80.vhd:616:98  */
  assign n677_o = regbusc[7:0]; // extract
  /* T80.vhd:616:111  */
  assign n680_o = n677_o + 8'b00000001;
  assign n681_o = {acc, n680_o};
  /* T80.vhd:615:65  */
  assign n682_o = n676_o ? n681_o : wz;
  /* T80.vhd:613:57  */
  assign n684_o = set_addr_to == 3'b001;
  /* T80.vhd:621:108  */
  assign n686_o = wz + 16'b0000000000000001;
  /* T80.vhd:624:92  */
  assign n687_o = wz[7:0]; // extract
  /* T80.vhd:625:82  */
  assign n689_o = setwz == 2'b10;
  /* T80.vhd:626:101  */
  assign n690_o = wz[7:0]; // extract
  /* T80.vhd:626:114  */
  assign n693_o = n690_o + 8'b00000001;
  assign n694_o = {acc, n693_o};
  /* T80.vhd:625:73  */
  assign n695_o = n689_o ? n694_o : wz;
  assign n696_o = {di_reg, n687_o};
  /* T80.vhd:620:65  */
  assign n697_o = inc_wz ? n686_o : n696_o;
  /* T80.vhd:620:65  */
  assign n698_o = inc_wz ? wz : n695_o;
  /* T80.vhd:619:57  */
  assign n700_o = set_addr_to == 3'b110;
  /* T80.vhd:631:73  */
  assign n702_o = iset == 2'b10;
  /* T80.vhd:631:86  */
  assign n703_o = ir[7:4]; // extract
  /* T80.vhd:631:99  */
  assign n705_o = n703_o == 4'b1011;
  /* T80.vhd:631:80  */
  assign n706_o = n705_o & n702_o;
  /* T80.vhd:631:112  */
  assign n707_o = ir[2:1]; // extract
  /* T80.vhd:631:125  */
  assign n709_o = n707_o == 2'b01;
  /* T80.vhd:631:106  */
  assign n710_o = n709_o & n706_o;
  /* T80.vhd:631:143  */
  assign n712_o = mcycle == 3'b011;
  /* T80.vhd:631:132  */
  assign n713_o = n712_o & n710_o;
  /* T80.vhd:631:158  */
  assign n714_o = ~no_btr;
  /* T80.vhd:631:147  */
  assign n715_o = n714_o & n713_o;
  /* T80.vhd:634:77  */
  assign n716_o = ~no_pc;
  /* T80.vhd:634:83  */
  assign n717_o = n716_o | no_btr;
  /* T80.vhd:634:116  */
  assign n718_o = incdecz & i_djnz;
  /* T80.vhd:634:99  */
  assign n719_o = n717_o | n718_o;
  /* T80.vhd:634:135  */
  assign n721_o = n719_o | 1'b0;
  /* T80.vhd:634:65  */
  assign n722_o = n721_o ? pc : n2427_q;
  /* T80.vhd:631:65  */
  assign n723_o = n715_o ? regbusa_r : n722_o;
  assign n724_o = {n700_o, n684_o, n674_o, n658_o, n656_o, n650_o};
  assign n725_o = n648_o[7:0]; // extract
  assign n726_o = sp[7:0]; // extract
  assign n727_o = regbusc[7:0]; // extract
  assign n728_o = regbusc[7:0]; // extract
  assign n729_o = n697_o[7:0]; // extract
  assign n730_o = n723_o[7:0]; // extract
  /* T80.vhd:573:57  */
  always @*
    case (n724_o)
      6'b100000: n731_o = n729_o;
      6'b010000: n731_o = n728_o;
      6'b001000: n731_o = n727_o;
      6'b000100: n731_o = n726_o;
      6'b000010: n731_o = di_reg;
      6'b000001: n731_o = n725_o;
      default: n731_o = n730_o;
    endcase
  assign n732_o = n648_o[15:8]; // extract
  assign n733_o = sp[15:8]; // extract
  assign n734_o = regbusc[15:8]; // extract
  assign n735_o = regbusc[15:8]; // extract
  assign n736_o = n697_o[15:8]; // extract
  assign n737_o = n723_o[15:8]; // extract
  /* T80.vhd:573:57  */
  always @*
    case (n724_o)
      6'b100000: n738_o = n736_o;
      6'b010000: n738_o = n735_o;
      6'b001000: n738_o = n734_o;
      6'b000100: n738_o = n733_o;
      6'b000010: n738_o = acc;
      6'b000001: n738_o = n732_o;
      default: n738_o = n737_o;
    endcase
  /* T80.vhd:573:57  */
  always @*
    case (n724_o)
      6'b100000: n739_o = n698_o;
      6'b010000: n739_o = n682_o;
      6'b001000: n739_o = n672_o;
      6'b000100: n739_o = wz;
      6'b000010: n739_o = n654_o;
      6'b000001: n739_o = wz;
      default: n739_o = wz;
    endcase
  assign n740_o = {n738_o, n731_o};
  assign n741_o = {i, n643_o};
  /* T80.vhd:566:49  */
  assign n742_o = n642_o ? n741_o : n740_o;
  assign n743_o = {i, n644_o};
  /* T80.vhd:566:49  */
  assign n744_o = n642_o ? n743_o : pc;
  /* T80.vhd:566:49  */
  assign n745_o = n642_o ? wz : n739_o;
  /* T80.vhd:563:49  */
  assign n747_o = n632_o ? 16'b0000000001100110 : n742_o;
  /* T80.vhd:563:49  */
  assign n749_o = n632_o ? 16'b0000000001100110 : n744_o;
  /* T80.vhd:563:49  */
  assign n750_o = n632_o ? wz : n745_o;
  /* T80.vhd:560:49  */
  assign n751_o = n630_o ? wz : n747_o;
  /* T80.vhd:560:49  */
  assign n752_o = n630_o ? wz : n749_o;
  /* T80.vhd:560:49  */
  assign n753_o = n630_o ? wz : n750_o;
  /* T80.vhd:557:49  */
  assign n754_o = jumpxy ? regbusc : n751_o;
  /* T80.vhd:557:49  */
  assign n755_o = jumpxy ? regbusc : n752_o;
  /* T80.vhd:557:49  */
  assign n756_o = jumpxy ? wz : n753_o;
  assign n757_o = {di_reg, n628_o};
  /* T80.vhd:552:49  */
  assign n758_o = jump ? n757_o : n754_o;
  assign n759_o = {di_reg, n629_o};
  /* T80.vhd:552:49  */
  assign n760_o = jump ? n759_o : n755_o;
  /* T80.vhd:552:49  */
  assign n761_o = jump ? wz : n756_o;
  /* T80.vhd:640:58  */
  assign n763_o = setwz == 2'b11;
  /* T80.vhd:640:49  */
  assign n764_o = n763_o ? id16 : n761_o;
  /* T80.vhd:669:72  */
  assign n765_o = ~acc;
  /* T80.vhd:670:85  */
  assign n766_o = acc[5]; // extract
  /* T80.vhd:670:78  */
  assign n767_o = ~n766_o;
  /* T80.vhd:672:85  */
  assign n769_o = acc[3]; // extract
  /* T80.vhd:672:78  */
  assign n770_o = ~n769_o;
  /* T80.vhd:549:41  */
  assign n772_o = n852_o ? n765_o : acc;
  assign n773_o = {n767_o, 1'b1, n770_o};
  assign n774_o = n515_o[1]; // extract
  /* T80.vhd:667:57  */
  assign n775_o = i_cpl ? 1'b1 : n774_o;
  assign n776_o = f[3]; // extract
  assign n777_o = f[5]; // extract
  assign n778_o = {n777_o, n516_o, n776_o};
  /* T80.vhd:667:57  */
  assign n779_o = i_cpl ? n773_o : n778_o;
  /* T80.vhd:678:83  */
  assign n780_o = f[0]; // extract
  /* T80.vhd:678:78  */
  assign n781_o = ~n780_o;
  /* T80.vhd:679:81  */
  assign n782_o = acc[5]; // extract
  /* T80.vhd:679:90  */
  assign n783_o = f[5]; // extract
  /* T80.vhd:679:112  */
  assign n784_o = q_reg[5]; // extract
  /* T80.vhd:679:103  */
  assign n785_o = ~n784_o;
  /* T80.vhd:679:99  */
  assign n786_o = n783_o & n785_o;
  /* T80.vhd:679:85  */
  assign n787_o = n782_o | n786_o;
  /* T80.vhd:680:79  */
  assign n788_o = f[0]; // extract
  /* T80.vhd:681:81  */
  assign n789_o = acc[3]; // extract
  /* T80.vhd:681:90  */
  assign n790_o = f[3]; // extract
  /* T80.vhd:681:112  */
  assign n791_o = q_reg[3]; // extract
  /* T80.vhd:681:103  */
  assign n792_o = ~n791_o;
  /* T80.vhd:681:99  */
  assign n793_o = n790_o & n792_o;
  /* T80.vhd:681:85  */
  assign n794_o = n789_o | n793_o;
  /* T80.vhd:683:85  */
  assign n796_o = acc[5]; // extract
  /* T80.vhd:683:94  */
  assign n797_o = f[5]; // extract
  /* T80.vhd:683:116  */
  assign n798_o = q_reg[5]; // extract
  /* T80.vhd:683:107  */
  assign n799_o = ~n798_o;
  /* T80.vhd:683:103  */
  assign n800_o = n797_o & n799_o;
  /* T80.vhd:683:89  */
  assign n801_o = n796_o | n800_o;
  /* T80.vhd:684:85  */
  assign n802_o = acc[3]; // extract
  /* T80.vhd:684:94  */
  assign n803_o = f[3]; // extract
  /* T80.vhd:684:116  */
  assign n804_o = q_reg[3]; // extract
  /* T80.vhd:684:107  */
  assign n805_o = ~n804_o;
  /* T80.vhd:684:103  */
  assign n806_o = n803_o & n805_o;
  /* T80.vhd:684:89  */
  assign n807_o = n802_o | n806_o;
  assign n808_o = {1'b0, n781_o};
  assign n809_o = {n787_o, n788_o, n794_o};
  assign n810_o = n515_o[0]; // extract
  assign n811_o = {n775_o, n810_o};
  /* T80.vhd:675:57  */
  assign n812_o = i_ccf ? n808_o : n811_o;
  /* T80.vhd:675:57  */
  assign n813_o = i_ccf ? n809_o : n779_o;
  assign n814_o = q_reg[3]; // extract
  /* T80.vhd:675:57  */
  assign n815_o = i_ccf ? n807_o : n814_o;
  assign n816_o = q_reg[5]; // extract
  /* T80.vhd:675:57  */
  assign n817_o = i_ccf ? n801_o : n816_o;
  /* T80.vhd:690:81  */
  assign n819_o = acc[5]; // extract
  /* T80.vhd:690:90  */
  assign n820_o = f[5]; // extract
  /* T80.vhd:690:112  */
  assign n821_o = q_reg[5]; // extract
  /* T80.vhd:690:103  */
  assign n822_o = ~n821_o;
  /* T80.vhd:690:99  */
  assign n823_o = n820_o & n822_o;
  /* T80.vhd:690:85  */
  assign n824_o = n819_o | n823_o;
  /* T80.vhd:692:81  */
  assign n826_o = acc[3]; // extract
  /* T80.vhd:692:90  */
  assign n827_o = f[3]; // extract
  /* T80.vhd:692:112  */
  assign n828_o = q_reg[3]; // extract
  /* T80.vhd:692:103  */
  assign n829_o = ~n828_o;
  /* T80.vhd:692:99  */
  assign n830_o = n827_o & n829_o;
  /* T80.vhd:692:85  */
  assign n831_o = n826_o | n830_o;
  /* T80.vhd:694:85  */
  assign n833_o = acc[5]; // extract
  /* T80.vhd:694:94  */
  assign n834_o = f[5]; // extract
  /* T80.vhd:694:116  */
  assign n835_o = q_reg[5]; // extract
  /* T80.vhd:694:107  */
  assign n836_o = ~n835_o;
  /* T80.vhd:694:103  */
  assign n837_o = n834_o & n836_o;
  /* T80.vhd:694:89  */
  assign n838_o = n833_o | n837_o;
  /* T80.vhd:695:85  */
  assign n839_o = acc[3]; // extract
  /* T80.vhd:695:94  */
  assign n840_o = f[3]; // extract
  /* T80.vhd:695:116  */
  assign n841_o = q_reg[3]; // extract
  /* T80.vhd:695:107  */
  assign n842_o = ~n841_o;
  /* T80.vhd:695:103  */
  assign n843_o = n840_o & n842_o;
  /* T80.vhd:695:89  */
  assign n844_o = n839_o | n843_o;
  assign n845_o = {1'b0, 1'b1};
  assign n846_o = {n824_o, 1'b0, n831_o};
  /* T80.vhd:686:57  */
  assign n847_o = i_scf ? n845_o : n812_o;
  /* T80.vhd:686:57  */
  assign n848_o = i_scf ? n846_o : n813_o;
  /* T80.vhd:686:57  */
  assign n849_o = i_scf ? n844_o : n815_o;
  /* T80.vhd:686:57  */
  assign n850_o = i_scf ? n838_o : n817_o;
  /* T80.vhd:549:41  */
  assign n851_o = t_res ? n758_o : n2427_q;
  /* T80.vhd:549:41  */
  assign n852_o = i_cpl & t_res;
  /* T80.vhd:549:41  */
  assign n853_o = t_res ? n847_o : n515_o;
  assign n854_o = f[3]; // extract
  assign n855_o = f[5]; // extract
  assign n856_o = {n855_o, n516_o, n854_o};
  /* T80.vhd:549:41  */
  assign n857_o = t_res ? n848_o : n856_o;
  /* T80.vhd:549:41  */
  assign n858_o = t_res ? n760_o : pc;
  /* T80.vhd:549:41  */
  assign n859_o = t_res ? n764_o : wz;
  /* T80.vhd:549:41  */
  assign n860_o = t_res ? n627_o : btr_r;
  /* T80.vhd:549:41  */
  assign n861_o = t_res ? i_btr : btr_r2;
  assign n862_o = q_reg[3]; // extract
  /* T80.vhd:549:41  */
  assign n863_o = t_res ? n849_o : n862_o;
  assign n864_o = q_reg[5]; // extract
  /* T80.vhd:549:41  */
  assign n865_o = t_res ? n850_o : n864_o;
  /* T80.vhd:549:41  */
  assign n867_o = t_res ? alu_op : 4'b0000;
  /* T80.vhd:549:41  */
  assign n869_o = t_res ? save_alu : 1'b0;
  /* T80.vhd:700:52  */
  assign n871_o = tstate == 3'b010;
  /* T80.vhd:700:56  */
  assign n872_o = i_btr & n871_o;
  /* T80.vhd:700:78  */
  assign n873_o = ir[0]; // extract
  /* T80.vhd:700:72  */
  assign n874_o = n873_o & n872_o;
  /* T80.vhd:700:100  */
  assign n876_o = tstate == 3'b001;
  /* T80.vhd:700:104  */
  assign n877_o = i_btr & n876_o;
  /* T80.vhd:700:126  */
  assign n878_o = ir[0]; // extract
  /* T80.vhd:700:130  */
  assign n879_o = ~n878_o;
  /* T80.vhd:700:120  */
  assign n880_o = n879_o & n877_o;
  /* T80.vhd:700:89  */
  assign n881_o = n874_o | n880_o;
  /* T80.vhd:703:54  */
  assign n882_o = ir[0]; // extract
  /* T80.vhd:703:58  */
  assign n883_o = ~n882_o;
  /* T80.vhd:704:69  */
  assign n885_o = {1'b0, di_reg};
  /* T80.vhd:704:90  */
  assign n886_o = wz[7:0]; // extract
  /* T80.vhd:704:86  */
  assign n888_o = {1'b0, n886_o};
  /* T80.vhd:704:79  */
  assign n889_o = n885_o + n888_o;
  /* T80.vhd:706:69  */
  assign n891_o = {1'b0, di_reg};
  /* T80.vhd:706:109  */
  assign n892_o = id16[7:0]; // extract
  /* T80.vhd:706:86  */
  assign n894_o = {1'b0, n892_o};
  /* T80.vhd:706:79  */
  assign n895_o = n891_o + n894_o;
  /* T80.vhd:703:49  */
  assign n896_o = n883_o ? n889_o : n895_o;
  /* T80.vhd:708:68  */
  assign n897_o = di_reg[7]; // extract
  /* T80.vhd:709:65  */
  assign n898_o = n896_o[8]; // extract
  /* T80.vhd:710:65  */
  assign n899_o = n896_o[8]; // extract
  /* T80.vhd:711:61  */
  assign n901_o = n896_o & 9'b000000111;
  /* T80.vhd:711:86  */
  assign n903_o = {1'b0, busa};
  /* T80.vhd:711:78  */
  assign n904_o = n901_o ^ n903_o;
  /* T80.vhd:712:70  */
  assign n905_o = n904_o[0]; // extract
  /* T80.vhd:712:81  */
  assign n906_o = n904_o[1]; // extract
  /* T80.vhd:712:74  */
  assign n907_o = n905_o ^ n906_o;
  /* T80.vhd:712:92  */
  assign n908_o = n904_o[2]; // extract
  /* T80.vhd:712:85  */
  assign n909_o = n907_o ^ n908_o;
  /* T80.vhd:712:103  */
  assign n910_o = n904_o[3]; // extract
  /* T80.vhd:712:96  */
  assign n911_o = n909_o ^ n910_o;
  /* T80.vhd:712:114  */
  assign n912_o = n904_o[4]; // extract
  /* T80.vhd:712:107  */
  assign n913_o = n911_o ^ n912_o;
  /* T80.vhd:712:125  */
  assign n914_o = n904_o[5]; // extract
  /* T80.vhd:712:118  */
  assign n915_o = n913_o ^ n914_o;
  /* T80.vhd:712:136  */
  assign n916_o = n904_o[6]; // extract
  /* T80.vhd:712:129  */
  assign n917_o = n915_o ^ n916_o;
  /* T80.vhd:712:147  */
  assign n918_o = n904_o[7]; // extract
  /* T80.vhd:712:140  */
  assign n919_o = n917_o ^ n918_o;
  /* T80.vhd:712:62  */
  assign n920_o = ~n919_o;
  assign n921_o = {n920_o, n897_o, n898_o};
  assign n922_o = f[2]; // extract
  assign n923_o = {n922_o, n853_o};
  /* T80.vhd:700:41  */
  assign n924_o = n881_o ? n921_o : n923_o;
  assign n925_o = n857_o[1]; // extract
  /* T80.vhd:700:41  */
  assign n926_o = n881_o ? n899_o : n925_o;
  assign n927_o = n857_o[2]; // extract
  assign n928_o = n857_o[0]; // extract
  /* T80.vhd:700:41  */
  assign n929_o = n881_o ? busa : last_b;
  /* T80.vhd:716:51  */
  assign n932_o = tstate == 3'b010;
  /* T80.vhd:716:55  */
  assign n933_o = wait_n & n932_o;
  /* T80.vhd:717:57  */
  assign n935_o = iset == 2'b01;
  /* T80.vhd:717:75  */
  assign n937_o = mcycle == 3'b111;
  /* T80.vhd:717:64  */
  assign n938_o = n937_o & n935_o;
  /* T80.vhd:716:41  */
  assign n939_o = n1014_o ? dinst : ir;
  /* T80.vhd:721:83  */
  assign n940_o = {{8{di_reg[7]}}, di_reg}; // sext
  /* T80.vhd:721:83  */
  assign n941_o = pc + n940_o;
  /* T80.vhd:722:91  */
  assign n942_o = {{8{di_reg[7]}}, di_reg}; // sext
  /* T80.vhd:722:91  */
  assign n943_o = pc + n942_o;
  /* T80.vhd:724:66  */
  assign n945_o = pc + 16'b0000000000000001;
  /* T80.vhd:723:49  */
  assign n946_o = inc_pc ? n945_o : n858_o;
  /* T80.vhd:720:49  */
  assign n947_o = jumpe ? n941_o : n946_o;
  /* T80.vhd:720:49  */
  assign n948_o = jumpe ? n943_o : n859_o;
  /* T80.vhd:727:70  */
  assign n950_o = pc - 16'b0000000000000010;
  /* T80.vhd:729:83  */
  assign n953_o = pc - 16'b0000000000000001;
  /* T80.vhd:730:76  */
  assign n954_o = n950_o[11]; // extract
  /* T80.vhd:731:76  */
  assign n955_o = n950_o[13]; // extract
  /* T80.vhd:736:69  */
  assign n956_o = f[0]; // extract
  /* T80.vhd:737:77  */
  assign n957_o = f[1]; // extract
  /* T80.vhd:738:105  */
  assign n958_o = last_b[0]; // extract
  /* T80.vhd:738:118  */
  assign n959_o = last_b[1]; // extract
  /* T80.vhd:738:109  */
  assign n960_o = n958_o | n959_o;
  /* T80.vhd:738:131  */
  assign n961_o = last_b[2]; // extract
  /* T80.vhd:738:122  */
  assign n962_o = n960_o | n961_o;
  /* T80.vhd:738:144  */
  assign n963_o = last_b[3]; // extract
  /* T80.vhd:738:135  */
  assign n964_o = n962_o | n963_o;
  /* T80.vhd:738:94  */
  assign n965_o = ~n964_o;
  /* T80.vhd:739:93  */
  assign n968_o = last_b - 8'b00000001;
  /* T80.vhd:741:100  */
  assign n969_o = last_b[0]; // extract
  /* T80.vhd:741:114  */
  assign n970_o = last_b[1]; // extract
  /* T80.vhd:741:104  */
  assign n971_o = n969_o & n970_o;
  /* T80.vhd:741:128  */
  assign n972_o = last_b[2]; // extract
  /* T80.vhd:741:118  */
  assign n973_o = n971_o & n972_o;
  /* T80.vhd:741:142  */
  assign n974_o = last_b[3]; // extract
  /* T80.vhd:741:132  */
  assign n975_o = n973_o & n974_o;
  /* T80.vhd:742:93  */
  assign n978_o = last_b + 8'b00000001;
  /* T80.vhd:737:73  */
  assign n979_o = n957_o ? n965_o : n975_o;
  /* T80.vhd:737:73  */
  assign n980_o = n957_o ? n968_o : n978_o;
  /* T80.vhd:732:57  */
  assign n981_o = n992_o ? n979_o : n926_o;
  /* T80.vhd:736:65  */
  assign n982_o = n956_o ? n980_o : last_b;
  /* T80.vhd:746:79  */
  assign n983_o = f[2]; // extract
  /* T80.vhd:746:94  */
  assign n984_o = n982_o[0]; // extract
  /* T80.vhd:746:103  */
  assign n985_o = n982_o[1]; // extract
  /* T80.vhd:746:98  */
  assign n986_o = n984_o ^ n985_o;
  /* T80.vhd:746:112  */
  assign n987_o = n982_o[2]; // extract
  /* T80.vhd:746:107  */
  assign n988_o = n986_o ^ n987_o;
  /* T80.vhd:746:88  */
  assign n989_o = n983_o ^ n988_o;
  assign n990_o = n924_o[2]; // extract
  /* T80.vhd:732:57  */
  assign n991_o = btr_r2 ? n989_o : n990_o;
  /* T80.vhd:732:57  */
  assign n992_o = n956_o & btr_r2;
  assign n994_o = {n955_o, n981_o, n954_o, n991_o};
  assign n995_o = n924_o[2]; // extract
  assign n996_o = {n927_o, n926_o, n928_o, n995_o};
  /* T80.vhd:726:49  */
  assign n997_o = btr_r ? n994_o : n996_o;
  /* T80.vhd:726:49  */
  assign n998_o = btr_r ? n950_o : n947_o;
  /* T80.vhd:726:49  */
  assign n999_o = btr_r ? n953_o : n948_o;
  /* T80.vhd:751:77  */
  assign n1002_o = ir[5:3]; // extract
  assign n1004_o = n1003_o[15:6]; // extract
  assign n1005_o = n1003_o[2:0]; // extract
  assign n1006_o = {n1004_o, n1002_o, n1005_o};
  /* T80.vhd:749:49  */
  assign n1007_o = rstp ? n1006_o : n999_o;
  assign n1008_o = n924_o[2]; // extract
  assign n1009_o = {n927_o, n926_o, n928_o, n1008_o};
  /* T80.vhd:716:41  */
  assign n1010_o = n933_o ? n997_o : n1009_o;
  assign n1011_o = n924_o[1:0]; // extract
  /* T80.vhd:716:41  */
  assign n1012_o = n933_o ? n998_o : n858_o;
  /* T80.vhd:716:41  */
  assign n1013_o = n933_o ? n1007_o : n859_o;
  /* T80.vhd:716:41  */
  assign n1014_o = n938_o & n933_o;
  /* T80.vhd:754:51  */
  assign n1018_o = tstate == 3'b011;
  /* T80.vhd:754:66  */
  assign n1020_o = mcycle == 3'b110;
  /* T80.vhd:754:55  */
  assign n1021_o = n1020_o & n1018_o;
  /* T80.vhd:755:88  */
  assign n1022_o = {{8{di_reg[7]}}, di_reg}; // sext
  /* T80.vhd:755:88  */
  assign n1023_o = regbusc + n1022_o;
  /* T80.vhd:754:41  */
  assign n1024_o = n1021_o ? n1023_o : n1013_o;
  /* T80.vhd:758:51  */
  assign n1026_o = mcycle == 3'b011;
  /* T80.vhd:758:70  */
  assign n1028_o = tstate == 3'b100;
  /* T80.vhd:758:59  */
  assign n1029_o = n1028_o & n1026_o;
  /* T80.vhd:758:85  */
  assign n1030_o = ~no_btr;
  /* T80.vhd:758:74  */
  assign n1031_o = n1030_o & n1029_o;
  /* T80.vhd:759:63  */
  assign n1032_o = i_bt | i_bc;
  /* T80.vhd:760:83  */
  assign n1035_o = pc - 16'b0000000000000001;
  /* T80.vhd:758:41  */
  assign n1036_o = n1037_o ? n1035_o : n1024_o;
  /* T80.vhd:758:41  */
  assign n1037_o = n1032_o & n1031_o;
  /* T80.vhd:764:52  */
  assign n1039_o = tstate == 3'b010;
  /* T80.vhd:764:56  */
  assign n1040_o = wait_n & n1039_o;
  /* T80.vhd:764:85  */
  assign n1042_o = tstate == 3'b100;
  /* T80.vhd:764:100  */
  assign n1044_o = mcycle == 3'b001;
  /* T80.vhd:764:89  */
  assign n1045_o = n1044_o & n1042_o;
  /* T80.vhd:764:74  */
  assign n1046_o = n1040_o | n1045_o;
  /* T80.vhd:765:61  */
  assign n1047_o = incdec_16[2:0]; // extract
  /* T80.vhd:765:74  */
  assign n1049_o = n1047_o == 3'b111;
  /* T80.vhd:766:69  */
  assign n1050_o = incdec_16[3]; // extract
  /* T80.vhd:767:74  */
  assign n1052_o = sp - 16'b0000000000000001;
  /* T80.vhd:769:74  */
  assign n1054_o = sp + 16'b0000000000000001;
  /* T80.vhd:766:57  */
  assign n1055_o = n1050_o ? n1052_o : n1054_o;
  /* T80.vhd:764:41  */
  assign n1056_o = n1057_o ? n1055_o : sp;
  /* T80.vhd:764:41  */
  assign n1057_o = n1049_o & n1046_o;
  /* T80.vhd:774:69  */
  assign n1059_o = tstate == 3'b010;
  /* T80.vhd:774:58  */
  assign n1060_o = n1059_o & addspdd;
  /* T80.vhd:776:74  */
  assign n1061_o = {{8{save_mux[7]}}, save_mux}; // sext
  /* T80.vhd:776:74  */
  assign n1062_o = sp + n1061_o;
  /* T80.vhd:774:41  */
  assign n1063_o = n1060_o ? n1062_o : n1056_o;
  /* T80.vhd:774:41  */
  assign n1064_o = n1060_o ? sp : n1036_o;
  /* T80.vhd:779:41  */
  assign n1065_o = ldsphl ? regbusc : n1063_o;
  /* T80.vhd:782:41  */
  assign n1066_o = exchangeaf ? ap : n772_o;
  assign n1067_o = f[7]; // extract
  assign n1068_o = {n1067_o, n517_o, n1010_o, n1011_o};
  /* T80.vhd:782:41  */
  assign n1069_o = exchangeaf ? fp : n1068_o;
  /* T80.vhd:782:41  */
  assign n1070_o = exchangeaf ? acc : ap;
  /* T80.vhd:782:41  */
  assign n1071_o = exchangeaf ? f : fp;
  /* T80.vhd:789:62  */
  assign n1072_o = ~alternate;
  /* T80.vhd:788:41  */
  assign n1073_o = exchangers ? n1072_o : alternate;
  /* T80.vhd:491:33  */
  assign n1074_o = n540_o ? n605_o : n851_o;
  /* T80.vhd:491:33  */
  assign n1075_o = n540_o ? acc : n1066_o;
  assign n1076_o = f[3:2]; // extract
  assign n1077_o = f[5]; // extract
  assign n1078_o = f[7]; // extract
  assign n1079_o = {n1078_o, n517_o, n1077_o, n516_o, n1076_o, n515_o};
  /* T80.vhd:491:33  */
  assign n1080_o = n540_o ? n1079_o : n1069_o;
  /* T80.vhd:491:33  */
  assign n1081_o = n540_o ? ap : n1070_o;
  /* T80.vhd:491:33  */
  assign n1082_o = n540_o ? fp : n1071_o;
  /* T80.vhd:491:33  */
  assign n1084_o = n543_o & n540_o;
  /* T80.vhd:491:33  */
  assign n1085_o = n540_o ? sp : n1065_o;
  /* T80.vhd:491:33  */
  assign n1086_o = n540_o ? n559_o : n1012_o;
  /* T80.vhd:491:33  */
  assign n1087_o = n540_o ? alternate : n1073_o;
  assign n1088_o = n1064_o[7:0]; // extract
  /* T80.vhd:491:33  */
  assign n1089_o = n540_o ? n578_o : n1088_o;
  assign n1090_o = n1064_o[15:8]; // extract
  assign n1091_o = wz[15:8]; // extract
  /* T80.vhd:491:33  */
  assign n1092_o = n540_o ? n1091_o : n1090_o;
  /* T80.vhd:491:33  */
  assign n1093_o = n540_o ? n611_o : n939_o;
  /* T80.vhd:491:33  */
  assign n1094_o = n540_o ? n612_o : n620_o;
  /* T80.vhd:491:33  */
  assign n1095_o = n543_o & n540_o;
  /* T80.vhd:491:33  */
  assign n1096_o = n540_o ? n614_o : n623_o;
  /* T80.vhd:491:33  */
  assign n1097_o = n540_o ? btr_r : n860_o;
  /* T80.vhd:491:33  */
  assign n1098_o = n540_o ? btr_r2 : n861_o;
  /* T80.vhd:491:33  */
  assign n1099_o = n540_o ? last_b : n929_o;
  assign n1100_o = q_reg[3]; // extract
  /* T80.vhd:491:33  */
  assign n1101_o = n540_o ? n1100_o : n863_o;
  assign n1102_o = q_reg[5]; // extract
  /* T80.vhd:491:33  */
  assign n1103_o = n540_o ? n1102_o : n865_o;
  /* T80.vhd:491:33  */
  assign n1105_o = n540_o ? 4'b0000 : n867_o;
  /* T80.vhd:491:33  */
  assign n1108_o = n540_o ? 1'b0 : n869_o;
  /* T80.vhd:793:43  */
  assign n1114_o = tstate == 3'b011;
  /* T80.vhd:794:41  */
  assign n1115_o = ldz ? di_reg : n1089_o;
  /* T80.vhd:797:41  */
  assign n1116_o = ldw ? di_reg : n1092_o;
  /* T80.vhd:801:54  */
  assign n1117_o = special_ld[2]; // extract
  /* T80.vhd:802:64  */
  assign n1118_o = special_ld[1:0]; // extract
  /* T80.vhd:806:71  */
  assign n1119_o = i[7]; // extract
  /* T80.vhd:808:62  */
  assign n1121_o = i == 8'b00000000;
  /* T80.vhd:808:57  */
  assign n1124_o = n1121_o ? 1'b1 : 1'b0;
  /* T80.vhd:814:71  */
  assign n1125_o = i[5]; // extract
  /* T80.vhd:816:71  */
  assign n1127_o = i[3]; // extract
  /* T80.vhd:803:49  */
  assign n1130_o = n1118_o == 2'b00;
  /* T80.vhd:823:71  */
  assign n1131_o = r[7]; // extract
  /* T80.vhd:825:62  */
  assign n1133_o = r == 8'b00000000;
  /* T80.vhd:825:57  */
  assign n1136_o = n1133_o ? 1'b1 : 1'b0;
  /* T80.vhd:831:71  */
  assign n1137_o = r[5]; // extract
  /* T80.vhd:833:71  */
  assign n1139_o = r[3]; // extract
  /* T80.vhd:820:49  */
  assign n1142_o = n1118_o == 2'b01;
  /* T80.vhd:836:49  */
  assign n1144_o = n1118_o == 2'b10;
  assign n1145_o = {n1144_o, n1142_o, n1130_o};
  /* T80.vhd:802:49  */
  always @*
    case (n1145_o)
      3'b100: n1146_o = n1075_o;
      3'b010: n1146_o = r;
      3'b001: n1146_o = i;
      default: n1146_o = n1075_o;
    endcase
  assign n1147_o = n1080_o[1]; // extract
  /* T80.vhd:802:49  */
  always @*
    case (n1145_o)
      3'b100: n1148_o = n1147_o;
      3'b010: n1148_o = 1'b0;
      3'b001: n1148_o = 1'b0;
      default: n1148_o = n1147_o;
    endcase
  assign n1149_o = n1080_o[2]; // extract
  /* T80.vhd:802:49  */
  always @*
    case (n1145_o)
      3'b100: n1150_o = n1149_o;
      3'b010: n1150_o = inte_ff2;
      3'b001: n1150_o = inte_ff2;
      default: n1150_o = n1149_o;
    endcase
  assign n1151_o = n1080_o[3]; // extract
  /* T80.vhd:802:49  */
  always @*
    case (n1145_o)
      3'b100: n1152_o = n1151_o;
      3'b010: n1152_o = n1139_o;
      3'b001: n1152_o = n1127_o;
      default: n1152_o = n1151_o;
    endcase
  assign n1153_o = n1080_o[4]; // extract
  /* T80.vhd:802:49  */
  always @*
    case (n1145_o)
      3'b100: n1154_o = n1153_o;
      3'b010: n1154_o = 1'b0;
      3'b001: n1154_o = 1'b0;
      default: n1154_o = n1153_o;
    endcase
  assign n1155_o = n1080_o[5]; // extract
  /* T80.vhd:802:49  */
  always @*
    case (n1145_o)
      3'b100: n1156_o = n1155_o;
      3'b010: n1156_o = n1137_o;
      3'b001: n1156_o = n1125_o;
      default: n1156_o = n1155_o;
    endcase
  assign n1157_o = n1080_o[6]; // extract
  /* T80.vhd:802:49  */
  always @*
    case (n1145_o)
      3'b100: n1158_o = n1157_o;
      3'b010: n1158_o = n1136_o;
      3'b001: n1158_o = n1124_o;
      default: n1158_o = n1157_o;
    endcase
  assign n1159_o = n1080_o[7]; // extract
  /* T80.vhd:802:49  */
  always @*
    case (n1145_o)
      3'b100: n1160_o = n1159_o;
      3'b010: n1160_o = n1131_o;
      3'b001: n1160_o = n1119_o;
      default: n1160_o = n1159_o;
    endcase
  /* T80.vhd:802:49  */
  always @*
    case (n1145_o)
      3'b100: n1161_o = acc;
      3'b010: n1161_o = i;
      3'b001: n1161_o = i;
      default: n1161_o = i;
    endcase
  assign n1162_o = r[7]; // extract
  assign n1163_o = {n1162_o, n607_o};
  /* T80.vhd:802:49  */
  always @*
    case (n1145_o)
      3'b100: n1164_o = n1163_o;
      3'b010: n1164_o = n1163_o;
      3'b001: n1164_o = n1163_o;
      default: n1164_o = acc;
    endcase
  /* T80.vhd:793:33  */
  assign n1165_o = n1173_o ? n1146_o : n1075_o;
  assign n1166_o = {n1160_o, n1158_o, n1156_o, n1154_o, n1152_o, n1150_o, n1148_o};
  assign n1167_o = n1080_o[7:1]; // extract
  /* T80.vhd:793:33  */
  assign n1168_o = n1175_o ? n1166_o : n1167_o;
  /* T80.vhd:452:25  */
  assign n1169_o = n1344_o ? n1161_o : i;
  assign n1170_o = r[7]; // extract
  assign n1171_o = {n1170_o, n607_o};
  /* T80.vhd:801:41  */
  assign n1172_o = n1117_o ? n1164_o : n1171_o;
  /* T80.vhd:793:33  */
  assign n1173_o = n1117_o & n1114_o;
  /* T80.vhd:793:33  */
  assign n1175_o = n1117_o & n1114_o;
  assign n1176_o = n1080_o[0]; // extract
  /* T80.vhd:793:33  */
  assign n1177_o = n1117_o & n1114_o;
  assign n1178_o = r[7]; // extract
  assign n1179_o = {n1178_o, n607_o};
  /* T80.vhd:793:33  */
  assign n1180_o = n1114_o ? n1172_o : n1179_o;
  assign n1181_o = {n1116_o, n1115_o};
  assign n1182_o = {n1092_o, n1089_o};
  /* T80.vhd:793:33  */
  assign n1183_o = n1114_o ? n1181_o : n1182_o;
  /* T80.vhd:844:44  */
  assign n1184_o = ~i_djnz;
  /* T80.vhd:844:50  */
  assign n1185_o = save_alu_r & n1184_o;
  /* T80.vhd:844:84  */
  assign n1187_o = alu_op_r == 4'b1001;
  /* T80.vhd:844:72  */
  assign n1188_o = n1185_o | n1187_o;
  /* T80.vhd:853:71  */
  assign n1189_o = f_out[7:1]; // extract
  /* T80.vhd:854:64  */
  assign n1190_o = ~preservec_r;
  /* T80.vhd:855:75  */
  assign n1191_o = f_out[0]; // extract
  /* T80.vhd:854:49  */
  assign n1192_o = n1190_o ? n1191_o : n1176_o;
  assign n1193_o = {n1189_o, n1192_o};
  assign n1194_o = {n1168_o, n1176_o};
  /* T80.vhd:844:33  */
  assign n1195_o = n1188_o ? n1193_o : n1194_o;
  /* T80.vhd:859:48  */
  assign n1196_o = i_inrc & t_res;
  /* T80.vhd:862:60  */
  assign n1199_o = di_reg[3]; // extract
  /* T80.vhd:863:60  */
  assign n1200_o = di_reg[5]; // extract
  /* T80.vhd:864:63  */
  assign n1202_o = di_reg == 8'b00000000;
  /* T80.vhd:864:41  */
  assign n1205_o = n1202_o ? 1'b1 : 1'b0;
  /* T80.vhd:869:60  */
  assign n1206_o = di_reg[7]; // extract
  /* T80.vhd:870:65  */
  assign n1207_o = di_reg[0]; // extract
  /* T80.vhd:870:79  */
  assign n1208_o = di_reg[1]; // extract
  /* T80.vhd:870:69  */
  assign n1209_o = n1207_o ^ n1208_o;
  /* T80.vhd:870:93  */
  assign n1210_o = di_reg[2]; // extract
  /* T80.vhd:870:83  */
  assign n1211_o = n1209_o ^ n1210_o;
  /* T80.vhd:870:107  */
  assign n1212_o = di_reg[3]; // extract
  /* T80.vhd:870:97  */
  assign n1213_o = n1211_o ^ n1212_o;
  /* T80.vhd:871:55  */
  assign n1214_o = di_reg[4]; // extract
  /* T80.vhd:870:111  */
  assign n1215_o = n1213_o ^ n1214_o;
  /* T80.vhd:871:69  */
  assign n1216_o = di_reg[5]; // extract
  /* T80.vhd:871:59  */
  assign n1217_o = n1215_o ^ n1216_o;
  /* T80.vhd:871:83  */
  assign n1218_o = di_reg[6]; // extract
  /* T80.vhd:871:73  */
  assign n1219_o = n1217_o ^ n1218_o;
  /* T80.vhd:871:97  */
  assign n1220_o = di_reg[7]; // extract
  /* T80.vhd:871:87  */
  assign n1221_o = n1219_o ^ n1220_o;
  /* T80.vhd:870:54  */
  assign n1222_o = ~n1221_o;
  assign n1223_o = {n1206_o, n1205_o, n1200_o, 1'b0, n1199_o, n1222_o, 1'b0};
  assign n1224_o = n1195_o[7:1]; // extract
  /* T80.vhd:859:33  */
  assign n1225_o = n1196_o ? n1223_o : n1224_o;
  assign n1226_o = n1195_o[0]; // extract
  /* T80.vhd:874:43  */
  assign n1228_o = tstate == 3'b001;
  /* T80.vhd:874:64  */
  assign n1229_o = ~auto_wait_t1;
  /* T80.vhd:874:47  */
  assign n1230_o = n1229_o & n1228_o;
  /* T80.vhd:876:57  */
  assign n1231_o = i_rld | i_rrd;
  /* T80.vhd:877:50  */
  assign n1232_o = ~i_rxdd;
  /* T80.vhd:877:41  */
  assign n1233_o = n1232_o ? busb : n2428_q;
  /* T80.vhd:881:71  */
  assign n1234_o = busa[3:0]; // extract
  /* T80.vhd:882:71  */
  assign n1235_o = busb[3:0]; // extract
  assign n1236_o = {n1235_o, n1234_o};
  /* T80.vhd:880:41  */
  assign n1237_o = i_rld ? n1236_o : n1233_o;
  /* T80.vhd:885:71  */
  assign n1238_o = busb[7:4]; // extract
  /* T80.vhd:886:71  */
  assign n1239_o = busa[3:0]; // extract
  assign n1240_o = {n1239_o, n1238_o};
  /* T80.vhd:884:41  */
  assign n1241_o = i_rrd ? n1240_o : n1237_o;
  /* T80.vhd:874:33  */
  assign n1242_o = n1230_o ? n1241_o : n2428_q;
  /* T80.vhd:452:25  */
  assign n1243_o = n1370_o ? n1231_o : i_rxdd;
  assign n1246_o = {1'b1, 4'b0111};
  assign n1247_o = {read_to_reg, set_busa_to};
  /* T80.vhd:893:41  */
  assign n1248_o = read_to_acc ? n1246_o : n1247_o;
  /* T80.vhd:890:33  */
  assign n1250_o = t_res ? n1248_o : 5'b00000;
  /* T80.vhd:899:43  */
  assign n1253_o = tstate == 3'b001;
  /* T80.vhd:899:47  */
  assign n1254_o = i_bt & n1253_o;
  /* T80.vhd:900:59  */
  assign n1255_o = aluqs[3]; // extract
  /* T80.vhd:901:59  */
  assign n1256_o = aluqs[1]; // extract
  assign n1259_o = {n1256_o, 1'b0, n1255_o};
  assign n1260_o = n1225_o[0]; // extract
  /* T80.vhd:899:33  */
  assign n1261_o = n1254_o ? 1'b0 : n1260_o;
  assign n1262_o = n1225_o[4:2]; // extract
  /* T80.vhd:899:33  */
  assign n1263_o = n1254_o ? n1259_o : n1262_o;
  assign n1265_o = n1225_o[6:5]; // extract
  assign n1266_o = n1225_o[1]; // extract
  /* T80.vhd:905:43  */
  assign n1268_o = tstate == 3'b001;
  /* T80.vhd:905:47  */
  assign n1269_o = i_bc & n1268_o;
  /* T80.vhd:906:72  */
  assign n1270_o = f_out[4]; // extract
  /* T80.vhd:906:65  */
  assign n1272_o = {7'b0000000, n1270_o};
  /* T80.vhd:906:52  */
  assign n1273_o = aluqs - n1272_o;
  /* T80.vhd:907:55  */
  assign n1274_o = n1273_o[3]; // extract
  /* T80.vhd:908:55  */
  assign n1275_o = n1273_o[1]; // extract
  assign n1276_o = n1263_o[0]; // extract
  /* T80.vhd:905:33  */
  assign n1277_o = n1269_o ? n1274_o : n1276_o;
  assign n1278_o = n1263_o[2]; // extract
  /* T80.vhd:905:33  */
  assign n1279_o = n1269_o ? n1275_o : n1278_o;
  assign n1281_o = n1263_o[1]; // extract
  /* T80.vhd:910:47  */
  assign n1283_o = i_bc | i_bt;
  /* T80.vhd:910:33  */
  assign n1284_o = n1283_o ? incdecz : n1266_o;
  /* T80.vhd:914:44  */
  assign n1286_o = tstate == 3'b001;
  /* T80.vhd:914:63  */
  assign n1287_o = ~save_alu_r;
  /* T80.vhd:914:48  */
  assign n1288_o = n1287_o & n1286_o;
  /* T80.vhd:914:86  */
  assign n1289_o = ~auto_wait_t1;
  /* T80.vhd:914:69  */
  assign n1290_o = n1289_o & n1288_o;
  /* T80.vhd:915:72  */
  assign n1292_o = alu_op_r != 4'b0111;
  /* T80.vhd:915:59  */
  assign n1293_o = n1292_o & save_alu_r;
  /* T80.vhd:914:93  */
  assign n1294_o = n1290_o | n1293_o;
  /* T80.vhd:917:41  */
  assign n1296_o = read_to_reg_r == 5'b10111;
  /* T80.vhd:919:41  */
  assign n1298_o = read_to_reg_r == 5'b10110;
  /* T80.vhd:921:41  */
  assign n1300_o = read_to_reg_r == 5'b11000;
  /* T80.vhd:923:41  */
  assign n1302_o = read_to_reg_r == 5'b11001;
  /* T80.vhd:925:41  */
  assign n1304_o = read_to_reg_r == 5'b11011;
  assign n1305_o = {n1304_o, n1302_o, n1300_o, n1298_o, n1296_o};
  /* T80.vhd:916:41  */
  always @*
    case (n1305_o)
      5'b10000: n1306_o = n1242_o;
      5'b01000: n1306_o = n1242_o;
      5'b00100: n1306_o = n1242_o;
      5'b00010: n1306_o = save_mux;
      5'b00001: n1306_o = n1242_o;
      default: n1306_o = n1242_o;
    endcase
  /* T80.vhd:916:41  */
  always @*
    case (n1305_o)
      5'b10000: n1307_o = n1165_o;
      5'b01000: n1307_o = n1165_o;
      5'b00100: n1307_o = n1165_o;
      5'b00010: n1307_o = n1165_o;
      5'b00001: n1307_o = save_mux;
      default: n1307_o = n1165_o;
    endcase
  assign n1308_o = {n1265_o, n1279_o, n1281_o, n1277_o, n1284_o, n1261_o, n1226_o};
  /* T80.vhd:916:41  */
  always @*
    case (n1305_o)
      5'b10000: n1309_o = save_mux;
      5'b01000: n1309_o = n1308_o;
      5'b00100: n1309_o = n1308_o;
      5'b00010: n1309_o = n1308_o;
      5'b00001: n1309_o = n1308_o;
      default: n1309_o = n1308_o;
    endcase
  assign n1310_o = n1085_o[7:0]; // extract
  /* T80.vhd:916:41  */
  always @*
    case (n1305_o)
      5'b10000: n1311_o = n1310_o;
      5'b01000: n1311_o = n1310_o;
      5'b00100: n1311_o = save_mux;
      5'b00010: n1311_o = n1310_o;
      5'b00001: n1311_o = n1310_o;
      default: n1311_o = n1310_o;
    endcase
  assign n1312_o = n1085_o[15:8]; // extract
  /* T80.vhd:916:41  */
  always @*
    case (n1305_o)
      5'b10000: n1313_o = n1312_o;
      5'b01000: n1313_o = save_mux;
      5'b00100: n1313_o = n1312_o;
      5'b00010: n1313_o = n1312_o;
      5'b00001: n1313_o = n1312_o;
      default: n1313_o = n1312_o;
    endcase
  /* T80.vhd:934:41  */
  assign n1314_o = xybit_undoc ? aluqs : n1306_o;
  /* T80.vhd:914:33  */
  assign n1315_o = n1294_o ? n1314_o : n1242_o;
  /* T80.vhd:914:33  */
  assign n1316_o = n1294_o ? n1307_o : n1165_o;
  assign n1317_o = {n1265_o, n1279_o, n1281_o, n1277_o, n1284_o, n1261_o, n1226_o};
  /* T80.vhd:914:33  */
  assign n1318_o = n1294_o ? n1309_o : n1317_o;
  assign n1319_o = {n1313_o, n1311_o};
  /* T80.vhd:914:33  */
  assign n1320_o = n1294_o ? n1319_o : n1085_o;
  /* T80.vhd:943:48  */
  assign n1322_o = 1'b1 & t_res;
  /* T80.vhd:944:50  */
  assign n1323_o = ~i_scf;
  /* T80.vhd:944:66  */
  assign n1324_o = ~i_ccf;
  /* T80.vhd:944:56  */
  assign n1325_o = n1324_o & n1323_o;
  /* T80.vhd:945:54  */
  assign n1326_o = f != f_last;
  /* T80.vhd:946:75  */
  assign n1327_o = f[5]; // extract
  /* T80.vhd:947:75  */
  assign n1328_o = f[3]; // extract
  /* T80.vhd:945:49  */
  assign n1331_o = n1326_o ? n1328_o : 1'b0;
  /* T80.vhd:945:49  */
  assign n1332_o = n1326_o ? n1327_o : 1'b0;
  /* T80.vhd:943:33  */
  assign n1333_o = n1335_o ? n1331_o : n1101_o;
  /* T80.vhd:943:33  */
  assign n1334_o = n1336_o ? n1332_o : n1103_o;
  /* T80.vhd:943:33  */
  assign n1335_o = n1325_o & n1322_o;
  /* T80.vhd:943:33  */
  assign n1336_o = n1325_o & n1322_o;
  /* T80.vhd:452:25  */
  assign n1337_o = n1362_o ? f : f_last;
  /* T80.vhd:452:25  */
  assign n1338_o = clken ? n1074_o : n2427_q;
  /* T80.vhd:452:25  */
  assign n1339_o = clken ? n1315_o : n2428_q;
  /* T80.vhd:452:25  */
  assign n1340_o = clken ? n1316_o : acc;
  /* T80.vhd:452:25  */
  assign n1341_o = clken ? n1318_o : f;
  /* T80.vhd:452:25  */
  assign n1342_o = clken ? n1081_o : ap;
  /* T80.vhd:452:25  */
  assign n1343_o = clken ? n1082_o : fp;
  /* T80.vhd:452:25  */
  assign n1344_o = n1177_o & clken;
  /* T80.vhd:452:25  */
  assign n1345_o = clken ? n1180_o : r;
  /* T80.vhd:452:25  */
  assign n1346_o = clken ? n1320_o : sp;
  /* T80.vhd:452:25  */
  assign n1347_o = clken ? n1086_o : pc;
  /* T80.vhd:452:25  */
  assign n1348_o = clken ? n1087_o : alternate;
  /* T80.vhd:452:25  */
  assign n1349_o = clken ? n1183_o : wz;
  /* T80.vhd:452:25  */
  assign n1350_o = clken ? n1093_o : ir;
  /* T80.vhd:452:25  */
  assign n1351_o = clken ? n1094_o : iset;
  /* T80.vhd:452:25  */
  assign n1352_o = n521_o & clken;
  /* T80.vhd:452:25  */
  assign n1353_o = n1095_o & clken;
  /* T80.vhd:452:25  */
  assign n1354_o = clken ? n1096_o : xy_ind;
  /* T80.vhd:452:25  */
  assign n1355_o = clken ? n1097_o : btr_r;
  /* T80.vhd:452:25  */
  assign n1356_o = clken ? n1098_o : btr_r2;
  /* T80.vhd:452:25  */
  assign n1357_o = clken ? n1099_o : last_b;
  assign n1358_o = q_reg[3]; // extract
  /* T80.vhd:452:25  */
  assign n1359_o = clken ? n1333_o : n1358_o;
  assign n1360_o = q_reg[5]; // extract
  /* T80.vhd:452:25  */
  assign n1361_o = clken ? n1334_o : n1360_o;
  /* T80.vhd:452:25  */
  assign n1362_o = n1322_o & clken;
  /* T80.vhd:452:25  */
  assign n1363_o = clken ? n1250_o : read_to_reg_r;
  /* T80.vhd:452:25  */
  assign n1364_o = clken ? arith16 : arith16_r;
  /* T80.vhd:452:25  */
  assign n1365_o = clken ? n535_o : z16_r;
  /* T80.vhd:452:25  */
  assign n1366_o = clken ? n1105_o : alu_op_r;
  /* T80.vhd:452:25  */
  assign n1367_o = clken ? n1108_o : save_alu_r;
  /* T80.vhd:452:25  */
  assign n1368_o = clken ? preservec : preservec_r;
  /* T80.vhd:452:25  */
  assign n1369_o = clken ? mcycles_d : mcycles;
  /* T80.vhd:452:25  */
  assign n1370_o = n1230_o & clken;
  /* T80.vhd:440:25  */
  assign n1376_o = dirset ? n460_o : n1338_o;
  /* T80.vhd:440:25  */
  assign n1377_o = dirset ? n2428_q : n1339_o;
  /* T80.vhd:440:25  */
  assign n1378_o = dirset ? n452_o : n1340_o;
  /* T80.vhd:440:25  */
  assign n1379_o = dirset ? n453_o : n1341_o;
  /* T80.vhd:440:25  */
  assign n1380_o = dirset ? n454_o : n1342_o;
  /* T80.vhd:440:25  */
  assign n1381_o = dirset ? n455_o : n1343_o;
  /* T80.vhd:440:25  */
  assign n1382_o = dirset ? n456_o : n1169_o;
  /* T80.vhd:440:25  */
  assign n1383_o = dirset ? n457_o : n1345_o;
  /* T80.vhd:440:25  */
  assign n1384_o = dirset ? n458_o : n1346_o;
  /* T80.vhd:440:25  */
  assign n1385_o = dirset ? n459_o : n1347_o;
  /* T80.vhd:440:25  */
  assign n1386_o = dirset ? alternate : n1348_o;
  /* T80.vhd:440:25  */
  assign n1387_o = dirset ? wz : n1349_o;
  /* T80.vhd:440:25  */
  assign n1388_o = dirset ? ir : n1350_o;
  /* T80.vhd:440:25  */
  assign n1389_o = dirset ? iset : n1351_o;
  /* T80.vhd:440:25  */
  assign n1390_o = dirset ? n461_o : n522_o;
  /* T80.vhd:440:25  */
  assign n1391_o = dirset ? xy_state : n613_o;
  /* T80.vhd:440:25  */
  assign n1392_o = dirset ? xy_ind : n1354_o;
  /* T80.vhd:440:25  */
  assign n1393_o = dirset ? btr_r : n1355_o;
  /* T80.vhd:440:25  */
  assign n1394_o = dirset ? btr_r2 : n1356_o;
  /* T80.vhd:440:25  */
  assign n1395_o = dirset ? last_b : n1357_o;
  assign n1396_o = q_reg[3]; // extract
  /* T80.vhd:440:25  */
  assign n1397_o = dirset ? n1396_o : n1359_o;
  assign n1398_o = q_reg[5]; // extract
  /* T80.vhd:440:25  */
  assign n1399_o = dirset ? n1398_o : n1361_o;
  /* T80.vhd:440:25  */
  assign n1400_o = dirset ? f_last : n1337_o;
  /* T80.vhd:440:25  */
  assign n1401_o = dirset ? read_to_reg_r : n1363_o;
  /* T80.vhd:440:25  */
  assign n1402_o = dirset ? arith16_r : n1364_o;
  /* T80.vhd:440:25  */
  assign n1403_o = dirset ? z16_r : n1365_o;
  /* T80.vhd:440:25  */
  assign n1404_o = dirset ? alu_op_r : n1366_o;
  /* T80.vhd:440:25  */
  assign n1405_o = dirset ? save_alu_r : n1367_o;
  /* T80.vhd:440:25  */
  assign n1406_o = dirset ? preservec_r : n1368_o;
  /* T80.vhd:440:25  */
  assign n1407_o = dirset ? mcycles : n1369_o;
  /* T80.vhd:440:25  */
  assign n1408_o = dirset ? i_rxdd : n1243_o;
  assign n1492_o = q_reg[2:0]; // extract
  /* T80.vhd:400:17  */
  assign n1493_o = n450_o ? 3'b000 : n1492_o;
  assign n1497_o = q_reg[4]; // extract
  /* T80.vhd:400:17  */
  assign n1498_o = n450_o ? 1'b0 : n1497_o;
  assign n1502_o = q_reg[7:6]; // extract
  /* T80.vhd:400:17  */
  assign n1503_o = n450_o ? 2'b00 : n1502_o;
  /* T80.vhd:967:102  */
  assign n1545_o = mulu_prod32[3:0]; // extract
  /* T80.vhd:967:81  */
  assign n1546_o = {4'b0, mulu_fakt1};  //  uext
  /* T80.vhd:967:81  */
  assign n1547_o = {16'b0, n1545_o};  //  uext
  /* T80.vhd:967:81  */
  assign n1548_o = n1546_o * n1547_o; // umul
  /* T80.vhd:967:144  */
  assign n1549_o = mulu_prod32[31:16]; // extract
  /* T80.vhd:967:132  */
  assign n1551_o = {4'b0000, n1549_o};
  /* T80.vhd:967:116  */
  assign n1552_o = n1548_o + n1551_o;
  /* T80.vhd:968:53  */
  assign n1553_o = mulu_prod32[15:4]; // extract
  /* T80.vhd:978:63  */
  assign n1558_o = set_busb_to[0]; // extract
  /* T80.vhd:979:91  */
  assign n1559_o = regbusb[7:0]; // extract
  /* T80.vhd:981:91  */
  assign n1560_o = regbusb[15:8]; // extract
  /* T80.vhd:978:49  */
  assign n1561_o = n1558_o ? n1559_o : n1560_o;
  assign n1563_o = {16'b0000000000000000, regbusa};
  assign n1564_o = {16'b0000000000000000, 8'bX, acc};
  /* T80.vhd:973:41  */
  assign n1565_o = i_mulub ? n1564_o : n1563_o;
  assign n1566_o = {n1561_o, 8'b00000000};
  /* T80.vhd:973:41  */
  assign n1567_o = i_mulub ? n1566_o : regbusb;
  /* T80.vhd:972:33  */
  assign n1568_o = t_res ? n1565_o : mulu_tmp;
  /* T80.vhd:971:25  */
  assign n1571_o = t_res & clken;
  /* T80.vhd:1005:70  */
  assign n1578_o = set_busa_to[2:1]; // extract
  /* T80.vhd:1005:57  */
  assign n1579_o = {alternate, n1578_o};
  /* T80.vhd:1006:43  */
  assign n1580_o = ~xy_ind;
  /* T80.vhd:1006:62  */
  assign n1582_o = xy_state != 2'b00;
  /* T80.vhd:1006:49  */
  assign n1583_o = n1582_o & n1580_o;
  /* T80.vhd:1006:85  */
  assign n1584_o = set_busa_to[2:1]; // extract
  /* T80.vhd:1006:98  */
  assign n1586_o = n1584_o == 2'b10;
  /* T80.vhd:1006:70  */
  assign n1587_o = n1586_o & n1583_o;
  /* T80.vhd:1007:63  */
  assign n1588_o = xy_state[1]; // extract
  /* T80.vhd:1007:67  */
  assign n1590_o = {n1588_o, 2'b11};
  /* T80.vhd:1006:33  */
  assign n1591_o = n1587_o ? n1590_o : n1579_o;
  /* T80.vhd:1011:70  */
  assign n1592_o = set_busb_to[2:1]; // extract
  /* T80.vhd:1011:57  */
  assign n1593_o = {alternate, n1592_o};
  /* T80.vhd:1012:43  */
  assign n1594_o = ~xy_ind;
  /* T80.vhd:1012:62  */
  assign n1596_o = xy_state != 2'b00;
  /* T80.vhd:1012:49  */
  assign n1597_o = n1596_o & n1594_o;
  /* T80.vhd:1012:85  */
  assign n1598_o = set_busb_to[2:1]; // extract
  /* T80.vhd:1012:98  */
  assign n1600_o = n1598_o == 2'b10;
  /* T80.vhd:1012:70  */
  assign n1601_o = n1600_o & n1597_o;
  /* T80.vhd:1013:63  */
  assign n1602_o = xy_state[1]; // extract
  /* T80.vhd:1013:67  */
  assign n1604_o = {n1602_o, 2'b11};
  /* T80.vhd:1012:33  */
  assign n1605_o = n1601_o ? n1604_o : n1593_o;
  /* T80.vhd:1017:68  */
  assign n1606_o = set_addr_to[1:0]; // extract
  /* T80.vhd:1017:55  */
  assign n1607_o = {alternate, n1606_o};
  /* T80.vhd:1019:50  */
  assign n1608_o = jumpxy | ldsphl;
  /* T80.vhd:1020:63  */
  assign n1610_o = {alternate, 2'b10};
  /* T80.vhd:1019:33  */
  assign n1611_o = n1608_o ? n1610_o : n1607_o;
  /* T80.vhd:1022:51  */
  assign n1612_o = jumpxy | ldsphl;
  /* T80.vhd:1022:81  */
  assign n1614_o = xy_state != 2'b00;
  /* T80.vhd:1022:68  */
  assign n1615_o = n1614_o & n1612_o;
  /* T80.vhd:1022:101  */
  assign n1617_o = mcycle == 3'b110;
  /* T80.vhd:1022:90  */
  assign n1618_o = n1615_o | n1617_o;
  /* T80.vhd:1023:61  */
  assign n1619_o = xy_state[1]; // extract
  /* T80.vhd:1023:65  */
  assign n1621_o = {n1619_o, 2'b11};
  /* T80.vhd:1022:33  */
  assign n1622_o = n1618_o ? n1621_o : n1611_o;
  /* T80.vhd:1026:49  */
  assign n1623_o = save_alu_r & i_djnz;
  /* T80.vhd:1026:70  */
  assign n1625_o = 1'b1 & n1623_o;
  /* T80.vhd:1027:57  */
  assign n1626_o = f_out[6]; // extract
  /* T80.vhd:1026:33  */
  assign n1627_o = n1625_o ? n1626_o : incdecz;
  /* T80.vhd:1029:44  */
  assign n1629_o = tstate == 3'b010;
  /* T80.vhd:1029:59  */
  assign n1631_o = tstate == 3'b011;
  /* T80.vhd:1029:74  */
  assign n1633_o = mcycle == 3'b001;
  /* T80.vhd:1029:63  */
  assign n1634_o = n1633_o & n1631_o;
  /* T80.vhd:1029:48  */
  assign n1635_o = n1629_o | n1634_o;
  /* T80.vhd:1029:97  */
  assign n1636_o = incdec_16[2:0]; // extract
  /* T80.vhd:1029:110  */
  assign n1638_o = n1636_o == 3'b100;
  /* T80.vhd:1029:84  */
  assign n1639_o = n1638_o & n1635_o;
  /* T80.vhd:1030:49  */
  assign n1641_o = id16 == 16'b0000000000000000;
  /* T80.vhd:1030:41  */
  assign n1644_o = n1641_o ? 1'b0 : 1'b1;
  /* T80.vhd:1029:33  */
  assign n1645_o = n1639_o ? n1644_o : n1627_o;
  /* T80.vhd:1044:46  */
  assign n1657_o = incdec_16[1:0]; // extract
  /* T80.vhd:1044:35  */
  assign n1658_o = {alternate, n1657_o};
  /* T80.vhd:1044:72  */
  assign n1660_o = tstate == 3'b010;
  /* T80.vhd:1045:41  */
  assign n1662_o = tstate == 3'b011;
  /* T80.vhd:1045:56  */
  assign n1664_o = mcycle == 3'b001;
  /* T80.vhd:1045:45  */
  assign n1665_o = n1664_o & n1662_o;
  /* T80.vhd:1045:77  */
  assign n1666_o = incdec_16[2]; // extract
  /* T80.vhd:1045:64  */
  assign n1667_o = n1666_o & n1665_o;
  /* T80.vhd:1044:76  */
  assign n1668_o = n1660_o | n1667_o;
  /* T80.vhd:1045:102  */
  assign n1670_o = xy_state == 2'b00;
  /* T80.vhd:1045:89  */
  assign n1671_o = n1670_o & n1668_o;
  /* T80.vhd:1044:59  */
  assign n1672_o = n1671_o ? n1658_o : n1690_o;
  /* T80.vhd:1046:33  */
  assign n1673_o = xy_state[1]; // extract
  /* T80.vhd:1046:37  */
  assign n1675_o = {n1673_o, 2'b11};
  /* T80.vhd:1046:57  */
  assign n1677_o = tstate == 3'b010;
  /* T80.vhd:1047:41  */
  assign n1679_o = tstate == 3'b011;
  /* T80.vhd:1047:56  */
  assign n1681_o = mcycle == 3'b001;
  /* T80.vhd:1047:45  */
  assign n1682_o = n1681_o & n1679_o;
  /* T80.vhd:1047:77  */
  assign n1683_o = incdec_16[2]; // extract
  /* T80.vhd:1047:64  */
  assign n1684_o = n1683_o & n1682_o;
  /* T80.vhd:1046:61  */
  assign n1685_o = n1677_o | n1684_o;
  /* T80.vhd:1047:102  */
  assign n1686_o = incdec_16[1:0]; // extract
  /* T80.vhd:1047:115  */
  assign n1688_o = n1686_o == 2'b10;
  /* T80.vhd:1047:89  */
  assign n1689_o = n1688_o & n1685_o;
  /* T80.vhd:1045:109  */
  assign n1690_o = n1689_o ? n1675_o : n1696_o;
  /* T80.vhd:1049:35  */
  assign n1692_o = {alternate, 2'b10};
  /* T80.vhd:1049:75  */
  assign n1694_o = tstate == 3'b011;
  /* T80.vhd:1049:64  */
  assign n1695_o = n1694_o & exchangedh;
  /* T80.vhd:1047:122  */
  assign n1696_o = n1695_o ? n1692_o : n1703_o;
  /* T80.vhd:1050:35  */
  assign n1698_o = {alternate, 2'b01};
  /* T80.vhd:1050:65  */
  assign n1699_o = exchangedh | i_mulu;
  /* T80.vhd:1050:93  */
  assign n1701_o = tstate == 3'b100;
  /* T80.vhd:1050:82  */
  assign n1702_o = n1701_o & n1699_o;
  /* T80.vhd:1049:79  */
  assign n1703_o = n1702_o ? n1698_o : n1712_o;
  /* T80.vhd:1052:35  */
  assign n1705_o = {alternate, 2'b10};
  /* T80.vhd:1052:77  */
  assign n1707_o = xy_state == 2'b00;
  /* T80.vhd:1052:64  */
  assign n1708_o = n1707_o & exchangewh;
  /* T80.vhd:1052:95  */
  assign n1710_o = tstate == 3'b100;
  /* T80.vhd:1052:84  */
  assign n1711_o = n1710_o & n1708_o;
  /* T80.vhd:1050:97  */
  assign n1712_o = n1711_o ? n1705_o : n1719_o;
  /* T80.vhd:1053:33  */
  assign n1713_o = xy_state[1]; // extract
  /* T80.vhd:1053:37  */
  assign n1715_o = {n1713_o, 2'b11};
  /* T80.vhd:1053:77  */
  assign n1717_o = tstate == 3'b100;
  /* T80.vhd:1053:66  */
  assign n1718_o = n1717_o & exchangewh;
  /* T80.vhd:1052:99  */
  assign n1719_o = n1718_o ? n1715_o : n1724_o;
  /* T80.vhd:1055:60  */
  assign n1722_o = tstate == 3'b100;
  /* T80.vhd:1055:49  */
  assign n1723_o = n1722_o & ldhlsp;
  /* T80.vhd:1053:81  */
  assign n1724_o = n1723_o ? 3'b010 : regaddra_r;
  /* T80.vhd:1062:35  */
  assign n1726_o = {alternate, 2'b01};
  /* T80.vhd:1062:75  */
  assign n1728_o = tstate == 3'b011;
  /* T80.vhd:1062:64  */
  assign n1729_o = n1728_o & exchangedh;
  /* T80.vhd:1062:42  */
  assign n1730_o = n1729_o ? n1726_o : regaddrb_r;
  /* T80.vhd:1066:33  */
  assign n1732_o = regbusa - 16'b0000000000000001;
  /* T80.vhd:1066:51  */
  assign n1733_o = incdec_16[3]; // extract
  /* T80.vhd:1066:37  */
  assign n1734_o = n1733_o ? n1732_o : n1736_o;
  /* T80.vhd:1067:41  */
  assign n1736_o = regbusa + 16'b0000000000000001;
  /* T80.vhd:1074:28  */
  assign n1740_o = tstate == 3'b001;
  /* T80.vhd:1074:47  */
  assign n1741_o = ~save_alu_r;
  /* T80.vhd:1074:32  */
  assign n1742_o = n1741_o & n1740_o;
  /* T80.vhd:1074:70  */
  assign n1743_o = ~auto_wait_t1;
  /* T80.vhd:1074:53  */
  assign n1744_o = n1743_o & n1742_o;
  /* T80.vhd:1075:56  */
  assign n1746_o = alu_op_r != 4'b0111;
  /* T80.vhd:1075:43  */
  assign n1747_o = n1746_o & save_alu_r;
  /* T80.vhd:1074:77  */
  assign n1748_o = n1744_o | n1747_o;
  /* T80.vhd:1078:60  */
  assign n1749_o = read_to_reg_r[0]; // extract
  /* T80.vhd:1078:43  */
  assign n1750_o = ~n1749_o;
  /* T80.vhd:1079:56  */
  assign n1751_o = read_to_reg_r[0]; // extract
  /* T80.vhd:1077:25  */
  assign n1753_o = read_to_reg_r == 5'b10000;
  /* T80.vhd:1077:38  */
  assign n1755_o = read_to_reg_r == 5'b10001;
  /* T80.vhd:1077:38  */
  assign n1756_o = n1753_o | n1755_o;
  /* T80.vhd:1077:48  */
  assign n1758_o = read_to_reg_r == 5'b10010;
  /* T80.vhd:1077:48  */
  assign n1759_o = n1756_o | n1758_o;
  /* T80.vhd:1077:58  */
  assign n1761_o = read_to_reg_r == 5'b10011;
  /* T80.vhd:1077:58  */
  assign n1762_o = n1759_o | n1761_o;
  /* T80.vhd:1077:68  */
  assign n1764_o = read_to_reg_r == 5'b10100;
  /* T80.vhd:1077:68  */
  assign n1765_o = n1762_o | n1764_o;
  /* T80.vhd:1077:78  */
  assign n1767_o = read_to_reg_r == 5'b10101;
  /* T80.vhd:1077:78  */
  assign n1768_o = n1765_o | n1767_o;
  /* T80.vhd:1076:25  */
  always @*
    case (n1768_o)
      1'b1: n1770_o = n1750_o;
      default: n1770_o = 1'b0;
    endcase
  /* T80.vhd:1076:25  */
  always @*
    case (n1768_o)
      1'b1: n1772_o = n1751_o;
      default: n1772_o = 1'b0;
    endcase
  /* T80.vhd:1074:17  */
  assign n1774_o = n1748_o ? n1770_o : 1'b0;
  /* T80.vhd:1074:17  */
  assign n1777_o = n1748_o ? n1772_o : 1'b0;
  /* T80.vhd:1084:60  */
  assign n1780_o = tstate == 3'b100;
  /* T80.vhd:1084:50  */
  assign n1781_o = t_res | n1780_o;
  /* T80.vhd:1084:33  */
  assign n1782_o = n1781_o & i_mulu;
  /* T80.vhd:1084:17  */
  assign n1784_o = n1782_o ? 1'b1 : n1774_o;
  /* T80.vhd:1084:17  */
  assign n1786_o = n1782_o ? 1'b1 : n1777_o;
  /* T80.vhd:1089:49  */
  assign n1788_o = tstate == 3'b011;
  /* T80.vhd:1089:63  */
  assign n1790_o = tstate == 3'b100;
  /* T80.vhd:1089:53  */
  assign n1791_o = n1788_o | n1790_o;
  /* T80.vhd:1089:37  */
  assign n1792_o = n1791_o & exchangedh;
  /* T80.vhd:1089:17  */
  assign n1794_o = n1792_o ? 1'b1 : n1784_o;
  /* T80.vhd:1089:17  */
  assign n1796_o = n1792_o ? 1'b1 : n1786_o;
  /* T80.vhd:1094:46  */
  assign n1798_o = mcycle == 3'b010;
  /* T80.vhd:1094:35  */
  assign n1799_o = n1798_o & ldhlsp;
  /* T80.vhd:1094:55  */
  assign n1800_o = n1799_o | exchangewh;
  /* T80.vhd:1094:87  */
  assign n1802_o = tstate == 3'b100;
  /* T80.vhd:1094:76  */
  assign n1803_o = n1802_o & n1800_o;
  /* T80.vhd:1094:17  */
  assign n1805_o = n1803_o ? 1'b1 : n1794_o;
  /* T80.vhd:1094:17  */
  assign n1807_o = n1803_o ? 1'b1 : n1796_o;
  /* T80.vhd:1099:29  */
  assign n1808_o = incdec_16[2]; // extract
  /* T80.vhd:1099:52  */
  assign n1810_o = tstate == 3'b010;
  /* T80.vhd:1099:56  */
  assign n1811_o = wait_n & n1810_o;
  /* T80.vhd:1099:84  */
  assign n1813_o = mcycle != 3'b001;
  /* T80.vhd:1099:73  */
  assign n1814_o = n1813_o & n1811_o;
  /* T80.vhd:1099:105  */
  assign n1816_o = tstate == 3'b011;
  /* T80.vhd:1099:120  */
  assign n1818_o = mcycle == 3'b001;
  /* T80.vhd:1099:109  */
  assign n1819_o = n1818_o & n1816_o;
  /* T80.vhd:1099:94  */
  assign n1820_o = n1814_o | n1819_o;
  /* T80.vhd:1099:39  */
  assign n1821_o = n1820_o & n1808_o;
  /* T80.vhd:1100:39  */
  assign n1822_o = incdec_16[1:0]; // extract
  /* T80.vhd:1101:25  */
  assign n1824_o = n1822_o == 2'b00;
  /* T80.vhd:1101:35  */
  assign n1826_o = n1822_o == 2'b01;
  /* T80.vhd:1101:35  */
  assign n1827_o = n1824_o | n1826_o;
  /* T80.vhd:1101:42  */
  assign n1829_o = n1822_o == 2'b10;
  /* T80.vhd:1101:42  */
  assign n1830_o = n1827_o | n1829_o;
  /* T80.vhd:1100:25  */
  always @*
    case (n1830_o)
      1'b1: n1832_o = 1'b1;
      default: n1832_o = n1805_o;
    endcase
  /* T80.vhd:1100:25  */
  always @*
    case (n1830_o)
      1'b1: n1834_o = 1'b1;
      default: n1834_o = n1807_o;
    endcase
  /* T80.vhd:1099:17  */
  assign n1835_o = n1821_o ? n1832_o : n1805_o;
  /* T80.vhd:1099:17  */
  assign n1836_o = n1821_o ? n1834_o : n1807_o;
  /* T80.vhd:1109:58  */
  assign n1838_o = {{8{save_mux[7]}}, save_mux}; // sext
  /* T80.vhd:1109:58  */
  assign n1839_o = sp + n1838_o;
  /* T80.vhd:1119:54  */
  assign n1842_o = mulu_prod32[31:24]; // extract
  /* T80.vhd:1120:54  */
  assign n1843_o = mulu_prod32[23:16]; // extract
  /* T80.vhd:1122:51  */
  assign n1844_o = mulu_tmp[15:8]; // extract
  /* T80.vhd:1123:51  */
  assign n1845_o = mulu_tmp[7:0]; // extract
  /* T80.vhd:1118:25  */
  assign n1846_o = t_res ? n1842_o : n1844_o;
  /* T80.vhd:1118:25  */
  assign n1847_o = t_res ? n1843_o : n1845_o;
  /* T80.vhd:1117:17  */
  assign n1848_o = i_mulu ? n1846_o : save_mux;
  /* T80.vhd:1117:17  */
  assign n1849_o = i_mulu ? n1847_o : save_mux;
  /* T80.vhd:1127:44  */
  assign n1851_o = mcycle == 3'b010;
  /* T80.vhd:1127:33  */
  assign n1852_o = n1851_o & ldhlsp;
  /* T80.vhd:1127:63  */
  assign n1854_o = tstate == 3'b100;
  /* T80.vhd:1127:52  */
  assign n1855_o = n1854_o & n1852_o;
  /* T80.vhd:1128:43  */
  assign n1856_o = tmpaddr2[15:8]; // extract
  /* T80.vhd:1129:43  */
  assign n1857_o = tmpaddr2[7:0]; // extract
  /* T80.vhd:1127:17  */
  assign n1858_o = n1855_o ? n1856_o : n1848_o;
  /* T80.vhd:1127:17  */
  assign n1859_o = n1855_o ? n1857_o : n1849_o;
  /* T80.vhd:1132:48  */
  assign n1861_o = tstate == 3'b011;
  /* T80.vhd:1132:37  */
  assign n1862_o = n1861_o & exchangedh;
  /* T80.vhd:1133:42  */
  assign n1863_o = regbusb[15:8]; // extract
  /* T80.vhd:1134:42  */
  assign n1864_o = regbusb[7:0]; // extract
  /* T80.vhd:1132:17  */
  assign n1865_o = n1862_o ? n1863_o : n1858_o;
  /* T80.vhd:1132:17  */
  assign n1866_o = n1862_o ? n1864_o : n1859_o;
  /* T80.vhd:1136:48  */
  assign n1868_o = tstate == 3'b100;
  /* T80.vhd:1136:37  */
  assign n1869_o = n1868_o & exchangedh;
  /* T80.vhd:1137:44  */
  assign n1870_o = regbusa_r[15:8]; // extract
  /* T80.vhd:1138:44  */
  assign n1871_o = regbusa_r[7:0]; // extract
  /* T80.vhd:1136:17  */
  assign n1872_o = n1869_o ? n1870_o : n1865_o;
  /* T80.vhd:1136:17  */
  assign n1873_o = n1869_o ? n1871_o : n1866_o;
  /* T80.vhd:1140:48  */
  assign n1875_o = tstate == 3'b100;
  /* T80.vhd:1140:37  */
  assign n1876_o = n1875_o & exchangewh;
  /* T80.vhd:1141:37  */
  assign n1877_o = wz[15:8]; // extract
  /* T80.vhd:1142:37  */
  assign n1878_o = wz[7:0]; // extract
  /* T80.vhd:1140:17  */
  assign n1879_o = n1876_o ? n1877_o : n1872_o;
  /* T80.vhd:1140:17  */
  assign n1880_o = n1876_o ? n1878_o : n1873_o;
  /* T80.vhd:1144:29  */
  assign n1881_o = incdec_16[2]; // extract
  /* T80.vhd:1144:52  */
  assign n1883_o = tstate == 3'b010;
  /* T80.vhd:1144:67  */
  assign n1885_o = mcycle != 3'b001;
  /* T80.vhd:1144:56  */
  assign n1886_o = n1885_o & n1883_o;
  /* T80.vhd:1144:88  */
  assign n1888_o = tstate == 3'b011;
  /* T80.vhd:1144:103  */
  assign n1890_o = mcycle == 3'b001;
  /* T80.vhd:1144:92  */
  assign n1891_o = n1890_o & n1888_o;
  /* T80.vhd:1144:77  */
  assign n1892_o = n1886_o | n1891_o;
  /* T80.vhd:1144:39  */
  assign n1893_o = n1892_o & n1881_o;
  /* T80.vhd:1145:56  */
  assign n1894_o = id16[15:8]; // extract
  /* T80.vhd:1146:56  */
  assign n1895_o = id16[7:0]; // extract
  /* T80.vhd:1144:17  */
  assign n1896_o = n1893_o ? n1894_o : n1879_o;
  /* T80.vhd:1144:17  */
  assign n1897_o = n1893_o ? n1895_o : n1880_o;
  /* T80.vhd:1161:33  */
  assign regs_n1899 = regs_doah; // (signal)
  /* T80.vhd:1162:33  */
  assign regs_n1900 = regs_doal; // (signal)
  /* T80.vhd:1163:33  */
  assign regs_n1901 = regs_dobh; // (signal)
  /* T80.vhd:1164:33  */
  assign regs_n1902 = regs_dobl; // (signal)
  /* T80.vhd:1165:33  */
  assign regs_n1903 = regs_doch; // (signal)
  /* T80.vhd:1166:33  */
  assign regs_n1904 = regs_docl; // (signal)
  /* T80.vhd:1167:33  */
  assign regs_n1905 = regs_dor; // (signal)
  /* T80.vhd:1169:36  */
  assign n1906_o = dir[207:80]; // extract
  /* T80.vhd:1150:9  */
  t80_reg regs (
    .clk(clk_n),
    .cen(clken),
    .weh(regweh),
    .wel(regwel),
    .addra(regaddra),
    .addrb(regaddrb),
    .addrc(regaddrc),
    .dih(regdih),
    .dil(regdil),
    .dirset(dirset),
    .dir(n1906_o),
    .doah(regs_doah),
    .doal(regs_doal),
    .dobh(regs_dobh),
    .dobl(regs_dobl),
    .doch(regs_doch),
    .docl(regs_docl),
    .dor(regs_dor));
  /* T80.vhd:1181:33  */
  assign n1925_o = set_busb_to == 4'b0111;
  /* T80.vhd:1184:55  */
  assign n1926_o = set_busb_to[0]; // extract
  /* T80.vhd:1185:64  */
  assign n1927_o = regbusb[7:0]; // extract
  /* T80.vhd:1187:64  */
  assign n1928_o = regbusb[15:8]; // extract
  /* T80.vhd:1184:41  */
  assign n1929_o = n1926_o ? n1927_o : n1928_o;
  /* T80.vhd:1183:33  */
  assign n1931_o = set_busb_to == 4'b0000;
  /* T80.vhd:1183:45  */
  assign n1933_o = set_busb_to == 4'b0001;
  /* T80.vhd:1183:45  */
  assign n1934_o = n1931_o | n1933_o;
  /* T80.vhd:1183:54  */
  assign n1936_o = set_busb_to == 4'b0010;
  /* T80.vhd:1183:54  */
  assign n1937_o = n1934_o | n1936_o;
  /* T80.vhd:1183:63  */
  assign n1939_o = set_busb_to == 4'b0011;
  /* T80.vhd:1183:63  */
  assign n1940_o = n1937_o | n1939_o;
  /* T80.vhd:1183:72  */
  assign n1942_o = set_busb_to == 4'b0100;
  /* T80.vhd:1183:72  */
  assign n1943_o = n1940_o | n1942_o;
  /* T80.vhd:1183:81  */
  assign n1945_o = set_busb_to == 4'b0101;
  /* T80.vhd:1183:81  */
  assign n1946_o = n1943_o | n1945_o;
  /* T80.vhd:1189:33  */
  assign n1948_o = set_busb_to == 4'b0110;
  /* T80.vhd:1192:68  */
  assign n1949_o = sp[7:0]; // extract
  /* T80.vhd:1191:33  */
  assign n1951_o = set_busb_to == 4'b1000;
  /* T80.vhd:1194:68  */
  assign n1952_o = sp[15:8]; // extract
  /* T80.vhd:1193:33  */
  assign n1954_o = set_busb_to == 4'b1001;
  /* T80.vhd:1195:33  */
  assign n1956_o = set_busb_to == 4'b1010;
  /* T80.vhd:1197:33  */
  assign n1958_o = set_busb_to == 4'b1011;
  /* T80.vhd:1200:68  */
  assign n1959_o = pc[7:0]; // extract
  /* T80.vhd:1199:33  */
  assign n1961_o = set_busb_to == 4'b1100;
  /* T80.vhd:1202:68  */
  assign n1962_o = pc[15:8]; // extract
  /* T80.vhd:1201:33  */
  assign n1964_o = set_busb_to == 4'b1101;
  /* T80.vhd:1204:47  */
  assign n1966_o = ir == 8'b01110001;
  /* T80.vhd:1204:55  */
  assign n1967_o = out0 & n1966_o;
  /* T80.vhd:1204:41  */
  assign n1970_o = n1967_o ? 8'b11111111 : 8'b00000000;
  /* T80.vhd:1203:33  */
  assign n1972_o = set_busb_to == 4'b1110;
  assign n1973_o = {n1972_o, n1964_o, n1961_o, n1958_o, n1956_o, n1954_o, n1951_o, n1948_o, n1946_o, n1925_o};
  /* T80.vhd:1180:33  */
  always @*
    case (n1973_o)
      10'b1000000000: n1976_o = n1970_o;
      10'b0100000000: n1976_o = n1962_o;
      10'b0010000000: n1976_o = n1959_o;
      10'b0001000000: n1976_o = f;
      10'b0000100000: n1976_o = 8'b00000001;
      10'b0000010000: n1976_o = n1952_o;
      10'b0000001000: n1976_o = n1949_o;
      10'b0000000100: n1976_o = di_reg;
      10'b0000000010: n1976_o = n1929_o;
      10'b0000000001: n1976_o = acc;
      default: n1976_o = 8'bX;
    endcase
  /* T80.vhd:1214:33  */
  assign n1978_o = set_busa_to == 4'b0111;
  /* T80.vhd:1217:55  */
  assign n1979_o = set_busa_to[0]; // extract
  /* T80.vhd:1218:64  */
  assign n1980_o = regbusa[7:0]; // extract
  /* T80.vhd:1220:64  */
  assign n1981_o = regbusa[15:8]; // extract
  /* T80.vhd:1217:41  */
  assign n1982_o = n1979_o ? n1980_o : n1981_o;
  /* T80.vhd:1216:33  */
  assign n1984_o = set_busa_to == 4'b0000;
  /* T80.vhd:1216:45  */
  assign n1986_o = set_busa_to == 4'b0001;
  /* T80.vhd:1216:45  */
  assign n1987_o = n1984_o | n1986_o;
  /* T80.vhd:1216:54  */
  assign n1989_o = set_busa_to == 4'b0010;
  /* T80.vhd:1216:54  */
  assign n1990_o = n1987_o | n1989_o;
  /* T80.vhd:1216:63  */
  assign n1992_o = set_busa_to == 4'b0011;
  /* T80.vhd:1216:63  */
  assign n1993_o = n1990_o | n1992_o;
  /* T80.vhd:1216:72  */
  assign n1995_o = set_busa_to == 4'b0100;
  /* T80.vhd:1216:72  */
  assign n1996_o = n1993_o | n1995_o;
  /* T80.vhd:1216:81  */
  assign n1998_o = set_busa_to == 4'b0101;
  /* T80.vhd:1216:81  */
  assign n1999_o = n1996_o | n1998_o;
  /* T80.vhd:1222:33  */
  assign n2001_o = set_busa_to == 4'b0110;
  /* T80.vhd:1225:68  */
  assign n2002_o = sp[7:0]; // extract
  /* T80.vhd:1224:33  */
  assign n2004_o = set_busa_to == 4'b1000;
  /* T80.vhd:1227:68  */
  assign n2005_o = sp[15:8]; // extract
  /* T80.vhd:1226:33  */
  assign n2007_o = set_busa_to == 4'b1001;
  /* T80.vhd:1228:33  */
  assign n2009_o = set_busa_to == 4'b1010;
  assign n2010_o = {n2009_o, n2007_o, n2004_o, n2001_o, n1999_o, n1978_o};
  /* T80.vhd:1213:33  */
  always @*
    case (n2010_o)
      6'b100000: n2013_o = 8'b00000000;
      6'b010000: n2013_o = n2005_o;
      6'b001000: n2013_o = n2002_o;
      6'b000100: n2013_o = di_reg;
      6'b000010: n2013_o = n1982_o;
      6'b000001: n2013_o = acc;
      default: n2013_o = 8'bX;
    endcase
  /* T80.vhd:1233:33  */
  assign n2014_o = xybit_undoc ? di_reg : n1976_o;
  /* T80.vhd:1233:33  */
  assign n2015_o = xybit_undoc ? di_reg : n2013_o;
  /* T80.vhd:1248:28  */
  assign n2023_o = ~reset_n;
  /* T80.vhd:1251:35  */
  assign n2025_o = ~dirset;
  /* T80.vhd:1251:41  */
  assign n2026_o = cen & n2025_o;
  /* T80.vhd:1252:43  */
  assign n2028_o = mcycle == 3'b001;
  /* T80.vhd:1252:64  */
  assign n2030_o = tstate == 3'b010;
  /* T80.vhd:1252:69  */
  assign n2031_o = wait_n & n2030_o;
  /* T80.vhd:1252:97  */
  assign n2033_o = tstate == 3'b011;
  /* T80.vhd:1252:87  */
  assign n2034_o = n2031_o | n2033_o;
  /* T80.vhd:1252:51  */
  assign n2035_o = n2034_o & n2028_o;
  /* T80.vhd:1252:33  */
  assign n2038_o = n2035_o ? 1'b0 : 1'b1;
  /* T80.vhd:1264:19  */
  assign n2044_o = ~halt_ff;
  /* T80.vhd:1265:32  */
  assign n2045_o = busack & reset_n;
  /* T80.vhd:1265:20  */
  assign n2046_o = ~n2045_o;
  /* T80.vhd:1266:23  */
  assign n2047_o = ~intcycle;
  /* T80.vhd:1276:26  */
  always @*
    n2048_oldnmi_n = n2345_q; // (isignal)
  initial
    n2048_oldnmi_n = 1'bX;
  /* T80.vhd:1278:28  */
  assign n2051_o = ~reset_n;
  /* T80.vhd:1296:48  */
  assign n2053_o = dir[211]; // extract
  /* T80.vhd:1297:48  */
  assign n2054_o = dir[210]; // extract
  /* T80.vhd:1299:42  */
  assign n2055_o = ~nmi_n;
  /* T80.vhd:1299:48  */
  assign n2056_o = n2048_oldnmi_n & n2055_o;
  /* T80.vhd:1299:33  */
  assign n2058_o = n2056_o ? 1'b1 : nmi_s;
  /* T80.vhd:1305:53  */
  assign n2059_o = ~busrq_n;
  /* T80.vhd:1311:75  */
  assign n2060_o = auto_wait | iorq_i;
  /* T80.vhd:1307:41  */
  assign n2062_o = t_res ? 1'b0 : n2060_o;
  /* T80.vhd:1307:41  */
  assign n2064_o = t_res ? 1'b0 : auto_wait_t1;
  /* T80.vhd:1313:68  */
  assign n2065_o = ir[4]; // extract
  /* T80.vhd:1313:62  */
  assign n2066_o = ~n2065_o;
  /* T80.vhd:1313:80  */
  assign n2067_o = f[2]; // extract
  /* T80.vhd:1313:75  */
  assign n2068_o = ~n2067_o;
  /* T80.vhd:1313:72  */
  assign n2069_o = n2066_o | n2068_o;
  /* T80.vhd:1313:57  */
  assign n2070_o = i_bt & n2069_o;
  /* T80.vhd:1314:74  */
  assign n2071_o = ir[4]; // extract
  /* T80.vhd:1314:68  */
  assign n2072_o = ~n2071_o;
  /* T80.vhd:1314:82  */
  assign n2073_o = f[6]; // extract
  /* T80.vhd:1314:78  */
  assign n2074_o = n2072_o | n2073_o;
  /* T80.vhd:1314:99  */
  assign n2075_o = f[2]; // extract
  /* T80.vhd:1314:94  */
  assign n2076_o = ~n2075_o;
  /* T80.vhd:1314:91  */
  assign n2077_o = n2074_o | n2076_o;
  /* T80.vhd:1314:63  */
  assign n2078_o = i_bc & n2077_o;
  /* T80.vhd:1313:91  */
  assign n2079_o = n2070_o | n2078_o;
  /* T80.vhd:1315:75  */
  assign n2080_o = ir[4]; // extract
  /* T80.vhd:1315:69  */
  assign n2081_o = ~n2080_o;
  /* T80.vhd:1315:83  */
  assign n2082_o = f[6]; // extract
  /* T80.vhd:1315:79  */
  assign n2083_o = n2081_o | n2082_o;
  /* T80.vhd:1315:64  */
  assign n2084_o = i_btr & n2083_o;
  /* T80.vhd:1314:110  */
  assign n2085_o = n2079_o | n2084_o;
  /* T80.vhd:1316:51  */
  assign n2087_o = tstate == 3'b010;
  /* T80.vhd:1317:49  */
  assign n2089_o = setei ? 1'b1 : inte_ff1;
  /* T80.vhd:1316:41  */
  assign n2091_o = n2094_o ? 1'b1 : inte_ff2;
  /* T80.vhd:1321:49  */
  assign n2092_o = i_retn ? inte_ff2 : n2089_o;
  /* T80.vhd:1316:41  */
  assign n2093_o = n2087_o ? n2092_o : inte_ff1;
  /* T80.vhd:1316:41  */
  assign n2094_o = setei & n2087_o;
  /* T80.vhd:1325:51  */
  assign n2096_o = tstate == 3'b011;
  /* T80.vhd:1325:41  */
  assign n2098_o = n2101_o ? 1'b0 : n2093_o;
  /* T80.vhd:1325:41  */
  assign n2100_o = n2102_o ? 1'b0 : n2091_o;
  /* T80.vhd:1325:41  */
  assign n2101_o = setdi & n2096_o;
  /* T80.vhd:1325:41  */
  assign n2102_o = setdi & n2096_o;
  /* T80.vhd:1331:59  */
  assign n2103_o = intcycle | nmicycle;
  /* T80.vhd:1331:41  */
  assign n2105_o = n2103_o ? 1'b0 : halt_ff;
  /* T80.vhd:1334:51  */
  assign n2107_o = mcycle == 3'b001;
  /* T80.vhd:1334:70  */
  assign n2109_o = tstate == 3'b010;
  /* T80.vhd:1334:59  */
  assign n2110_o = n2109_o & n2107_o;
  /* T80.vhd:1334:74  */
  assign n2111_o = wait_n & n2110_o;
  /* T80.vhd:1334:41  */
  assign n2113_o = n2111_o ? 1'b1 : n2424_q;
  /* T80.vhd:1337:59  */
  assign n2114_o = busack & busreq_s;
  /* T80.vhd:1340:59  */
  assign n2116_o = tstate == 3'b010;
  /* T80.vhd:1340:74  */
  assign n2117_o = ~wait_n;
  /* T80.vhd:1340:63  */
  assign n2118_o = n2117_o & n2116_o;
  /* T80.vhd:1342:71  */
  assign n2120_o = 1'b1 & halt;
  /* T80.vhd:1341:49  */
  assign n2122_o = n2231_o ? 1'b1 : n2105_o;
  /* T80.vhd:1359:79  */
  assign n2124_o = ir == 8'b00110110;
  /* T80.vhd:1359:92  */
  assign n2126_o = 1'b1 & n2124_o;
  /* T80.vhd:1359:73  */
  assign n2128_o = n2126_o ? 3'b010 : mcycle;
  /* T80.vhd:1362:79  */
  assign n2130_o = mcycle == 3'b111;
  /* T80.vhd:1362:88  */
  assign n2134_o = n2130_o | 1'b0;
  /* T80.vhd:1363:121  */
  assign n2136_o = pre_xy_f_m + 3'b001;
  /* T80.vhd:1364:79  */
  assign n2137_o = mcycle == mcycles;
  /* T80.vhd:1364:90  */
  assign n2138_o = n2137_o | no_btr;
  /* T80.vhd:1364:117  */
  assign n2140_o = mcycle == 3'b010;
  /* T80.vhd:1364:125  */
  assign n2141_o = i_djnz & n2140_o;
  /* T80.vhd:1364:142  */
  assign n2142_o = incdecz & n2141_o;
  /* T80.vhd:1364:106  */
  assign n2143_o = n2138_o | n2142_o;
  /* T80.vhd:1369:99  */
  assign n2145_o = prefix == 2'b00;
  /* T80.vhd:1369:88  */
  assign n2146_o = n2145_o & nmi_s;
  /* T80.vhd:1373:103  */
  assign n2147_o = ~int_n;
  /* T80.vhd:1373:94  */
  assign n2148_o = n2147_o & inte_ff1;
  /* T80.vhd:1373:119  */
  assign n2150_o = prefix == 2'b00;
  /* T80.vhd:1373:108  */
  assign n2151_o = n2150_o & n2148_o;
  /* T80.vhd:1373:136  */
  assign n2152_o = ~setei;
  /* T80.vhd:1373:126  */
  assign n2153_o = n2152_o & n2151_o;
  /* T80.vhd:1373:73  */
  assign n2157_o = n2153_o ? 1'b0 : n2098_o;
  /* T80.vhd:1373:73  */
  assign n2159_o = n2153_o ? 1'b0 : n2100_o;
  /* T80.vhd:1373:73  */
  assign n2162_o = n2153_o ? 1'b1 : 1'b0;
  /* T80.vhd:1369:73  */
  assign n2164_o = n2146_o ? 1'b0 : n2157_o;
  /* T80.vhd:1369:73  */
  assign n2165_o = n2146_o ? n2100_o : n2159_o;
  /* T80.vhd:1364:65  */
  assign n2167_o = n2183_o ? 1'b0 : n2058_o;
  /* T80.vhd:1369:73  */
  assign n2169_o = n2146_o ? 1'b0 : n2162_o;
  /* T80.vhd:1369:73  */
  assign n2173_o = n2146_o ? 1'b1 : 1'b0;
  /* T80.vhd:1381:117  */
  assign n2176_o = mcycle + 3'b001;
  /* T80.vhd:1364:65  */
  assign n2178_o = n2143_o ? 1'b0 : n2113_o;
  /* T80.vhd:1364:65  */
  assign n2180_o = n2143_o ? 3'b001 : n2176_o;
  /* T80.vhd:1364:65  */
  assign n2181_o = n2143_o ? n2164_o : n2098_o;
  /* T80.vhd:1364:65  */
  assign n2182_o = n2143_o ? n2165_o : n2100_o;
  /* T80.vhd:1364:65  */
  assign n2183_o = n2146_o & n2143_o;
  /* T80.vhd:1364:65  */
  assign n2184_o = n2143_o ? n2169_o : intcycle;
  /* T80.vhd:1364:65  */
  assign n2185_o = n2143_o ? n2173_o : nmicycle;
  /* T80.vhd:1362:65  */
  assign n2186_o = n2134_o ? n2113_o : n2178_o;
  /* T80.vhd:1362:65  */
  assign n2187_o = n2134_o ? n2136_o : n2180_o;
  /* T80.vhd:1362:65  */
  assign n2188_o = n2134_o ? n2098_o : n2181_o;
  /* T80.vhd:1362:65  */
  assign n2189_o = n2134_o ? n2100_o : n2182_o;
  /* T80.vhd:1362:65  */
  assign n2190_o = n2134_o ? n2058_o : n2167_o;
  /* T80.vhd:1362:65  */
  assign n2191_o = n2134_o ? intcycle : n2184_o;
  /* T80.vhd:1362:65  */
  assign n2192_o = n2134_o ? nmicycle : n2185_o;
  /* T80.vhd:1356:65  */
  assign n2193_o = nextis_xy_fetch ? n2113_o : n2186_o;
  /* T80.vhd:1356:65  */
  assign n2195_o = nextis_xy_fetch ? 3'b110 : n2187_o;
  /* T80.vhd:1356:65  */
  assign n2196_o = nextis_xy_fetch ? n2098_o : n2188_o;
  /* T80.vhd:1356:65  */
  assign n2197_o = nextis_xy_fetch ? n2100_o : n2189_o;
  /* T80.vhd:1356:65  */
  assign n2198_o = nextis_xy_fetch ? n2058_o : n2190_o;
  /* T80.vhd:1356:65  */
  assign n2199_o = nextis_xy_fetch ? n2128_o : pre_xy_f_m;
  /* T80.vhd:1356:65  */
  assign n2200_o = nextis_xy_fetch ? intcycle : n2191_o;
  /* T80.vhd:1356:65  */
  assign n2201_o = nextis_xy_fetch ? nmicycle : n2192_o;
  /* T80.vhd:1345:57  */
  assign n2202_o = busreq_s ? n2113_o : n2193_o;
  /* T80.vhd:1345:57  */
  assign n2204_o = busreq_s ? tstate : 3'b001;
  /* T80.vhd:1345:57  */
  assign n2205_o = busreq_s ? mcycle : n2195_o;
  /* T80.vhd:1345:57  */
  assign n2206_o = busreq_s ? n2098_o : n2196_o;
  /* T80.vhd:1345:57  */
  assign n2207_o = busreq_s ? n2100_o : n2197_o;
  /* T80.vhd:1345:57  */
  assign n2210_o = busreq_s ? 1'b1 : 1'b0;
  /* T80.vhd:1345:57  */
  assign n2211_o = busreq_s ? n2058_o : n2198_o;
  /* T80.vhd:1345:57  */
  assign n2212_o = busreq_s ? pre_xy_f_m : n2199_o;
  /* T80.vhd:1345:57  */
  assign n2213_o = busreq_s ? intcycle : n2200_o;
  /* T80.vhd:1345:57  */
  assign n2214_o = busreq_s ? nmicycle : n2201_o;
  /* T80.vhd:1385:94  */
  assign n2215_o = ~auto_wait_t2;
  /* T80.vhd:1385:77  */
  assign n2216_o = n2215_o & auto_wait;
  /* T80.vhd:1386:77  */
  assign n2218_o = iorq_i & 1'b1;
  /* T80.vhd:1386:111  */
  assign n2219_o = ~auto_wait_t1;
  /* T80.vhd:1386:94  */
  assign n2220_o = n2219_o & n2218_o;
  /* T80.vhd:1385:101  */
  assign n2221_o = n2216_o | n2220_o;
  /* T80.vhd:1385:101  */
  assign n2222_o = ~n2221_o;
  /* T80.vhd:1387:82  */
  assign n2224_o = tstate + 3'b001;
  /* T80.vhd:1385:57  */
  assign n2225_o = n2222_o ? n2224_o : tstate;
  /* T80.vhd:1341:49  */
  assign n2226_o = t_res ? n2202_o : n2113_o;
  /* T80.vhd:1341:49  */
  assign n2227_o = t_res ? n2204_o : n2225_o;
  /* T80.vhd:1341:49  */
  assign n2228_o = t_res ? n2205_o : mcycle;
  /* T80.vhd:1341:49  */
  assign n2229_o = t_res ? n2206_o : n2098_o;
  /* T80.vhd:1341:49  */
  assign n2230_o = t_res ? n2207_o : n2100_o;
  /* T80.vhd:1341:49  */
  assign n2231_o = n2120_o & t_res;
  /* T80.vhd:1341:49  */
  assign n2233_o = t_res ? n2210_o : 1'b0;
  /* T80.vhd:1341:49  */
  assign n2234_o = t_res ? n2211_o : n2058_o;
  /* T80.vhd:1341:49  */
  assign n2235_o = t_res ? n2212_o : pre_xy_f_m;
  /* T80.vhd:1341:49  */
  assign n2236_o = t_res ? n2213_o : intcycle;
  /* T80.vhd:1341:49  */
  assign n2237_o = t_res ? n2214_o : nmicycle;
  /* T80.vhd:1340:49  */
  assign n2238_o = n2118_o ? n2113_o : n2226_o;
  /* T80.vhd:1340:49  */
  assign n2239_o = n2118_o ? tstate : n2227_o;
  /* T80.vhd:1340:49  */
  assign n2240_o = n2118_o ? mcycle : n2228_o;
  /* T80.vhd:1340:49  */
  assign n2241_o = n2118_o ? n2098_o : n2229_o;
  /* T80.vhd:1340:49  */
  assign n2242_o = n2118_o ? n2100_o : n2230_o;
  /* T80.vhd:1340:49  */
  assign n2243_o = n2118_o ? n2105_o : n2122_o;
  /* T80.vhd:1340:49  */
  assign n2245_o = n2118_o ? 1'b0 : n2233_o;
  /* T80.vhd:1340:49  */
  assign n2247_o = n2118_o ? n2058_o : n2234_o;
  /* T80.vhd:1340:49  */
  assign n2248_o = n2118_o ? pre_xy_f_m : n2235_o;
  /* T80.vhd:1340:49  */
  assign n2249_o = n2118_o ? intcycle : n2236_o;
  /* T80.vhd:1340:49  */
  assign n2250_o = n2118_o ? nmicycle : n2237_o;
  /* T80.vhd:1337:41  */
  assign n2251_o = n2114_o ? n2113_o : n2238_o;
  /* T80.vhd:1337:41  */
  assign n2252_o = n2114_o ? tstate : n2239_o;
  /* T80.vhd:1337:41  */
  assign n2253_o = n2114_o ? mcycle : n2240_o;
  /* T80.vhd:1337:41  */
  assign n2254_o = n2114_o ? n2098_o : n2241_o;
  /* T80.vhd:1337:41  */
  assign n2255_o = n2114_o ? n2100_o : n2242_o;
  /* T80.vhd:1337:41  */
  assign n2256_o = n2114_o ? n2105_o : n2243_o;
  /* T80.vhd:1337:41  */
  assign n2257_o = n2114_o ? busack : n2245_o;
  /* T80.vhd:1337:41  */
  assign n2258_o = n2114_o ? n2058_o : n2247_o;
  /* T80.vhd:1337:41  */
  assign n2259_o = n2114_o ? pre_xy_f_m : n2248_o;
  /* T80.vhd:1337:41  */
  assign n2260_o = n2114_o ? intcycle : n2249_o;
  /* T80.vhd:1337:41  */
  assign n2261_o = n2114_o ? nmicycle : n2250_o;
  /* T80.vhd:1391:51  */
  assign n2263_o = tstate == 3'b000;
  /* T80.vhd:1391:41  */
  assign n2265_o = n2263_o ? 1'b0 : n2251_o;
  /* T80.vhd:1304:33  */
  assign n2266_o = cen ? n2265_o : n2424_q;
  /* T80.vhd:1304:33  */
  assign n2267_o = cen ? n2252_o : tstate;
  /* T80.vhd:1304:33  */
  assign n2268_o = cen ? n2253_o : mcycle;
  /* T80.vhd:1304:33  */
  assign n2269_o = cen ? n2254_o : inte_ff1;
  /* T80.vhd:1304:33  */
  assign n2270_o = cen ? n2255_o : inte_ff2;
  /* T80.vhd:1304:33  */
  assign n2271_o = cen ? n2256_o : halt_ff;
  /* T80.vhd:1304:33  */
  assign n2272_o = cen ? n2059_o : busreq_s;
  /* T80.vhd:1304:33  */
  assign n2273_o = cen ? n2257_o : busack;
  /* T80.vhd:1304:33  */
  assign n2274_o = cen ? n2258_o : n2058_o;
  /* T80.vhd:1304:33  */
  assign n2275_o = cen ? n2259_o : pre_xy_f_m;
  /* T80.vhd:1304:33  */
  assign n2276_o = cen ? n2085_o : no_btr;
  /* T80.vhd:1304:33  */
  assign n2277_o = cen ? n2062_o : auto_wait_t1;
  /* T80.vhd:1304:33  */
  assign n2278_o = cen ? n2064_o : auto_wait_t2;
  /* T80.vhd:1304:33  */
  assign n2279_o = cen ? n2260_o : intcycle;
  /* T80.vhd:1304:33  */
  assign n2280_o = cen ? n2261_o : nmicycle;
  /* T80.vhd:1295:25  */
  assign n2281_o = dirset ? n2424_q : n2266_o;
  /* T80.vhd:1295:25  */
  assign n2282_o = dirset ? tstate : n2267_o;
  /* T80.vhd:1295:25  */
  assign n2283_o = dirset ? mcycle : n2268_o;
  /* T80.vhd:1295:25  */
  assign n2284_o = dirset ? n2054_o : n2269_o;
  /* T80.vhd:1295:25  */
  assign n2285_o = dirset ? n2053_o : n2270_o;
  /* T80.vhd:1295:25  */
  assign n2286_o = dirset ? halt_ff : n2271_o;
  /* T80.vhd:1295:25  */
  assign n2287_o = dirset ? busreq_s : n2272_o;
  /* T80.vhd:1295:25  */
  assign n2288_o = dirset ? busack : n2273_o;
  /* T80.vhd:1295:25  */
  assign n2289_o = dirset ? nmi_s : n2274_o;
  /* T80.vhd:1295:25  */
  assign n2290_o = dirset ? pre_xy_f_m : n2275_o;
  /* T80.vhd:1295:25  */
  assign n2291_o = dirset ? no_btr : n2276_o;
  /* T80.vhd:1295:25  */
  assign n2292_o = dirset ? auto_wait_t1 : n2277_o;
  /* T80.vhd:1295:25  */
  assign n2293_o = dirset ? auto_wait_t2 : n2278_o;
  /* T80.vhd:1295:25  */
  assign n2294_o = dirset ? intcycle : n2279_o;
  /* T80.vhd:1295:25  */
  assign n2295_o = dirset ? nmicycle : n2280_o;
  /* T80.vhd:1295:25  */
  assign n2296_o = dirset ? n2048_oldnmi_n : nmi_n;
  /* T80.vhd:1275:9  */
  assign n2343_o = ~n2051_o;
  /* T80.vhd:1294:17  */
  assign n2344_o = n2343_o ? n2296_o : n2048_oldnmi_n;
  /* T80.vhd:1294:17  */
  always @(posedge clk_n)
    n2345_q <= n2344_o;
  /* T80.vhd:1399:57  */
  assign n2348_o = mcycle == 3'b001;
  /* T80.vhd:1399:46  */
  assign n2349_o = n2348_o & intcycle;
  /* T80.vhd:1399:26  */
  assign n2350_o = n2349_o ? 1'b1 : 1'b0;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2352_q <= 8'b11111111;
    else
      n2352_q <= n1378_o;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2353_q <= 8'b11111111;
    else
      n2353_q <= n1379_o;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2354_q <= 8'b11111111;
    else
      n2354_q <= n1380_o;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2355_q <= 8'b11111111;
    else
      n2355_q <= n1381_o;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2356_q <= 8'b00000000;
    else
      n2356_q <= n1382_o;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2357_q <= 8'b00000000;
    else
      n2357_q <= n1383_o;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2358_q <= 16'b1111111111111111;
    else
      n2358_q <= n1384_o;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2359_q <= 16'b0000000000000000;
    else
      n2359_q <= n1385_o;
  /* T80.vhd:400:17  */
  assign n2360_o = {regs_n1899, regs_n1900};
  /* T80.vhd:400:17  */
  assign n2361_o = {regs_n1901, regs_n1902};
  /* T80.vhd:400:17  */
  assign n2362_o = {regs_n1903, regs_n1904};
  /* T80.vhd:1002:17  */
  assign n2363_o = clken ? n1591_o : regaddra_r;
  /* T80.vhd:1002:17  */
  always @(posedge clk_n)
    n2364_q <= n2363_o;
  /* T80.vhd:1002:17  */
  assign n2365_o = clken ? n1605_o : regaddrb_r;
  /* T80.vhd:1002:17  */
  always @(posedge clk_n)
    n2366_q <= n2365_o;
  /* T80.vhd:1002:17  */
  assign n2367_o = clken ? n1622_o : regaddrc;
  /* T80.vhd:1002:17  */
  always @(posedge clk_n)
    n2368_q <= n2367_o;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2369_q <= 1'b0;
    else
      n2369_q <= n1386_o;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2370_q <= 16'b0000000000000000;
    else
      n2370_q <= n1387_o;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2371_q <= 8'b00000000;
    else
      n2371_q <= n1388_o;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2372_q <= 2'b00;
    else
      n2372_q <= n1389_o;
  /* T80.vhd:1002:17  */
  assign n2373_o = clken ? regbusa : regbusa_r;
  /* T80.vhd:1002:17  */
  always @(posedge clk_n)
    n2374_q <= n2373_o;
  /* T80.vhd:970:17  */
  assign n2375_o = clken ? n1568_o : mulu_prod32;
  /* T80.vhd:970:17  */
  always @(posedge clk_n)
    n2376_q <= n2375_o;
  /* T80.vhd:970:17  */
  assign n2377_o = {n1552_o, n1553_o};
  /* T80.vhd:970:17  */
  assign n2378_o = n1571_o ? n1567_o : mulu_fakt1;
  /* T80.vhd:970:17  */
  always @(posedge clk_n)
    n2379_q <= n2378_o;
  /* T80.vhd:1294:17  */
  always @(posedge clk_n or posedge n2051_o)
    if (n2051_o)
      n2380_q <= 3'b000;
    else
      n2380_q <= n2282_o;
  /* T80.vhd:1294:17  */
  always @(posedge clk_n or posedge n2051_o)
    if (n2051_o)
      n2381_q <= 3'b001;
    else
      n2381_q <= n2283_o;
  /* T80.vhd:1294:17  */
  always @(posedge clk_n or posedge n2051_o)
    if (n2051_o)
      n2382_q <= 1'b0;
    else
      n2382_q <= n2284_o;
  /* T80.vhd:1294:17  */
  always @(posedge clk_n or posedge n2051_o)
    if (n2051_o)
      n2383_q <= 1'b0;
    else
      n2383_q <= n2285_o;
  /* T80.vhd:1294:17  */
  always @(posedge clk_n or posedge n2051_o)
    if (n2051_o)
      n2384_q <= 1'b0;
    else
      n2384_q <= n2286_o;
  /* T80.vhd:1275:9  */
  assign n2385_o = ~n2051_o;
  /* T80.vhd:1294:17  */
  assign n2386_o = n2385_o ? n2287_o : busreq_s;
  /* T80.vhd:1294:17  */
  always @(posedge clk_n)
    n2387_q <= n2386_o;
  initial
    n2387_q = 1'b0;
  /* T80.vhd:1275:9  */
  assign n2388_o = ~n2051_o;
  /* T80.vhd:1294:17  */
  assign n2389_o = n2388_o ? n2288_o : busack;
  /* T80.vhd:1294:17  */
  always @(posedge clk_n)
    n2390_q <= n2389_o;
  initial
    n2390_q = 1'b0;
  /* T80.vhd:1294:17  */
  always @(posedge clk_n or posedge n2051_o)
    if (n2051_o)
      n2391_q <= 1'b0;
    else
      n2391_q <= n2289_o;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2392_q <= 2'b00;
    else
      n2392_q <= n1390_o;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2393_q <= 2'b00;
    else
      n2393_q <= n1391_o;
  /* T80.vhd:1294:17  */
  always @(posedge clk_n or posedge n2051_o)
    if (n2051_o)
      n2394_q <= 3'b000;
    else
      n2394_q <= n2290_o;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2395_q <= 1'b0;
    else
      n2395_q <= n1392_o;
  /* T80.vhd:1294:17  */
  always @(posedge clk_n or posedge n2051_o)
    if (n2051_o)
      n2396_q <= 1'b0;
    else
      n2396_q <= n2291_o;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2397_q <= 1'b0;
    else
      n2397_q <= n1393_o;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2398_q <= 1'b0;
    else
      n2398_q <= n1394_o;
  /* T80.vhd:1294:17  */
  always @(posedge clk_n or posedge n2051_o)
    if (n2051_o)
      n2399_q <= 1'b0;
    else
      n2399_q <= n2292_o;
  /* T80.vhd:1294:17  */
  always @(posedge clk_n or posedge n2051_o)
    if (n2051_o)
      n2400_q <= 1'b0;
    else
      n2400_q <= n2293_o;
  /* T80.vhd:1002:17  */
  assign n2401_o = clken ? n1645_o : incdecz;
  /* T80.vhd:1002:17  */
  always @(posedge clk_n)
    n2402_q <= n2401_o;
  /* T80.vhd:393:9  */
  assign n2403_o = ~n450_o;
  /* T80.vhd:438:17  */
  assign n2404_o = n2403_o ? n1395_o : last_b;
  /* T80.vhd:438:17  */
  always @(posedge clk_n)
    n2405_q <= n2404_o;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2406_q <= 1'b0;
    else
      n2406_q <= n1399_o;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2407_q <= 1'b0;
    else
      n2407_q <= n1397_o;
  /* T80.vhd:400:17  */
  assign n2408_o = {n1503_o, n2406_q, n1498_o, n2407_q, n1493_o};
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2409_q <= 8'b11111111;
    else
      n2409_q <= n1400_o;
  /* T80.vhd:1178:17  */
  assign n2410_o = clken ? n2014_o : busb;
  /* T80.vhd:1178:17  */
  always @(posedge clk_n)
    n2411_q <= n2410_o;
  /* T80.vhd:1178:17  */
  assign n2412_o = clken ? n2015_o : busa;
  /* T80.vhd:1178:17  */
  always @(posedge clk_n)
    n2413_q <= n2412_o;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2414_q <= 5'b00000;
    else
      n2414_q <= n1401_o;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2415_q <= 1'b0;
    else
      n2415_q <= n1402_o;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2416_q <= 1'b0;
    else
      n2416_q <= n1403_o;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2417_q <= 4'b0000;
    else
      n2417_q <= n1404_o;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2418_q <= 1'b0;
    else
      n2418_q <= n1405_o;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2419_q <= 1'b0;
    else
      n2419_q <= n1406_o;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2420_q <= 3'b000;
    else
      n2420_q <= n1407_o;
  /* T80.vhd:1294:17  */
  always @(posedge clk_n or posedge n2051_o)
    if (n2051_o)
      n2421_q <= 1'b0;
    else
      n2421_q <= n2294_o;
  /* T80.vhd:1294:17  */
  always @(posedge clk_n or posedge n2051_o)
    if (n2051_o)
      n2422_q <= 1'b0;
    else
      n2422_q <= n2295_o;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2423_q <= 1'b0;
    else
      n2423_q <= n1408_o;
  /* T80.vhd:1294:17  */
  always @(posedge clk_n or posedge n2051_o)
    if (n2051_o)
      n2424_q <= 1'b1;
    else
      n2424_q <= n2281_o;
  /* T80.vhd:1250:17  */
  assign n2425_o = n2026_o ? n2038_o : n2426_q;
  /* T80.vhd:1250:17  */
  always @(posedge clk_n or posedge n2023_o)
    if (n2023_o)
      n2426_q <= 1'b1;
    else
      n2426_q <= n2425_o;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2427_q <= 16'b0000000000000000;
    else
      n2427_q <= n1376_o;
  /* T80.vhd:438:17  */
  always @(posedge clk_n or posedge n450_o)
    if (n450_o)
      n2428_q <= 8'b00000000;
    else
      n2428_q <= n1377_o;
endmodule

module T80pa
  (input  RESET_n,
   input  CLK,
   input  CEN_p,
   input  CEN_n,
   input  WAIT_n,
   input  INT_n,
   input  NMI_n,
   input  BUSRQ_n,
   input  OUT0,
   input  [7:0] DI,
   input  R800_mode,
   input  DIRSet,
   input  [211:0] DIR,
   output M1_n,
   output MREQ_n,
   output IORQ_n,
   output RD_n,
   output WR_n,
   output RFSH_n,
   output HALT_n,
   output BUSAK_n,
   output [15:0] A,
   output [7:0] DO,
   output [211:0] REG);
  wire intcycle_n;
  wire [1:0] intcycled_n;
  wire iorq;
  wire noread;
  wire write;
  wire busak;
  wire [7:0] di_reg;
  wire [2:0] mcycle;
  wire [2:0] tstate;
  wire cen_pol;
  wire cen;
  wire n11_o;
  wire n12_o;
  localparam n13_o = 1'b1;
  wire u0_n14;
  wire u0_n15;
  wire u0_n16;
  wire u0_n17;
  wire u0_n18;
  wire u0_n19;
  wire u0_n20;
  wire [15:0] u0_n21;
  wire [7:0] u0_n22;
  wire [2:0] u0_n23;
  wire [2:0] u0_n24;
  wire u0_n25;
  wire [211:0] u0_n28;
  wire u0_m1_n;
  wire u0_iorq;
  wire u0_noread;
  wire u0_write;
  wire u0_rfsh_n;
  wire u0_halt_n;
  wire u0_busak_n;
  wire [15:0] u0_a;
  wire [7:0] u0_do;
  wire [2:0] u0_mc;
  wire [2:0] u0_ts;
  wire u0_intcycle_n;
  wire u0_inte;
  wire u0_stop;
  wire n60_o;
  wire n61_o;
  wire n62_o;
  wire n64_o;
  wire n66_o;
  wire n68_o;
  wire n70_o;
  wire n72_o;
  wire n74_o;
  wire n75_o;
  wire n76_o;
  wire n78_o;
  wire n79_o;
  wire n80_o;
  wire n81_o;
  wire n82_o;
  wire n83_o;
  wire n84_o;
  wire n85_o;
  wire n87_o;
  wire n88_o;
  wire n90_o;
  wire n92_o;
  wire n93_o;
  wire [7:0] n94_o;
  wire n96_o;
  wire n98_o;
  wire n99_o;
  wire [1:0] n100_o;
  wire n101_o;
  wire n102_o;
  wire n103_o;
  wire n104_o;
  wire n105_o;
  wire n106_o;
  wire [1:0] n107_o;
  wire n109_o;
  wire n111_o;
  wire n113_o;
  wire [1:0] n115_o;
  wire n117_o;
  wire n119_o;
  wire n120_o;
  wire n121_o;
  wire n122_o;
  wire n124_o;
  wire n126_o;
  wire n127_o;
  wire n128_o;
  wire n129_o;
  wire n131_o;
  wire n132_o;
  wire n133_o;
  wire n135_o;
  wire n137_o;
  wire n139_o;
  wire n141_o;
  wire n143_o;
  wire n144_o;
  wire n145_o;
  wire n146_o;
  wire n147_o;
  wire [1:0] n148_o;
  wire n149_o;
  wire n150_o;
  wire n151_o;
  wire n152_o;
  wire n153_o;
  wire n154_o;
  wire n155_o;
  wire n156_o;
  wire n157_o;
  wire n158_o;
  wire n159_o;
  wire [1:0] n160_o;
  wire [7:0] n161_o;
  wire n163_o;
  wire n165_o;
  wire n167_o;
  wire n169_o;
  wire n171_o;
  wire [1:0] n172_o;
  wire [7:0] n174_o;
  wire n176_o;
  reg [1:0] n185_q;
  reg [7:0] n186_q;
  reg n187_q;
  reg n188_q;
  reg n189_q;
  reg n190_q;
  reg n191_q;
  wire [211:0] n8532_o;
  assign M1_n = u0_n14; //(module output)
  assign MREQ_n = n188_q; //(module output)
  assign IORQ_n = n189_q; //(module output)
  assign RD_n = n190_q; //(module output)
  assign WR_n = n191_q; //(module output)
  assign RFSH_n = u0_n18; //(module output)
  assign HALT_n = u0_n19; //(module output)
  assign BUSAK_n = busak; //(module output)
  assign A = u0_n21; //(module output)
  assign DO = u0_n22; //(module output)
  assign REG = u0_n28; //(module output)
  /* T80pa.vhd:95:12  */
  assign intcycle_n = u0_n25; // (signal)
  /* T80pa.vhd:96:12  */
  assign intcycled_n = n185_q; // (signal)
  /* T80pa.vhd:97:12  */
  assign iorq = u0_n15; // (signal)
  /* T80pa.vhd:98:12  */
  assign noread = u0_n16; // (signal)
  /* T80pa.vhd:99:12  */
  assign write = u0_n17; // (signal)
  /* T80pa.vhd:100:12  */
  assign busak = u0_n20; // (signal)
  /* T80pa.vhd:101:12  */
  assign di_reg = n186_q; // (signal)
  /* T80pa.vhd:102:12  */
  assign mcycle = u0_n23; // (signal)
  /* T80pa.vhd:103:12  */
  assign tstate = u0_n24; // (signal)
  /* T80pa.vhd:104:12  */
  assign cen_pol = n187_q; // (signal)
  /* T80pa.vhd:105:12  */
  assign cen = n12_o; // (signal)
  /* T80pa.vhd:108:22  */
  assign n11_o = ~cen_pol;
  /* T80pa.vhd:108:18  */
  assign n12_o = CEN_p & n11_o;
  /* T80pa.vhd:118:24  */
  assign u0_n14 = u0_m1_n; // (signal)
  /* T80pa.vhd:119:24  */
  assign u0_n15 = u0_iorq; // (signal)
  /* T80pa.vhd:120:24  */
  assign u0_n16 = u0_noread; // (signal)
  /* T80pa.vhd:121:24  */
  assign u0_n17 = u0_write; // (signal)
  /* T80pa.vhd:122:24  */
  assign u0_n18 = u0_rfsh_n; // (signal)
  /* T80pa.vhd:123:24  */
  assign u0_n19 = u0_halt_n; // (signal)
  /* T80pa.vhd:129:24  */
  assign u0_n20 = u0_busak_n; // (signal)
  /* T80pa.vhd:131:24  */
  assign u0_n21 = u0_a; // (signal)
  /* T80pa.vhd:134:24  */
  assign u0_n22 = u0_do; // (signal)
  /* T80pa.vhd:136:24  */
  assign u0_n23 = u0_mc; // (signal)
  /* T80pa.vhd:137:24  */
  assign u0_n24 = u0_ts; // (signal)
  /* T80pa.vhd:140:27  */
  assign u0_n25 = u0_intcycle_n; // (signal)
  /* T80pa.vhd:135:24  */
  assign u0_n28 = n8532_o; // (signal)
  /* T80pa.vhd:111:5  */
  t80_0_1_0_1_2_3_4_5_6_7 u0 (
    .reset_n(RESET_n),
    .clk_n(CLK),
    .cen(cen),
    .wait_n(n13_o),
    .int_n(INT_n),
    .nmi_n(NMI_n),
    .busrq_n(BUSRQ_n),
    .dinst(DI),
    .di(di_reg),
    .r800_mode(R800_mode),
    .out0(OUT0),
    .dirset(DIRSet),
    .dir(DIR),
    .m1_n(u0_m1_n),
    .iorq(u0_iorq),
    .noread(u0_noread),
    .write(u0_write),
    .rfsh_n(u0_rfsh_n),
    .halt_n(u0_halt_n),
    .busak_n(u0_busak_n),
    .a(u0_a),
    .do_(u0_do),
    .mc(u0_mc),
    .ts(u0_ts),
    .intcycle_n(u0_intcycle_n),
    .inte(),
    .stop(),
    .\reg (n8532_o));
  /* T80pa.vhd:148:24  */
  assign n60_o = ~RESET_n;
  /* T80pa.vhd:155:43  */
  assign n61_o = ~cen_pol;
  /* T80pa.vhd:155:31  */
  assign n62_o = n61_o & CEN_p;
  /* T80pa.vhd:157:27  */
  assign n64_o = mcycle == 3'b001;
  /* T80pa.vhd:158:31  */
  assign n66_o = tstate == 3'b010;
  /* T80pa.vhd:157:17  */
  assign n68_o = n81_o ? 1'b1 : n188_q;
  /* T80pa.vhd:158:21  */
  assign n70_o = n66_o ? 1'b1 : n189_q;
  /* T80pa.vhd:158:21  */
  assign n72_o = n66_o ? 1'b1 : n190_q;
  /* T80pa.vhd:164:31  */
  assign n74_o = tstate == 3'b001;
  /* T80pa.vhd:164:39  */
  assign n75_o = iorq & n74_o;
  /* T80pa.vhd:165:35  */
  assign n76_o = ~write;
  /* T80pa.vhd:164:21  */
  assign n78_o = n75_o ? 1'b0 : n189_q;
  /* T80pa.vhd:164:21  */
  assign n79_o = n75_o ? write : n190_q;
  /* T80pa.vhd:164:21  */
  assign n80_o = n75_o ? n76_o : n191_q;
  /* T80pa.vhd:157:17  */
  assign n81_o = n66_o & n64_o;
  /* T80pa.vhd:157:17  */
  assign n82_o = n64_o ? n70_o : n78_o;
  /* T80pa.vhd:157:17  */
  assign n83_o = n64_o ? n72_o : n79_o;
  /* T80pa.vhd:157:17  */
  assign n84_o = n64_o ? n191_q : n80_o;
  /* T80pa.vhd:170:31  */
  assign n85_o = cen_pol & CEN_n;
  /* T80pa.vhd:171:27  */
  assign n87_o = tstate == 3'b010;
  /* T80pa.vhd:172:32  */
  assign n88_o = ~WAIT_n;
  /* T80pa.vhd:171:17  */
  assign n90_o = n87_o ? n88_o : 1'b0;
  /* T80pa.vhd:176:27  */
  assign n92_o = tstate == 3'b011;
  /* T80pa.vhd:176:35  */
  assign n93_o = busak & n92_o;
  /* T80pa.vhd:170:13  */
  assign n94_o = n154_o ? DI : di_reg;
  /* T80pa.vhd:179:27  */
  assign n96_o = mcycle == 3'b001;
  /* T80pa.vhd:180:31  */
  assign n98_o = tstate == 3'b001;
  /* T80pa.vhd:181:51  */
  assign n99_o = intcycled_n[0]; // extract
  /* T80pa.vhd:181:55  */
  assign n100_o = {n99_o, intcycle_n};
  /* T80pa.vhd:182:35  */
  assign n101_o = ~intcycle_n;
  /* T80pa.vhd:183:35  */
  assign n102_o = ~intcycle_n;
  /* T80pa.vhd:184:46  */
  assign n103_o = intcycled_n[1]; // extract
  /* T80pa.vhd:180:21  */
  assign n104_o = n98_o ? n102_o : n188_q;
  /* T80pa.vhd:180:21  */
  assign n105_o = n98_o ? n103_o : n189_q;
  /* T80pa.vhd:180:21  */
  assign n106_o = n98_o ? n101_o : n190_q;
  /* T80pa.vhd:180:21  */
  assign n107_o = n98_o ? n100_o : intcycled_n;
  /* T80pa.vhd:186:31  */
  assign n109_o = tstate == 3'b011;
  /* T80pa.vhd:186:21  */
  assign n111_o = n109_o ? 1'b0 : n104_o;
  /* T80pa.vhd:186:21  */
  assign n113_o = n109_o ? 1'b1 : n106_o;
  /* T80pa.vhd:186:21  */
  assign n115_o = n109_o ? 2'b11 : n107_o;
  /* T80pa.vhd:191:31  */
  assign n117_o = tstate == 3'b100;
  /* T80pa.vhd:191:21  */
  assign n119_o = n117_o ? 1'b1 : n111_o;
  /* T80pa.vhd:195:31  */
  assign n120_o = ~noread;
  /* T80pa.vhd:195:46  */
  assign n121_o = ~iorq;
  /* T80pa.vhd:195:37  */
  assign n122_o = n121_o & n120_o;
  /* T80pa.vhd:196:35  */
  assign n124_o = tstate == 3'b001;
  /* T80pa.vhd:195:21  */
  assign n126_o = n128_o ? 1'b0 : n188_q;
  /* T80pa.vhd:195:21  */
  assign n127_o = n129_o ? write : n190_q;
  /* T80pa.vhd:195:21  */
  assign n128_o = n124_o & n122_o;
  /* T80pa.vhd:195:21  */
  assign n129_o = n124_o & n122_o;
  /* T80pa.vhd:201:31  */
  assign n131_o = tstate == 3'b010;
  /* T80pa.vhd:202:35  */
  assign n132_o = ~write;
  /* T80pa.vhd:201:21  */
  assign n133_o = n131_o ? n132_o : n191_q;
  /* T80pa.vhd:204:31  */
  assign n135_o = tstate == 3'b011;
  /* T80pa.vhd:204:21  */
  assign n137_o = n135_o ? 1'b1 : n126_o;
  /* T80pa.vhd:204:21  */
  assign n139_o = n135_o ? 1'b1 : n189_q;
  /* T80pa.vhd:204:21  */
  assign n141_o = n135_o ? 1'b1 : n127_o;
  /* T80pa.vhd:204:21  */
  assign n143_o = n135_o ? 1'b1 : n133_o;
  /* T80pa.vhd:179:17  */
  assign n144_o = n96_o ? n119_o : n137_o;
  /* T80pa.vhd:179:17  */
  assign n145_o = n96_o ? n105_o : n139_o;
  /* T80pa.vhd:179:17  */
  assign n146_o = n96_o ? n113_o : n141_o;
  /* T80pa.vhd:179:17  */
  assign n147_o = n96_o ? n191_q : n143_o;
  /* T80pa.vhd:170:13  */
  assign n148_o = n153_o ? n115_o : intcycled_n;
  /* T80pa.vhd:170:13  */
  assign n149_o = n85_o ? n144_o : n188_q;
  /* T80pa.vhd:170:13  */
  assign n150_o = n85_o ? n145_o : n189_q;
  /* T80pa.vhd:170:13  */
  assign n151_o = n85_o ? n146_o : n190_q;
  /* T80pa.vhd:170:13  */
  assign n152_o = n85_o ? n147_o : n191_q;
  /* T80pa.vhd:170:13  */
  assign n153_o = n96_o & n85_o;
  /* T80pa.vhd:170:13  */
  assign n154_o = n93_o & n85_o;
  /* T80pa.vhd:170:13  */
  assign n155_o = n85_o ? n90_o : cen_pol;
  /* T80pa.vhd:155:13  */
  assign n156_o = n62_o ? n68_o : n149_o;
  /* T80pa.vhd:155:13  */
  assign n157_o = n62_o ? n82_o : n150_o;
  /* T80pa.vhd:155:13  */
  assign n158_o = n62_o ? n83_o : n151_o;
  /* T80pa.vhd:155:13  */
  assign n159_o = n62_o ? n84_o : n152_o;
  /* T80pa.vhd:155:13  */
  assign n160_o = n62_o ? intcycled_n : n148_o;
  /* T80pa.vhd:155:13  */
  assign n161_o = n62_o ? di_reg : n94_o;
  /* T80pa.vhd:155:13  */
  assign n163_o = n62_o ? 1'b1 : n155_o;
  /* T80pa.vhd:148:13  */
  assign n165_o = n60_o ? 1'b1 : n156_o;
  /* T80pa.vhd:148:13  */
  assign n167_o = n60_o ? 1'b1 : n157_o;
  /* T80pa.vhd:148:13  */
  assign n169_o = n60_o ? 1'b1 : n158_o;
  /* T80pa.vhd:148:13  */
  assign n171_o = n60_o ? 1'b1 : n159_o;
  /* T80pa.vhd:148:13  */
  assign n172_o = n60_o ? intcycled_n : n160_o;
  /* T80pa.vhd:148:13  */
  assign n174_o = n60_o ? 8'b00000000 : n161_o;
  /* T80pa.vhd:148:13  */
  assign n176_o = n60_o ? 1'b0 : n163_o;
  /* T80pa.vhd:147:9  */
  always @(posedge CLK)
    n185_q <= n172_o;
  /* T80pa.vhd:147:9  */
  always @(posedge CLK)
    n186_q <= n174_o;
  /* T80pa.vhd:147:9  */
  always @(posedge CLK)
    n187_q <= n176_o;
  /* T80pa.vhd:147:9  */
  always @(posedge CLK)
    n188_q <= n165_o;
  /* T80pa.vhd:147:9  */
  always @(posedge CLK)
    n189_q <= n167_o;
  /* T80pa.vhd:147:9  */
  always @(posedge CLK)
    n190_q <= n169_o;
  /* T80pa.vhd:147:9  */
  always @(posedge CLK)
    n191_q <= n171_o;
endmodule

