# Storage 사이드바

[Figma 39:1275](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=39-1275)의 사이드바를 `src/App/Drive/StorageSidebar.qml`로 구성한다. 기존 LVRS Label·ListItem.Navigation·VStack과 Qt Flickable을 사용하며 추가 외부 의존성은 없다.

- 폭 228 px, 안쪽 여백 12 px, 항목 폭 204 px, 행 높이 32 px, 간격 8 px이다. 그룹 제목은 Caption 11 px, 항목은 Body 13 px, 아이콘 영역은 18 px이다. 배경은 panelBackground04, 선택 배경은 panelBackground12, 맨 아래 1 px 구분선은 panelBackground08이다.
- My storage → Other devices → Guild → Organization 순서이다. My storage의 순서는 Files, Photos, Asset Library, Generation History, Models, Thinking Space, Forked, Published, Deleted이다.
- Storage 탭은 데스크톱·모바일 모두 바로 Files 최상위 폴더를 연다. 탭 재선택이나 다른 탭에서 돌아올 때도 Files로 이동하며, 기존 Overview 영역 그리드는 제공하지 않는다. 다른 영역은 사이드바·모바일 탐색 시트로 선택한다. Dashboard의 특정 영역·폴더 바로가기는 지정한 목적지를 유지한다. Storage에서 뒤로 이동해 컨테이너 루트에 도달하거나 초기 동기화가 완료되면 Files를 표시한다.
- 짧은 창과 긴 목록은 사이드바 안에서 세로 스크롤한다. 휠·트랙패드·드래그·스크롤바를 지원하며 키보드로 초점을 이동하면 해당 행을 보이게 한다. 목록 스크롤은 파일·모델 콘텐츠 스크롤과 독립적이다. 760 px 미만 모바일에서는 같은 항목을 탐색 시트로 제공한다.

## 탐색 객체와 실제 데이터

`StorageNavigation`은 QML에 노출되는 탐색 객체다. 화면과 공급자 사이에서 표시용 목록을 만들고 `activate(group, id)`를 실제 목록에 있는 식별자로 제한한다. `sections`, `devices`, `guilds`, `organizations`는 `{id, name, icon}` 목록이며, 로컬 영역은 SDK의 영역 키·디렉터리 이름을 사용한다. 선택 상태 `selectedSection`은 현재 디렉터리에 따라 바뀌므로 번역된 표시 이름이나 기기 이름으로 라우팅하지 않는다. Models 하위 폴더에서도 Models 선택을 유지한다.

Main은 기존 AccountController·NetworkDriveController의 변경 신호를 받아 `replaceDevices(accountId, currentDeviceId, remembered, nearby, hosts)`로 전체 스냅샷을 한 번에 전달한다. 계정과 기기 목록의 개별 QML 바인딩 순서에 의존하지 않는다. `rememberedDevices`에는 해당 계정의 저장된 페어링 이력, `nearbyDevices`에는 검증되거나 이미 연결된 주변 기기, `hosts`에는 현재 Files를 제공하는 인증된 호스트가 들어간다. 기기 ID로 병합하고 현재 기기는 제외한다. 이름 변경은 최신 탐색·연결 정보를 우선하며 같은 이름의 서로 다른 기기는 유지한다. Figma의 데스크톱·휴대폰·태블릿 순서를 따르고 같은 종류는 이름과 ID 순으로 안정적으로 정렬한다. desktop·phone·tablet 아이콘은 Figma가 사용한 LVRS screens·iPhoneDevice·nodesdataColumn이다.

기기가 LAN에서 사라져도 저장된 이력은 Offline으로 남는다. 화면에 노출하는 기기 이력은 ID·이름·종류 등 기존 페어링 기록이며 계정 인증 정보는 포함하지 않는다. `AccountController.rememberedDevices`는 로그아웃하면 빈 목록이다. 페어링 이력 추가는 전용 `rememberedDevicesChanged` 신호로 알리며, 연결 처리 도중 계정·탐색 갱신을 재진입시키지 않는다. 기기 선택은 기존 NetworkDevices 패널을 해당 기기 이름으로 열고 연결된 호스트이면 Files를 조회한다. 아직 Files를 제공하지 않는 기기는 연결 안내를 표시하며 이전에 열었던 다른 기기의 파일을 표시하지 않는다. 선택한 기기가 다시 호스트 목록에 나타나면 해당 기기를 조회한다. 연결·다운로드·자동 동기화는 기존 객체가 담당한다.

Guild·Organization 목록은 `replaceMemberships(accountId, guilds, organizations)`로 계정에 속한 전체 `{id, name}` 스냅샷을 받는다. 각각의 ID 영역은 분리하며 빈 ID·이름 및 중복 ID를 제외한다. 계정 전환·로그아웃은 목록을 비우고 이전 계정의 늦은 결과를 거부한다. 동일한 스냅샷은 갱신 신호를 보내지 않아 행과 스크롤을 유지한다. 선택은 `workspaceRequested(kind, id, name)`으로 전달한다.

현재 계정 SDK에는 Guild·Organization 저장소 멤버십 조회 서비스가 없다. 기본 목록은 비어 있으며 No guilds·No organizations를 표시한다. Figma의 Guild 1 등의 예시 항목은 테스트 데이터에만 사용한다. 저자 프로필의 organization은 저장소 멤버십·접근 권한으로 취급하지 않는다. 별도 공급자가 목록을 제공하더라도 저장소 서비스가 연결되기 전에는 선택한 이름의 안내창을 연다. 멤버십 서버·공유 저장소·접근 권한 정책은 이 UI 변경에 포함되지 않는다.

## 검증

`SocietyDriveTests storageTabOpensFilesDirectly`는 실제 데스크톱·모바일 탭 클릭, 탭 재선택·왕복, Files 선택 표시, Overview 제거, 특정 영역 바로가기, 루트 복귀와 초기 동기화 완료 후 Files 표시를 검사한다.

`SocietyDriveTests storageNavigationUsesStableIdentitiesAndAccountScopedMemberships`는 ID 병합·자기 기기 제외·미검증 기기 제외·오프라인 유지·계정 격리·변경 없는 스냅샷·선택 라우팅을 검사한다. `sidebarMatchesFigmaAndRoutesDynamicTargets`는 실제 Main에서 네 그룹의 좌표·크기·아이콘·기기 선택·빈 상태·워크스페이스 선택·짧은 창 스크롤·키보드 초점·모바일 숨김을 확인한다. `SOCIETY_SIDEBAR_SCREENSHOT_PATH`를 설정하면 합성 기기·멤버십을 사용하는 검증 화면을 저장한다. `Society.Account`는 기기 이력의 즉시 알림과 로그아웃 후 빈 목록을 확인한다. `Society.ClientOnlyNetwork`의 `deviceSelectionWaitsForTransfersAndTracksHostAvailability`는 두 테스트 호스트를 사용해 진행 중인 조회 이후 새 기기 선택, 호스트 재연결과 오프라인 기기의 이전 파일 숨김을 확인한다. 기존 Models·Files·동기화 회귀 테스트와 전체 빌드·QML 검사를 함께 실행한다.

Photos의 안정 키는 `photos`이며 `Society/Photos/`로 직접 이동한다. 모바일 탐색 시트에서도 Files와 같은 계층이다.

최상위 Photos 행 추가로 하위 그룹 제목과 Models 행이 기존 기준에서 40 px 아래로 이동한다. 각 행의 32 px 높이·8 px 간격과 사이드바 스크롤은 유지한다.

모바일에서도 같은 `StorageNavigation`의 9개 영역·기기·Guild·Organization 항목을 사용한다. 760 px 미만에서는 상단 탐색 버튼으로 여는 LVRS 시트에 같은 `StorageSidebar`를 배치하고, 항목을 탭하면 시트를 닫아 본문을 표시한다. 760 px 이상에서는 사이드바를 유지하며 터치 행 높이는 44 px이다. 데스크톱의 32 px 행과 기존 위치는 유지한다. Up·breadcrumb 표시줄은 모든 Storage 영역에서 제거한다. 모바일 가져오기·컨테이너 선택·OS 연결은 앱 상단 바의 `Storage actions` 버튼이 여는 시트에서 실행한다. 상세 기준과 검증은 [MobileViews.md](MobileViews.md)에 기록한다.
