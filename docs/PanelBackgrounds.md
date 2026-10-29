# Panel backgrounds

Society panels expose the default `LV.ApplicationWindow` background. They do not
paint their own resting background or replace the window's material settings.
The LVRS default follows
[Figma ApplicationWindow 997:3](https://www.figma.com/design/0GkItQYSNIR0lZ3iJhfJzc?node-id=997-3):
`#141414` neutral paint at 66% opacity with a uniform 4% app accent overlay.
Society supplies green `#57965C` through `primaryColor`; Dreamscapes supplies
blue `#0A84FF`. The default ApplicationWindow has no radial gradients.
The overlay follows live accent changes and auxiliary window accents through
LVRS. Its 4% opacity applies to the accent layer, while content remains opaque.
On supported desktop platforms, `backgroundBlurEnabled` also remains at its
LVRS default. On macOS, LVRS places an active, behind-window
`NSVisualEffectView` under the transparent Qt surface. The tint opacity applies
to the background alone; the window, text, and controls retain full opacity.
The native blur kernel is controlled by AppKit. LVRS's material blur value is
64 logical pixels, not a private native blur-radius setting.

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

The editable [Society Figma file](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society)
preserves transparent nested panels across Dashboard, Tools, Storage, and Environment,
including their existing prototypes and local default card components. Nested
panel fills are empty. Desktop window previews still contain the earlier eight-gradient snapshot.
The current framework background follows the newer LVRS Figma 997:3
66% neutral / 4% accent contract; this code change does not edit those product
Figma previews.
The opacity belongs to the paint, not the entire
frame, so text and controls remain legible. The mobile preview retains its opaque platform base.
Existing library instances, text, layout, icons, and prototype links are retained.

`SocietyDriveTests windowUsesFrameworkChromeAndContent` loads Society's actual
main window and checks its default tint, blur support and enabled state, and
full foreground opacity. A native Cocoa run additionally checks active blur,
a transparent Qt window color, and the actual nonopaque AppKit window with
one active behind-window backdrop. It also checks that the rendered resting
background remains translucent, and compares edge and center pixels to require a uniform green 4% overlay.
The neutral base is 66%; overlay compositing produces about 67.36% alpha
before an opaque platform fallback. Set
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
