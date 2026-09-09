# Society desktop dashboard

Society 데스크톱의 기본 화면은 [Figma 18:14](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=18-14)의 대시보드 디자인이다. 1440×900 프레임은 뷰 배치의 기준이며 운영체제 창을 재현하는 사양이 아니다. `Main.qml`의 `LV.ApplicationWindow`가 창과 컨트롤러·대화상자를 소유하고, `SocietyView.qml`을 공개 `content` 슬롯에 전달한다. 창의 프레임·닫기·최소화·최대화와 이동·크기 조절은 LVRS 기본 정책에 맡긴다. 앱은 창 플래그와 기본 드래그 영역을 덮어쓰거나 창 제어 버튼을 직접 그리지 않는다. 뷰의 상단은 LVRS가 제공하는 창 드래그 영역과 모바일 시스템 안전 영역 아래에 배치한다.

`SocietyView`는 56 px 상단 도구 모음, 220 px 사이드바, 24 px 콘텐츠 여백, QuickGenerate 입력과 두 줄의 Resource 목록을 조합한다. 세그먼트, 입력, 버튼, 목록, 메뉴, 안내창은 설치된 LVRS 컴포넌트이며 새로 그린 아이콘이나 버튼 배경은 없다. 배경은 LVRS의 창/목록 표면, 배치는 Item·GridLayout·ScrollView와 LVRS Stack을 사용한다. `SocietyView`에는 창 플래그나 창 제어 동작이 없으며 사용자 동작은 신호로 `Main.qml`에 전달한다.

- `Dashboard`는 기본 화면이다. `Storage`는 기존 Society 드라이브의 8개 영역, 폴더 탐색, 모델 가져오기, OS 드라이브 연결 화면이다. 두 화면을 유지하므로 탭 전환으로 현재 폴더, 프롬프트, 종횡비, 생성 수량을 초기화하지 않는다.
- `View all files`는 각각 Storage의 Files와 Generation History를 연다. 카드의 `Reveal`은 해당 파일의 부모 폴더를 Storage에서 열고, `Open`은 기존 DriveController의 경계 검사를 거쳐 연결된 앱을 실행한다. `More`도 같은 동작을 제공한다.
- 사이드바 `Local`은 Storage의 드라이브 루트로, `Deleted`는 기존 Deleted 영역으로 이동한다. OS 휴지통을 변경하거나 새 삭제 정책을 추가하지 않는다.
- `Browse`와 사이드바 `Cloud`/`This Mac → View`는 기존 계정의 네트워크 기기 탐색을 연다. `Environment`는 데스크톱 Preferences 창을 연다. `Ctrl+,`(macOS의 Command+,)도 유지한다.
- Search는 이름·Society 내부 경로를 대소문자 구분 없이 검색하고 Dashboard를 표시한다. 빈 입력은 전체 최근 목록으로 돌아간다.
- 탐색 항목은 실제 접근성 이름을 제공하며 라이브러리의 임시 상세 설명을 화면 읽기 프로그램에 노출하지 않는다.
- 760 px 미만에서는 사이드바를 숨기고, 카드 목록은 가용 폭에 따라 3/2/1열로 바뀐다. 작은 창에서는 도구 모음의 검색을 숨긴다. 모바일은 LVRS의 플랫폼 판정으로 기존 Storage 화면을 유지하며 호스트 모드는 제공하지 않는다.

`DashboardFiles`는 iiSocietyContainer의 유효한 드라이브와 영역 경계만 읽는다. QtConcurrent 작업 스레드에서 파일을 읽고 수정 시각 순으로 정렬한다. 최근 파일은 최대 3개, 생성 이력은 Generation History 바로 아래의 이미지 파일 최대 3개이다. 이력의 JSON과 이전 앱별 하위 폴더, 심볼릭 링크, 숨김 파일은 제외한다. 빈 경로·상대 경로는 작업 디렉터리를 열지 않는다. 컨테이너가 바뀌면 진행 중 조회를 취소하고 이전 결과를 폐기한다. Dashboard로 돌아오거나 창을 다시 활성화하거나 모델 가져오기를 마치면 갱신한다. 별도 인덱스·프롬프트·생성 요청 파일을 드라이브에 쓰지 않는다.

Figma의 예시 파일과 `Up to date` 상태는 제품 데이터에 하드코딩하지 않는다. 실제 파일의 이름·수정 시각·크기·경로와 `Available locally`를 표시하고, 데이터가 없으면 빈 상태를 표시한다. 네트워크에 연결된 경우에만 기기 상태를 `Online`으로 표시한다.

QuickGenerate는 [Dreamscapes Figma 15:218](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=15-218)에 맞춰 Prompt, Image, 종횡비, 생성 수량 선택과 Enter/Generate 제출을 제공한다. 수량의 기본값은 1이며 `1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 20, 25, 30, 40, 50, 100, 200, 500, 1000` 중 선택한다. 기존 LVRS 버튼·메뉴·메뉴 항목을 재사용하고 긴 수량 목록은 Qt Quick ListView로 스크롤한다. 메뉴를 창 높이 안으로 제한하며 다시 열면 선택 항목을 표시한다. 선택 수량은 Dashboard → SocietyView → Main의 `generateRequested(prompt, mediaType, aspectRatio, count)` 신호에 전달되고 탭 전환 후에도 유지된다. 현재 Society에는 생성 공급자가 연결되지 않았으므로 제출 시 LVRS 안내창으로 그 상태를 알리고 입력을 보존한다. Guild·Organization의 서비스 연결도 이번 화면 구현 범위에 포함되지 않으며 안내를 표시한다.

외부 패키지는 추가하지 않았다. 파일 조회에는 이미 사용하는 Qt 6.8.3의 Concurrent 모듈을 추가로 연결했다. 동일한 Qt 배포·라이선스·유지보수 범위이며 앱 자체 도메인인 카드 데이터만 C++로 구성한다.

검증: `Society.Drive`에서 기본 창 프레임 유지, `content` 슬롯의 뷰 소유 관계와 제목 표시줄 영역 분리, 파일 정렬·검색·이미지 이력·링크/외부 경계·빈 경로·취소 후 오래된 결과 폐기, 생성 수량 옵션·스크롤·요청 전달, 기본 Dashboard와 Storage 왕복 시 상태 보존, 기존 탐색·드롭·Preferences 동작을 검사한다. 빌드와 `Society_qmllint`, 실제 macOS 창의 화면 비교도 수행한다.
