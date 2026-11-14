#!/usr/bin/env python3
"""T215 self-check of ci/code_hash.py (docs/specs/T215-ci-hash-code-only.md section 2). python3 ci/test_code_hash.py; exit 1 on any failure."""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import code_hash as ch

fails = []


def check(name, ok):
    print(("ok   " if ok else "FAIL ") + name)
    if not ok:
        fails.append(name)


def same(path, a, b):
    return ch.normalise(path, a.encode()) == ch.normalise(path, b.encode())


V = "module m(input a, output b);\n  assign b = a;\nendmodule\n"
# (a) comment-only changes in HDL, (b) a real change in the same file
check("a verilog: // and /* */ added, blank lines, trailing blanks",
      same("top.v", V, "// header\nmodule m(input a, output b); // port list\n\n  assign b = a;   \n  /* old:\n     assign b = ~a; */\nendmodule\n"))
check("a verilog: comment text edited", same("top.sv", "x = 1; // one\n", "x = 1; // two\n"))
check("a vhdl: -- comments", same("a.vhd", "b <= a; -- x\n", "-- new\nb <= a;\n"))
check("a vhdl: /* */ (2008)", same("a.vhd", "b <= a;\n", "b <= a; /* c */\n"))
check("b verilog: operator changed", not same("top.v", V, V.replace("assign b = a", "assign b = ~a")))
check("b verilog: constant changed", not same("top.v", "x = 4'b0101;\n", "x = 4'b0111;\n"))
check("b vhdl: signal changed", not same("a.vhd", "b <= a; -- x\n", "b <= not a; -- x\n"))
check("b verilog: indentation is code", not same("top.v", "a\n b\n", "a\nb\n"))
# (c) app sources
check("c c: comments", same("m.c", "int x = 1;\n", "/* h */\nint x = 1; // one\n"))
check("c cc: real change", not same("m.cc", "int x = 1; // a\n", "int x = 2; // a\n"))
check("c h: block comment over lines", same("m.h", "int f(void);\n", "/* a\n b */\nint f(void);\n"))
# (d) other file types
check("d tcl: whole-line #", same("b.tcl", "set a 1\n", "# c\nset a 1\n   # d\n"))
check("d tcl: # inside braces is data", not same("b.tcl", "puts {a #b}\n", "puts {a}\n"))
check("d xdc: whole-line #", same("c.xdc", "set_property A B [get_ports x]\n", "# note\nset_property A B [get_ports x]\n"))
check("d sh: # lines and trailing", same("r.sh", "echo a\n", "# c\necho a # t\n"))
check("d sh: comment edit", same("r.sh", "echo a # one\n", "echo a # two\n"))
check("d sh: $# and ${#a} are code", not same("r.sh", 'echo $#\n', "echo $\n") and not same("r.sh", "echo ${#a}\n", "echo ${a}\n"))
check("d sh: shebang is kept", not same("r.sh", "#!/bin/bash\n", "#!/bin/sh\n"))
check("d py: comments", same("t.py", "x = 1\n", "# c\nx = 1  # t\n"))
check("d py: docstring is code", not same("t.py", 'def f():\n    """a"""\n', 'def f():\n    """b"""\n'))
check("d py: # inside a triple-quoted block is kept", not same("t.py", 'x = """\n# a\n"""\n', 'x = """\n# b\n"""\n'))
check("d toml: # comment", same("core.toml", 'id = "a"\n', '# c\nid = "a" # t\n'))
check("d toml: real change", not same("core.toml", 'id = "a"\n', 'id = "b"\n'))
check("d unknown type is raw", not same("layers.txt", "a\n", "a # c\n"))
check("d md is raw", not same("x.md", "a\n", "a\n\n"))
# (e) markers inside literals
check("e c: // in a string", not same("m.c", 'char *u = "http://a/b";\n', 'char *u = "http:";\n'))
check("e c: /* in a string", not same("m.c", 'char *s = "/* x";\n', 'char *s = "";\n'))
check("e c: escaped quote", not same("m.c", 'char *s = "a\\"//b";\n', 'char *s = "a\\"";\n'))
check("e c: '\"' char literal does not open a string", same("m.c", "c = '\"'; // t\nx = 1;\n", "c = '\"';\nx = 1;\n"))
check("e c: '/' char literal", not same("m.c", "c = '/'; d = '/';\n", "c = '/'; d = ' ';\n"))
check("e verilog: // in a string", not same("t.v", '$display("a//b");\n', '$display("a");\n'))
check("e verilog: 4'b is not a char literal", not same("t.v", "x = 4'b01; // a\ny = 4'b10;\n", "x = 4'b01;\ny = 4'b11;\n"))
check("e vhdl: -- in a string", not same("a.vhd", 'report "a--b";\n', 'report "a";\n'))
check("e vhdl: doubled quote", not same("a.vhd", 'report "a"" --b";\n', 'report "a"" ";\n'))
check("e vhdl: '-' char literal", not same("a.vhd", "x <= '-';\n", "x <= '0';\n"))
check("e sh: # in quotes", not same("r.sh", 'echo "a # b"\n', 'echo "a "\n'))
check("e py: # in a string", not same("t.py", 'x = "a#b"\n', 'x = "a"\n'))
check("e toml: # in a string", not same("c.toml", 'u = "a#b"\n', 'u = "a"\n'))
# (f) negative control: the mutant stripper (no string knowledge) must make the cases below equal, i.e. the (e) kind of check would fail on it
cases = [("m.c", 'f("http://a", 1);\n', 'f("http://a", 2);\n'), ("t.v", '$display("a//b", x);\n', '$display("a//b", y);\n'),
         ("m.c", 'f("/* x", 1); /* c */\n', 'f("/* x", 2); /* c */\n')]
for p_, a_, b_ in cases:       # the real stripper tells them apart ...
    check("f real stripper tells %r apart" % a_.strip(), not same(p_, a_, b_))
def naive(p_, d):        # the mutant: a regex stripper that knows no strings
    return re.sub(r"//.*|/\*.*?\*/", "", d.decode(), flags=re.S).encode()
caught = sum(naive(p_, a_.encode()) == naive(p_, b_.encode()) for p_, a_, b_ in cases)
check("f mutant stripper (regex, strings unknown) fails the (e) kind of check in %d of %d cases" % (caught, len(cases)), caught == len(cases))
# (f) every single-character mutation of a code line must change the normalised text
src = 'module m(input a); assign b = a & 4\'b01; // c\n$display("x//y");\nendmodule\n'
base = ch.normalise("t.v", src.encode())
code_chars = [i for i, c in enumerate(src) if i < src.index("// c") and c.isalnum()] + [src.index("x//y")]
bad = [i for i in code_chars if ch.normalise("t.v", (src[:i] + "~" + src[i + 1:]).encode()) == base]
check("f mutation of code characters always changes the hash (%d tried)" % len(code_chars), not bad)
# (g) synthesis pragmas are semantic: never cut, moving or removing one moves the hash
VP = "a <= b;\n--synthesis translate_off\nreport \"x\";\n--synthesis translate_on\nc <= d;\n"
check("g vhdl: pragma removed", not same("a.vhd", VP, VP.replace("--synthesis translate_off\n", "")))
check("g vhdl: pragma moved", not same("a.vhd", VP, "a <= b;\n--synthesis translate_off\n--synthesis translate_on\nreport \"x\";\nc <= d;\n"))
check("g vhdl: pragma added", not same("a.vhd", "a <= b;\nc <= d;\n", VP.replace('report "x";\n', "")))
check("g vhdl: pragma text edited (off -> on)", not same("a.vhd", VP, VP.replace("translate_off", "translate_on")))
check("g vhdl: plain comment around a pragma still cut", same("a.vhd", VP, "-- hello\n" + VP.replace("c <= d;", "c <= d; -- t")))
check("g verilog: // synthesis keep", not same("t.v", "wire a; // synthesis keep\n", "wire a;\n"))
check("g verilog: // synopsys translate_off", not same("t.v", "// synopsys translate_off\nwire a;\n", "wire a;\n"))
check("g verilog: /* synthesis */ block", not same("t.v", "wire a /* synthesis keep */;\n", "wire a;\n"))
check("g verilog: case-insensitive", not same("t.v", "// SYNTHESIS translate_off\nwire a;\n", "wire a;\n"))
check("g verilog: plain comment still cut", same("t.v", "wire a; // fanout\n", "wire a;\n"))
check("g c: #pragma stays code, // pragma-comment kept", not same("m.c", "int x; // pragma foo\n", "int x;\n"))
if os.path.exists(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "cores/c264/rtl/src/t65/T65_ALU.vhd")):
    alu = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "cores/c264/rtl/src/t65/T65_ALU.vhd"), "rb").read().decode("latin-1")
    check("g T65_ALU.vhd: a translate_off line removed moves the hash", not same("T65_ALU.vhd", alu, re.sub(r"--synthesis translate_off\r?\n", "", alu, count=1)))
    check("g T65_ALU.vhd: a translate_on line removed moves the hash", not same("T65_ALU.vhd", alu, re.sub(r"--synthesis translate_on\r?\n", "", alu, count=1)))
    check("g T65_ALU.vhd: a plain comment line added is the same", same("T65_ALU.vhd", alu, "-- a new remark\n" + alu))
# (h) whitespace inside multi-line literals is data
check("h sh: blank line in a heredoc", not same("r.sh", "cat <<E\na\n\nb\nE\n", "cat <<E\na\nb\nE\n"))
check("h sh: trailing blank in a heredoc", not same("r.sh", "cat <<E\na  \nE\n", "cat <<E\na\nE\n"))
check("h py: blank line in a triple-quoted string", not same("t.py", 'x = """a\n\nb"""\n', 'x = """a\nb"""\n'))
check("h py: trailing blank in a triple-quoted string", not same("t.py", 'x = """a  \nb"""\n', 'x = """a\nb"""\n'))
check("h toml: blank line in a multi-line string", not same("c.toml", 'a = """x\n\ny"""\n', 'a = """x\ny"""\n'))
check("h sh: comment lines around a heredoc still cut", same("r.sh", "cat <<E\na\n\nb\nE\n", "# c\ncat <<E\na\n\nb\nE\n# d\n"))
check("h sh: comment next to blank lines", same("r.sh", "a\n\nb\n", "a\n# c\n\nb # t\n"))
print("%d failed" % len(fails))
sys.exit(1 if fails else 0)
