# Society Preferences

데스크톱 상단의 **Preferences…** 또는 **⌘+,**(macOS), **Ctrl+,**(Windows/Linux)로 독립 LVRS 설정 창을 연다. 좁은 창에서는 설정 아이콘을 표시하며 Devices의 **Preferences…**도 같은 창을 연다. 창은 최초 요청 때 생성하고 이후에는 기존 창을 활성화한다. **Done**, Escape, OS 창 닫기로 숨기며 메인 창을 닫으면 함께 닫는다. 본문은 스크롤하고 하단 버튼은 창 안에 유지된다.

Preferences에는 호스트·클라이언트 선택 항목이 없다. 계정의 컨테이너 드라이브 위치, 이동한 디스크 연결, 연결 상태와 **Devices…**를 제공한다. 모바일 Environment는 같은 `PreferencesContent`를 LVRS 시트로 사용하며 역할 선택 항목을 표시하지 않는다.

기기 역할은 플랫폼으로 고정된다.

| 플랫폼 | 역할 |
| --- | --- |
| macOS, Windows, Linux 데스크톱 및 NAS의 SocietyDaemon | Host |
| iOS, Android | Client |

`NetworkDriveController.mode`는 읽기 전용 상수이며 런타임 변경 API가 없다. 서버 연결·연결 해제·로그아웃·컨테이너 변경도 이 값을 바꾸지 않는다. Devices는 현재 기기의 역할을 설명하고 서버 연결 버튼 하나만 제공한다. 데스크톱은 유효한 컨테이너와 인증된 연결이 준비되면 호스팅하며, 모바일에서는 호스팅 옵션을 전달해도 파일 서버를 열지 않는다.

서버 설정에는 주소만 저장한다. 이전 버전이 저장한 `host` 값은 복원 시 제거하고 주소와 로그인 세션은 유지한다. 기기 역할과 컨테이너의 기본 호스트는 서로 다른 개념이다. 기존 미러의 기본 호스트 연결·자동 선출·데이터 보존 정책은 유지하며, 다른 기본 호스트에 연결된 데스크톱도 기기 역할은 Host이다.

`Society.NetworkDrive`는 데스크톱 기본 호스팅, 읽기 전용 역할, 연결 해제 시 다운로드 보존을 검사한다. `Society.ClientOnlyNetwork`는 모바일로 별도 컴파일하여 역할 고정, 레거시 호스트 설정 무시, 호스팅 옵션 차단과 LVRS 화면의 선택 항목 제거를 검사한다. `Society.Account`는 데스크톱의 레거시 클라이언트 설정 복원을, `Society.Pairing`은 여러 기기의 페어링을 검사한다. 한 프로세스 안의 여러 기기 통합 검사는 생성 시 역할이 고정된 `MobileNetworkDevice` fixture를 사용한다. 제품의 기본 생성자는 항상 빌드 플랫폼을 따른다.

`Society.Drive`의 `preferencesKeepsPlatformHostingAndReusesItsWindow`는 역할 선택 없이 호스팅이 시작되는지와 창 재사용, 크기 변경, 키보드 닫기·재열기, Devices 왕복을 검사한다.

```sh
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure -R 'Society\.(NetworkDrive|ClientOnlyNetwork|Account|Pairing|Drive|Daemon)$'
cmake --build build --target Society_qmllint
```

실제 모바일 기기 실행과 외부 서버 배포 검증은 데스크톱의 정책·통합 검사와 별도이다. 연결 및 공개 범위의 상세 계약은 [NetworkDrive.md](NetworkDrive.md)를 따른다.
