#!/usr/bin/env python3
"""mkmod.py — link a Writer module (an .OVL file) against the program

    mkmod.py LOG OUT.ovl ID OBJ.o45 ...

LOG is basic45 -v's log of the program's link: its ln45 command is run
again with -M for the program's map (basic45 writes none). The module is
linked to run at the program's mod_area (render.c), with:

  - a header first: "WM", the build's ID (16 bits), JMP _mod_main
    (mod_run checks the first four bytes before it jumps); it also says it
    uses all the register pool, so the module's base-page blocks start at
    $80;
  - the program's symbols the module's objects use, as equates (its
    functions, variables, the run time's helpers), so it calls into the
    program and brings no copy of what the program has;

and checked: it must fit in MOD_SIZE (the region, wp.h), and its
base-page blocks (ln45 places the most used variables of functions that
make calls in the program's base page, $80-$F1, where the program has its
own) must miss the program's blocks that are live while it runs: those of
the functions that lead to mod_run (BASIC's SYS, wp_get), and those of the
program functions it calls and everything they call. Should a module need
those bytes, compile it with -fno-zp-locals (its locals are then all in the
module, in absolute storage: larger code).
"""
import os
import re
import shlex
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CC45_ROOT = os.environ.get("CC45_ROOT", os.path.join(ROOT, "..", "m65compiler"))
BIN = os.path.join(CC45_ROOT, "bin")


def die(msg):
    sys.exit("mkmod: " + msg)


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        die("%s failed:\n%s%s" % (os.path.basename(cmd[0]), r.stdout, r.stderr))
    return r.stdout


def program_map(log, out):
    """the program's link map, from the ln45 command in basic45's log"""
    cmd = None
    for line in open(log):
        m = re.search(r"\[basic45:INFO\]\s+(.*/ln45' .*)$", line)
        if m:
            cmd = shlex.split(m.group(1))
    if not cmd:
        die("no ln45 command in " + log + " (basic45 -v)")
    i = cmd.index("-o")
    cmd[i + 1] = out + ".prg"
    for f in cmd[1:]:
        if f.endswith(".o45") and not os.path.exists(f):
            die("%s is gone (basic45's link directory, kept by BASIC45_KEEP_TMP): link again" % f)
    log = run_log(cmd[:1] + ["-vv", "-M", out] + cmd[1:])
    os.remove(out + ".prg")
    syms = {}
    for line in open(out):
        m = re.match(r"\s+\$([0-9A-F]+)\s+(\S+)", line)
        if m and m.group(2) not in syms:
            syms[m.group(2)] = int(m.group(1), 16)
    return syms, blocks(log), calls(open(out).read(), log)


def run_log(cmd):
    """run a link with -vv: its log (stderr)"""
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        die("%s failed:\n%s%s" % (os.path.basename(cmd[0]), r.stdout, r.stderr))
    return r.stderr


def blocks(log):
    """function: the base-page bytes of its block (ln45 -vv)"""
    b = {}
    for m in re.finditer(r"base-page block: (\S+) at \$([0-9a-fA-F]+) len (\d+)", log):
        a = int(m.group(2), 16)
        b[m.group(1)] = set(range(a, a + int(m.group(3))))
    return b


def calls(mp, log):
    """function: the functions it calls (the map's call graph); ANY: through
    a pointer (ln45 -vv), which reaches every function"""
    g = {}
    for m in re.finditer(r"^\s+(\S+) -> (.*)$", mp, re.M):
        g.setdefault(m.group(1), set()).update(m.group(2).split())
    for m in re.finditer(r"call graph: (\S+) calls through any pointer", log):
        g.setdefault(m.group(1), set()).add("ANY")
    return g


def reach(g, start):
    seen, todo = set(), list(start)
    while todo:
        f = todo.pop()
        if f not in seen:
            seen.add(f)
            todo.extend(g.get(f, ()))
    return seen


def callers(g, f):
    up = {}
    for a, bs in g.items():
        for b in bs:
            up.setdefault(b, set()).add(a)
    return reach(up, [f])


def symbols(objs):
    used, defined = set(), set()
    for o in objs:
        for line in run([os.path.join(BIN, "nm45"), o]).splitlines():
            t = line.split()
            if len(t) >= 2:
                (used if t[-2] == "U" else defined).add(t[-1])
    return used - defined


def mod_size():
    h = open(os.path.join(ROOT, "src", "wp.h")).read()
    return int(re.search(r"#define MOD_SIZE (\d+)", h).group(1))


def main():
    if len(sys.argv) < 5:
        die(__doc__)
    log, out, ident, objs = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4:]
    base = os.path.splitext(out)[0]
    prog, pblocks, pcalls = program_map(log, base + ".prog.map")
    if "_mod_area" not in prog:
        die("the program has no mod_area")
    org = prog["_mod_area"]

    head = base + ".head.s45"
    with open(head, "w") as f:
        f.write('    .segment "code"\n    .extern _mod_main\n')
        # (as using all the register pool: ln45 then puts the module's
        # base-page blocks from $80, above the program's that are live)
        f.write("    .weak cc45.bptop\n    cc45.bptop = $80\n")
        f.write("    .byte $57, $4D, $%02X, $%02X\n" % (ident & 255, ident >> 8))
        f.write("    jmp _mod_main\n")
    run([os.path.join(BIN, "ca45"), "-c", head, "-o", base + ".head.o45"])

    equ = base + ".equ.s45"
    # (and the program's base page: the module's base-page variables,
    # <f>__zar, are __zp_page + their place, used also by absolute address)
    need = sorted(s for s in symbols(objs) | {"__zp_page"} if s in prog)
    with open(equ, "w") as f:
        f.write("; the program's symbols this module uses (mkmod.py)\n")
        for s in need:
            f.write("    .global %s\n    %s = $%04X\n" % (s, s, prog[s]))
    run([os.path.join(BIN, "ca45"), "-c", equ, "-o", base + ".equ.o45"])

    lib = os.path.join(CC45_ROOT, "lib", "build", "c45.lib")
    mlog = run_log([os.path.join(BIN, "ln45"), "-vv", "-prg", "-t", "%X" % org,
                    "-M", base + ".map", "-o", out, base + ".head.o45"] + objs +
                   [base + ".equ.o45", lib])

    # the program's base-page bytes live while the module runs
    down = reach(pcalls, need)
    if "ANY" in down:
        down = set(pblocks)         # (a call through a pointer: anything)
    live = callers(pcalls, "_mod_run") | down
    busy = {}
    for f in live:
        for a in pblocks.get(f, ()):
            busy.setdefault(a, f)
    for f, bytes_ in sorted(blocks(mlog).items()):
        hit = sorted(bytes_ & set(busy))
        if hit:
            die("%s's base-page block ($%02X...) shares $%02X with the program's %s, "
                "live while the module runs: compile the module with -fno-zp-locals"
                % (f, min(bytes_), hit[0], busy[hit[0]]))

    mp = open(base + ".map").read()
    top = max(int(m.group(2), 16)
              for m in re.finditer(r"^\s+(TEXT|DATA|BSS)\s+\$[0-9A-F]+ - \$([0-9A-F]+)", mp, re.M))
    size = top + 1 - org
    if size > mod_size():
        die("%s is %d bytes: more than MOD_SIZE (%d, wp.h)" % (out, size, mod_size()))
    if not mp.count("$%04X" % org) or "_mod_main" not in mp:
        die("no _mod_main in " + out)
    if re.search(r"crt0", mp):
        die(out + " links crt0: a program symbol is missing from the equates")
    print("%s: %d bytes at $%04X (of %d), %d program symbols" %
          (out, size, org, mod_size(), len(need)))


if __name__ == "__main__":
    main()
