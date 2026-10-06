# Society 폴더 온보딩

Society의 기본 저장소는 일반 디렉터리이다. **Create a new folder…**에서 선택한 기존 부모 폴더 아래에 `Society/`를 만들고, **Choose an existing folder…**에서는 `.society-drive.json`이 있는 Society 루트를 직접 선택한다. 두 선택 모두 네이티브 `FolderDialog`를 사용한다.

## 생성과 복원

SDK `SocietyDrive::createAt(parent)`가 일반 폴더와 9개 섹션을 준비한다. 이미 선택한 폴더가 유효한 드라이브라면 컨테이너 UUID를 재사용한다. 무관한 파일, 심볼릭 링크, 다른 드라이브 내부, 기존 디스크 이미지 내부에는 새 저장소를 만들지 않는다. 생성 실패 시 기존 내용을 덮어쓰지 않는다.

`DriveController`는 준비를 백그라운드에서 실행하고, 유효한 드라이브를 검증한 뒤 `SharedStorage::setDefaultContainer()`로 설정을 저장한다. 일반 디렉터리 설정은 `schemaVersion: 1`, `path`, `containerId`이며 `imagePath`를 포함하지 않는다. 다음 실행은 동일한 폴더와 UUID를 확인하여 복원한다. 폴더가 없어졌으면 온보딩을 표시하고 기존 저장 설정을 보존한다.

`Open in Finder`는 Society 루트를 연다. Storage의 Files, Photos, Asset Library, Generation History, Models, Thinking Space, Forked, Published, Deleted가 루트 바로 아래 실제 디렉터리이다. Models의 기존 유형별 하위 폴더와 각 섹션의 원래 콘텐츠 경로를 유지한다. Finder 연결을 위해 디스크 이미지나 File Provider 복제본을 만들지 않는다.

## 기존 디스크 이전

기존 sparsebundle을 일반 폴더로 이전하는 재실행 가능한 도구는 SDK `tools/migrate_directory.py`이다. 쓰기 앱과 동기화 데몬을 정상 종료한 뒤, 마운트된 내부 데이터 루트와 공개 Files 루트를 입력한다. 이전은 복사 전용이며 원본 이미지와 UUID를 보존한다. 공개 Files를 새 루트의 `Files/`로 합치고, 사진·객체 이력·동기화 메타데이터와 모델 데이터를 함께 보존한다. macOS 볼륨 관리 파일과 `.society-disk.plist`는 새 저장소에 포함하지 않는다.

도구는 파일별 SHA-256과 복사한 바이트의 읽기 검증을 기록한다. 중간 상태를 숨김 작업 폴더와 검증 로그에 저장하며, 재실행 시 원본 인벤토리와 완료된 파일의 해시를 재검사한다. 전체 검증이 끝나야 최종 이름으로 공개한다. 저장 설정은 복사 도구가 변경하지 않으며, 완성된 폴더를 앱으로 열어 전환한다.

레거시 `DiskImage` API와 이미지 설정 복원은 복구·이전을 위한 호환 경로이다. 새 폴더 생성에서는 호출하지 않는다. iOS·Android는 기존 관리형 디렉터리와 호스트 연결 계약을 유지한다.

<a id="account-owned-location-and-relocation"></a>

## 계정 위치

계정의 컨테이너 ID와 소유 장치 ID는 유지한다. 기존 서버 필드 `imagePath`에는 호환성을 위해 일반 디렉터리의 절대 경로를 전달한다. 다른 장치는 이 호스트 경로로 자신의 복제본을 교체하지 않는다. 이동한 폴더는 소유 호스트에서 **Locate moved folder…**로 선택하며 UUID가 다르면 거부한다. 로컬 설정의 변경과 실제 계정 서버 반영은 별도의 결과이다.

## 검증

레이아웃과 버튼은 설치된 LVRS를 사용한다. `Society.Drive`는 네이티브 폴더 선택·취소, 기존 폴더 생성·재사용, 유니코드 경로, 일반 폴더 설정 복원과 Finder 경로를 검사한다. SDK 드라이브 테스트는 9개 실제 섹션과 UUID 재사용, 충돌·리디렉션 거부를 검사한다. `test_directory_migration.py`는 별도 Files 볼륨 병합, 숨김 상태·페이로드·UUID 보존, 원본 유지와 충돌 거부를 검사한다. 레거시 디스크 테스트는 복구 호환성을 검증한다.

## 모바일 실행 시 호스트 연결 검증

기기 역할은 플랫폼으로 고정한다. 데스크톱은 로컬 디스크를 검증하고, 모바일은 관리형 로컬
컨테이너뿐 아니라 `NetworkDriveController::hostConnectionReady`까지 참이어야 기본 화면에 진입한다.
이 상태는 디스크에 저장하지 않으며 매 실행과 연결 재수립 시 새로 확인한다.

- 계정 인증과 현재 호스트 연결이 유효해야 한다.
- 계정으로 승인된 호스트와 이번 연결에서 실제 동기화 라운드를 완료해야 한다.
- 완성된 로컬 복제본의 컨테이너 UUID와 호스트 바인딩이 검증 결과에 일치해야 한다.
- 미완료·실패한 동기화, 릴레이 서버만 연결된 상태, 오프라인 캐시만 있는 상태는 진입 조건을 충족하지 않는다.

조건을 충족하지 못하면 LVRS 온보딩에서 **Sign in to connect / Retry host connection**과
**Find a host or configure a server…**를 제공한다. 후자는 기존 Devices 시트를 열어 주변 호스트 검색,
계정 자동 연결, `wss://` Society 서버 설정을 제공한다. 로그인 및 기기 시트는 기본 화면이 숨겨져도
사용할 수 있다. QR 수동 페어링은 기존 Files 열람 기능이며 계정 검증 동기화를 대신하지 않는다.

호스트가 응답하지 않아 동기화가 실패하거나 연결이 끊기면 검증 상태를 폐기하고 다시 온보딩을
표시한다. 저장된 복제본·다운로드·미전송 변경은 삭제하지 않는다. 일반적인 후속 동기화가 시작되는
것만으로는 검증 상태를 폐기하지 않으므로 사용 중 매 동기화마다 화면이 바뀌지 않는다.
다른 컨테이너·계정·전송 세션으로 바뀌면 이전 검증 결과를 재사용하지 않는다.

`Society.ClientOnlyNetwork`는 오프라인 완성 캐시와 응답 대기 중인 인증 연결이 검증을 우회하지
못함을 검사한다. `Society.Account`의 실제 로컬 인증·동기화 통합 검사는 최초 진입, 연결 해제,
재연결 후 재검증을 확인한다. LVRS 모바일 온보딩은 390×844와 320×568에서 버튼 클릭과
스크롤을 검사한다. 이 테스트는 macOS의 모바일 정책 빌드이며 실제 iOS/Android 기기 검증과는 구분한다.
