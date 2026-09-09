# Society 로컬 네트워크 드라이브

Society는 같은 Wi-Fi 또는 사설 LAN의 데스크탑 `Files/`에 직접 접속한다. 데스크탑의 **Devices → Pair mobile device**에서 QR을 표시하고 iPhone/iPad/Android Society의 **Pair desktop → Scan QR code**로 읽는다. 외부 중계 주소 입력과 인터넷 fallback이 없으며 로컬 연결에 로그인 서버를 호출하지 않는다. 상세 계약과 제약은 [Pairing.md](Pairing.md)를 따른다.

앱 시작 시 데스크탑은 Client mode이다. QR 발급은 현재 컨테이너를 제공하는 Host mode로 전환한다. 별도 **Preferences** 창에서도 모드를 바꿀 수 있다. `hosting`은 실제 파일 리스너 상태이고 단순 모드 선택만으로 Files를 공개하지 않는다. Client mode 전환·컨테이너 변경·연결 해제 시 이전 리스너와 연결을 닫는다. 진행 중인 다운로드는 취소하며 기존 목적지 파일은 보존한다.

iOS와 Android는 클라이언트 전용이다. C++에서 HostMode나 startLocalHost를 호출하더라도 로컬 Files 리스너를 만들지 않는다. 모바일이 백그라운드로 내려가면 연결을 닫는다. 다시 접속할 때 데스크탑에서 새 QR을 만들고 스캔한다. 같은 LAN에서 QR을 촬영하는 행위가 접근 승인이고 iisacc 계정 인증은 별개 기능이다.

`NetworkDriveController.startLocalHost()`는 원본 컨테이너 UUID를 고정하고 `iiServerHost::LanPeer`에 Files 핸들러를 제공한다. `joinLocalHost(qr)`는 계정 쿠키 없이 QR의 지문과 일회용 키로 직접 접속한다. 목록 응답과 완료 교환이 성공한 뒤에만 모바일에서 Files를 탐색한다. `NetworkDriveController.startSession(PeerOptions)`와 `iiServerHost::Peer`의 기존 C++ 통합 API는 호환성을 위해 남지만 Society의 Devices/QR 화면은 호출하지 않는다.

Files 공개 범위·심볼릭 링크 차단·디렉터리 교체 검사는 기존 SharedStorage/FileShare를 재사용한다. 목록은 256개씩 읽고 파일은 256 KiB 청크로 받는다. 크기·오프셋·버전을 대조한 뒤 QSaveFile로 완성본만 저장한다. 실패하면 기존 목적지 내용을 보존한다. Finder와 모바일 OS File Provider는 기존 경로를 유지한다.

## 빌드와 확인

`iiServerHost 0.4.0`을 설치한 뒤 `cmake -S . -B build`, `cmake --build build --parallel 4`로 빌드한다. 로그인 테스트를 제외할 때 `ctest --test-dir build -E '^Society.Account$' --output-on-failure`를 사용한다. `Society.Pairing`과 `Society.ClientOnlyNetwork`가 실제 사용 경로를 검사하며, `Society.NetworkDrive`는 기존 네이티브 Peer 소비자의 모드 전환·파일 경계를 검사한다. 실제 모바일 카메라 검증은 빌드 성공이나 테스트용 QR 해독으로 대체하지 않는다.


## 주변 기기 선택

동일 계정 기기 목록과 직접 초대·코드 확인 방식도 지원한다. 탐색 수명, 플랫폼 API, 계정 범위 필터와 최종 Files 접근 승인 경계는 [Pairing.md](Pairing.md)의 주변 기기 탐색 계약을 따른다. 모바일은 UDP 초대만 수신하고 Files 리스너는 만들지 않는다.
