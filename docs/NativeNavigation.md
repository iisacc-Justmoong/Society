# Society navigation segment

The desktop navigation follows [Society's nine-item Figma segment](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=940-8700): Environment, Tasks, Library, Space, Apps, Service, Community, Learning and Hosting, in that order. LVUI owns the segmented control, icon buttons, geometry and selection colors; Society supplies only destinations and the active selection. The Society accent is green.

The Environment segment opens Workspace (Dashboard), Tasks opens the existing Tools view, Library opens Storage, and Space opens the native Space surface. Apps opens the [native Apps view](NativeApps.md), initially selecting All apps and using the existing Environment catalogue as its data source. Hosting opens the existing Environment hosting information. Service, Community and Learning report that the feature is not connected and preserve the current selection. The existing remote-device Browse view remains available through device navigation and is represented by the Environment selection. The selection follows navigation initiated elsewhere in the application as well as a segment click.

`NativeNavigation.lvui` generates iiGUI widgets and `NativeNavigationViewModel` owns their destination requests and selected state. Eight vector icons are exports of the corresponding icons in the reference Figma segment; the Environment icon comes from the LVUI catalog. Every button uses LVUI's 22 × 22 button and 18 × 18 icon defaults. The group is 214 × 22, has no border or outside padding, and its buttons are separated by 2 px.

The existing application shell hosts the generated view through `NativeNavigationSurface` and iiGUI's `PaintView`; `QtPaintDevice` translates the resulting drawing commands to the existing window's surface. It does not introduce application-specific button or color tokens. Input is sent to iiGUI, and the host exposes the same nine named and selectable buttons to the platform accessibility interface. Each icon button uses LVUI’s `pointerFocusVisible: false`. A mouse click displays the selected background when the shell confirms the destination. Tab focus and keyboard activation display the focus ring on the focused segment. A queued destination request prevents a page change from reentering an active iiGUI input dispatch.

`LegacyNavigationBridge` connects this surface to the current shell without modifying the legacy QML view. It uses the existing toolbar viewport for placement and translates destination requests to the shell's existing navigation signals. The old segment group remains only as a hidden, disabled layout dependency of that shell; it cannot receive input or be presented by accessibility. The bridge is installed once per window and falls back to the old group if the LVUI surface cannot initialize. It follows external page changes, resolves Apps and Hosting after the asynchronous Environment loader finishes, and keeps the Apps selection synchronized with the Environment subpage. This bridge is temporary and should be removed when the surrounding shell has its own LVUI view.

Building the application requires an LVUI native package and an iiGUI package that includes `iiGUI/PaintView.h`. CMake defaults to the packages under `~/.local/SDK` and reports an explicit error if the selected iiGUI is too old. An alternate compatible package can be selected with `SOCIETY_NAV_IIGUI_PREFIX` and its `iiGUI_DIR`; `SOCIETY_NAV_LVUI_DIR` supplies the LVUI package hint. These navigation settings are separate from the existing native model chooser and file helper dependency settings. When changing a cached LVUI package, update `LVUI_DIR` as well.

The desktop segment is replaced as one unit. The existing mobile bottom navigation remains a separate layout. Parts of the Society shell still use QML and LVRS; this change does not represent a complete application migration to LVUI. `Society.NativeNavigationFocus` checks pointer presses, page requests, selection, and keyboard focus using the generated view’s iiGUI paint frames. `Society.NativeWorkspaceSurface` and `Society.NativeAppsSurface` check shell routing and the Apps data adapters. `Society.ToolbarSegmentAppearance` and `Society.TasksNavigation` cover the app shell's selection and routing. An installed application or a real OS window requires separate verification.

미구현 화면 등의 안내는 기존 셸 요청을 받아 LVUI의 단일 액션 Alert와
iiGUI 위젯으로 표시한다. 제목·본문·버튼 문구는 기존 요청에서 전달한다.
네이티브 표면을 준비할 수 없으면 기존 셸의 안내 경로가 유지된다.

현재 Qt 그리기 어댑터는 Figma의 유리 블러와 그림자 레이어를 지원하지 않는다.
공통 Alert 구분선 수정은 최신 LVUI와 iiGUI 패키지로 다시 빌드할 때 반영된다.
macOS 번들에는 셸과 네이티브 보조 도구가 선택한 LVUI 라이브러리를 우선
배치한다. `Society.NativeLvuiPackage`와 배포 단계는 번들 파일의 Mach-O UUID를
선택한 SDK와 비교하여 이전 버전이 남지 않았는지 확인한다.
