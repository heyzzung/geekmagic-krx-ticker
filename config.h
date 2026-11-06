#pragma once

#include <Arduino.h>
#include <ESP8266WebServer.h>

static const uint8_t MAX_STOCKS = 10;

struct Config {
  String wifiSsid;
  String wifiPass;
  String adminPass;          // /settings·/update 비밀번호 (비어 있으면 보호 안 함)
  String codes[MAX_STOCKS];  // 6자리 종목 코드 (예: 005930)
  uint8_t codeCount = 0;
  uint16_t rotateSec = 10;   // 종목 전환 간격
  uint16_t refreshSec = 60;  // 시세 갱신 주기
  uint8_t brightness = 100;  // 백라이트 %
};

extern Config config;

// LittleFS 의 설정을 읽는다. 없거나 깨졌으면 기본값 (Wi-Fi 없음 → 설정 모드로 부팅).
void configLoad();
bool configSave();

// /settings 페이지 등록 (주변 Wi-Fi 목록, 종목 코드, 간격, 밝기, 관리 비밀번호), /reset (설정 초기화)
void configRegisterRoutes(ESP8266WebServer& server);

// 설정 페이지에서 값이 바뀌었으면 true 를 한 번 돌려준다.
bool configTakeChanged();

// Wi-Fi 정보가 바뀌었거나 설정을 초기화해서 재시작이 필요하면 true.
bool configRestartRequested();
