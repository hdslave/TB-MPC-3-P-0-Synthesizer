#!/usr/bin/env python3
"""Writes src/scales.h: the Ornament & Crime firmware's quantizer scales, which TB-3PO picks its notes from.

    python3 tools/gen_scales.py

Reads tools/oc-source/: the firmware's braids_quantizer_scales.h, unchanged (each scale's span, note count and notes in
1/128 semitone; MIT licensed), and scale_names.txt (the names its menus show, in its order). The firmware's list starts with four user scales (copies of Semitone until edited on the module) and Off;
the plug-in starts at Semitone. A scale's notes past its count are left out, as the firmware's quantizer ignores
them (one table row lists 15 values for 14 notes).
"""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE = os.path.join(HERE, "oc-source")
OUT = os.path.join(HERE, "..", "src", "scales.h")


def scales():
    text = open(os.path.join(SOURCE, "braids_quantizer_scales.h"), encoding="utf-8").read()
    start = text.index("const Scale scales[] = {") + len("const Scale scales[] = {")
    body = re.sub(r"//[^\n]*", "", text[start:text.index("} ;", start)])
    rows = []
    for span, count, notes in re.findall(r"\{\s*([^,{}]+?)\s*,\s*(\d+)\s*,\s*\{([^}]*)\}\s*\}", body):
        span = eval(span, {"__builtins__": {}})  # "12 << 7" or "0"
        values = [int(v) for v in notes.split(",") if v.strip()]
        rows.append((span, int(count), values[: int(count)]))
    return rows


def names():
    long_names, short_names = [], []
    for line in open(os.path.join(SOURCE, "scale_names.txt"), encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        long_name, short_name = line.rstrip("\n").split("\t")
        long_names.append(long_name)
        short_names.append(short_name)
    return long_names, short_names


def main():
    rows = scales()
    long_names, short_names = names()
    assert len(long_names) == len(short_names) == 4 + len(rows), (len(long_names), len(rows))
    assert rows[0] == (0, 0, []) and long_names[4].strip() == "Off" and long_names[5] == "Semitone"
    rows, long_names, short_names = rows[1:], long_names[5:], short_names[5:]
    for span, count, notes in rows:
        assert span == 1536 and 4 <= count <= 16 and len(notes) == count and notes == sorted(notes)

    out = []
    out.append("// The quantizer scales of the Ornament & Crime firmware (Benisphere Suite), from Semitone on: written by")
    out.append("// tools/gen_scales.py from tools/oc-source/; do not edit. The firmware's scale N is row N - 5 here (its four")
    out.append("// user scales and Off are left out).")
    out.append("//")
    out.append("// The scale table: Copyright 2015 Emilie Gillet; MIT license, see")
    out.append("// licenses/MIT-oc-firmware.txt.")
    out.append("#pragma once")
    out.append("")
    out.append("#include <cstddef>")
    out.append("#include <cstdint>")
    out.append("")
    out.append("namespace tbmpc")
    out.append("{")
    out.append("struct ScaleDef")
    out.append("{")
    out.append("    int16_t span;   // an octave, in 1/128 semitone")
    out.append("    uint8_t count;  // notes")
    out.append("    int16_t notes[16];")
    out.append("};")
    out.append("")
    out.append("inline constexpr ScaleDef scaleTable[] = {")
    for (span, count, notes), name in zip(rows, long_names):
        out.append("    { %d, %d, { %s } }, // %s" % (span, count, ", ".join(str(n) for n in notes), name.strip()))
    out.append("};")
    out.append("constexpr int scaleCount = (int) (sizeof (scaleTable) / sizeof (scaleTable[0]));")
    out.append("")
    out.append("// The names the firmware's menus show, long and short (the short ones are the applet's).")
    out.append("inline const char* const scaleNames[] = {")
    for name in long_names:
        out.append('    "%s",' % name.strip())
    out.append("};")
    out.append("inline const char* const scaleShortNames[] = {")
    for name in short_names:
        out.append('    "%s",' % name.strip())
    out.append("};")
    out.append("static_assert (sizeof (scaleNames) / sizeof (scaleNames[0]) == (std::size_t) scaleCount, \"a name a scale\");")
    out.append("")
    gunakri = long_names.index("Gunakri")
    out.append("// The applet's own first scale (its Start() picks the firmware's scale 29, Gunakri).")
    out.append("constexpr int appletDefaultScale = %d;" % gunakri)
    out.append("} // namespace tbmpc")
    open(OUT, "w", encoding="utf-8").write("\n".join(out) + "\n")
    print("wrote %s: %d scales, default %d (%s)" % (os.path.relpath(OUT), len(rows), gunakri, long_names[gunakri]))


if __name__ == "__main__":
    main()
