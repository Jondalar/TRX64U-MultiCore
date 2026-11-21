-- T248 S2, S3: the carry-select s816_addsubbcd (DUT, cores/c64scpu/cpu) against the upstream adder (s816_addsubbcd_ref, made by tools/scpu_vendor816.py --ref).
library IEEE;
use IEEE.std_logic_1164.all;
use IEEE.numeric_std.all;

entity tb_addsub_equiv is
end tb_addsub_equiv;

architecture sim of tb_addsub_equiv is
	signal A, B, S, Sr : std_logic_vector(15 downto 0) := (others => '0');
	signal CI, ADD, BCD, w16 : std_logic := '0';
	signal CO, VO, COr, VOr : std_logic;
	constant S3_CASES : natural := 4194304;
begin

	dut : entity work.s816_addsubbcd port map (A => A, B => B, CI => CI, ADD => ADD, BCD => BCD, w16 => w16, S => S, CO => CO, VO => VO);
	ref : entity work.s816_addsubbcd_ref port map (A => A, B => B, CI => CI, ADD => ADD, BCD => BCD, w16 => w16, S => Sr, CO => COr, VO => VOr);

	process
		variable lfsr : unsigned(31 downto 0) := x"A5C3_1E77";
		variable bad  : natural := 0;
		variable n    : natural;

		procedure step is
			variable fb : std_logic;
		begin
			for k in 1 to 32 loop		-- a full 32 bits per draw, taps 32 22 2 1 (maximal)
				fb := lfsr(31) xor lfsr(21) xor lfsr(1) xor lfsr(0);
				lfsr := lfsr(30 downto 0) & fb;
			end loop;
		end procedure;

		procedure check(tag : string) is
		begin
			wait for 1 ns;
			if S /= Sr or CO /= COr or VO /= VOr then
				bad := bad + 1;
				if bad <= 10 then
					report tag & " MISMATCH A=" & to_hstring(A) & " B=" & to_hstring(B) & " CI=" & std_logic'image(CI) & " ADD=" & std_logic'image(ADD) &
						" BCD=" & std_logic'image(BCD) & " w16=" & std_logic'image(w16) & " S=" & to_hstring(S) & "/" & to_hstring(Sr) &
						" CO=" & std_logic'image(CO) & "/" & std_logic'image(COr) & " VO=" & std_logic'image(VO) & "/" & std_logic'image(VOr) severity error;
				end if;
			end if;
		end procedure;
	begin
		-- S2: w16 = 0, every A(7:0), B(7:0), CI, ADD, BCD; the upper bytes from the LFSR
		w16 <= '0';
		n := 0;
		for v in 0 to 524287 loop
			step;
			A <= std_logic_vector(lfsr(15 downto 8)) & std_logic_vector(to_unsigned(v mod 256, 8));
			B <= std_logic_vector(lfsr(31 downto 24)) & std_logic_vector(to_unsigned((v / 256) mod 256, 8));
			CI  <= std_logic(to_unsigned(v / 65536, 3)(0));
			ADD <= std_logic(to_unsigned(v / 65536, 3)(1));
			BCD <= std_logic(to_unsigned(v / 65536, 3)(2));
			check("S2");
			n := n + 1;
		end loop;
		if bad = 0 then
			report "S2 PASS " & integer'image(n) & "/" & integer'image(n);
		else
			report "S2 FAIL " & integer'image(bad) & " mismatches of " & integer'image(n) severity error;
		end if;

		-- S3: w16 = 1, everything from the LFSR
		w16 <= '1';
		bad := 0;
		n := 0;
		for v in 1 to S3_CASES loop
			step;
			A <= std_logic_vector(lfsr(15 downto 0));
			B <= std_logic_vector(lfsr(31 downto 16));
			step;
			CI  <= lfsr(3);
			ADD <= lfsr(9);
			BCD <= lfsr(17);
			check("S3");
			n := n + 1;
		end loop;
		if bad = 0 then
			report "S3 PASS " & integer'image(n) & "/" & integer'image(n);
		else
			report "S3 FAIL " & integer'image(bad) & " mismatches of " & integer'image(n) severity error;
		end if;
		wait;
	end process;

end sim;
