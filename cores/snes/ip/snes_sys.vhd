-- T142: the machine as the core wants it: the SNES (rtl/SNES.vhd: S-CPU, S-PPU, S-SMP, S-DSP, WRAM controller) and the plain cartridge mapper
-- (rtl/chip/DSP/DSP_LHRomMap.vhd: LoROM, HiROM, ExHiROM, SpecialLoROM, 96 Mbit; with the S-RTC and OBC-1 blocks and, on since T218, the DSP-n
-- core), joined the way SNES_MiSTer's rtl/main.v joins them for the plain-cartridge branch (USE_DLH): the mapper's DO and IRQ_N feed the S-CPU, the mapper
-- sees the S-CPU's bus, ROM/BSRAM/WRAM/VRAM/ARAM are ports. Everything else of main.v is out (T142 section 4): the other coprocessors and their address
-- multiplexer, MSU-1, the save-state engine, the Game Genie. One entity so that the converter (tools/snes_gen.py) has one top. Ours, GPL v3 as the core.
-- The two clock ports are SNES.vhd's: MCLK (CPU, PPU, mapper) and DSPCLK (S-SMP, S-DSP); main.v feeds one net to both and so does the glue.
library ieee; use ieee.std_logic_1164.all;

entity snes_sys is
  port (
    MCLK         : in  std_logic;
    DSPCLK       : in  std_logic;
    RST_N        : in  std_logic;
    PAL          : in  std_logic;
    BLEND        : in  std_logic;
    DSP_FREQ     : in  std_logic;
    -- the cartridge (header decisions are the application's: ROM_TYPE / masks as SNES.sv derives them)
    ROM_TYPE     : in  std_logic_vector(7 downto 0);
    ROM_MASK     : in  std_logic_vector(23 downto 0);
    RAM_MASK     : in  std_logic_vector(23 downto 0);
    EXT_RTC      : in  std_logic_vector(64 downto 0);
    ROM_ADDR     : out std_logic_vector(23 downto 0);
    ROM_Q        : in  std_logic_vector(15 downto 0);
    ROM_CE_N     : out std_logic;
    ROM_OE_N     : out std_logic;
    BSRAM_ADDR   : out std_logic_vector(19 downto 0);
    BSRAM_D      : out std_logic_vector(7 downto 0);
    BSRAM_Q      : in  std_logic_vector(7 downto 0);
    BSRAM_CE_N   : out std_logic;
    BSRAM_OE_N   : out std_logic;
    BSRAM_WE_N   : out std_logic;
    -- the machine's own memories
    WRAM_ADDR    : out std_logic_vector(16 downto 0);
    WRAM_D       : out std_logic_vector(7 downto 0);
    WRAM_Q       : in  std_logic_vector(7 downto 0);
    WRAM_CE_N    : out std_logic;
    WRAM_OE_N    : out std_logic;
    WRAM_WE_N    : out std_logic;
    VRAM_ADDRA   : out std_logic_vector(15 downto 0);
    VRAM_ADDRB   : out std_logic_vector(15 downto 0);
    VRAM_DAI     : in  std_logic_vector(7 downto 0);
    VRAM_DBI     : in  std_logic_vector(7 downto 0);
    VRAM_DAO     : out std_logic_vector(7 downto 0);
    VRAM_DBO     : out std_logic_vector(7 downto 0);
    VRAM_WRA_N   : out std_logic;
    VRAM_WRB_N   : out std_logic;
    VRAM_RD_N    : out std_logic;
    ARAM_ADDR    : out std_logic_vector(15 downto 0);
    ARAM_D       : out std_logic_vector(7 downto 0);
    ARAM_Q       : in  std_logic_vector(7 downto 0);
    ARAM_CE_N    : out std_logic;
    ARAM_OE_N    : out std_logic;
    ARAM_WE_N    : out std_logic;
    -- bus phases the glue needs
    SYSCLKF_CE   : out std_logic;
    SYSCLKR_CE   : out std_logic;
    REFRESH      : out std_logic;
    -- picture
    HIGH_RES     : out std_logic;
    FIELD_OUT    : out std_logic;
    INTERLACE    : out std_logic;
    V224_MODE    : out std_logic;
    DOTCLK       : out std_logic;
    RGB_OUT      : out std_logic_vector(23 downto 0);
    HDE          : out std_logic;
    VDE          : out std_logic;
    HSYNC        : out std_logic;
    VSYNC        : out std_logic;
    HVCNT_ATZERO : out std_logic;
    -- controller ports
    JOY1_DI      : in  std_logic_vector(1 downto 0);
    JOY2_DI      : in  std_logic_vector(1 downto 0);
    JOY_STRB     : out std_logic;
    JOY1_CLK     : out std_logic;
    JOY2_CLK     : out std_logic;
    -- sound
    AUDIO_L      : out std_logic_vector(15 downto 0);
    AUDIO_R      : out std_logic_vector(15 downto 0);
    -- the S-SMP's boot ROM, written by the application while the machine is held (tools/snes_gen.py, T142 D9)
    IPL_WR       : in  std_logic;
    IPL_WA       : in  std_logic_vector(5 downto 0);
    IPL_WD       : in  std_logic_vector(7 downto 0);
    -- the DSP-n coprocessor's firmware (T218): written by the application while the machine is held; FW_SEL 0 = program word FW_A (24 bit), 1 = data word FW_A(9:0) (16 bit)
    FW_WR        : in  std_logic;
    FW_SEL       : in  std_logic;
    FW_A         : in  std_logic_vector(10 downto 0);
    FW_D         : in  std_logic_vector(23 downto 0)
  );
end entity;

architecture rtl of snes_sys is
  signal CA          : std_logic_vector(23 downto 0);
  signal CPURD_N     : std_logic;
  signal CPUWR_N     : std_logic;
  signal PA          : std_logic_vector(7 downto 0);
  signal PARD_N      : std_logic;
  signal PAWR_N      : std_logic;
  signal DI          : std_logic_vector(7 downto 0);
  signal DO          : std_logic_vector(7 downto 0);
  signal RAMSEL_N    : std_logic;
  signal ROMSEL_N    : std_logic;
  signal CLKF, CLKR  : std_logic;
  signal REFRESH_i   : std_logic;
  signal DLH_DO      : std_logic_vector(7 downto 0);
  signal DLH_IRQ_N   : std_logic;
  signal DLH_SS_DO   : std_logic_vector(7 downto 0);
  signal MAP_ACTIVE  : std_logic;
  signal ROM_WORD    : std_logic;
  signal GG_AVAIL    : std_logic;
  signal SNI_JOY     : std_logic_vector(63 downto 0);
  signal JOY1_P6, JOY2_P6 : std_logic;
  signal SS_SPC_DO, SS_PPU_DO : std_logic_vector(7 downto 0);
begin
  snes_i : entity work.SNES
    port map (
      MCLK => MCLK, DSPCLK => DSPCLK, RST_N => RST_N, ENABLE => '1', PAL => PAL, BLEND => BLEND,
      CA => CA, CPURD_N => CPURD_N, CPUWR_N => CPUWR_N,
      PA => PA, PARD_N => PARD_N, PAWR_N => PAWR_N, DI => DLH_DO, DO => DO,
      RAMSEL_N => RAMSEL_N, ROMSEL_N => ROMSEL_N,
      SYSCLKF_CE => CLKF, SYSCLKR_CE => CLKR, REFRESH => REFRESH_i,
      IRQ_N => DLH_IRQ_N,
      WSRAM_ADDR => WRAM_ADDR, WSRAM_D => WRAM_D, WSRAM_Q => WRAM_Q, WSRAM_CE_N => WRAM_CE_N, WSRAM_OE_N => WRAM_OE_N, WSRAM_WE_N => WRAM_WE_N,
      VRAM_ADDRA => VRAM_ADDRA, VRAM_ADDRB => VRAM_ADDRB, VRAM_DAI => VRAM_DAI, VRAM_DBI => VRAM_DBI,
      VRAM_DAO => VRAM_DAO, VRAM_DBO => VRAM_DBO, VRAM_WRA_N => VRAM_WRA_N, VRAM_WRB_N => VRAM_WRB_N, VRAM_RD_N => VRAM_RD_N,
      ARAM_ADDR => ARAM_ADDR, ARAM_D => ARAM_D, ARAM_Q => ARAM_Q, ARAM_CE_N => ARAM_CE_N, ARAM_OE_N => ARAM_OE_N, ARAM_WE_N => ARAM_WE_N,
      HIGH_RES => HIGH_RES, FIELD_OUT => FIELD_OUT, INTERLACE => INTERLACE, V224_MODE => V224_MODE, DOTCLK => DOTCLK,
      RGB_OUT => RGB_OUT, HDE => HDE, VDE => VDE, HSYNC => HSYNC, VSYNC => VSYNC, HVCNT_ATZERO => HVCNT_ATZERO,
      JOY1_DI => JOY1_DI, JOY2_DI => JOY2_DI, JOY_STRB => JOY_STRB, JOY1_CLK => JOY1_CLK, JOY2_CLK => JOY2_CLK,
      JOY1_P6 => JOY1_P6, JOY2_P6 => JOY2_P6, JOY2_P6_in => '1', SNI_JOY => SNI_JOY,
      LRCK => open, BCK => open, SDAT => open,
      GG_EN => '0', GG_CODE => (others => '0'), GG_RESET => '0', GG_AVAILABLE => GG_AVAIL,
      SPC_MODE => '0', IO_ADDR => (others => '0'), IO_DAT => (others => '0'), IO_WR => '0',
      SS_ADDR => (others => '0'), SS_BUSY => '0', SS_REGS_SEL => '0', SS_SMP_SEL => '0', SS_WR => '0', SS_DI => (others => '0'),
      SS_SPC_DO => SS_SPC_DO, SS_PPU_DO => SS_PPU_DO,
      TURBO => '0', DSP_FREQ => DSP_FREQ, DBG_BG_EN => "11111", DBG_CPU_EN => '1',
      AUDIO_L => AUDIO_L, AUDIO_R => AUDIO_R,
      IPL_WR => IPL_WR, IPL_WA => IPL_WA, IPL_WD => IPL_WD);

  dlh_i : entity work.DSP_LHRomMap
    generic map (USE_DSPn => '1')
    port map (
      MCLK => MCLK, RST_N => RST_N, ENABLE => '1',
      CA => CA, DI => DO, DO => DLH_DO, CPURD_N => CPURD_N, CPUWR_N => CPUWR_N,
      PA => PA, PARD_N => PARD_N, PAWR_N => PAWR_N, ROMSEL_N => ROMSEL_N, RAMSEL_N => RAMSEL_N,
      SYSCLKF_CE => CLKF, SYSCLKR_CE => CLKR, REFRESH => REFRESH_i,
      IRQ_N => DLH_IRQ_N,
      ROM_ADDR => ROM_ADDR, ROM_Q => ROM_Q, ROM_CE_N => ROM_CE_N, ROM_OE_N => ROM_OE_N, ROM_WORD => ROM_WORD,
      BSRAM_ADDR => BSRAM_ADDR, BSRAM_D => BSRAM_D, BSRAM_Q => BSRAM_Q, BSRAM_CE_N => BSRAM_CE_N, BSRAM_OE_N => BSRAM_OE_N, BSRAM_WE_N => BSRAM_WE_N,
      MAP_ACTIVE => MAP_ACTIVE, MAP_CTRL => ROM_TYPE, ROM_MASK => ROM_MASK, BSRAM_MASK => RAM_MASK,
      EXT_RTC => EXT_RTC, CC_DIP => x"10",
      SS_BUSY => '0', SS_RAM_A => (others => '0'), SS_DSPN_RAM_SEL => '0', SS_DSPN_REGS_SEL => '0', SS_DI => (others => '0'), SS_DO => DLH_SS_DO,
      FW_WR => FW_WR, FW_SEL => FW_SEL, FW_A => FW_A, FW_D => FW_D);

  SYSCLKF_CE <= CLKF;
  SYSCLKR_CE <= CLKR;
  REFRESH    <= REFRESH_i;
end architecture;
