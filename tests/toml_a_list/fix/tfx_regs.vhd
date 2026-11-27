-- fixture decoder (VHDL): page 0, byte offsets
library ieee; use ieee.std_logic_1164.all;
entity tfx_regs is
  port (clk, we, re : in std_logic; addr : in std_logic_vector(16 downto 0); wdata : in std_logic_vector(7 downto 0); q : out std_logic_vector(7 downto 0));
end entity;
architecture rtl of tfx_regs is
  signal off : std_logic_vector(11 downto 0);
  signal page : std_logic_vector(3 downto 0);
  signal win : std_logic;
begin
  off <= addr(11 downto 0);
  page <= addr(15 downto 12);
  win <= addr(16);
  process (clk) begin
    if rising_edge(clk) then
      q <= x"00";
      if win = '0' and page = x"0" then
        case off is
          when x"000" | x"001" | x"002" | x"003" => q <= x"55";
          when x"004" => q <= x"C0";
          when x"005" => q <= x"02";
          when x"008" => q <= x"01";
          when x"009" => q <= x"01";
          when x"00A" => q <= x"01";
          when x"00B" => q <= x"01";
          when x"010" => q <= x"01";
          when x"014" => q <= x"01";
          when x"015" => q <= x"01";
          when x"016" => q <= x"01";
          when x"017" => q <= x"01";
          when others => null;
        end case;
      end if;
    end if;
  end process;
end architecture;
