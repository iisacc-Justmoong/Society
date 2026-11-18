<a id="single-app-build-contract"></a>

# 단일 앱 빌드 계약

제품은 [워크스페이스 단일 앱 정책](../../build-policy/README.md)를 사용합니다. `build/` 아래에 오직 정통 애플리케이션 번들이 생성됩니다. 테스트와 헬퍼는 일반 실행 파일이며, 런타임 배포 및 패키징은 원위치로 작동합니다. 정통 경로, 플랫폼 전환 및 검증 명령에 대한 정책을 참조하십시오.

iOS 독립 실행형 XCTest UI 실행기가 비활성화되었습니다. 집계 가드는 두 번째 앱이 생성되기 전에 빌드를 거부합니다. 상호 작용 테스트 Swift 소스는 러너 프리 하네스에 계속 사용할 수 있습니다.

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
