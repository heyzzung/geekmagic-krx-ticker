// GeekMagic SmallTV Ultra 주식 시세 표시기
// 6단계: 장 운영 시간 처리 — 장 외 시간에는 저장된 값으로 로테이션만

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <time.h>

#include <functional>

#include "chart.h"
#include "config.h"
#include "ota.h"
#include "quote.h"
#include "textkr.h"

#define FW_VERSION "0.8.0"

// SmallTV Ultra 핀 (실기기 확인 완료, 화면 방향은 rotation 2)
#define TFT_DC 0
#define TFT_RST 2
#define TFT_BL 5
#define TFT_BL_ACTIVE_LOW true

// 색 (RGB565)
#define COLOR_BG 0x0000     // 검정
#define COLOR_TEXT 0xFFFF
#define COLOR_PRICE 0xFD48  // 주황 #FFA840
#define COLOR_UP 0xF904     // 상승 빨강 #F82020 (#F04040 과 #FF0000 의 중간)
#define COLOR_DOWN 0x4C1F   // 하락 파랑 #4A80FF
#define COLOR_FLAT 0x8C71   // 보합/마감 회색
#define COLOR_LIVE 0x4D7F   // 장중 표시 점 하늘색

static Adafruit_ST7789 tft(-1, TFT_DC, TFT_RST);
static bool recoveryMode = false;
static time_t lastDrawnTime = 0;
static uint8_t currentIndex = 0;
static uint32_t lastRotateMs = 0;
static uint32_t lastFetchMs = 0;
static time_t lastFetchTime = 0;  // 마지막 요청 시각 (NTP 기준)
static bool fetchedOnce = false;

// 장 외 시간·휴장일 확인 주기
static const uint32_t CLOSED_CHECK_MS = 30UL * 60 * 1000;
static const uint32_t RETRY_MS = 60UL * 1000;

// 평일 from~to (분 단위, to 는 포함하지 않음)
static bool inWeekdayWindow(int from, int to) {
  if (!timeValid()) return false;
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  int minutes = t.tm_hour * 60 + t.tm_min;
  bool weekday = t.tm_wday >= 1 && t.tm_wday <= 5;
  return weekday && minutes >= from && minutes < to;
}

// 정규장: 평일 09:00~15:40 (15:30 장 마감 후 확정 종가를 받을 여유 10분 포함)
static bool inSessionWindow() {
  return inWeekdayWindow(9 * 60, 15 * 60 + 40);
}

// NXT 애프터마켓: 평일 15:30~20:10 (20:00 마감 후 확정 가격을 받을 여유 10분 포함)
static bool inNxtWindow() {
  return inWeekdayWindow(15 * 60 + 30, 20 * 60 + 10);
}

// 지금 장중인지: 마지막 응답이 '장중'이어도 시간 창 밖이면 장중이 아니다
static bool marketOpenNow() {
  if (!inSessionWindow()) return false;
  for (uint8_t i = 0; i < config.codeCount; i++) {
    if (quotes[i].valid && quotes[i].marketOpen) return true;
  }
  return false;
}

// 지금 NXT 애프터마켓이 거래 중인지
static bool nxtOpenNow() {
  if (!inNxtWindow()) return false;
  for (uint8_t i = 0; i < config.codeCount; i++) {
    if (quotes[i].valid && quotes[i].nxtOpen) return true;
  }
  return false;
}

static bool allQuotesValid() {
  for (uint8_t i = 0; i < config.codeCount; i++) {
    if (!quotes[i].valid) return false;
  }
  return true;
}

// 시세를 지금 받아야 하는지.
// - 값이 없으면(부팅·종목 변경) 바로, 실패하면 1분 뒤 재시도
// - 평일 09:00~15:40(정규장)·15:30~20:10(NXT 애프터마켓) 창 안에서만 주기적으로 받는다.
//   창 밖(밤·주말)은 요청하지 않는다.
//   - 오늘 창에 들어온 뒤 첫 요청은 바로 (어제 상태가 '마감'이어도 개장을 놓치지 않게)
//   - 정규장·NXT 가 거래 중이면 설정한 갱신 주기, 창 안인데 마감 상태(휴장일·개장 지연)면 30분 주기
//   20:00 이후 첫 요청이 NXT 확정 가격을 받고, 다음 확인은 창이 끝난 뒤라 멈춘다.
static bool shouldFetch(uint32_t ms) {
  uint32_t since = ms - lastFetchMs;
  if (!fetchedOnce) return true;
  if (quoteHasError() || !allQuotesValid()) return since >= RETRY_MS;

  if (!timeValid()) return since >= config.refreshSec * 1000UL;  // 시계 맞기 전 임시
  if (!inSessionWindow() && !inNxtWindow()) return false;

  time_t now = time(nullptr);
  struct tm open;
  localtime_r(&now, &open);
  open.tm_hour = 9;
  open.tm_min = 0;
  open.tm_sec = 0;
  if (lastFetchTime < mktime(&open)) return true;

  bool trading = marketOpenNow() || nxtOpenNow();
  return since >= (trading ? config.refreshSec * 1000UL : CLOSED_CHECK_MS);
}

static void setBacklight(uint8_t percent) {
  int duty = map(percent, 0, 100, 0, 1023);
  analogWrite(TFT_BL, TFT_BL_ACTIVE_LOW ? 1023 - duty : duty);
}

// NTP 로 받은 시각이 유효한지 (동기화 전에는 1970년)
static bool timeValid() {
  return time(nullptr) > 1700000000;
}

// 1234567 -> "1,234,567"
static String withCommas(int32_t v) {
  String digits = String(v < 0 ? -v : v);
  String out;
  int len = digits.length();
  for (int i = 0; i < len; i++) {
    if (i && (len - i) % 3 == 0) out += ',';
    out += digits[i];
  }
  return v < 0 ? "-" + out : out;
}

// ---- 레이아웃 (240×240) ----
// 좌우·위 여백 8px. 아래는 케이스 테두리가 패널 가장자리를 가려서 12px.
// 위 여백은 한글 글자의 잉크 기준 (22px 폰트에서 한글 잉크는 셀 1px 아래부터).
static const int16_t MARGIN_X = 8;
static const int16_t RIGHT = 240 - MARGIN_X;
static const int16_t HEADER_H = 34;
static const int16_t TEXT_Y = 7;  // 헤더 글자 셀 y → 한글 잉크 y=8
// 증감 숫자는 화면 세로 가운데. 현재가는 시계 잉크 아래(24)와 증감 잉크 위(113) 사이 가운데에 둔다.
// 34~43 은 줄 그리기에서 빠지지만 부팅 때 전체를 지우므로 검정으로 남는다.
// 현재가 숫자 25×45, 잉크 46~90 (쉼표 꼬리 ~92) → 위 빈 간격 21, 아래 22
static const int16_t PRICE_Y = 43, PRICE_H = 52, PRICE_DIGIT_Y = 3, PRICE_DIGIT_H = 45;
// 숫자 잉크(셀 3~17px)의 가운데를 화면 세로 가운데(120)에: 잉크 113~127
static const int16_t CHANGE_Y = 95, CHANGE_H = 45, CHANGE_TEXT_Y = 15;
static const int16_t CHANGE_SPLIT = 120;  // 증감 금액(왼쪽 칸) / 등락률(오른쪽 칸)
// 차트 영역 140~224. 선은 영역 안쪽 4px 패딩 + 두께 2px → 선 y=144~219 (아래 여백 20)
static const int16_t CHART_TOP = 140, CHART_H = 84, CHART_BANDS = 3;

// 화면 영역(x, y, w × h)을 메모리 캔버스에 그린 뒤 한 번에 전송한다.
// 화면을 지웠다 다시 그리면 깜빡임·잔상이 생기므로 항상 덮어쓰기만 한다.
// 240×52 가 약 25KB 이므로 그때그때 잡았다가 바로 해제한다.
// w 는 반드시 짝수: drawRGBBitmap 이 줄마다 버퍼 포인터를 w×2 바이트씩 넘기며 SPI 로 보내는데,
// ESP8266 SPI writeBytes 는 4바이트 정렬을 가정해서 w 가 홀수면 둘째 줄에서 예외(cause 9)로 죽는다.
static void drawBox(int16_t x, int16_t y, int16_t w, int16_t h,
                    std::function<void(GFXcanvas16&)> draw) {
  GFXcanvas16 canvas(w, h);
  if (!canvas.getBuffer()) {
    return;  // 메모리 부족: 이번 갱신은 건너뛴다
  }
  canvas.fillScreen(COLOR_BG);
  draw(canvas);
  tft.drawRGBBitmap(x, y, canvas.getBuffer(), w, h);
}

// 화면 한 줄(가로 240 × h)
static void drawRow(int16_t y, int16_t h, std::function<void(GFXcanvas16&)> draw) {
  drawBox(0, y, 240, h, draw);
}

// ---- 헤더: 종목명(왼쪽, 길면 마키) / 상태 점 + 시계(오른쪽) ----
static const uint16_t MARQUEE_STEP_MS = 30;    // 1px 이동 간격
static const int16_t MARQUEE_GAP = 32;          // 이름 끝과 다시 나오는 이름 앞 사이
static int16_t nameW = 0;                       // 종목명 칸 폭 (시계 폭에 따라 drawHeader 가 정함)
static int16_t nameOffset = 0;                  // 마키로 왼쪽으로 민 거리
static uint32_t marqueeNextMs = 0;

static String currentName() {
  if (config.codeCount == 0) return String();
  const Quote& q = quotes[currentIndex];
  return q.valid && q.name.length() ? q.name : config.codes[currentIndex];
}

// 종목명 칸만 다시 그린다. 칸보다 길면 nameOffset 만큼 민 이름과, 그 뒤에 이어 나오는 이름을 함께 그린다.
// 왼쪽 여백(0~MARGIN_X)은 그리지 않으므로 늘 검정 → 칸 밖으로 나간 글자는 잘려 보인다.
static void drawName() {
  if (nameW <= 0) return;
  String name = currentName();
  int16_t w = krWidth(name);
  drawBox(MARGIN_X, 0, nameW, HEADER_H, [&](GFXcanvas16& c) {
    if (w <= nameW) {
      krDraw(c, 0, TEXT_Y, name, COLOR_TEXT, COLOR_BG);
      return;
    }
    krDraw(c, -nameOffset, TEXT_Y, name, COLOR_TEXT, COLOR_BG);
    krDraw(c, -nameOffset + w + MARQUEE_GAP, TEXT_Y, name, COLOR_TEXT, COLOR_BG);
  });
}

// 종목이 바뀔 때: 마키를 처음 위치로 돌린다 (멈춤 없이 바로 흐름)
static void resetMarquee() {
  nameOffset = 0;
  marqueeNextMs = millis() + MARQUEE_STEP_MS;
}

// loop() 에서 호출. 종목명이 칸보다 길 때만 1px 씩 민다.
static void tickMarquee() {
  if ((int32_t)(millis() - marqueeNextMs) < 0) return;
  int16_t w = krWidth(currentName());
  if (w <= nameW) return;
  // 한 바퀴 돌면 뒤따라온 이름이 처음 위치에 온 것과 같으므로 0 으로 되감아 끊김 없이 이어 간다
  if (++nameOffset >= w + MARQUEE_GAP) nameOffset = 0;
  marqueeNextMs = millis() + MARQUEE_STEP_MS;
  drawName();
}

static void drawHeader() {
  String clock = F("--:--");
  if (timeValid()) {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    clock = buf;
  }
  int16_t clockX = RIGHT - krWidth(clock);
  // 종목명 칸 끝 = 상태 점 칸 시작. 두 칸 폭이 짝수가 되도록 짝수로 내림 (drawBox 참고)
  int16_t boxX = (clockX - 20) & ~1;
  nameW = boxX - MARGIN_X;

  drawBox(boxX, 0, 240 - boxX, HEADER_H, [&](GFXcanvas16& c) {
    krDraw(c, clockX - boxX, TEXT_Y, clock, COLOR_TEXT, COLOR_BG);

    uint16_t dot = COLOR_FLAT;
    if (quoteHasError()) {
      dot = ST77XX_YELLOW;
    } else if (config.codeCount) {
      // 정규장 마감 후 NXT 가격을 보여줄 때는 NXT 거래 여부로 판단
      const Quote& q = quotes[currentIndex];
      if (q.nxt ? inNxtWindow() && q.nxtOpen : inSessionWindow() && q.marketOpen) dot = COLOR_LIVE;
    }
    // 시계 숫자 잉크(셀 3~17px)의 가운데에 맞춘 점
    c.fillCircle(clockX - 10 - boxX, TEXT_Y + 10, 4, dot);
  });
  drawName();
}

// 당일 차트. 240×108 캔버스(52KB)는 메모리에 안 들어가서 세 띠로 나눠 그린다.
static void drawChart() {
  if (config.codeCount == 0) return;
  const Quote& q = quotes[currentIndex];
  const int16_t bandH = CHART_H / CHART_BANDS;
  for (int16_t band = 0; band < CHART_BANDS; band++) {
    int16_t y = CHART_TOP + band * bandH;
    drawRow(y, bandH, [&](GFXcanvas16& c) {
      if (!q.valid) return;
      chartDraw(c, y, currentIndex, MARGIN_X, RIGHT, CHART_TOP, CHART_H, q.open, COLOR_UP,
                COLOR_DOWN, COLOR_FLAT);
    });
  }
}

// 현재가를 일의 자리부터 한 자리씩, 0 에서 목표 숫자까지 세며 그린다 (1,230 → 0 / 0123 / 012 / 01).
// 아직 차례가 안 온 윗자리(와 그 오른쪽 쉼표)는 비워 두고, 자리 위치는 완성된 금액 기준으로 고정한다.
static const uint16_t ROLL_STEP_MS = 30;
static const uint16_t STAGE_PAUSE_MS = 100;  // 종목명·현재가·증감·차트 사이 쉬는 시간
static const uint16_t CHART_STEP_MS = 10;    // 차트 선을 한 칸(약 3.6분) 이을 때마다 쉬는 시간

// 화면 갱신 중 잠깐 쉰다. 여기서 웹 요청(otaHandle)은 처리하지 않는다:
// 그리기 함수 깊은 곳에서 설정 저장·종목명 조회·OTA 핸들러까지 돌면 4KB 스택이 넘칠 수 있다.
// 요청은 화면을 다 그린 뒤 loop() 에서 처리된다 (최대 약 3초 대기). delay() 가 Wi-Fi 는 돌려 준다.
static void pauseDraw(uint16_t ms) {
  delay(ms);
}

static void drawPriceRolling(int32_t price) {
  const String text = withCommas(price);
  String shown = text;
  for (int i = text.length() - 1; i >= 0; i--) {
    if (!isDigit(text[i])) continue;
    for (char d = '0'; d <= text[i]; d++) {
      shown[i] = d;
      drawRow(PRICE_Y, PRICE_H, [&](GFXcanvas16& c) {
        segDrawRight(c, RIGHT, PRICE_DIGIT_Y, PRICE_DIGIT_H, RIGHT - MARGIN_X, shown, COLOR_PRICE, i);
      });
      pauseDraw(ROLL_STEP_MS);
    }
  }
}

// 차트를 09:00 부터 시간 순서로 이어 그린다. 영역은 drawQuote 시작 때 비웠으므로
// 캔버스 없이 화면에 바로 선을 더해 간다 (지우지 않으니 깜빡임 없음).
static void drawChartAnimated() {
  const Quote& q = quotes[currentIndex];
  if (!q.valid) return;
  chartDraw(tft, 0, currentIndex, MARGIN_X, RIGHT, CHART_TOP, CHART_H, q.open, COLOR_UP,
            COLOR_DOWN, COLOR_FLAT, []() { pauseDraw(CHART_STEP_MS); });
}

// 화면 갱신 순서: 종목명 → 현재가(자리별로 촤라락) → 증감 → 차트
static void drawQuote() {
  resetMarquee();
  drawHeader();
  if (config.codeCount == 0) return;
  const Quote& q = quotes[currentIndex];
  uint16_t color = q.change > 0 ? COLOR_UP : q.change < 0 ? COLOR_DOWN : COLOR_FLAT;

  // 증감·차트는 현재가를 다 그린 뒤에 나타나도록 먼저 비운다 (증감 줄부터 차트 끝까지 붙어 있음)
  tft.fillRect(0, CHANGE_Y, 240, CHART_TOP + CHART_H - CHANGE_Y, COLOR_BG);
  pauseDraw(STAGE_PAUSE_MS);

  // 현재가: 7-세그먼트, 오른쪽 정렬, 모든 종목 같은 크기
  if (q.valid) {
    drawPriceRolling(q.price);
  } else {
    drawRow(PRICE_Y, PRICE_H, [&](GFXcanvas16& c) {
      krDraw(c, MARGIN_X, 16, F("불러오는 중..."), COLOR_FLAT, COLOR_BG);
    });
  }
  pauseDraw(STAGE_PAUSE_MS);

  // 등락 금액 / 등락률: 화면을 반으로 나눠 각 칸의 오른쪽에 맞춘다
  drawRow(CHANGE_Y, CHANGE_H, [&](GFXcanvas16& c) {
    if (!q.valid) return;
    String change = withCommas(q.change);
    if (q.change > 0) change = "+" + change;
    int16_t changeRight = CHANGE_SPLIT - MARGIN_X / 2;
    krDraw(c, max<int16_t>(MARGIN_X, changeRight - krWidth(change)), CHANGE_TEXT_Y, change, color,
           COLOR_BG);
    String ratio = String(q.ratio, 2) + "%";
    krDraw(c, RIGHT - krWidth(ratio), CHANGE_TEXT_Y, ratio, color, COLOR_BG);
  });
  pauseDraw(STAGE_PAUSE_MS);

  drawChartAnimated();
}

static void initDisplay(uint8_t brightness) {
  pinMode(TFT_BL, OUTPUT);
  setBacklight(brightness);
  tft.init(240, 240, SPI_MODE3);
  tft.setRotation(2);
  tft.fillScreen(COLOR_BG);  // 전체 지우기는 부팅 때 한 번만
}

// 시세 대신 안내 문구를 보여준다 (복구 모드·AP 설정 모드). 첫 줄은 제목색.
static void drawNotice(uint16_t titleColor, const String* lines, uint8_t count) {
  int16_t y = 16;
  for (uint8_t i = 0; i < count; i++) {
    krDraw(tft, MARGIN_X, y, lines[i], i ? COLOR_TEXT : titleColor, COLOR_BG, RIGHT - MARGIN_X);
    y += i ? 30 : 44;
  }
}

static bool apNotice = false;  // AP 설정 모드: 시세 대신 접속 안내만 표시

void setup() {
  // OTA 를 가장 먼저 띄운다. 이후 코드가 죽어도 업로드 경로는 남는다.
  recoveryMode = otaCheckRecovery();
  if (recoveryMode) {
    // 복구 모드는 파일시스템(저장된 Wi-Fi·비밀번호)을 건드리지 않고 열린 AP 로만 뜬다.
    // 화면은 접속 방법만 알려준다 (까만 화면이면 먹통과 구분이 안 됨).
    otaBegin(FW_VERSION, "", "", "");
    initDisplay(100);
    String lines[] = {F("복구 모드"), F("Wi-Fi: " AP_SSID), otaAddress(),
                      F("/update 로 펌웨어 업로드"), F("전원을 다시 켜면 정상 부팅")};
    drawNotice(ST77XX_YELLOW, lines, 5);
    return;
  }

  configLoad();
  otaBegin(FW_VERSION, config.wifiSsid, config.wifiPass, config.adminPass);
  configRegisterRoutes(otaServer());
  quoteRegisterRoutes(otaServer());
  configTime("KST-9", "pool.ntp.org", "time.google.com");

  initDisplay(config.brightness);
  if (otaIsApMode()) {
    // Wi-Fi 정보가 없거나 연결 실패 → 기기가 만든 Wi-Fi 에 접속해 설정하도록 안내
    apNotice = true;
    String lines[] = {config.wifiSsid.isEmpty() ? F("Wi-Fi 설정 필요") : F("Wi-Fi 연결 실패"),
                      F("Wi-Fi: " AP_SSID),
                      F("비밀번호 없음"),
                      F("접속 후 아래 주소에서 설정"), String(F("http://")) + otaAddress()};
    drawNotice(ST77XX_YELLOW, lines, 5);
    return;
  }
  drawQuote();
}

void loop() {
  otaHandle();
  if (recoveryMode) {
    return;
  }

  if (configRestartRequested()) {
    delay(500);  // 응답 페이지가 전송될 시간
    ESP.restart();
  }
  if (configTakeChanged()) {
    setBacklight(config.brightness);
    currentIndex = 0;
    fetchedOnce = false;  // 종목이 바뀌었을 수 있으니 바로 다시 받는다
  }
  if (apNotice) {
    return;  // AP 설정 모드: 인터넷이 없으니 안내 화면 유지 (Wi-Fi 를 저장하면 위에서 재시작)
  }

  uint32_t ms = millis();
  if (shouldFetch(ms)) {
    fetchedOnce = true;
    lastFetchMs = ms;
    lastFetchTime = time(nullptr);
    if (quoteFetchAll()) {
      for (uint8_t i = 0; i < config.codeCount; i++) {
        // 차트는 정규장만 (NXT 가격은 넣지 않는다)
        if (quotes[i].marketOpen && !quotes[i].nxt) chartAppend(i, quotes[i].price);
      }
    }
    drawQuote();
    lastRotateMs = millis();
  }

  // ms 는 시세 요청 전에 잰 값이라 그 사이 lastRotateMs 가 더 커졌을 수 있다 → millis() 로 비교
  if (config.codeCount > 1 && millis() - lastRotateMs >= config.rotateSec * 1000UL) {
    currentIndex = (currentIndex + 1) % config.codeCount;
    drawQuote();
    lastRotateMs = millis();  // 로테이트 간격은 차트까지 다 그린 뒤부터 센다
  }

  if (chartUpdate(marketOpenNow()) == currentIndex) {
    drawChart();
  }

  time_t now = time(nullptr);
  if (now / 60 != lastDrawnTime / 60) {
    lastDrawnTime = now;
    drawHeader();
  }

  tickMarquee();
}
