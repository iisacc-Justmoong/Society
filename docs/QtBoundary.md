# Qt dependency boundary

Society retains LVRS/Qt Quick for its GUI. Backend packages consume narrower
SDK targets rather than presentation facades:

| Consumer | SDK target | Boundary |
| --- | --- | --- |
| Native container monitor | `iiSocietyContainer::FileTree` | C++23, no Qt discovery/linkage |
| Native storage maintenance | `iiSocietyContainer::Maintenance` | C++23 and temporary leases, no Qt |
| Helper canvas storage and media I/O | `iiSharedCanvas::Core` | No QML or Quick items |
| Photo controller clipboard/sharing | `iiSocietyContainer::Sharing` | Qt Gui actions, no Quick image provider or application/window helpers |
| Society GUI | LVRS, Qt Quick, container `Gui` | Presentation and platform GUI integration |

The daemon's QML registration annotations use the header-only QmlIntegration
target. It does not link the QML runtime. The runtime verifier records the
libraries loaded by the actual packaged `SocietyDaemon --check-runtime` process
and rejects Qt QML, Qt Quick and the canvas Quick adapter, including transitive
loads. This assertion is also part of post-build package validation.

Directory-wide `link_libraries` calls previously propagated Qt file adapters
into unrelated targets. Society, the affected canvas/helper/photo SDKs and
container targets now declare their dependencies on the owning target.

This is a dependency reduction, not a claim that every existing backend has
been rewritten. Account/network/IPC adapters still use Qt Core, Network, Sql,
Bluetooth and Concurrent; image/vector/PDF codecs still use Qt Gui. Pure native
container and monitor builds are separately verified with Qt discovery disabled.
The remaining codec and event-loop adapters are explicit migration boundaries.

Verification covers native storage fixtures and installed consumers; native
monitor library/process tests; the full canvas suite and a Core-only installed
consumer with QML/Quick discovery disabled; sharing/photo/helper regressions;
and the packaged Society app, daemon, GUI and live container connection.

## macOS verification, 2026-10-15

The installed application was built against isolated installations of the
changed SDKs. Shared installations under `~/.local/SDK` were preserved because
other workspace consumers still use a different file-provider package version.
The native storage and monitor builds, Core-only canvas build, and installed
Core/Sharing consumers all configured with the relevant Qt package discovery
disabled.

The packaged `SocietyDaemon --check-runtime` process went from five loaded
QML/Quick frameworks to none; its total loader inventory went from 776 to 771
libraries. The runtime regression rejects a future presentation dependency even
when it arrives through another SDK. Core, network, database and codec adapters
remain explicit dependencies.

The signed application was installed at `/Applications/Society.app`. Dashboard,
Tools and Storage were inspected in the installed GUI; the existing container
selection and its 20 files were preserved. The previous container-monitor
process remained alive but stopped publishing fresh heartbeats during the
update. Refreshing only that login agent restored healthy state publication
from the installed bundle, with zero loaded Qt libraries. Its container UUID
was preserved and its monitor instance changed as expected for a new process.
The existing synchronization daemon and Dreamscapes process were left running.

The application bundle changed from 346.71 MiB to 346.92 MiB, an increase of
about 0.21 MiB for the split libraries. The GUI still ships Qt Quick, so this
change reduces backend coupling and runtime loads without removing the GUI's
frameworks from the application package.

Validation includes 11 Society test groups and eight native GUI cases. The
canvas run passed 62 functional groups; the updated dependency contract and
eight API/rendering/serialization groups passed after the final library split.
The installed compatibility facade also resolved all 922 previously exported
canvas namespace symbols on macOS. SDK-specific native, installed-consumer,
sharing, helper, photo and container tests are recorded in
`build/qt-boundary-20261015/report.json`, together with package identity,
signature, loader and screenshot evidence. Cold runtime-launch timeouts and
the subsequent successful retry remain in the validation logs. Execution on
Windows, Linux and physical mobile devices was not verified in this change.
