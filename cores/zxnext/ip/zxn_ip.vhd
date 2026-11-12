-- T116 D1: the memories of the ZX Spectrum Next that the original keeps outside its VHDL (five Xilinx IP cores, ".xcix" in
-- synth-zxnext/ipcore_dir, not part of the source tree) or writes in a style the converter cannot read (rtl/ram/dpram2.vhd opens a
-- file in VHDL-87 syntax, rtl/ram/tdpram.vhd shares a non-protected variable between two processes), written again here as plain
-- behavioural VHDL. Same entity names, same ports, same read timing as the originals; ours, GPL v3 or later as the rest of the core.
--   sdpbram_16k_8  16384 x 8, simple dual port, registered read (sprite pattern RAM)           -> block RAM
--   sdpram_128_8   128 x 8, asynchronous read (sprite attributes, x5)                           -> distributed RAM
--   sdpram_64_9    64 x 9 (UART transmit FIFOs, x2)
--   sdpram_16_9    16 x 9 (tilemap)
--   spram_320_9    320 x 9, one address for both ports (sprite line buffers, x2)
--   dpram2         port A writes and reads, port B reads, registered read on both, read before write on A (bank 5/7, palettes, Copper)
--   tdpram         2 x 1024 x 9 of the UART receive FIFO; both ports are on one clock in the only instance (uart.vhd), so one process
-- tools/zxnext_gen.py converts them together with rtl/ to Verilog; the lint stage keeps the converted result on its hash.
library ieee; use ieee.std_logic_1164.all; use ieee.numeric_std.all;

entity sdpram_128_8 is port (DPRA: in std_logic_vector(6 downto 0); CLK: in std_logic; WE: in std_logic;
  DPO: out std_logic_vector(7 downto 0); A: in std_logic_vector(6 downto 0); D: in std_logic_vector(7 downto 0)); end;
architecture m of sdpram_128_8 is type t is array (0 to 127) of std_logic_vector(7 downto 0); signal r: t := (others => (others => '0'));
begin
  process(CLK) begin if rising_edge(CLK) then if WE='1' then r(to_integer(unsigned(A))) <= D; end if; end if; end process;
  DPO <= r(to_integer(unsigned(DPRA)));
end;

library ieee; use ieee.std_logic_1164.all; use ieee.numeric_std.all;
entity sdpram_64_9 is port (DPRA: in std_logic_vector(5 downto 0); CLK: in std_logic; WE: in std_logic;
  DPO: out std_logic_vector(8 downto 0); A: in std_logic_vector(5 downto 0); D: in std_logic_vector(8 downto 0)); end;
architecture m of sdpram_64_9 is type t is array (0 to 63) of std_logic_vector(8 downto 0); signal r: t := (others => (others => '0'));
begin
  process(CLK) begin if rising_edge(CLK) then if WE='1' then r(to_integer(unsigned(A))) <= D; end if; end if; end process;
  DPO <= r(to_integer(unsigned(DPRA)));
end;

library ieee; use ieee.std_logic_1164.all; use ieee.numeric_std.all;
entity sdpram_16_9 is port (DPRA: in std_logic_vector(3 downto 0); CLK: in std_logic; WE: in std_logic;
  DPO: out std_logic_vector(8 downto 0); A: in std_logic_vector(3 downto 0); D: in std_logic_vector(8 downto 0)); end;
architecture m of sdpram_16_9 is type t is array (0 to 15) of std_logic_vector(8 downto 0); signal r: t := (others => (others => '0'));
begin
  process(CLK) begin if rising_edge(CLK) then if WE='1' then r(to_integer(unsigned(A))) <= D; end if; end if; end process;
  DPO <= r(to_integer(unsigned(DPRA)));
end;

library ieee; use ieee.std_logic_1164.all; use ieee.numeric_std.all;
entity spram_320_9 is port (CLK: in std_logic; WE: in std_logic; SPO: out std_logic_vector(8 downto 0);
  A: in std_logic_vector(8 downto 0); D: in std_logic_vector(8 downto 0)); end;
architecture m of spram_320_9 is type t is array (0 to 319) of std_logic_vector(8 downto 0); signal r: t := (others => (others => '0'));
begin
  process(CLK) begin if rising_edge(CLK) then if WE='1' then r(to_integer(unsigned(A))) <= D; end if; end if; end process;
  SPO <= r(to_integer(unsigned(A)));
end;

library ieee; use ieee.std_logic_1164.all; use ieee.numeric_std.all;
entity sdpbram_16k_8 is port (WEA: in std_logic_vector(0 downto 0); ADDRA: in std_logic_vector(13 downto 0); DINA: in std_logic_vector(7 downto 0); CLKA: in std_logic;
  ENB: in std_logic; ADDRB: in std_logic_vector(13 downto 0); DOUTB: out std_logic_vector(7 downto 0); CLKB: in std_logic); end;
architecture m of sdpbram_16k_8 is type t is array (0 to 16383) of std_logic_vector(7 downto 0); signal r: t := (others => (others => '0'));
begin
  process(CLKA) begin if rising_edge(CLKA) then if WEA(0)='1' then r(to_integer(unsigned(ADDRA))) <= DINA; end if; end if; end process;
  process(CLKB) begin if rising_edge(CLKB) then if ENB='1' then DOUTB <= r(to_integer(unsigned(ADDRB))); end if; end if; end process;
end;

library ieee; use ieee.std_logic_1164.all; use ieee.numeric_std.all;
entity dpram2 is
  generic (addr_width_g : integer := 8; data_width_g : integer := 8; init_file_g : string := "init/none.bin.txt");
  port (clk_a_i : in std_logic; we_i : in std_logic; addr_a_i : in std_logic_vector(addr_width_g-1 downto 0);
        data_a_i : in std_logic_vector(data_width_g-1 downto 0); data_a_o : out std_logic_vector(data_width_g-1 downto 0);
        clk_b_i : in std_logic; addr_b_i : in std_logic_vector(addr_width_g-1 downto 0); data_b_o : out std_logic_vector(data_width_g-1 downto 0));
end dpram2;
architecture m of dpram2 is
  type ram_t is array (0 to 2**addr_width_g-1) of std_logic_vector(data_width_g-1 downto 0);
  signal ram_q : ram_t := (others => (others => '0'));
begin
  process (clk_a_i) begin
    if rising_edge(clk_a_i) then
      if we_i = '1' then ram_q(to_integer(unsigned(addr_a_i))) <= data_a_i; end if;
      data_a_o <= ram_q(to_integer(unsigned(addr_a_i)));
    end if;
  end process;
  process (clk_b_i) begin
    if rising_edge(clk_b_i) then data_b_o <= ram_q(to_integer(unsigned(addr_b_i))); end if;
  end process;
end m;

library ieee; use ieee.std_logic_1164.all; use ieee.numeric_std.all;
entity tdpram is
  generic (addr_width_g : integer := 8; data_width_g : integer := 8);
  port (clk_a_i : in std_logic; we_a_i : in std_logic; addr_a_i : in std_logic_vector(addr_width_g-1 downto 0);
        data_a_i : in std_logic_vector(data_width_g-1 downto 0); data_a_o : out std_logic_vector(data_width_g-1 downto 0);
        clk_b_i : in std_logic; we_b_i : in std_logic; addr_b_i : in std_logic_vector(addr_width_g-1 downto 0);
        data_b_i : in std_logic_vector(data_width_g-1 downto 0); data_b_o : out std_logic_vector(data_width_g-1 downto 0));
end entity;
architecture m of tdpram is
  type ram_t is array (0 to 2**addr_width_g-1) of std_logic_vector(data_width_g-1 downto 0);
  signal ram_q : ram_t := (others => (others => '0'));
begin
  -- clk_b_i is clk_a_i in the one instance; both ports read before they write (their addresses differ in bit 9, so no collision)
  process (clk_a_i) begin
    if rising_edge(clk_a_i) then
      data_a_o <= ram_q(to_integer(unsigned(addr_a_i)));
      data_b_o <= ram_q(to_integer(unsigned(addr_b_i)));
      if we_a_i = '1' then ram_q(to_integer(unsigned(addr_a_i))) <= data_a_i; end if;
      if we_b_i = '1' then ram_q(to_integer(unsigned(addr_b_i))) <= data_b_i; end if;
    end if;
  end process;
end m;
