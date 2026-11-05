# stock — 국내 주식 시세 표시기

하드웨어·빌드·업로드는 `README.md` 참고 (로컬 작업 환경의 공통 규칙은 `../CLAUDE.md`).

## 현재 상태 (2026-11-05)
- 펌웨어 0.7.0 (NXT 애프터마켓 표시 추가), 원래 계획 1~6단계 완료
  1. 화면 + OTA + 복구 모드 / 2. Wi-Fi 설정 페이지·설정 저장·NTP / 3. 시세 조회·로테이트
  4. 디자인(한글 종목명, 7-세그먼트) / 5. 당일 차트 / 6. 장 운영 시간 처리
- 기기 웹 페이지: `/` 상태, `/settings` 설정(Wi-Fi·종목 코드·간격·밝기, 코드 입력 시 종목명 표시), `/quotes` 받은 시세 표, `/lookup?codes=` 종목명 조회 JSON, `/update` OTA

## 파일 구성
- `stock.ino` — 레이아웃 상수, 화면 그리기(drawHeader/drawQuote/drawChart), 장 시간 처리(shouldFetch), loop
- `ota.*` — Wi-Fi(STA, 실패 시 AP `GeekStock` + 캡티브 포털), mDNS, `/update`, 복구 모드(EEPROM 부팅 카운터)
- `config.*` — LittleFS `/config.json`, `/settings` 페이지
- `quote.*` — 시세 조회, `/quotes` 페이지
- `chart.*` — 분봉 조회(스트림 파싱), 108칸 차트 데이터, 차트 그리기
- `textkr.*` — 한글 비트맵 폰트 출력(krDraw/krWidth), 7-세그먼트 숫자(segDrawRight)
- `font_kr.h` — 생성 파일. 직접 수정 금지
- `tools/gen_font.py` — 폰트 생성기 (Pretendard Bold 22px, KS X 1001 한글 2,350자 + ASCII, 2bpp)
  - 실행: Pillow 가 있는 venv 에서 `python gen_font.py Pretendard-Bold.otf ../font_kr.h`
  - 폰트 라이선스: `tools/Pretendard-LICENSE.txt` (SIL OFL)
- `secrets.h` — Wi-Fi SSID/비밀번호, OTA 계정, AP 비밀번호. git 제외(`.gitignore`), 형식은 `secrets.example.h`

## 데이터 출처 (네이버 증권, 비공식 — 바뀌거나 막힐 수 있음)
- 시세: `http://polling.finance.naver.com/api/realtime/domestic/stock/<코드,코드,...>` — 여러 종목 한 번에
  - 사용 필드: itemCode, stockName, closePriceRaw, compareToPreviousClosePriceRaw, fluctuationsRatioRaw, openPriceRaw, marketStatus
  - NXT: `overMarketPriceInfo` 의 tradingSessionType, overMarketStatus, overPrice, compareToPreviousClosePrice, fluctuationsRatio
    - 값이 `"-16,000"` 처럼 쉼표 문자열로만 옴 (Raw 없음)
    - 정규장 `marketStatus` 는 15:30 이후에도 `OPEN`(marketStatusDetailType `break`)으로 옴 → 마감 판단은 시간 창으로
- 분봉: `http://api.stock.naver.com/chart/domestic/item/<코드>/minute` — 하루치 1분 데이터 JSON 배열(최대 약 60KB)
- 둘 다 HTTP 로 받음(TLS 메모리 절약), `useHTTP10(true)` 로 chunked 회피

## 확정된 디자인
- 배경 검정, 여백 좌우·위 8px(위는 한글 잉크 기준), 아래: 차트 선 기준 20px(케이스 테두리가 패널 아래를 가림)
- 헤더: 종목명(왼쪽, 칸보다 길면 마키: 멈춤 없이 30ms/1px 왼쪽으로 계속 이동, 끝과 다음 반복 사이 32px) / 상태 점 + 시계(오른쪽). 점: 장중 하늘색, 마감 회색, 통신 오류 노랑
- 현재가: 주황(#FFA840) 7-세그먼트, 모든 종목 숫자 25×45(비율 9:16) 고정 (1,000,000 대 7자리까지 줄지 않음), 오른쪽 정렬. `1` 도 다른 숫자와 같은 칸(실제 7-세그먼트처럼 왼쪽 빈 공간), 쉼표는 폰트가 아니라 코드로 그린 7-세그먼트식 짧은 기운 획(아래 획 높이에서 t*1.5 길이). 참고 이미지 식 긴 세그먼트 쉼표는 해 보고 되돌림
- 증감 금액 / 등락률: 화면을 반으로 나눠 각 칸 오른쪽 정렬. 숫자 잉크 가운데가 화면 세로 가운데(y=120, 잉크 113~127). 색은 전일 종가 기준(상승 빨강 #F04040, 하락 파랑)
- 차트: 선만(채움·끝점 없음), 시가 기준 회색 점선, 점선 위 구간 빨강 / 아래 구간 파랑(교차점에서 분할)
- 화면 갱신(로테이션·시세 수신) 순서: 종목명 → 현재가 → 증감 → 차트. 증감·차트 영역은 먼저 비움. 단계 사이 100ms 쉼
  - 현재가는 일의 자리부터 자리마다 0→목표 숫자로 세며 등장(30ms/단계, 윗자리·쉼표는 차례 전까지 숨김, 자리 위치는 고정)
  - 차트는 09:00 부터 시간 순서로 선을 이어 그림(칸당 10ms, 캔버스 없이 화면에 바로). 차트 재수신 때는 drawChart() 로 한 번에
  - 애니메이션 중에는 otaHandle() 을 부르지 않음 (그리기 깊은 곳에서 웹 핸들러가 돌면 4KB 스택 위험). 웹 요청은 그리기가 끝난 뒤 처리
- 세로 배치: 헤더 0~34, 현재가 43~95(잉크 46~90, 시계와 증감 사이 가운데), 증감 95~140(잉크 113~127), 차트 140~224(선 144~219). 증감↔차트 선 빈 간격 16px

## 동작 규칙
- 복구 모드: 화면에 노란 "복구 모드" + 접속 IP 표시 (예전엔 까만 화면이라 먹통과 구분 안 됨). `/quotes`·`/settings` 없음(Not found)
- AP 설정 모드(Wi-Fi 연결 실패): 화면에 AP 이름·비밀번호·`http://192.168.4.1` 안내, 시세 동작 안 함
- `/` 상태 페이지: 이번 부팅 원인(ESP.getResetReason) + EEPROM 에 남긴 마지막 크래시(reason/exccause/epc1)
  - epc 주소 → `xtensa-lx106-elf-addr2line -e build/stock.ino.elf 0x...` 로 위치 확인
- 장 시간 창: 평일 09:00~15:40 (15:30 마감 후 확정 종가용 10분), NXT 창: 평일 15:30~20:10
  - 정규장 마감 후(tradingSessionType `AFTER_MARKET`)에는 현재가·증감·등락률을 NXT 값으로 표시. 시가·차트는 정규장 그대로
  - 상태 점: NXT 값을 보여줄 때는 NXT 거래 중이면 하늘색
  - 장중: 설정한 주기(기본 60초)로 시세, 차트는 30분마다 전체 재요청 + 시세로 현재 칸 갱신
  - 창 안인데 '마감' 상태(휴장일·개장 지연): 30분마다 확인
  - NXT 거래 중에도 설정한 주기로 시세 요청 (차트는 갱신 안 함)
  - 창 밖(20:10 이후·주말·밤): 요청 안 함, 저장된 값으로 로테이션만
  - 값이 없을 때(부팅·종목 변경)는 시간과 상관없이 한 번 받음, 실패 시 1분 뒤 재시도
- 종목 로테이트: 설정 간격(기본 10초)을 화면을 다 그린 뒤(차트 애니메이션 끝)부터 셈, 입력 장치 없음

## 메모리 제약
- 남은 heap 약 40KB. 화면은 `drawRow()` 로 줄 단위 GFXcanvas16 에 그린 뒤 전송(깜빡임 방지)
  - 줄 하나 최대 약 25KB, 차트는 3개 띠로 나눔. 전체 화면 지우기는 부팅 때 한 번만
- 펌웨어 약 760KB (폰트 323KB 포함), OTA 한도 약 1019KB

## 남은 일
- 0.6.0 레이아웃(여백·간격·`1`·쉼표) 실제 화면 사진으로 확인
- 15:40 이후 요청을 멈추는지 확인 (`/quotes` 의 "N초 전 갱신" 이 계속 커지는지)
- 화면 추가 수정 (사용자가 다음 세션에서 요청 예정)
- NXT 표시 실기기 확인 (15:30~20:00 가격 갱신, 20:10 이후 요청 멈춤). 프리마켓(08:00~)은 미지원
- 원하면: 코스피·코스닥 지수, 환율 화면
- 하지 않기로 함: 야간 화면 끄기·시간대별 밝기, 급등락·목표가 강조
