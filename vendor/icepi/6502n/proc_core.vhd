library ieee;
use ieee.std_logic_1164.all;

library work;
use work.pkg_6502_defs.all;

entity proc_core is
generic (
    vector_page  : std_logic_vector(15 downto 4) := X"FFF";
    support_bcd  : boolean := true;
    g_sb_full    : boolean := false );
port(
    clock        : in  std_logic;
    clock_en     : in  std_logic := '1';

    reset        : in  std_logic;

    ready        : in  std_logic := '1';
    irq_n        : in  std_logic := '1';
    nmi_n        : in  std_logic := '1';
    so_n         : in  std_logic := '1';
    carry        : out std_logic;

    sync_out     : out std_logic;
    intg_out     : out std_logic;   -- an interrupt is decided: this fetch is dropped
    -- the registers, for the state bus (c64_core.v; ARCH-lean-state-access §5.4)
    a_out        : out std_logic_vector(7 downto 0);
    x_out        : out std_logic_vector(7 downto 0);
    y_out        : out std_logic_vector(7 downto 0);
    s_out        : out std_logic_vector(7 downto 0);
    p_out        : out std_logic_vector(7 downto 0);
    interrupt_ack: out std_logic;
    pc_out       : out std_logic_vector(15 downto 0);
    addr_out     : out std_logic_vector(16 downto 0);
    data_in      : in  std_logic_vector(7 downto 0);
    data_out     : out std_logic_vector(7 downto 0);
    -- T29: data_out without the late sources, and whether this cycle's data is one of them
    data_early   : out std_logic_vector(7 downto 0);
    data_late    : out std_logic;
    read_write_n : out std_logic;
    -- state access (c64_core.v, ARCH-lean-state-access §5): every flip-flop
    -- of the core as one vector (bits 101:0 proc_registers, 123:102
    -- proc_control, 129:124 proc_interrupt); sb_ld loads them all from
    -- sb_stg on the next clock instead of their next state
    sb_ld        : in  std_logic := '0';
    sb_stg       : in  std_logic_vector(129 downto 0) := (others => '0');
    sb_cur       : out std_logic_vector(129 downto 0);
    -- {P, S, Y, X, A} after the next clock_en (proc_registers nx_regs)
    nx_out       : out std_logic_vector(39 downto 0) );

end proc_core;

architecture structural of proc_core is
    signal index_carry  : std_logic;
    signal pc_carry     : std_logic;
    signal branch_taken : boolean;
    signal i_reg        : std_logic_vector(7 downto 0);
    signal d_reg        : std_logic_vector(7 downto 0);
    signal a_reg        : std_logic_vector(7 downto 0);
    signal x_reg        : std_logic_vector(7 downto 0);
    signal y_reg        : std_logic_vector(7 downto 0);
    signal s_reg        : std_logic_vector(7 downto 0);
    signal p_reg        : std_logic_vector(7 downto 0);

    signal latch_dreg   : std_logic;
    signal reg_update   : std_logic;
    signal flags_update : std_logic;
    signal copy_d2p     : std_logic;
    signal sync         : std_logic;
    signal rwn          : std_logic;
    signal a_mux        : t_amux;
    signal pc_oper      : t_pc_oper;
    signal s_oper       : t_sp_oper;
    signal adl_oper     : t_adl_oper;
    signal adh_oper     : t_adh_oper;
    signal dout_mux     : t_dout_mux;

    signal alu_out      : std_logic_vector(7 downto 0);
    signal impl_out     : std_logic_vector(7 downto 0);
    signal mem_out      : std_logic_vector(7 downto 0);
    signal mem_n        : std_logic;
    signal mem_z        : std_logic;
    signal mem_c        : std_logic;

    signal set_a        : std_logic;
    signal set_x        : std_logic;
    signal set_y        : std_logic;
    signal set_s        : std_logic;

    signal vect_addr    : std_logic_vector(3 downto 0);
    signal interrupt    : std_logic;
    signal vectoring    : std_logic;
    signal nmi_done     : std_logic;
    signal set_i_flag   : std_logic;
    signal vect_sel     : std_logic_vector(2 downto 1);

    signal flags_imm    : std_logic;
    signal new_flags    : std_logic_vector(7 downto 0);
    signal n_out        : std_logic;
    signal v_out        : std_logic;
    signal c_out        : std_logic;
    signal z_out        : std_logic;
    signal d_out        : std_logic;
    signal i_out        : std_logic;
    signal a16          : std_logic;
    signal sb_cur_r     : std_logic_vector(100 downto 0);

    attribute keep : string;
    attribute keep of interrupt : signal is "true";
begin
    carry <= p_reg(0);

    new_flags(7) <= n_out;
    new_flags(6) <= v_out;
    new_flags(5) <= '1';
    new_flags(4) <= p_reg(4);
    new_flags(3) <= d_out;
    new_flags(2) <= i_out;
    new_flags(1) <= z_out;
    new_flags(0) <= c_out;

    ctrl: entity work.proc_control
    generic map (
        g_sb_full    => g_sb_full )
    port map (
        clock        => clock,
        clock_en     => clock_en,
        ready        => ready,
        reset        => reset,

        interrupt    => interrupt,
        vectoring    => vectoring,
        taking_intr  => interrupt_ack,
        vect_sel     => vect_sel,
        nmi_done     => nmi_done,
        set_i_flag   => set_i_flag,
        vect_addr    => vect_addr,

        i_reg        => i_reg,
        index_carry  => index_carry,
        pc_carry     => pc_carry,
        branch_taken => branch_taken,

        sync         => sync,
        latch_dreg   => latch_dreg,
        reg_update   => reg_update,
        flags_update => flags_update,
        copy_d2p     => copy_d2p,
        a16          => a16,
        rwn          => rwn,
        a_mux        => a_mux,
        dout_mux     => dout_mux,
        pc_oper      => pc_oper,
        s_oper       => s_oper,
        adl_oper     => adl_oper,
        adh_oper     => adh_oper,
        sb_ld        => sb_ld,
        sb_stg       => sb_stg(123 downto 102),
        sb_cur       => sb_cur(123 downto 102) );

    oper: entity work.data_oper
    generic map (
        support_bcd => support_bcd )
    port map (
        inst       => i_reg,

        n_in       => p_reg(7),
        v_in       => p_reg(6),
        z_in       => p_reg(1),
        c_in       => p_reg(0),
        d_in       => p_reg(3),
        i_in       => p_reg(2),

        data_in    => d_reg,
        a_reg      => a_reg,
        x_reg      => x_reg,
        y_reg      => y_reg,
        s_reg      => s_reg,

        alu_out    => alu_out,
        mem_out    => mem_out,
        mem_c      => mem_c,
        mem_z      => mem_z,
        mem_n      => mem_n,
        impl_out   => impl_out,

        set_a      => set_a,
        set_x      => set_x,
        set_y      => set_y,
        set_s      => set_s,

        flags_imm  => flags_imm,
        n_out      => n_out,
        v_out      => v_out,
        z_out      => z_out,
        c_out      => c_out,
        d_out      => d_out,
        i_out      => i_out );

    regs: entity work.proc_registers
    generic map (
        vector_page  => vector_page,
        g_sb_full    => g_sb_full )
    port map (
        clock        => clock,
        clock_en     => clock_en,
        ready        => ready,
        reset        => reset,

        -- package pins
        data_in      => data_in,
        data_out     => data_out,
        so_n         => so_n,

        -- data from "data_oper"
        alu_data     => alu_out,
        mem_data     => mem_out,
        mem_c        => mem_c,
        mem_z        => mem_z,
        mem_n        => mem_n,
        new_flags    => new_flags,
        flags_imm    => flags_imm,

        -- from implied handler
        set_a        => set_a,
        set_x        => set_x,
        set_y        => set_y,
        set_s        => set_s,
        set_data     => impl_out,

        -- from interrupt controller
        vect_addr    => vect_addr,

        -- from processor state machine and decoder
        sync         => sync,
        rwn          => rwn,
        latch_dreg   => latch_dreg,
        set_i_flag   => set_i_flag,
        vectoring    => vectoring,
        reg_update   => reg_update,
        flags_update => flags_update,
        copy_d2p     => copy_d2p,
        a_mux        => a_mux,
        dout_mux     => dout_mux,
        pc_oper      => pc_oper,
        s_oper       => s_oper,
        adl_oper     => adl_oper,
        adh_oper     => adh_oper,

        -- outputs to processor state machine
        i_reg        => i_reg,
        index_carry  => index_carry,
        pc_carry     => pc_carry,
        branch_taken => branch_taken,

        -- register outputs
        addr_out     => addr_out(15 downto 0),
        data_early   => data_early,

        d_reg        => d_reg,
        a_reg        => a_reg,
        x_reg        => x_reg,
        y_reg        => y_reg,
        s_reg        => s_reg,
        p_reg        => p_reg,
        pc_out       => pc_out,
        sb_ld        => sb_ld,
        sb_stg       => sb_stg(101 downto 0),
        sb_cur       => sb_cur_r,
        nx_regs      => nx_out );
    sb_cur(101 downto 0) <= index_carry & sb_cur_r;

    intr: entity work.proc_interrupt
    generic map (
        g_sb_full   => g_sb_full )
    port map (
        clock       => clock,
        clock_en    => clock_en,

        reset       => reset,

        irq_n       => irq_n,
        nmi_n       => nmi_n,

        i_flag      => p_reg(2),

        interrupt   => interrupt,
        nmi_done    => nmi_done,
        reset_done  => set_i_flag,
        vect_sel    => vect_sel,
        sb_ld       => sb_ld,
        sb_stg      => sb_stg(129 downto 124),
        sb_cur      => sb_cur(129 downto 124) );

    data_late    <= '1' when dout_mux = shift_res or dout_mux = reg_axy else '0';
    read_write_n <= rwn;
    addr_out(16) <= a16;
    sync_out     <= sync;
    intg_out     <= vectoring;
    a_out        <= a_reg;
    x_out        <= x_reg;
    y_out        <= y_reg;
    s_out        <= s_reg;
    p_out        <= p_reg;
end structural;
