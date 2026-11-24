library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
library work;
use work.mpkg.all;

entity msys is
port (
    clock    : in  std_logic;
    reset    : in  std_logic;
    dev_we   : out std_logic;
    dev_re   : out std_logic;
    dev_addr : out std_logic_vector(7 downto 0);
    dev_wdata: out std_logic_vector(7 downto 0);
    dev_rdata: in  std_logic_vector(7 downto 0);
    ctl_out  : out std_logic_vector(7 downto 0) );
end entity;

architecture rtl of msys is
    type t_region is (r_none, r_page, r_dev);
    signal io_req    : t_io_req;
    signal io_resp   : t_io_resp;
    signal region    : t_region;
    signal region_r  : t_region := r_none;
    signal off_r     : unsigned(7 downto 0) := (others => '0');
    signal ctl       : std_logic_vector(7 downto 0) := X"00";
    signal fixed     : std_logic_vector(7 downto 0);
begin
    process(io_req)
    begin
        region <= r_none;
        if io_req.address(15 downto 8) = X"00" then
            region <= r_page;
        elsif io_req.address(15 downto 8) = X"01" then
            region <= r_dev;
        end if;
    end process;

    dev_we    <= io_req.write when region = r_dev else '0';
    dev_re    <= io_req.read when region = r_dev else '0';
    dev_addr  <= std_logic_vector(io_req.address(7 downto 0));
    dev_wdata <= io_req.data;
    ctl_out   <= ctl;

    process(clock)
    begin
        if rising_edge(clock) then
            if io_req.read = '1' or io_req.write = '1' then
                region_r <= region;
                off_r    <= io_req.address(7 downto 0);
            end if;
            if io_req.write = '1' and region = r_page then
                case to_integer(io_req.address(7 downto 0)) is
                when 0 => ctl <= io_req.data;
                when others => null;
                end case;
            end if;
        end if;
    end process;

    process(region_r, off_r, ctl, dev_rdata)
    begin
        fixed <= X"00";
        case region_r is
        when r_page =>
            if off_r = X"00" then
                fixed <= ctl;
            elsif off_r = X"01" then
                fixed <= X"5A";
            end if;
        when r_dev =>
            fixed <= dev_rdata;
        when others => null;
        end case;
    end process;

    process(region_r, fixed)
    begin
        io_resp <= c_io_resp_init;
        io_resp.data <= fixed;
    end process;
end architecture;
