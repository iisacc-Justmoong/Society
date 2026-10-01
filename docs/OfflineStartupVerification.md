# iPhone 독립 실행 검증 — 2026-09-29

수정본을 iPhone 15 Pro Max에 덮어쓰기 설치했다. 계정, 페어링 경로 및 앱 데이터는
삭제하지 않았다. 최종 앱은 진단 환경변수 없이 실행 중인 PID 1052로 두 차례 확인했다.

## 원인과 변경

실기기 `Society-2026-09-29-161842.ips`는 실행 약 0.99초 후 메인 스레드에서
`setAccountSession → disconnectSessionImpl → queued_activate`의 널 포인터 접근으로 종료되었다.
구형 실행 파일 UUID는 `DF988607-5C00-3297-958A-3246D71E03C5`이다.
그 파일에서 `NearbyDevices`와 다음 멤버 사이의 간격은 256바이트인 반면,
링크된 SDK의 실제 객체 크기는 264바이트였다. SDK의 `clear()`가 256바이트
위치까지 초기화하면서 다음 객체의 가상 함수 테이블 포인터를 덮어쓰는 구조였다.

자체 SDK의 imported include를 일반 헤더로 추적하여 Xcode `-MMD` 의존성 누락을
제거했다. 새 의존성 파일에는 `NearbyDevices.h`, `AutomaticPairing.h`,
`AccountSession.h`가 포함된다. 새 앱 UUID는 `C31603E8-B78A-30BE-8DE0-B3F6CD0D170D`이다.

모바일 기본 화면의 표시 조건을 계정·미러·호스트의 준비 상태에서 분리했다.
SDK의 기존 비동기 연결 및 동기화 워커를 유지하며, 파일 접근 인증 조건은 유지한다.

## 실기기 결과

아래 시간은 QML 루트 생성 이후 측정값이며 앱 아이콘 터치부터의 전체 실행 시간이 아니다.

| 실행 | 로컬 작업공간 준비 | 통신 연결 | 화면 상태 |
| --- | ---: | ---: | --- |
| 미연결 1, PID 1042 | 871 ms | 연결 비활성 | Dashboard·Storage·Tools 전환, 16.16초 동안 화면 유지 |
| 미연결 2, PID 1044 | 810 ms | 연결 비활성 | Dashboard·Storage·Tools 전환, 16.01초 동안 화면 유지 |
| 자동 연결 1, PID 1048 | 1,277 ms | 2,551 ms | 연결 전후 화면 유지 |
| 자동 연결 2, PID 1050 | 1,313 ms | 2,320 ms | 연결 전후 화면 유지 |

미연결 probe는 해당 프로세스의 네트워크 런타임만 비활성화했다. 기기의 비행기 모드를
조작한 테스트가 아니다. 모든 실행에서 `onboardingRequired=false`, `shellVisible=true`를
확인했다. 자동 연결 측정 종료 시점에는 `hostConnectionReady=false`였으므로 이 기록을
전체 동기화 완료의 증거로 해석하지 않는다. 연결 없이 화면과 로컬 작업공간이 먼저
준비되고 이후 통신 연결이 이루어졌다는 증거이다.

최초 반복 시 devicectl이 한 번 오류 10004를 반환했다. 새 크래시 기록은 없었으며,
동일 명령 재실행으로 미연결 2회 및 자동 연결 2회의 검증을 완료했다.
최종 크래시 목록에는 수정 이전 09:56, 14:34, 16:18의 네 기록만 존재했다.

## 빌드와 회귀 검증

- iOS Release: `BUILD SUCCEEDED`.
- 앱, File Provider, Live Activity 서명·프로비저닝·필수 런타임 검사 통과.
- 단일 앱 감사: `build/bin/Society.app` 하나, 중첩 앱 없음.
- 설치 SDK 헤더 변경 후 증분 재컴파일 테스트 1개 및 iOS 빌드 계약 테스트 3개 통과.
- 최종 관련 QtTest 결과: 페어링 9/9, 모바일 클라이언트 7/7, 미연결 화면 3/3 통과.
  이 수치는 각 실행의 init/cleanup 항목을 포함한다.
- 모바일 서비스 생명주기 CTest 통과, `git diff --check` 통과.

전체 Pairing/ClientOnlyNetwork suite는 통과로 보고하지 않는다. 실행 중 기존 인증
fixture 관련 6개 실패 및 60초 제한시간 초과가 관측되었다. 최종 검증은 실제 계정
fixture의 최초 연결·캐시 복원·QR 파일 읽기와 이번 변경에 관련된 테스트로 범위를
명시했으며, 전체 suite의 정비는 별도 잔여 사항이다.

원시 증거는 `build/offline-startup-20260929/`에 보존했다. 핵심 파일은
`latest.ips`, `ios-build.log`, `bundle-verification.json`, `install.json`,
`offline-device-retry/summary.json`, `reconnect-device/summary.json`,
`normal-processes-final.json`, `crashes-final.json`, `app-audit.json` 및
`pairing-final.log`, `client-final.log`, `shell-final.log`이다.
