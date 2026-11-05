#pragma once

#include <Adafruit_GFX.h>
#include <Arduino.h>

#include <functional>

#include "config.h"

// 09:00~15:30(390분)을 CHART_POINTS 칸으로 나눠 칸마다 마지막 가격을 저장한다.
static const uint8_t CHART_POINTS = 108;

struct Chart {
  String code;
  uint32_t date = 0;               // YYYYMMDD (데이터의 거래일)
  int32_t pts[CHART_POINTS] = {};  // 0 = 데이터 없음
  uint32_t loadedMs = 0;
  bool loaded = false;
};

extern Chart charts[MAX_STOCKS];

// loop() 에서 호출. 다시 받아야 하는 차트를 한 번에 하나만 받는다.
// 데이터가 없으면 항상, 장중(marketOpen)이면 30분 경과·거래일 변경 시.
// 받은 종목 인덱스, 없으면 -1.
int chartUpdate(bool marketOpen);

// 실시간 시세로 현재 칸을 갱신한다 (정규장 시간에만).
void chartAppend(uint8_t index, int32_t price);

// 차트를 그린다. 캔버스가 화면의 일부(띠)일 수 있으므로 화면 좌표에서 originY 를 뺀다.
// left/right, top/height: 차트 영역(화면 좌표, right 는 끝 바로 다음). base: 시가(기준선).
// 선은 기준선 위 구간은 upColor, 아래 구간은 downColor 로 나눠 칠한다.
// onPoint: 선을 한 칸 이을 때마다 호출 (시간 순서로 그려지는 효과용, 화면에 바로 그릴 때)
void chartDraw(Adafruit_GFX& g, int16_t originY, uint8_t index, int16_t left, int16_t right,
               int16_t top, int16_t height, int32_t base, uint16_t upColor, uint16_t downColor,
               uint16_t baseColor, std::function<void()> onPoint = nullptr);
