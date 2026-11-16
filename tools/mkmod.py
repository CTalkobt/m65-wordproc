#!/usr/bin/env python3
"""mkmod.py — link a Writer module (an .OVL file) against the program

    mkmod.py [--at SYM] [--ofs MACRO] [--size MACRO] [--entry FUNC]
             [--from FUNC] LOG OUT.ovl ID OBJ.o45 ...

    --at     the program's region the module runs in (_mod_area)
    --ofs    wp.h's #define of where in it (0)
    --size   wp.h's #define of the region's size (MOD_SIZE)
    --entry  the module's entry, called after the header (required)
    --from   the program functions that call the module, by commas
             (_mod_run; a static function's name has its file's suffix, and
             one cc45 inlined is not one: its callers are)

LOG is basic45 -v's log of the program's link: its ln45 command is run
again with -M for the program's map (basic45 writes none). The module is
linked to run at the program's mod_area (render.c), with:

  - a header first: "WM", the build's ID (16 bits), then the code the
    program jumps to (mod_run checks the first four bytes before it
    jumps): it saves the program's base-page bytes of the functions that
    lead to the call (--from), calls the entry, and restores them; it also
    says it uses all the register pool, so the module's base-page blocks
    start at $80;
  - the program's symbols the module's objects use, as equates (its
    functions, variables, the run time's helpers), so it calls into the
    program and brings no copy of what the program has;

and checked: it must fit in its region (--size, wp.h), and its base-page
blocks (ln45 places the most used variables of functions that make calls
in the program's base page, $80-$F1, where the program has its own) must
miss those of the program functions it calls and everything they call,
which run while the module's are in use. Should a module need
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
    image = open(out + ".prg", "rb").read()
    os.remove(out + ".prg")
    syms = {}
    for line in open(out):
        m = re.match(r"\s+\$([0-9A-F]+)\s+(\S+)", line)
        if m and m.group(2) not in syms:
            syms[m.group(2)] = int(m.group(1), 16)
    return syms, blocks(log), calls(open(out).read(), log), image


def word_at(image, a):
    """the program's 16-bit value at address a (image: a .prg)"""
    o = a - (image[0] | image[1] << 8) + 2
    return image[o] | image[o + 1] << 8


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


def macro(name):
    """wp.h's #define name: a number, or an expression of others"""
    if name.isdigit():
        return int(name)
    h = open(os.path.join(ROOT, "src", "wp.h")).read()
    m = re.search(r"#define %s\s+([^/\n]+)" % name, h)
    if not m:
        die("no #define %s in wp.h" % name)
    e = re.sub(r"[A-Za-z_]\w*", lambda w: str(macro(w.group(0))), m.group(1).strip())
    if not re.fullmatch(r"[\d\s()+*-]+", e):
        die("#define %s: %s" % (name, e))
    return eval(e)


def main():
    opt = {"--at": "_mod_area", "--ofs": "0", "--size": "MOD_SIZE", "--entry": None, "--from": "_mod_run"}
    args = sys.argv[1:]
    while args and args[0] in opt:
        if len(args) < 2:
            die(__doc__)
        opt[args[0]] = args[1]
        args = args[2:]
    if len(args) < 4:
        die(__doc__)
    log, out, ident, objs = args[0], args[1], int(args[2]), args[3:]
    at, entry, site = opt["--at"], opt["--entry"], opt["--from"]
    if not entry:
        die("--entry: the module's entry function")
    size_max = macro(opt["--size"])
    base = os.path.splitext(out)[0]
    prog, pblocks, pcalls, image = program_map(log, base + ".prog.map")
    if at not in prog:
        die("the program has no " + at)
    ofs = macro(opt["--ofs"])
    m = re.search(r"#define %s (\d+)" % opt["--ofs"], open(os.path.join(ROOT, "src", "wp.bas")).read())
    if m and int(m.group(1)) != ofs:
        die("wp.bas's %s is %s, wp.h's %d" % (opt["--ofs"], m.group(1), ofs))
    org = prog[at] + ofs
    if ofs and at == "_mod_area":
        # (past the drawing's buffers, render.c's mod_bufsize)
        used = word_at(image, prog["_mod_bufsize"])
        if used > ofs:
            die("the drawing's buffers in mod_area are %d bytes: more than %s (%d, wp.h)"
                % (used, opt["--ofs"], ofs))

    # (and the program's base page: the module's base-page variables,
    # <f>__zar, are __zp_page + their place, used also by absolute address)
    need = sorted(s for s in symbols(objs) | {"__zp_page"} if s in prog)
    # The program's base-page bytes in use while the module runs: the
    # blocks of the functions that lead to the call (site, up), which the
    # header saves and restores around the module (they are those functions'
    # variables, untouched until it returns), and of the program functions
    # the module calls and all they call (down), which run with the module's
    # own and so must not share bytes with them (checked after the link)
    down = reach(pcalls, need)
    if "ANY" in down:
        down = set(pblocks)         # (a call through a pointer: anything)
    busy = {}
    for f in down:
        for a in pblocks.get(f, ()):
            busy.setdefault(a, f)
    sites = site.split(",")
    for f in sites:
        if f not in pcalls and not any(f in v for v in pcalls.values()):
            die("--from %s: not in the program's call graph (inlined? renamed?)" % f)
    saved = set()
    for f in sites:
        for g in callers(pcalls, f):
            saved |= pblocks.get(g, set())
    lo, hi = (min(saved), max(saved)) if saved else (0, -1)
    nsave = hi + 1 - lo
    if nsave > 128:
        die("the program's blocks live at the call are %d bytes ($%02X-$%02X): more than 128" % (nsave, lo, hi))

    head = base + ".head.s45"
    with open(head, "w") as f:
        f.write('    .segment "code"\n    .extern %s\n' % entry)
        # (as using all the register pool: its base-page blocks from $80)
        f.write("    .weak cc45.bptop\n    cc45.bptop = $80\n")
        f.write("    .byte $57, $4D, $%02X, $%02X\n" % (ident & 255, ident >> 8))
        if nsave <= 0:
            f.write("    jmp %s\n" % entry)
        else:
            f.write("    ldx #%d\n__mod_save_l:\n    lda $%02X,x\n    sta __mod_save,x\n"
                    "    dex\n    bpl __mod_save_l\n" % (nsave - 1, lo))
            f.write("    jsr %s\n" % entry)
            f.write("    ldx #%d\n__mod_rest_l:\n    lda __mod_save,x\n    sta $%02X,x\n"
                    "    dex\n    bpl __mod_rest_l\n    rts\n" % (nsave - 1, lo))
            f.write('    .segment "bss"\n__mod_save:\n    .res %d\n' % nsave)
    run([os.path.join(BIN, "ca45"), "-c", head, "-o", base + ".head.o45"])

    equ = base + ".equ.s45"
    with open(equ, "w") as f:
        f.write("; the program's symbols this module uses (mkmod.py)\n")
        for s in need:
            f.write("    .global %s\n    %s = $%04X\n" % (s, s, prog[s]))
    run([os.path.join(BIN, "ca45"), "-c", equ, "-o", base + ".equ.o45"])

    lib = os.path.join(CC45_ROOT, "lib", "build", "c45.lib")
    mlog = run_log([os.path.join(BIN, "ln45"), "-vv", "-prg", "-t", "%X" % org,
                    "-M", base + ".map", "-o", out, base + ".head.o45"] + objs +
                   [base + ".equ.o45", lib])

    for f, bytes_ in sorted(blocks(mlog).items()):
        hit = sorted(bytes_ & set(busy))
        if hit:
            die("%s's base-page block ($%02X...) shares $%02X with the program's %s, "
                "which it calls: compile the module with -fno-zp-locals"
                % (f, min(bytes_), hit[0], busy[hit[0]]))

    mp = open(base + ".map").read()
    top = max(int(m.group(2), 16)
              for m in re.finditer(r"^\s+(TEXT|DATA|BSS)\s+\$[0-9A-F]+ - \$([0-9A-F]+)", mp, re.M))
    size = top + 1 - org
    if size > size_max:
        die("%s is %d bytes: more than %s (%d, wp.h)" % (out, size, opt["--size"], size_max))
    if not mp.count("$%04X" % org) or entry not in mp:
        die("no %s in %s" % (entry, out))
    if re.search(r"crt0", mp):
        die(out + " links crt0: a program symbol is missing from the equates")
    print("%s: %d bytes at $%04X (of %d), %d program symbols%s" %
          (out, size, org, size_max, len(need),
           ", base page $%02X-$%02X saved" % (lo, hi) if nsave > 0 else ""))


if __name__ == "__main__":
    main()
