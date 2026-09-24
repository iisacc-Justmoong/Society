# Society Preferences

데스크톱 환경설정은 좌측 카테고리와 우측 상세 내용으로 구성한다. macOS 글로벌 메뉴 바의 앱 메뉴
Preferences… 및 ⌘+,로 열며, 같은 창을 재사용한다. Escape·⌘W·창 닫기로 숨긴다.
File·Edit·Window·Help는 확장을 위한 빈 메뉴 구조이다. 환경설정과 종료만 연결한다.
계정 드라이브·연결 관련 기존 설정은 모바일 Environment에 유지한다.

데스크톱 상단의 **Preferences…** 또는 **⌘+,**(macOS), **Ctrl+,**(Windows/Linux)로 독립 LVRS 설정 창을 연다. 좁은 창에서는 설정 아이콘을 표시하며 Devices의 **Preferences…**도 같은 창을 연다. 창은 최초 요청 때 생성하고 이후에는 기존 창을 활성화한다. Escape, OS 창 닫기로 숨기며 메인 창을 닫으면 함께 닫는다.

## Society drive

좌측 Society drive 카테고리의 우측에서 현재 위치를 확인하고, 기존 드라이브 루트 폴더를 직접 입력하거나 Choose folder…로 선택한 뒤 Apply로 적용한다. 선택 취소는 현재 드라이브를 변경하지 않는다. 저장은 기존 `DriveController::openContainer`와 SDK `SharedStorage::setDefaultContainer`를 사용한다. 잘못된 폴더는 기존 연결과 저장 위치를 유지하며 오류를 표시한다. 새 드라이브 생성, 파일 이동·삭제, 계정의 디스크 이미지 위치 변경은 수행하지 않는다. sparsebundle은 먼저 마운트하고 마운트된 드라이브 루트를 선택한다. 공유 위치는 Dreamscapes의 기본 연결에도 사용되며, 이미 실행 중인 다른 앱을 강제로 전환하지 않는다.

Society의 기존 시작 정책은 유지한다. 로그인된 계정에 등록된 호스트 디스크가 있으면 재시작 시 해당 계정 디스크를 우선 복원하며, macOS의 기본 드라이브 복원은 마운트된 Society 디스크를 대상으로 한다. 이 화면은 계정에 등록된 호스트 디스크를 교체하는 기능이 아니다.

모바일 Environment는 `PreferencesContent`를 LVRS 시트로 사용하여 계정의 컨테이너 드라이브 위치, 이동한 디스크 연결, 연결 상태와 **Devices…**를 제공한다. 호스트·클라이언트 역할 선택 항목은 표시하지 않는다.

기기 역할은 플랫폼으로 고정된다.

| 플랫폼 | 역할 |
| --- | --- |
| macOS, Windows, Linux 데스크톱 및 NAS의 SocietyDaemon | Host |
| iOS, Android | Client |

`NetworkDriveController.mode`는 읽기 전용 상수이며 런타임 변경 API가 없다. 서버 연결·연결 해제·로그아웃·컨테이너 변경도 이 값을 바꾸지 않는다. Devices는 현재 기기의 역할을 설명하고 서버 연결 버튼 하나만 제공한다. 데스크톱은 유효한 컨테이너와 인증된 연결이 준비되면 호스팅하며, 모바일에서는 호스팅 옵션을 전달해도 파일 서버를 열지 않는다.

서버 설정에는 주소만 저장한다. 이전 버전이 저장한 `host` 값은 복원 시 제거하고 주소와 로그인 세션은 유지한다. 기기 역할과 컨테이너의 기본 호스트는 서로 다른 개념이다. 기존 미러의 기본 호스트 연결·자동 선출·데이터 보존 정책은 유지하며, 다른 기본 호스트에 연결된 데스크톱도 기기 역할은 Host이다.

`Society.NetworkDrive`는 데스크톱 기본 호스팅, 읽기 전용 역할, 연결 해제 시 다운로드 보존을 검사한다. `Society.ClientOnlyNetwork`는 모바일로 별도 컴파일하여 역할 고정, 레거시 호스트 설정 무시, 호스팅 옵션 차단과 LVRS 화면의 선택 항목 제거를 검사한다. `Society.Account`는 데스크톱의 레거시 클라이언트 설정 복원을, `Society.Pairing`은 여러 기기의 페어링을 검사한다. 한 프로세스 안의 여러 기기 통합 검사는 생성 시 역할이 고정된 `MobileNetworkDevice` fixture를 사용한다. 제품의 기본 생성자는 항상 빌드 플랫폼을 따른다.

`Society.Drive`의 `preferencesDriveLocationAndWindowLifecycle`는 카테고리/상세 영역, 위치 적용·공유 저장, 잘못된 경로에서 기존 연결 보존, 창 재사용, 크기 변경, 키보드 닫기·재열기, Devices 왕복을 검사한다. 이 창은 로그인이나 네트워크 연결 없이 열 수 있다.

```sh
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure -R 'Society\.(NetworkDrive|ClientOnlyNetwork|Account|Pairing|Drive|Daemon)$'
cmake --build build --target Society_qmllint
```

실제 모바일 기기 실행과 외부 서버 배포 검증은 데스크톱의 정책·통합 검사와 별도이다. 연결 및 공개 범위의 상세 계약은 [NetworkDrive.md](NetworkDrive.md)를 따른다.
