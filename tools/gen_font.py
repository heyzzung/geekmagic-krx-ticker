"""한글 비트맵 폰트 생성기 -> font_kr.h

KS X 1001 완성형 한글 2,350자 + ASCII 를 2bpp(4단계 안티앨리어싱) 비트맵으로 만든다.
펌웨어에 PROGMEM 으로 들어가므로 파일시스템 업로드가 필요 없다.

저장 형식: 글자마다 잉크가 있는 줄만(위아래 빈 줄 제거), 글자 폭만큼만 픽셀을
줄 경계 없이 2bit 씩 이어 붙인다. 글자 데이터 앞 2바이트는 [첫 줄, 줄 수].

사용법:
  python gen_font.py <폰트.otf> <출력 font_kr.h>
폰트: Pretendard Bold (SIL OFL 1.1, tools/Pretendard-LICENSE.txt)
"""

import sys

CELL_H = 22      # 글자 높이(px)
MAX_W = 22       # 글자 최대 폭(px)
FONT_SIZE = 21
BASELINE = 18    # 셀 위에서부터 기준선 위치


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
    """(폭, 줄별 단계값 0..3 목록) 을 돌려준다."""
    from PIL import Image, ImageDraw

    width = min(MAX_W, max(1, round(font.getlength(ch))))
    img = Image.new("L", (MAX_W, CELL_H), 0)
    ImageDraw.Draw(img).text((0, BASELINE), ch, font=font, fill=255, anchor="ls")
    rows = [[img.getpixel((x, y)) * 3 // 255 for x in range(width)] for y in range(CELL_H)]
    return width, rows


def pack(rows):
    """위아래 빈 줄을 잘라내고 [첫 줄, 줄 수] + 2bit 픽셀 비트열로 만든다."""
    ink = [y for y, r in enumerate(rows) if any(r)]
    if not ink:
        return bytes([0, 0])
    top, bottom = ink[0], ink[-1]
    levels = [v for r in rows[top:bottom + 1] for v in r]
    out = bytearray((len(levels) * 2 + 7) // 8)
    for i, level in enumerate(levels):
        bit = i * 2
        out[bit // 8] |= level << (6 - bit % 8)
    return bytes([top, bottom - top + 1]) + bytes(out)


def write_header(out_path, glyphs):
    """glyphs: (코드포인트, 폭, 줄별 단계값) 목록, 코드포인트 순"""
    codes, widths, offsets, bitmaps = [], [], [], bytearray()
    for code, width, rows in glyphs:
        codes.append(code)
        widths.append(width)
        offsets.append(len(bitmaps))
        bitmaps += pack(rows)

    def rows_text(values, fmt, per_line):
        items = [fmt.format(v) for v in values]
        return ",\n".join(
            "  " + ",".join(items[i:i + per_line]) for i in range(0, len(items), per_line)
        )

    with open(out_path, "w") as f:
        f.write("// tools/gen_font.py 로 생성됨 — 직접 수정하지 말 것\n")
        f.write("// Pretendard Bold (SIL OFL 1.1)\n#pragma once\n\n#include <Arduino.h>\n\n")
        f.write(f"static const uint8_t FONT_KR_HEIGHT = {CELL_H};\n")
        f.write(f"static const uint16_t FONT_KR_COUNT = {len(codes)};\n\n")
        f.write("static const uint16_t FONT_KR_CODES[] PROGMEM = {\n")
        f.write(rows_text(codes, "0x{:04X}", 16) + "\n};\n\n")
        f.write("static const uint8_t FONT_KR_WIDTHS[] PROGMEM = {\n")
        f.write(rows_text(widths, "{}", 32) + "\n};\n\n")
        f.write("static const uint32_t FONT_KR_OFFSETS[] PROGMEM = {\n")
        f.write(rows_text(offsets, "{}", 16) + "\n};\n\n")
        f.write("static const uint8_t FONT_KR_BITMAPS[] PROGMEM = {\n")
        f.write(rows_text(bitmaps, "0x{:02X}", 25) + "\n};\n")
    print(f"{len(codes)} glyphs, {len(bitmaps)} bytes bitmap")


def main(font_path, out_path):
    from PIL import ImageFont

    font = ImageFont.truetype(font_path, FONT_SIZE)
    glyphs = []
    for ch in charset():
        width, rows = render(font, ch)
        glyphs.append((ord(ch), width, rows))
    write_header(out_path, glyphs)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
