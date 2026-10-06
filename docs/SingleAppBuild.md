<a id="single-app-build-contract"></a>

# 단일 앱 빌드 계약

제품은 [워크스페이스 단일 앱 정책](../../build-policy/README.md)를 사용합니다. `build/` 아래에 오직 정통 애플리케이션 번들이 생성됩니다. 테스트와 헬퍼는 일반 실행 파일이며, 런타임 배포 및 패키징은 원위치로 작동합니다. 정통 경로, 플랫폼 전환 및 검증 명령에 대한 정책을 참조하십시오.

iOS 독립 실행형 XCTest UI 실행기가 비활성화되었습니다. 집계 가드는 두 번째 앱이 생성되기 전에 빌드를 거부합니다. 상호 작용 테스트 Swift 소스는 러너 프리 하네스에 계속 사용할 수 있습니다.

계정과 데몬 테스트 대상은 컴파일 정의가 생성되기 전에 같은 데몬 실행 가능 파일을 해결합니다. macOS 에서는 정통 Society 번들로 의존하며 Contents/ MacOS / SocietyDaemon 헬퍼를 사용합니다. 계정 테스트 설정은 이 경로가 비어있지 않고 실행 가능함을 확인하므로 빈 QProcess 명령이 원인을 가리키기 전에 패키징 회귀가 실패합니다.

iOS 빌드 계약은 SDK 세트의 전체 이름을 확인하며, iiPhotoLibrary 를 포함합니다. Mac Catalyst 드롭 문법 확인에는 AccountManager 와 Qt 동시 헤더가 포함되어 있으며, DriveController 에서 사용됩니다. 이는 iOS 런타임 테스트가 아닌 문법 유효성 검사입니다. 문법 확인은 C++23 를 사용하여 SDK 공개 헤더와 일치하며, DiskImage .h 파일과 같이 std::expected 를 노출합니다.
