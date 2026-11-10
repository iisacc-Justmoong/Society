# Society in system file managers

Society connects its existing `Files/` contents to the platform file manager. The original directory, manifest UUID, other eight sections, and application metadata stay at their existing locations. Connecting does not create a copied image or a Finder/Explorer favorite.

| Platform | System connection | Success check |
| --- | --- | --- |
| macOS 26+ | FSKit volume named Society | Native result and `QStorageInfo` identify `societyfs` at the mounted root |
| Windows | Dokan 2 removable drive named Society, using a free letter starting at S: | Windows reports the Society label, filesystem name, and original UUID-derived serial; `QStorageInfo` identifies the returned drive root |
| Android 9+ | Society root in system Files and Storage Access Framework pickers | The installed DocumentsProvider returns the selected UUID and its public root document |

데스크톱의 디스크 활성화와 재연결은 Society 백엔드 데몬의 작업자만 수행한다. 앱은 선택한 컨테이너에 대한 데몬 상태를 비동기로 구독하고 `Reconnect` 또는 `Refresh` 요청을 전달한다. 앱이 마운트 실행 파일이나 파일시스템 호스트를 직접 실행하지 않는다. 데몬이 연결되지 않았거나 다른 컨테이너·UUID를 보고하면 이전 시스템 경로를 지우며, 실제 원본 폴더를 마운트된 디스크로 표시하지 않는다. 검증된 시스템 경로는 `Open in Finder` 또는 `Open in File Explorer`에서 열 수 있다. iOS와 Android의 시스템 파일 제공자 연결은 각 모바일 플랫폼에서 계속 처리한다.

이미 선택된 컨테이너를 다시 열어 화면의 경로가 초기화되면 앱은 데몬의 최신 상태 스냅샷을 다시 요청한다. 컨테이너를 더 이상 열 수 없으면 화면의 경로와 연결 상태를 함께 지운다.

Society 백엔드는 GUI 창이 없어도 선택된 드라이브를 연결한다. 반환된 컨테이너 UUID·마운트 루트·플랫폼 파일시스템을 검증하고, 일시적 실패를 재시도하며, 선택이 바뀌기 전 요청의 결과는 무시한다. 누락되거나 잘못 서명된 호스트는 연결 실패로 보고하며, 볼륨 루트에는 선택한 `Files/`의 내용만 표시한다.

`SOCIETY_NATIVE_DRIVE_BUNDLE`은 macOS 백엔드 작업자가 사용하는 설치된 네이티브 호스트를 지정한다. 기본값은 선택된 iiSocietyContainer SDK의 호스트이며, 임시 SDK 스테이징을 사용해 빌드할 때는 앱 패키징 전에 실제 설치 호스트를 지정해야 한다.

On macOS, a correctly signed and registered FSKit module must also be enabled by the user. In **System Settings → General → Login Items & Extensions → Extensions → By Category → File System Extensions**, enable Society. If the module is disabled, macOS can reject mounting with `Operation not permitted`; the backend reports the failure and retries after the setting is enabled. The Society mount is a separate `societyfs` filesystem volume in Finder, backed by the existing `Files/` directory; it is not a block device or an additional copy of the data.

Windows needs the signed Dokan 2 kernel driver installed on the target machine. The SDK copies an available architecture-matched `dokan2.dll` next to its mount executable and exposes that runtime to the Society package. Copying the DLL does not install the kernel driver. See [the SDK desktop instructions](../../../SDK/iiSocietyContainer/platform/desktop/README.md).

Android uses the official DocumentsProvider integration, not a kernel block-device mount or drive letter. The provider accesses app-private storage and exposes only Files-relative document IDs. `register`, `refresh`, and `path` verify the actual root before reporting success; `path` opens the system file manager. The initial replica readiness requirement remains in force: a pending account mirror must not be presented as a ready drive. See [the SDK Android instructions](../../../SDK/iiSocietyContainer/platform/android/README.md).

Validation is separated into compilation, host regression tests, and optional target execution. `tests/native_mount.py` checks an actual Windows/Linux mount, Files-only CRUD, Windows removable-volume recognition, reconnect, unmount/remount, and original manifest preservation. Android `ProviderInstrumentation` verifies connection identity, provider root, public operations, and process restart. Target-device execution can be omitted without presenting compilation as runtime proof.

`tests/drive_compile` compiles the production `DriveController.cpp` with the selected target Qt toolchain, without creating or launching an application. Configure it below `build/`, set `SOCIETY_ACCOUNT_INCLUDE` to installed account headers, and select the Windows or Android CMake toolchain. This validates platform-specific dispatch and native APIs separately from a complete Society package and its other SDK dependencies.

Society's stored source, state, cache and service registry/lock data live beneath the selected package. macOS 27 creates and manages its FSKit mount namespace under `/Volumes/` through the signed host's public mounting API; it stores no additional source data there. The macOS 26 and Linux backing directories are inside `.society-runtime/`. OS extension registration, startup registration and the small shared locator remain OS integration metadata. The app does not manage an ordinary external data directory or restore an old disk image.

Product test processes explicitly set `SOCIETY_DISABLE_NATIVE_DRIVE=1`. They exercise valid package storage without registering or mounting disposable fixtures on the user’s computer. The dedicated SDK native integration and installed-app validation run separately with that override removed.
