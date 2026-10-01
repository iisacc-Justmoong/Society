# Society 모바일 공통 화면

데스크톱의 `SocietyView`·`Dashboard`·`ToolsView`·`StorageView`를 iOS와 Android에서도 그대로 사용한다. 모바일 기본 화면은 Dashboard이다. 화면 인스턴스를 유지하여 탭 전환이나 회전으로 프롬프트·대시보드 검색·모델 병합 설정을 초기화하지 않는다. 화면 회전은 현재 폴더도 유지하지만, Storage 탭을 누르면 Files 최상위 폴더를 바로 연다. Overview 영역 그리드는 제공하지 않는다. `DashboardFiles`는 플랫폼과 관계없이 준비된 컨테이너를 읽으며, 초기 동기화가 끝나기 전에는 미완료 드라이브를 검색하지 않는다.

| 영역 | 760 px 미만 모바일 | 넓은 모바일 화면 / 데스크톱 |
| --- | --- | --- |
| 주요 탐색 | LVRS MobileTabBar: Home·Tools·Storage·Browse·Environment 순서. iOS 70 px / Android 64 px + 외부 안전 영역 | 기존 상단 세그먼트 |
| 검색·계정 | 상단 52 px 바 안의 22×22 px 아이콘 버튼, 검색 입력 높이 22 px | 상단 세그먼트·검색·계정 컨트롤 높이 22 px |
| 사이드바 | 상단 탐색 버튼으로 Workspace 또는 Storage 시트 열기 | 기존 사이드바 표시 |
| Dashboard | 16 px 본문 여백, 22 px 제목 행, 140×160 px 파일 카드 | 24 px 본문 여백과 같은 컨트롤·카드 크기 |
| Tools | 모델 병합 카드 → 작업 화면. 한 열 카드, All tools·가장자리 뒤로 가기 | 모델 병합 카드 하나, 검색·작업 화면 |
| Storage | Files로 바로 진입하고 탐색 시트로 영역 선택. 가장자리 뒤로 이동 지원. Up·breadcrumb 표시줄 없음 | Files로 바로 진입하고 사이드바로 영역 선택. Up·breadcrumb 표시줄 없음 |
| Storage 작업 | 앱 상단 바의 Storage actions 버튼에서 작업 시트 열기 | 넓은 모바일도 앱 상단 바, 데스크톱은 기존 가져오기·OS 연결 영역 |
| Environment | Overview·Devices·Apps와 네 개 앱 컬렉션 탭 | 같은 페이지와 좌측 탐색·우측 계정 패널 |

하단바는 LVRS의 `MobileTabBar`를 사용한다. iOS에서는 떠 있는 캡슐, Android에서는 Material 3의 활성 표시와 아이콘·레이블 배치를 적용한다. QML 기반 플랫폼 형태이며 UIKit/Android Views 자체를 삽입하지 않는다. `autoSelect: false`와 `selectedTab`에서 계산한 `currentIndex`로 선택 바인딩을 유지한다. Browse와 Environment는 독립된 페이지를 선택한다. 환경설정은 Preferences 메뉴 또는 Hosting settings를 통해 연다.

Browse는 연결된 기기의 파일을 탐색하는 페이지이다. 기존 기기 검색 시트는 Environment → Devices → Add device의 모달로 연다. 계정 버튼은 기존 iisacc 계정 화면을 연다. Workspace와 Storage의 기존 탐색 및 터치 길게 누르기 메뉴는 유지한다.

Generation History와 Photos는 기존 [정사각형 갤러리](Gallery.md)를 유지한다. 파일 이름은 타일에 표시하지 않으며, 2 px 간격·중앙 크롭·가로 드래그/핀치 확대·세로 스크롤·탭 후 정보 표시를 공유한다. 모바일 Storage의 가져오기·시스템 연결은 시트로 옮겨 갤러리의 본문 높이를 확보한다. Models에서도 뒤로 이동 버튼을 유지한다.

LVRS ApplicationWindow의 상하좌우 시스템 안전 영역 안에 콘텐츠를 배치한다. 소프트 키보드가 창의 아래쪽을 덮으면 겹치는 높이만큼 본문을 줄이며, 탭을 전환하면 키보드를 닫는다. 갤러리의 정보 시트와 탐색·설정·작업 시트는 LVRS의 안전 영역·닫기·스크롤 동작을 사용한다. 화면 배치와 실제 플랫폼 기능 판정은 분리되어 있다. 호스트 모드와 로컬 모델 병합은 기존 컨트롤러가 지원하는 플랫폼에서만 실행한다.

새 외부 의존성은 추가하지 않는다. LVRS Tabs 페이지를 구현한 `MobileTabBar`·`MobileTab`과 기존 입력·카드·시트·접근성 계약을 사용한다. Main이 시스템 안전 영역을 이미 적용하므로 탭바의 `bottomSafeInset`은 0으로 둔다. 선택·키보드 탐색·터치 영역과 플랫폼별 탭바 모양은 LVRS가 담당하고 Society는 목적지와 화면·패널 연결을 담당한다.

Dashboard 본문의 크기는 Qt 논리 px를 기준으로 한다. Tools 탭 QuickGenerate는 Figma Dreamscapes `203:6930`의 compact 배치로 복원하였다. 위쪽 전체 폭 Prompt 입력란과 8px 아래의 Image·화면비·수량 선택 및 오른쪽 Generate로 구성한다. LVRS 입력과 버튼 높이는 22px, 콘텐츠는 52px, 바깥 여백을 포함한 높이는 72px이다. 원본 18×18 SVG를 세 드롭다운에 사용하며 LVRS 재질·Pretendard Medium 13px을 유지한다. 기본 Image/1:1/1개와 기존 Image/Video 요청·초안 보존 계약은 유지한다. `quickGenerateUsesLvrsComposer`는 320·390·960px와 Figma 원본 콘텐츠 폭 1152px에서 compact 배치, 아이콘, 비율·수량 선택, 마우스·터치 제출을 검증한다.

선택한 미디어 유형과 프롬프트는 Tools 이동·크기 변경 후에도 유지한다. Generate와 Enter는 기존 `generateRequested(prompt, mediaType, aspectRatio, count)`로 전달하며 신규 입력의 내부 기본값은 1:1, 1개이다. Society의 생성 공급자는 아직 연결되지 않았으므로 완료를 가장하지 않고 기존 안내를 표시한다. `quickGenerateUsesLvrsComposer`는 320·390·960px 배치와 터치 제출을, `dashboardKeepsStorageAndPromptState`는 Tools 통합과 Video 선택 보존을 검사한다.

Recent files는 `Files/` 및 하위 폴더에서, Generation history는 `Generation History/`에서 각각 최신 20개까지 표시한다. 검색도 각 출처 안에서 같은 상한을 적용하며 데스크톱과 동일한 `DashboardFiles` 데이터 모델을 사용한다. [조회·검증 기준](Dashboard.md)을 따른다.

상단의 탐색·검색·계정 버튼도 같은 크기 규칙을 따른다. Society에서 44×44 px로 강제하던 크기를 제거하고 LVRS `IconButton`의 기본 22×22 px 프레임과 18×18 px 아이콘을 사용한다. 검색 입력과 가로 화면의 상단 세그먼트·계정 버튼도 모바일용 44 px 최소/명시 높이를 적용하지 않는다. 플랫폼이나 화면 회전으로 상단 컨트롤의 논리 크기를 확대하지 않는다.

`SocietyDriveTests mobileDashboardKeepsLogicalControlSizes`는 iOS 테마와 430·320·932 px 폭에서 데스크톱 대비 입력·버튼 높이, 섹션 간격, 카드 크기, 화면 경계와 실제 터치 제출을 검사한다. 실기기 XCTest의 `testDashboardUsesLogicalControlSizesInBothOrientations`는 iPhone 세로·가로 화면의 접근성 좌표에서 22 pt 높이를 확인하며, `testDashboardMenusKeepTheirSelectionAtLogicalSize`는 터치 메뉴 선택과 탭 왕복 후 상태 보존을 검사한다.

검증은 `SocietyDriveTests mobileViewsShareDesktopContentAndKeepState mobileStorageKeepsSectionsActionsAndGalleryReachable`로 실행한다. 320×568·390×844·844×390·1024×768의 실제 Main 화면에서 터치 탭·검색 데이터·상태 보존·패널·가용 높이·버튼 범위를 확인한다. 기존 데스크톱 상단 창 버튼 정렬, 카드·Models·Storage·갤러리 회귀 검사도 함께 실행한다. `SOCIETY_MOBILE_SCREENSHOT_DIRECTORY`를 지정하면 합성 파일을 사용하는 화면 캡처를 저장한다. 호스트에서의 모바일 레이아웃 검증과 iPhone/Android 실기기 검증·설치 완료는 각각 별도로 기록한다. 실기기 회귀 시나리오는 `tests/ios/InteractionsTests.swift`에 포함한다.

하단 탭바 회귀 검사는 `SocietyDriveTests mobileViewsShareDesktopContentAndKeepState`의 iOS·Android 두 행에 포함한다. PageTabList/PageTab 역할, Browse·Environment 페이지 전환과 선택 상태, 기존 화면·입력 보존, Storage 탭의 Files 복귀와 작은 화면의 레이블 경계를 확인한다. 같은 검사에서 상단 버튼의 22×22 px 화면 좌표 크기, 18×18 px 아이콘, 펼친 검색창과 가로 화면 컨트롤의 22 px 높이를 확인한다. 캡처 이름에도 플랫폼을 포함한다. LVRS의 세부 API는 `docs/components/navigation/Tabs.md`를 참조한다.
