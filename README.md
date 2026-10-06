# Society

`Files/`  does not provide default files or folders. `Documents` , `Audios` , `3D objects`  are also standard user folders and do not apply automatic creation, recovery, or name reservation. One-time cleanup of existing empty default folders and preservation of user content follow [Files management documentation](docs/Files.md).

App icons are generated per platform in `resources/Appicon/Artboard 1.png` . macOS · iOS · Android · Windows · Linux · WebAssembly  packaging and Qt  runtime are connected, and regeneration, specification, and verification methods are in [app icons documentation](resources/Appicon/README.md).

Society's main window and Preferences use `LV.ApplicationWindow`  and `LV.Theme.accentGreen`  ( `#57965C` ) Primary. The entire window is filled with `#0B0B0B`  nearly black without gradient, and fill opacity is 50%. The window background is separated from Primary, and opacity for buttons and selected states is maintained at the app accent color. 64px  material blur and macOS  native background blur are maintained, and opacity for text and buttons is not reduced. The background of the dashboard list is kept transparent to reveal the window material. `Society.Drive`  verifies color, opacity, and gradient removal for both windows and existing window behavior.

The latest LVRS  package must be used. The current Workspace build shares the framework installed in `SDK/LVRS/build/material-runtime`  between two apps, and Society's `build/`  is configured by specifying `lib/cmake/LVRS`  as `LVRS_DIR`  .

Android currently uses a package built from the same LVRS source at `SDK/LVRS/build/society-android` using the same Qt/NDK and installed at `build/android/installed/LVRS`. Updating only the desktop package does not reflect new window properties on mobile. After changing the package, rebuild the APK and verify up to the actual QML root loading.

Login recovery information and pairing authentication, device history, and auto-connect settings are encrypted and stored in the [group container](docs/GroupState.md), including mobile.

The login and sign-up screens owned by the iiAccountManager SDK are called from desktop, iPhone, and Android. From the common account screen, log in directly with the iisacc.com email and password. Sessions are kept in the app owner's secure storage to automatically recover from the next execution and update the entire account object. Passwords are not stored, and recovery is disabled upon logout, server expiration, or revocation. Refer to the [account connection contract](docs/Account.md).

Search for iisacc devices of the same Society account via BLE and LAN, verify the certificate issued by the account server, and add to the automatic queue and connect. Other desktops, mobile phones, and tablets connect to one desktop host in sequence, and failed devices are automatically retried. BLE exchanges small connection information, and files are transmitted via Wi-Fi/LAN TCP /TLS. Apple Bonjour and Android NSD discovery are also maintained. If [self-hosted web server and NAS](docs/SelfHosting.md)are set up, devices of the same account can be connected over the internet, and the entire container is synchronized. Refer to [automatic connection contract](docs/AutomaticPairing.md)and [manual invitation QR procedure](docs/Pairing.md).

<a id="iisacc-계정"></a>

## iisacc account

A common LVRS account screen is provided for all Society platforms. Log in directly with just email and password from the desktop top account icon or the mobile Sign in button. The account panel, Devices, and iiSocietyHelper share the same iiAccountManager object. Independent limits of PC 2 desktops, 2 tablets, and 2 mobiles are used, and no relay address is required for login. Refer to the [authentication flow, device identification, and verification scope](docs/Account.md).

On desktop, the [Figma dashboard](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=18-14)is placed as `SocietyView` in the `content` slot of `LV.ApplicationWindow`, using the default LVRS window frame and controls. The top tabs are ordered **Dashboard → Tools → Storage → Browse → Environment**. **Tools**provides the [model addition/subtraction merge tool](docs/ModelMerge.md)based on iiLocalDiffusion; base and additional checkpoints/LoRA are selected from the dropdown and context menu in the container's `Models/`. It provides weights, output, cache, the Python environment, and configuration validation. **Storage**opens the existing Society drive. **Browse**browses files on connected devices. **Environment**displays [Overview, Devices, Apps, and four app collections](docs/Environment.md). The device-pairing modal opens through Environment → Devices → Add device. Merge settings, the current folder, and prompts persist when switching tabs. Actual recent files and generation history, screen composition, and the scope of behavior are documented in the [Dashboard documentation](docs/Dashboard.md). The Storage sidebar has four groups: My storage, Other devices, Guild, and Organization. Actual device lists, per-account browsing objects, empty states, and scrolling behavior are documented in the [Storage navigation documentation](docs/StorageNavigation.md). Storage → Photos [connects bidirectionally to the device photo library](docs/Photos.md)and provides photo and video aliases, directly saved previews, and authenticated original transfers.

App title, drive home, and path display, and the OS drive display name are `Society`. iiSocietyContainer 0.9.1 reads existing drives with previous names while maintaining UUID and files. Re-registering the original at macOS updates the display name of the same File Provider domain. `Society.Drive` validates app title, home, and path display.

If Finder's `~/Library/CloudStorage/` replica or the interior of existing Society container is selected as the container original, an error is displayed and the original and common storage settings are maintained. This validation is applied to the creation/opening boundary of iiSocietyContainer and is also passed to Helper consumer apps. `Society.Drive`'s `rejectsPublicFilesAsANewContainer` validates that no new area or manifest is created even if `Files/` is incorrectly selected.

iiSocietyHelper   0.4.0's `societyHelper.fileSystem` provides Society original's 9 areas as general file system paths. SDK iiSocietyContainer   0.8.0 is reused so iOS build tools configure Helper after Container installation and specify Container packages of the same ABI. `Society.IosBuildContract` validates this sequence and package path. File access itself and iOS signature and runtime permission verification are separated.

[SocietyDaemon](docs/SocietyDaemon.md)is a service that receives and holds Helper's data independently of the host window. The host's `societyInbox` object receives cumulative data and new data after re-execution. macOS login service registration, storage guarantee, and platform scope follow separate documentation.

When the app starts, iiSocietyHelper   0.3.1's `Helper` is run as `com.iisacc.society`. It observes execution instances bidirectionally with other Helpers on the same device, shares foreground and background states, and unregisters itself when the app ends. QML provides `societyHelper.observedApplications` and observation events. This state is recorded on the device's local location, separated from Society drive content. `SocietyRuntime` retries start failure and, when iOS stops, cleans up the connection and restores Helper, receiver, and inbox together. Detailed features and validation scope follow [iOS implementation document](docs/iOS.md).

`Society.Dependencies` validates bidirectional discovery between the actual app executable and the test Helper. `SOCIETY_HELPER_DIRECTORY` and repository settings are isolated by test-specific `build/` paths. SDK   0.3.1 installation paths are specified as `iiSocietyHelper_DIR`. The current Workspace validation is `SDK/iiSocietyHelper/build/install/lib/cmake/iiSocietyHelper`.

Society is the common original storage for iisacc apps. Opening a container registers its path and UUID through `SharedStorage::setDefaultContainer()` in iiSocietyContainer 0.7.0. The next launch reopens the registered container; consumer apps such as Dreamscapes use `SharedStorage::open()` to access the same `Models/`, `Asset Library/`, and `Generation History/`. Models dropped into Society need not be duplicated for each app. Across devices, Society instances synchronize models, and each Dreamscapes reads its own device's Society container to generate images. Society does not receive remote generation requests and launch Dreamscapes. Desktop Society creates an ordinary Society directory with nine direct section folders matching Storage. It saves that root and UUID without a disk image; Open in Finder opens the full directory tree. Existing images remain available for recovery and migration. Mobile File Provider exposes `Files/` as the public root. On iOS, the same App Group is the common original storage, and other apps do not register duplicate File Providers. Helper and Container handle this local-sharing contract; a separate iiSocietySync handles per-account synchronization with Society on other devices.

The `Society.Drive` test checks whether the UUID of the common repository matches after container selection and whether the new controller reopens the same drive. Configuration files are separated by `build/` temporary paths per test.

It is a Society drive navigation app using Qt Quick and LVRS. It receives the disk storage location, creates a separate APFS disk, and displays 9 areas within it. The default window size is 1120 × 720, and the minimum size is 360 × 320.

<a id="컨테이너-탐색"></a>

## Container navigation

If the container settings are missing or the saved path cannot be opened, LVRS provides a role for the container and a file dialog for creating a new disk and assigning an existing disk in the onboarding screen. If verification succeeds, it remembers the location and enters the dashboard. On mobile, it must complete synchronization with the host verified by the account and the current connection before entering, and if the connection is unavailable, it provides login, nearby hosts, and server settings. It can also reattempt the saved location after reconnecting the external storage. [Onboarding behavior and verification](docs/Onboarding.md)are referenced.

In `Create a new disk…`, select the folder to store disk images. In macOS, create and mount the actual `Society.societycontainer` at that location and configure the container on the APFS disk. Existing files in the selected folder are preserved. Images already created Society are restored with the same UUID and data. `--container <absolute path>` is a compatible path that explicitly opens an already configured logical container.

The start screen displays Asset Library, Deleted, Files, Forked, Generation History, Photos, Models, Published, Thinking Space. Clicking an area moves to the file grid. A wide window also displays the area sidebar. Navigate using folder double-click, Enter, `Up`, path button, and drive root button, and open files with the default connection app. Navigation outside the container or to an unclassified root folder is denied.

Generation History displays all completed generated images from all apps as a single list. Image files are automatically saved directly below the area without creating app-specific or task-specific folders. This screen displays only PNG · JPEG · WebP and does not allow subfolder navigation. Since generated images are not Assets, they are not added to the Asset Library merely by generation. Queue and execution status are managed in each app's memory and are not saved to Society. Generated data that requires file paths, such as task materials and cache used outside Society, is cleaned up after use. Generated data permanently saved to Society is only the completed image file. In `Society.Drive`, image filtering and denial of subfolder navigation are verified for both desktop and mobile widths. If the path and filter change, FolderListModel is regenerated together to ensure no asynchronous list from the previous area remains. `Society.Gui` also checks for recursive switching between the image list and the regular folder list. No separate trash, publish, or archive policies are assigned to other areas.

The new macOS onboarding completes disk creation and mounting before opening the dashboard. It opens a separate `Open in Finder` Society volume, which can also be verified as a APFS disk in Disk Utility. It does not treat regular folders as drives or register new File Provider copies. Since it saves the original image path, it restores the same container after extraction and restart, even if the mount name changes. Existing folder save settings guide the new disk settings without automatically moving data. Managed storage on mobile, existing File Provider, Dokan, and FUSE adapters are kept separately. The Windows · Linux image generation backend does not exist yet, and new generation requests show an explicit error. Container synchronization within authenticated Society is handled by iiSocietySync.

<a id="모델-파일-드래그-앤-드롭"></a>

## Drag and drop model file

After opening the container, dropping Society or `.safetensor` or `.safetensors` files into the window copies them to the `Models/<type>/` of the original container, regardless of the current browsing area. Extensions are case-insensitive. It shows the destination during drag and moves to the `Models` screen upon completion. It does not add models to `Files/Models/` or public drives in Finder or the Files app.

- Preserves the original file. If `weights.safetensor` already exists, it chooses an unused name like `weights (1).safetensor`, and does not overwrite existing files, folders, or links.
- Can import multiple models at once. Repeated paths in the same drop are processed only once, and files already under the `Models/` of that container are not copied again.
- Drops mixed with other extensions or web URLs are rejected entirely. If actual read/write errors occur, failed files are displayed, and completed models are held.
- Large files are copied to the 1 MiB buffer in the worker thread. Overall progress, results, and errors reflecting the ready state and per-file progress are displayed at the bottom of the window. `Cancel` stops remaining tasks and cleans up incomplete temporary files. The area can be navigated even during copying, and the container selection button is locked.
- Temporary files are created in private `Models/`, written completely, and then renamed atomically. The drive ID and section boundaries are checked before and after copying. No writes are made to invalid containers or symbolic links that connect `Models` to `Files`.

External import targets the above two extensions. iiSocietyContainer 0.11.0's `ModelStore` determines headers, settings, and metadata, organizing them into 23 types, and stores unclear models in `Other/`. When opening a container, existing models are also automatically classified. It does not execute models or check full tensor integrity and inference compatibility. [Models management document](docs/Models.md)explains the type list and organization, preservation, and reference contracts.

Desktop input uses the existing Qt Quick
[DropArea](https://doc.qt.io/qt-6.8/qml-qtquick-droparea.html)and
[copy drop action](https://doc.qt.io/qt-6.8/qml-qtquick-dragevent.html). iOS / iPadOS is in the native view of the Qt window
[UIDropInteraction](https://developer.apple.com/documentation/uikit/uidropinteraction)and receives the `NSItemProvider` of the file app. Provider file access is
Maintained as [loadInPlaceFileRepresentation](https://developer.apple.com/documentation/foundation/nsitemprovider/loadinplacefilerepresentation(fortypeidentifier:completionhandler:)) and `NSFileCoordinator`, and executes the same model copy logic within that access scope. It does not redundantly create separate temporary copies in the app. It ignores provider callbacks that arrive late after cancellation. It does not add external packages and uses existing Qt licenses and Apple default SDK API.

<a id="파일-그리드"></a>

## File grid

Storage's sidebar is the 228 px navigation list from Figma. The default Models screen displays Image, Video, Audio, and Language models in separate horizontally scrolling card lists. It uses the bounded metadata queries in iiSocietyContainer 0.11.1. Existing subfolders are explored through the path at the bottom of the screen and card menus. Refer to the [model document](docs/Models.md)for dimension, classification, update, and verification contracts.

`src/App/FileGridView.qml` receives the absolute path of the local folder as a required `string path` argument. If no container is selected or it is at the drive root, it passes an empty path and hides the file list. When opening an area, it passes the corresponding actual directory. It can also be used as an independent component.

```qml
FileGridView {
    anchors.fill: parent
    path: ""
}
```

An empty string is unspecified and does not create a file list model or open the current working folder. In an independent component, it displays `No folder selected` and 0 items. To display other folders, it passes the corresponding absolute path to `path`. It does not receive URL, relative paths, or `~` extensions. Paths containing spaces, Hangul, `#`, and `%` are converted to `QUrl::fromLocalFile()`. Non-existent folders, file paths, and unreadable folders are displayed as error states.

- The default is folder-first, case-insensitive name sorting, and excludes hidden items and `.` / `..`.
- `chronological: true` maintains folder priority while arranging files from the oldest modification time to the newest. It is used in the Files area of Storage and its subfolders, and starts from the bottom where the newest file is located once asynchronous list retrieval and layout are complete. The sort criterion is the file's modification time.
- PNG / JPEG / WebP / GIF / BMP /SVG are displayed as Qt image loaders with asynchronous previews. Other files and images that failed to load use the LVRS file icon, and folders use the LVRS folder icon. Content previews for videos, PDF, and documents are not yet provided.
- Supports single selection by click, movement by arrow keys, and selection cancellation by Escape. Double-click or Enter emits a `activated(string path, bool isDirectory)` signal. The app connects this signal to DriveController boundary validation and folder navigation/file opening.
- `count` and `selectedPath` are read-only. On `path` change, selection is cleared and the initial scroll position matching the sort method is applied. In auto-refresh, the path and scroll position of the selected file are restored, and if it is at the bottom of the time-sorted list, it follows the newly added newest file. Even if the file count does not change but only the modification time changes causing re-sorting, the selection of the same file is maintained.
- `heading` is the display title, with the default value being `Files`, and the app passes the current area name.
- Long file names are limited to a maximum of two lines, and the grid uses vertical scrolling and column re-arrangement based on window size.

<a id="의존성"></a>

## dependencies

- CMake  3.31 or more, Ninja, C++23 compiler
- Qt 6.8.3: Quick, QuickControls2, Qt .labs.folderlistmodel, test Test module
- Installed LVRS CMake package and QML module
- iiSocietyHelper 0.7.1, iiSocietySync 0.7.0, iiServerHost 0.6.0 or above
- iiSocietyContainer 0.14.0's `DiskImage` API; macOS's APFS disk image tool

Reuses existing Qt /LVRS's window, font, theme, app bootstrap. File listing, sorting, folder change detection is included in Qt
Uses [FolderListModel](https://doc.qt.io/qt-6.8/qml-qt-labs-folderlistmodel-folderlistmodel.html). Since the Qt Quick module is already installed, no additional libraries or services are introduced. Reuses the implementation provided by Qt and the existing Qt license scope,
Follow [Qt Quick's license notice](https://doc.qt.io/qt-6.8/qtquick-index.html#licenses-and-attributions). The `Qt.labs` API does not guarantee future compatibility, so the current Qt 6.8.3 fix is maintained, and grid integration tests are performed upon version changes. `DirectoryLocation` is responsible only for path verification. Drive ID, area placement, and path determination are delegated to iiSocietyContainer. The app uses existing Qt's QProcess, QDesktopServices, FolderDialog, so no additional dependencies are introduced. The current macOS preset uses Qt `/Volumes/Storage/Qt/6.8.3/macos` and LVRS `~/.local/SDK/LVRS` installers. At execution, the library path provided by the LVRS package and the explicit QML import path are used, so separate `DYLD_LIBRARY_PATH` or `QML2_IMPORT_PATH` configuration is not required.

macOS's `Society` build copies Qt · LVRS · SDK ·native library of GUI and embedded daemon to the app bundle respectively, links them via relative paths, and signs them. `build/bin/Society.app` must be runnable directly from Finder, and post-build checks remove development library environment variables and run the main body and daemon in actual execution. `Society.MacRuntimeLaunch` also validates QML window loading in the container unset state. Runtime such as OpenSSL installed in a separate location can specify a directory list to `SOCIETY_MAC_RUNTIME_SEARCH_PATHS`. Missing libraries are treated as build errors

On desktop, the following 13 SDKs are found via `find_package(... CONFIG REQUIRED)`, and the `PackageName::PackageName` CMake target of the corresponding SDK is linked directly to `Society`. The default search path is `~/.local/SDK/<package name>`, and if even one is missing, configuration fails

|Required SDK|Public API checked in consumer tests|
| --- | --- |
| iiFileProvider | `helloWorld()` |
| iiGeneralDocument |Document generation and metadata access|
| iiLicenseManager |`LicenseClient` Meta object and status enum|
| iiLocalDiffusion |Query operation device name|
| iiLocalLLM | `helloWorld()` |
| iiServerHost |LAN   TLS ·Account-based self-server connection·Remote  WebSocket  request/response|
| iiSharedCanvas |Query vector asset ID and type|
| iiSocietyContainer |`helloWorld()` ,  `SocietyDrive`  create·area·persistence  ID|
| iiSocietyHelper |`Helper`  Actual app cross-observation and existing  `helloWorld()`  compatibility|
| iiSocietySync |`Controller` ,  `RemoteFiles` , actual bidirectional container transfer|
| iiUpdateManager |`UpdateManager` Meta object and status enum|
| iiVoiceOver | `helloWorld()` |
| iiWhatsNew | `helloWorld()` |

The `iiLisenceManager`  of the request is connected to the actual installed name `iiLicenseManager` . Some SDKs require Qt 6.8.3  exactly, so Society also uses the same Qt  version.  `iiXml` , `iiHtmlBlock` , `iiPaintEngine`  and other indirect dependencies are declared by each SDK  package.

File grid uses Qt's local file list and image loader. For  Society  connections verified by account, it executes  iiSocietySync   0.7.0  host permission-based Namespace replication. Host finalizes revision·change journal and client submits changes then receives finalization result. Mac Storage· NAS · S3  providers fall under this permission and Mac's local record is maintained even offline. Responsibility separation·conflict·recovery contract follows  [sync document](docs/Synchronization.md).  iiSocietyContainer 's new drive API is used in app and  `Society.Drive`  tests.

<a id="빌드-및-실행"></a>

## Build and run

Run from project directory. All build outputs are created in  `build/` .

```sh
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug
open build/bin/Society.app
# You can also run it directly by specifying the prepared source path.
build/bin/Society.app/Contents/MacOS/Society --container "/absolute/path/to/container"
```

To use the installed SDK in the workspace, specify the package path as follows.

```sh
cmake --preset debug \
  -DiiSocietyContainer_DIR=/Volumes/Storage/Workspace/SDK/iiSocietyContainer/build/install/lib/cmake/iiSocietyContainer
```

iiSocietyContainer's macOS build requires Swift Command Line Tools and Python 3. Native adapter signature, installation, and connection removal follow the `platform/macos/README.md` of the SDK.

CLion also specifies the CMake build directory as `build/`. The execution target is `Society`. In other installation paths, preset values can be overridden with `cmake --preset debug -DQt6_DIR="<Qt>/lib/cmake/Qt6" -DLVRS_DIR="<LVRS>"`.

<a id="구조와-검증"></a>

## Structure and Validation

- `src/main.cpp`: LVRS runtime initialization and `Society.Main` QML load
- `src/App/Main.qml`: LVRS window, folder selection, 9 area, sidebar, path navigation, Finder connection UI
- `src/App/FileGridView.qml`: file list, preview, selection, empty state UI
- `src/App/Files/DirectoryLocation.h/.cpp`: local absolute path validation and file URL conversion
- `src/App/Files/ModelImporter.h/.cpp`: model classification, asynchronous copy, duplicate name handling, and progress status
- `src/App/Files/ModelImportSource.h`: contract for local files and Apple provider to maintain read access during copy
- `src/App/Files/AppleModelSource.h/.mm`: Apple file provider input maintaining security scope and file adjustment
- `src/App/Files/IosModelDrop.h/.mm`: iOS native drop across entire window and destination feedback
- `src/App/Drive/DriveController.h/.cpp`: SDK drive load, navigation within area boundaries, native connection status
- `src/App/AI/`: Empty initial skeleton of the Civitai·Hugging Face·Ollama integration class
- `tests/tst_filegrid.cpp`: empty path, invalid path, sorting and image preview of actual temporary files, click, double-click, keyboard navigation and scroll, three kinds window size, path change, QML warning check
- `tests/FileGridHarness.qml`: LVRS window that validates the file grid independently of the drive screen
- `tests/tst_drive.cpp`: Checks persistent ID, retention of in-app file browsing for all 9 sections, rejection of boundary escapes, preservation of an existing container on conflicts, and section clicks, folder navigation, parent navigation, small-window layouts, and QML warnings in the actual drive screen. Also checks the window's URL drop, copy action, automatic section placement, and model-list updates.
- `tests/tst_modelimporter.cpp` : two model extensions, preserve original, name conflict, duplicate input, prevent overwrite during simultaneous import, invalid input, reject region redirect, cancel and temporary file cleanup
- `tests/tst_applemodelsource.mm`: Copy and delayed callback cancellation check through actual Foundation file providers
- `Society.IosDropSyntax` : If the host SDK has a UIKit header, compile-check the iOS drop delegate's API ·type at iOS target. It is not app link or execution check.
- `tests/tst_dependencies.cpp`: Share actual app link settings to check headers·symbols·load at runtime for 13 SDKs

AI class source and header are registered in `SocietyDependencyTests` and included in the build. Since the current class has no API or execution behavior and is not connected to the app, no separate feature assertions are added. Existing dependencies, GUI tests, and the full build are used to verify this skeleton addition.

`ctest --preset debug` checks GUI behavior in a software rendering environment without a screen. The temporary folder for the file grid test is created under `build/` and removed after execution. `Society.Dependencies` test calls 13 public SDK symbols. Inference model download, network requests, license activation, and update installation are not performed. `cmake --build build --target Society_qmllint` QML static analysis can also be run. The local app for execution uses installed Qt /LVRS. Separate packaging is required for deployment to other computers.

## iOS / iPadOS

iOS 16 and above automatically open the Society of the shared App Group when the app is opened and register it with the Files app. The app's home maintains all 9 areas. Opening Society from the Files app immediately shows the content of `Files/` and does not expose the remaining 8 areas. iOS `Open in Files` opens a system document navigator starting from this public root.

`ios-device`, `ios-simulator` CMake preset, and built-in `SocietyFileProvider.appex` are used. Full Xcode 16 and above, Qt 6.8.3 iOS, LVRS · iiSocietyContainer · iiSocietyHelper · iiSocietySync and their corresponding iOS target package of dependencies are required. `python3 -B tools/build_ios.py --platform ios-simulator` performs SDK build and installation through to app and extension build. Device package uses `build/ios-device/install`, simulator package uses `build/ios-simulator/install`. iOS configuration does not replace missing packages with desktop installation.

```sh
cmake --preset ios-device -DSOCIETY_IOS_TEAM=<development-team-id>
cmake --build --preset ios-device
```

The default App Group for app and extension is `group.com.iisacc.society` and is set to `SOCIETY_IOS_APP_GROUP`. App ID and extension ID are `com.iisacc.society` and `com.iisacc.society.fileprovider` respectively. Same App Group access permission is required for both signing profiles. The original is stored in the group's `Library/Application Support/Society` and app Documents full sharing is not enabled. Detailed storage contract and SDK build procedure follow [iiSocietyContainer iOS document](../../SDK/iiSocietyContainer/platform/ios/README.md).

Host GUI test verifies navigation of 9 areas and system-specific connection messages on phone portrait and landscape sizes. This test and the common File Provider storage test distinguish actual iOS build and device execution results. Model import also follows the same distinction. macOS opens the Foundation provider and file adjustment path `Import models…` to open the iOS file picker, which copies `.safetensor` and `.safetensors` to Models within the original access scope. Files and folders are opened by tapping one time, and supported documents and images are displayed as QuickLook. Copying requests a finite background execution time and cancels upon expiration. Detailed behavior and current iOS SDK actual build and device verification remaining uninstalled are recorded in [iOS implementation document](docs/iOS.md).

<a id="windows--linux--android-드라이브"></a>

## Windows · Linux · Android drive

Society uses iiSocietyContainer 0.9 and iiSocietyHelper 0.5. Windows uses Dokan 2 based drive letters, Linux uses libfuse 3 mount, and Android connects to the system document provider. Opening a drive on the system immediately shows Files content, and the app maintains existing 9 areas internally. Windows / Linux adapter is a separate user session process, so closing the Society window maintains the drive and restores saved registration at login. Windows requires a signed Dokan 2 driver and DLL, Linux requires fuse3 and /dev/fuse access.

Android automatically prepares app-specific storage. Other iisacc apps provide internal ContentProvider with the same signature. It directly reads the content URI of the external file picker to import .safetensor/.safetensors to Models. LVRS applies actual WindowInsets of Android system bars and screen cropping to place the bottom Open in Files button outside the navigation bar.

```sh
python3 tools/build_android.py --qt <Qt-6.8.3-Android-ABI> --qt-host <Qt-6.8.3-host> \
  --sdk <Android-SDK> --ndk <NDK-r27c> --lvrs <same-ABI-LVRS-prefix> --package
```

Build order is Container installation → Helper installation → Society APK. Output and SDK installation are placed under `build/android/`. Android and Windows /Linux depend only on the Container and Helper actually invoked by the current app, maintaining existing full SDK compatibility check of macOS. Device-specific signature APK deployment is a separate step.

Regression tests are in SDK's `files_view`, `mount_service`, `tests/native_mount.py`, `tests/android/run_device.py`, and Helper's `tests/android/`. Actual platform execution results are recorded with build logs, distinguishing cross-compilation and actual OS drive verification.
<a id="ios-files-실행-회귀-검사"></a>

## iOS Files execution regression test

The actual iOS  system drive check is activated as normal iOS  build followed by `cmake -S . -B build/ios-device -DSOCIETY_IOS_FILES_INTEGRATION_TEST=ON` , and Society is rebuilt, installed, and executed again. This option is off by default.  `tests/IosFilesIntegration.swift`  compares the App Group source with the system's public root enumeration, and checks creation, reading, editing, renaming, deletion, and source reflection within a test folder with a new UUID attached. Existing user files are not deleted or the domain is not initialized.

The results are kept in the app's `Documents/ios-files-integration.json` and `SOCIETY_FILES_INTEGRATION` console logs. If successful, the public root is opened with the existing `Open in Files` feature. Results can be retrieved via the `devicectl device copy from` `appDataContainer` domain. After confirmation, change the option to OFF and rebuild and install again. This check calls the actual File Provider on the connected device and is separate from the host's plist and Swift syntax check. If a test folder remains on a failed step, only that UUID folder is checked. The signature bundle check `tests/verify_ios_bundle.py` rejects builds containing this diagnostic function. If Release LTO inlines the resource initialization function, it verifies the static generators of the remaining qrc translation units and checks the links to LVRS · QR license resources.

If the device connection is broken and an empty test folder remains, the exact `Society Files Test <UUID>` name can be passed as the `SOCIETY_FILES_TEST_CLEANUP` environment variable in the next check execution. This processing deletes only empty regular folders with that name and does not search and clean other files.

<a id="같은-계정의-로컬원격-기기-파일"></a>

## Local and remote device files of the same account

Select the same account's peripheral device from the top Devices or scan the desktop's QR with a mobile to browse and download the same Wi-Fi/LAN `Files/`. The QR is independent of account login. The desktop is fixed as the host, iOS /Android as the client, and there is no role selection in the [Preferences window](docs/Preferences.md). Connection is made based on the platform role during device selection, QR creation, or server connection. A self-hosted account-based server can be set up separately from direct LAN connection at Devices' **Your Society server**. Server connection first attempts the local TLS path and switches to WebSocket relay. The public scope, mobile restrictions, and verification methods follow [NetworkDrive .md](docs/NetworkDrive.md).

The iPhone QR scan uses AVFoundation metadata and Vision decoding of the actual camera frame together. It displays scan frames, recognition status, and distance/reflection guidance and sets continuous autofocus. The desktop QR is displayed with a maximum width of 432. Photo regression verification and frame processing/cancellation contracts are recorded in [Pairing.md](docs/Pairing.md).

The host and all clients use the same logical container UUID. The first pairing mirrors the entire host drive and then starts bidirectional changes, leaving previous client content in the private recovery area. The contract for initial mirror, offline editing, and native registration follows the [synchronization document](docs/Synchronization.md).

<a id="ios-live-activity-동기화"></a>

### iOS Live Activity synchronization

iOS   26  or higher, synchronization started via Devices → **Sync now**is connected to the system Live Activity.  CPU  While network execution permission is granted, background transfer continues and is released upon completion, failure, or cancellation. Automatic discovery does not repeatedly generate this task. Upon old version or request rejection, finite execution is maintained and return resume is held.  [behavior and verification contract](docs/iOS.md#live-activity를-통한-동기화-지속)are referenced.

<a id="로컬-mcp-제어"></a>

## Local MCP control

The desktop POSIX build provides Society's controller running on the iiLocalLLM 0.36 family as an authenticated local MCP tool. Following the SDK's same minor version compatibility policy, use 0.36.0 or higher patch versions, and verify connection with the installer via `Society.Mcp` integration tests. Automatic discovery, input/permission/cancellation contracts, and actual app execution tests are recorded in [Mcp.md](docs/Mcp.md). It can be disabled via `IILOCALLLM_DISABLE_APP_MCP=1`.


When entering mobile, the container status lookup of iiSocietySync and file processing are performed in the work thread. The screen reads only cached states and receives the lookup, download, and synchronization results asynchronously. Mobile exit does not wait for work cleanup and maintains the desktop execution right handover wait. iiSocietySync 0.5.0 and [asynchronous execution contract](docs/Synchronization.md#모바일-시작과-화면-응답성)are followed.

Photos are stored in the `Society/Photos/` hierarchy, similar to Files. Photos and Generation History are a square gallery with 2 px spacing, and zoom in/out by horizontal drag and pinch, and clicking or touching opens file information. [gallery operation and verification](docs/Gallery.md)are referenced.

The mobile screen shares the Dashboard, Tools, and Storage of the desktop. The default Dashboard, bottom 5 tabs, and the responsive criteria for collapsible navigation, settings, and Storage work sheets are recorded in [MobileViews .md](docs/MobileViews.md).

## Source layout

Implementation files and their headers live together under `src/`. Existing feature and platform subdirectories retain their responsibilities. Build configuration, tests, documentation, resources, and maintenance scripts remain at the project root. Configure and build using the repository-local `build/` directory.

The file grid and photo gallery test harnesses import their views from `src/App` and `src/App/Photos`, matching the application's source layout.


### Dashboard calendar

The monthly calendar and daily schedule details of the dashboard use C++23 `iiCalendar::iiCalendar`. Before building, prepare iiCalendar 0.1 and ICU ·SQLite. The installation path and local data storage and verification contract are recorded in [Dashboard — iiCalendar](docs/Dashboard.md#iicalendar-대시보드).

<a id="macos에서-library-missing으로-실행되지-않을-때"></a>

### When not running with Library missing at macOS

If `libiiLocalDiffusion` refers to the `libjson-c.5.dylib` path of the deleted Homebrew, the app terminates with an DYLD error before opening QML. `build/bin/Society.app` can also be run from Finder only after native library distribution, path rewriting, and signing are all completed. Do not use the completed app with the POST_BUILD result immediately after linking or with failed/aborted results.

Specify the runtime library directory verified at `SOCIETY_MAC_RUNTIME_SEARCH_PATHS` and complete `cmake --build build --target Society -j4` to the end. Also resolve the deleted Homebrew absolute path by the same filename in that directory and copy it into the app. `tests/test_macos_runtime.py` includes a regression test that runs after removing external sources of this transition dependency. After checking the development loader environment variable without `tests/verify_macos_runtime.py APP --gui` and the GUI ·daemon· QML startup, replace the package.

If the `build/bin/Society.app` of the same build has already passed independent execution and signature verification, first check if the Mach-O UUID of GUI ·daemon·SDK matches, then you can backup the aborted development build and restore the entire bundle of the verified package. If only the executable file is copied, external library paths may be reintroduced, so preserve Frameworks, Helpers, Resources, and the signature together. If the source or SDK build is different, do not use this restoration method.

macOS window close, app termination, and Dock reopen actions follow the [application lifecycle policy](docs/ApplicationLifetime.md).
## Model Packaging

Tools의 Model merge 옆 Model Packaging에서 로컬 모델 폴더를 단일 Safetensors로 묶을 수 있다.
구성요소와 샤드를 자동 식별하고 중복·불량 파일을 검사하며, 원래 정밀도와 GGUF 형식을 보존한다.
SDK가 출력 검증과 원자적 저장을 완료해야 성공으로 표시한다. [사용법과 검증](docs/ModelPackaging.md)을 참조한다.
