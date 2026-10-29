# File creation ownership in Society

Daemon/status state, synchronization ownership, model import/merge workspace and photo file creation use provider factories. Android media and container storage use the shared provider bridge.

The common [ownership contract](../../../SDK/iiFileProvider/docs/FILE_OWNERSHIP.md) defines stream, staging, control-file and
installed-package rules. Source regression tests remain with this module; the
workspace creation audit also checks these boundaries. Build/deployment and target
runtime validation are separate evidence.

Changed boundaries:


- `App/Society/Files/ModelImporter.cpp`
- `App/Society/Network/NetworkDriveController.cpp`
- `App/Society/Network/NetworkDriveController.h`
- `App/Society/Photos/PhotoLibraryApple.mm`
- `App/Society/Photos/PhotoLibraryWindows.cpp`
- `App/Society/Photos/PhotoStore.cpp`
- `App/Society/Services/SyncOwnership.cpp`
- `App/Society/Services/SyncOwnership.h`
- `App/Society/Tools/ModelMergeController.cpp`
- `App/Daemon/DaemonService.cpp`
- `App/Daemon/DaemonService.h`
- `App/Daemon/main.cpp`
- `App/platform/android/src/com/iisacc/society/SocietyPhotoLibrary.java`
