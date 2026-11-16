#!/bin/sh
# T216 S1/S2/S14/S15: the keyboard matrices (positional against VICE, symbolic against the PET key legends) and the pad keys, without the machine.
set -e
cd "$(dirname "$0")/.."
python3 sim/gen_keys.py | cmp -s - rtl/trx/pet_keys.v || { echo "pet_keys.v is not what gen_keys.py writes"; exit 1; }
python3 sim/gen_key_expect.py | cmp -s - sim/keys_expect.vh || { echo "keys_expect.vh is not what gen_key_expect.py writes"; exit 1; }
iverilog -g2012 -I sim -o /tmp/tb_pet_keys.vvp sim/tb_pet_keys.v rtl/trx/pet_keys.v rtl/trx/pet_padkeys.v
out=$(timeout 600 vvp /tmp/tb_pet_keys.vvp)
echo "$out" | tail -8
echo "$out" | grep -q '^PASS tb_pet_keys'
