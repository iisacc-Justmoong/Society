# Society desktop dashboard

## 공용 안내창

`Main.qml`의 `dashboardNotice`는 `LV.Alert`를 사용하며, 아이콘 숨김과 단일
확인 버튼만 지정한다. 크기·폰트·모서리를 앱에서 재정의하지 않는다.
[LVRS 원본 Alert 658:229](https://www.figma.com/design/0GkItQYSNIR0lZ3iJhfJzc/Layerd-Visual-Render-System?node-id=658-229)의
폭 500, 제목 26, 본문 13, 버튼 높이 56을 논리 픽셀로 사용한다. DPR 2인
Retina 화면 캡처의 1000 픽셀 폭은 이중 확대가 아니다. 390 폭의 창에서는
좌우 24 여백을 확보해 카드가 342로 줄어든다.

Society의 프라이머리 컬러는 녹색 `#57965C`이며 Alert의 확인 버튼과 아이콘도
이 앱 강조색을 따른다. Figma의 파란색은 기본 테마의 예시이며 Society의
브랜드 색상을 대체하지 않는다. 배경 유리 효과는 LVRS 창의
`materialBackdropSource`를 사용한다. `Society.Drive`의
`noticeKeepsFigmaGeometryAndMaterial`은 1440×900, 800×600, 390×844 창에서
실제 안내창의 크기·글자·색상·배경 캡처 경로와 확인 버튼 닫기를 검증한다.
`SOCIETY_ALERT_CAPTURE_DIR`를 `build/` 아래 경로로 지정하면 각 화면의
PNG와 논리 치수·DPR JSON을 저장한다. GPU 유리 효과는 LVRS의 네이티브
Metal 픽셀 검사로 별도 검증하며, 호스트의 좁은 창 검사는 모바일 실기기
검증과 구분한다.

## 화면 구성

Society 데스크톱의 기본 화면은 [Figma 18:14](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=18-14)의 대시보드 디자인이다. `Main.qml`의 `LV.ApplicationWindow`가 창과 컨트롤러·대화상자를 소유하고, `SocietyView.qml`을 공개 `content` 슬롯에 전달한다. 창의 프레임·닫기·최소화·최대화와 이동·크기 조절은 LVRS가 소유하며 앱이 창 제어 버튼을 직접 그리지 않는다.

상단 바는 [Figma 39:2296](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=39-2296)의 56 px 한 행이다. 데스크톱 콘텐츠를 창 위쪽에서 바로 시작하고, `nativeTitleBarHeight`를 상단 바 높이에 연결해 macOS의 실제 창 버튼과 탭·검색·계정 버튼의 중심을 맞춘다. `nativeTitleBarControlsRect` 오른쪽에 12 px 여백을 두어 탭과 창 버튼이 겹치지 않도록 한다. 전체 화면에서는 네이티브 버튼 예약 폭을 없애며, 복귀와 크기 변경은 LVRS가 처리한다. `windowDragHandleHeight`는 같은 행 전체를 덮고 `windowDragExclusionItems`로 탭·검색·계정 영역을 제외하여 클릭과 입력을 보존한다. 모바일은 같은 Dashboard·Tools·Storage 화면을 시스템 안전 영역 안에 표시하며, 좁은 화면의 내비게이션은 하단으로 이동한다. [모바일 레이아웃](MobileViews.md)을 참조한다.

대시보드 콘텐츠는 [Figma 62:2386](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=62-2386)의 카드 구성이다. `SocietyView`는 56 px 상단 도구 모음, 204 px 사이드바, 24 px 콘텐츠 여백, 캘린더와 Recent files·Generate history의 가로 카드 목록을 조합한다. `LV.Card.File`의 Small/Brief를 140×160 논리 px로 배치하며 카드 간격은 8 px, 제목 행은 22 px, 제목과 카드 간격은 12 px, 섹션 간격은 24 px다. 세그먼트, 입력, 버튼, 카드, 메뉴, 안내창은 LVRS 컴포넌트이며 새 아이콘이나 카드 표면을 직접 그리지 않는다. 배경은 LVRS의 공유 창 표면을 그대로 사용하고, 배치는 Item·ListView·ScrollView와 LVRS Stack을 사용한다. `SocietyView`에는 창 플래그나 창 제어 동작이 없으며 사용자 동작은 신호로 `Main.qml`에 전달한다.

- 상단 세그먼트는 `Dashboard → Tools → Storage → Browse → Environment` 순서이다. `Dashboard`는 기본 화면이다. `Tools`는 [도구 카드 목록](Tools.md)을 열며, 카드 선택 후 이미지 빠른 작업 또는 모델 병합 화면으로 진입한다. `Storage` 탭은 Overview 없이 Files 최상위 폴더를 바로 열며, 사이드바로 Society 드라이브의 9개 영역을 탐색한다. 모델 가져오기·OS 드라이브 연결도 유지한다. 세 화면을 유지하므로 Tools를 포함한 탭 전환으로 병합 설정, 프롬프트, 종횡비, 생성 수량을 초기화하지 않는다. Storage 탭을 다시 누르면 이전 영역·하위 폴더에서 Files 최상위로 돌아온다.
- `View all files`는 각각 Storage의 Files와 Generation History를 연다. 카드 클릭과 키보드 활성화는 기존 DriveController의 경계 검사를 거쳐 파일을 연다. 우클릭 또는 호버·키보드 포커스 시 나타나는 카드 메뉴에서 `Open`과 `Reveal in Storage`를 선택할 수 있으며, 후자는 파일의 부모 폴더를 Storage에서 연다.
- Recent files의 `View all files`로 여는 Storage Files 목록은 수정 시각 오름차순으로 배치한다. 오래된 파일은 위쪽, 최신 파일은 아래쪽에 있고 최초 조회와 레이아웃 완료 후 최하단을 표시한다. Files의 하위 폴더에도 같은 동작을 적용하며 폴더를 먼저 표시한다. 자동 갱신 시 과거 파일을 탐색하던 위치와 선택을 보존하고, 최하단에 있으면 새 최신 파일을 따라간다.
- 사이드바는 Home·Projects·Calendar·Activity·People의 Workspace 그룹, Guilds와 Organization 목록으로 구성한다. Home은 대시보드 맨 위로, Calendar는 월간 캘린더로, Activity는 선택 날짜의 전체 활동 목록으로 이동한다.
- `Browse`는 기존 계정의 네트워크 기기 탐색을 연다. `Environment`는 데스크톱 Preferences 창을 열고 모바일에서는 같은 설정 내용을 시트에 표시한다. `Ctrl+,`(macOS의 Command+,)도 유지한다.
- Search는 이름·Society 내부 경로를 대소문자 구분 없이 검색하고 Dashboard를 표시한다. 빈 입력은 전체 최근 목록으로 돌아간다.
- 탐색 항목은 실제 접근성 이름을 제공하며 라이브러리의 임시 상세 설명을 화면 읽기 프로그램에 노출하지 않는다.
- 760 px 미만에서는 사이드바를 숨기고, 카드는 크기를 유지하며 각 목록 안에서 가로 스크롤한다. 목록은 영역 밖을 클리핑하고 방향키·Home/End 탐색을 지원한다. 700 px 미만에서는 도구 모음의 검색을 숨기며, 탭의 가용 폭이 부족하면 가로 스크롤을 제공하여 창 버튼과 계정 버튼을 가리지 않는다. 모바일도 Dashboard로 시작하고 같은 데이터 모델로 검색한다. 760 px 미만에서는 5개 탭을 하단에, 검색·계정·탐색 패널 버튼을 상단에 표시한다. 숨겨진 사이드바는 탐색 시트에서 사용할 수 있으며 호스트 모드는 제공하지 않는다.

`DashboardFiles`는 iiSocietyContainer의 유효한 드라이브에서 `Files/`와 `Generation History/`만 조회한다. Recent files는 `Files/` 및 그 하위 폴더의 파일만 표시하고, Generation history는 `Generation History/` 바로 아래의 이미지 파일만 표시한다. 두 목록은 출처가 분리되어 있어 생성 이미지가 더 최신이어도 Recent files에 섞이지 않으며, `Files/`가 비어 있으면 Recent files도 빈 상태이다. `Files/` 안의 이미지는 다른 파일과 동일하게 Recent files에 포함한다. Photos·Models·Deleted·Asset Library 등 다른 영역은 두 목록의 조회 대상이 아니다. QtConcurrent 작업 스레드에서 파일을 읽고 수정 시각 내림차순으로 정렬하며, 데스크톱·모바일 모두 각 목록의 상한은 20개이다. 검색은 각 영역의 전체 스냅샷에 먼저 적용한 뒤 최신 20개까지 표시하므로, 처음 표시되지 않은 파일도 검색할 수 있다. `View all files`로 여는 Storage 전체 목록에는 이 대시보드 상한을 적용하지 않는다. 이력의 JSON과 이전 앱별 하위 폴더, 심볼릭 링크, 숨김 파일은 제외한다. 빈 경로·상대 경로는 작업 디렉터리를 열지 않는다. 컨테이너가 바뀌면 진행 중 조회를 취소하고 이전 결과를 폐기한다. `Dashboard`는 필수 `DashboardFiles viewModel`을 주입받고 생성될 때 현재 저장소를 다시 조회한다. 경로가 같은 뷰모델로 화면을 재생성해도 최신 스냅샷을 읽으며, 앱 재실행·Dashboard 복귀·창 재활성화·모델 가져오기 완료 시에도 갱신한다. 조회 경로는 로컬 드라이브에 직접 연결하므로 네트워크 준비 신호를 기다리지 않는다. 다만 SDK가 아직 공개하지 않은 초기 복제본(`isReady() == false`)은 표시하지 않는다. 뷰모델은 manifest와 대상 디렉터리·파일을 감시해 추가·수정·삭제와 원자적 교체를 150 ms 단위로 모아 다시 조회한다. 컨테이너 전환 시 감시·예약 갱신과 이전 작업을 해제하며, 조회 중 중복 요청은 다음 조회 한 번으로 합친다. 내용이 같은 스냅샷은 목록을 교체하지 않는다. 화면 제목은 `Recent files`, `Generate history`이며 저장 디렉터리명 `Generation History`는 유지한다. 별도 인덱스·프롬프트·생성 요청 파일을 드라이브에 쓰지 않는다.

카드에는 실제 파일명과 로컬 수정 날짜(`yyyy-MM-dd`)를 표시한다. 이미지 파일은 경계를 확인한 로컬 파일 URL을 `LV.Card.previewSource`에 전달하며 LVRS가 카드 전체에 맞춰 중앙 크롭한다. 비이미지 파일과 읽을 수 없는 이미지는 LVRS의 기본 미리보기 아이콘을 사용한다. Figma의 예시 사진·파일명·날짜를 제품 데이터에 하드코딩하지 않으며, 데이터가 없으면 빈 상태를 표시한다. 네트워크에 연결된 경우에만 기기 상태를 `Online`으로 표시한다.

미리보기 URL에는 수정 시각·파일 크기 버전을 붙인다. 같은 경로의 이미지가 교체되어도 Qt의 이전 이미지 캐시를 재사용하지 않고 변경된 썸네일을 읽는다.

Tools 탭 최상단으로 이동한 QuickGenerate는 [Dreamscapes Figma 15:218](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=15-218)에 맞춰 Prompt, Image, 종횡비, 생성 수량 선택과 Enter/Generate 제출을 제공한다. 수량의 기본값은 1이며 `1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 20, 25, 30, 40, 50, 100, 200, 500, 1000` 중 선택한다. 기존 LVRS 버튼·메뉴·메뉴 항목을 재사용하고 긴 수량 목록은 Qt Quick ListView로 스크롤한다. 메뉴를 창 높이 안으로 제한하며 다시 열면 선택 항목을 표시한다. 선택 수량은 ToolsView → SocietyView → Main의 `generateRequested(prompt, mediaType, aspectRatio, count)` 신호에 전달되고 탭 전환 후에도 유지된다. 현재 Society에는 생성 공급자가 연결되지 않았으므로 제출 시 LVRS 안내창으로 그 상태를 알리고 입력을 보존한다. Guild·Organization의 서비스 연결도 이번 화면 구현 범위에 포함되지 않으며 안내를 표시한다.

캘린더는 Qt 비의존 C++23 SDK `iiCalendar`(ICU·SQLite)를 사용한다. 파일 조회와 캘린더 조회에는 Qt 6.8.3의 Concurrent 모듈을 연결한다. 동일한 Qt 배포·라이선스·유지보수 범위이며 앱 자체 도메인인 카드 데이터만 C++로 구성한다.

카드 회귀 검증은 `SocietyDriveTests dashboardCardRowsMatchFigmaAndRemainInteractive`로 실행한다. 1440/760/390 px 폭에서 Figma 좌표·140×160 크기·8 px 간격, 이미지 크롭, 날짜, 가로 스크롤, 카드 클릭·Space·Enter, 메뉴의 열기·부모 폴더 이동, 빈 검색 결과를 확인한다. `SOCIETY_DASHBOARD_CARDS_SCREENSHOT_PATH`로 캡처 위치를 지정할 수 있고, 선택적인 `SOCIETY_DASHBOARD_CARDS_PREVIEW_PATH`는 테스트용 참조 사진에만 사용한다.

`SocietyDriveTests dashboardListsShowAtMostTwentyFiles`는 파일 25개와 그보다 최신인 생성 이미지 25개를 준비해 데스크톱·모바일의 실제 Main 화면에서 출처 분리, 목록별 20개 상한, 최신순 정렬, 마지막 카드까지의 가로 탐색을 검사한다. 상한 밖의 파일 검색, 영역별 검색 결과와 검색 해제 후 복원을 함께 확인한다. `dashboardFilesUseRealDriveDataAndRejectStaleRoots`는 5개 최근 파일·15개 생성 이력, 하위 폴더의 이미지, 두 영역의 동일한 파일명, 다른 저장 영역 제외, 생성 이력만 존재하는 컨테이너를 검사한다.

Storage 시간순 탐색은 `SocietyDriveTests viewAllRecentFilesOpensStorageAtTheNewest`에서 실제 버튼 클릭, 파일 이름과 반대인 수정 시각 순서, 초기 최하단 위치를 검사한다. `SOCIETY_RECENT_STORAGE_SCREENSHOT_PATH`로 해당 화면을 캡처할 수 있다. `SocietyGuiTests chronologicalFilesOpenAtTheNewestAndPreserveBrowsing`는 새 파일 추가·삭제와 수정 시각 변경 후 선택·스크롤 복원, 최하단 추적, 빈 폴더 왕복과 이름순 복귀를 검사한다. 새 파일 도착 후 비동기 조회와 예약된 위치 복원이 끝난 것을 확인한 다음 과거 파일 탐색을 재현하여 이전 레이아웃의 `atYEnd` 값으로 검사가 앞서 진행되지 않도록 한다.

검증: `Society.Drive`에서 기본 창 프레임 유지, `content` 슬롯의 뷰 소유 관계, 상단 바의 y=0·높이 56 px·컨트롤 중심선, 1440/760/360 px 폭에서 Tools·Storage 탭 클릭과 검색 입력의 드래그 영역 충돌 방지, Dashboard·Tools·Storage의 순서와 화면 전환·접근성 선택 상태, 파일 정렬·검색·이미지 이력·링크/외부 경계·빈 경로·취소 후 오래된 결과 폐기, 생성 수량 옵션·스크롤·요청 전달, 세 화면 왕복 시 상태 보존, 기존 탐색·드롭·Preferences 동작을 검사한다. 빌드와 `Society_qmllint`, 실제 macOS 창의 화면 비교도 수행한다.

모바일 회귀 검증은 `SocietyDriveTests mobileViewsShareDesktopContentAndKeepState mobileStorageKeepsSectionsActionsAndGalleryReachable`로 실행한다. 실제 Main을 모바일 레이아웃으로 열어 320×568, 390×844, 844×390, 1024×768에서 탭 전환·터치·검색·상태 보존·작업 시트·Models 뒤로 이동과 갤러리의 가용 높이를 확인한다. `SOCIETY_MOBILE_SCREENSHOT_DIRECTORY`는 이 테스트의 화면 캡처 디렉터리이다. 이 검증은 호스트 Qt에서 모바일 레이아웃을 실행하며, 실기기의 안전 영역·키보드·사진 권한 검증과 구분한다.

초기 데이터 회귀 검증은 `dashboardLoadsLocalFilesWhileNetworkMirrorIsPending`, `dashboardCreationRefreshesTheInjectedViewModel`, `dashboardObservesLocalAdditionsEditsAndRemovals`이다. 네트워크 준비와 무관한 기존 로컬 파일 표시, 동일 뷰모델로 화면 재생성, 실제 폴더·파일 추가/수정/삭제, 초기 복제본의 공개 전후 전환과 제목 대소문자를 확인한다. 카드·모바일 크기 테스트도 수동 배열 대신 실제 컨테이너의 뷰모델을 사용한다.

UI Automation을 사용할 수 없는 실기기는 기본 OFF인 `SOCIETY_DASHBOARD_RUNTIME_PROBE` 빌드와 실행 환경 `SOCIETY_DASHBOARD_PROBE=1`로 검증할 수 있다. 최대 30초 동안 실제 뷰모델과 화면 목록의 개수, 첫 목록 표시 시각, 제목·미리보기 상태를 읽고 앱 Documents에 `dashboard-probe.json`·`dashboard-probe.png`를 저장한다. 저장소 내용·계정·동기화 설정은 바꾸지 않으며, 검증 후 이 옵션을 끈 일반 빌드를 설치한다.

## Dreamscapes 공유 생성 기록

`DashboardFiles`의 조회·감시 구현은 `iiSocietyContainer::Gui`로 이동했다. 앱의 동명 클래스는 QML 등록 래퍼이며 기존 대시보드 동작을 유지한다. Dreamscapes도 같은 SDK의 `generationHistory`를 QuickGenerate 아래에서 표시한다.

`View all`이 여는 `society://generation-history`는 Society의 `showStorage("generation-history")`에 연결되며 열린 창을 전면에 표시한다. `cmake/ApplicationLinks.cmake`가 Apple URL scheme과 Android intent filter를 기존 패키지 설정에 추가하고 Linux desktop entry도 같은 scheme을 등록한다. 다른 저장소 경로나 사용자 자격 증명은 URL로 받지 않는다. SDK URL 테스트 및 실제 앱 실행 인수와 MCP 상태 조회로 Generation History 이동을 검사한다.


## iiCalendar 대시보드

[Figma 62:2386](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=62-2386)의
월간 캘린더와 일별 상세 패널을 대시보드 최상단에 구현했다. 1440 px 데스크톱에서
사이드바 교체 후 캘린더는 803×370, 상세 패널은 355×370이며 간격은 10이다. 부모의 콘텐츠 여백 24와
캘린더 내부 여백 10을 사용한다. 캘린더 영역의 가용 폭이 700 미만이면 370 높이의
두 패널을 세로 배치하고 기존 대시보드 전체 스크롤로 캘린더와 파일 목록에 접근한다.
글꼴·버튼·체크박스·아이콘은 설치된 LVRS를 사용한다. 테마의 녹색 강조색을 유지한다.

- 월 이전/다음, Today, 날짜 선택, 상세 패널의 일 이전/다음, 날짜 방향키 탐색을 제공한다.
- 월 그리드는 `iiCalendar::makeView`의 월요일 시작 42칸이다. 선택 날짜는 녹색 배경,
  오늘은 외곽선, 해당 날짜의 일정은 점으로 표시한다. 날짜·DST·반복 일정 계산은 SDK가 수행한다.
- `iiCalendar::dayDetails`의 Events·Tasks·Activity·Files를 2/2/1/1개로 미리 보여준다.
  Notes·Reminders도 값이 있으면 표시한다. `View all`은 같은 패널에서 전체 목록을 열며
  100개 단위 이전/다음 페이지로 최대 SDK 조회 한도 내의 모든 항목에 접근한다.
  항목 선택 시 설명, 정확한 시작/종료 시각, 위치, 참가자·첨부 개수를 확인한다.
- 작업 완료는 SQLite에 revision 조건으로 저장한다. 충돌은 오류로 표시하고 타 변경을
  덮어쓰지 않는다. 반복 작업은 SDK에 개별 회차 완료 모델이 없으므로 체크박스를 비활성화한다.
- 첨부파일은 현재 Society 컨테이너 안에 실제 존재하는 파일만 기존 파일 열기 동작으로
  전달한다. 외부 URL·상위 디렉터리 이탈·심볼릭 링크는 열지 않는다.
- 임의의 일정과 Figma 예시 파일은 테스트 픽스처에만 사용한다. 실제 데이터가 없으면
  날짜별 빈 상태를 표시하며 사용자 데이터베이스에 데모를 자동 삽입하지 않는다.

`DashboardCalendar`는 Society의 Qt 표현 계층이다. SDK는 Qt/QML에 의존하지 않으며,
QML에서 JavaScript Date나 부동소수점 epoch로 일정을 계산하지 않는다. 정확한 시각과
revision은 문자열로 전달하여 18자리 초 소수부·64비트 정수가 손실되지 않는다.
기본 시간대는 생성 시 OS 시간대이며 `timeZone`·`calendarSystem` C++/QML 속성으로
변경할 수 있다. 현재 디자인의 기본 화면은 Gregorian 달력이며 역법 선택 UI는 추가하지 않았다.

조회는 QtConcurrent에서 실행한다. 컨테이너·날짜 변경 시 이전 요청의 결과를 폐기하고
중복 요청은 다음 한 번으로 합친다. 대시보드가 활성인 동안 30초마다, 대시보드 복귀 시
갱신한다. 컨테이너가 최초 복제를 마치면 파일 모델의 갱신을 통해 다시 연결한다.
일별 총수와 첨부파일은 SDK의 중복 제거 규칙을 사용한다. SDK 조회 한도를 초과하면
오류를 표시하며 결과를 조용히 잘라내지 않는다.

SQLite 경로는 `QStandardPaths::AppLocalDataLocation/Calendar/<SHA256(containerId)>.sqlite`이다.
`SOCIETY_CALENDAR_DIRECTORY`는 테스트와 명시적인 로컬 저장 위치 지정에 사용한다.
드라이브 경로 이동 후에도 동일 식별자의 데이터는 유지하고 다른 컨테이너의 일정은 격리한다.
SQLite/WAL 파일을 일반 파일 동기화에 넣지 않으며 **현재 일정은 기기 로컬 데이터**이다.
외부 캘린더 공급자, OS 일정 가져오기, 원격 일정 동기화, 일정 생성·편집 폼은 이 뷰 구현에
포함되지 않는다. 기존 SDK 소비자는 `databasePath`의 `iiCalendar::Store::put`으로 이벤트·
할 일·활동·메모·알림·첨부를 기록하고 `refresh()`로 화면에 반영할 수 있다.

### 빌드와 검증

먼저 `SDK/iiCalendar` README대로 SDK를 빌드·테스트하고 `build/install`에 설치한다.
Society의 기존 CMake 설정에 다음 패키지 경로를 추가한다(크로스 빌드는 대상 플랫폼용
ICU·SQLite·iiCalendar 설치본을 사용해야 한다).

```sh
cmake -S . -B build \
  -DiiCalendar_DIR=/Volumes/Storage/Workspace/SDK/iiCalendar/build/install/lib/cmake/iiCalendar \
  -DICU_ROOT=/Volumes/Storage/Workspace/SDK/iiCalendar/build/deps/icu/install
cmake --build build --target Society SocietyCalendarTests SocietyDriveTests Society_qmllint --parallel 8
ctest --test-dir build -R '^Society.Calendar$' --output-on-failure
```

`Society.Calendar`는 실제 SQLite 데이터의 월간 표시, 고정밀 시각 전달, 완료 상태 재열기,
revision 충돌, 잘못된 날짜·시간대, DST 전환일, 컨테이너별 격리와 오래된 응답 폐기를 검사한다.
`SocietyDriveTests dashboardCalendarMatchesFigmaAndNavigates`는 실제 Dashboard QML의
Figma 치수, 점·날짜 선택, 전체 목록·뒤로 이동, 작업 완료, 첨부 경계, 월·일·오늘 이동,
390/760 px 세로 배치를 검사한다. `build/calendar-dashboard.png`와
`build/calendar-dashboard-mobile.png`에 테스트 창을 캡처한다.


2026-09-19 검증에서 SDK CTest 9/9, Society Calendar 데이터 통합 테스트,
대시보드 회귀 실행 13/13이 통과했다. macOS Cocoa 네이티브 창에서도 같은 캘린더
인터랙션을 검증하고 Retina 캡처를 저장했다. 모바일 크기는 macOS 호스트에서의
반응형 레이아웃 검사이며 iOS/Android 실기기 검증을 의미하지 않는다.
전체 `Society_qmllint`는 기존 SDK 래퍼의 기반 타입 메타데이터 경고를 출력하지만,
추가한 Calendar QML 파일에서는 경고가 발생하지 않았다.

최종 `build/bin/Society.app`는 서명 검증과 `Society.MacRuntimeLaunch`를 통과했다.
개발용 DYLD/QML 검색 경로 없이 GUI 실행 파일 776개, 데몬 765개, QML 시작 단계
810개의 동적 라이브러리가 번들 내부 또는 macOS 시스템 경로에서만 로드되었다.
컨테이너가 없는 시작도 성공했다. 실행 증거는 `build/calendar-runtime-evidence.log`이다.

## 대시보드 사이드바 192:4382

[Figma 192:4382](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=192-4382)를
`DashboardSidebar.qml`에 반영했다. 폭 204 px, 바깥 여백 12 px, 그룹 간격과 제목 아래 간격
8 px, 제목 높이 12 px, 연속 행 높이 32 px를 사용한다. LVRS의 `panelBackground04`,
Description·Body 텍스트와 Navigation ListItem 및 18 px disclosure 아이콘을 재사용한다.
행 사이에 추가 간격·구분선·카드를 넣지 않는다. 이전 Locations와 하단 기기 카드는 제거했다.

Home·Projects·Calendar·Activity·People·Organization 아이콘은 해당 Figma 노드의 SVG
export를 `src/App/Dashboard/icons/`에 보존했다. Guild 깃발은 같은 Figma 컴포넌트의
기존 `SDK/LVRS/resources/iconset/flag-asset.svg` 원본이다. 새 벡터를 직접 그리지 않았으며
앱 QML 리소스에도 포함하여 네트워크가 없어도 표시한다.

Guilds·Organization은 `StorageNavigation`의 계정별 실제 목록을 사용한다. 항목 선택은
기존 workspace 요청으로 전달한다. 데이터가 없으면 빈 상태를 표시하며 Figma의
`GuildName` 3개는 검증 픽스처에서만 사용한다. Projects·People은 현재 연결된 화면이 없어
기존 기능 안내창을 연다. 모바일 탐색 시트도 같은 사이드바와 목록을 사용하며 선택 후 닫힌다.
좁은 높이에서 세로 스크롤과 키보드 포커스에 따른 자동 스크롤을 지원한다.

`SocietyDriveTests dashboardSidebarMatchesFigmaAndRoutesWorkspaceTargets`는 참조 프레임의
204×844 치수와 그룹 위치·행 크기, 실제 클릭과 Space, 좁은 높이에서의 포커스 접근,
동적 목록 제거를 검사한다. `SOCIETY_DASHBOARD_SIDEBAR_SCREENSHOT_PATH`로 이미지를 저장한다.
`dashboardSidebarOpensCalendarAndAccountMemberships`는 Calendar·Activity 본문 연결과
실제 계정 목록 선택·계정 해제 후 항목 제거를 검사한다.

2026-09-19에 `SocietyDriveTests`와 변경된 QML의 C++ 캐시·아이콘 리소스 빌드를 완료했다.
새 사이드바 검사 2개와 기존 전체 화면 상태·캘린더·파일 카드 검사 3개가 모두 통과했다
(QtTest 초기화·정리 포함 7 passed). 204×844 사이드바 캡처를 참조 프레임과 비교했으며,
전체 대시보드 캡처도 확인했다. 로그와 PNG는 `build/dashboard-sidebar/`에 있다.
이 검증은 소스 기반 UI와 빌드 대상에 대한 것이며 앱 번들 재패키징을 포함하지 않는다.
