-- T182 area trial: wrapper in library work around the MiSTer 65C816 (cores/snes/rtl/65C816, read into the library s816 so that its
-- entity names ALU / MCode / AddrGen do not meet the 6502n's alu / ... in work).
library ieee;
use ieee.std_logic_1164.all;
library s816;

entity scpu_cpu816 is
    port (
        CLK, RST_N, CE, RDY_IN, NMI_N, IRQ_N, ABORT_N : in std_logic;
        D_IN   : in  std_logic_vector(7 downto 0);
        D_OUT  : out std_logic_vector(7 downto 0);
        A_OUT  : out std_logic_vector(23 downto 0);
        WE, RDY_OUT, VPA, VDA, MLB, VPB : out std_logic
    );
end scpu_cpu816;

architecture rtl of scpu_cpu816 is
begin
    u : entity s816.P65C816
        port map (CLK => CLK, RST_N => RST_N, CE => CE, RDY_IN => RDY_IN, NMI_N => NMI_N, IRQ_N => IRQ_N, ABORT_N => ABORT_N,
                  D_IN => D_IN, D_OUT => D_OUT, A_OUT => A_OUT, WE => WE, RDY_OUT => RDY_OUT, VPA => VPA, VDA => VDA, MLB => MLB, VPB => VPB);
end rtl;
