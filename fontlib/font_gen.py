# -*- coding: utf-8 -*-
"""
font_gen.py — 生成 ZBFT 点阵字库 font.bin

用法:
    python font_gen.py                            # 默认: 黑体 16px, 输出 font.bin
    python font_gen.py --font C:/Windows/Fonts/simhei.ttf --size 16 --out font.bin
    python font_gen.py --preview 晴天ABC          # 只预览字符点阵(ASCII art), 不生成文件

字符集: ASCII 可见字符(0x20~0x7E) + GB2312 全部可编码字符
        (含一级/二级汉字 6763 个及全角标点、希腊字母等符号区)

输出格式(与 docs/lvgl_chinese_font_design.md 一致, 小端):
    Header 16B : magic "ZBFT" | version u8 | width u8 | height u8 | bpp u8
                 | glyph_cnt u32 | crc32 u32 (索引表+点阵区)
    索引表     : glyph_cnt × u16 Unicode 码点, 严格升序
    点阵区     : glyph_cnt × 32B, 16 行 × 每行 2B, MSB 在左
                 ASCII(码点<0x80)渲染在左半 8 列, 运行时按 adv_w=8 处理
"""
import argparse
import struct
import sys
import zlib

from PIL import Image, ImageDraw, ImageFont

MAGIC = b"ZBFT"
VERSION = 1
WIDTH = 16
HEIGHT = 16
BPP = 1
BYTES_PER_GLYPH = WIDTH * HEIGHT // 8  # 32


def build_charset():
    """ASCII 可见字符 + GB2312 全部双字节字符, 按 Unicode 码点升序去重。"""
    chars = set(chr(c) for c in range(0x20, 0x7F))  # ASCII 95 个(含空格)

    for hi in range(0xA1, 0xF8):
        for lo in range(0xA1, 0xFF):
            try:
                ch = bytes((hi, lo)).decode("gb2312")
            except UnicodeDecodeError:
                continue
            if ord(ch) <= 0xFFFF:  # 索引表是 u16, 只收 BMP
                chars.add(ch)

    return sorted(chars, key=ord)


def render_glyph(font, ch, offset_y):
    """渲染单字为 16x16 1bpp, 返回 32 字节。ASCII 半角字符水平居中到左 8 列。"""
    img = Image.new("1", (WIDTH, HEIGHT), 0)
    draw = ImageDraw.Draw(img)

    if ord(ch) < 0x80:
        # 半角: 以 8px 宽为槽位, 按实际墨宽在 0~8 内居中
        bbox = font.getbbox(ch)
        ink_w = bbox[2] - bbox[0]
        x = max(0, (8 - ink_w) // 2 - bbox[0])
    else:
        x = 0

    draw.text((x, offset_y), ch, font=font, fill=1)

    data = bytearray(BYTES_PER_GLYPH)
    px = img.load()
    for row in range(HEIGHT):
        b0 = 0
        b1 = 0
        for col in range(8):
            if px[col, row]:
                b0 |= 0x80 >> col
        for col in range(8, WIDTH):
            if px[col, row]:
                b1 |= 0x80 >> (col - 8)
        data[row * 2] = b0
        data[row * 2 + 1] = b1
    return bytes(data)


def glyph_to_ascii_art(data):
    lines = []
    for row in range(HEIGHT):
        b0 = data[row * 2]
        b1 = data[row * 2 + 1]
        bits = (b0 << 8) | b1
        lines.append("".join("#" if bits & (0x8000 >> c) else "." for c in range(WIDTH)))
    return "\n".join(lines)


def compute_offset_y(font):
    """让 ascent+descent 的行盒在 16px 单元格内居中, 所有字共用同一基线。"""
    ascent, descent = font.getmetrics()
    return (HEIGHT - (ascent + descent)) // 2


def main():
    ap = argparse.ArgumentParser(description="generate ZBFT bitmap font for LVGL")
    ap.add_argument("--font", default="C:/Windows/Fonts/simhei.ttf",
                    help="TTF 字体路径(默认黑体)")
    ap.add_argument("--size", type=int, default=16, help="渲染字号(默认 16)")
    ap.add_argument("--out", default="font.bin", help="输出文件(默认 font.bin)")
    ap.add_argument("--preview", default=None,
                    help="仅预览这些字符的点阵, 不生成文件")
    args = ap.parse_args()

    font = ImageFont.truetype(args.font, args.size)
    offset_y = compute_offset_y(font)
    ascent, descent = font.getmetrics()
    print(f"font={args.font} size={args.size} ascent={ascent} descent={descent} "
          f"offset_y={offset_y}")

    if args.preview:
        for ch in args.preview:
            print(f"\nU+{ord(ch):04X} '{ch}':")
            print(glyph_to_ascii_art(render_glyph(font, ch, offset_y)))
        return

    charset = build_charset()
    print(f"charset: {len(charset)} chars "
          f"(U+{ord(charset[0]):04X} ~ U+{ord(charset[-1]):04X})")

    index_blob = b"".join(struct.pack("<H", ord(c)) for c in charset)
    bitmap_parts = []
    for i, ch in enumerate(charset):
        bitmap_parts.append(render_glyph(font, ch, offset_y))
        if (i + 1) % 1000 == 0:
            print(f"  rendered {i + 1}/{len(charset)}")
    bitmap_blob = b"".join(bitmap_parts)

    payload = index_blob + bitmap_blob
    crc = zlib.crc32(payload) & 0xFFFFFFFF

    header = struct.pack("<4sBBBBII", MAGIC, VERSION, WIDTH, HEIGHT, BPP,
                         len(charset), crc)
    assert len(header) == 16

    with open(args.out, "wb") as f:
        f.write(header)
        f.write(payload)

    total = 16 + len(payload)
    print(f"\nOK -> {args.out}")
    print(f"  glyphs   : {len(charset)}")
    print(f"  index    : {len(index_blob)} bytes")
    print(f"  bitmaps  : {len(bitmap_blob)} bytes")
    print(f"  total    : {total} bytes ({total / 1024:.1f} KB)")
    print(f"  crc32    : 0x{crc:08X}")
    print("  (font_info 命令应回显相同的 glyphs/crc)")


if __name__ == "__main__":
    main()
