# 같은 iisacc 계정의 자동 LAN 페어링

Society를 같은 계정으로 로그인하고 같은 사설 IPv4 LAN에서 실행하면 검증된 기기를 자동 대기열에 넣고 순서대로 연결한다. 패널 열기, 초대 수락, 수동 코드 비교가 필요하지 않다. 모바일은 전경과 OS가 허용한 백그라운드 실행 시간에 동작하며 Files 호스트가 되지 않는다.

## 연결 정책

Society 컨테이너가 열린 데스크톱 가운데 기기 ID가 사전순으로 가장 앞선 한 대를 호스트로 정한다. 다른 데스크톱, 휴대폰, 태블릿은 그 호스트에 연결한다. 호스트는 한 번에 한 기기를 처리하고, 연결된 ID와 중복 발견은 다시 등록하지 않는다. 대기열은 메모리에만 존재하고 재실행 시 현재 기기로 다시 구성한다.

각 시도는 최대 20초이다. 실패하면 0.25·0.5·1·2·4초 간격으로 LAN에서 재시도하면서 다른 후보를 진행한다. 이탈한 기기는 대기열에서 지우고 재발견 시 다시 시도한다. 계정 변경·로그아웃·로컬 권한 만료·모바일 백그라운드 실행 시간 만료는 자동 연결과 미완료 작업을 정리한다. 전경 복귀는 유효한 내부 상태를 사용하며 서버 요청을 매번 만들지 않는다.

**Disconnect**, 명시적인 모드 변경, 수동 QR/초대 시작은 자동 작업을 일시 중지한다. **Resume automatic pairing**으로 재개한다. 패널 닫기는 자동 작업에 영향을 주지 않는다. 상태는 **Queued**, **Connecting…**, **Retrying…**, **Connected**로 표시한다.

연결 형태는 한 호스트와 여러 클라이언트이다. 계정 증명을 검증하고 연결되면 iiSocietySync가 각 기기의 8개 컨테이너 영역을 양방향으로 동기화한다. 모바일도 업로드·다운로드에 참여하되 클라이언트 전용이다. Files 공개 루트는 계속 `Files/`이다. 복제와 같은 기기 내 Helper 협업의 경계는 [Synchronization.md](Synchronization.md)를 따른다.

## 인증

공개 계정 ID 해시만으로 접근을 허용하지 않는다. `AccountController`는 기존 HTTPS 쿠키 저장소를 사용하여 `/Account/Session/App`에 `intent: pairing`을 보낸다. Rails의 `app_pairing`은 활성 세션, 원래 등록된 기기 ID·유형과 `com.iisacc.society` 앱 ID를 검사하고 계정 subject와 서비스 origin에 묶인 LAN 전용 키를 발급한다. 이 키로 iisacc에 로그인할 수는 없다.

현재 앱은 기기 요청에 `pairingVersion: 2`를 포함한다. 서버는 현재 UTC 날짜부터 최대 7일의 계정·origin 전용 일별 seed를 준비하고, 앱의 `SocietyPairingCredentials`가 해당 날짜 seed에서 5분 구간별 HMAC-SHA-256 키를 직접 계산한다. 기간이 바뀌거나 기기가 늘어날 때 서버에 질의하지 않는다. 새 권한은 여섯 번째 UTC 자정부터 준비하며 기존 권한의 만료까지 계속 LAN을 사용할 수 있다. 발급 세션 만료가 더 빠르면 그 시각까지만 유효하다.

일별 seed는 `HMAC-SHA256(serverSecret, "society-auto-pair-day-v2\n" + scope + "\n" + UTC day)`이고 구간 키는 `HMAC-SHA256(seed, "society-auto-pair-key-v2\n" + scope + "\n" + five-minute epoch)`이다. 서버 비밀과 로그인 토큰을 앱의 LAN 프로토콜에 넣지 않는다. 공개 subject·이메일·기기 ID만으로 키를 만들지 않는다. 구형 앱에는 같은 계산으로 만든 현재·다음 구간 키를 반환하므로 기존 wire HMAC과 호환된다. 배포 이전 키를 캐시한 앱은 기존 키 갱신까지 최대 10분의 전환 시간이 생길 수 있다.

seed·연결 기기 이력·자동 연결 설정과 계정 스냅샷은 모바일을 포함한 [그룹 컨테이너](GroupState.md)에 암호화한다. QML 속성·평문 설정·로그·Bonjour TXT·UDP에는 키를 넣지 않는다. 서버에도 새 키 저장소를 추가하지 않는다.

기존 앱의 짧은 권한이 아직 유효하더라도 업데이트 후에는 로컬 권한으로 한 번 즉시 전환한다. 구형 서버가 짧은 권한만 반환하면 해당 권한의 실제 만료까지 기다리므로 전환 요청이 반복되지 않는다.

발견 레코드의 ID·이름·역할·실행 nonce와 자동 초대·수락·거절·취소·연결 확인 메시지를 HMAC으로 검증한다. 요청 ID, 양쪽 nonce와 대상 ID를 검사한다. 클라이언트는 인증된 초대의 인증서 지문으로 TLS를 연결하고 해당 연결의 확인 코드를 서명하여 반환한다. 호스트는 자기 연결의 코드와 일치할 때만 `LanPeer::confirmDevice()`를 호출한다. 그 전에는 Files 목록·바이트를 제공하지 않는다. 다른 계정 키, 변조 레코드, 다른 대상, 만료 요청과 이전 실행 nonce는 자동 연결을 허용하지 않는다.

최초 로그인과 seed 준비에는 서버가 필요하다. 계정 데이터 검증·인증 여부·계정 바인딩과 세션 수명은 iiAccountManager가 전담한다. Society는 SDK의 `authenticated`, `matchesSessionBinding()`, `acceptsSessionCredential()` 결과를 사용한다. 계정 스냅샷은 SDK가 같은 보안 그룹 저장소의 독립 레코드로 저장한다. 페어링 seed가 없거나 만료되어도 유효한 계정 캐시는 HTTP 없이 복원된다.

로그인/SDK 서버 확인 완료, 페어링 캐시 복원으로 확인된 누락, 실제 권한 만료에만 seed가 필요하면 요청한다. SDK가 보고한 실제 변경과 명시적 새로고침은 실패한 준비를 다시 허용한다. `refreshAt`만 지났거나 앱이 전경으로 돌아왔다는 이유로 갱신하지 않는다. LAN의 5초 발견 갱신·HMAC 회전·페어링 큐·Sync는 계정 HTTP를 생성하지 않는다.

하루/45분 계정 확인, 6일 seed 선갱신, 15분/1시간/6시간 실패 재시도를 제거했다. 실패한 seed 요청은 그룹 상태의 `requestBlocked`에 저장해 다음 실행에도 새 트리거를 기다린다. 유효한 권한은 일시적 네트워크 실패로 지우지 않고 만료 후에는 자동 연결을 중단한다. 서버의 인증 오류는 SDK에 전달하여 철회 판단과 세션 종료를 일원화한다. 서버에 접속하지 않는 동안 원격 철회를 즉시 알 수 없으며 LAN 권한은 발급된 seed와 SDK 세션의 유효기간까지만 사용한다.

## 구현·의존성·검증

`AutomaticPairing`은 대기열·호스트 선택·재시도를, `NetworkDriveController`는 계정·컨테이너·앱 수명과 인증된 전송의 연결을, `iiSocietySync`는 다른 기기의 Society와 Files 전송·컨테이너 복제를, `NearbyDevices`는 발견·서명·초대·연결 확인을 담당한다. `DevicePairing`은 수동 UI를 담당한다.

기존 Qt 6.8 [QMessageAuthenticationCode](https://doc.qt.io/qt-6.8/qmessageauthenticationcode.html), Ruby [OpenSSL::HMAC](https://ruby-doc.org/3.2.4/exts/openssl/OpenSSL/HMAC.html), OS Bonjour/Android NSD 및 iiServerHost 0.4.1 TLS를 재사용한다. 새 암호 라이브러리·유료 서비스·자체 암호 알고리즘은 추가하지 않으며 기존 유지보수·라이선스 범위를 따른다.

`Society.Discovery`는 로컬 구간·날짜 변경, 서버의 호환 키와 로컬 seed의 상호 인증, 서로 다른 키, 무서명·변조 레코드와 만료를 검사한다. `Society.Pairing`은 실제 로컬 TLS 다중 연결, 목록·다운로드 바이트, 중복 발견, 패널 종료, 연결 해제·재개, 호스트 선택과 재시도를 검사한다. `Society.Account`는 캐시 복원 후 HTTP 0회, 서버 장애 중 로컬 인증, 반복 발견 시 추가 호출 없음, 구형 권한의 즉시 전환·구형 서버 응답의 중복 억제와 실패 후 새 트리거 대기, 서버 철회 후 캐시 제거를 검사한다. 식별자와 키는 합성 값이며 물리 기기 검증은 별도로 기록한다. 이번 실행·배포 결과는 `build/local-autopairing/REPORT.md`에 기록한다.

`Society.Account::accountVerifiedPairingAutomaticallySynchronizesContainers`는 합성 계정으로 두 Society가 자동 페어링된 뒤 실제 TLS에서 Models 내려받기와 Files 올리기를 완료하는지 검사한다. 서버 요청은 두 기기의 로그인·seed 준비 총 4회이며 동기화 때문에 증가하지 않는다. 로그인 그룹 상태·Helper 큐는 복제 대상이 아니다.

기존 primary host의 로컬 소유 기록과 서명된 발견 필드가 새 데스크톱 ID 정렬보다 우선한다. 클라이언트는 저장된 primary를 따르고 미러를 자동 호스트로 승격하지 않는다. 페어링 후에는 호스트 UUID의 드라이브로 초기 미러링하며, 그 이전의 독립 클라이언트 파일을 자동 업로드하지 않는다.
## 복수 LAN 주소

같은 기기가 여러 인터페이스에서 발견되면 초대 패킷의 발신 주소·포트를 그 기기의 모든 최근 발견 endpoint와 대조한다. 목록 표시용으로 선택한 한 주소와 다르다는 이유로 정상 초대를 버리지 않는다. 발견되지 않은 발신 주소, 계정 증명, 인스턴스 nonce, 초대 수명과 TLS 지문 검사는 유지한다. `Society.Discovery`의 복수 endpoint 검사는 기존 임의 선택 경로에서 실패하고, 수정 경로에서 통과한다. 선택적 `SOCIETY_DISCOVERY_TRACE=1`은 패킷 내용·토큰 대신 송수신 및 거부 단계만 기록한다.

Apple DNS-SD는 8초의 조회 창에서 여러 DNS 응답의 주소를 모두 수집하고, Linux는 조회된 모든 주소, Android 14 이상은 `getHostAddresses()`의 모든 IPv4 주소를 전달한다. Android 이전 버전은 OS가 제공하는 단일 주소 API를 유지한다. 동일 서비스의 여러 주소를 별도로 보관하며 서비스 이탈은 모든 주소를 정리한다. 프로세스 nonce 또는 포트 변경 시 이전 주소 기록도 제거한다. 진단 모드의 경로 불일치 로그는 사설 IP·포트만 기록한다.

## 자동 연결의 기본 화면과 실행 수명

Devices의 기본 동작은 같은 계정 로그인 후 자동 동기화이며, Sync now는 자동 연결을 재개하고 현재 변경을 즉시 확인한다. 수동 연결은 Connect manually 보조 동작으로 제공한다. 현재와 같은 모드의 재설정은 자동 연결을 멈추지 않는다. 발견 변경·TLS 단계 변경은 이벤트 루프의 다음 차례에 연결 큐를 실행하며, 기존 500ms 타이머는 이벤트 누락과 재시도를 보완한다. 인증·nonce·TLS 검사는 그대로 유지한다.

데스크톱 GUI와 SocietyDaemon은 하나의 기기 실행 소유권을 공유한다. 창 종료 후 데몬이 계정 캐시와 선택된 컨테이너로 자동 재연결하며, 실행 소유권은 250ms마다 확인한다. 모바일의 UIKit background task와 Android dataSync foreground service는 현재 회차가 끝나거나 허용 시간이 끝날 때 반환한다. 자세한 수명과 제약은 [Synchronization.md](Synchronization.md)를 따른다.
