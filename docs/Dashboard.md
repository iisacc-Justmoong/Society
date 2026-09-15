# Society desktop dashboard

Society 데스크톱의 기본 화면은 [Figma 18:14](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=18-14)의 대시보드 디자인이다. `Main.qml`의 `LV.ApplicationWindow`가 창과 컨트롤러·대화상자를 소유하고, `SocietyView.qml`을 공개 `content` 슬롯에 전달한다. 창의 프레임·닫기·최소화·최대화와 이동·크기 조절은 LVRS가 소유하며 앱이 창 제어 버튼을 직접 그리지 않는다.

상단 바는 [Figma 39:2296](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=39-2296)의 56 px 한 행이다. 데스크톱 콘텐츠를 창 위쪽에서 바로 시작하고, `nativeTitleBarHeight`를 상단 바 높이에 연결해 macOS의 실제 창 버튼과 탭·검색·계정 버튼의 중심을 맞춘다. `nativeTitleBarControlsRect` 오른쪽에 12 px 여백을 두어 탭과 창 버튼이 겹치지 않도록 한다. 전체 화면에서는 네이티브 버튼 예약 폭을 없애며, 복귀와 크기 변경은 LVRS가 처리한다. `windowDragHandleHeight`는 같은 행 전체를 덮고 `windowDragExclusionItems`로 탭·검색·계정 영역을 제외하여 클릭과 입력을 보존한다. 모바일은 같은 Dashboard·Tools·Storage 화면을 시스템 안전 영역 안에 표시하며, 좁은 화면의 내비게이션은 하단으로 이동한다. [모바일 레이아웃](MobileViews.md)을 참조한다.

대시보드 콘텐츠는 [Figma 62:2386](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=62-2386)의 카드 구성이다. `SocietyView`는 56 px 상단 도구 모음, 220 px 사이드바, 24 px 콘텐츠 여백, QuickGenerate 입력과 Recent files·Generate History의 가로 카드 목록을 조합한다. `LV.Card.File`의 Small/Brief를 140×160 논리 px로 배치하며 카드 간격은 8 px, 제목 행은 22 px, 제목과 카드 간격은 12 px, 섹션 간격은 24 px다. 세그먼트, 입력, 버튼, 카드, 메뉴, 안내창은 LVRS 컴포넌트이며 새 아이콘이나 카드 표면을 직접 그리지 않는다. 배경은 LVRS의 공유 창 표면을 그대로 사용하고, 배치는 Item·ListView·ScrollView와 LVRS Stack을 사용한다. `SocietyView`에는 창 플래그나 창 제어 동작이 없으며 사용자 동작은 신호로 `Main.qml`에 전달한다.

- 상단 세그먼트는 `Dashboard → Tools → Storage → Browse → Environment` 순서이다. `Dashboard`는 기본 화면이다. `Tools`는 [iiLocalDiffusion 모델 합·차 병합 도구](ModelMerge.md)를 열며 선택 상태를 표시한다. `Storage`는 기존 Society 드라이브의 9개 영역, 폴더 탐색, 모델 가져오기, OS 드라이브 연결 화면이다. 세 화면을 유지하므로 Tools를 포함한 탭 전환으로 병합 설정, 현재 폴더, 프롬프트, 종횡비, 생성 수량을 초기화하지 않는다.
- `View all files`는 각각 Storage의 Files와 Generation History를 연다. 카드 클릭과 키보드 활성화는 기존 DriveController의 경계 검사를 거쳐 파일을 연다. 우클릭 또는 호버·키보드 포커스 시 나타나는 카드 메뉴에서 `Open`과 `Reveal in Storage`를 선택할 수 있으며, 후자는 파일의 부모 폴더를 Storage에서 연다.
- Recent files의 `View all files`로 여는 Storage Files 목록은 수정 시각 오름차순으로 배치한다. 오래된 파일은 위쪽, 최신 파일은 아래쪽에 있고 최초 조회와 레이아웃 완료 후 최하단을 표시한다. Files의 하위 폴더에도 같은 동작을 적용하며 폴더를 먼저 표시한다. 자동 갱신 시 과거 파일을 탐색하던 위치와 선택을 보존하고, 최하단에 있으면 새 최신 파일을 따라간다.
- 사이드바 `Local`은 Storage의 드라이브 루트로, `Deleted`는 기존 Deleted 영역으로 이동한다. OS 휴지통을 변경하거나 새 삭제 정책을 추가하지 않는다.
- `Browse`와 사이드바 `Cloud`/`This Mac → View`는 기존 계정의 네트워크 기기 탐색을 연다. `Environment`는 데스크톱 Preferences 창을 열고 모바일에서는 같은 설정 내용을 시트에 표시한다. `Ctrl+,`(macOS의 Command+,)도 유지한다.
- Search는 이름·Society 내부 경로를 대소문자 구분 없이 검색하고 Dashboard를 표시한다. 빈 입력은 전체 최근 목록으로 돌아간다.
- 탐색 항목은 실제 접근성 이름을 제공하며 라이브러리의 임시 상세 설명을 화면 읽기 프로그램에 노출하지 않는다.
- 760 px 미만에서는 사이드바를 숨기고, 카드는 크기를 유지하며 각 목록 안에서 가로 스크롤한다. 목록은 영역 밖을 클리핑하고 방향키·Home/End 탐색을 지원한다. 700 px 미만에서는 도구 모음의 검색을 숨기며, 탭의 가용 폭이 부족하면 가로 스크롤을 제공하여 창 버튼과 계정 버튼을 가리지 않는다. 모바일도 Dashboard로 시작하고 같은 데이터 모델로 검색한다. 760 px 미만에서는 5개 탭을 하단에, 검색·계정·탐색 패널 버튼을 상단에 표시한다. 숨겨진 사이드바는 탐색 시트에서 사용할 수 있으며 호스트 모드는 제공하지 않는다.

`DashboardFiles`는 iiSocietyContainer의 유효한 드라이브와 영역 경계만 읽는다. QtConcurrent 작업 스레드에서 파일을 읽고 수정 시각 순으로 정렬한다. Figma의 각 목록에 맞춰 최근 파일은 최대 6개, 생성 이력은 Generation History 바로 아래의 이미지 파일 최대 11개이다. 검색은 표시 개수 제한 전에 전체 스냅샷에 적용한다. 이력의 JSON과 이전 앱별 하위 폴더, 심볼릭 링크, 숨김 파일은 제외한다. 빈 경로·상대 경로는 작업 디렉터리를 열지 않는다. 컨테이너가 바뀌면 진행 중 조회를 취소하고 이전 결과를 폐기한다. Dashboard로 돌아오거나 창을 다시 활성화하거나 모델 가져오기를 마치면 갱신한다. 별도 인덱스·프롬프트·생성 요청 파일을 드라이브에 쓰지 않는다.

카드에는 실제 파일명과 로컬 수정 날짜(`yyyy-MM-dd`)를 표시한다. 이미지 파일은 경계를 확인한 로컬 파일 URL을 `LV.Card.previewSource`에 전달하며 LVRS가 카드 전체에 맞춰 중앙 크롭한다. 비이미지 파일과 읽을 수 없는 이미지는 LVRS의 기본 미리보기 아이콘을 사용한다. Figma의 예시 사진·파일명·날짜를 제품 데이터에 하드코딩하지 않으며, 데이터가 없으면 빈 상태를 표시한다. 네트워크에 연결된 경우에만 기기 상태를 `Online`으로 표시한다.

QuickGenerate는 [Dreamscapes Figma 15:218](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=15-218)에 맞춰 Prompt, Image, 종횡비, 생성 수량 선택과 Enter/Generate 제출을 제공한다. 수량의 기본값은 1이며 `1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 20, 25, 30, 40, 50, 100, 200, 500, 1000` 중 선택한다. 기존 LVRS 버튼·메뉴·메뉴 항목을 재사용하고 긴 수량 목록은 Qt Quick ListView로 스크롤한다. 메뉴를 창 높이 안으로 제한하며 다시 열면 선택 항목을 표시한다. 선택 수량은 Dashboard → SocietyView → Main의 `generateRequested(prompt, mediaType, aspectRatio, count)` 신호에 전달되고 탭 전환 후에도 유지된다. 현재 Society에는 생성 공급자가 연결되지 않았으므로 제출 시 LVRS 안내창으로 그 상태를 알리고 입력을 보존한다. Guild·Organization의 서비스 연결도 이번 화면 구현 범위에 포함되지 않으며 안내를 표시한다.

외부 패키지는 추가하지 않았다. 파일 조회에는 이미 사용하는 Qt 6.8.3의 Concurrent 모듈을 추가로 연결했다. 동일한 Qt 배포·라이선스·유지보수 범위이며 앱 자체 도메인인 카드 데이터만 C++로 구성한다.

카드 회귀 검증은 `SocietyDriveTests dashboardCardRowsMatchFigmaAndRemainInteractive`로 실행한다. 1440/760/390 px 폭에서 Figma 좌표·140×160 크기·8 px 간격, 이미지 크롭, 날짜, 가로 스크롤, 카드 클릭·Space·Enter, 메뉴의 열기·부모 폴더 이동, 빈 검색 결과를 확인한다. `SOCIETY_DASHBOARD_CARDS_SCREENSHOT_PATH`로 캡처 위치를 지정할 수 있고, 선택적인 `SOCIETY_DASHBOARD_CARDS_PREVIEW_PATH`는 테스트용 참조 사진에만 사용한다.

Storage 시간순 탐색은 `SocietyDriveTests viewAllRecentFilesOpensStorageAtTheNewest`에서 실제 버튼 클릭, 파일 이름과 반대인 수정 시각 순서, 초기 최하단 위치를 검사한다. `SOCIETY_RECENT_STORAGE_SCREENSHOT_PATH`로 해당 화면을 캡처할 수 있다. `SocietyGuiTests chronologicalFilesOpenAtTheNewestAndPreserveBrowsing`는 새 파일 추가·삭제와 수정 시각 변경 후 선택·스크롤 복원, 최하단 추적, 빈 폴더 왕복과 이름순 복귀를 검사한다. 새 파일 도착 후 비동기 조회와 예약된 위치 복원이 끝난 것을 확인한 다음 과거 파일 탐색을 재현하여 이전 레이아웃의 `atYEnd` 값으로 검사가 앞서 진행되지 않도록 한다.

검증: `Society.Drive`에서 기본 창 프레임 유지, `content` 슬롯의 뷰 소유 관계, 상단 바의 y=0·높이 56 px·컨트롤 중심선, 1440/760/360 px 폭에서 Tools·Storage 탭 클릭과 검색 입력의 드래그 영역 충돌 방지, Dashboard·Tools·Storage의 순서와 화면 전환·접근성 선택 상태, 파일 정렬·검색·이미지 이력·링크/외부 경계·빈 경로·취소 후 오래된 결과 폐기, 생성 수량 옵션·스크롤·요청 전달, 세 화면 왕복 시 상태 보존, 기존 탐색·드롭·Preferences 동작을 검사한다. 빌드와 `Society_qmllint`, 실제 macOS 창의 화면 비교도 수행한다.

모바일 회귀 검증은 `SocietyDriveTests mobileViewsShareDesktopContentAndKeepState mobileStorageKeepsSectionsActionsAndGalleryReachable`로 실행한다. 실제 Main을 모바일 레이아웃으로 열어 320×568, 390×844, 844×390, 1024×768에서 탭 전환·터치·검색·상태 보존·작업 시트·Models 뒤로 이동과 갤러리의 가용 높이를 확인한다. `SOCIETY_MOBILE_SCREENSHOT_DIRECTORY`는 이 테스트의 화면 캡처 디렉터리이다. 이 검증은 호스트 Qt에서 모바일 레이아웃을 실행하며, 실기기의 안전 영역·키보드·사진 권한 검증과 구분한다.
