-- T142: the memories and the one stub that SNES_MiSTer keeps outside the files vendored in ../rtl, written again as plain behavioural VHDL.
-- The originals are rtl/bram.vhd and rtl/mlab.vhd, Altera altsyncram / altdpram wrappers ("subject to the terms and conditions of the Altera
-- Program License Subscription Agreement": tool output for Altera devices, not vendored), and rtl/cheatcodes.sv (Game Genie, out, T142 section 4).
-- Same entity names, same generics, same ports, same read timing as the originals (the read is of the address of the clock edge; the written
-- word is read back as written - Altera "NEW_DATA_NO_NBE_READ" - except in dpram_dif, below); ours, GPL v3 as the rest of the core. What the machine instantiates (T142 build):
--   dpram     PPU CGRAM (8,15), SPR_BUF (8,9), DSP REGRAM (7,8), BRR_BUF (7,16), DSP-n data RAM (11,8)
--   dpram_dif PPU OAM (8,16 | 7,32): port A is 256 words of 16 bit, port B the same memory as 128 words of 32 bit
--   spram     PPU HOAM (5,8)
--   spram_sz  the DSP-n program and data ROM (only when USE_DSPn = '1'; the .mif of the original are chip dumps and never here, so they stay empty)
--   mlab      PPU RANGE_TBL (5,7): asynchronous read
--   CODES     Game Genie: no code is ever available, the data passes
-- Every memory powers up as zeros (the original's "power_up_uninitialized => FALSE"). The enable inputs of the originals are not used by the
-- machine and are not modelled. tools/snes_gen.py converts these together with rtl/ to Verilog.
library ieee; use ieee.std_logic_1164.all; use ieee.numeric_std.all;

entity spram_sz is
  generic (addr_width : integer := 8; data_width : integer := 8; numwords : integer := 2**8; mem_init_file : string := " "; mem_name : string := "MEM");
  port (clock : in std_logic; address : in std_logic_vector(addr_width-1 downto 0); data : in std_logic_vector(data_width-1 downto 0) := (others => '0');
        enable : in std_logic := '1'; wren : in std_logic := '0'; q : out std_logic_vector(data_width-1 downto 0); cs : in std_logic := '1');
end entity;
architecture syn of spram_sz is
  type ram_t is array (0 to numwords-1) of std_logic_vector(data_width-1 downto 0);
  signal ram : ram_t := (others => (others => '0'));
  signal q0 : std_logic_vector(data_width-1 downto 0) := (others => '0');
begin
  process (clock) begin
    if rising_edge(clock) then
      if wren = '1' and cs = '1' then
        ram(to_integer(unsigned(address))) <= data;
        q0 <= data;
      else
        q0 <= ram(to_integer(unsigned(address)));
      end if;
    end if;
  end process;
  q <= q0 when cs = '1' else (others => '1');
end architecture;

library ieee; use ieee.std_logic_1164.all;
entity spram is
  generic (addr_width : integer := 8; data_width : integer := 8; mem_init_file : string := " "; mem_name : string := "MEM");
  port (clock : in std_logic; address : in std_logic_vector(addr_width-1 downto 0); data : in std_logic_vector(data_width-1 downto 0) := (others => '0');
        enable : in std_logic := '1'; wren : in std_logic := '0'; q : out std_logic_vector(data_width-1 downto 0); cs : in std_logic := '1');
end entity;
architecture syn of spram is
begin
  ram : entity work.spram_sz generic map(addr_width, data_width, 2**addr_width, mem_init_file, mem_name)
    port map(clock, address, data, enable, wren, q, cs);
end architecture;

-- port A 2n words of w bits, port B n words of 2w bits, the same memory; both may write. The read gives the word as it was before the write of the same clock
-- (the PPU's own OAM_DATA multiplexer returns the written word when it needs it)
library ieee; use ieee.std_logic_1164.all; use ieee.numeric_std.all;
entity dpram_dif is
  generic (addr_width_a : integer := 8; data_width_a : integer := 8; addr_width_b : integer := 8; data_width_b : integer := 8; mem_init_file : string := " ");
  port (clock : in std_logic;
        address_a : in std_logic_vector(addr_width_a-1 downto 0); data_a : in std_logic_vector(data_width_a-1 downto 0) := (others => '0');
        enable_a : in std_logic := '1'; wren_a : in std_logic := '0'; q_a : out std_logic_vector(data_width_a-1 downto 0); cs_a : in std_logic := '1';
        address_b : in std_logic_vector(addr_width_b-1 downto 0) := (others => '0'); data_b : in std_logic_vector(data_width_b-1 downto 0) := (others => '0');
        enable_b : in std_logic := '1'; wren_b : in std_logic := '0'; q_b : out std_logic_vector(data_width_b-1 downto 0); cs_b : in std_logic := '1');
end entity;
architecture syn of dpram_dif is
  -- the one case the machine has beside the equal ports is the PPU's OAM: port B twice as wide as port A (T142 build); anything else is refused at elaboration
  constant LANES : integer := data_width_b / data_width_a;
  type word_t is array (0 to 2**addr_width_b-1) of std_logic_vector(data_width_a-1 downto 0);
  signal l0, l1 : word_t := (others => (others => '0'));   -- lane 0 = the low half of a port B word = the even port A addresses
  signal qa0, qa1 : std_logic_vector(data_width_a-1 downto 0) := (others => '0');
  signal qb0, qb1 : std_logic_vector(data_width_a-1 downto 0) := (others => '0');
  signal sel_a : std_logic := '0';
begin
  assert LANES = 2 and addr_width_a = addr_width_b + 1 and data_width_b = 2 * data_width_a
    report "snes_ip dpram_dif: port B has to be twice as wide and half as deep as port A (the only case the machine has)" severity failure;
  process (clock)
    variable ia : integer;
    variable ib : integer;
  begin
    if rising_edge(clock) then
      ia := to_integer(unsigned(address_a(addr_width_a-1 downto 1)));
      ib := to_integer(unsigned(address_b));
      sel_a <= address_a(0);
      -- port A: one half word
      if wren_a = '1' and cs_a = '1' then
        if address_a(0) = '0' then l0(ia) <= data_a; else l1(ia) <= data_a; end if;
      end if;
      -- port B: the whole word
      if wren_b = '1' and cs_b = '1' then
        l0(ib) <= data_b(data_width_a-1 downto 0);
        l1(ib) <= data_b(data_width_b-1 downto data_width_a);
      end if;
      qa0 <= l0(ia); qa1 <= l1(ia);
      qb0 <= l0(ib); qb1 <= l1(ib);
    end if;
  end process;
  q_a <= (others => '1') when cs_a = '0' else qa0 when sel_a = '0' else qa1;
  q_b <= (others => '1') when cs_b = '0' else qb1 & qb0;
end architecture;

-- equal ports
library ieee; use ieee.std_logic_1164.all; use ieee.numeric_std.all;
entity dpram is
  generic (addr_width : integer := 8; data_width : integer := 8; mem_init_file : string := " ");
  port (clock : in std_logic;
        address_a : in std_logic_vector(addr_width-1 downto 0); data_a : in std_logic_vector(data_width-1 downto 0) := (others => '0');
        enable_a : in std_logic := '1'; wren_a : in std_logic := '0'; q_a : out std_logic_vector(data_width-1 downto 0); cs_a : in std_logic := '1';
        address_b : in std_logic_vector(addr_width-1 downto 0) := (others => '0'); data_b : in std_logic_vector(data_width-1 downto 0) := (others => '0');
        enable_b : in std_logic := '1'; wren_b : in std_logic := '0'; q_b : out std_logic_vector(data_width-1 downto 0); cs_b : in std_logic := '1');
end entity;
architecture syn of dpram is
  type ram_t is array (0 to 2**addr_width-1) of std_logic_vector(data_width-1 downto 0);
  signal ram : ram_t := (others => (others => '0'));
  signal qa, qb : std_logic_vector(data_width-1 downto 0) := (others => '0');
begin
  process (clock)
    variable ia, ib : integer;
  begin
    if rising_edge(clock) then
      ia := to_integer(unsigned(address_a));
      ib := to_integer(unsigned(address_b));
      if wren_a = '1' and cs_a = '1' then ram(ia) <= data_a; qa <= data_a; else qa <= ram(ia); end if;
      if wren_b = '1' and cs_b = '1' then ram(ib) <= data_b; qb <= data_b; else qb <= ram(ib); end if;
    end if;
  end process;
  q_a <= qa when cs_a = '1' else (others => '1');
  q_b <= qb when cs_b = '1' else (others => '1');
end architecture;

-- written by one port, read asynchronously
library ieee; use ieee.std_logic_1164.all; use ieee.numeric_std.all;
entity mlab is
  generic (addr_width : integer := 8; data_width : integer := 8);
  port (clock : in std_logic; rdaddress : in std_logic_vector(addr_width-1 downto 0); wraddress : in std_logic_vector(addr_width-1 downto 0);
        data : in std_logic_vector(data_width-1 downto 0) := (others => '0'); wren : in std_logic := '0';
        q : out std_logic_vector(data_width-1 downto 0); cs : in std_logic := '1');
end entity;
architecture syn of mlab is
  type ram_t is array (0 to 2**addr_width-1) of std_logic_vector(data_width-1 downto 0);
  signal ram : ram_t := (others => (others => '0'));
begin
  process (clock) begin
    if rising_edge(clock) then
      if wren = '1' then ram(to_integer(unsigned(wraddress))) <= data; end if;
    end if;
  end process;
  q <= ram(to_integer(unsigned(rdaddress))) when cs = '1' else (others => '1');
end architecture;

-- Game Genie: nothing is ever available and the data passes (T142 section 4)
library ieee; use ieee.std_logic_1164.all;
entity CODES is
  generic (ADDR_WIDTH : integer := 16; DATA_WIDTH : integer := 8);
  port (clk : in std_logic; reset : in std_logic; enable : in std_logic; addr_in : in std_logic_vector(23 downto 0);
        data_in : in std_logic_vector(7 downto 0); code : in std_logic_vector(128 downto 0);
        available : out std_logic; genie_ovr : out std_logic; genie_data : out std_logic_vector(7 downto 0));
end entity;
architecture syn of CODES is
begin
  available <= '0';
  genie_ovr <= '0';
  genie_data <= (others => '0');
end architecture;
