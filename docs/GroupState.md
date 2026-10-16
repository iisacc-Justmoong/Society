# 선택한 컨테이너의 로그인과 페어링 상태

Society의 로그인 복원 쿠키와 페어링 상태는 `GroupSessionStore`가 관리하는 **AES-256-GCM 암호문**으로 선택한 `.societycontainer/.society-runtime/state/`에 저장한다. GUI와 데몬은 같은 패키지의 상태를 사용하며 선택이 바뀌면 상태 경로도 함께 바뀐다. `Files/`, Finder, Files 앱, Android DocumentsProvider 및 LAN 전송에는 이 비공개 운영 경로를 포함하지 않는다.

| 플랫폼 | 상태 저장 위치 | 암호화 키 |
| --- | --- | --- |
| macOS | 선택한 `.societycontainer/.society-runtime/state/` | 기존 Society 그룹 서비스 이름의 macOS Keychain 항목 |
| iOS·iPadOS | App Group의 `Society.societycontainer/.society-runtime/state/` | 같은 App Group의 Keychain access group, `AfterFirstUnlockThisDeviceOnly` |
| Android | 앱 관리 `Society.societycontainer/.society-runtime/state/` | 기존 QtKeychain·Android Keystore |
| Windows·Linux | 선택한 `.societycontainer/.society-runtime/state/` | 기존 QtKeychain 플랫폼 보안 저장소 |

`SharedStorage::open()`으로 유효한 선택 패키지와 UUID를 확인한 뒤 `ensureRuntimeDirectory("state")`를 통해 필요한 경로만 생성한다. 선택한 패키지가 없거나 접근할 수 없으면 저장·복원을 실패로 보고하며 외부 Application Support, GenericDataLocation, 앱별 임시 경로로 대체하지 않는다. 상태 디렉터리와 파일의 링크·junction·리디렉션은 거부한다. Apple 상태 파일은 백업에서 제외하고 iOS Data Protection을 적용하며, 디렉터리는 0700, 파일은 0600 권한으로 생성한다.

암호화 키는 운영체제의 기존 보안 저장소가 관리한다. macOS의 `SOCIETY_MAC_APP_GROUP=<TeamID>.com.iisacc.society`와 `SOCIETY_MAC_SIGN_IDENTITY`, iOS·iPadOS의 `group.com.iisacc.society` 및 App Group provisioning은 네이티브 키 접근과 앱 협업을 위한 설정이다. 이 설정은 암호문 파일의 저장 경계를 패키지 밖으로 넓히지 않는다. 운영체제의 키 접근 실패에도 평문 저장으로 대체하지 않는다.

이전 OS 그룹·개발 그룹·Android no-backup·Windows/Linux GenericDataLocation 상태는 기존 암호화 레코드를 이전하기 위한 읽기 전용 입력으로 남아 있다. 현재 패키지에 레코드와 철회 표식이 없을 때 요청된 레코드만 크기·형식·경로를 검증하고 내부 `state/`에 복사한다. 이전 디렉터리의 잠금과 무관한 파일은 수정·삭제하지 않고 그곳에 새 상태를 기록하지 않는다. 이 로그인 상태 이전은 과거 디스크 이미지의 마운트·복원과 무관하다.

모바일 경로 검사는 운영체제가 반환한 `.societycontainer` 관리 루트에서 시작해 그 아래 구성요소를 검증한다. 앱 샌드박스 밖의 `/var`·`/data/user` 등을 열람해야만 저장할 수 있도록 만들지 않는다.

각 평문 레코드는 최대 256 KiB이다. SDK가 허용하는 전체 계정 스냅샷과 기기 이력을 함께 담을 수 있으며, 초과한 쓰기는 기존 레코드를 교체하지 않는다. 암호문은 이 제한에 버전·철회 세대·nonce·인증 태그 48바이트를 더한 크기까지만 읽는다.

## 저장과 복원

v3 레코드의 선택적 `hostConnection` 필드에 계정 scope·등록 호스트·컨테이너와
최근 LAN 주소/포트 힌트를 추가 저장한다. 현재 등록 정보와 다르면 사용하지 않으며
로그아웃 시 함께 제거한다. 힌트는 새 서명 응답을 얻기 위한 경로일 뿐 인증 또는
워크스페이스 접근 권한으로 사용하지 않는다. 기존 v3 캐시는 첫 정상 연결 후 자동 보완한다.

로그인은 iiAccountManager가 같은 보안 저장소의 독립 v2 레코드에 쿠키·전체 계정/세션 스냅샷·확인 시각·실패 후 재검증 대기 상태를 저장한다. 비밀번호, OTP, ID token과 challenge는 저장하지 않는다. 계정 복원의 구조·origin·기기·앱·만료 판단을 SDK에 전적으로 위임한다. Society는 구형 v2 페어링 레코드를 SDK에 그대로 전달하여 이전하며 별도 계정 검증이나 서버 확인 타이머를 운영하지 않는다.

페어링 v3 레코드는 SDK의 세션 바인딩, 최대 7일의 일별 LAN seed, 기기 연결 이력 최대 128개, 자동 연결 설정, 실패 요청 차단 상태만 저장한다. 계정 모델과 계정 확인/재시도 시각은 중복 저장하지 않는다. 구형 레코드 복원에 성공하면 즉시 v3으로 다시 저장하여 중복 계정 스냅샷을 제거한다. SDK가 바인딩을 확인한 뒤 Society가 seed 형식·HMAC과 LAN 기기 증명을 검사한다. 저장 기기 목록은 연결 이력이며 접근 권한이 아니다. 실행 nonce·QR·TLS 소켓·큐는 복원하지 않고 현재 발견 결과로 다시 구성한다. 실패 뒤 재실행이나 시간 경과만으로 서버를 재호출하지 않는다. [자동 페어링](AutomaticPairing.md)과 SDK 이벤트 정책을 따른다.

명시적인 Disconnect·모드 변경·수동 페어링으로 일시 중지한 설정은 다음 실행에도 유지한다. Resume automatic pairing으로 다시 활성화한다. 패널 닫기, 객체 파괴, 앱 종료, 모바일 백그라운드 전환은 저장된 설정을 일시 중지로 변경하지 않는다.

## 이전과 무결성

현재 패키지에 레코드가 없을 때만 기존 앱 전용 SessionStore에서 해당 로그인 항목을 읽는다. 선택한 패키지의 `state/`에 암호문을 원자적으로 저장한 뒤 이전 보안 저장소의 항목을 제거한다. 저장에 실패하면 이전 항목을 남긴다. 손상된 암호문을 읽을 때는 이전 로그인으로 우회하지 않는다.

레코드별 키 이름은 SHA-256으로 파일명을 만들며, 원래 키와 철회 세대는 AES-GCM의 인증 데이터에 포함한다. 다른 origin·기기의 파일을 바꿔 넣거나 암호문을 변조하면 복호화하지 못한다. 파일 잠금과 QSaveFile로 쓰기를 직렬화하며, 다른 프로세스가 저장 중이면 이벤트 루프를 막지 않고 최대 10초 기다린다. 로그아웃은 Keychain 비동기 작업 전에 철회 세대를 기록하여 같은 프로세스와 다른 프로세스에서 진행 중이던 오래된 쓰기를 무효화한다. 이전 저장소 삭제 실패나 재실행으로 로그아웃한 세션이 되살아나지 않는다. 정상적인 새 로그인만 새 세대에 저장할 수 있다.

## 의존성과 검증

기존 Qt·QtKeychain, 이미 사용하는 OpenSSL 3 및 Apple의 CryptoKit·Security·Foundation을 재사용한다. 별도 암호 패키지나 서비스는 추가하지 않는다. AES-GCM은 [Apple CryptoKit](https://developer.apple.com/documentation/cryptokit/aes/gcm), 그룹 경로는 [Foundation App Group API](https://developer.apple.com/documentation/foundation/filemanager/containerurl%28forsecurityapplicationgroupidentifier%3A%29), iOS 키 공유는 [Apple Keychain access group](https://developer.apple.com/documentation/security/sharing-access-to-keychain-items-among-a-collection-of-apps) 계약을 따른다. OpenSSL의 Apache-2.0 및 기존 QtKeychain 라이선스 고지를 유지한다.

Android OpenSSL 빌더는 런타임과 같은 ABI에서 생성한 공개 헤더도 함께 배치한다. 헤더가 없는 이전 빌드 캐시는 재사용하지 않는다. 앱은 그 헤더와 이미 패키징하던 `libcrypto_3.so`를 사용한다.

`Society.GroupState`는 재실행 복원, 암호문·nonce·인증 데이터, 기존 로그인 이전, 저장 실패, 변조, symlink 거부, 파일 권한과 동시 로그아웃을 검사한다. `Society.Account`는 실제 loopback 계정 서버·암호화 파일을 사용해 로그인·페어링·일시 중지 복원 및 로그아웃 후 미복원을 검사한다. `SOCIETY_GROUP_STATE_RUNTIME_PROBE`는 opt-in 실기기 합성 레코드 저장·다음 프로세스 읽기 검증이며 실제 쿠키·키를 로그나 결과 파일에 쓰지 않는다. `SOCIETY_GROUP_STATE_PROBE_STAGE=write/read`로 실행하고 기본 빌드에서는 꺼져 있다.

같은 검증 옵션의 `inspect-cache`는 명시한 절대 결과 경로에 스키마·유효기간·바인딩 일치 여부·요청 상태만 기록한다. 계정 식별자, 프로필, 쿠키, seed와 암호화 키를 내보내지 않으며 저장 레코드도 수정하지 않는다.
