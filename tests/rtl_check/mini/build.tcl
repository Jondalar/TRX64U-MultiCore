read_vhdl -library work [list mpkg.vhd msys.vhd]
read_verilog -sv [list top.v mio.v]
read_vhdl -library work [list ultisid_stub.vhd]
