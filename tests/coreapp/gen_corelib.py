#!/usr/bin/env python3
"""Fixtures of test_corelib.cc (T239.3 base 3.3, a core whose code is a library, core_hooks.h): the description records of a core that names its hooks library and one that does not,
made by the real tool (tools/descrec.py). Base 4.0 loads no library file: the model libraries are linked by the test through LibLoader::link.

    gen_corelib.py OUTDIR REPO_ROOT
"""
import os
import sys

out, root = sys.argv[1], sys.argv[2]
sys.path.insert(0, os.path.join(root, "tools"))
import descrec  # noqa: E402

os.makedirs(out, exist_ok=True)

CORE = '''[core]
id = "CLIB"
name = "Model Core"
title = "Model Core"
short_title = "MODEL"
folder = "modelcore"
store = "Model Core Settings"
reset_ms = 1
%s
[mount]
names = ["D1", "Cart"]
'''


def write(name, data):
    with open(os.path.join(out, name), "wb") as f:
        f.write(data)


rec = descrec.compile_text(CORE % 'hooks = "cmodel"')
write("desc.rec", rec)
write("desc_nohooks.rec", descrec.compile_text(CORE % ""))
# the reader's limits on the name: the writer refuses a bad name, so the bad records are made of a good one by changing the name's bytes in the string table
assert rec.count(b"cmodel\0") == 1
write("desc_upper.rec", rec.replace(b"cmodel\0", b"cModel\0"))
write("desc_empty.rec", rec.replace(b"cmodel\0", b"\0model\0"))
for bad in ('hooks = "CModel"', 'hooks = ""', 'hooks = "0123456789abcdef"', 'hooks = 3'):
    try:
        descrec.compile_text(CORE % bad)
    except descrec.DescError:
        continue
    raise SystemExit("descrec.py accepted %s" % bad)
write("desc_long.rec", descrec.compile_text(CORE % 'hooks = "0123456789abcde"'))      # 15 characters: the longest
