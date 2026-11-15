#!/usr/bin/env python3
"""xemu_regs.py — start the program in headless xemu, wait, print the CPU's
registers a few times and some memory (debugging a hang).

Usage: python3 tools/xemu_regs.py program.prg disk.d81 SECONDS [hexaddr ...]
"""
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xemu_run  # noqa: E402

prg, disk, secs = sys.argv[1], sys.argv[2], float(sys.argv[3])
tmp = tempfile.mkdtemp()
sockname = os.path.join(tmp, "umon")
png = os.path.join(tmp, "s.png")
x = subprocess.Popen(["xemu-xmega65", "-headless", "-besure", "-fastboot", "-prgmode", "65", "-uartmon", sockname,
                      "-screenshot", png, "-prg", os.path.abspath(prg), "-8", os.path.abspath(disk)],
                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
for i in range(100):
    try:
        s.connect(sockname)
        break
    except OSError:
        time.sleep(0.1)
s.settimeout(0.3)
time.sleep(secs)
for k in range(3):
    out = xemu_run.monitor(s, "r").splitlines()
    print([l for l in out if l.strip()][-3])
    time.sleep(0.3)
for a in sys.argv[4:]:
    print(a, xemu_run.peek(s, int(a, 16)).hex(" "))
x.send_signal(signal.SIGTERM)
x.wait()
if len(sys.argv) > 4 or True:
    os.replace(png, "build/regs.png") if os.path.exists(png) else None
