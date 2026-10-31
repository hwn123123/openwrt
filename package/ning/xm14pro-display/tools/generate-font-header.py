#!/usr/bin/env python3
"""Generate the small antialiased UTF-8 font used by the native dashboard."""

from pathlib import Path
import argparse
from PIL import Image, ImageDraw, ImageFont


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("font")
    parser.add_argument("source", type=Path, nargs="+",
                        help="source files whose non-ASCII glyphs are embedded")
    parser.add_argument("output", type=Path)
    parser.add_argument("--latin-font")
    parser.add_argument("--cjk-fallback-output", type=Path,
                        help="optional 32px 1-bit fallback for U+4E00-U+9FFF")
    args = parser.parse_args()

    em = 64
    cjk = ImageFont.truetype(args.font, em)
    latin = ImageFont.truetype(args.latin_font, em) if args.latin_font else cjk
    ascent, descent = cjk.getmetrics()
    source = "\n".join(path.read_text(encoding="utf-8")
                       for path in args.source)
    chars = sorted(
        set("".join(chr(code) for code in range(32, 127)) +
            "".join(ch for ch in source if ord(ch) >= 128)),
        key=ord,
    )
    bitmap = bytearray()
    glyphs = []
    missing = []
    for char in chars:
        font = latin if ord(char) < 128 else cjk
        x0, y0, x1, y1 = font.getbbox(char, anchor="ls")
        width, height = max(0, x1 - x0), max(0, y1 - y0)
        # Every printable glyph must have pixels.  Checking only CJK let a
        # broken icon-font mapping for ASCII "7" silently enter the screen
        # binary, which made values such as 70 and 75 render as 0 and 5.
        if not char.isspace() and (not width or not height):
            missing.append(char)
        offset = len(bitmap)
        if width and height:
            image = Image.new("L", (width, height), 0)
            ImageDraw.Draw(image).text((-x0, -y0), char, font=font,
                                       fill=255, anchor="ls")
            bitmap.extend(image.tobytes())
        glyphs.append((ord(char), offset, width, height, x0, y0,
                       max(1, round(font.getlength(char)))))

    if missing:
        preview = " ".join(f"{char}(U+{ord(char):04X})"
                           for char in missing[:16])
        raise SystemExit(
            "selected UI fonts are missing "
            f"{len(missing)} required glyphs: "
            f"{preview}"
        )

    lines = [
        "/* Generated bitmap subset; do not edit. */",
        "#ifndef XM14_FONT_ZH_H", "#define XM14_FONT_ZH_H",
        "#include <stdint.h>", f"#define UI_FONT_EM {em}",
        f"#define UI_FONT_ASCENT {ascent}",
        f"#define UI_FONT_DESCENT {descent}",
        "struct ui_font_glyph { uint32_t codepoint, offset; uint16_t width, height; int16_t bearing_x, bearing_y; uint16_t advance; };",
        "static const uint8_t ui_font_bitmap[] = {",
    ]
    for start in range(0, len(bitmap), 24):
        lines.append("    " + ",".join(str(v) for v in bitmap[start:start + 24]) + ",")
    lines += ["};", "static const struct ui_font_glyph ui_font_glyphs[] = {"]
    for values in glyphs:
        lines.append("    {%dU,%dU,%dU,%dU,%d,%d,%dU}," % values)
    lines += ["};", "#define UI_FONT_GLYPH_COUNT (sizeof(ui_font_glyphs) / sizeof(ui_font_glyphs[0]))", "#endif", ""]
    args.output.write_text("\n".join(lines), encoding="utf-8")

    if args.cjk_fallback_output:
        fallback_em = 30
        cell = 32
        fallback = ImageFont.truetype(args.font, fallback_em)
        rows = []
        for codepoint in range(0x4E00, 0xA000):
            image = Image.new("L", (cell, cell), 0)
            ImageDraw.Draw(image).text(
                (cell // 2, cell // 2), chr(codepoint), font=fallback,
                fill=255, anchor="mm",
            )
            pixels = image.load()
            for y in range(cell):
                bits = 0
                for x in range(cell):
                    if pixels[x, y] >= 80:
                        bits |= 1 << (cell - 1 - x)
                rows.append(bits)
        fallback_lines = [
            "/* Generated full basic-CJK fallback; do not edit. */",
            "#ifndef XM14_FONT_CJK32_H",
            "#define XM14_FONT_CJK32_H",
            "#include <stdint.h>",
            "#define UI_CJK32_FIRST 0x4E00U",
            "#define UI_CJK32_LAST 0x9FFFU",
            "#define UI_CJK32_SIZE 32U",
            "static const uint32_t ui_cjk32_rows[] = {",
        ]
        for start in range(0, len(rows), 8):
            fallback_lines.append(
                "    " + ",".join(f"0x{value:08x}U"
                                     for value in rows[start:start + 8]) + ","
            )
        fallback_lines += ["};", "#endif", ""]
        args.cjk_fallback_output.write_text(
            "\n".join(fallback_lines), encoding="utf-8"
        )


if __name__ == "__main__":
    main()
