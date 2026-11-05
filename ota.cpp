#include "ota.h"

#include <DNSServer.h>
#include <EEPROM.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>

#include "secrets.h"

// 복구 모드 진입 조건: 부팅 후 STABLE_MS 안에 리셋(크래시 또는 전원 재투입)이
// MAX_SHORT_BOOTS 번 연속되면 다음 부팅은 Wi-Fi + OTA 만 띄운다.
// 버튼이 없으므로 "전원 빠르게 3번 껐다 켜기"가 수동 복구 방법이다.
static const uint32_t STABLE_MS = 10000;
static const uint8_t MAX_SHORT_BOOTS = 3;
static const uint32_t WIFI_TIMEOUT_MS = 20000;
static const uint32_t BOOT_MAGIC = 0x5354434B;  // "STCK"

struct BootRecord {
  uint32_t magic;
  uint8_t shortBoots;
  // 마지막 비정상 리셋(예외·워치독) 정보. 복구 모드로 빠진 원인을 / 페이지에서 보기 위함.
  uint8_t crashReason;  // rst_info.reason (1=WDT, 2=Exception, 3=Soft WDT), 0=없음
  uint32_t crashCause;  // exccause
  uint32_t crashEpc;    // epc1 (죽은 코드 주소, .elf 로 addr2line)
};

static ESP8266WebServer server(80);
static ESP8266HTTPUpdateServer updater;
static DNSServer dns;
static bool apMode = false;
static bool stableMarked = false;
static const char* fwVersion = "";
static bool recovery = false;
static BootRecord boot = {};

static void saveShortBoots(uint8_t n) {
  boot.magic = BOOT_MAGIC;
  boot.shortBoots = n;
  EEPROM.put(0, boot);
  EEPROM.commit();
}

bool otaCheckRecovery() {
  EEPROM.begin(sizeof(BootRecord));
  EEPROM.get(0, boot);
  if (boot.magic != BOOT_MAGIC) boot = {};
  uint8_t n = boot.shortBoots;

  // 이번 부팅이 크래시 때문이면 원인을 남긴다 (전원 재투입·OTA 재시작은 덮어쓰지 않음)
  const rst_info* ri = ESP.getResetInfoPtr();
  if (ri->reason == REASON_WDT_RST || ri->reason == REASON_EXCEPTION_RST ||
      ri->reason == REASON_SOFT_WDT_RST) {
    boot.crashReason = ri->reason;
    boot.crashCause = ri->exccause;
    boot.crashEpc = ri->epc1;
  }

  if (n + 1 >= MAX_SHORT_BOOTS) {
    // 복구 모드 진입. 카운터를 비워서 업로드 후 재부팅은 정상 부팅이 되게 한다.
    saveShortBoots(0);
    stableMarked = true;
    recovery = true;
    return true;
  }
  saveShortBoots(n + 1);
  return false;
}

static void startAp() {
  apMode = true;
  // AP_STA: 설정 페이지에서 주변 Wi-Fi 스캔이 가능하도록
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  // 모든 도메인을 기기로 돌려서 폰에서 캡티브 포털 창이 뜨게 한다
  dns.start(53, "*", WiFi.softAPIP());
}

static void handleRoot() {
  String body = F("<html><head><meta charset='utf-8'>"
                  "<meta name='viewport' content='width=device-width,initial-scale=1'></head><body>");
  body += F("<h3>GeekStock ");
  body += fwVersion;
  body += F("</h3><p>");
  if (recovery) body += F("<b>복구 모드</b> / ");
  body += apMode ? F("AP 모드") : F("STA 모드");
  body += F(" / heap ");
  body += ESP.getFreeHeap();
  body += F(" / uptime ");
  body += millis() / 1000;
  body += F("s<br>이번 부팅 원인: ");
  body += ESP.getResetReason();
  if (boot.crashReason) {
    char buf[64];
    snprintf(buf, sizeof(buf), "reason %u, cause %u, epc 0x%08x", boot.crashReason,
             boot.crashCause, boot.crashEpc);
    body += F("<br>마지막 크래시: ");
    body += buf;
  }
  body += F("</p><p><a href='/settings'>설정</a></p>"
            "<p><a href='/update'>펌웨어 업데이트</a></p></body></html>");
  server.send(200, F("text/html; charset=utf-8"), body);
}

static void handleNotFound() {
  if (apMode) {
    // 캡티브 포털 감지 요청(captive.apple.com 등)을 첫 화면으로 보낸다
    server.sendHeader(F("Location"), String(F("http://")) + WiFi.softAPIP().toString() + "/");
    server.send(302, F("text/plain"), "");
    return;
  }
  server.send(404, F("text/plain"), F("Not found"));
}

void otaBegin(const char* version, const String& ssid, const String& pass) {
  fwVersion = version;

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.hostname("geekstock");
  WiFi.begin(ssid, pass);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
    delay(200);
  }
  if (WiFi.status() != WL_CONNECTED) {
    startAp();
  }

  MDNS.begin("geekstock");
  updater.setup(&server, "/update", OTA_USER, OTA_PASS);
  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);
  server.begin();
  MDNS.addService("http", "tcp", 80);
}

void otaHandle() {
  if (apMode) {
    dns.processNextRequest();
  }
  server.handleClient();
  MDNS.update();

  if (!stableMarked && millis() > STABLE_MS) {
    saveShortBoots(0);
    stableMarked = true;
  }
}

ESP8266WebServer& otaServer() {
  return server;
}

String otaAddress() {
  return apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
}

bool otaIsApMode() {
  return apMode;
}
