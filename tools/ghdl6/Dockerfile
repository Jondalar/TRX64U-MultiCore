# T116: GHDL 6.0.0 (LLVM back end) for tools/zxnext_gen.py. Debian trixie's GHDL 5.0.1 splits the Z80N's register file into memories of 3, 2 and 3
# bits and writes the read data of the three with the wrong widths (found with the boot ROM: LD BC,0x4000 read back as 0x0000); the generator
# records the GHDL version in gen/zxnext.vhd.sha256's neighbour check and refuses another one.
#   container build -t trx-ghdl6 tools/ghdl6        then:   ZXN_GHDL_IMAGE=trx-ghdl6 python3 tools/zxnext_gen.py --ghdl
FROM debian:trixie-slim
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends gnat gcc g++ make clang llvm-dev libz-dev git ca-certificates >/dev/null \
 && git clone -q --depth 1 --branch v6.0.0 https://github.com/ghdl/ghdl /src \
 && cd /src && ./configure --prefix=/usr/local --with-llvm-config >/dev/null && make -j4 >/dev/null && make install >/dev/null \
 && rm -rf /src && ghdl --version | head -1
