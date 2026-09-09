# 로컬 네트워크 기기 탐색·페어링

## 같은 계정의 주변 기기 탐색

로그인된 계정의 세션이 유효하면 앱이 `_society-pair._udp` 서비스를 등록하고 탐색을 계속한다. 데스크탑은 창·페어링 패널이 닫혀 있어도 앱이 실행 중이면 탐색한다. iPhone/iPad/Android는 Society가 전경에 있을 때 자신을 알리고 요청을 받으며 백그라운드에서는 중지한다. 전경 복귀·자동 로그인 완료 시 다시 시작한다. 로그아웃·계정 변경·세션 만료 시 광고, 기기 목록과 대기 중 요청을 지운다. 탐색만으로 Files 호스트를 시작하지 않는다.

데스크탑에서 **Devices → Pair a device → Nearby devices**를 열면 이름·기기 유형·로컬 주소와 연결 완료 상태를 표시한다. 이미 연결된 기기의 Pair 버튼은 Connected로 바뀌며 중복 요청을 막는다. 같은 계정으로 로그인한 상대의 Society를 열어 둔 상태에서 **Pair**를 누른다. 상대 앱의 **Accept pairing request**를 누르면 양쪽에 4-4-4 형식의 확인 코드가 표시된다. 이를 대조하여 데스크탑에서 **Codes match — allow connection**을 누른 후에만 Files 접근과 페어링을 완료한다. 다른 데스크탑도 대상이 될 수 있으며 수신 측은 Files 클라이언트가 된다. 모바일은 Files를 호스팅하지 않지만 제한된 UDP 연결 요청 수신 소켓은 연다.

기기 목록은 OS의 Bonjour/NSD 발견·이탈 통지와 6초 주기의 주소 재확인을 이용한다. Apple의 주소 확인은 DNSServiceGetAddrInfo로 발견한 네트워크 인터페이스의 IPv4를 직접 조회한다. 최신 상태를 25초 동안 받지 못한 항목은 제거한다. 인터페이스 변경은 다음 확인에 반영하며 탐색 오류는 5초 뒤 재시도한다. 초대는 60초 수명이며 2초 간격으로 제한된 크기의 UDP 유니캐스트만 재전송한다. 수락·거절·취소·완료 시 중단한다. 계정 ID·서비스 origin의 SHA-256 범위 태그, 기기 ID·이름·유형·실행 nonce만 탐색 레코드에 넣으며 이메일·비밀번호·로그인 쿠키·전체 계정 모델은 전송하지 않는다.

계정 범위 태그는 일반 Society 앱 사이에서 다른 계정의 후보를 제외하는 필터이며 원격 기기의 서버 인증을 증명하는 서명이 아니다. DNS-SD 메타데이터나 초대 수신만으로 Files 권한을 주지 않는다. 최종 권한은 양쪽 TLS 연결에 바인딩된 코드 비교와 사용자의 데스크탑 확인으로 부여한다. 로컬 탐색·초대에는 외부 서버 조회, 로그인 정보 재전송, 클라우드 relay가 없다. 계정 로그인·세션 갱신 자체는 기존 iiAccountManager가 담당한다.

`NearbyDevices`는 계정 범위·목록·초대·수명을, `DiscoveryService`의 플랫폼 구현은 발견·재확인·이탈 통지를 담당한다. `NetworkDriveController`가 앱 계정 수명과 연결하고 `DevicePairing`과 LVRS 기반 `PairingPanel.qml`이 선택·수신·코드 확인 UI를 소유한다. `iiServerHost::LanPeer::createDeviceOffer()`와 `confirmDevice()`는 최종 확인 전 파일 접근을 차단한다.

외부 의존성은 OS의 DNS-SD API를 재사용한다. Apple은 시스템 Bonjour의 dns_sd C API, Android는 API 28 이상에서 사용 가능한 NsdManager를 사용한다. 별도 discovery 라이브러리·암호 구현·서버는 추가하지 않는다. Apple Info.plist에 서비스 유형과 로컬 네트워크 목적을 선언하며 앱 자체는 멀티캐스트 패킷을 만들지 않는다. Windows 빌드에는 Bonjour SDK/런타임, Linux에는 dns_sd 호환 Avahi 개발 패키지가 필요하다. API·유지보수는 각 OS 배포에 따른다. [Apple 로컬 네트워크 문서](https://developer.apple.com/documentation/technotes/tn3179-understanding-local-network-privacy), [Android NSD 문서](https://developer.android.com/develop/connectivity/wifi/use-nsd)를 따른다.

`Society.Discovery`는 비인증 합성 식별자로 계정 범위 필터, 후보 갱신·이탈, 주소·레코드 제한, UDP 초대 수락·거절·취소·만료와 TLS 최종 확인 전 Files 접근 금지를 검사한다. `Society.Pairing`은 UI 조정 객체를 통한 기기 선택→수신→코드 확인→Files 연결을 검사한다. `SOCIETY_VERIFY_NATIVE_DISCOVERY=1`로 실행하는 별도 Bonjour 검사는 합성 식별자의 OS 서비스 등록·실제 LAN 발견·이탈만 검사한다. 계정 로그인/자동 로그인 요청은 실행하지 않는다. `SOCIETY_DISCOVERY_TRACE=1`은 DNS-SD 단계·결과 코드·레코드 크기만 기록하며 계정 태그·기기 이름·주소·초대 내용은 로그에 넣지 않는다.

## 기존 QR 방식

2026-09-09 요구사항에 따라 Society 앱의 페어링을 외부 중계에서 직접 LAN 연결로 전환했다. 데스크탑이 QR을 표시하고 모바일이 카메라로 읽는다. 중계 주소 입력, 클라우드 기기 검색, 원격 전송 fallback은 이 경로에 없다.

## 사용 절차

1. 데스크탑과 iPhone/iPad/Android를 같은 Wi-Fi 또는 연결된 사설 LAN에 둔다.
2. 데스크탑에서 Society 컨테이너를 열고 **Devices → Pair mobile device**를 누른다. 호스트 모드로 전환하면서 QR을 즉시 만든다.
3. 모바일에서 **Devices → Pair desktop → Scan QR code**를 누르고 데스크탑 QR을 비춘다. 최초 카메라·로컬 네트워크 권한을 허용한다.
4. 모바일이 QR의 주소로 직접 연결하고 인증서를 확인한다. 데스크탑의 Files 목록을 읽고 확인 응답까지 끝나면 양쪽에 완료를 표시한다.
5. 모바일의 **Open host Files**로 목록을 탐색하거나 내려받는다. 데스크탑의 **Done**은 QR 창만 닫고 연결은 유지한다.

LAN 페어링은 iisacc 로그인과 독립적이다. 비밀번호, 로그인 쿠키, 계정 데이터와 세션을 다른 기기로 전달하지 않는다. QR을 촬영한 기기에 사용자가 접근을 승인하는 방식이므로 같은 계정 검사도 이 경로의 권한 근거가 아니다. 기존 iiAccountManager 로그인·회원가입 및 PC 2대/태블릿 2대/휴대폰 2대 로그인 정책은 계정 기능에 그대로 남는다. 로그인 테스트는 실행하지 않는다.

QR 수명은 60초이고 한 번만 소비한다. 재발급·창 닫기·취소·호스트 종료로 미완료 요청을 무효화한다. Files 확인 단계는 최대 15초이다. 다른 기기가 코드를 재사용하거나 인증서가 다르면 완료하지 않는다. 이미 완료된 연결은 QR 만료나 Done으로 끊지 않는다. **Disconnect**, 데스크탑 Client mode 전환, 컨테이너 변경, 앱 종료 또는 모바일 백그라운드 전환은 연결을 닫는다. 연결이 끊기거나 앱이 재시작되면 새 QR로 다시 페어링한다. 장기 접근 토큰이나 QR은 디스크에 저장하지 않는다.

## 코드 책임

| 구성 | 역할 |
| --- | --- |
| `NetworkDevices.qml` | 데스크탑 QR 표시 / 모바일 스캔 진입과 로컬 Files 탐색 |
| `PairingPanel.qml` | 플랫폼 역할에 따른 QR·스캔 버튼·진행·만료·완료 표시 |
| `DevicePairing` | UI 상태와 `NetworkDriveController.localPeer()` 연결 |
| `NetworkDriveController` | 호스트 모드·컨테이너 수명·Files 공개·다운로드 관리 |
| `iiServerHost::LanPeer` | 직접 TLS 리스너/클라이언트, 일회용 키, Files 확인, 연결 수명, 요청·응답 |
| `iiServerHost::LanLink` | 버전 2 QR의 사설 IPv4 주소·포트·인증서 지문·코드·만료 정보 검증 |
| `PairingQr` | 고정된 Nayuki C++ 라이브러리로 QR 행렬 생성·정수 배율 렌더링 |
| `IosQrScanner.mm` | AVFoundation QR 캡처, 권한·중단·취소 |
| `AndroidQrScanner.cpp` / `SocietyActivity.java` | JNI 콜백과 앱 내부 QR 카메라, 권한·중단·취소 |

주소는 활성 Wi-Fi/Ethernet을 우선하여 최대 8개 사설 IPv4 주소를 포함한다. 모바일은 이 주소만 순서대로 시도한다. DNS 이름, 공인 인터넷 주소와 링크 로컬 metadata 주소를 QR 연결 대상으로 허용하지 않는다. 자동 감지에는 loopback을 넣지 않으며 SDK 테스트는 명시한 loopback TLS 주소를 사용할 수 있다. IPv6 전용 LAN은 현재 지원하지 않는다. 공유기의 client isolation, VPN 분리, 방화벽 또는 로컬 네트워크 권한 거부로 접속이 막힐 수 있다.

데스크탑은 실행 중 RSA-2048 개인 키와 하루 수명의 인증서를 메모리에서 만들고 QR에는 SHA-256 지문만 넣는다. 모바일은 TLS 핸드셰이크와 연결 직후 지문을 모두 대조한 뒤 일회용 코드만 전송한다. 인증서 오류를 무조건 무시하지 않는다. 기존 FileShare의 경로·inode·symlink·원본 컨테이너 UUID 검사를 유지하며 공개 루트는 `Files/`이다. Models 등 나머지 영역을 네트워크 루트에 포함하지 않는다.

## 의존성과 검증

Qt 6.8.3 Core/Network/WebSockets를 재사용한다. 데스크탑 인증서 생성은 OpenSSL 3 Crypto를 사용한다. SDK의 `ThirdParty/OpenSSL-LICENSE.txt`는 Apache 2.0 고지이며 클라이언트 전용 iOS/Android에는 인증서 생성 의존성을 추가하지 않는다. Qt Secure Transport 호스팅에서 검증한 RSA 형식을 사용한다. [OpenSSL 키 생성](https://docs.openssl.org/3.5/man3/EVP_PKEY_keygen/), [인증서 서명](https://docs.openssl.org/3.5/man3/X509_sign/), [Qt WebSocket TLS](https://doc.qt.io/qt-6.8/qwebsocket.html)를 따른다.

Android 카메라는 [ZXing Android Embedded 4.3.0](https://github.com/journeyapps/zxing-android-embedded)의 공개 안정 버전을 고정한다. Apache 2.0이며 카메라·프레임 수명 관리는 해당 라이브러리에 맡긴다. AndroidX 및 ZXing decoder 전이 의존성이 있고 서버나 별도 설치 앱은 필요하지 않다. 배포 간격이 긴 라이브러리이므로 target SDK를 변경할 때 실제 카메라 회귀 검증이 필요하다. QR만 해독하며 이미지 저장·업로드·마이크 접근·소리를 사용하지 않는다.

`iiServerHost.lan`과 설치 소비자는 실제 TLS 연결·파일 바이트·재사용·취소·만료·인증서 불일치를 검사한다. `Society.Pairing`은 로그인/중계가 없는 기본 상태에서 QR 생성·해독·페어링·파일 다운로드·모드 전환·Files 실패와 여러 창 크기를 검사한다. `Society.ClientOnlyNetwork`는 로그인 없이 모바일 스캔 버튼을 제공하고 호스팅을 차단하는지 검사한다. 실제 카메라로 모니터를 촬영하는 검증과 자동 해독·빌드 검증은 구분해서 보고한다.

## 2026-09-09 검증 기록

- macOS 실제 Society 앱에서 로그인하지 않은 상태로 **Devices → Pair mobile device**를 눌러 QR과 60초 카운트다운이 표시되는 것을 확인했다.
- Society의 로그인 검사를 제외한 CTest 13/13과 QML 정적 검사가 통과했다. 로컬 페어링/클라이언트 전용/기존 네이티브 파일 경계 검사를 포함한다.
- 1120×720, 390×844, 640×360의 네이티브 QR 화면 검사가 통과했다. CoreImage와 Android에 실제 포함된 ZXing decoder가 Qt 렌더링 QR을 해독했다.
- iOS Release 서명 및 File Provider/카메라 목적/리소스 검사를 통과했고 연결된 iPhone 15 Pro Max에 새 앱을 설치했다.
- Android arm64 Release APK와 Gradle lint가 통과했다. APK의 SocietyActivity 진입점, CAMERA 권한 및 마이크 권한 부재를 확인했다.

로그인 테스트와 실제 휴대폰 카메라로 모니터를 촬영하는 동작은 수행하지 않았다. 위 자동 QR 해독과 파일 전송 검사로 실물 카메라 검증을 대체하지 않는다. 세부 로그·스크린샷은 `build/local-pairing-*.log`, `build/local-pairing-desktop.png`, iPhone 설치 기록은 `build/local-pairing-ios-install.json`에 있다.
