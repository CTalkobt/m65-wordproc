#!/usr/bin/env python3
"""mkmsg.py — the message table (src/messages.txt) for the Writer

    mkmsg.py messages.txt writer.msg msg.h msgtab.c

writer.msg: the table as BASIC BLOADs it (a load address, MSG_TAB, then
each message in ASCII ended by a 0; @ as 1, % as 2: api.c's wp_say);
msg.h: MSG_TAB, MSG_PARM and each MSG_<name> (its number), #defines for the
BASIC program and the C half; msgtab.c: the table as a C array, for the
host build (tests, mkhelp).
"""
import re
import sys

MSG_TAB = 0x1800        # to MSG_PARM: the table
MSG_PARM = 0x1E80       # 81 bytes: what BASIC gave, while wp_say makes msg
MAX_LEN = 78            # a message's at most (the bottom bar)


def main():
    src, out_bin, out_h, out_c = sys.argv[1:5]
    names, data = [], bytearray()
    for n, line in enumerate(open(src, encoding="utf-8"), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        m = re.fullmatch(r'([A-Z][A-Z0-9]*)\s+"(.*)"', line)
        if not m:
            sys.exit("%s:%d: not NAME \"text\"" % (src, n))
        name, text = m.groups()
        if name in names:
            sys.exit("%s:%d: %s twice" % (src, n, name))
        if len(text) > MAX_LEN:
            sys.exit("%s:%d: longer than %d" % (src, n, MAX_LEN))
        names.append(name)
        data += text.replace("@", "\x01").replace("%", "\x02").encode("ascii") + b"\0"
    if len(names) > 255:
        sys.exit("%s: more than 255 messages" % src)
    if MSG_TAB + len(data) > MSG_PARM:
        sys.exit("%s: %d bytes, more than the %d there is room for"
                 % (src, len(data), MSG_PARM - MSG_TAB))
    with open(out_bin, "wb") as f:
        f.write(bytes([MSG_TAB & 255, MSG_TAB >> 8]) + data)
    # (decimal: basic45 --minify mangles $xx, M65Compiler #603)
    h = ["#define MSG_TAB %d" % MSG_TAB, "#define MSG_PARM %d" % MSG_PARM]
    h += ["#define MSG_%s %d" % (s, i) for i, s in enumerate(names)]
    with open(out_h, "w") as f:
        f.write("\n".join(h) + "\n")
    with open(out_c, "w") as f:
        f.write("/* made by tools/mkmsg.py from src/messages.txt */\n")
        f.write("const char msg_tab[%d] = {\n" % len(data))
        for i in range(0, len(data), 16):
            f.write("    " + ", ".join("%d" % b for b in data[i:i + 16]) + ",\n")
        f.write("};\n")
    print("%s: %d messages, %d bytes at $%04X (of %d)"
          % (out_bin, len(names), len(data), MSG_TAB, MSG_PARM - MSG_TAB))


main()
