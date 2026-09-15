# Society 모바일 공통 화면

데스크톱의 `SocietyView`·`Dashboard`·`ModelMergeTool`·`StorageView`를 iOS와 Android에서도 그대로 사용한다. 모바일 기본 화면은 Dashboard이다. 화면 인스턴스를 유지하여 탭 전환이나 회전으로 프롬프트·검색·모델 병합 설정·현재 폴더·갤러리 선택을 초기화하지 않는다. `DashboardFiles`는 플랫폼과 관계없이 준비된 컨테이너를 읽으며, 초기 동기화가 끝나기 전에는 미완료 드라이브를 검색하지 않는다.

| 영역 | 760 px 미만 모바일 | 넓은 모바일 화면 / 데스크톱 |
| --- | --- | --- |
| 주요 탐색 | Dashboard·Tools·Storage·Browse·Environment 순서의 하단 60 px 바 | 기존 상단 세그먼트 |
| 검색·계정 | 상단 52 px 바, 검색 버튼을 누르면 입력 행 표시 | 기존 상단 검색·계정 |
| 사이드바 | 상단 탐색 버튼으로 Workspace 또는 Storage 시트 열기 | 기존 사이드바 표시 |
| Dashboard | 16 px 본문 여백, 같은 생성 입력과 가로 파일 카드 목록 | 24 px 본문 여백과 기존 카드 크기 |
| Tools | 한 열의 스크롤 폼, 터치 입력과 버튼 | 가용 폭에 따른 기존 한 열 / 두 열 폼 |
| Storage | 2열부터 시작하는 영역 그리드, breadcrumb와 뒤로 이동 | 기존 영역·파일·모델 화면 |
| Storage 작업 | breadcrumb 오른쪽의 작업 시트 | 기존 가져오기·OS 연결 영역 |
| Environment | 공통 `PreferencesContent`를 LVRS 시트에서 표시 | 공통 내용을 별도 Preferences 창에서 표시 |

Browse는 기존 네트워크 기기 패널, 계정 버튼은 기존 iisacc 계정 화면을 연다. Workspace의 Home·Guild·Organization·Local·Cloud·Deleted·기기 항목과 Storage의 9개 영역·다른 기기·Guild·Organization을 모바일에서도 제공한다. 아직 연결되지 않은 워크스페이스 기능은 데스크톱과 같은 안내를 표시한다. Dashboard 파일과 모델 카드의 기존 메뉴는 터치 길게 누르기로도 연다.

Generation History와 Photos는 기존 [정사각형 갤러리](Gallery.md)를 유지한다. 파일 이름은 타일에 표시하지 않으며, 2 px 간격·중앙 크롭·가로 드래그/핀치 확대·세로 스크롤·탭 후 정보 표시를 공유한다. 모바일 Storage의 가져오기·시스템 연결은 시트로 옮겨 갤러리의 본문 높이를 확보한다. Models에서도 뒤로 이동 버튼을 유지한다.

LVRS ApplicationWindow의 상하좌우 시스템 안전 영역 안에 콘텐츠를 배치한다. 소프트 키보드가 창의 아래쪽을 덮으면 겹치는 높이만큼 본문을 줄이며, 탭을 전환하면 키보드를 닫는다. 갤러리의 정보 시트와 탐색·설정·작업 시트는 LVRS의 안전 영역·닫기·스크롤 동작을 사용한다. 화면 배치와 실제 플랫폼 기능 판정은 분리되어 있다. 호스트 모드와 로컬 모델 병합은 기존 컨트롤러가 지원하는 플랫폼에서만 실행한다.

새 외부 의존성은 추가하지 않는다. 기존 설치된 LVRS의 버튼·입력·카드·시트·접근성 계약과 Qt Quick Layouts·ScrollView·Flickable을 사용한다. 앱별 내비게이션과 배치만 Society에서 구성한다.

검증은 `SocietyDriveTests mobileViewsShareDesktopContentAndKeepState mobileStorageKeepsSectionsActionsAndGalleryReachable`로 실행한다. 320×568·390×844·844×390·1024×768의 실제 Main 화면에서 터치 탭·검색 데이터·상태 보존·패널·가용 높이·버튼 범위를 확인한다. 기존 데스크톱 상단 창 버튼 정렬, 카드·Models·Storage·갤러리 회귀 검사도 함께 실행한다. `SOCIETY_MOBILE_SCREENSHOT_DIRECTORY`를 지정하면 합성 파일을 사용하는 화면 캡처를 저장한다. 호스트에서의 모바일 레이아웃 검증과 iPhone/Android 실기기 검증·설치 완료는 각각 별도로 기록한다. 실기기 회귀 시나리오는 `tests/ios/InteractionsTests.swift`에 포함한다.
