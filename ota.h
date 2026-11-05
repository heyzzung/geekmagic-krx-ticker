#pragma once

#include <Arduino.h>
#include <ESP8266WebServer.h>

// 부팅 카운터를 올리고, 복구 모드로 들어가야 하면 true.
// setup() 맨 처음에 호출할 것.
bool otaCheckRecovery();

// Wi-Fi(STA, 실패 시 AP + 캡티브 포털) + mDNS + 웹 OTA(/update) 시작.
void otaBegin(const char* version, const String& ssid, const String& pass);

// loop() 에서 매번 호출. 부팅 후 일정 시간 지나면 부팅 카운터를 초기화한다.
void otaHandle();

// 다른 모듈이 페이지를 추가할 때 사용
ESP8266WebServer& otaServer();

// 접속 주소 (STA IP 또는 AP IP)
String otaAddress();
bool otaIsApMode();
