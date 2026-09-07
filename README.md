# Society

Qt Quick와 LVRS를 사용하는 Hello world GUI 앱이다. `Society` 창 중앙에
`Hello world!`를 표시하며 창 크기가 바뀌어도 중앙 정렬을 유지한다.
기본 창 크기는 640 × 400, 최소 크기는 320 × 240이다.

## 의존성

- CMake 3.31 이상, Ninja, C++20 컴파일러
- Qt 6.8.3: Quick, QuickControls2, 테스트용 Test 모듈
- 설치된 LVRS CMake 패키지와 QML 모듈

기존 Qt/LVRS의 창, 글꼴, 테마, 앱 부트스트랩을 재사용한다.
현재 macOS 프리셋은 Qt `/Volumes/Storage/Qt/6.8.3/macos`와
LVRS `~/.local/SDK/LVRS` 설치본을 사용한다.
실행 시 LVRS 패키지가 제공하는 라이브러리 경로와 명시적인 QML import 경로를 사용하므로
별도의 `DYLD_LIBRARY_PATH` 또는 `QML2_IMPORT_PATH` 설정은 필요하지 않다.

다음 13개 SDK를 `find_package(... CONFIG REQUIRED)`로 찾고 해당 SDK의
`패키지명::패키지명` CMake 타깃을 `Society`에 직접 링크한다.
기본 검색 경로는 `~/.local/SDK/<패키지명>`이며 하나라도 누락되면 구성이 실패한다.

| 필수 SDK | 소비자 테스트에서 확인하는 공개 API |
| --- | --- |
| iiFilePreview | `helloWorld()` |
| iiGeneralDocument | 문서 생성과 메타데이터 접근 |
| iiLicenseManager | `LicenseClient` 메타 객체와 상태 열거형 |
| iiLocalDiffusion | 연산 장치 이름 조회 |
| iiLocalLLM | `helloWorld()` |
| iiServerHost | `helloWorld()` |
| iiSharedCanvas | 벡터 에셋 ID와 종류 조회 |
| iiSocietyContainer | `helloWorld()` |
| iiSocietyHelper | `helloWorld()` |
| iiSocietySync | `helloWorld()` |
| iiUpdateManager | `UpdateManager` 메타 객체와 상태 열거형 |
| iiVoiceOver | `helloWorld()` |
| iiWhatsNew | `helloWorld()` |

요청의 `iiLisenceManager`는 실제 설치된 이름인 `iiLicenseManager`로 연결한다.
일부 SDK가 Qt 6.8.3을 정확히 요구하므로 Society도 같은 Qt 버전을 사용한다.
`iiXml`, `iiHtmlBlock`, `iiPaintEngine` 등 간접 의존성은 각 SDK 패키지가 선언한다.

이번 단계는 SDK의 빌드·링크 연결이다. Hello world 화면은 유지하며,
미리보기·추론·서버·동기화 등 제품 기능을 앱에서 실행하지 않는다.
위 표에서 `helloWorld()`를 확인하는 8개 SDK의 현재 설치본은 해당 초기 API를 제공한다.

## 빌드 및 실행

프로젝트 디렉터리에서 실행한다. 모든 빌드 산출물은 `build/`에 생성한다.

```sh
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug
open build/bin/Society.app
```

CLion에서도 CMake 빌드 디렉터리를 `build/`로 지정했다. 실행 대상은 `Society`이다.
다른 설치 경로에서는 `cmake --preset debug -DQt6_DIR="<Qt>/lib/cmake/Qt6" -DLVRS_DIR="<LVRS>"`로
프리셋 값을 덮어쓸 수 있다.

## 구조와 검증

- `main.cpp`: LVRS 런타임 초기화와 `Society.Main` QML 로드
- `App/Main.qml`: LVRS 창과 인사 문구
- `App/AI/`: Civitai·Hugging Face·Ollama 연동 클래스의 빈 초기 골격
- `tests/tst_hello.cpp`: 창·문구 표시, 세 가지 크기에서 중앙 정렬, QML 로드 경고 검사
- `tests/tst_dependencies.cpp`: 실제 앱의 링크 설정을 공유하여 13개 SDK의 헤더·심볼·실행 시 로드를 검사

AI 클래스의 소스와 헤더는 `SocietyDependencyTests`에 등록되어 빌드에 포함된다.
현재 클래스에는 API나 실행 동작이 없고 앱과 연결되어 있지 않으므로 별도의 기능
단언은 추가하지 않는다. 기존 의존성·GUI 테스트와 전체 빌드로 이번 골격 추가를 검증한다.

`ctest --preset debug`는 화면 없는 소프트웨어 렌더링 환경에서 GUI 동작을 검사한다.
`Society.Dependencies` 테스트는 13개 SDK의 공개 심볼을 호출한다. 추론 모델 다운로드,
네트워크 요청, 라이선스 활성화, 업데이트 설치는 수행하지 않는다.
`cmake --build build --target Society_qmllint`로 QML 정적 검사도 실행할 수 있다.
로컬 실행용 앱은 설치된 Qt/LVRS를 사용한다. 다른 컴퓨터에 배포하려면 별도 패키징이 필요하다.
