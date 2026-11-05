#include "chart.h"

#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <time.h>

Chart charts[MAX_STOCKS];

// 네이버 증권 분봉 (비공식). 하루치 1분 데이터 JSON 배열, HTTP 로도 응답한다.
// 응답이 최대 약 60KB 라서 JSON 전체를 메모리에 올리지 않고 스트림에서 필요한 값만 읽는다.
static const char MINUTE_URL[] PROGMEM = "http://api.stock.naver.com/chart/domestic/item/";
static const uint32_t RELOAD_MS = 30UL * 60 * 1000;
static const uint32_t RETRY_MS = 60UL * 1000;
static const uint16_t HTTP_TIMEOUT_MS = 8000;
static const uint16_t SESSION_MINUTES = 390;  // 09:00~15:30

static int bucketOf(int hour, int minute) {
  int m = (hour - 9) * 60 + minute;
  if (m < 0 || m > SESSION_MINUTES) return -1;
  return min<int>(CHART_POINTS - 1, m * CHART_POINTS / (SESSION_MINUTES + 1));
}

static bool localNow(struct tm& t) {
  time_t now = time(nullptr);
  if (now < 1700000000) return false;
  localtime_r(&now, &t);
  return true;
}

static uint32_t yyyymmdd(const struct tm& t) {
  return (t.tm_year + 1900) * 10000UL + (t.tm_mon + 1) * 100 + t.tm_mday;
}

static bool chartLoad(uint8_t index) {
  Chart& c = charts[index];
  if (c.code != config.codes[index]) {
    // 다른 종목의 차트가 남아 보이지 않도록 먼저 비운다
    c = Chart();
    c.code = config.codes[index];
  }
  c.loadedMs = millis();
  if (WiFi.status() != WL_CONNECTED) return false;

  String url = FPSTR(MINUTE_URL);
  url += c.code;
  url += F("/minute");

  WiFiClient client;
  HTTPClient http;
  http.useHTTP10(true);  // chunked 없이 받아서 스트림을 바로 읽는다
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(client, url)) return false;
  if (http.GET() != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  // 항목 예: {"localDateTime":"20261105090000","currentPrice":211500.0,...}
  Stream& s = http.getStream();
  s.setTimeout(HTTP_TIMEOUT_MS);
  int32_t pts[CHART_POINTS] = {};
  uint32_t date = 0;
  char stamp[13];
  while (s.find("\"localDateTime\":\"")) {
    if (s.readBytes(stamp, 12) != 12) break;
    stamp[12] = 0;
    if (!s.find("\"currentPrice\":")) break;
    int32_t price = s.parseInt();

    int hour = (stamp[8] - '0') * 10 + (stamp[9] - '0');
    int minute = (stamp[10] - '0') * 10 + (stamp[11] - '0');
    stamp[8] = 0;
    date = strtoul(stamp, nullptr, 10);
    int b = bucketOf(hour, minute);
    if (b >= 0 && price > 0) pts[b] = price;
  }
  http.end();

  memcpy(c.pts, pts, sizeof(pts));
  c.date = date;
  c.loaded = true;
  return true;
}

int chartUpdate(bool marketOpen) {
  struct tm t;
  bool haveTime = localNow(t);
  for (uint8_t i = 0; i < config.codeCount; i++) {
    Chart& c = charts[i];
    uint32_t age = millis() - c.loadedMs;
    // 데이터가 없으면(부팅·종목 변경) 장 상태와 상관없이 받는다. 실패하면 1분 뒤 재시도.
    bool stale = c.code != config.codes[i] || (!c.loaded && (c.loadedMs == 0 || age > RETRY_MS));
    // 장중에만: 30분마다 전체 재요청(빠진 칸 보정), 새 거래일이면 바로 교체
    if (!stale && marketOpen) {
      stale = age > RELOAD_MS || (haveTime && c.date != yyyymmdd(t) && age > RETRY_MS);
    }
    if (stale) {
      chartLoad(i);
      return i;  // 한 번에 하나만 (OTA·화면 갱신이 막히지 않도록)
    }
  }
  return -1;
}

void chartAppend(uint8_t index, int32_t price) {
  Chart& c = charts[index];
  struct tm t;
  if (!c.loaded || price <= 0 || !localNow(t) || c.date != yyyymmdd(t)) return;
  int b = bucketOf(t.tm_hour, t.tm_min);
  if (b >= 0) c.pts[b] = price;
}

void chartDraw(Adafruit_GFX& g, int16_t originY, uint8_t index, int16_t left, int16_t right,
               int16_t top, int16_t height, int32_t base, uint16_t upColor, uint16_t downColor,
               uint16_t baseColor, std::function<void()> onPoint) {
  const Chart& c = charts[index];
  if (!c.loaded || c.code != config.codes[index]) return;

  int32_t lo = base, hi = base;
  int count = 0;
  for (uint8_t i = 0; i < CHART_POINTS; i++) {
    if (!c.pts[i]) continue;
    lo = min(lo, c.pts[i]);
    hi = max(hi, c.pts[i]);
    count++;
  }
  if (count == 0 || base <= 0) return;
  if (hi == lo) {
    hi++;
    lo--;
  }

  const int16_t pad = 4;
  int16_t yTop = top + pad - originY;
  int16_t span = height - pad * 2 - 2;  // 선 두께 2px
  auto yOf = [&](int32_t p) { return (int16_t)(yTop + (int32_t)(hi - p) * span / (hi - lo)); };

  // 칸 i 의 x: left ~ right-1 에 고르게 배치
  int16_t lastX = right - 1;
  auto xOf = [&](uint8_t i) {
    return (int16_t)(left + (int32_t)i * (lastX - left) / (CHART_POINTS - 1));
  };

  // 시가 기준 점선
  int16_t by = yOf(base);
  for (int16_t x = left; x <= lastX; x += 8) {
    g.drawFastHLine(x, by, min<int16_t>(4, lastX - x + 1), baseColor);
  }

  auto segment = [&](int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    g.drawLine(x0, y0, x1, y1, color);
    g.drawLine(x0, y0 + 1, x1, y1 + 1, color);
  };

  // 가격 선 (빈 칸은 건너뛰고 앞뒤 점을 잇는다).
  // 기준선을 가로지르는 구간은 교차점에서 잘라 위·아래 색을 나눈다.
  int16_t px = -1, py = 0;
  for (uint8_t i = 0; i < CHART_POINTS; i++) {
    if (!c.pts[i]) continue;
    int16_t x = xOf(i);
    int16_t y = yOf(c.pts[i]);
    if (px >= 0) {
      bool crosses = (py < by && y > by) || (py > by && y < by);
      if (crosses) {
        int16_t cx = px + (int32_t)(x - px) * (by - py) / (y - py);
        segment(px, py, cx, by, py < by ? upColor : downColor);
        segment(cx, by, x, y, y < by ? upColor : downColor);
      } else {
        // 한쪽 끝이 기준선 위에 걸치면 다른 쪽 끝으로 판단
        bool up = py < by || y < by;
        segment(px, py, x, y, up ? upColor : downColor);
      }
      if (onPoint) onPoint();
    }
    px = x;
    py = y;
  }
}
