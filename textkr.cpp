#include "textkr.h"

#include "font_kr.h"

static const uint8_t GLYPH_GAP = 1;

// 코드포인트 -> 글리프 인덱스 (없으면 '?')
static int glyphIndex(uint16_t cp) {
  int lo = 0, hi = FONT_KR_COUNT - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    uint16_t v = pgm_read_word(&FONT_KR_CODES[mid]);
    if (v == cp) return mid;
    if (v < cp) lo = mid + 1;
    else hi = mid - 1;
  }
  return cp == '?' ? 0 : glyphIndex('?');
}

// UTF-8 에서 다음 코드포인트를 읽고 i 를 전진시킨다 (BMP 만)
static uint16_t nextCodepoint(const String& s, unsigned int& i) {
  uint8_t c = s[i++];
  if (c < 0x80) return c;
  if ((c & 0xE0) == 0xC0 && i < s.length()) {
    return ((c & 0x1F) << 6) | (s[i++] & 0x3F);
  }
  if ((c & 0xF0) == 0xE0 && i + 1 < s.length()) {
    uint16_t cp = ((c & 0x0F) << 12) | ((s[i] & 0x3F) << 6) | (s[i + 1] & 0x3F);
    i += 2;
    return cp;
  }
  return '?';
}

static uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t level) {
  // level: 1..3 (3 = fg)
  uint8_t r = (((fg >> 11) & 0x1F) * level + ((bg >> 11) & 0x1F) * (3 - level)) / 3;
  uint8_t g = (((fg >> 5) & 0x3F) * level + ((bg >> 5) & 0x3F) * (3 - level)) / 3;
  uint8_t b = ((fg & 0x1F) * level + (bg & 0x1F) * (3 - level)) / 3;
  return (r << 11) | (g << 5) | b;
}

static int16_t drawGlyph(Adafruit_GFX& g, int16_t x, int16_t y, int idx, const uint16_t* shades) {
  uint8_t w = pgm_read_byte(&FONT_KR_WIDTHS[idx]);
  const uint8_t* bm = FONT_KR_BITMAPS + (uint32_t)idx * FONT_KR_STRIDE;
  for (uint8_t row = 0; row < FONT_KR_HEIGHT; row++) {
    for (uint8_t col = 0; col < w; col++) {
      uint8_t b = pgm_read_byte(bm + row * FONT_KR_ROW_BYTES + (col >> 2));
      uint8_t level = (b >> (6 - (col & 3) * 2)) & 3;
      if (level) g.drawPixel(x + col, y + row, shades[level]);
    }
  }
  return w + GLYPH_GAP;
}

static int16_t glyphAdvance(int idx) {
  return pgm_read_byte(&FONT_KR_WIDTHS[idx]) + GLYPH_GAP;
}

int16_t krWidth(const String& text) {
  int16_t w = 0;
  unsigned int i = 0;
  while (i < text.length()) w += glyphAdvance(glyphIndex(nextCodepoint(text, i)));
  return w;
}

int16_t krDraw(Adafruit_GFX& g, int16_t x, int16_t y, const String& text, uint16_t fg,
               uint16_t bg, int16_t maxWidth) {
  const uint16_t shades[4] = {bg, blend565(fg, bg, 1), blend565(fg, bg, 2), fg};
  int dotIdx = glyphIndex('.');
  int16_t ellipsis = glyphAdvance(dotIdx) * 2;
  bool truncate = maxWidth > 0 && krWidth(text) > maxWidth;
  int16_t limit = x + maxWidth - ellipsis;

  unsigned int i = 0;
  while (i < text.length()) {
    int idx = glyphIndex(nextCodepoint(text, i));
    if (truncate && x + glyphAdvance(idx) > limit) {
      x += drawGlyph(g, x, y, dotIdx, shades);
      x += drawGlyph(g, x, y, dotIdx, shades);
      break;
    }
    x += drawGlyph(g, x, y, idx, shades);
  }
  return x;
}

// ---- 7-세그먼트 ----

// 비트: a b c d e f g (a=위, b=오른쪽 위, c=오른쪽 아래, d=아래, e=왼쪽 아래, f=왼쪽 위, g=가운데)
static const uint8_t SEG_DIGITS[10] = {
    0b1111110, 0b0110000, 0b1101101, 0b1111001, 0b0110011,
    0b1011011, 0b1011111, 0b1110000, 0b1111111, 0b1111011,
};

static void drawSegments(Adafruit_GFX& g, int16_t x, int16_t y, int16_t w, int16_t h,
                         uint8_t bits, uint16_t color) {
  int16_t t = max<int16_t>(3, w / 5);  // 획 두께
  int16_t half = h / 2;
  int16_t r = t / 2;
  int16_t gap = 1;
  // 가로 획
  if (bits & 0b1000000) g.fillRoundRect(x + t / 2 + gap, y, w - t - gap * 2, t, r, color);
  if (bits & 0b0000001) g.fillRoundRect(x + t / 2 + gap, y + half - t / 2, w - t - gap * 2, t, r, color);
  if (bits & 0b0001000) g.fillRoundRect(x + t / 2 + gap, y + h - t, w - t - gap * 2, t, r, color);
  // 세로 획
  int16_t vh = half - gap * 2;
  if (bits & 0b0000010) g.fillRoundRect(x, y + gap + t / 2, t, vh - t / 2, r, color);
  if (bits & 0b0100000) g.fillRoundRect(x + w - t, y + gap + t / 2, t, vh - t / 2, r, color);
  if (bits & 0b0000100) g.fillRoundRect(x, y + half + gap, t, vh - t / 2, r, color);
  if (bits & 0b0010000) g.fillRoundRect(x + w - t, y + half + gap, t, vh - t / 2, r, color);
}

void segDrawRight(Adafruit_GFX& g, int16_t right, int16_t y, int16_t height,
                  int16_t maxWidth, const String& text, uint16_t color, uint8_t hideLeft) {
  int16_t digitW = height * 9 / 16;
  int16_t space = max<int16_t>(3, digitW / 5);
  int16_t punctW = max<int16_t>(6, digitW / 3);

  // '1' 도 실제 7-세그먼트처럼 다른 숫자와 같은 칸을 쓴다 (오른쪽 획만, 왼쪽은 빈 공간)
  int digits = 0, puncts = 0;
  for (char c : text) {
    if (isDigit(c) || c == '-' || c == '+') digits++;
    else puncts++;
  }
  auto total = [&]() { return digits * (digitW + space) + puncts * punctW; };
  while (digitW > 8 && total() - space > maxWidth) {  // 마지막 숫자 뒤 간격은 그리지 않음
    digitW--;
    space = max<int16_t>(3, digitW / 5);
  }

  int16_t t = max<int16_t>(3, digitW / 5);
  int16_t x = right - total() + space;
  for (unsigned int i = 0; i < text.length(); i++) {
    char c = text[i];
    if (i < hideLeft) {
      x += isDigit(c) || c == '-' || c == '+' ? digitW + space : punctW;
    } else if (isDigit(c)) {
      drawSegments(g, x, y, digitW, height, SEG_DIGITS[c - '0'], color);
      x += digitW + space;
    } else if (c == '-' || c == '+') {
      drawSegments(g, x, y, digitW, height, 0b0000001, color);
      if (c == '+') g.fillRoundRect(x + digitW / 2 - t / 2, y + height / 4, t, height / 2, t / 2, color);
      x += digitW + space;
    } else if (c == ',') {
      // 7-세그먼트 쉼표: 아래 획 높이에서 시작해 왼쪽 아래로 짧게 기운 획 (길이 t*1.5)
      int16_t cx = x + punctW / 2 - t / 2 - space / 2;
      int16_t y0 = y + height - t, y1 = y + height + t / 2 - 1, s = t / 2;
      g.fillTriangle(cx, y0, cx + t - 1, y0, cx + t - 1 - s, y1, color);
      g.fillTriangle(cx, y0, cx + t - 1 - s, y1, cx - s, y1, color);
      x += punctW;
    } else {  // '.'
      g.fillRoundRect(x + punctW / 2 - t / 2 - space / 2, y + height - t, t, t, t / 2, color);
      x += punctW;
    }
  }
}
