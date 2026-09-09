# Society 로컬·원격 드라이브

Society는 `iiServerHost 0.3.0`의 `Peer`로 같은 계정의 Society 기기를 검색하고 그 기기의 `Files/`에 접근한다. 상단 **Devices**에서 iisacc 이메일·비밀번호로 바로 로그인한 뒤 기기를 선택하면 폴더를 탐색하고 파일을 로컬 위치에 저장할 수 있다. 기존 `iiAcountManager`의 앱 세션 및 PC 2대·태블릿 2대·휴대폰 2대 정책을 재사용한다. 모바일의 기기 ID·기기 유형은 해당 SDK의 명시적 deviceInfo 설정 계약을 따른다.

데스크톱은 독립 **Preferences** 창의 **Client mode / Host mode**에서 전환한다. 상단 Preferences 버튼, ⌘+, / Ctrl+, 단축키 또는 Devices의 Preferences 버튼으로 연다. 창 동작과 검증은 [Preferences.md](Preferences.md)를 따른다. 앱을 시작하면 클라이언트가 기본값이며 모드 선택은 현재 실행 동안 유지된다. 클라이언트는 다른 호스트의 파일을 탐색·다운로드한다. 호스트는 이 기능에 더해 현재 컨테이너의 `Files/`를 같은 계정에 공개한다. 호스트 모드에는 유효한 컨테이너가 필요하며, 모드 선택만으로 로그인이나 연결이 실행되지는 않는다.

연결 중 모드를 바꾸면 기존 로컬 리스너·파일 제공 핸들러·연결을 종료하고 같은 인증 세션으로 다시 연결한다. 진행 중인 다운로드는 취소하며 기존 목적지 파일은 보존한다. 클라이언트로 전환한 기기는 호스트 목록에서 사라지고 이미 열었던 로컬/원격 경로로도 파일을 제공하지 않는다. 다시 호스트를 선택하면 원래 연결·TLS 설정으로 공개를 재개한다. 클라이언트 모드에서 로컬 컨테이너를 변경해도 네트워크 연결은 유지되며, 로컬 컨테이너가 없거나 유효하지 않아도 다른 기기에 접근할 수 있다.

**iOS와 Android는 항상 클라이언트이다.** 모드 선택 UI를 표시하지 않으며, `NetworkDriveController`도 `HostMode` 설정을 무시한다. C++ `startSession`에 `hostFiles=true`나 로컬 호스팅 설정을 전달하더라도 컨테이너를 공개하거나 리스너를 열지 않는다. 이 제한은 재연결·컨테이너 변경·백그라운드 복귀에도 적용된다. 모바일의 로컬 컨테이너 사용 및 다운로드 저장은 그대로 가능하며, 가까운 호스트에 대한 로컬 네트워크 접속도 지원한다.

각 기기는 동일한 신뢰 중계 주소를 `SOCIETY_RELAY_URL` 또는 Devices 입력란에 설정한다. 공용 주소를 임의로 기본값에 넣지 않았다. 운영 가능한 중계·HTTPS 인증서와 iisacc 계정 서비스가 필요하다. 로그인 쿠키는 메모리의 Qt cookie jar와 Peer에만 두며 JSON 설정이나 로그에 기록하지 않는다. 중계 주소를 바꾸면 이전 연결을 먼저 해제한다.

로컬 호스팅에는 기기 TLS 인증서와 개인 키를 `SOCIETY_HOST_CERTIFICATE`, `SOCIETY_HOST_KEY`로 설정한다. 중계가 인증된 계정의 인증서 fingerprint를 전달하므로 기기 인증서는 자체 서명해도 된다. 중계 인증서는 정상적인 CA 신뢰가 필요하다. 호스트 모드를 선택한 데스크톱은 기기 인증서가 없으면 원격 호스트로 동작한다. 클라이언트 모드에서는 호스트 인증서·키 설정을 읽지 않으며, 자신의 기기 인증서 없이도 다른 호스트에 로컬로 접근한다.

```sh
SOCIETY_RELAY_URL='wss://your-relay.example/server-host' \
SOCIETY_HOST_CERTIFICATE='/secure/device-cert.pem' \
SOCIETY_HOST_KEY='/secure/device-key.pem' \
build/bin/Society.app/Contents/MacOS/Society
```

`NetworkDriveController.startSession(PeerOptions)`는 이미 인증한 다른 네이티브 로그인 흐름을 연결하는 C++ 진입점이다. 데스크톱의 네이티브 호스트도 먼저 `setMode(NetworkDriveController::HostMode)`를 호출해야 한다. 공개 여부와 메타데이터는 컨트롤러가 현재 모드와 컨테이너로 결정하며 호출자의 값으로 우회할 수 없다. `mode`는 선택한 모드, `hostModeAvailable`은 플랫폼의 호스트 허용 여부, `hosting`은 연결된 파일 호스트의 실제 상태이다. 계정 JSON을 표시하는 것만으로 접속 권한을 얻지 못하며, 중계는 전달받은 세션을 다시 확인한다. 호스트 ID는 앱 간 공통 기기 ID를 사용한다.

로컬 주소 연결을 먼저 시도하고 실패 시 원격 중계로 전환한다. 선택한 경로는 화면에 Local network / Remote로 표시한다. 기기 검색·인증은 로컬 전송 중에도 중계에 연결되어 있어야 한다. 앱이 종료되거나 기기가 잠들면 호스트에 접근할 수 없다. 모바일은 suspended/hidden 시 연결을 해제하고 active 시 재연결한다. 기존 Helper 수신 데몬과 OS File Provider를 원격 마운트 구현으로 바꾸지 않는다.

공개 범위는 Finder/iOS Files 드라이브와 동일한 `Files/`이다. `Models`, `Generation History` 등 내부 섹션과 컨테이너 manifest를 원격 루트에 노출하지 않는다. `SharedStorage`로 선택한 원본 컨테이너 UUID를 고정하고 각 요청에서 검사한다. FileShare가 Files 디렉터리 inode와 경로를 추가 검사하여 교체·링크를 차단한다.

목록은 256개씩 읽는다. 파일은 256 KiB씩 받으며 크기·오프셋·버전을 검사한 후 `QSaveFile`로 완성본만 저장한다. 전송 중 원본이 바뀌거나 연결이 끊기면 기존 목적지 파일을 보존하고 임시 파일을 취소한다. SDK는 새 파일 생성 API도 제공하지만 현재 Society 화면은 탐색과 다운로드를 제공한다.

QR로 호스트를 선택하는 절차는 [Pairing.md](Pairing.md)를 따른다. 데스크톱의 Pair iPhone은 호스트 모드로 전환하고 코드를 표시한다. iPhone은 로그인 후 카메라로 스캔하며, Files 목록 응답을 확인한 뒤 연결 완료와 파일 열기를 제공한다. 이 과정은 기존 계정 격리·전송 경로·Files 공개 범위를 재사용한다.

## 빌드와 검증

먼저 iiServerHost와 iiSocietyContainer 0.9.1 이상을 각 SDK의 `build/install`에 설치하고 다음과 같이 구성한다.

```sh
cmake -S . -B build \
  -DiiServerHost_DIR=/Volumes/Storage/Workspace/SDK/iiServerHost/build/install/lib/cmake/iiServerHost \
  -DiiSocietyContainer_DIR=/Volumes/Storage/Workspace/SDK/iiSocietyContainer/build/install/lib/cmake/iiSocietyContainer
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
cmake --build build --target Society_qmllint
```

`Society.NetworkDrive`는 별도 임시 컨테이너와 인증 fixture를 사용해 로컬/원격 모두에서 실제 700,000바이트 파일 다운로드, 개인 영역 차단, Files 디렉터리 교체, 연결 해제를 검증한다. 데스크톱 모드 반복 전환 시 공개·비공개, 리스너 해제, 기존 경로 차단, 컨테이너 없는 클라이언트 동작도 검증한다. `Society.ClientOnlyNetwork`는 모바일과 같은 C++ 제한 분기를 `SOCIETY_CLIENT_ONLY=1`로 컴파일하여 강제 호스팅 요청 차단, 로컬/원격 다운로드와 중단·복귀 후 클라이언트 유지 여부를 호스트 환경에서 검증한다. 실제 iOS/Android 타깃은 이 정의와 함께 Qt 플랫폼 매크로로도 호스팅을 차단한다. SDK는 TLS, 동일 계정 격리, 로컬 실패 시 중계 전환, 만료, 쿠키 분리와 파일 경계를 검증한다. 이 테스트는 운영 계정 로그인·메일·공용 중계 배포·다른 물리 디바이스에서의 실행 검증을 대신하지 않는다.

iOS/Android 빌드 계획에 iiAcountManager와 iiServerHost를 포함했다. iOS iiServerHost는 정적 라이브러리로 구성하며, 앱 plist에는 로컬 네트워크 사용 목적을 넣는다. 현재 플랫폼 검증 기록은 실제로 수행한 결과만 별도로 보고한다. SDK 기본 FileShare의 네이티브 파일 제공은 Unix에 한정하며 Windows 파일 제공은 아직 지원하지 않는다.

전송 중 원본이 변경되는 사례도 테스트한다. 두 번째 청크 직전 호스트 파일을 바꾸면 `file_changed`로 종료하고, 다운로드 목적지의 기존 내용이 그대로 남는지 확인한다. `Society.Drive`는 390×844, 844×390, 360×320, 1120×720에서 Devices 패널과 Preferences 진입 버튼의 창 경계를 확인한다. 별도 Preferences 창에서 실제 모드 선택과 기기 공개 여부를 검사한다. `SOCIETY_NETWORK_SCREENSHOT_PATH`를 지정해 Devices의 네이티브 Qt 실행 화면을 기록할 수 있다.

모드 전환 도중 다운로드의 두 번째 청크가 도착하는 경우에도 완료 신호가 발생하지 않고 기존 목적지가 보존되는지 확인한다. 클라이언트 전용 Devices 패널에는 Preferences 진입 버튼이 표시되지 않으며, Preferences의 열기 함수와 호스트 선택 동작을 직접 호출해도 클라이언트로 유지되는지 검증한다.
