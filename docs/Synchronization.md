# Society 컨테이너 동기화와 SDK 책임

같은 기기에서는 iiSocietyHelper를 통해 iisacc 앱이 Society와 협업한다. 다른 기기에서는 Society끼리 iiSocietySync를 통해 컨테이너 데이터를 동기화한다. Helper 메시지·객체·계정 모델 참조를 네트워크 프로토콜로 재사용하지 않는다. Dreamscapes 등의 소비 앱은 호스트에 연결하지 않으며, 같은 기기에 동기화가 끝난 원본 컨테이너에서 모델을 읽고 로컬에서 생성한다. [이미지 생성 책임](Generation.md)을 참조한다.

| 계층 | 담당 책임 |
| --- | --- |
| iiAccountManager 0.2.6 | 계정 검증·인증 상태·세션 바인딩·이벤트 기반 갱신·캐시 수명 |
| Society 앱 | 보안 그룹 저장소 제공, LAN 증명, LAN 발견·자동 페어링 큐, 호스트 선택, 데몬·모바일 백그라운드 수명과 사용자 화면 |
| iiSocietyHelper 0.7.1 | 기기 내 관측·메시지/ACK·객체 스냅샷·계정 참조·로컬 Container 접근 |
| iiSocietySync 0.4.0 | 기기 간 변경 기록·청크·이어받기·벡터 시계·삭제·충돌·원격 Files 탐색 |
| iiSocietyContainer 0.10.0 | 드라이브 UUID·8개 영역·로컬 공유 및 Files 전용 OS 투영 |
| iiServerHost 0.4.1 | 계정으로 인증된 일반 연결과 TLS, 요청·응답·Files 전용 전송 원시 기능 |

Helper와 Sync는 서로 링크하지 않는다. 하위 SDK는 Society 앱을 참조하지 않는다. 새 외부 의존성은 없으며 기존 Qt/SQLite·Container·ServerHost를 재사용한다. 파일 시스템·버전·충돌의 상세 계약과 외부 문서는 [Sync README](../../../SDK/iiSocietySync/README.md)에 있다.

## 실행 흐름

1. AccountController가 그룹 캐시의 로그인 상태와 유효한 LAN 권한을 복원한다.
2. NearbyDevices와 AutomaticPairing이 동일 계정 증명·기기 nonce·TLS 지문/코드를 확인하고 자동 연결한다.
3. NetworkDriveController가 현재 컨테이너와 계정 scope, 인증된 peer ID 목록, 원격 호스트를 Sync Controller에 제공한다.
4. Sync는 인증된 호스트의 descriptor를 확인하고 첫 연결에서 클라이언트의 기존 내용을 비공개 복구 영역에 보존한다. 호스트 UUID를 채택하고 전체 초기 상태 및 전송 중 변경을 내려받은 후에만 앱·OS에 미러를 공개하고 업로드를 허용한다. 이후 같은 논리 드라이브의 양방향 변경과 커서를 저장한다.
5. 양방향 해시·매니페스트 페이지의 교환과 마지막 ACK가 끝난 뒤 파일 바이트를 전송한다. 로컬 변경은 75ms로 합친 파일 감시 이벤트로 시작하고, 1초 간격 재검사로 원격 변경·누락을 회복한다. 대형 전송도 약 1초마다 청크 경계에서 매니페스트를 갱신하여 삭제와 작은 파일을 먼저 처리하고 부분 파일의 다음 오프셋부터 이어간다. 한 요청·해시·파일 적용에 걸리는 시간은 이 예약 간격에 포함되지 않으므로 완료 시간의 상한은 아니다. 새 파일 바이트가 없으면 변경 목록만 교환한다. Devices 화면은 동기화 상태·오류를 표시한다.

주기 동기화는 파일 뷰를 초기화하지 않는다. Sync는 전용 작업 스레드에서 실행하고, 완료 후 컨테이너 확인도 기존 QtConcurrent 작업으로 합쳐 처리한다. 동일한 미러 상태와 동일한 디스크 결과는 화면 변경 알림을 내보내지 않는다. 파일 뷰는 Qt FolderListModel 인스턴스를 유지하여 자체 비동기 파일 감시로 생성·수정·삭제를 반영한다. 실제 파일 추가·삭제로 Qt가 행을 다시 게시할 때에도 파일 경로로 선택 항목을 복원하고 유효 범위 안에서 스크롤을 유지한다. 모델 재생성은 사용자가 경로나 필터를 바꿀 때만 수행한다. 컨테이너 전환 뒤 도착한 오래된 결과는 폐기하고, 확인 도중 다른 폴더로 이동한 경우 이전 경로 검사로 현재 탐색 위치를 되돌리지 않는다. 회귀 검사는 반복 완료 신호 이후 모델·선택·스크롤 유지와 실제 파일 추가·삭제 반영을 함께 확인한다.

한 데스크톱 호스트와 여러 클라이언트 형태이다. 각 클라이언트의 업로드는 호스트를 통해 다른 클라이언트의 다음 회차에 전달된다. 모바일은 전경과 허용된 백그라운드 시간에 내려받기와 올리기를 모두 수행하며 Files/Sync 리스너를 만들지 않는다. 백그라운드 실행 시간 만료·로그아웃·계정 만료·컨테이너 변경은 이전 전송과 작업을 취소한다. 돌아온 뒤 현재 연결의 기기 권한으로 재개한다.

모바일에서 연결된 동기화 회차가 실행 중일 때만 화면 자동 잠금을 늦춘다. 완료·연결 해제·비활성 또는 백그라운드 상태에서는 즉시 해제한다. 수동 화면 잠금이나 백그라운드 실행 제한을 우회하지 않는다. OS 설정을 변경하지 않고 앱 범위의 [Apple idle timer](https://developer.apple.com/documentation/uikit/uiapplication/isidletimerdisabled)와 [Android 화면 유지 플래그](https://developer.android.com/develop/background-work/background-tasks/awake/screen-on)를 사용한다.

일반 수동 QR 연결은 Files 접근 권한만 갖는다. 공개 계정 해시나 QR 소지만으로 Models 등 전체 영역을 복제하지 않는다. 자동 페어링의 계정 증명을 통과한 기기와 기존 계정 인증 relay 채널만 전체 동기화에 참여한다. relay 호환 API는 남아 있으며 기본 Devices 경로는 LAN이다.

## 저장과 개인정보 경계

8개 영역의 일반 파일·디렉터리와 삭제 기록을 대상으로 한다. 컨테이너 UUID는 호스트와 모든 클라이언트가 공유한다. 매니페스트의 `localIdentifier`는 기기별 OS 등록을 유지하고 `replicaReady`는 초기 미러 공개 여부를 나타낸다. `.society-sync/` 저널·임시 청크·복구 파일과 replica UUID는 기기마다 유지한다. 로그인 쿠키·계정 스냅샷·seed·페어링 이력은 기존 [보안 그룹 상태](GroupState.md)에 보관하며 Sync에 전달하지 않는다. Finder와 모바일 OS 파일 앱의 공개 드라이브는 계속 `Files/`만 보여준다.

Helper 관측과 SQLite 메시지/ACK는 별도 로컬 런타임 디렉터리에 둔다. Helper를 동기화 컨테이너 내부나 알려진 네트워크 파일 시스템에서 시작할 수 없다. 실행 후 해당 위치가 컨테이너 내부가 되어도 다음 작업에서 기록을 거부한다. 로컬 앱은 Helper로 완료된 컨테이너 파일을 작성하고, 그 파일의 다른 기기 복제는 Sync에 맡긴다.

Sync 자체는 iisacc.com 요청을 생성하지 않는다. 유효한 캐시가 있는 LAN 동기화는 인터넷이 없어도 동작한다. 최초 로그인·권한 준비·변경 및 만료 이벤트 정책은 [AutomaticPairing.md](AutomaticPairing.md)의 계정 수명을 따른다.

## 데이터 보존과 제약

최초 클라이언트 내용은 `.society-sync/detached/<이전 UUID>-<마이그레이션 UUID>/`에 영역별로 보존하며 호스트에 섞지 않는다. SQL 이동 계획·원자적 UUID 채택·중단 복구로 영역 루트는 항상 유지한다. SHA-256 검증 후 파일 단위로 원자적 교체한다. 손상·중단 시 기존 목적지를 유지하고 다음 회차에 재시도한다. 동시 편집의 다른 내용은 충돌 사본으로 남긴다. 삭제와 동시 편집이 충돌하면 파일을 유지한다. 비어 있지 않은 디렉터리를 삭제나 파일로 덮지 않는다. 대소문자·정규화 충돌은 오류로 드러낸다. 동일 호스트가 다른 드라이브를 선택하면 이전 미러·미전송 변경을 보존하고 새 드라이브를 초기 미러링한다. 다른 primary로의 임의 전환은 거부한다.

교체·삭제된 일반 파일은 기기 로컬 `.society-sync/recovery/`에 경로 JSON과 hard link로 남는다. 삭제 기록·복구 자료는 자동 삭제하지 않는다. 이는 파일 단위 복제이며 여러 파일이나 실행 중인 SQLite DB의 트랜잭션 스냅샷이 아니다. 소비 앱은 닫힌 DB·정상 백업·완료된 파일을 저장해야 한다.

파일 적용 구현은 POSIX(macOS/iOS/Android/Linux)와 Windows NT 핸들 기반 구현이다. Windows는 디렉터리 상대 열기·원자적 교체, reparse point 거부, 파일 ID 고정, hard link 복구를 사용한다. 로컬 볼륨의 hard link 지원이 필수이며 예약 이름·ADS·끝의 점/공백은 원본을 보존한 채 오류로 처리한다. Windows 페어링 직후의 Files 목록과 읽기·생성 서비스도 Sync의 같은 파일 접근 구현을 사용한다. OS 제공자의 Windows Files/Dokan 기능과 Sync의 전체 컨테이너 복제는 별도 책임이다. 물리 기기 간 Wi-Fi 연결·모바일 실행 검증 범위는 작업 보고서에 따로 기록한다.

## 검증

SDK의 실제 파일/SQLite 검사와 loopback TLS 검사는 손실 ACK·재시작·증분 전송·동시 편집·삭제·유형 충돌·파일명 충돌·심볼릭 링크·컨테이너 교체·손상 해시·인증 거부를 포함한다. 설치 소비자가 같은 기능을 설치 헤더·라이브러리만으로 검사한다.

Society.Account 통합 검사는 합성 계정 두 개 세션으로 자동 발견→페어링→TLS 연결→Models 700,000바이트 내려받기와 Files 올리기까지 실행하고 실제 바이트를 비교한다. HTTP 요청은 2회의 로그인과 2회의 권한 발급에서 증가하지 않는다. 기존 수동 QR·Files 탐색·모바일 호스트 거부 검사를 유지한다.

`tools/build_ios.py`, iOS presets, `tools/build_android.py`는 각 ABI에서 Container/ServerHost 뒤에 Sync를 설치한다. iOS는 정적 링크하고 Android는 Sync 공유 라이브러리를 APK에 포함한다. 빌드·설치·실행 결과는 `build/responsibility-boundary/REPORT.md`에 기록한다.

2026-09-10 실제 Mac 호스트와 iPhone 클라이언트에서 같은 UUID 채택, 32% 중단 후 이어받기, 초기 미러 완료, 양쪽의 생성·수정·삭제 및 호스트 종료 후 재실행·자동 재연결을 검증했다. iPhone의 8개 영역을 직접 복사해 독립적으로 SHA-256을 계산한 결과 17개 파일 6,941,660,775바이트가 Mac 원본과 일치했다. 인터넷 연결을 차단한 검사는 아니며, Android는 빌드 검증 범위이다. 실기기 증거와 산출물은 로컬 `build/host-mirror/REPORT.md` 및 `evidence.json`에 기록했다.

## 동일 드라이브 화면과 native 제공자

미러가 완료되기 전 앱은 이전 독립 파일 목록·모델 가져오기·native 연결을 열지 않는다. 완료 후 DriveController를 다시 열고 대시보드·파일 모델을 갱신한다. Apple은 기기별 기존 File Provider domain을 유지하고 논리 UUID를 재채택하여 중복 드라이브를 만들지 않는다. 기존 provider anchor는 유지해 이전 파일 제거와 새 파일 추가를 OS에 전달한다. Android는 바뀐 매니페스트를 감지하여 cached DriveStore를 다시 만들고 루트 변경을 알린다. 완료된 미러는 오프라인에도 사용할 수 있다.

Storage의 초기 미러 화면에는 현재 파일의 전송률·준비 단계·오류를 직접 표시한다. 연결된 상태에서 다시 연결하라는 고정 문구를 보여주지 않는다. 파일명과 오류는 일반 텍스트로 렌더링한다.

처음 정한 호스트는 로컬 primary 기록과 서명된 발견 메타데이터로 유지한다. 미러의 primary가 오프라인이어도 다른 독립 드라이브를 자동 승격하지 않는다. 계정 판단과 서버 이벤트 정책은 iiAccountManager에 그대로 위임하며 이 과정에는 새 서버 요청이 없다.

실기기 검증용 `SOCIETY_GROUP_STATE_RUNTIME_PROBE=ON` 빌드는 `SOCIETY_GROUP_STATE_PROBE_STAGE=mirror-inspect|mirror-write|mirror-remove`일 때만 최대 100분간 `Documents/mirror-probe.json`에 UUID·준비/연결 상태와 합성 파일의 해시를 기록한다. 쿠키·계정 ID·토큰·실제 파일 내용은 출력하지 않는다. 쓰기/삭제는 준비된 드라이브의 `Files/Society-mirror-check-<소문자 영숫자 또는 하이픈>.txt` 한 개만 대상으로 하며, 기존 파일을 덮지 않고 삭제 시 예상 합성 내용과 일치해야 한다. 검사 후 기본 빌드 옵션은 OFF로 유지한다.

검사 기록에는 자동 페어링 활성 여부·검증된 인접 기기 수·연결 단계·전경 상태·OS 백그라운드 실행 허가 여부도 포함한다. `SOCIETY_MIRROR_PROBE_RESUME=1`을 명시하면 첫 표본에서 앱의 정상 재개 API를 한 번 호출해 이전 수동 검사에서 저장된 일시정지를 해제한다. 계정 증명과 권한 검사는 동일하게 유지한다. ClientOnlyNetwork 검사는 전경 전송 중 유지와 완료·연결 해제·비활성·백그라운드 해제를 검증한다.

iOS의 선택적 discovery 진단 모드는 UIKit에 화면 유지 상태가 적용되거나 해제된 시점도 기록한다. 수동 잠금·앱 전환과 자동 잠금 정책 적용을 구분하는 검사 근거이다.

## 자동 백그라운드 동기화

macOS 로그인 서비스의 SocietyDaemon --sync는 보안 그룹 캐시를 복원해 GUI가 마지막으로 선택한 컨테이너를 동기화한다. 그룹 상태의 SyncRuntime에는 선택 경로만 보관하며 계정 비밀을 복제하지 않는다. foreground.lock과 owner.lock으로 한 기기의 GUI·데몬·중복 창이 같은 기기 ID를 동시에 연결하지 못하게 한다. GUI가 우선하며, 소유권 변경 때 이전 연결을 정리한 후 새 프로세스가 계정 증명과 TLS를 다시 확인한다. 잠금은 250ms마다 확인하고 비정상 종료된 프로세스의 잠금을 복구한다. 상세 등록은 [SocietyDaemon.md](SocietyDaemon.md)에 있다.

iOS는 전경 연결 시 UIKit beginBackgroundTask를 준비하고 앱 전환 후에도 현재 동기화 회차를 계속한다. 완료 시 즉시 반환하고, expiration handler 또는 실행 허가 실패 시 연결을 정리한다. iOS의 실행 시간은 OS가 정하며 독립 상주 데몬이나 무기한 LAN 실행은 제공하지 않는다. [Apple 실행 시간 연장](https://developer.apple.com/documentation/uikit/extending-your-app-s-background-execution-time)의 계약을 사용한다.

Android는 같은 프로세스의 비공개 dataSync foreground service와 전송 알림·제한된 CPU wake lock을 사용한다. 서비스는 앱이 전경일 때 시작하고 회차 완료·OS 시간 만료·15분 회차 한도에서 중단한다. Android 15의 dataSync 예산 종료도 onTimeout으로 처리하며 강제 종료 후 자체 부활하지 않는다. 허용 시간이 끝나면 저장된 청크와 커서에서 전경 복귀 시 다시 시작한다. [Android foreground service 시작](https://developer.android.com/develop/background-work/services/fgs/launch), [실행 시간 제한](https://developer.android.com/develop/background-work/services/fgs/timeout), [Qt 백그라운드 실행](https://doc.qt.io/qt-6/android-services.html)을 따른다.

Society.SyncOwnership은 GUI 우선권·동시 소유 방지·종료 후 데몬 재획득·경로 보존을 검사한다. Society.ClientOnlyNetwork는 백그라운드 허가·만료·이전 콜백 무효화·연결 유지·전경 복귀를 검사한다. 설치된 SDK 소비자와 물리 기기·서비스 실행 검증은 build/automatic-sync/REPORT.md에 별도로 기록한다.

네이티브 회귀 검사는 동적 Qt의 각 테스트 실행 파일에서 반복되는 QML 정적 플러그인 검색을 생략하며, 실제 QML 로딩은 기존 GUI 테스트로 검사한다. 빌드/실행 검증 시 셸의 DYLD_LIBRARY_PATH·DYLD_FRAMEWORK_PATH·QML_IMPORT_PATH·QML2_IMPORT_PATH가 설치 경로를 덮어쓰지 않도록 제거해야 한다. CMake의 패키지 선택과 실제 로드된 dylib는 별도로 확인한다.

Apple 기기의 파일 해시는 CommonCrypto SHA-256을 사용한다. 같은 Mac의 512MiB 메모리 입력에서 기존 Qt 5,677ms와 CommonCrypto 228ms를 관측했고 결과가 일치했다. 이 수치는 해시 계산 측정이며 네트워크 전송 속도나 모든 기기의 성능 보장은 아니다. 파일 상태가 바뀐 대용량 모델은 첫 식별 정보 교환 전에 전체 해시를 다시 검증하며 이후 동일 상태는 저널의 검증된 해시를 재사용한다.
