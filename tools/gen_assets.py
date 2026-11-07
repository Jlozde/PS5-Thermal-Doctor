#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Turns a file into a C array (web/index.html -> gen/web_index.c).
Bir dosyayı C dizisine çevirir.

Usage / kullanım: gen_assets.py <input> <output.c> <symbol>
"""
import sys


def main():
    if len(sys.argv) != 4:
        print("usage / kullanım: gen_assets.py <input> <output.c> <symbol>", file=sys.stderr)
        return 2
    src, dst, sym = sys.argv[1:4]
    with open(src, "rb") as f:
        data = f.read()
    with open(dst, "w", encoding="utf-8") as f:
        f.write("/* generated from %s (%d bytes), do not edit */\n" % (src.replace("\\", "/"), len(data)))
        f.write("const unsigned char %s[] = {\n" % sym)
        for i in range(0, len(data), 16):
            f.write("  " + ",".join("0x%02x" % b for b in data[i:i + 16]) + ",\n")
        f.write("  0x00\n};\n")
        f.write("const unsigned int %s_len = %du;\n" % (sym, len(data)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
