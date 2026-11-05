#include "config.h"

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>

#include "secrets.h"

Config config;

static const char* CONFIG_PATH = "/config.json";
static bool changed = false;
static bool restartRequested = false;

static void setDefaults() {
  config = Config();
  config.wifiSsid = WIFI_SSID;
  config.wifiPass = WIFI_PASS;
  config.codes[0] = "005930";  // 삼성전자
  config.codeCount = 1;
}

void configLoad() {
  setDefaults();
  // 처음 마운트할 때 공장 펌웨어의 파일시스템이면 자동 포맷된다
  if (!LittleFS.begin()) {
    return;
  }
  File f = LittleFS.open(CONFIG_PATH, "r");
  if (!f) {
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    return;
  }

  const char* ssid = doc["ssid"];
  if (ssid && *ssid) {
    config.wifiSsid = ssid;
    config.wifiPass = doc["pass"] | "";
  }
  JsonArray codes = doc["codes"];
  if (codes.size() > 0) {
    config.codeCount = 0;
    for (const char* code : codes) {
      if (code && config.codeCount < MAX_STOCKS) {
        config.codes[config.codeCount++] = code;
      }
    }
  }
  config.rotateSec = doc["rotate"] | config.rotateSec;
  config.refreshSec = doc["refresh"] | config.refreshSec;
  config.brightness = doc["brightness"] | config.brightness;
}

bool configSave() {
  JsonDocument doc;
  doc["ssid"] = config.wifiSsid;
  doc["pass"] = config.wifiPass;
  JsonArray codes = doc["codes"].to<JsonArray>();
  for (uint8_t i = 0; i < config.codeCount; i++) {
    codes.add(config.codes[i]);
  }
  doc["rotate"] = config.rotateSec;
  doc["refresh"] = config.refreshSec;
  doc["brightness"] = config.brightness;

  File f = LittleFS.open(CONFIG_PATH, "w");
  if (!f) {
    return false;
  }
  serializeJson(doc, f);
  f.close();
  return true;
}

static String htmlEscape(const String& s) {
  String out;
  out.reserve(s.length());
  for (char c : s) {
    switch (c) {
      case '&': out += F("&amp;"); break;
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '"': out += F("&quot;"); break;
      case '\'': out += F("&#39;"); break;
      default: out += c;
    }
  }
  return out;
}

static bool isValidCode(const String& code) {
  if (code.length() != 6) {
    return false;
  }
  for (char c : code) {
    if (!isDigit(c) && !(c >= 'A' && c <= 'Z')) {
      return false;
    }
  }
  return true;
}

static const char PAGE_HEAD[] PROGMEM =
    "<html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>body{font-family:sans-serif;max-width:420px;margin:16px auto;padding:0 12px}"
    "label{display:block;margin-top:14px;font-weight:bold}"
    "input{width:100%;padding:8px;font-size:16px;box-sizing:border-box}"
    "small{color:#666}button{margin-top:20px;padding:10px 24px;font-size:16px}</style>"
    "</head><body><h3>GeekStock 설정</h3>";

// 종목 코드 입력칸 아래에 코드별 종목명을 보여준다 (입력이 멈추고 0.6초 뒤 /lookup 조회)
static const char NAMES_SCRIPT[] PROGMEM =
    "<script>(function(){"
    "var inp=document.querySelector('input[name=codes]'),box=document.getElementById('names'),t;"
    "function row(code,name,bad){var d=document.createElement('div');"
    "d.textContent=code+'  '+name;if(bad)d.style.color='#c00';box.appendChild(d);}"
    "function look(){var v=inp.value.toUpperCase().split(/[\\s,]+/).filter(Boolean);"
    "if(!v.length){box.textContent='';return;}"
    "box.textContent='조회 중...';"
    "fetch('/lookup?codes='+encodeURIComponent(v.join(','))).then(function(r){return r.json();})"
    ".then(function(m){box.textContent='';v.forEach(function(c){"
    "if(!/^[0-9A-Z]{6}$/.test(c))row(c,'6자리 코드가 아님',1);"
    "else if(m[c])row(c,m[c]);else row(c,'없는 종목',1);});})"
    ".catch(function(){box.textContent='종목명 조회 실패 (인터넷 연결 확인)';});}"
    "inp.addEventListener('input',function(){clearTimeout(t);t=setTimeout(look,600);});"
    "look();})();</script>";

static void sendSettingsPage(ESP8266WebServer& server, const String& message) {
  // 주변 Wi-Fi 목록 (2.4GHz 만 보임)
  int n = WiFi.scanNetworks();
  String body = FPSTR(PAGE_HEAD);
  if (message.length()) {
    body += F("<p style='color:#c00'>");
    body += message;
    body += F("</p>");
  }
  body += F("<form method='POST' action='/settings'>"
            "<label>Wi-Fi 이름</label><input name='ssid' list='ssids' value='");
  body += htmlEscape(config.wifiSsid);
  body += F("'><datalist id='ssids'>");
  for (int i = 0; i < n; i++) {
    body += F("<option value='");
    body += htmlEscape(WiFi.SSID(i));
    body += F("'>");
  }
  WiFi.scanDelete();
  body += F("</datalist>"
            "<label>Wi-Fi 비밀번호</label>"
            "<input name='pass' type='password' placeholder='바꿀 때만 입력'>"
            "<label>종목 코드</label><input name='codes' value='");
  for (uint8_t i = 0; i < config.codeCount; i++) {
    if (i) body += ',';
    body += config.codes[i];
  }
  body += F("'><small>6자리 코드, 쉼표로 구분, 최대 10개 (예: 005930,000660)</small>"
            "<div id='names' style='margin-top:6px;line-height:1.6'></div>"
            "<label>종목 전환 간격 (초)</label><input name='rotate' type='number' min='3' max='600' value='");
  body += config.rotateSec;
  body += F("'><label>시세 갱신 주기 (초)</label><input name='refresh' type='number' min='10' max='3600' value='");
  body += config.refreshSec;
  body += F("'><label>밝기 (%)</label><input name='brightness' type='range' min='5' max='100' value='");
  body += config.brightness;
  body += F("'><button type='submit'>저장</button></form>"
            "<p><a href='/'>처음으로</a></p>");
  body += FPSTR(NAMES_SCRIPT);
  body += F("</body></html>");
  server.send(200, F("text/html; charset=utf-8"), body);
}

static void handleSave(ESP8266WebServer& server) {
  // 종목 코드: 쉼표·공백 구분, 대문자로 정규화
  String raw = server.arg("codes");
  raw.toUpperCase();
  raw.replace(' ', ',');
  String codes[MAX_STOCKS];
  uint8_t count = 0;
  int from = 0;
  while (from <= (int)raw.length()) {
    int comma = raw.indexOf(',', from);
    if (comma < 0) comma = raw.length();
    String code = raw.substring(from, comma);
    from = comma + 1;
    if (code.isEmpty()) continue;
    if (!isValidCode(code)) {
      sendSettingsPage(server, String(F("잘못된 종목 코드: ")) + htmlEscape(code));
      return;
    }
    if (count < MAX_STOCKS) codes[count++] = code;
  }
  if (count == 0) {
    sendSettingsPage(server, F("종목 코드를 하나 이상 입력하세요."));
    return;
  }

  String ssid = server.arg("ssid");
  ssid.trim();
  String pass = server.arg("pass");
  if (ssid.isEmpty()) {
    sendSettingsPage(server, F("Wi-Fi 이름을 입력하세요."));
    return;
  }
  bool wifiChanged = ssid != config.wifiSsid || (pass.length() && pass != config.wifiPass);
  if (wifiChanged) {
    // 이름만 바꾸고 비밀번호를 비워두면 비밀번호 없는 Wi-Fi 로 본다
    config.wifiPass = pass;
  }
  config.wifiSsid = ssid;

  for (uint8_t i = 0; i < count; i++) config.codes[i] = codes[i];
  config.codeCount = count;
  config.rotateSec = constrain(server.arg("rotate").toInt(), 3, 600);
  config.refreshSec = constrain(server.arg("refresh").toInt(), 10, 3600);
  config.brightness = constrain(server.arg("brightness").toInt(), 5, 100);

  if (!configSave()) {
    sendSettingsPage(server, F("저장 실패 (파일시스템 오류)"));
    return;
  }
  changed = true;

  String body = FPSTR(PAGE_HEAD);
  if (wifiChanged) {
    restartRequested = true;
    body += F("<p>저장했습니다. 새 Wi-Fi 로 연결하려고 재시작합니다.</p>"
              "<p>연결에 실패하면 'GeekStock' Wi-Fi 에 접속해 다시 설정하세요.</p>");
  } else {
    body += F("<meta http-equiv='refresh' content='2;url=/settings'><p>저장했습니다.</p>");
  }
  body += F("</body></html>");
  server.send(200, F("text/html; charset=utf-8"), body);
}

void configRegisterRoutes(ESP8266WebServer& server) {
  server.on("/settings", HTTP_GET, [&server]() { sendSettingsPage(server, ""); });
  server.on("/settings", HTTP_POST, [&server]() { handleSave(server); });
}

bool configTakeChanged() {
  bool c = changed;
  changed = false;
  return c;
}

bool configRestartRequested() {
  return restartRequested;
}
