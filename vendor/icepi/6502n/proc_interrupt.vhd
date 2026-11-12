library ieee;
use ieee.std_logic_1164.all;

entity proc_interrupt is
generic (
    g_sb_full   : boolean := false );
port (
    clock       : in  std_logic;
    clock_en    : in  std_logic;
    
    reset       : in  std_logic;
    
    irq_n       : in  std_logic;
    nmi_n       : in  std_logic;
    
    i_flag      : in  std_logic;

    nmi_done    : in  std_logic;
    reset_done  : in  std_logic;
    
    interrupt   : out std_logic;
    vect_sel    : out std_logic_vector(2 downto 1);
    -- state access (c64_core.v): {resetting, nmi_act, nmi_d2, nmi_d1, nmi_c, irq_c}
    sb_ld       : in  std_logic := '0';
    sb_stg      : in  std_logic_vector(5 downto 0) := (others => '0');
    sb_cur      : out std_logic_vector(5 downto 0) );

end proc_interrupt;

architecture gideon of proc_interrupt is
    signal irq_c    : std_logic := '1';
    signal irq_d1   : std_logic := '0';
    signal nmi_c    : std_logic := '0';
    signal nmi_d1   : std_logic := '0';
    signal nmi_d2   : std_logic := '0';
    signal nmi_act  : std_logic := '0';
    signal nmi_act_comb : std_logic := '0';
    signal vect_h   : std_logic_vector(2 downto 1) := "10";
    signal resetting : std_logic := '1';

    --         21
    -- NMI   1 01-
    -- RESET 1 10-
    -- IRQ   1 11-
    function calc_vec_addr(rst, irq, nmi : std_logic) return std_logic_vector is
        variable v : std_logic_vector(2 downto 1);
    begin
        if rst='1' then
            v := "10";
        elsif nmi='1' then
            v := "01";
        else
            v := "11";
        end if;
        return v;
    end function;

begin
    vect_sel <= vect_h;
    sb_cur   <= resetting & nmi_act & nmi_d2 & nmi_d1 & nmi_c & irq_c;
    
    interrupt  <= (irq_d1 or nmi_act_comb);

    irq_d1  <= not irq_c and not i_flag; -- Visual6502: INTP

    -- For NMI we need to further investigate
    -- http://visual6502.org/JSSim/expert.html?nmi0=13&nmi1=14&a=0000&d=78085828eaea4c&logmore=rdy,ir,State,irq,nmi,~IRQP,NMIP,882,INTG
    -- Node 882 seems functionally equivalent to ~IRQP
    process(clock)
    begin
        if rising_edge(clock) then
            nmi_c   <= not nmi_n;

            -- synchronization flipflop (near PAD)
            if clock_en='1' then
                irq_c   <= irq_n;   -- in Visual6502 irq_c is called ~IRQP
            end if;
                        
            if clock_en='1' then
                nmi_d1 <= nmi_c;
            end if;
            if sb_ld = '1' and g_sb_full then   -- else sampled again first
                irq_c  <= sb_stg(0);
                nmi_c  <= sb_stg(1);
                nmi_d1 <= sb_stg(2);
            end if;
        end if;
    end process;
        
    vect_h <= calc_vec_addr(resetting, irq_d1, nmi_act_comb);
    nmi_act_comb <= (nmi_d1 and not nmi_d2) or nmi_act;

    process(clock)
    begin
        if rising_edge(clock) then
            if clock_en='1' then
                
                nmi_d2 <= nmi_d1;
                if nmi_done = '1' then
                    nmi_act <= '0';
                elsif nmi_d2 = '0' and nmi_d1 = '1' then -- edge
                    nmi_act <= '1';
                end if;

                if reset_done = '1' then
                    resetting <= '0';
                end if; 
                    
            end if;

            if reset='1' then
                nmi_act <= '0';
                resetting <= '1';
            end if;
            if sb_ld = '1' then
                nmi_d2    <= sb_stg(3);
                nmi_act   <= sb_stg(4);
                resetting <= sb_stg(5);
            end if;
        end if;
    end process;

end gideon;
