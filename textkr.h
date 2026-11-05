#pragma once

#include <Adafruit_GFX.h>
#include <Arduino.h>

// UTF-8 문자열(한글 + ASCII)을 20px 안티앨리어싱 폰트로 그린다.
// y 는 글자 셀의 위쪽. bg 는 안티앨리어싱 혼합용 배경색. 그린 뒤의 x 를 돌려준다.
// maxWidth 를 넘으면 뒤를 "…" 대신 ".." 으로 자른다.
int16_t krDraw(Adafruit_GFX& g, int16_t x, int16_t y, const String& text, uint16_t fg,
               uint16_t bg, int16_t maxWidth = 0);

// 그렸을 때의 폭(px)
int16_t krWidth(const String& text);

// 7-세그먼트 스타일 숫자. "0-9", ',', '-', '+', '.' 지원.
// 오른쪽 끝을 right 에 맞추고, 전체 폭이 maxWidth 를 넘으면 숫자 폭을 줄인다.
// hideLeft: 앞에서부터 이 개수만큼의 글자는 자리만 차지하고 그리지 않는다 (자리별 등장 효과용).
void segDrawRight(Adafruit_GFX& g, int16_t right, int16_t y, int16_t height,
                  int16_t maxWidth, const String& text, uint16_t color, uint8_t hideLeft = 0);
