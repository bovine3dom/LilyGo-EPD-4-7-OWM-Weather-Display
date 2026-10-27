#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate the battery font from the public-domain X11 9x18B font."""
from pathlib import Path
import argparse
import zlib
from PIL import Image, ImageDraw, ImageFont

CODES = [0x20, *range(0x2d, 0x3b), ord("d"), ord("v")]


def generate(font_path):
    font = ImageFont.truetype(str(font_path), 18)
    data, records = bytearray(), []
    for code in CODES:
        image = Image.new("1", (9, 18), 1)
        ImageDraw.Draw(image).text((0, 0), chr(code), font=font, fill=0)
        raw = bytes(sum((15 if x + bit < 9 and image.getpixel((x + bit, y)) == 0 else 0) << (4 * bit)
                        for bit in (0, 1))
                    for y in range(18) for x in range(0, 9, 2))
        bitmap = zlib.compress(raw)
        records.append(f"{{9,18,9,0,18,{len(bitmap)},{len(data)}}}")
        data.extend(bitmap)
    lines = ["// Generated from the public-domain X11 misc 9x18B font by scripts/generate_battery_font.py.",
             "#pragma once", '#include "epd_driver.h"', "", "static const uint8_t Battery18Data[] = {"]
    lines.extend("    " + ",".join(f"0x{b:02x}" for b in data[i:i + 24]) + ","
                 for i in range(0, len(data), 24))
    lines += ["};", "static const GFXglyph Battery18Glyphs[] = {"]
    lines.extend("    " + ",".join(records[i:i + 4]) + "," for i in range(0, len(records), 4))
    lines += ["};", "static const UnicodeInterval Battery18Intervals[] = {{0x20,0x20,0},{0x2d,0x3a,1},{0x64,0x64,15},{0x76,0x76,16}};",
              "static const GFXfont Battery18 = {(uint8_t*)Battery18Data,(GFXglyph*)Battery18Glyphs,"
              "(UnicodeInterval*)Battery18Intervals,4,true,18,14,-4};"]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", type=Path, default=Path("/usr/share/fonts/misc/9x18B.pcf.gz"))
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent.parent / "fonts/battery_9x18.h")
    args = parser.parse_args()
    args.output.write_text(generate(args.font))


if __name__ == "__main__":
    main()
