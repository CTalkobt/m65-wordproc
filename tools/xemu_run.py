#!/usr/bin/env python3
"""xemu_run.py — run the word processor in headless xemu, type keys, and
save a screenshot (after M65Compiler's examples/capture_screens.py).

Usage:
    python3 tools/xemu_run.py program.prg disk.d81 out.png [step ...]

Each step, in order, after the program has started:
    wait:SECONDS        let it run
    keys:CODES          PETSCII codes (decimal, comma separated) put in the
                        KERNAL's keyboard buffer ($02B0, its count at $D0),
                        up to ten at a time; it waits until they are read
    text:STRING         the string's characters as GET gives them (letters:
                        lower case $41-$5A, capitals $C1-$DA; "|" is RETURN)
    peek:ADDR:N         print N bytes at the 28-bit address (hex)
    mon:CMD             a monitor command, its answer printed (e.g. mon:r,
                        the CPU's registers)
    poke:ADDR:B,B,...   write bytes (hex) there; e.g. poke:ffd3615:3a,2 holds
                        CTRL and cursor right (the virtual keys $D615/$D616:
                        matrix positions; 7f: none)

The screenshot is xemu's -screenshot, written when xemu exits.
"""
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time


def monitor(sock, cmd):
    sock.sendall((cmd + "\r").encode())
    data = b""
    end = time.time() + 2
    while time.time() < end:
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            break
        if not chunk:
            break
        data += chunk
        if data.rstrip().endswith(b"."):
            break
    return data.decode(errors="replace")


def peek(sock, addr, n=16):
    out = monitor(sock, "m%x" % addr)
    for line in out.splitlines():
        line = line.strip()
        if line.startswith(":"):
            parts = line[1:].split(":", 1)
            if len(parts) == 2 and int(parts[0], 16) == addr:
                return bytes.fromhex(parts[1].strip()[:2 * n])
    return None


def petscii(ch):
    if ch == "|":
        return 13
    o = ord(ch)
    if "a" <= ch <= "z":
        return o - 32
    if "A" <= ch <= "Z":
        return o + 128
    return o


def put_keys(sock, codes):
    while codes:
        chunk, codes = codes[:10], codes[10:]
        deadline = time.time() + 20
        while time.time() < deadline:
            n = peek(sock, 0xD0, 1)
            if n is not None and n[0] == 0:
                break
            time.sleep(0.05)
        monitor(sock, "s2b0 " + " ".join("%x" % c for c in chunk))
        monitor(sock, "sd0 %x" % len(chunk))
    deadline = time.time() + 30
    while time.time() < deadline:
        n = peek(sock, 0xD0, 1)
        if n is not None and n[0] == 0:
            return
        time.sleep(0.05)
    print("keys: the buffer was not emptied")


def main():
    prg, disk, png = sys.argv[1:4]
    steps = sys.argv[4:]
    data = open(prg, "rb").read()
    addr = data[0] | data[1] << 8
    want = data[2:18]
    png = os.path.abspath(png)
    if os.path.exists(png):
        os.remove(png)
    tmp = tempfile.mkdtemp()
    sockname = os.path.join(tmp, "umon")
    xemu = subprocess.Popen(
        ["xemu-xmega65", "-headless", "-besure", "-fastboot", "-prgmode", "65",
         "-uartmon", sockname, "-screenshot", png, "-prg", os.path.abspath(prg),
         "-8", os.path.abspath(disk)],
        stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        sock = None
        deadline = time.time() + 30
        while time.time() < deadline and sock is None:
            try:
                s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                s.connect(sockname)
                s.settimeout(0.3)
                sock = s
            except OSError:
                time.sleep(0.1)
        if sock is None:
            sys.exit("no monitor")
        while time.time() < deadline:
            if peek(sock, addr) == want:
                break
            time.sleep(0.05)
        else:
            sys.exit("the program never appeared at $%04X" % addr)
        for st in steps:
            kind, _, arg = st.partition(":")
            t0 = time.time()
            if kind == "wait":
                time.sleep(float(arg))
            elif kind == "keys":
                put_keys(sock, [int(c) for c in arg.split(",") if c])
            elif kind == "text":
                put_keys(sock, [petscii(c) for c in arg])
            elif kind == "poke":
                # poke:ADDR:B,B,...  (28-bit addresses reach the I/O: ffd3615 = $D615)
                a, _, bs = arg.partition(":")
                monitor(sock, "s%s %s" % (a, " ".join(bs.split(","))))
            elif kind == "mon":
                print(monitor(sock, arg).strip())
            elif kind == "peek":
                a, _, n = arg.partition(":")
                b = peek(sock, int(a, 16), int(n or "16"))
                print("peek %s: %s" % (a, b.hex(" ") if b else None))
            print("%-30s %.2fs" % (st[:30], time.time() - t0))
        sock.close()
        xemu.send_signal(signal.SIGTERM)
        xemu.wait(timeout=15)
    finally:
        if xemu.poll() is None:
            xemu.kill()
        try:
            os.remove(sockname)
            os.rmdir(tmp)
        except OSError:
            pass
    ok = os.path.exists(png) and os.path.getsize(png) > 0
    print(png if ok else "no screenshot")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
