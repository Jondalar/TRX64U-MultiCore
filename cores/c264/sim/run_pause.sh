#!/bin/sh
# T90 S6, LOCAL ONLY: Freeze bench, see tb_c264_pause.v.   sh run_pause.sh KERNAL.bin BASIC.bin [MS=600]
here=$(cd "$(dirname "$0")" && pwd)
TB=tb_c264_pause MS=${MS:-600} exec sh "$here/run_boot.sh" "$1" "$2"
