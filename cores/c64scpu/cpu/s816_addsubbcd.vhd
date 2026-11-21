-- T182 step 2: own copy of cores/snes/rtl/65C816/AddSubBCD.vhd (MiSTer SNES, GPL-3.0), made by tools/scpu_vendor816.py - do not edit, change the script.
library IEEE;
use IEEE.std_logic_1164.all;
use ieee.numeric_std.all;
library work;
use work.s816_pkg.all;

entity s816_addsubbcd is
	port( 
		A		: in std_logic_vector(15 downto 0); 
		B		: in std_logic_vector(15 downto 0); 
		CI		: in std_logic;
		ADD	: in std_logic;
		BCD	: in std_logic; 
		w16	: in std_logic; 
		S		: out std_logic_vector(15 downto 0);
		CO		: out std_logic;
		VO		: out std_logic
    );
end s816_addsubbcd;

architecture rtl of s816_addsubbcd is

	-- T248: carry select. One nibble of s816_bcdadder (binary sum, decimal correction) for a given carry in, with `+` so that the carry chain is used.
	type nib_t is record
		S  : std_logic_vector(3 downto 0);
		CO : std_logic;
		VO : std_logic;
	end record;
	type nib_a is array(0 to 3) of nib_t;

	function nibble(A, B : std_logic_vector(3 downto 0); c, ADD, BCD : std_logic) return nib_t is
		variable B2     : std_logic_vector(3 downto 0);
		variable BIN    : unsigned(4 downto 0);
		variable BIN_S  : std_logic_vector(3 downto 0);
		variable BIN_CO : std_logic;
		variable BCD_CO : std_logic;
		variable BCD_B  : std_logic_vector(3 downto 0);
		variable CIN    : natural range 0 to 1;
		variable CC     : natural range 0 to 1;
		variable CORR   : unsigned(3 downto 0);
		variable r      : nib_t;
	begin
		B2 := B xor (3 downto 0 => not ADD);
		CIN := 0;
		if c = '1' then CIN := 1; end if;
		BIN := ('0' & unsigned(A)) + ('0' & unsigned(B2)) + CIN;
		BIN_S := std_logic_vector(BIN(3 downto 0));
		BIN_CO := BIN(4);
		BCD_CO := (((BIN_S(3) and BIN_S(2)) or (BIN_S(3) and BIN_S(1))) and ADD) or (not (BIN_CO xor ADD));
		BCD_B := not ADD & ((BCD_CO and BCD) xor not ADD) & ((BCD_CO and BCD) xor not ADD) & not ADD;
		CC := 0;
		if ADD = '0' then CC := 1; end if;
		CORR := unsigned(BIN_S) + unsigned(BCD_B) + CC;
		r.S := std_logic_vector(CORR);
		if BCD = '0' then
			r.CO := BIN_CO;
		else
			r.CO := BCD_CO xor not ADD;
		end if;
		r.VO := (not (A(3) xor B2(3))) and (A(3) xor BIN_S(3));
		return r;
	end function;

	signal n0, n1 : nib_a;			-- every nibble for carry in 0 and for carry in 1
	signal cy     : std_logic_vector(4 downto 0);	-- cy(i) = the real carry into nibble i
	signal VOn    : std_logic_vector(3 downto 0);

begin

	gn : for i in 0 to 3 generate
		n0(i) <= nibble(A(4*i+3 downto 4*i), B(4*i+3 downto 4*i), '0', ADD, BCD);
		n1(i) <= nibble(A(4*i+3 downto 4*i), B(4*i+3 downto 4*i), '1', ADD, BCD);
		S(4*i+3 downto 4*i) <= n1(i).S when cy(i) = '1' else n0(i).S;
		cy(i+1)             <= n1(i).CO when cy(i) = '1' else n0(i).CO;
		VOn(i)              <= n1(i).VO when cy(i) = '1' else n0(i).VO;
	end generate;
	cy(0) <= CI;

	VO <= VOn(1) when w16 = '0' else VOn(3);
	CO <= cy(2) when w16 = '0' else cy(4);

end rtl;
