"""한글 비트맵 폰트 생성기 -> font_kr.h

KS X 1001 완성형 한글 2,350자 + ASCII 를 2bpp(4단계 안티앨리어싱) 비트맵으로 만든다.
펌웨어에 PROGMEM 으로 들어가므로 파일시스템 업로드가 필요 없다.

사용법:
  python gen_font.py <폰트.otf> <출력 font_kr.h>
폰트: Pretendard Bold (SIL OFL 1.1, tools/Pretendard-LICENSE.txt)
"""

import sys

from PIL import Image, ImageDraw, ImageFont

CELL_H = 22      # 글자 높이(px)
MAX_W = 22       # 글자 최대 폭(px)
FONT_SIZE = 21
BASELINE = 18    # 셀 위에서부터 기준선 위치
ROW_BYTES = (MAX_W * 2 + 7) // 8
STRIDE = ROW_BYTES * CELL_H


def charset():
    chars = [chr(c) for c in range(0x20, 0x7F)]
    # EUC-KR 0xB0A1~0xC8FE 가 완성형 한글 2,350자
    for hi in range(0xB0, 0xC9):
        for lo in range(0xA1, 0xFF):
            try:
                chars.append(bytes([hi, lo]).decode("euc-kr"))
            except UnicodeDecodeError:
                pass
    return sorted(set(chars), key=ord)


def render(font, ch):
    width = min(MAX_W, max(1, round(font.getlength(ch))))
    img = Image.new("L", (MAX_W, CELL_H), 0)
    ImageDraw.Draw(img).text((0, BASELINE), ch, font=font, fill=255, anchor="ls")
    out = bytearray(STRIDE)
    for y in range(CELL_H):
        for x in range(width):
            level = img.getpixel((x, y)) * 3 // 255  # 0..3
            bit = x * 2
            out[y * ROW_BYTES + bit // 8] |= level << (6 - bit % 8)
    return width, bytes(out)


def main(font_path, out_path):
    font = ImageFont.truetype(font_path, FONT_SIZE)
    chars = charset()
    codes, widths, bitmaps = [], [], bytearray()
    for ch in chars:
        w, data = render(font, ch)
        codes.append(ord(ch))
        widths.append(w)
        bitmaps += data

    def rows(values, fmt, per_line):
        items = [fmt.format(v) for v in values]
        return ",\n".join(
            "  " + ",".join(items[i:i + per_line]) for i in range(0, len(items), per_line)
        )

    with open(out_path, "w") as f:
        f.write("// tools/gen_font.py 로 생성됨 — 직접 수정하지 말 것\n")
        f.write("// Pretendard Bold (SIL OFL 1.1)\n#pragma once\n\n#include <Arduino.h>\n\n")
        f.write(f"static const uint8_t FONT_KR_HEIGHT = {CELL_H};\n")
        f.write(f"static const uint8_t FONT_KR_ROW_BYTES = {ROW_BYTES};\n")
        f.write(f"static const uint8_t FONT_KR_STRIDE = {STRIDE};\n")
        f.write(f"static const uint16_t FONT_KR_COUNT = {len(codes)};\n\n")
        f.write("static const uint16_t FONT_KR_CODES[] PROGMEM = {\n")
        f.write(rows(codes, "0x{:04X}", 16) + "\n};\n\n")
        f.write("static const uint8_t FONT_KR_WIDTHS[] PROGMEM = {\n")
        f.write(rows(widths, "{}", 32) + "\n};\n\n")
        f.write("static const uint8_t FONT_KR_BITMAPS[] PROGMEM = {\n")
        f.write(rows(bitmaps, "0x{:02X}", 25) + "\n};\n")
    print(f"{len(codes)} glyphs, {len(bitmaps)} bytes bitmap")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
