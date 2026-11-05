#pragma once

#include <Arduino.h>
#include <ESP8266WebServer.h>

#include "config.h"

struct Quote {
  String code;
  String name;        // UTF-8 (한글)
  int32_t price = 0;  // 현재가
  int32_t change = 0; // 전일 대비
  float ratio = 0;    // 등락률 %
  int32_t open = 0;   // 시가 (정규장)
  bool marketOpen = false;
  bool nxt = false;      // 정규장 마감 후 NXT 애프터마켓 값으로 price/change/ratio 를 채웠는지
  bool nxtOpen = false;  // NXT 애프터마켓이 거래 중인지
  bool valid = false;
};

extern Quote quotes[MAX_STOCKS];

// config.codes 전체를 한 번의 요청으로 받아 quotes[] 를 갱신한다.
// 실패하면 이전 값을 유지하고 false.
bool quoteFetchAll();

// 마지막 요청이 실패했으면 true
bool quoteHasError();

// /quotes 페이지 (받아온 값 확인용), /lookup?codes=005930,000660 (종목명 조회, 설정 페이지용) 등록
void quoteRegisterRoutes(ESP8266WebServer& server);
