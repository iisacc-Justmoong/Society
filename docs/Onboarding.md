# Society 디스크 온보딩

Society는 저장소가 없거나 연결되지 않아도 LVRS 온보딩 창을 표시한다.
macOS에서 파일 다이얼로그로 선택한 폴더는 **디스크 이미지의 보관 위치**이며 컨테이너 자체가 아니다.
그 안에 `Society.sparsebundle`을 만들고 컨테이너 ID와 9개 논리 영역을 구성한다.
Finder의 실제 APFS `Society` 디스크 루트는 `Files`의 내용이다. 나머지 영역은
파일 관리자에 표시하지 않는 내부 APFS 볼륨에 두며 Society 앱이 별도로 접근한다.

## 생성과 복원

1. 설정이 없거나 저장된 디스크를 검증하지 못한 실행에는 LVRS 설명과 두 가지 복구 버튼을 표시한다.
2. **Create a new disk…**는 네이티브 `FolderDialog`로 보관 폴더를 선택한 뒤 디스크를 생성한다.
   **Choose an existing disk…**는 `FileDialog`로 이미지를 선택한 뒤 기존 디스크를 마운트한다.
   로그인 여부와 관계없이 기존 이미지 선택이 가능하며, 취소하면 설정이나 파일을 변경하지 않는다.
3. SDK `DiskImage::create()`가 선택한 위치에 APFS sparsebundle을 생성하고 마운트한다.
   같은 위치에 이미 Society가 만든 이미지가 있으면 해당 이미지를 다시 사용한다.
4. 마운트된 볼륨을 검증한 뒤 `SocietyDrive` 레이아웃과 공통 설정을 저장한다.
   모두 성공해야 대시보드로 진입한다. 생성·마운트 중에도 GUI는 응답하며 중복 제출을 막는다.
5. 다음 실행에는 `SharedStorage::open()`이 저장된 이미지 경로로 다시 마운트한다.
   마운트 경로가 `/Volumes/Society 1` 등으로 변경되어도 컨테이너 UUID를 확인해 복원한다.

새 디스크의 보관 위치는 존재하는 로컬 폴더이다. 해당 폴더의 기존 파일·디렉터리는 유지한다.
이미지는 파일 추가에 따라 실제 점유량이 증가한다. 생성 시 논리 용량은 저장 위치의 가용 공간을
기준으로 정하며 최소 512 MiB가 필요하다. 같은 저장 장치를 사용하는 다른 파일도 공간을 소비하므로
이미지의 남은 논리 용량이 호스트 장치의 실제 가용량을 보장하지는 않는다.

이미지 저장 폴더에는 `Society.sparsebundle`과 동시 준비를 조정하는 숨김 잠금 파일이 있다.
9개 영역과 컨테이너 매니페스트는 마운트된 볼륨에만 생성한다. 디스크를 자신의 내부에 중첩 생성할 수 없다.
이름이 같은 무관한 파일·이미지, 잘못된 이미지, 저장된 UUID와 다른 컨테이너를 덮어쓰지 않는다.

## 연결 해제와 기존 설정

Finder의 추출 기능을 사용할 수 있다. 앱이 볼륨을 읽지 못하면 온보딩으로 돌아가며,
Retry saved location으로 같은 이미지를 다시 연결한다. 외장 저장소가 분리되었거나 이미지가 사라졌으면
오류를 표시하고 기존 설정을 유지한다. 사라진 디스크를 빈 디스크로 대체하지 않는다.
앱 종료 시에는 다른 iisacc 앱도 사용하는 볼륨을 자동 추출하지 않는다.

이전 폴더 방식의 저장 설정은 macOS의 정상 디스크 설정으로 인정하지 않는다.
온보딩에서 새 디스크를 만들도록 안내하며 기존 파일을 자동 이동·삭제하지 않는다.
`--container`는 이미 구성된 논리 컨테이너를 명시적으로 여는 개발·호환 경로로 유지되며,
일반 폴더에 새 컨테이너를 생성하지 않는다.

macOS의 새 디스크는 File Provider 복제본을 등록하지 않는다. `Open in Finder`는 공개 Files 볼륨의
실제 루트를 연다. 따라서 사용자가 저장한 파일과 폴더가 바로 보이고, 신규 Files는 빈 상태이며,
`Files`라는 중간 폴더나 `Models`, `Photos`, 매니페스트, 동기화 데이터는 노출되지 않는다.
앱은 `SocietyDrive::sectionPath()`와 논리/실제 경로 변환 API를 사용한다.

공개 이미지 `Society.Files.sparsebundle`은 원래 `Society.sparsebundle` 패키지 내부에 보관한다.
계정에는 원래 패키지 경로 하나를 저장하므로 디스크 이동은 두 영역을 함께 이동한다.
기존 단일 볼륨 디스크는 처음 다시 열 때 Files 데이터를 복사하고 내부 원본을
`.society-legacy-files`에 보존한 다음 공개한다. 컨테이너 UUID와 나머지 영역은 유지한다.
복사 실패 시 기존 파일을 삭제하거나 빈 드라이브로 대체하지 않는다.

iOS·Android는 기존 관리형 컨테이너를 유지한다. 현재 `DiskImage`의 생성 백엔드는 macOS용이다.
Windows·Linux의 새 이미지 생성은 미지원 오류를 반환하며 일반 폴더를 디스크로 등록하는 대체 동작을 하지 않는다.
기존 Dokan/FUSE와 명시 경로 열기 기능은 유지된다.

## UI와 검증

레이아웃·설명·버튼은 설치된 LVRS의 `VStack`, `Label`, `LabelButton`을 사용한다.
파일 선택은 네이티브 `FolderDialog`와 `FileDialog`이며 작은 창에서는 세로 스크롤한다.
`Society.Drive`는 데스크톱·모바일 역할별 복구 버튼, 실제 다이얼로그 열기와 취소,
1440×900, 800×600, 360×320, 390×844 레이아웃에서 생성·기존 이미지 지정,
유니코드·공백·특수문자 경로, 비동기 진행 상태, APFS 디바이스, 설정 복원, 이미지 부재 복구,
기존 일반 폴더 거부를 검사한다. 디스크 통합 검증은 macOS에서 실행한다.
SDK의 `iiSocietyContainer.disk_image`는 데이터 기록 후 추출·볼륨 이름 변경·재마운트·UUID 보존과
저장 위치의 기존 파일 보존을 검사한다. Qt 없이 공개 헤더를 컴파일하는 검사도 별도로 실행한다.
모든 테스트 파일과 로그는 `build/` 하위에 둔다. OS가 마운트 위치를 관리하며 테스트 종료 시 추출한다.

## Account-owned location and relocation

The account model (`iiAcountManager::Account::societyContainerDrive`) is the authority for a signed-in
host's disk location. It contains the disk UUID, owning device ID, absolute image path and server
revision. The local `storage.json` remains a mount/restart cache for offline and signed-out use;
`/Volumes/Society` is never the shared path. Other devices receive the same account record without
replacing their own replica roots with a host filesystem path.

New disks are registered when signed in. A locally created disk can be registered after login from
**Preferences → Account drive location → Save current drive to account**. After moving an existing
image, use **Locate moved disk…** on its host. Recovery onboarding provides **Choose an existing disk…** for the same action,
including when the image filename changed; selecting the new parent works for `Society.sparsebundle`.
Society mounts it and checks the original UUID before saving the new image path. The account server
rejects a stale revision, a different disk UUID, or an update by a different host. Publication errors
remain visible in Preferences; the current local disk stays usable. Disk creation and location changes
complete asynchronously and discard results after the initiating account session changes.

The common `AccountSession` reads location changes immediately after authentication and every
30 seconds while authenticated. An offline device catches up after reconnection; this is eventual
synchronization, not server push. A full authenticated snapshot/secure restore can also restore the
host image locally without waiting for another server response. Account-specific updates never
create a missing disk or reinterpret an arbitrary folder as a disk.

The matching `Service/iisacc.com` container API must be deployed before account publication works.
Older servers remain compatible with local onboarding and show an actionable publication failure.

Validation: the isolated native relocation test creates an APFS disk, registers it against a local
account authority, ejects and renames the image, reconnects the same UUID, and verifies the second
account session receives the new path while its local replica root is unchanged. Existing onboarding,
layout, file navigation and Preferences behavior are covered by `tests/tst_drive.cpp`.
The suite isolates storage settings below `build/`; temporary per-case overrides restore the
suite's setting path so later Preferences tests never write the user's default configuration.

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
