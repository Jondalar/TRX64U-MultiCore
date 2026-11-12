-- TRX Apple //e core (T89): one memory array of the machine, true dual port, both ports on sys_clk.
--   port A  the machine: one access per `a_en` (the machine's enable, or the sample of its RAM once per phase), read first,
--           the output register holds between accesses - the behaviour of the Gowin blocks of NanoApple2 and of
--           the synchronous ROMs of Stephen Edwards' Apple (clocked by CLK_14M)
--   port B  the host's window: write, and read with the data one clock after the address
-- Written for this project (TREX), no upstream code.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity a2_ram is
  generic (
    AW : integer := 10;
    DW : integer := 8
  );
  port (
    clk    : in  std_logic;
    a_en   : in  std_logic;
    a_we   : in  std_logic;
    a_addr : in  std_logic_vector(AW-1 downto 0);
    a_din  : in  std_logic_vector(DW-1 downto 0);
    a_dout : out std_logic_vector(DW-1 downto 0);
    b_we   : in  std_logic;
    b_addr : in  std_logic_vector(AW-1 downto 0);
    b_din  : in  std_logic_vector(DW-1 downto 0);
    b_dout : out std_logic_vector(DW-1 downto 0)
  );
end entity;

architecture rtl of a2_ram is
  type ram_t is array (0 to 2**AW - 1) of std_logic_vector(DW-1 downto 0);
  shared variable mem : ram_t := (others => (others => '0'));
  attribute ram_style : string;
  attribute ram_style of mem : variable is "block";
  signal qa : std_logic_vector(DW-1 downto 0) := (others => '0');
  signal qb : std_logic_vector(DW-1 downto 0) := (others => '0');
begin
  pa : process (clk)
  begin
    if rising_edge(clk) then
      if a_en = '1' then
        qa <= mem(to_integer(unsigned(a_addr)));
        if a_we = '1' then
          mem(to_integer(unsigned(a_addr))) := a_din;
        end if;
      end if;
    end if;
  end process;
  a_dout <= qa;

  pb : process (clk)
  begin
    if rising_edge(clk) then
      qb <= mem(to_integer(unsigned(b_addr)));
      if b_we = '1' then
        mem(to_integer(unsigned(b_addr))) := b_din;
      end if;
    end if;
  end process;
  b_dout <= qb;
end architecture;
