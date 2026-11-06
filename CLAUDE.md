# geekmagic-krx-ticker — 국내 주식 시세 표시기 (기기 이름 GeekStock)

하드웨어·빌드·업로드는 `README.md` 참고 (로컬 작업 환경의 공통 규칙은 `../CLAUDE.md`).

## 현재 상태 (2026-11-06)
- 펌웨어 0.8.0 (빌드 하나로 배포: 첫 부팅 설정 모드, 관리 비밀번호, 설정 초기화, 기본 밝기 100%), 원래 계획 1~6단계 완료
  1. 화면 + OTA + 복구 모드 / 2. Wi-Fi 설정 페이지·설정 저장·NTP / 3. 시세 조회·로테이트
  4. 디자인(한글 종목명, 7-세그먼트) / 5. 당일 차트 / 6. 장 운영 시간 처리
- 기기 웹 페이지: `/` 상태, `/settings` 설정(Wi-Fi·종목 코드·간격·밝기·관리 비밀번호, 코드 입력 시 종목명 표시), `/reset` 설정 초기화(POST), `/quotes` 받은 시세 표, `/lookup?codes=` 종목명 조회 JSON, `/update` OTA

## 파일 구성
- `geekmagic-krx-ticker.ino` — 레이아웃 상수, 화면 그리기(drawHeader/drawQuote/drawChart), 장 시간 처리(shouldFetch), loop
- `ota.*` — Wi-Fi(STA, SSID 없거나 실패 시 열린 AP `GeekStock` + 캡티브 포털), mDNS, `/update`, 복구 모드(EEPROM 부팅 카운터). AP 이름·관리 계정 이름(`admin`) 상수
- `config.*` — LittleFS `/config.json`(Wi-Fi·종목·간격·밝기·관리 비밀번호 `admin`), `/settings`·`/reset`, 관리 비밀번호 Basic 인증
- `quote.*` — 시세 조회, `/quotes` 페이지
- `chart.*` — 분봉 조회(스트림 파싱), 108칸 차트 데이터, 차트 그리기
- `textkr.*` — 한글 비트맵 폰트 출력(krDraw/krWidth), 7-세그먼트 숫자(segDrawRight)
- `font_kr.h` — 생성 파일. 직접 수정 금지
- `tools/gen_font.py` — 폰트 생성기 (Pretendard Bold 22px, KS X 1001 한글 2,350자 + ASCII, 2bpp)
  - 실행: Pillow 가 있는 venv 에서 `python gen_font.py Pretendard-Bold.otf ../font_kr.h`
  - 폰트 라이선스: `tools/Pretendard-LICENSE.txt` (SIL OFL)
- 빌드 시 비밀 값 없음 (0.8.0 에서 `secrets.h` 제거). 개인 Wi-Fi·비밀번호는 기기 `/config.json` 에만 있고 OTA 로 유지됨
  - 그래서 직접 빌드한 `.bin` 이 곧 배포용. 로컬에 예전 `secrets.h` 가 남아 있어도 읽지 않음(.gitignore 유지)
- `local/` — git 제외. 원본 사진·영상, 참고 이미지(`참고1.png`), 메모. 컴퓨터를 바꾸면 따로 옮길 것
- `docs/` — README 용으로 줄인 사진·GIF (git 포함)

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
- 증감 금액 / 등락률: 화면을 반으로 나눠 각 칸 오른쪽 정렬. 숫자 잉크 가운데가 화면 세로 가운데(y=120, 잉크 113~127). 색은 전일 종가 기준(상승 빨강 #F82020, 하락 파랑)
- 차트: 선만(채움·끝점 없음), 시가 기준 회색 점선, 점선 위 구간 빨강 / 아래 구간 파랑(교차점에서 분할)
- 화면 갱신(로테이션·시세 수신) 순서: 종목명 → 현재가 → 증감 → 차트. 증감·차트 영역은 먼저 비움. 단계 사이 100ms 쉼
  - 현재가는 일의 자리부터 자리마다 0→목표 숫자로 세며 등장(30ms/단계, 윗자리·쉼표는 차례 전까지 숨김, 자리 위치는 고정)
  - 차트는 09:00 부터 시간 순서로 선을 이어 그림(칸당 10ms, 캔버스 없이 화면에 바로). 차트 재수신 때는 drawChart() 로 한 번에
  - 애니메이션 중에는 otaHandle() 을 부르지 않음 (그리기 깊은 곳에서 웹 핸들러가 돌면 4KB 스택 위험). 웹 요청은 그리기가 끝난 뒤 처리
- 세로 배치: 헤더 0~34, 현재가 43~95(잉크 46~90, 시계와 증감 사이 가운데), 증감 95~140(잉크 113~127), 차트 140~224(선 144~219). 증감↔차트 선 빈 간격 16px

## 동작 규칙
- 복구 모드: 설정 파일을 읽지 않고 열린 AP `GeekStock` 로만 뜸. 화면에 노란 "복구 모드" + Wi-Fi 이름 + `192.168.4.1` 표시. `/update` 비밀번호 없음. `/quotes`·`/settings` 없음(Not found)
- AP 설정 모드(Wi-Fi 정보 없음 = "Wi-Fi 설정 필요" / 연결 실패 = "Wi-Fi 연결 실패"): 화면에 `GeekStock`·"비밀번호 없음"·`http://192.168.4.1` 안내, 시세 동작 안 함
  - AP 모드에 들어가면 재부팅 전까지 STA 재시도 안 함 → 핫스팟이 꺼진 채 부팅했으면 전원을 한 번 다시 켜야 함
- 관리 비밀번호: 처음엔 없음. `/settings` 에서 정하면 `/settings`·`/reset`·`/update` 를 `admin`/비밀번호로 보호. 잊으면 복구 모드로 업로드는 가능
- 밝기 기본값 100% (75~80% 는 칙칙함). 복구 모드도 100%
- `/` 상태 페이지: 이번 부팅 원인(ESP.getResetReason) + EEPROM 에 남긴 마지막 크래시(reason/exccause/epc1)
  - epc 주소 → `xtensa-lx106-elf-addr2line -e build/geekmagic-krx-ticker.ino.elf 0x...` 로 위치 확인
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
- 펌웨어 약 667KB (폰트 약 218KB 포함 — 글자마다 위아래 빈 줄·오른쪽 빈칸을 잘라 저장), OTA 한도 약 1019KB

## 릴리즈
- GitHub Releases 는 사용자에게 보이는 변화를 실기기에서 확인했을 때만 (푸시마다 하지 않음). 문서 수정은 릴리즈 불필요
- 절차: `FW_VERSION` 올림 → 커밋·푸시 → `build/` 지우고 다시 컴파일 → `.bin` 에 개인 정보(Wi-Fi·IP·이메일·`/Users/`) 없는지 확인
  → `gh release create v<버전> <파일> --target main` (파일명 `geekmagic-krx-ticker-<버전>.bin`, 설명에 설치 순서·변경 내용·SHA-256)
- `gh` 는 heyzzung 계정으로 로그인됨 (keyring)
- 0.8.0 릴리즈 (2026-11-06)

## 남은 일
- 15:40 이후 요청을 멈추는지 확인 (`/quotes` 의 "N초 전 갱신" 이 계속 커지는지)
- NXT 표시 실기기 확인 — 18:26 에 NXT 가격·하늘색 점 표시는 확인(2026-11-05). 남은 것: 가격이 계속 갱신되는지, 20:10 이후 요청 멈춤. 프리마켓(08:00~)은 미지원
- 공장 펌웨어에서 릴리즈 `.bin` 을 처음 올리는 경로는 미확인 (이 기기는 이미 우리 펌웨어). 첫 부팅 설정 흐름은 설정 초기화로 확인함
- `/` 에 남은 마지막 크래시(reason 2, cause 9, epc 0x402134da)는 언제 생겼는지 모름. 당시 `.elf` 가 없어 위치 확인 불가 → 재발하면 그때 addr2line
- 원하면:
  - 부팅 후 Wi-Fi 연결되면 화면에 IP 를 몇 초 표시 (지금은 mDNS `geekstock.local` 외에 IP 알 방법이 공유기·앱뿐)
  - AP 모드에서 저장된 Wi-Fi 로 주기적 재연결 시도 (지금은 핫스팟이 꺼진 채 부팅하면 전원을 다시 켜야 함)
  - 코스피·코스닥 지수, 환율 화면
- 하지 않기로 함: 야간 화면 끄기·시간대별 밝기, 급등락·목표가 강조
