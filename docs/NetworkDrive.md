# Society 기기 연결과 호스팅

Society의 동기화 범위는 로컬 네트워크를 포함하는 기기 간 서버 호스팅이다. [같은 계정의 자동 LAN 페어링](AutomaticPairing.md)과 [NAS·자체 호스트 웹서버 연결](SelfHosting.md)을 제공한다. 두 경로에서 iiSocietySync가 동일한 [8개 영역의 양방향 동기화](Synchronization.md)를 실행한다. 계정은 호스트 발견과 접근 격리의 식별자이며, 실제 권한은 검증된 계정 세션에서 얻는다.

Devices의 **Your Society server**에 자체 서버 주소를 입력한다. **Connect to server**를 선택하면 데스크톱은 호스트, 모바일은 클라이언트로 연결한다. 서버 설정은 로그인 세션에 바인딩된 암호화 저장소에 보존하며, 같은 세션을 복원한 앱과 데몬이 자동 재접속한다. 서버 연결은 Bonjour/NSD 탐색 및 LAN 증명 발급 없이 동작한다. 호스트가 등록한 로컬 TLS 경로가 있으면 먼저 시도하고, 접근할 수 없으면 WebSocket 중계 경로를 사용한다.

직접 QR 경로는 같은 Wi-Fi 또는 사설 LAN의 데스크톱 `Files/`에 접속한다. 이 경로만 인터넷 fallback 없이 동작하며 로그인 서버를 호출하지 않는다. 상세 계약과 제약은 [Pairing.md](Pairing.md)를 따른다.

데스크톱은 Host, iOS/Android는 Client로 고정되며 Preferences에 역할 선택이 없다. `mode`는 읽기 전용 상수이고 `hosting`은 실제 연결과 파일 핸들러 상태이다. Devices의 서버 연결은 주소만 받으며 과거 서버 설정의 `host` 값은 복원 시 제거한다. 기기 역할은 기본 호스트 선출과 구분되므로 기존 미러는 원래 기본 호스트를 계속 따른다. 컨테이너 변경·연결 해제 시 이전 전송을 정리하며 다운로드 목적지의 기존 내용을 보존한다.

iOS와 Android는 클라이언트 전용이며 자체 서버에도 클라이언트로 연결한다. Files/Sync 리스너는 만들지 않는다. 허용된 백그라운드 실행 시간이 끝나면 전송을 중지하고, 복귀 후 현재 계정 세션으로 재접속한다. 수동 QR을 촬영하는 행위의 접근 권한과 iisacc 계정 인증은 별개이다.

`startLocalHost()`와 `joinLocalHost(qr)`는 직접 LAN 연결에 `iiServerHost::LanPeer`를 사용한다. `configureServer(url)`는 검증한 서버 설정과 현재 계정 세션으로 `iiServerHost::Peer`를 시작한다. 계정이 검증된 두 경로 모두 전체 컨테이너 동기화 핸들러를 사용한다. 수동 QR만으로는 전체 컨테이너 동기화 권한을 부여하지 않는다. `startSession(PeerOptions)`는 기존 C++ 통합 API로 유지한다.

원격 Files 탐색·다운로드의 상태와 저장은 `iiSocietySync::RemoteFiles`가 담당한다. Files 공개 범위·심볼릭 링크 차단·디렉터리 교체 검사는 SDK의 `filesHandler`가 기존 SharedStorage/FileShare를 재사용한다. 목록은 256개씩 읽고 파일은 256 KiB 청크로 받는다. 크기·오프셋·버전을 대조한 뒤 QSaveFile로 완성본만 저장한다. 실패하면 기존 목적지 내용을 보존한다. Finder와 모바일 OS File Provider는 기존 경로를 유지한다.

## 빌드와 확인

`iiServerHost 0.4.1`, `iiSocietyHelper 0.7.1`, `iiSocietySync 0.2.0`을 설치한 뒤 `cmake -S . -B build`, `cmake --build build --parallel 4`로 빌드한다. 로그인 테스트를 제외할 때 `ctest --test-dir build -E '^Society.Account$' --output-on-failure`를 사용한다. `Society.Pairing`과 `Society.ClientOnlyNetwork`가 실제 사용 경로를 검사하며, `Society.NetworkDrive`는 네이티브 Peer 소비자의 플랫폼 역할 고정·파일 경계를 검사한다. 실제 모바일 카메라 검증은 빌드 성공이나 테스트용 QR 해독으로 대체하지 않는다.


## 주변 기기 선택

동일 계정 기기 목록과 직접 초대·코드 확인 방식도 지원한다. 탐색 수명, 플랫폼 API, 계정 범위 필터와 최종 Files 접근 승인 경계는 [Pairing.md](Pairing.md)의 주변 기기 탐색 계약을 따른다. 모바일은 UDP 초대만 수신하고 Files 리스너는 만들지 않는다.

## 실행과 재연결 시 진입 조건

`containerReady`는 로컬 복제본의 구조와 완료 상태이다. `hostConnectionReady`는 모바일에서
현재 계정·연결의 승인된 호스트와 동기화에 성공했고, 완성된 복제본의 컨테이너와 호스트가
그 검증에 일치함을 의미한다. 릴레이 소켓만 연결되거나 이전 실행의 캐시만 있어서는 참이 되지 않는다.
데스크톱은 로컬 드라이브 검증을 사용하므로 네트워크 연결을 진입 조건으로 요구하지 않는다.

모바일 연결 단절·동기화 오류·계정 또는 컨테이너 교체 시 검증 결과를 폐기한다.
`Main.qml`은 이 결과에 따라 LVRS 온보딩의 로그인·주변 호스트·서버 설정 인터페이스를 제공한다.
재연결 후 실제 동기화가 완료되면 기본 화면으로 돌아간다. 기존 파일과 미전송 변경은 유지한다.
상세 UI 및 파일 다이얼로그 검사는 [Onboarding.md](Onboarding.md)에 있다.
