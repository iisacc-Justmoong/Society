# Society in system file managers

Society connects its existing `Files/` contents to the platform file manager. The original directory, manifest UUID, other eight sections, and application metadata stay at their existing locations. Connecting does not create a copied image or a Finder/Explorer favorite.

| Platform | System connection | Success check |
| --- | --- | --- |
| macOS 26+ | FSKit volume named Society | Native result and `QStorageInfo` identify `societyfs` at the mounted root |
| Windows | Dokan 2 removable drive named Society, using a free letter starting at S: | Windows reports the Society label, filesystem name, and original UUID-derived serial; `QStorageInfo` identifies the returned drive root |
| Android 9+ | Society root in system Files and Storage Access Framework pickers | The installed DocumentsProvider returns the selected UUID and its public root document |

Desktop startup, opening a `.societycontainer` package, and `Reconnect` request a native mount when the installed adapter is available. `Refresh` reconnects a missing desktop mount instead of treating the original package as a system drive. Builds without an adapter retain package access with no claim of an OS mount. The mount service preserves registrations across its own restart and can restore them at login. The app opens the mounted root through `Open in Finder` or `Open in File Explorer`.

Windows needs the signed Dokan 2 kernel driver installed on the target machine. The SDK copies an available architecture-matched `dokan2.dll` next to its mount executable and exposes that runtime to the Society package. Copying the DLL does not install the kernel driver. See [the SDK desktop instructions](../../../SDK/iiSocietyContainer/platform/desktop/README.md).

Android uses the official DocumentsProvider integration, not a kernel block-device mount or drive letter. The provider accesses app-private storage and exposes only Files-relative document IDs. `register`, `refresh`, and `path` verify the actual root before reporting success; `path` opens the system file manager. The initial replica readiness requirement remains in force: a pending account mirror must not be presented as a ready drive. See [the SDK Android instructions](../../../SDK/iiSocietyContainer/platform/android/README.md).

Validation is separated into compilation, host regression tests, and optional target execution. `tests/native_mount.py` checks an actual Windows/Linux mount, Files-only CRUD, Windows removable-volume recognition, reconnect, unmount/remount, and original manifest preservation. Android `ProviderInstrumentation` verifies connection identity, provider root, public operations, and process restart. Target-device execution can be omitted without presenting compilation as runtime proof.

`tests/drive_compile` compiles the production `DriveController.cpp` with the selected target Qt toolchain, without creating or launching an application. Configure it below `build/`, set `SOCIETY_ACCOUNT_INCLUDE` to installed account headers, and select the Windows or Android CMake toolchain. This validates platform-specific dispatch and native APIs separately from a complete Society package and its other SDK dependencies.

Society's stored source, state, cache and service registry/lock data live beneath the selected package. macOS 27 creates and manages its FSKit mount namespace under `/Volumes/` through the signed host's public mounting API; it stores no additional source data there. The macOS 26 and Linux backing directories are inside `.society-runtime/`. OS extension registration, startup registration and the small shared locator remain OS integration metadata. The app does not manage an ordinary external data directory or restore an old disk image.

Product test processes explicitly set `SOCIETY_DISABLE_NATIVE_DRIVE=1`. They exercise valid package storage without registering or mounting disposable fixtures on the user’s computer. The dedicated SDK native integration and installed-app validation run separately with that override removed.
