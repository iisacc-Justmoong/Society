# Windows 실행 검증

Windows GUI 검증은 windows QPA를 사용한다. GUI fixture에도 실제 앱과 동일한 Society App QML 리소스를 포함하여 qmldir의 상대 경로를 유지한다. SDK 헤더 재빌드 검증은 CMake 경로를 POSIX 표기로 직렬화하고 플랫폼 실행 파일 확장자를 적용한다.

Qt 6.8의 QML registrar가 basename으로 포함하는 QML_ELEMENT 헤더의 실제 디렉터리를 앱 include 경로에 명시한다. QrScanner를 포함한 앱 타입 등록이 누락되어 Main.qml 로드가 실패하던 문제를 실제 앱 실행 로그로 확인하였다.

암호화된 그룹 상태의 Windows 접근 범위는 POSIX mode 비트 대신 실제 NTFS DACL의 소유자 SID 및 상속 차단으로 검증한다.

모델 병합 러너의 절대 경로는 Windows에서 .exe 확장자를 해석한다. 실제 설치본을 찾는 모델 병합 통합 테스트에서 확인한다.

실행파일에서 생성된 qml_register_types_Society를 참조하여 MinGW LTO·섹션 GC가 타입 등록 코드를 제거하지 못하도록 한다. 실제 QML 시작 및 네이티브 창 생성 러너로 검증한다.

Qt의 대규모 타입 registrar는 MinGW PE big-object LTO 플러그인이 해석하지 못해 등록 함수 자체가 링크에서 사라졌다. 해당 생성 소스만 -fno-lto로 컴파일하고 나머지 제품 최적화 정책은 유지한다.

앱과 GUI 테스트는 organizationName 및 organizationDomain을 설정하여 Qt Settings가 Windows에서 정상 초기화되도록 한다. 파일 격자 테스트에는 실제 PreviewProvider를 등록하여 프로덕션 썸네일 경로를 검증한다. 번들 아이콘은 file URL과 qrc URL 모두의 경로를 판별한다.

ModelPackagingController는 SDK의 네이티브 Windows 역슬래시 출력 경로와 QML의 슬래시 경로를 정규화한 뒤 일치 여부를 검증한다. 실제 패키지 생성 및 QML 통합 테스트가 저장된 결과와 원본 보존을 함께 확인한다.

데몬의 정상 종료 검증은 Windows에서 POSIX SIGTERM 대신 기존 --exit-after-ms 옵션을 사용하며, 서비스 소유권 해제를 실제 잠금 파일로 확인한다.

Windows 런타임 선택은 네이티브 exe를 우선하며, 사용자 지정 shebang 스크립트는 명시된 Python 또는 Git Bash 인터프리터에 파일 경로와 인수를 별도로 전달한다. 실제 텐서 병합 테스트용 Python은 SOCIETY_MODEL_MERGE_TEST_PYTHON CMake 옵션으로 지정한다.

파일 썸네일 검증은 실제 society-preview 이미지 provider URL과 SDK thumbnailUrl을 비교하며, 한글 및 # 문자가 포함된 PNG의 Image.Ready 상태를 함께 검사한다.

병합 결과의 output/base_model/cache_dir/additional_models 경로는 SDK 결과 검증 후 QML의 슬래시 표현으로 정규화한다. 실제 CPU 텐서 연산과 결과 재읽기 검증을 수행하는 집계 테스트의 전체 한도는 600초이며 개별 작업 제한은 유지한다.

검사 보고서의 compatibility_models, resolved_sources 및 sources[].path도 동일한 로컬 경로 표현을 사용한다. 패키지 전체 전달, 아키텍처 검사와 QML의 재선택 진단 회귀가 원본 모델 보존 및 새로운 검사 결과 반영을 검증한다.

Models 탐색 항목의 위치 검증은 StorageNavigation의 현재 계약인 그룹 내부 간격 0 px와 행 높이 32 px에 따른 Files와 Models의 상대 좌표를 검사한다. 이전의 10 px 간격에 해당하는 절대 좌표를 제거하였다.

모델 가져오기 창을 연속 열고 닫는 회귀에서 Qt 6.8 Windows native dialog helper의 null 객체 접근을 GDB로 확인하였다. Windows 모델 선택은 Qt Quick FileDialog의 DontUseNativeDialog 옵션을 사용하며, 다른 플랫폼의 기존 선택은 유지한다. Drive 회귀는 옵션과 파일 선택 흐름·모델 카드 탐색을 함께 검사한다.
