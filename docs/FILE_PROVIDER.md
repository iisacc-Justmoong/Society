# File creation ownership in Society

Daemon/status state, synchronization ownership, model import/merge workspace and photo file creation use provider factories. Android media and container storage use the shared provider bridge.

The common [ownership contract](../../../SDK/iiFileProvider/docs/FILE_OWNERSHIP.md) defines stream, staging, control-file and
installed-package rules. Source regression tests remain with this module; the
workspace creation audit also checks these boundaries. Build/deployment and target
runtime validation are separate evidence.

Changed boundaries:


- `src/App/Files/ModelImporter.cpp`
- `src/App/Network/NetworkDriveController.cpp`
- `src/App/Network/NetworkDriveController.h`
- `src/App/Photos/PhotoLibraryApple.mm`
- `src/App/Photos/PhotoLibraryWindows.cpp`
- `src/App/Photos/PhotoStore.cpp`
- `src/App/Services/SyncOwnership.cpp`
- `src/App/Services/SyncOwnership.h`
- `src/App/Tools/ModelMergeController.cpp`
- `src/Daemon/DaemonService.cpp`
- `src/Daemon/DaemonService.h`
- `src/Daemon/main.cpp`
- `src/platform/android/src/com/iisacc/society/SocietyPhotoLibrary.java`
