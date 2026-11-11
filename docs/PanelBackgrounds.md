# Panel backgrounds

Society의 메인 창과 설정 창은 [공통 ApplicationWindow](https://www.figma.com/design/0GkItQYSNIR0lZ3iJhfJzc?node-id=997-3)의
`Panel/window` 배경을 사용한다. 기존 LVRS 창의 배경색만 이 토큰에 연결하여
앱 악센트가 창 바탕에 섞이지 않게 한다. 녹색 악센트는 버튼과 선택 표시에서
계속 사용한다. LVRS 모듈 자체는 변경하지 않는다.
공통 창 재질의 Dense 75와 Glass 25는 각각 75%·25% 채색과 64·16 논리 px
블러를 유지한다. 현재 레거시 LVRS 앱 창은 불투명 경로이므로 이 문장은 그
창에서 운영체제 배경 블러가 동작한다는 의미가 아니다.

This applies to the global toolbar, Dashboard and Storage sidebars, calendar
month and detail panels, Dashboard file cards, model cards, selected-file
information, Tools cards, model merge and packaging panels, the packaging
sidebar, Environment navigation, launch docks, sections, app and device cards,
the account panel, and the Preferences sidebar. Full-view backgrounds in the
merge, packaging, and Environment views are transparent as well.

Panel outlines, layout, content previews, icon badges, primary actions,
selection, focus, and hover feedback retain their existing roles. Model and
tool cards use transparent resting backgrounds and retain their interactive
state colors. File-card hover feedback remains visible in the LVRS outline.
Table headers and alternating rows, input fields, menus, and the LVRS sheet
material continue to identify their controls and interaction states.

## 패널 외곽선

기존 캘린더 월·상세 패널, Environment의 정보 섹션과 계정 패널,
모델 병합·패키징 작업 패널은 공통 `App/Society/PanelOutline.qml`의
`panelBorderColor` 스타일 토큰을 외곽선으로 사용한다. 토큰은 불투명한
`#0C0C0C`이다. 표면은 그대로 투명하며
기존 패널의 선 두께와 모서리 반경을 유지한다. 카드와 버튼의 선택·포커스·
호버 표시, 달력의 셀 구분선과 대화상자 테두리는 각자의 기존 색을 사용한다.
`SocietyDriveTests panelsRevealApplicationWindowBackground`는 데스크톱과
모바일에서 패널의 투명도와 외곽선이 이 토큰을 사용하는지 함께 검사한다.
Society Figma의 기존 패널 경계는 로컬 의미 변수 `Panel/Border`를 사용하며,
이 변수는 같은 `#0C0C0C` 값을 가진 `Color/Neutral/12`에 연결된다.
앱과 Figma 변수는 별도 정의이므로 변경 시 두 값을 함께 갱신해야 한다.

Society Figma의 공통 ApplicationWindow 인스턴스에서도 기존 2% 악센트 레이어를
숨겼다. 중첩 패널과 텍스트·배치·아이콘 및 프로토타입 연결은 유지한다.

`SocietyDriveTests windowUsesFrameworkChromeAndContent`는 실제 메인 창의 프레임과
콘텐츠 계약을 검사한다. 과거 66% 바탕·4% 악센트 및 Cocoa 블러 검사 기록은 현재
불투명 LVRS 창의 동작 증거가 아니다. Set
`SOCIETY_TEST_APPLICATION_RENDERING=1` for the application's normal graphics
bootstrap and render-quality defaults when running this test with Cocoa/Metal.
The native toolbar resize test waits for a presented frame before sending a
drag, so logical width changes cannot race the rendered hit geometry.
An offscreen run covers the material
contract and does not establish native window translucency.

`SocietyDriveTests panelsRevealApplicationWindowBackground` checks desktop and
mobile layouts, changes the window color twice, and verifies that the toolbar,
sidebars, calendar, Environment view, account panel, and merge/packaging panels
remain transparent. `globalToolbarMatchesFigmaAndRoutesActions` retains its
geometry and routing checks and now requires a transparent toolbar surface.
The calendar, sidebar, card, Environment, Preferences, and tool interaction
tests continue to cover layout and control behavior.

Set `SOCIETY_PANEL_CAPTURE_DIR` to an existing directory under `build/` to
capture the Dashboard, Storage, Environment, Tools, merge, and packaging views
at both test sizes. This panel test deliberately disables native blur and uses
an opaque tint while alternating synthetic window colors; its screenshots
verify panel transparency, not the application's default window material.
The mobile captures exercise responsive QML on the host;
they do not establish physical-device execution.
