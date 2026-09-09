# Society iOS / iPadOS

현재 Society에 연결된 저장소·탐색·모델 가져오기·Helper 관측·데이터 수신과 iisacc 계정 로그인을 iOS 16 이상에서 사용하는 구현이다. Qt 6.8.3과 LVRS UI, Apple의 Foundation·UIKit·QuickLook·FileProvider, Qt SQL의 SQLite 드라이버를 사용한다. QR 카메라는 Apple AVFoundation을 사용하며 QR 생성에는 MIT 라이선스의 Nayuki 소스를 고정 버전으로 포함한다. 유료 서비스는 추가하지 않는다. 계정 UI·SDK 구현과 운영 API 배포 상태는 [Account.md](Account.md)에 구분해 기록한다.

공통 화면은 LVRS가 제공하는 기기의 상하좌우 안전 영역을 적용하여 iPhone의 상태 표시줄·노치·홈 표시 영역에 제목이나 하단 버튼이 겹치지 않게 한다. 별도 모바일 QML 화면은 두지 않는다.

| 기능 | iOS 동작 |
| --- | --- |
| iisacc 계정 | 공통 LVRS 패널에서 이메일·비밀번호만 입력한다. 계정 SDK가 전체 프로필을 받아 Helper·기기 연결과 공유한다. iPhone은 phone, iPad는 tablet으로 보고하며 각각 2대 제한이다. |
| QR 페어링 | Devices → Pair desktop → Scan QR code에서 데스크톱 코드를 촬영한다. 같은 계정·호스트의 Files 접근 후 완료를 표시한다. 카메라 권한 거부 시 설정 열기를 제공하고 영상은 저장·전송하지 않는다. [Pairing.md](Pairing.md) 참조. |
| 원본 저장소 | 동일 App Group의 `Library/Application Support/Society`에 UUID와 8개 영역을 유지한다. |
| 앱 탐색 | 8개 영역을 모두 탐색한다. 파일·폴더를 한 번 탭하여 열며 이미지·문서는 QuickLook으로 표시한다. 미리보기를 지원하지 않는 파일에는 오류를 표시한다. |
| OS 드라이브 | 앱에 File Provider 확장을 포함한다. 파일 앱의 Society 루트는 `Society/Files/`이다. 나머지 7개 영역과 Helper 데이터는 공개하지 않는다. |
| 모델 입력 | `Import models…`와 파일 앱의 드래그를 지원한다. 두 확장자 `.safetensor`·`.safetensors`를 `Models/`로 복사한다. |
| 파일 제공자 접근 | 선택기에서 받은 원본 NSURL을 유지하고 보안 범위·파일 조정 안에서 복사한다. 취소·중복 이름·다중 입력·원자적 완료 처리는 기존 importer와 공유한다. |
| 앱 간 공유 | 같은 App Group entitlement와 `SocietyAppGroup`을 가진 iisacc 앱이 `SharedStorage::open()`으로 동일 원본 모델·생성 기록을 이용한다. 다른 소비 앱에는 File Provider를 중복 설치하지 않는다. |
| Helper 수신 | 실행 중 내장 수신기가 영속 outbox를 inbox로 옮긴다. Society가 중단되어도 다른 실행 중 앱은 outbox에 데이터를 저장할 수 있다. |

## 실행 수명과 보존

`SocietyRuntime`이 Helper·SocietyInbox·내장 수신기를 함께 관리한다. Qt iOS의 `ApplicationSuspended`(UIApplicationStateBackground)와 Hidden에서는 재시도 타이머를 멈추고, 마지막 Helper 이벤트를 기록한 뒤 수신기의 잠금과 SQLite 연결을 해제한다. 전경 복귀 때 모두 다시 연결한다. 파일 선택기나 시스템 시트가 만드는 Inactive는 중단으로 처리하지 않는다. 시작 시 공유 위치가 일시적으로 접근 불가능해도 실행 중 1초마다 재시도한다.

드라이브 연결도 중단 때 요청 세대를 만료시키고 복귀 때 상태를 다시 확인한다. 중단 전의 늦은 File Provider 콜백은 새 연결 상태를 덮어쓰지 않는다. 첫 실행에서 원본을 열지 못한 경우 전경 복귀 시 기본 App Group 컨테이너를 다시 연다.

수신 데이터는 명시적으로 ACK한 sequence까지만 소비 완료로 취급한다. ACK하지 않은 데이터는 앱 복귀·재실행 시 다시 전달될 수 있으므로 소비자는 message ID로 중복 처리를 방지해야 한다. outbox에서 inbox로의 이동은 트랜잭션이며 같은 ID의 저장 행은 중복되지 않는다. 데몬 상태 snapshot은 마지막 관측이며 살아 있는 프로세스라는 보증이 아니다.

Helper는 iOS에서 정적으로 링크한다. Society는 정적 Qt의 `QSQLiteDriverPlugin`을 명시적으로 가져오며, 정적 LVRS의 QML 등록·리소스·폰트를 전체 아카이브 링크로 유지한다. 앱 실행 파일에서 LVRS 초기화 심볼이 빠지면 기기에 설치되어도 화면을 열 수 없다. App Group의 DB·WAL·SHM과 전달 디렉터리에 `CompleteUntilFirstUserAuthentication` 보호를 적용한다. 기기 재부팅 후 첫 잠금 해제 전에는 접근 실패를 허용하고 복귀·재시도로 회복한다. 공개 Files 영역으로 DB를 옮기지 않는다.

모델 복사에는 UIKit의 유한한 background task를 사용한다. 완료·취소 시 작업을 종료하고, OS가 시간 만료를 통지하면 남은 복사를 취소한다. 완료한 모델은 유지하고 미완료 모델은 정상 취소 경로에서 임시 파일을 제거한다. 이는 대용량 복사를 무제한 백그라운드 실행하거나 바이트 단위로 재개하는 기능이 아니다. 강제 종료 때 최종 모델 이름으로 불완전 파일을 공개하지 않는다.

iOS는 임의 앱을 상주 데몬으로 계속 실행하거나 다른 앱을 강제로 깨우게 하지 않는다. 이 구현은 App Group의 영속 큐로 데이터를 보존하고 실행 기회에 수신한다. 다른 기기 간 실시간 전달이나 원격 클라우드 동기화는 별도 서버 기능이다. 불필요한 background mode를 선언하지 않는다. [Apple의 백그라운드 실행 연장](https://developer.apple.com/documentation/uikit/extending-your-app-s-background-execution-time)

## 재현 가능한 빌드

전체 Xcode와 iOS SDK가 필요하다. Qt만 설치되어 있거나 macOS Command Line Tools만 있는 환경은 iOS 빌드 환경으로 인정하지 않는다. 기존 macOS 빌드와 iOS 기기·시뮬레이터 ABI를 분리한다.

```sh
python3 -B tools/build_ios.py --platform ios-simulator --check
python3 -B tools/build_ios.py --platform ios-simulator
python3 -B tools/build_ios.py --platform ios-device --team <Apple-team-id>
```

스크립트는 Workspace의 SDK 소스에서 LVRS(static), iiAcountManager(static), iiServerHost, iiSocietyContainer, iiSocietyHelper를 각각 `build/ios-device` 또는 `build/ios-simulator`에 빌드하고, `Workspace/build/<대상>/install`에 설치한 뒤 Society와 확장을 Xcode로 빌드한다. `--qt`로 Qt 6.8.3 경로를 지정할 수 있다. 사전 점검 결과와 명령은 `Society/build/<대상>/preflight.json`에 기록한다. 개발자 경로 선택은 `DEVELOPER_DIR` 또는 기존 `xcode-select` 설정을 따른다.

현재 앱이 호출하는 계정·네트워크·저장소 SDK는 iiAcountManager, iiServerHost, iiSocietyContainer와 iiSocietyHelper이다. iOS는 이들과 LVRS를 필수로 연결한다. 나머지 SDK는 데스크톱의 기존 의존성·호환성 테스트로 유지한다. 설치 후 직접 `cmake --preset ios-device` 또는 `ios-simulator`를 사용해도 같은 설치 위치를 참조한다.

앱 ID `com.iisacc.society`와 확장 ID `com.iisacc.society.fileprovider`는 동일 개발자 팀의 `group.com.iisacc.society` 권한으로 프로비저닝해야 한다. 스크립트는 Apple 계정의 식별자나 프로필을 임의 생성하지 않는다. [App Group 설정](https://developer.apple.com/documentation/xcode/configuring-app-groups)

## 검증 범위

서명된 기기 앱의 플랫폼·arm64·App Group·기기 프로파일·File Provider 포함 여부와
LVRS QML 등록·리소스·SQLite 초기화 심볼을 설치 전에 검사한다. QR 카메라 사용 목적, AVFoundation 링크, 마이크 권한 미요청과 QR 라이선스 리소스도 확인한다.

```sh
DEVELOPER_DIR=/Applications/Xcode-beta.app/Contents/Developer \
python3 -B tests/verify_ios_bundle.py build/ios-device/bin/Debug/Society.app \
  --device <iPhone-UDID>
```

- `Society.MobileLifecycle`: 중단 후 수신기 잠금 해제, 중단 중 전송한 데이터 수신, ACK 복원, 미확인 메시지 재전달, 시작 실패 복구, Inactive 처리, 데스크톱 수신 유지.
- `Society.AppleModelSource`: 실제 Foundation 제공자와 선택기 URL 입력을 통한 원본 보존·Models 배치·접근 수명·지연 콜백 취소.
- `Society.Gui`, `Society.Drive`: 작은 창 탐색·한 번 탭하는 모바일 모드·기존 데스크톱 선택과 더블클릭·모델 드롭.
- `Society.IosDropSyntax`: 호스트의 Catalyst UIKit·QuickLook 헤더로 드롭·선택기·미리보기·background task 및 DriveController·ModelImporter의 실제 iOS 분기의 API와 타입을 검사한다. iOS 앱 링크 증거는 아니다.
- `Society.IosBuildContract`: SDK 없는 환경의 거부와 기기·시뮬레이터 빌드 경로·필수 SDK 계약.
- Container의 `ios_package_contract`, `shared_location`, `native_files`: 실제 plist·entitlement·모델 UTI와 공유 경로·Files 공개 경계·영속 ID·파일 작업.

2026-09-08 Xcode 27 beta 6(27A5252f)와 Qt 6.8.3으로 Society와 File Provider를 arm64 기기용으로 빌드·서명하고 iPhone 15 Pro Max(iOS 27)에 설치했다. 기기 앱 목록·실행 프로세스·실제 화면, 8개 영역 생성, Dreamscapes와 동일 App Group 경로 사용 및 Helper 이벤트 전달을 확인했다. 정적 LVRS 누락으로 첫 실행이 종료되는 문제와 상태 표시줄에 제목이 겹치는 문제를 수정한 빌드로 검증했다. 설치 로그·패키지 검증·화면 캡처는 `Workspace/build/ios-install/`에 있다.

iPad는 사용자 지시로 이번 설치에서 제외했다. 파일 앱 위치 활성화와 파일 작업, 모델 가져오기·미리보기, 기기 재부팅 후 복원은 별도 실기기 검증 범위로 남아 있다.


2026-09-09 계정 화면 소유권 변경: iOS 빌드도 iiAccountManager 0.2.3 Quick 모듈을 포함한다.
로그인·회원가입 화면 QML과 대화상자는 SDK가 소유하며 Society는 공용 manager와 overlay를 연결한다.
화면 전환·가입 API와 인증 요청을 제외한 검증 범위는 [Account.md](Account.md)를 따른다.
