# Single app build contract

The product uses the [workspace single-app policy](../../build-policy/README.md).
Only the canonical application bundle is generated beneath `build/`. Tests and
helpers are ordinary executables; runtime deployment and packaging work in place.
See the policy for canonical paths, platform switching and verification commands.

The iOS standalone XCTest UI runner is disabled: its aggregate guard rejects builds before a second app can be generated. Interaction test Swift sources remain available for a runner-free harness.

The account and daemon test targets resolve the same daemon executable before
their compile definitions are created. On macOS they depend on the canonical
Society bundle and use its plain Contents/MacOS/SocietyDaemon helper. Account
test setup checks that this path is nonempty and executable, so packaging
regressions fail before an empty QProcess command can obscure the cause.

The iOS build contract checks the complete named SDK set, including iiPhotoLibrary. The Mac Catalyst drop syntax check includes AccountManager and Qt Concurrent headers used by DriveController; this is syntax validation, not an iOS runtime test.
The syntax check uses C++23, matching SDK public headers such as DiskImage.h that expose std::expected.
