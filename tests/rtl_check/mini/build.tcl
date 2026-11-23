read_vhdl -library work [list mpkg.vhd msys.vhd]
read_verilog -sv [list top.v mio.v]
read_verilog -sv [list ultisid_stub.v]
