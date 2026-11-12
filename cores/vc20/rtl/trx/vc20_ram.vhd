-- TRX VC-20 core: one memory array of the machine, true dual port.
--   port A  the machine (clk, clk_en): one access per enabled clock, read-first, the output holds
--           between accesses (this is what the Gowin single-port blocks of VIC20Nano did, with
--           CLK_EN = the 4.43 MHz pixel enable). A ROM ties clk_en = 1, en_n = 0, wr_n = 1.
--   port B  the host's window (conf_clk): write, and read with the data one clock after the
--           address. The window sees the array at the 64 KB VIC-20 addresses LO..HI; the array
--           index is the low AW bits of the address (the machine uses the same bits).
-- Written for this project (TREX), no upstream code.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vc20_ram is
  generic (
    AW : integer := 10;
    DW : integer := 8;
    LO : integer := 0;
    HI : integer := 1023
  );
  port (
    clk      : in  std_logic;
    clk_en   : in  std_logic;
    en_n     : in  std_logic;
    wr_n     : in  std_logic;
    addr     : in  std_logic_vector(AW-1 downto 0);
    din      : in  std_logic_vector(DW-1 downto 0);
    dout     : out std_logic_vector(DW-1 downto 0);

    conf_clk : in  std_logic;
    conf_wr  : in  std_logic;
    conf_ai  : in  std_logic_vector(15 downto 0);
    conf_di  : in  std_logic_vector(7 downto 0);
    conf_do  : out std_logic_vector(7 downto 0)
  );
end entity;

architecture rtl of vc20_ram is
  type ram_t is array (0 to 2**AW - 1) of std_logic_vector(DW-1 downto 0);
  shared variable mem : ram_t := (others => (others => '0'));
  attribute ram_style : string;
  attribute ram_style of mem : variable is "block";
  signal dout_q : std_logic_vector(DW-1 downto 0) := (others => '0');
  signal cdo_q  : std_logic_vector(DW-1 downto 0) := (others => '0');
  signal hit_q  : std_logic := '0';
  signal hit    : std_logic;
begin
  hit <= '1' when (to_integer(unsigned(conf_ai)) >= LO and to_integer(unsigned(conf_ai)) <= HI) else '0';

  pa : process (clk)
  begin
    if rising_edge(clk) then
      if clk_en = '1' and en_n = '0' then
        dout_q <= mem(to_integer(unsigned(addr)));
        if wr_n = '0' then
          mem(to_integer(unsigned(addr))) := din;
        end if;
      end if;
    end if;
  end process;
  dout <= dout_q;

  pb : process (conf_clk)
  begin
    if rising_edge(conf_clk) then
      cdo_q <= mem(to_integer(unsigned(conf_ai(AW-1 downto 0))));
      hit_q <= hit;
      if conf_wr = '1' and hit = '1' then
        mem(to_integer(unsigned(conf_ai(AW-1 downto 0)))) := conf_di(DW-1 downto 0);
      end if;
    end if;
  end process;

  po : process (cdo_q, hit_q)
    variable v : std_logic_vector(7 downto 0);
  begin
    v := (others => '0');
    if hit_q = '1' then
      v(DW-1 downto 0) := cdo_q;
    end if;
    conf_do <= v;
  end process;
end architecture;
