#pragma once

// 이 파일을 secrets.h 로 복사한 뒤 값을 채운다. secrets.h 는 git 에 올리지 않는다.

// 2.4GHz / WPA2 Wi-Fi (처음 부팅·복구 모드에서 사용, 이후엔 /settings 에서 변경)
#define WIFI_SSID "your-wifi"
#define WIFI_PASS "your-password"

// /update 페이지 접속 계정
#define OTA_USER "admin"
#define OTA_PASS "change-me"

// STA 연결 실패 시 띄우는 AP (비밀번호 8자 이상, 기기 화면에 표시됨)
#define AP_SSID "GeekStock"
#define AP_PASS "change-me"
