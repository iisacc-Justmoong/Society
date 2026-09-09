# Society

iiAccountManager SDK가 소유하는 로그인·회원가입 화면을 데스크톱·iPhone·Android에서 호출한다.
공통 계정 화면에서 iisacc.com 이메일과 비밀번호로 바로 로그인한다. 전체 계정 객체와 기기 세션을 같은 응답으로 받는다. [계정 연결 계약](docs/Account.md)을 참고한다.

데스크톱 **Devices → Pair iPhone**의 QR을 iPhone Society의 **Devices → Pair desktop → Scan QR code**로 읽으면 같은 계정의 호스트와 연결된다. 실제 Files 접근 확인 후 양쪽에 완료를 표시한다. [페어링 절차·서버 계약·검증 범위](docs/Pairing.md)를 참고한다.

## iisacc 계정

모든 Society 플랫폼에 공통 LVRS 계정 화면을 제공한다. 데스크톱 상단 계정 아이콘 또는 모바일의
Sign in 버튼에서 이메일·비밀번호만으로 바로 로그인한다. 계정 패널·Devices·iiSocietyHelper는
같은 iiAccountManager 객체를 공유한다. PC 2대·태블릿 2대·휴대폰 2대의 독립 한도를 사용하며,
로그인에는 릴레이 주소가 필요하지 않다. [인증 흐름·기기 식별·검증 범위](docs/Account.md)를 참고한다.

데스크톱은 [Figma 대시보드](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=18-14)를 `LV.ApplicationWindow`의 `content` 슬롯에 `SocietyView`로 배치하며, LVRS 기본 창 프레임과 창 제어를 사용한다.
상단 **Storage**에서 기존 Society 드라이브를 열며, 탭을 오가도 현재 폴더와 프롬프트가 유지된다.
실제 최근 파일·생성 이력, 화면 구성과 동작 범위는 [Dashboard 문서](docs/Dashboard.md)에 정리했다.

앱 제목·드라이브 홈·경로 표시와 OS의 드라이브 표시 이름은 `Society`이다. iiSocietyContainer 0.9.1은 이전 이름의 기존 드라이브도 UUID와 파일을 유지하며 읽는다. macOS에서 기존 원본을 다시 등록하면 동일한 File Provider 도메인의 표시 이름을 갱신한다. `Society.Drive`가 앱 제목, 홈과 경로 표시를 검증한다.

컨테이너 원본으로 Finder의 `~/Library/CloudStorage/` 복제본이나 기존 Society 컨테이너 내부를 선택하면 오류를 표시하고 기존 원본과 공통 저장 설정을 유지한다. 이 검증은 iiSocietyContainer의 생성·열기 경계에 적용되어 Helper 소비 앱에도 전달된다. `Society.Drive`의 `rejectsPublicFilesAsANewContainer`가 `Files/`를 잘못 선택해도 새 영역이나 매니페스트가 만들어지지 않는지 검증한다.

iiSocietyHelper 0.4.0의 `societyHelper.fileSystem`은 Society 원본의 8개 영역을 일반 파일 시스템 경로로 제공한다. SDK가 iiSocietyContainer 0.8.0을 재사용하므로 iOS 빌드 도구는 Container 설치 후 Helper를 구성하고 동일 ABI의 Container 패키지를 명시한다. `Society.IosBuildContract`는 이 순서와 패키지 경로를 검증한다. 파일 접근 자체와 iOS 서명·실기기 권한 검증은 구분한다.

[SocietyDaemon](docs/SocietyDaemon.md)은 본체 창과 독립적으로 Helper의 데이터를 수신·보관하는 서비스이다. 본체의 `societyInbox` 객체가 재실행 후 누적 데이터와 새 데이터를 전달받는다. macOS 로그인 서비스 등록, 저장 보장과 플랫폼 범위는 별도 문서를 따른다.

앱 시작 시 iiSocietyHelper 0.3.1의 `Helper`를 `com.iisacc.society`로 가동한다. 같은 기기의 다른 Helper와 양방향으로 실행 인스턴스를 관측하고, 전경·배경 상태를 공유하며 앱 종료 때 자기 등록을 해제한다. QML에는 `societyHelper.observedApplications`와 관측 이벤트를 제공한다. 이 상태는 Society 드라이브 콘텐츠와 분리된 기기 로컬 위치에 기록한다. `SocietyRuntime`이 시작 실패를 재시도하고 iOS 중단 때 연결을 정리한 뒤 복귀 시 Helper·수신기·수신함을 함께 복구한다. 상세 기능과 검증 범위는 [iOS 구현 문서](docs/iOS.md)를 따른다.

`Society.Dependencies`는 실제 앱 실행 파일과 테스트 Helper의 양방향 발견을 검증한다. `SOCIETY_HELPER_DIRECTORY`와 저장소 설정을 테스트별 `build/` 경로로 격리한다. SDK 0.3.1 설치 경로는 `iiSocietyHelper_DIR`로 지정한다. 현재 Workspace 검증본은 `SDK/iiSocietyHelper/build/install/lib/cmake/iiSocietyHelper`이다.

Society는 iisacc 앱들의 공통 원본 스토리지이다. 컨테이너를 열 때 iiSocietyContainer 0.7.0의 `SharedStorage::setDefaultContainer()`로 경로와 UUID를 등록한다. 다음 실행은 등록된 컨테이너를 다시 열며, Dreamscapes 등의 소비 앱은 `SharedStorage::open()`으로 같은 `Models/`·`Asset Library/`·`Generation History/`에 접근한다. Society에 드롭한 모델을 앱별로 복제할 필요가 없다. 시스템 드라이브의 루트는 계속 `Files/`이며, 나머지 영역은 iisacc 앱의 SDK 경로로 접근한다. iOS에서는 동일 App Group이 공통 원본이고 다른 앱이 File Provider를 중복 등록하지 않는다. 이는 로컬 공유 계약이며 원격 계정 동기화를 추가하지 않는다.

`Society.Drive` 테스트는 컨테이너 선택 후 공통 저장소의 UUID가 일치하고 새 컨트롤러가 같은 드라이브를 다시 여는지 검사한다. 설정 파일은 테스트별 `build/` 임시 경로로 분리한다.

Qt Quick와 LVRS를 사용하는 Society 드라이브 탐색 앱이다. 폴더 경로를
받아 `iiSocietyContainer`의 영속 드라이브로 열고 8개 논리 영역을 분리해 보여준다.
기본 창 크기는 1120 × 720,
최소 크기는 360 × 320이다.

## 컨테이너 탐색

`Choose folder…`에서 기존 폴더를 선택하거나 실행 시 `--container <절대 경로>`를
전달한다. 처음 열 때 SDK가 드라이브 ID와 8개 영역 디렉터리를 구성한다.
기존 파일은 보존하며 영역 이름과 충돌하는 파일이나 잘못된 매니페스트가 있으면
오류를 표시한다. 다시 열어도 드라이브 ID는 유지된다.

시작 화면에는 Asset Library, Deleted, Files, Forked, Generation History,
Models, Published, Thinking Space가 표시된다. 영역을 클릭하면 파일 그리드로
이동한다. 넓은 창에는 영역 사이드바도 표시한다. 폴더 더블클릭·Enter, `Up`,
경로 버튼, 드라이브 루트 버튼으로 이동하고, 파일은 기본 연결 앱으로 연다.
컨테이너 밖이나 미분류 루트 폴더로의 탐색은 거부한다.

Generation History는 모든 앱의 완성된 생성 이미지를 한 목록으로 보여준다.
앱별·작업별 폴더를 만들지 않고 이미지 파일을 영역 바로 아래에 자동 저장한다.
이 화면은 PNG·JPEG·WebP만 표시하며 하위 폴더 탐색을 허용하지 않는다.
생성 이미지는 Asset이 아니므로 생성만으로 Asset Library에 추가하지 않는다.
큐와 실행 상태는 각 앱의 메모리로 관리하며 Society에 저장하지 않는다.
생성기가 파일 경로를 요구하는 작업 자료와 캐시도 Society 밖의 앱 전용 임시 위치에서 사용한 뒤 정리한다.
Society에 영구 저장하는 생성 데이터는 완성된 이미지 파일뿐이다.
`Society.Drive`는 데스크톱과 휴대폰 폭에서 이미지 필터와 하위 폴더 탐색 거부를 검증한다.
경로와 필터가 바뀌면 FolderListModel을 함께 재생성하여 이전 영역의 비동기 목록이 남지 않게 한다.
`Society.Gui`는 이미지 목록과 일반 폴더 목록 사이의 반복 전환도 검사한다.
그 외 영역에는 별도 휴지통·게시·보관 정책을 부여하지 않는다.

macOS에서는 `Connect to Finder`로 네이티브 File Provider를 등록하고
`Open in Finder`로 시스템 드라이브를 연다. 첫 연결에서 Finder가 `Enable`을
표시하면 이를 통해 활성화한다. 앱은 원본 폴더를 탐색하고 Finder는 OS가
관리하는 `Files/` 복제본을 제공한다. Finder에서 디스크를 클릭하면 `Files/`의
내용이 바로 보이고, 루트에 저장한 파일은 원본의 `Files/` 안에 생성된다.
나머지 7개 영역은 시스템 드라이브에서 제외하고 Society 앱의 영역 탐색으로 제공한다.
Files의 내용과 이동·생성·삭제는 양방향으로 반영된다. 앱에서 Files 밖으로 옮긴 항목은
시스템 드라이브에서 사라진다. 원본 디렉터리 자체의 파일 권한은 변경하지 않는다.
연결 상태와 오류를 화면 하단에 표시한다. 네이티브 어댑터는 iiSocietyContainer
설치 패키지에서 가져오며 프로세스 실행은 비동기이고 제한 시간과 오류를 처리한다.
원격 클라우드 동기화는 아직 연결하지 않았다. macOS와 iOS 이외의 운영체제에서는
앱 탐색만 제공한다.

## 모델 파일 드래그 앤 드롭

컨테이너를 연 뒤 Society 창 안에 `.safetensor` 또는 `.safetensors` 파일을 드롭하면
현재 탐색 영역과 관계없이 원본 컨테이너의 `Models/`로 복사한다. 확장자는 대소문자를
구분하지 않는다. 드래그 중 목적지를 표시하고, 완료 후 `Models` 화면으로 이동한다.
`Files/Models/`나 Finder·파일 앱의 공개 드라이브에는 모델을 추가하지 않는다.

- 원본 파일을 보존한다. `weights.safetensor`가 이미 있으면 `weights (1).safetensor`처럼
  사용하지 않는 이름을 선택하며, 기존 파일·폴더·링크를 덮어쓰지 않는다.
- 여러 모델을 한 번에 가져올 수 있다. 같은 드롭에 반복된 경로는 한 번만 처리하고,
  이미 해당 컨테이너의 `Models/` 아래에 있는 파일은 다시 복사하지 않는다.
- 다른 확장자나 웹 URL이 섞인 드롭은 전체를 거부한다. 실제 읽기·쓰기 오류가 발생하면
  실패한 파일을 표시하며 이미 완료한 모델은 유지한다. 폴더와 심볼릭 링크는 가져오지 않는다.
- 큰 파일은 작업 스레드에서 1 MiB 버퍼로 복사한다. 준비 상태·파일별 진행률을 반영한
  전체 진행률·결과·오류를 창 하단에 표시한다. `Cancel`은 남은 작업을 중단하고 미완료
  임시 파일을 정리한다. 복사 중에도 영역을 탐색할 수 있으며 컨테이너 선택 버튼은 잠근다.
- 임시 파일은 비공개 `Models/`에 만들고 완전히 기록한 뒤 원자적으로 이름을 바꾼다.
  복사 전후에 드라이브 ID와 영역 경계를 검사한다. 잘못된 컨테이너나 `Models`를
  `Files`로 연결한 심볼릭 링크에는 기록하지 않는다.

첫 분류 규칙은 위 두 확장자만 대상으로 한다. 모델의 텐서 헤더, 내부 데이터 유효성,
추론 엔진 호환성은 판정하지 않으며 모델을 실행하지 않는다. `iiSocietyContainer`의
8개 논리 영역 계약은 그대로 사용하고 앱의 `ModelImporter`가 가져오기 동작을 담당한다.

데스크톱 입력은 기존 Qt Quick의
[DropArea](https://doc.qt.io/qt-6.8/qml-qtquick-droparea.html)와
[복사 드롭 액션](https://doc.qt.io/qt-6.8/qml-qtquick-dragevent.html)을 사용한다.
iOS/iPadOS는 Qt 창의 네이티브 뷰에
[UIDropInteraction](https://developer.apple.com/documentation/uikit/uidropinteraction)을 붙이고
파일 앱의 `NSItemProvider`를 받는다. 제공자의 파일 접근은
[loadInPlaceFileRepresentation](https://developer.apple.com/documentation/foundation/nsitemprovider/loadinplacefilerepresentation(fortypeidentifier:completionhandler:))과
`NSFileCoordinator`로 유지하며, 그 접근 범위 안에서 같은 모델 복사 로직을 실행한다.
별도 임시 복사본을 앱에 중복 생성하지 않는다. 취소 후 늦게 도착하는 제공자 콜백은 무시한다.
외부 패키지는 추가하지 않고 기존 Qt 라이선스와 Apple 기본 SDK API를 사용한다.

## 파일 그리드

`App/FileGridView.qml`은 필수 `string path` 인자로 로컬 폴더의 절대 경로를 받는다.
컨테이너를 선택하지 않았거나 드라이브 루트에 있을 때는 빈 경로를 전달하고
파일 목록을 숨긴다. 영역을 열면 해당 실제 디렉터리를 전달한다. 독립 컴포넌트로도 사용할 수 있다.

```qml
FileGridView {
    anchors.fill: parent
    path: ""
}
```

빈 문자열은 미지정 상태이며 파일 목록 모델을 생성하거나 현재 작업 폴더를
열지 않는다. 독립 컴포넌트에서는 `No folder selected`와 0개 항목을 표시한다.
다른 폴더를 표시하려면 `path`에 해당 절대 경로를 전달한다. URL, 상대 경로,
`~` 확장은 받지 않는다. 공백·한글·`#`·`%`가 포함된 경로는 `QUrl::fromLocalFile()`로
변환한다. 존재하지 않는 폴더, 파일 경로, 읽을 수 없는 폴더는 오류 상태로 표시한다.

- 폴더 우선, 대소문자를 구분하지 않는 이름순 정렬이며 숨김 항목과 `.`/`..`는 제외한다.
- PNG/JPEG/WebP/GIF/BMP/SVG는 Qt 이미지 로더로 비동기 미리보기를 표시한다.
  그 외 파일과 로드에 실패한 이미지는 LVRS 파일 아이콘, 폴더는 LVRS 폴더 아이콘을 사용한다.
  영상·PDF·문서의 내용 미리보기는 아직 제공하지 않는다.
- 클릭으로 단일 선택, 방향키로 이동, Escape로 선택 해제를 지원한다.
  더블클릭 또는 Enter는 `activated(string path, bool isDirectory)` 신호를 내보낸다.
  앱은 이 신호를 DriveController의 경계 검증과 폴더 탐색·파일 열기에 연결한다.
- `count`와 `selectedPath`는 읽기 전용이다. `path` 변경 시 선택과 스크롤을 초기화한다.
- `heading`은 표시 제목이며 기본값은 `Files`, 앱에서는 현재 영역 이름을 전달한다.
- 긴 파일명은 최대 두 줄, 그리드는 세로 스크롤과 창 크기에 따른 열 재배치를 사용한다.

## 의존성

- CMake 3.31 이상, Ninja, C++20 컴파일러
- Qt 6.8.3: Quick, QuickControls2, Qt.labs.folderlistmodel, 테스트용 Test 모듈
- 설치된 LVRS CMake 패키지와 QML 모듈
- iiSocietyContainer 0.9.1 이상; macOS 네이티브 연결은 서명된 어댑터와 macOS 15 이상

기존 Qt/LVRS의 창, 글꼴, 테마, 앱 부트스트랩을 재사용한다.
파일 열거·정렬·폴더 변경 감지는 Qt에 포함된
[FolderListModel](https://doc.qt.io/qt-6.8/qml-qt-labs-folderlistmodel-folderlistmodel.html)을
사용한다. 이미 설치된 Qt Quick 모듈이므로 추가 라이브러리나 서비스는 도입하지 않는다.
Qt가 제공하는 구현과 기존 Qt 라이선스 범위를 재사용하며,
[Qt Quick의 라이선스 안내](https://doc.qt.io/qt-6.8/qtquick-index.html#licenses-and-attributions)를 따른다.
`Qt.labs` API는 향후 호환성이 보장되지 않으므로 현재 Qt 6.8.3 고정을 유지하고,
버전 변경 시 그리드 통합 테스트로 확인한다. `DirectoryLocation`은 경로 검증만 담당한다.
드라이브 ID·영역 배치·경로 판정은 iiSocietyContainer에 위임한다.
앱은 기존 Qt의 QProcess, QDesktopServices, FolderDialog를 사용하므로 추가 의존성은 도입하지 않는다.
현재 macOS 프리셋은 Qt `/Volumes/Storage/Qt/6.8.3/macos`와
LVRS `~/.local/SDK/LVRS` 설치본을 사용한다.
실행 시 LVRS 패키지가 제공하는 라이브러리 경로와 명시적인 QML import 경로를 사용하므로
별도의 `DYLD_LIBRARY_PATH` 또는 `QML2_IMPORT_PATH` 설정은 필요하지 않다.

데스크톱에서는 다음 13개 SDK를 `find_package(... CONFIG REQUIRED)`로 찾고 해당 SDK의
`패키지명::패키지명` CMake 타깃을 `Society`에 직접 링크한다.
기본 검색 경로는 `~/.local/SDK/<패키지명>`이며 하나라도 누락되면 구성이 실패한다.

| 필수 SDK | 소비자 테스트에서 확인하는 공개 API |
| --- | --- |
| iiFileProvider | `helloWorld()` |
| iiGeneralDocument | 문서 생성과 메타데이터 접근 |
| iiLicenseManager | `LicenseClient` 메타 객체와 상태 열거형 |
| iiLocalDiffusion | 연산 장치 이름 조회 |
| iiLocalLLM | `helloWorld()` |
| iiServerHost | `helloWorld()` |
| iiSharedCanvas | 벡터 에셋 ID와 종류 조회 |
| iiSocietyContainer | `helloWorld()`, `SocietyDrive` 생성·영역·영속 ID |
| iiSocietyHelper | `Helper` 실제 앱 상호 관측과 기존 `helloWorld()` 호환성 |
| iiSocietySync | `helloWorld()` |
| iiUpdateManager | `UpdateManager` 메타 객체와 상태 열거형 |
| iiVoiceOver | `helloWorld()` |
| iiWhatsNew | `helloWorld()` |

요청의 `iiLisenceManager`는 실제 설치된 이름인 `iiLicenseManager`로 연결한다.
일부 SDK가 Qt 6.8.3을 정확히 요구하므로 Society도 같은 Qt 버전을 사용한다.
`iiXml`, `iiHtmlBlock`, `iiPaintEngine` 등 간접 의존성은 각 SDK 패키지가 선언한다.

파일 그리드는 Qt의 로컬 파일 목록과 이미지 로더를 사용한다.
SDK의 추론·서버·동기화 기능은 앱에서 실행하지 않는다.
iiSocietyContainer의 새 드라이브 API는 앱과 `Society.Drive` 테스트에서 사용한다.

## 빌드 및 실행

프로젝트 디렉터리에서 실행한다. 모든 빌드 산출물은 `build/`에 생성한다.

```sh
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug
open build/bin/Society.app
# 준비된 원본 경로를 지정하여 직접 실행할 수도 있다.
build/bin/Society.app/Contents/MacOS/Society --container "/absolute/path/to/container"
```

작업 공간 안에 설치한 SDK를 사용하려면 다음처럼 패키지 경로를 지정한다.

```sh
cmake --preset debug \
  -DiiSocietyContainer_DIR=/Volumes/Storage/Workspace/SDK/iiSocietyContainer/build/install/lib/cmake/iiSocietyContainer
```

iiSocietyContainer의 macOS 빌드는 Swift Command Line Tools와 Python 3이 필요하다.
네이티브 어댑터의 서명·설치·연결 해제는 해당 SDK의 `platform/macos/README.md`를 따른다.

CLion에서도 CMake 빌드 디렉터리를 `build/`로 지정했다. 실행 대상은 `Society`이다.
다른 설치 경로에서는 `cmake --preset debug -DQt6_DIR="<Qt>/lib/cmake/Qt6" -DLVRS_DIR="<LVRS>"`로
프리셋 값을 덮어쓸 수 있다.

## 구조와 검증

- `main.cpp`: LVRS 런타임 초기화와 `Society.Main` QML 로드
- `App/Main.qml`: LVRS 창, 폴더 선택, 8개 영역, 사이드바, 경로 탐색, Finder 연결 UI
- `App/FileGridView.qml`: 파일 목록·미리보기·선택·빈 상태 UI
- `App/Files/DirectoryLocation.h/.cpp`: 로컬 절대 경로 검증과 파일 URL 변환
- `App/Files/ModelImporter.h/.cpp`: 모델 분류, 비동기 복사, 중복 이름 처리와 진행 상태
- `App/Files/ModelImportSource.h`: 로컬 파일과 Apple 제공자가 복사 중 읽기 접근을 유지하는 계약
- `App/Files/AppleModelSource.h/.mm`: 보안 범위와 파일 조정을 유지하는 Apple 파일 제공자 입력
- `App/Files/IosModelDrop.h/.mm`: iOS 창 전체의 네이티브 드롭과 목적지 피드백
- `App/Drive/DriveController.h/.cpp`: SDK 드라이브 로드, 영역 경계 내 탐색, 네이티브 연결 상태
- `App/AI/`: Civitai·Hugging Face·Ollama 연동 클래스의 빈 초기 골격
- `tests/tst_filegrid.cpp`: 빈 경로, 잘못된 경로, 실제 임시 파일의 정렬·이미지 미리보기,
  클릭·더블클릭·키보드 이동과 스크롤, 세 가지 창 크기, 경로 변경, QML 경고 검사
- `tests/FileGridHarness.qml`: 드라이브 화면과 독립적으로 파일 그리드를 검증하는 LVRS 창
- `tests/tst_drive.cpp`: 영속 ID, 전체 8개 영역의 앱 내부 파일 탐색 유지, 경계 이탈 거부, 충돌 시 기존 컨테이너 보존,
  실제 드라이브 화면의 영역 클릭·폴더 이동·상위 이동·작은 창 레이아웃·QML 경고 검사,
  창의 URL 드롭·복사 액션·영역 자동 배치·모델 목록 갱신 검사
- `tests/tst_modelimporter.cpp`: 두 모델 확장자, 원본 보존, 이름 충돌, 중복 입력,
  동시 가져오기의 덮어쓰기 방지, 잘못된 입력, 영역 리다이렉트 거부, 취소와 임시 파일 정리
- `tests/tst_applemodelsource.mm`: 실제 Foundation 파일 제공자를 통한 복사와 지연 콜백 취소 검사
- `Society.IosDropSyntax`: 호스트 SDK에 UIKit 헤더가 있으면 Mac Catalyst 대상으로
  iOS 드롭 델리게이트의 API·타입을 컴파일 검사한다. iOS 앱 링크·실행 검사는 아니다.
- `tests/tst_dependencies.cpp`: 실제 앱의 링크 설정을 공유하여 13개 SDK의 헤더·심볼·실행 시 로드를 검사

AI 클래스의 소스와 헤더는 `SocietyDependencyTests`에 등록되어 빌드에 포함된다.
현재 클래스에는 API나 실행 동작이 없고 앱과 연결되어 있지 않으므로 별도의 기능
단언은 추가하지 않는다. 기존 의존성·GUI 테스트와 전체 빌드로 이번 골격 추가를 검증한다.

`ctest --preset debug`는 화면 없는 소프트웨어 렌더링 환경에서 GUI 동작을 검사한다.
파일 그리드 테스트의 임시 폴더는 `build/` 아래에 만들고 실행 후 제거한다.
`Society.Dependencies` 테스트는 13개 SDK의 공개 심볼을 호출한다. 추론 모델 다운로드,
네트워크 요청, 라이선스 활성화, 업데이트 설치는 수행하지 않는다.
`cmake --build build --target Society_qmllint`로 QML 정적 검사도 실행할 수 있다.
로컬 실행용 앱은 설치된 Qt/LVRS를 사용한다. 다른 컴퓨터에 배포하려면 별도 패키징이 필요하다.

## iOS / iPadOS

iOS 16 이상에서는 앱을 열 때 공유 App Group의 Society를 자동으로 열고 파일 앱에 등록한다. 앱의 홈에는 8개 영역을 모두 유지한다. 파일 앱에서 Society를 열면 `Files/`의 내용이 바로 보이며 나머지 7개 영역은 노출하지 않는다. iOS의 `Open in Files`는 이 공개 루트에서 시작하는 시스템 문서 탐색기를 연다.

`ios-device`, `ios-simulator` CMake preset과 내장 `SocietyFileProvider.appex`를 사용한다. 전체 Xcode 16 이상, Qt 6.8.3 iOS, LVRS·iiSocietyContainer·iiSocietyHelper의 해당 iOS 대상 패키지가 필요하다. `python3 -B tools/build_ios.py --platform ios-simulator`가 SDK 빌드·설치부터 앱과 확장 빌드까지 수행한다. 기기 패키지는 Workspace의 `build/ios-device/install`, 시뮬레이터 패키지는 `build/ios-simulator/install`을 사용한다. iOS 구성에서는 누락된 패키지를 데스크톱 설치로 대체하지 않는다.

```sh
cmake --preset ios-device -DSOCIETY_IOS_TEAM=<development-team-id>
cmake --build --preset ios-device
```

앱과 확장의 기본 App Group은 `group.com.iisacc.society`이며 `SOCIETY_IOS_APP_GROUP`으로 설정한다. 앱 ID와 확장 ID는 각각 `com.iisacc.society`, `com.iisacc.society.fileprovider`이다. 두 서명 프로필에 같은 App Group 접근 권한이 필요하다. 원본은 그룹의 `Library/Application Support/Society`에 보관하며 앱 Documents 전체 공유를 켜지 않는다. 상세 저장 계약과 SDK 빌드 절차는 [iiSocietyContainer iOS 문서](../../SDK/iiSocietyContainer/platform/ios/README.md)를 따른다.

호스트 GUI 테스트는 휴대폰 세로·가로 크기에서도 8개 영역 탐색과 시스템별 연결 문구를 검증한다. 이 테스트와 공통 File Provider 저장소 테스트는 실제 iOS 빌드·기기 실행 결과와 구분한다.
모델 가져오기도 같은 구분을 따른다. macOS에서 Foundation 제공자·파일 조정 경로를
`Import models…`는 iOS 파일 선택기를 열며 `.safetensor`·`.safetensors`를 원본 접근 범위 안에서 Models로 복사한다. 파일·폴더는 한 번 탭하여 열고, 지원되는 문서·이미지는 QuickLook으로 표시한다. 복사에는 유한한 백그라운드 실행 시간을 요청하며 만료 시 취소한다. 상세 동작과 현재 iOS SDK 미설치로 남아 있는 실제 빌드·기기 검증은 [iOS 구현 문서](docs/iOS.md)에 기록한다.

## Windows · Linux · Android 드라이브

Society는 iiSocietyContainer 0.9와 iiSocietyHelper 0.5를 사용한다. Windows에서는 Dokan 2 기반 드라이브 문자, Linux에서는 libfuse 3 마운트, Android에서는 시스템 문서 제공자를 연결한다. 시스템에서 드라이브를 열면 Files 내용이 바로 보이고 앱 내부에는 기존 8개 영역이 유지된다. Windows/Linux 어댑터는 별도 사용자 세션 프로세스여서 Society 창을 닫아도 드라이브가 유지되고 로그인 때 저장된 등록을 복원한다. Windows에는 서명된 Dokan 2 드라이버와 DLL, Linux에는 fuse3와 /dev/fuse 접근이 필요하다.

Android는 앱 전용 저장소를 자동으로 준비한다. 다른 iisacc 앱에는 동일 서명을 확인한 내부 ContentProvider를 제공한다. 외부 파일 선택기의 content URI를 직접 읽어 .safetensor/.safetensors를 Models에 가져온다. LVRS는 Android 시스템 막대·화면 잘림의 실제 WindowInsets를 적용하여 하단 Open in Files 버튼을 탐색 막대 밖에 배치한다.

```sh
python3 tools/build_android.py --qt <Qt-6.8.3-Android-ABI> --qt-host <Qt-6.8.3-host> \
  --sdk <Android-SDK> --ndk <NDK-r27c> --lvrs <same-ABI-LVRS-prefix> --package
```

빌드 순서는 Container 설치 → Helper 설치 → Society APK이다. 출력과 SDK 설치는 `build/android/` 아래에 둔다. Android와 Windows/Linux는 현재 앱이 실제로 호출하는 Container·Helper만 의존하며 macOS의 기존 전체 SDK 호환성 검사는 유지한다. 디바이스별 서명 APK 배포는 별도 단계이다.

회귀 검사는 SDK의 `files_view`, `mount_service`, `tests/native_mount.py`, `tests/android/run_device.py` 및 Helper의 `tests/android/`에 있다. 실제 플랫폼 실행 결과는 빌드 로그와 함께 기록하며 교차 컴파일과 실제 OS 드라이브 검증을 구분한다.
## iOS Files 실행 회귀 검사

실제 iOS 시스템 드라이브 검사는 정상 iOS 빌드 뒤 `cmake -S . -B build/ios-device -DSOCIETY_IOS_FILES_INTEGRATION_TEST=ON`으로 활성화하고 Society를 다시 빌드·설치·실행한다. 이 옵션은 기본적으로 꺼져 있다. `tests/IosFilesIntegration.swift`는 App Group 원본과 시스템의 공개 루트 열거를 비교하고, 새 UUID가 붙은 테스트 폴더 안에서 생성·읽기·편집·이름 변경·삭제와 원본 반영을 확인한다. 기존 사용자 파일을 삭제하거나 도메인을 초기화하지 않는다.

결과는 앱의 `Documents/ios-files-integration.json`과 `SOCIETY_FILES_INTEGRATION` 콘솔 로그에 남는다. 성공하면 기존 `Open in Files` 기능으로 공개 루트를 연다. `devicectl device copy from`의 `appDataContainer` 도메인으로 결과를 가져올 수 있다. 확인 뒤 옵션을 OFF로 바꾸고 다시 빌드·설치한다. 이 검사는 연결된 기기에서 실제 File Provider를 호출하며 호스트의 plist·Swift 구문 검사와 별개이다. 실패한 단계에 테스트 폴더가 남으면 해당 UUID 폴더만 확인한다. 서명 번들 검사 `tests/verify_ios_bundle.py`는 이 진단 함수가 포함된 빌드를 배포용 검사에서 거부한다.

기기 연결이 끊겨 빈 테스트 폴더가 남았다면, 다음 검사 실행에 `SOCIETY_FILES_TEST_CLEANUP` 환경 변수로 그 정확한 `Society Files Test <UUID>` 이름을 전달할 수 있다. 이 처리는 해당 이름의 빈 일반 폴더만 삭제하며 다른 파일을 검색해 정리하지 않는다.

## 같은 계정의 로컬·원격 기기 파일

상단 Devices에서 로그인한 뒤 같은 계정의 Society 호스트 `Files/`를 탐색하고 다운로드한다. 데스크톱은 독립 [Preferences 창](docs/Preferences.md)에서 Client mode / Host mode를 전환한다. 상단 Preferences 버튼 또는 ⌘+, / Ctrl+,로 열며 변경은 즉시 적용된다. 앱 시작 시 기본값은 클라이언트이다. 호스트 모드에서만 현재 컨테이너의 `Files/`를 공개한다. iOS/Android는 항상 클라이언트이며 C++ 진입점에서도 호스팅을 차단한다. iiServerHost 0.2.0은 로컬 TLS 연결을 우선하고 실패하면 원격 중계로 전환한다. 중계 주소와 기기 TLS 설정, 인증·공개 범위·모바일 제한 및 검증 방법은 [NetworkDrive.md](docs/NetworkDrive.md)를 따른다.
