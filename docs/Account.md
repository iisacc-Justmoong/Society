# Society iisacc 계정 로그인

인증된 Society 세션은 최대 7일의 LAN 전용 일별 seed를 준비하고, 앱이 짧은 주기의 인증 키를 직접 계산해 [자동 페어링](AutomaticPairing.md)을 수행한다. `AccountController::requestPairingCredentials()`는 준비·갱신이 필요할 때만 기존 쿠키로 `intent: pairing`을 호출하며 SDK에 기기·세션·만료 바인딩 판단을 위임한다. 로그인과 페어링 정보는 [그룹 컨테이너](GroupState.md)에 암호화한다. 계정 판단은 iiAccountManager 0.2.6이 전담한다. SDK의 보안 캐시로 HTTP 없이 복원하고 실제 변경·명시적 요청·만료 전환에만 통신한다. 계정 서버 이외의 디바이스에 로그인 쿠키를 전달하지 않는다.

Society의 데스크톱·iOS/iPadOS·Android는 iiAccountManager SDK의 `AccountViews`를 사용한다.
로그인·회원가입 QML과 대화상자 QObject는 SDK가 소유하며 앱에는 폼 구현을 두지 않는다.
`AccountController`는 앱의 기기 정보·세션·네트워크 연결만 중개한다.
LVRS 입력 필드·버튼·라벨과 화면 크기에 맞는 스크롤 패널을 사용하며, 데스크톱 상단 계정 아이콘과
모바일 Storage의 Sign in/Account 버튼, Devices의 계정 버튼에서 같은 화면을 연다.
로그인에 릴레이 주소나 Society 컨테이너 선택이 필요하지 않다. 로컬 파일 탐색은 로그인 전에도 사용할 수 있다.

## 객체와 책임

`Main.qml`이 `objectName: societyAccount`인 `AccountController` 하나를 소유한다.
이 컨트롤러가 `iisacc::accounts::AccountManager`와 전용 `QNetworkAccessManager`를 소유한다.
`NetworkDriveController.accountSession`은 이를 `QPointer`로 참조하고 별도 계정을 만들지 않는다.
공통 `main.cpp`는 루트 생성 후 같은 manager를 `iiSocietyHelper::Helper::setAccountManager()`에 연결한다.
따라서 모든 플랫폼에서 계정 패널, 네트워크 드라이브, Helper가 같은 Account·AuthorDetails 객체를 읽는다.
계정이 파괴되거나 교체되면 네트워크 연결을 해제하며 Helper의 비소유 참조도 자동 해제된다.

Society의 `signedIn`은 SDK의 `isAuthenticated()`를 그대로 따른다. 공개 JSON에 계정과 세션이 모두 있어도 SDK가 인증하지 않으면 로그인 상태를 만들지 않는다. 실제 원격 파일 접근 권한은 서버가 쿠키를 검증해 결정한다.

## 인증 흐름

1. 이메일·비밀번호를 `POST https://iisacc.com/Account/Session/App`, `intent: login`으로 보낸다.
2. 서버가 Cognito에서 비밀번호와 반환된 ID token을 검증하고 기기 한도를 확인한 뒤 같은 응답으로
   전체 Account 10개 필드·AuthorDetails 20개 필드·앱 로그인 세션을 반환한다. 이메일 코드 입력은 없다.
3. 화면에 표시 이름, 이메일, 사용자 ID, Society Cloud 멤버십을 표시한다. `refresh`로 다시 조회할 수 있다.
4. `logout`은 기기 연결을 먼저 끊고 서버의 현재 앱 세션과 로컬 쿠키·계정 모델을 해제한다.

잘못된 비밀번호·기기 한도·시간 초과·서비스 오류를 표시한다. 비밀번호는 제출 직후와 패널을 닫을 때
입력 필드에서 지운다. 서버가 전체 계정과 해당 기기 세션을 반환해야만 로그인 상태로 전환한다.
회원가입과 웹 계정 관리는 각각 `/Account/SignUp`, `/Account`를 기본 브라우저에서 연다.

실행 중 쿠키는 전용 Qt cookie jar에 두고, refresh·앱 세션 쿠키 두 개를 모바일을 포함한 그룹 컨테이너에 암호화해 보관한다.
암호화 키는 macOS/iOS의 Keychain, Android의 Keystore에 둔다. 비밀번호·코드·ID token·challenge를 영속 저장하지 않는다.
인증정보를 Helper 관측 기록·전달 큐·LAN 페어링·공개 C++ 계정 모델에 넣지 않는다.
앱 시작 시 v2 로컬 권한과 계정·기기·세션 바인딩이 유효한 암호화 캐시가 있으면 보안 쿠키와 전체 계정 모델을 HTTP 없이 복원한다. 캐시가 없으면 서버에서 확인한다. 계정 연결 중이면 로그인 폼 대신 복원 상태를 표시한다.
기존 앱 전용 저장 항목은 그룹 암호문 저장에 성공한 뒤 이전 위치에서 제거한다. 평문 fallback은 없다.

일시적 네트워크 실패는 저장 정보를 유지하고 자동 재시도한다. 로그아웃은 오프라인이어도 자동 복원을 해제한다.
서버 만료·철회 또는 기기 불일치는 세션을 제거하므로 다시 로그인해야 한다. 서버의 30일 절대 만료 정책은 유지한다.
기존 보안 저장소에 유효한 세션이 있으면 다시 로그인하지 않고 그룹 컨테이너로 이전한다.
자세한 SDK 계약은 `SDK/iiAcountManager/docs/SESSION_PERSISTENCE.md`를 따른다.

자동 로그인용 iiAccountManager 0.2.4는 QtKeychain의 플랫폼 보안 저장소 구현을 정적으로 포함한다.
iOS 앱은 Qt의 AVFoundation 백엔드를 명시적으로 선택하여 설치본에 네이티브 의존성이 없는 FFmpeg 플러그인의 자동 링크를 막는다.
기기 번들 검사는 자동 복원 API·보안 저장소 라이선스·네이티브 미디어 플러그인의 실제 심벌도 확인한다.
`SOCIETY_ACCOUNT_URL`은 개발 검증용 origin 재정의이며 기본은 `https://iisacc.com`이다.
SDK는 HTTPS와 loopback HTTP만 허용하고 리다이렉트를 따라가지 않는다.

## 기기와 한도

사용자가 지정한 한도는 **PC 2대·태블릿 2대·휴대폰 2대**이다. 각 유형은 독립적이고 동일 기기의
여러 앱은 슬롯 하나를 공유한다. 휴대폰을 태블릿으로 보고하지 않는다.

| 플랫폼 | 식별자와 유형 |
| --- | --- |
| 데스크톱 | Qt의 machineUniqueId를 iisacc 접두사와 함께 SHA-256으로 해시, `pc` |
| iOS/iPadOS | identifierForVendor 해시, 실제 userInterfaceIdiom으로 `phone`/`tablet` 구분 |
| Android | 서명·사용자·기기 범위의 ANDROID_ID 해시, smallestScreenWidthDp 600 이상은 `tablet`, 그 외 `phone` |

OS가 식별자를 제공하지 않을 때만 QSettings의 비밀값이 아닌 설치 ID를 생성해 재사용한다.
앱 ID는 모든 플랫폼에서 `com.iisacc.society`, 버전은 CMake 프로젝트 버전이다.
이 값은 앱이 보고하는 기기 정보이며 하드웨어 증명이 아니다. OS 초기화·공급자 앱 전체 삭제·앱 서명 변경 등은
식별자를 바꿀 수 있으므로 기존 기기는 iisacc.com의 로그인 기기 관리에서 해제할 수 있다.

## 빌드와 검증

iiAcountManager **0.2.3**의 **Quick** 컴포넌트과 iiSocietyHelper **0.7.0** 이상이 필요하다. 기존 Qt Network·LVRS와
OS API를 재사용하고 새로운 인증 라이브러리나 유료 서비스를 추가하지 않는다. iOS의 계정 SDK는 정적 라이브러리이다.
`tools/build_ios.py`와 `tools/build_android.py`는 대상 ABI의 계정 SDK를 먼저 설치한 뒤 Helper·Society를 빌드한다.
Android SDK별 빌드 디렉터리는 각 SDK의 `build/society-android`, 설치본은 Society의 `build/android/installed`이다.
패키징용 JDK는 `--java-home`으로 지정한다. Qt의 Gradle과 호환되는 JDK 17 또는 21을 사용하며,
이 작업에서는 설치된 CLion의 JBR 21을 사용했다. 시스템 기본 Java 설정은 변경하지 않는다.

Android의 Qt Network는 OpenSSL 백엔드 플러그인과 TLS 공유 라이브러리를 함께 패키징해야 한다.
`tools/build_android_openssl.py`가 Apache-2.0 라이선스의 **OpenSSL 3.5.8 LTS** 공식 소스를
SHA-256 `a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2`로 검증하고
NDK로 빌드한다. 기존 Qt 백엔드의 HTTPS 기능을 사용하기 위한 런타임 의존성으로, 자체 암호 코드는 추가하지 않는다.
유지보수 지원은 2030-04-08까지이다. [공식 배포·라이선스·지원 기간](https://openssl-library.org/source/)

빌드 산출물은 `build/android/openssl-3.5.8/<ABI>/lib`에 있으며 빌드 설정이 같으면 재사용한다.
Qt가 요구하는 `libcrypto_3.so`와 `libssl_3.so` 이름으로 빌드하고 upstream 심볼 버전은 유지한다.
16 KiB 페이지 정렬을 적용한다. CMake의 `SOCIETY_ANDROID_OPENSSL_DIR`가 두 라이브러리를
`QT_ANDROID_EXTRA_LIBS`에 등록하며 누락되면 구성을 실패시킨다. 라이선스 원문도 앱 리소스
`:/licenses/openssl/LICENSE.txt`에 포함한다. [Qt Android OpenSSL 패키징 계약](https://doc.qt.io/qt-6.8/android-openssl-support.html)

`Society.Account`는 합성 계정과 로컬 HTTP 서버로 AccountManager의 비밀번호→계정→갱신→로그아웃,
전체 모델 공유, 잘못된 비밀번호·시간 초과·재시도, 객체 소멸, 데스크톱·휴대폰·가로 화면의 LVRS 패널을 검사하도록 정의한다. 2026-09-09 즉시 로그인 변경에서는 사용자 지시에 따라 이 테스트를 실행하지 않고 컴파일만 한다.
SDK의 계정 네트워크 테스트도 `pc`, `tablet`, `phone` 모두를 검사한다.
서버의 휴대폰 제한은 별도 로컬 Redis에서 동시 로그인·앱 공유·슬롯 해제를 검증한다.

`Society.AndroidTlsBuild`는 다운로드 체크섬 불일치 차단과 Qt 라이브러리 이름/심볼 버전 패치를 검사한다.
`SOCIETY_ACCOUNT_RUNTIME_PROBE=ON` 검증 빌드는 계정 패널을 열고 앱 자체 화면을
AppDataLocation의 `account-runtime.png`로 저장한다. 별도 cookie jar로 인증정보 없이 공개 API를
GET 조회하며 TLS 지원, OpenSSL 버전, 서버 인증서, HTTP 상태, 기기 유형과 앱 ID만 로그에 남긴다.
비밀번호·쿠키·기기 식별자는 기록하지 않는다. 이 옵션은 기본 OFF이며 일반 앱은 이 probe를 실행하지 않는다.
Android 빌드 스크립트에서는 `--account-runtime-probe`로 켜며, 옵션이 없으면 이전 CMake 캐시와 관계없이 끈다.
`--package`는 서명 전 Release APK를 만든다. 개발 실행용 APK는 Qt `androiddeployqt`의 기본 debug 패키징과
검증 기기에 설치된 기존 테스트 서명을 사용한다. 배포용 서명과 스토어 제출은 별도이다.

2026-09-09 macOS 앱 빌드와 **CTest 13/13**, 계정 SDK **CTest 4/4**, 로컬 Redis **13/13**을 통과했다.
macOS 실제 창에서 계정 패널을 열었고 iOS arm64 기기용 Society.app와 File Provider의 서명 빌드를 완료했다.
iOS 시뮬레이터는 설치된 Qt 6.8.3의 arm64 QSQLiteDriverPlugin 초기화 객체가 iOS 기기용이어서 링크에 실패했다.
이 실행에서 iPhone/iPad 실기기 설치·로그인은 확인하지 않았다.

Android arm64 APK를 Android 36 에뮬레이터에 기존 테스트 서명으로 갱신 설치하고 계정 패널을 확인했다.
실제 앱 로그에서 `deviceType: phone`, `appId: com.iisacc.society`, `supportsSsl: true`,
`OpenSSL 3.5.8 25 Aug 2026`, `certificatePresent: true`, `httpStatus: 404`를 확인했다.
APK에 계정·Helper SDK, Qt OpenSSL 플러그인과 두 TLS 라이브러리가 포함되며
`libssl_3.so`의 SONAME·libcrypto 의존성과 16 KiB ELF 정렬도 검사했다.
결과는 `build/account-android-runtime.log`, 앱 화면은 `build/account-android-panel.png`이다.
진단 옵션을 끈 일반 Release APK도 다시 빌드했다. 배포용 서명·스토어 제출은 수행하지 않았다.
설치된 Android LVRS에서는 기존 ApplicationWindow의 Qt 6.8에 없는 padding 속성에 관한 경고 4개가 남아 있다.
계정 패널 표시와 위 TLS 요청은 완료됐으며, macOS QML lint는 경고 없이 통과했다.

실제 Cognito 사용자 로그인·메일 수신과 로컬 fixture 검증은 별도이다. 2026-09-09 공개
`https://iisacc.com/Account/Session/App`은 GET과 인증정보 없는 POST refresh 조회에서 404였다. 이 native API와 서버의 `phone` 지원을
운영 배포하기 전에는 공개 서버에서 앱 로그인이 된다고 가정하지 않는다.

## 2026-09-09 즉시 로그인 변경

모든 Society 플랫폼의 공통 화면에서 이메일 코드 입력·확인·재전송 단계를 제거했다.
기본 서버는 `https://iisacc.com`이며 네이티브 API의 `login` 한 번으로 전체 모델과 기기 세션을 받는다.
서버 연결 및 배포 검증은 인증 없는 상태 확인과 빌드·정적 검사로 제한한다. 실제 계정과 합성 계정 모두 로그인 테스트를 실행하지 않는다.

검증 결과: SDK 0.2.2, Society macOS·iOS·Android 빌드와 QML lint가 통과했다.
iPhone 앱의 서명·번들 검사를 통과했고 연결된 iPhone 15 Pro Max에 설치했다.
앱을 통한 로그인과 합성 로그인 테스트는 실행하지 않았다. 운영 배포는 AWS 관리 세션의 macOS 패스키 잠금 해제 대기이다.

## 2026-09-09 SDK 소유 화면으로 전환

`App/Main.qml`은 `import iiAccountManager as Accounts` 후 `Accounts.AccountViews`에
`accountSession.manager`와 창 overlay만 연결한다. 계정 버튼은 `manager.showAccount()`를 호출한다.
로그인에서 가입으로 이동하는 동작도 SDK의 `showSignUp()`이며 가입 폼을 외부 브라우저로 열지 않는다.
표시 이름·유저 ID·메일·비밀번호/확인·약관·선택 이메일 동의를 입력하고 이메일 코드로 가입을 완료한다.
가입 성공은 동일한 manager.account와 기기 세션을 갱신하므로 Helper·Devices 연결을 바꾸지 않는다.

로그아웃은 SDK의 sessionEnding 신호를 통해 원격 파일 연결을 먼저 종료한다.
미인증 폼 전환·닫기는 비밀번호와 코드 입력, 진행 중 요청 및 이전 코드 힌트를 제거한다.
macOS/iOS/Android 빌드가 같은 Quick 모듈을 패키징하도록 구성했다.
SDK의 화면 구조 검사에는 네트워크 차단기를 사용한다. 실제·합성 로그인 테스트는 실행하지 않는다.
운영 API 배포는 앞서 기록한 AWS 관리 세션 복구가 필요하며 소스 변경과 별도이다.


이 전환의 검증 결과: SDK 0.2.3과 Society macOS·iOS device·Android Release APK 빌드,
SDK/Society QML lint가 통과했다. SDK 화면 구조·이동한 설치 prefix 소비자 CTest 2/2와
macOS Metal 화면 구조 7/7(초기화·정리 포함)이 통과했다. Society 대시보드→계정 화면
열기·닫기 검사도 통과했다. 실제·합성 인증 요청을 수행하지 않은 결과이다.

같은 갱신본은 서명·App Group·File Provider 번들 검사 후 연결된 iPhone 15 Pro Max에 설치했다.
설치 결과는 `build/ios-device/account-views-install.json`에 있으며 앱 로그인은 실행하지 않았다.
Android는 서명되지 않은 Release APK를 생성했고 SDK 가입 API 및 AccountViews/SignUpView 포함을 확인했다.
