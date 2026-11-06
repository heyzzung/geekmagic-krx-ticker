#pragma once

#include <Arduino.h>
#include <ESP8266WebServer.h>

// 설정 모드 AP 이름 (비밀번호 없는 열린 Wi-Fi)
#define AP_SSID "GeekStock"
// /update·/settings 계정 이름. 비밀번호는 /settings 에서 정한다 (비어 있으면 보호 안 함)
#define ADMIN_USER "admin"

// 부팅 카운터를 올리고, 복구 모드로 들어가야 하면 true.
// setup() 맨 처음에 호출할 것.
bool otaCheckRecovery();

// Wi-Fi(STA, 실패하거나 SSID 가 없으면 AP + 캡티브 포털) + mDNS + 웹 OTA(/update) 시작.
// adminPass 가 비어 있으면 /update 는 비밀번호 없이 열린다.
void otaBegin(const char* version, const String& ssid, const String& pass, const String& adminPass);

// /update 비밀번호 변경 (설정 페이지에서 바꿨을 때)
void otaSetAdminPassword(const String& adminPass);

// loop() 에서 매번 호출. 부팅 후 일정 시간 지나면 부팅 카운터를 초기화한다.
void otaHandle();

// 다른 모듈이 페이지를 추가할 때 사용
ESP8266WebServer& otaServer();

// 접속 주소 (STA IP 또는 AP IP)
String otaAddress();
bool otaIsApMode();
