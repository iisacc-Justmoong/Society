# 앱이 먼저 만든 공유 논리 디스크 인수

Society 설치 전에 Dreamscapes 등의 앱이 만든 iiSocietyContainer 논리 디스크를 그대로 사용할 수 있다. 공통 접근 계층은 모든 플랫폼에서 같은 `.societycontainer` 저장소 계약이며 Apple App Group은 해당 플랫폼의 공유 위치 어댑터이다. 공유 등록 정보에는 상대 경로, 원래 소유 앱, 현재 관리 앱, 안정적인 기기 내 저장소 식별자가 들어 있다. 물리 디렉터리 이름에 패키지 확장자가 필수인 것은 아니다.

iOS에서는 네이티브 기본 드라이브 초기화가 공유 등록 정보를 먼저 검증하고 관리 앱을 Society로 변경한다. 같은 C++·Swift 공유 잠금과 원자적 등록 파일 교체를 사용한다. 기존 File Provider 식별자, 모델, 생성 이미지와 파일 이력은 같은 경로에 유지된다. 기존 패키지와 과거 디렉터리 업그레이드 경로도 유지한다.

데스크톱에서는 저장소 설정이 없는 첫 실행에 `ApplicationStorage::sharedRoot()`를 탐색한다. Windows·Linux는 앱 이름에 의존하지 않는 사용자 공통 데이터 루트, macOS는 권한이 있는 App Group을 사용한다. 데스크톱 배포는 `IISACC_SHARED_STORAGE_ROOT`로 공유 루트를 지정할 수도 있다. 등록된 논리 디스크가 있으면 기존 비동기 드라이브 준비 경로에서 인수하고 기본 선택으로 저장한다. 계정에 등록된 다른 호스트의 저장소와 명시적인 사용자 선택은 기존 우선순위를 유지한다.

Container와 Helper 모두 `HAS_APPLICATION_STORAGE` 기능을 제공해야 빌드할 수 있다. 논리 디스크를 여는 Container와 최초 호스트 연결에서 앱 원본을 보존하는 Helper를 함께 갱신한다. Society의 기존 iiSharedCanvas ABI를 사용하여 Helper를 별도 프로파일로 빌드할 수 있다.

`SocietyDriveTest::appOwnedLogicalStorageOpensAndRestoresWithoutCopyingFiles`는 논리 디스크의 직접 열기, 기본 선택 복구, Society 인수 후 동일 UUID·경로·파일 보존을 검사한다. SDK의 논리 저장소 및 Swift 공유 위치 테스트는 등록 오류, 원본 이력, 호스트 UUID 변경과 File Provider 투영을 검증한다.

`suiteStorageIsDiscoveredAndAdoptedOnFirstLaunch`는 기본 선택 설정이 없는 상태에서 다른 제품의 공유 등록을 발견하고 동일 경로·UUID를 유지하여 인수하는 동작을 검사한다.
