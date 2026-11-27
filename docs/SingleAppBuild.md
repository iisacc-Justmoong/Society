<a id="single-app-build-contract"></a>

# 단일 앱 빌드 계약

Society는 iiSDKCore 0.20 이상을 명시적으로 참조하며 iiSDKBuilder가 선택한 Core 패키지로 빌드한다. 이전 직접 CMake 구성의 [워크스페이스 단일 앱 정책](../../build-policy/README.md)은 레거시 경로로 남아 있다. 현재 배포 대상은 `App/`의 Society, Daemon, Uninstaller 세 앱이며 SocietyCommunity, platform, Shared의 자리 표시자는 패키지에서 제외한다. iiSDKBuilder의 통합 앱 트리에서 배포 목록에 없는 독립 CMake 앱이 발견되면 빌드를 중단한다. macOS 제품 패키지는 `/Applications/Society/` 아래에 세 `.app`을 설치하도록 구성한다. 배포 구성에서는 독립 Daemon 번들에도 Society와 같은 선택된 SDK 입력으로 런타임을 배치하고 서명하며 기존 내장 데몬은 유지한다. Society 번들은 복사한 내장 데몬의 의존성을 독립 데몬의 런타임에서도 찾는다. Uninstaller의 진입점은 현재 기능이 구현되지 않은 자리 표시자이다. `Society.AppDistribution`은 앱을 실행하거나 설치하지 않고 빌더의 시험용 설치기 내용과 macOS 제품 폴더 아이콘을 검사한다. 폴더 아이콘 기능은 `SDK/iiSDKBuilder/build/iisdk-builder`에서 검증했으며 기존 `~/.local` 빌더 설치본에는 이 변경이 자동으로 반영되지 않는다. 2026-11-25 macOS arm64에서 여섯 앱을 포함한 이전 패키지를 생성한 기록은 현재 배포 목록과 구분한다. 빌더 생성 패키지는 서명되지 않으며 공개 설치기는 별도로 서명과 공증이 필요하다. 실제 설치와 앱 실행은 검증하지 않았다.

기존 단일 앱 iOS 구성에서 독립 실행형 XCTest UI 실행기는 비활성화되며 집계 가드는 두 번째 앱이 생성되기 전에 빌드를 거부한다. 상호 작용 테스트 Swift 소스는 러너 프리 하네스에 계속 사용할 수 있다. macOS 다중 앱 패키지 검증은 이 iOS 제약을 해소하지 않는다.

계정과 데몬 테스트 대상은 컴파일 정의가 생성되기 전에 같은 데몬 실행 가능 파일을 해결합니다. macOS 에서는 정통 Society 번들로 의존하며 Contents/ MacOS / SocietyDaemon 헬퍼를 사용합니다. 계정 테스트 설정은 이 경로가 비어있지 않고 실행 가능함을 확인하므로 빈 QProcess 명령이 원인을 가리키기 전에 패키징 회귀가 실패합니다.

iOS 빌드 계약은 SDK 세트의 전체 이름을 확인하며, iiPhotoLibrary 를 포함합니다. Mac Catalyst 드롭 문법 확인에는 AccountManager 와 Qt 동시 헤더가 포함되어 있으며, DriveController 에서 사용됩니다. 이는 iOS 런타임 테스트가 아닌 문법 유효성 검사입니다. 문법 확인은 C++23 를 사용하여 SDK 공개 헤더와 일치하며, DiskImage .h 파일과 같이 std::expected 를 노출합니다.

macOS releases use the [compact bundle profile](BundleFootprint.md). Prepare the
dedicated native image SDK without optional LibTorch, then build the canonical
app. Imported QML modules, Basic Controls, native dependency closure, and release
symbol stripping keep the package within the verified 512 MiB budget. Runtime
and footprint checks run before an installed application is replaced.

기존 `build/`에 남은 테스트 컨테이너, 이미지, 설치 이전 백업과 조사 기록은 정리 전에 `resources/.recovery/society-build-history-20261030/`에 보존한다. 활성 애플리케이션과 실제 Society 저장소는 별도로 유지한다.

테스트용 컨테이너, 설정, 캡처와 MCP 시험 자료는 구성별 `resources/.state/tests-*`에 생성하며 CTest 종료 시 정리한다. 시험 실행 파일과 필요한 실제 앱 번들의 경로는 계속 `build/`에서 확인한다.
Python 기반 이미지·모델 fixture도 전용 임시 리소스 위치를 사용하며, 임시 디렉터리의 상위 경로 누락을 자동 검사한다.
배포 단계의 임시 보고서는 `resources/.state/`에서 생성하고 성공 시 제거하므로 `build/`에 별도 로그를 남기지 않는다.

## Clean deployment layout

macOS deployment prints its report to the build console by default and stores
per-bundle locks under `resources/.state/deploy-locks/`. Framework copies retain
runtime binaries and resources without SDK headers or module maps. The
`tests/deploy_layout_test.py` contract checks these behaviors with disposable
resources and does not launch the application.
Interrupted native-tool temporary executables are excluded from helper discovery
and removed only when the corresponding real executable exists in the generated
bundle. The deployment test covers this alongside real helper discovery.

The selected iiSocietyContainer, GUI, sharing, FileTree, and maintenance
libraries are all explicit native deployment inputs. After deployment, the
build compares each bundled Mach-O architecture UUID with its selected SDK
binary; an older transitive copy left by a previous build fails verification.
This check reads bundle files and does not start Society.

When compatible macOS inference packages have been prepared under
`resources/dependencies/macos-runtime/`, a clean Society configure selects
those packages and their matching MLX runtime by default. Explicit package
paths supplied by the caller remain authoritative, and the macOS defaults do
not affect iOS. This keeps Society's build inputs independent of another
product's transient build directory.

iOS와 Android의 프로젝트 전용 SDK 설치 입력은
`resources/dependencies/ios/`와 `resources/dependencies/android/<ABI>/`에 둔다.
모바일 도구의 사전 점검과 빌드 출력은 표준 출력으로 전달한다. Android Gradle,
사용자 설정, Java 임시 파일은 소유한 `resources/.state/` 작업 공간에서
사용하고 성공·실패 시 정리한다. OpenSSL 소스 압축 파일은 리소스 의존성에
보관하고, 검증 후 임시 리소스에 푼 원본을 `build/android/`의 별도 컴파일
디렉터리에서 빌드한다. 소스와 작업 디렉터리는 빌드가 끝나면 정리한다.
Gradle 문제 보고서 파일은 만들지 않으며 경고와 오류는 콘솔에 표시한다.
## QML debug symbols

Standard and compact macOS deployment keep QML `.dSYM` debug-symbol bundles
outside the application while preserving the QML runtime files.

The product-local macOS inference dependency uses a relocatable pkg-config
prefix, so its metadata does not point into another product's build directory.
The build helper rejects symlinks in that pkg-config path before changing it.
When the verified `resources/dependencies/macos-mlx-runtime/` pair is present,
it is selected automatically for native macOS unless the caller has set an
explicit `SOCIETY_MLX_RUNTIME_DIR`. Compatible local SDK packages are likewise
found from `resources/dependencies/`, with explicit package choices preserved.

그룹 상태 저장의 기존 아홉 사례는 CTest 실행기로 유지하고 제품과 SDK 소비자에서 QtTest를 링크하지 않는다. 암호화 컨텍스트와 지연 저장 취소는 가짜 키 저장소를 사용하는 단독 사례로 선택할 수 있다. 지연 콜백의 결과는 호출 뒤 검사하며 임시 파일은 resources fixture에 둔다.

네이티브 파일 실행기의 기존 사례와 파일 열기 이벤트의 세 사례도 QtTest 없이 CTest에서 유지한다. 실행기 검사는 가짜 시작 함수를 사용하고 파일 fixture는 resources 작업 공간 안에 둔다.

제품 의존성의 세 기존 사례도 CTest 실행기로 유지한다. 파일 공유의 제어 파일 경계는 앱을 시작하지 않고 단독 검사하며, 실제 패키지 앱을 시작하는 피어 검사는 별도의 실행 범위로 남긴다.

작업 저장소의 중단 복구 사례와 추천 모델의 세 사례도 QtTest 없이 CTest에서 유지한다. 추천 검사는 가짜 모델을 사용하며 잘못된 응답에 대한 예외 계약을 보존한다.

알림 센터·최근 항목 백엔드·상주 게이트웨이의 기존 여섯 사례는 QtTest 없이 CTest에서 실행한다. 알림 모델 신호와 게이트웨이 이벤트의 인자를 직접 기록하며, 최근 항목의 지연 작업은 실패 시에도 대기 장벽을 해제한다. 이 검사는 가짜 백엔드와 비시각 QML 바인딩만 사용한다.

## 패키징을 생략하는 컴파일 확인 계약

`SOCIETY_COMPILE_ONLY=ON`으로 `build/`를 구성하면 Society를 컴파일하고 링크하되 해당 앱 대상의 실행용 헬퍼·리소스 복사, macOS 런타임 배치·서명과 마운트 도구 복사를 실행하지 않는다. 앱 실행을 요구하지 않는 코드 검증에 사용하며, 그 결과가 완성된 앱 번들이나 배포 패키지라는 뜻은 아니다.

기존 배포 빌드의 기본 동작은 이 옵션을 끈 상태로 유지한다. 컴파일 전용 구성에서는 런타임 실행 검증과 패키지 대상 실행을 거부한다. 이전 빌드가 남긴 번들 파일은 식별 없이 제거하지 않으며, 정상 앱의 리소스를 `resources/`에서 연결하는 개발 및 배포 경로는 별도 작업이다. `Society.CompileOnlyCommand` 시험은 생성된 앱 링크 명령과 패키지 대상의 실행 거부를 검사하며 앱을 실행하지 않는다. 기존 기본 배포 빌드의 리소스 복사 방식은 아직 `resources/` 연결 방식으로 이행되지 않았다.

## macOS 번들의 라이브러리 심벌 검사

macOS 배포기는 경로 정리와 선택적 바이너리 축소를 마친 뒤 서명 전에 앱과 헬퍼, 프레임워크, 플러그인의 Mach-O 의존성을 아키텍처별로 읽는다. 번들 안의 지정된 공급 라이브러리와 그 라이브러리가 재수출하는 번들 라이브러리에서 필수 심벌을 찾지 못하거나 공급자를 구분할 수 없으면 배포를 중단한다. 이 검사는 동일한 라이브러리 이름과 버전으로 서로 다른 ABI가 선택되어도 누락된 심벌을 발견한다.

운영체제 라이브러리, 약한 심벌, 실행 시 동적 검색하는 심벌은 이 정적 검사의 범위 밖이다. 앱을 시작하지 않으므로 실제 실행 성공이나 동적 플러그인 경로의 기능을 보증하지 않는다. `Society.MacSymbolBindings`는 별도 fixture를 컴파일하여 정상 연결, 호환되지 않는 공급자의 필수 심벌 누락, 약한 심벌, 재수출을 검사한다.

`iiSocietyHelper`를 준비할 때는 제품이 실제로 선택하는 `iiAccountManager`와 호환되는 SDK를 사용한다. 계정 라이브러리에 우연히 포함된 OpenSSL 심벌에 연결된 예전 Helper와 OpenSSL을 별도 라이브러리로 사용하는 새 계정 라이브러리는 같은 install name이어도 혼용할 수 없다. Helper를 선택된 계정 SDK에 맞춰 다시 연결한 후 심벌 검사를 통과한 번들로 설치기를 만든다.
