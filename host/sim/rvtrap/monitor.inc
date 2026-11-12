--@DECL
    -- monitor (host/sim/rvtrap/monitor.inc): spliced in by run.sh, reads the core's own signals
    type t_mon_mem is array(0 to 16383) of std_logic_vector(31 downto 0);
    impure function mon_load return t_mon_mem is
        file f : text open read_mode is work.rvtrap_pkg.c_prog;
        variable l : line;
        variable m : t_mon_mem := (others => X"00000000");
        variable i : natural := 0;
        variable w : std_logic_vector(31 downto 0);
    begin
        while not endfile(f) loop
            readline(f, l);
            hread(l, w);
            m(i) := w;
            i := i + 1;
        end loop;
        return m;
    end function;
    constant mon_code    : t_mon_mem := mon_load;
    signal   mon_mstatus : std_logic_vector(31 downto 0);
--@BODY
    p_mon: process(clock)
        variable cyc      : natural := 0;
        variable errors   : natural := 0;
        variable started  : boolean := false;
        variable exp_ok   : boolean := false;
        variable exp_a    : unsigned(31 downto 0);
        variable exp_b    : unsigned(31 downto 0);
        variable exp_two  : boolean := false;
        variable s_mie    : std_logic := '0';
        variable s_mpie   : std_logic := '0';
        variable s_mepc   : std_logic_vector(31 downto 0) := (others => '0');
        variable last_ev  : string(1 to 28) := (others => ' ');
        variable last_pc  : std_logic_vector(31 downto 0) := (others => '0');
        variable pc       : std_logic_vector(31 downto 0);
        variable w        : std_logic_vector(31 downto 0);
        variable old, nw  : std_logic_vector(31 downto 0);
        variable n_ecall, n_irqtrap, n_mret, n_retire : natural := 0;

        type t_hist is array(0 to 15) of string(1 to 130);
        variable hist  : t_hist := (others => (others => ' '));
        variable hh    : natural := 0;
        variable dumped : boolean := false;
        procedure put(constant s : string) is
            variable n : natural := s'length;
        begin
            if n > 130 then n := 130; end if;
            hist(hh) := (others => ' ');
            hist(hh)(1 to n) := s(s'left to s'left + n - 1);
        end procedure;

        procedure set_ev(constant s : string) is
        begin
            last_ev := (others => ' ');
            last_ev(1 to s'length) := s;
        end procedure;

        procedure err(msg : string) is
        begin
            errors := errors + 1;
            if not dumped then
                dumped := true;
                for i in 1 to 16 loop
                    report "TB: hist " & hist((hh + i) mod 16);
                end loop;
            end if;
            if errors <= 12 then
                report "TB: ERROR @" & integer'image(cyc) & " " & msg & " (last: " & last_ev & " pc=" & to_hstring(last_pc) & ")" severity note;
            end if;
        end procedure;
    begin
        if rising_edge(clock) then
            cyc := cyc + 1;
            put(integer'image(cyc) & " dpc=" & to_hstring(decode_o.program_counter) & " dv=" & std_logic'image(decode_o.valid)(2 to 2)
                & " ft=" & t_flow_target'image(decode_o.flow_target) & " exrdy=" & std_logic'image(ex_ready)(2 to 2)
                & " eov=" & std_logic'image(exec_o.valid)(2 to 2) & " ejmp=" & std_logic'image(exec_o.do_jump)(2 to 2)
                & " trap=" & std_logic'image(csr_req.trap.trap)(2 to 2) & " mret=" & std_logic'image(csr_req.trap.mret)(2 to 2)
                & " ms=" & std_logic'image(mon_mstatus(3))(2 to 2) & std_logic'image(mon_mstatus(7))(2 to 2)
                & " irq=" & std_logic'image(csr_resp.irq)(2 to 2) & " rdyd=" & std_logic'image(dec_ready)(2 to 2) & " wb=" & std_logic'image(wb_ready)(2 to 2) & " hz=" & std_logic'image(hazard)(2 to 2) & " dvm=" & std_logic'image(decode_o.csr_access)(2 to 2));
            hh := (hh + 1) mod 16;
            if reset = '0' then
                if not started then
                    started := true;
                    s_mie := mon_mstatus(3);
                    s_mpie := mon_mstatus(7);
                    s_mepc := csr_resp.mepc;
                end if;

                -- CSR against the model of what retired
                if mon_mstatus(3) /= s_mie or mon_mstatus(7) /= s_mpie then
                    err("mstatus MIE/MPIE " & std_logic'image(mon_mstatus(3)) & std_logic'image(mon_mstatus(7))
                        & " but retired instructions give " & std_logic'image(s_mie) & std_logic'image(s_mpie));
                    s_mie := mon_mstatus(3);
                    s_mpie := mon_mstatus(7);
                end if;
                if csr_resp.mepc /= s_mepc then
                    err("mepc " & to_hstring(csr_resp.mepc) & " but retired instructions give " & to_hstring(s_mepc));
                    s_mepc := csr_resp.mepc;
                end if;

                -- instruction word and PC of the fetch -> decode hand-over
                if fetch_o.inst_valid = '1' and dec_ready = '1' then
                    if unsigned(fetch_o.program_counter(31 downto 16)) = 0 then
                        w := mon_code(to_integer(unsigned(fetch_o.program_counter(15 downto 2))));
                        if fetch_o.instruction /= w then
                            err("fetch: word " & to_hstring(fetch_o.instruction) & " travels with pc " & to_hstring(fetch_o.program_counter) & " where the program has " & to_hstring(w));
                        end if;
                    end if;
                end if;

                -- retire: the execute stage advances on a valid, unflushed instruction
                if ex_ready = '1' and decode_o.valid = '1' and exec_o.do_jump = '0' then
                    pc := decode_o.program_counter;
                    n_retire := n_retire + 1;
                    last_pc := pc;
                    if exp_ok then
                        if not (unsigned(pc) = exp_a or (exp_two and unsigned(pc) = exp_b)) then
                            err("pc sequence: retired " & to_hstring(pc) & " after " & last_ev & ", expected " & to_hstring(std_logic_vector(exp_a)));
                        end if;
                    end if;
                    exp_ok := true;
                    exp_two := false;
                    exp_a := unsigned(pc) + 4;
                    w := mon_code(to_integer(unsigned(pc(15 downto 2))));
                    set_ev("ordinary");

                    if csr_req.enable = '1' and csr_req.oper /= CSR_NONE then
                        if csr_req.address = X"300" then
                            old := (others => '0');
                            old(3) := s_mie; old(7) := s_mpie;
                        elsif csr_req.address = X"341" then
                            old := s_mepc;
                        else
                            old := (others => '0');
                        end if;
                        case csr_req.oper is
                        when CSR_WRITE => nw := csr_req.wdata;
                        when CSR_SET   => nw := csr_req.wdata or old;
                        when CSR_CLR   => nw := old and not csr_req.wdata;
                        when others    => nw := old;
                        end case;
                        if csr_req.address = X"300" then
                            s_mie := nw(3); s_mpie := nw(7);
                            set_ev("csr write mstatus");
                        elsif csr_req.address = X"341" then
                            s_mepc := nw;
                            set_ev("csr write mepc");
                        end if;
                    end if;

                    if decode_o.flow_ctrl = FL_JUMP and decode_o.flow_target = TRGT_TRAP then
                        if decode_o.cause = "01011" then
                            n_ecall := n_ecall + 1;
                            set_ev("ecall");
                            if w /= X"00000073" then
                                err("ECALL-cause trap on pc " & to_hstring(pc) & " whose program word is " & to_hstring(w));
                            end if;
                        elsif decode_o.cause = "11011" then
                            n_irqtrap := n_irqtrap + 1;
                            set_ev("interrupt dummy");
                            if s_mie = '0' then
                                err("interrupt taken with MIE = 0");
                            end if;
                        else
                            err("unexpected trap cause " & integer'image(to_integer(unsigned(decode_o.cause))));
                        end if;
                        s_mepc := pc;
                        s_mpie := s_mie;
                        s_mie := '0';
                        exp_a := unsigned(csr_resp.mtvec);
                    elsif decode_o.flow_ctrl = FL_JUMP and decode_o.flow_target = TRGT_MEPC then
                        n_mret := n_mret + 1;
                        set_ev("mret");
                        if csr_resp.mepc /= s_mepc then
                            err("mret reads mepc " & to_hstring(csr_resp.mepc) & " but retired instructions give " & to_hstring(s_mepc));
                        end if;
                        s_mie := s_mpie;
                        s_mpie := '1';
                        exp_a := unsigned(s_mepc);
                    elsif decode_o.flow_ctrl = FL_COND then
                        exp_two := true;
                        exp_b := unsigned(pc) + unsigned(decode_o.imm_value);
                    elsif decode_o.flow_ctrl = FL_JUMP and decode_o.flow_target = TRGT_REL then
                        exp_a := unsigned(pc) + unsigned(decode_o.imm_value);
                    elsif decode_o.flow_ctrl = FL_JUMP and decode_o.flow_target = TRGT_MEM then
                        exp_ok := false;
                    end if;
                end if;
                work.rvtrap_pkg.mon_errors  <= errors;
                work.rvtrap_pkg.mon_retired <= n_retire;
                work.rvtrap_pkg.mon_ecalls  <= n_ecall;
                work.rvtrap_pkg.mon_irqs    <= n_irqtrap;
                work.rvtrap_pkg.mon_mrets   <= n_mret;
            end if;
        end if;
    end process;
