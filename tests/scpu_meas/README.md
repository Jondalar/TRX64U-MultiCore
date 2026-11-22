# T241 S4 measurement program (C64 with SuperCPU)

`meas.asm` is one board run that tells lost raster IRQs, lost jiffy-clock increments, an NTSC latch, a slow clock
and a fast CPU apart (T241 S4). Its first line tests whether a read-modify-write on `$D019` acknowledges the raster
flag, at software 1 MHz and at the speed switch's speed. The header of `meas.asm` lists every field and the values
of a PAL C64.

1. Assemble with 64tass: `64tass -o meas.prg meas.asm`. The `meas.prg` here is that output (checked byte for byte on 2026-11-22).
2. Load and RUN it on the `c64scpu` core.
3. Film the screen for about 60 s, then photograph the end screen.

`pal_end.gif` and `pal_end2.gif` are end screens of two earlier versions of the program, without the RMW line, on a
PAL C64 in the c64re runtime (2026-11-20).
