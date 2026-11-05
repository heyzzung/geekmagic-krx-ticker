#include "quote.h"

#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>

Quote quotes[MAX_STOCKS];

// 네이버 증권 실시간 시세 (비공식). HTTP 로도 응답해서 TLS 메모리를 쓰지 않는다.
// 여러 종목은 쉼표로 묶어 한 번에 요청: .../stock/005930,000660
static const char QUOTE_URL[] PROGMEM = "http://polling.finance.naver.com/api/realtime/domestic/stock/";
static const uint16_t HTTP_TIMEOUT_MS = 8000;

static bool lastError = false;
static uint32_t lastFetchMs = 0;

// "-16,000" -> -16000 (NXT 값은 쉼표가 들어간 문자열로만 온다)
static int32_t parseCommaNumber(const char* s) {
  int32_t v = 0;
  bool neg = false;
  for (; *s; s++) {
    if (*s == '-') neg = true;
    else if (isDigit(*s)) v = v * 10 + (*s - '0');
    else if (*s != ',' && *s != '+') break;
  }
  return neg ? -v : v;
}

static Quote* findQuote(const String& code) {
  for (uint8_t i = 0; i < config.codeCount; i++) {
    if (quotes[i].code == code) return &quotes[i];
  }
  return nullptr;
}

// codes("005930,000660") 시세를 받아 filter 에 남긴 필드만 doc 에 담는다
static bool fetchQuotes(const String& codes, JsonDocument& filter, JsonDocument& doc) {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClient client;
  HTTPClient http;
  // HTTP/1.0: chunked 인코딩 없이 받아서 스트림을 바로 JSON 파서에 넘긴다
  http.useHTTP10(true);
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(client, String(FPSTR(QUOTE_URL)) + codes)) return false;
  if (http.GET() != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  DeserializationError err =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  return !err;
}

bool quoteFetchAll() {
  if (config.codeCount == 0) {
    lastError = true;
    return false;
  }

  String codes;
  for (uint8_t i = 0; i < config.codeCount; i++) {
    if (i) codes += ',';
    codes += config.codes[i];
  }

  // 종목당 약 2.5KB 응답 중 필요한 필드만 남긴다
  JsonDocument filter;
  JsonObject f = filter["datas"][0].to<JsonObject>();
  f["itemCode"] = true;
  f["stockName"] = true;
  f["closePriceRaw"] = true;
  f["compareToPreviousClosePriceRaw"] = true;
  f["fluctuationsRatioRaw"] = true;
  f["openPriceRaw"] = true;
  f["marketStatus"] = true;
  JsonObject fo = f["overMarketPriceInfo"].to<JsonObject>();
  fo["tradingSessionType"] = true;
  fo["overMarketStatus"] = true;
  fo["overPrice"] = true;
  fo["compareToPreviousClosePrice"] = true;
  fo["fluctuationsRatio"] = true;

  JsonDocument doc;
  if (!fetchQuotes(codes, filter, doc)) {
    lastError = true;
    return false;
  }

  // 설정 순서대로 채운다 (응답 순서와 무관하게)
  for (uint8_t i = 0; i < config.codeCount; i++) {
    if (quotes[i].code != config.codes[i]) {
      quotes[i] = Quote();
      quotes[i].code = config.codes[i];
    }
  }
  for (JsonObject d : doc["datas"].as<JsonArray>()) {
    const char* code = d["itemCode"];
    if (!code) continue;
    Quote* q = findQuote(code);
    if (!q) continue;
    q->name = d["stockName"] | "";
    q->price = atol(d["closePriceRaw"] | "0");
    q->change = atol(d["compareToPreviousClosePriceRaw"] | "0");
    q->ratio = atof(d["fluctuationsRatioRaw"] | "0");
    q->open = atol(d["openPriceRaw"] | "0");
    q->marketOpen = strcmp(d["marketStatus"] | "", "OPEN") == 0;

    // 정규장 마감 후(NXT 애프터마켓 세션)에는 NXT 가격을 보여준다. 시가·차트는 정규장 그대로.
    JsonObject o = d["overMarketPriceInfo"];
    int32_t nxtPrice = parseCommaNumber(o["overPrice"] | "");
    q->nxt = strcmp(o["tradingSessionType"] | "", "AFTER_MARKET") == 0 && nxtPrice > 0;
    q->nxtOpen = q->nxt && strcmp(o["overMarketStatus"] | "", "OPEN") == 0;
    if (q->nxt) {
      q->price = nxtPrice;
      q->change = parseCommaNumber(o["compareToPreviousClosePrice"] | "");
      q->ratio = atof(o["fluctuationsRatio"] | "0");
    }
    q->valid = true;
  }

  lastError = false;
  lastFetchMs = millis();
  return true;
}

bool quoteHasError() {
  return lastError;
}

// 설정 페이지에서 입력 중인 코드의 종목명을 보여주기 위한 조회. {"005930":"삼성전자",...}
// 형식이 틀린 코드는 빼고 묻는다. 없는 코드는 응답에 빠진다.
static void handleLookup(ESP8266WebServer& server) {
  String raw = server.arg("codes");
  raw.toUpperCase();
  String codes;
  uint8_t count = 0;
  int from = 0;
  while (from <= (int)raw.length() && count < MAX_STOCKS) {
    int comma = raw.indexOf(',', from);
    if (comma < 0) comma = raw.length();
    String code = raw.substring(from, comma);
    from = comma + 1;
    bool ok = code.length() == 6;
    for (char c : code) ok = ok && (isDigit(c) || (c >= 'A' && c <= 'Z'));
    if (!ok) continue;
    if (count++) codes += ',';
    codes += code;
  }

  JsonDocument out;
  out.to<JsonObject>();
  if (count) {
    JsonDocument filter;
    JsonObject f = filter["datas"][0].to<JsonObject>();
    f["itemCode"] = true;
    f["stockName"] = true;
    JsonDocument doc;
    if (!fetchQuotes(codes, filter, doc)) {
      server.send(502, F("application/json"), F("{}"));
      return;
    }
    for (JsonObject d : doc["datas"].as<JsonArray>()) {
      const char* code = d["itemCode"];
      if (code) out[code] = d["stockName"] | "";
    }
  }
  String body;
  serializeJson(out, body);
  server.send(200, F("application/json; charset=utf-8"), body);
}

void quoteRegisterRoutes(ESP8266WebServer& server) {
  server.on("/lookup", HTTP_GET, [&server]() { handleLookup(server); });
  server.on("/quotes", HTTP_GET, [&server]() {
    String body = F("<html><head><meta charset='utf-8'>"
                    "<meta name='viewport' content='width=device-width,initial-scale=1'></head><body>"
                    "<h3>시세</h3><table border='1' cellpadding='4'>"
                    "<tr><th>코드</th><th>종목</th><th>현재가</th><th>대비</th><th>%</th><th>시가</th><th>장</th></tr>");
    for (uint8_t i = 0; i < config.codeCount; i++) {
      const Quote& q = quotes[i];
      body += F("<tr><td>");
      body += config.codes[i];
      body += F("</td><td>");
      if (q.valid) {
        body += q.name;
        body += F("</td><td>");
        body += q.price;
        body += F("</td><td>");
        body += q.change;
        body += F("</td><td>");
        body += String(q.ratio, 2);
        body += F("</td><td>");
        body += q.open;
        body += F("</td><td>");
        body += q.nxt ? (q.nxtOpen ? F("NXT 거래 중") : F("NXT 마감"))
                      : q.marketOpen ? F("장중") : F("마감");
      } else {
        body += F("-</td><td></td><td></td><td></td><td></td><td>");
      }
      body += F("</td></tr>");
    }
    body += F("</table><p>");
    body += lastError ? F("마지막 요청 실패") : F("정상");
    body += F(" / ");
    body += lastFetchMs ? String((millis() - lastFetchMs) / 1000) + F("초 전 갱신") : String(F("아직 없음"));
    body += F("</p><p><a href='/'>처음으로</a></p></body></html>");
    server.send(200, F("text/html; charset=utf-8"), body);
  });
}
