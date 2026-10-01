# File loading verification — 2026-09-28

Measurements below are model-level timings, not app launch or rendered-frame timings.
The machine reports 10 logical CPUs / 32 GiB RAM. Files live on external storage;
build activity and the OS page cache affect first-run timings. Originals were not
modified for the real-library benchmark.

## Current-turn regression rerun

- Rebuilt `iiSocietyContainerPreviewCacheTests`, `iiSocietyContainerDashboardTests`,
  and `iiSocietyContainerSharedStorageTests` against the current SDK.
- Preview cache passed in 43.40 s and Dashboard passed in 25.90 s.
- The first shared-storage run passed 23 of 24 cases; one small filesystem-backed
  model-catalog fixture exceeded its generic five-second QtTest wait, completing
  after 7 s. Its readiness assertion now allows 30 s, without changing runtime
  behavior. The SDK test rebuilt and shared-storage then passed 1/1 in 10.10 s.
- This confirms regression coverage under the observed storage load, not a cold
  cache, whole-volume throughput, or physical iPhone performance result.

## Follow-up validation: current storage contention and concurrency check

The later revalidation is not fully green and does not replace the historical
measurements below with a new performance claim:

- The installed macOS GUI library still exports `PreviewCache`/`PreviewProvider`,
  and both app-cache directories exist with owner-only permissions.
- The preview-cache and shared-storage targets passed again (77.85 / 96.58 s).
  Dashboard terminated with SIGTRAP; the native crash report reports heap free-block
  corruption while parallel section scans are active. Root cause is under review;
  a separate heap-instrumented build and repeated section-isolation test were added.
  Log: `SDK/iiSocietyContainer/build/file-loading-current-tests.log`.
- After rebuilding the GUI SDK and its Dashboard test together, the normal
  Dashboard suite passed five consecutive runs (86.84 s total). A separate
  AddressSanitizer build passed five consecutive runs (89.80 s total). Each run
  includes twelve three-section refreshes plus the existing edit/delete/watch
  contract. The earlier single crash was not reproduced in these runs; no
  confirmed corruption fix or universal crash-free guarantee is claimed.
  Logs: `file-loading-dashboard-stress.log`, `file-loading-asan-tests.log` in
  `SDK/iiSocietyContainer/build/`.
- Bounded streaming watch discovery replaces whole-directory materialization in
  the sync SDK. Its standalone/CMake contracts passed (final CTest 0.52 s), but
  the controller suite exceeded its 60-second timeout during startup. The existing
  installed SDK also failed the isolated startup check; this does not establish
  that the new code caused it or that the integration is verified.
- A native 16 MiB source read with 1 MiB blocks took 88.75 seconds. This is evidence
  of severe storage latency during this observation, not a diagnosis of disk damage.
  The old bulk packager was cancelled normally (exit 130, zero committed imports)
  to reduce background contention. Originals were not modified.
- The additional watch-discovery changes have not been delivered in a new app
  bundle. Do not conflate historical installed-cache proof below with deployment
  of this follow-up patch or with resolution of the observed Dashboard crash.
- The final watch-discovery SDK build and focused CTest passed (0.60 s), and
  `cmake --install build` updated the canonical `~/.local/SDK/iiSocietySync`
  installation. This is an SDK installation, not replacement of the running
  Society app's bundled library. Logs: `watch-paths-delivery-build.log`,
  `watch-paths-delivery-tests.log`, `watch-paths-install.log` in the sync SDK build.

## Repeat navigation

| Dataset | Previous first rows / Ready | Cached first rows | Live reconciliation Ready |
| --- | --- | --- | --- |
| Real Generation History, 94 entries, runs 2–3 | 216 / 212 ms | 2 / 2 ms | 11 / 11 ms |
| Synthetic text directory, 2,000 entries, runs 2–3 | 23 / 22 ms | 25 / 24 ms | 87 / 87 ms |

The real-library warm path improved materially. The simple text fixture did not
improve first-row latency, and snapshot reconciliation adds work to its Ready path.
Do not claim a universal speedup. The first run was not a controlled cold-cache
comparison and is intentionally not used to calculate a speedup.

Raw reports live under `SDK/iiSocietyContainer/build/`:
`loading-real-baseline.json`, `loading-watch-real.json`,
`loading-baseline-warm.json`, `loading-watch-fixture.json`.

Preview regression runs measured 21.9–156.1 ms to decode/hash/store a 2400×1600 test
PNG and approximately 0.7–0.8 ms to load its persisted 512-pixel preview in a fresh
cache object. These are one test image's results, not whole-library throughput.

## Checks

- SDK preview, dashboard and shared-storage CTest targets passed (3/3).
- Source overwrite/deletion, provisional snapshot reconciliation, watcher
  refresh, content SHA-256, concurrent cache requests, corrupt previews, cancellation
  and older Dashboard `previewSource` compatibility are covered.
- Product QML tests verified rendered `Image.Ready`, image replacement/cache update,
  selection/scroll preservation, dashboard add/edit/remove updates and interactive
  dashboard cards using the new thumbnail field (8 passed; final compatibility build).
- macOS and iOS SDK builds/installations passed.

## Application delivery

- macOS application compilation/linking completed. An interrupted deployment left
  `QtNetwork.framework` incomplete; its missing resources and version symlink were
  restored from the same Qt 6.8.3 installation, then signing was resumed.
- The external-storage candidate's standard runtime probe exceeded its unchanged
  30-second limit. This is a failed startup probe, not a passing build/runtime check.
  Log: `build/file-loading-macos-source-runtime.log`.
- The repaired macOS candidate passed `codesign --verify --deep --strict`, was
  installed in `/Applications/Society.app`, and the installed bundle passed the
  same signature check and standard runtime probe (776 GUI-loader / 771
  daemon-loader libraries; none outside bundled/system locations).
  Logs: `build/file-loading-sign-recovery.log`,
  `build/file-loading-macos-installed-runtime.json`.
- The installed GUI reached `bootstrap.entry.root-loaded`; its Generation History
  showed 94 rows and real image previews, including after navigating away/back.
  The observed application cache at `~/Library/Caches/Society/society` contained
  one directory snapshot and 45 PNG/manifest pairs (13,834,190 bytes in that
  sample). One preview was 512x512 with mode 0600 and a 64-character SHA-256 field.
  Independently hashing that manifest's original file matched its stored digest.
  This observation does not measure rendered-frame latency or cold app launch.
- The iOS Release build succeeded. The app, File Provider and Live Activity passed
  the signed-bundle/provisioning check for the connected iPhone 15 Pro Max, then
  `devicectl` installed `com.iisacc.society` successfully without uninstalling or
  resetting its data. Logs: `build/file-loading-ios-build.log`,
  `build/file-loading-ios-bundle.json`, `build/file-loading-ios-install.json`.
- Launching that new iPhone installation was rejected with
  `FBSOpenApplicationErrorDomain / Locked`. The new installation is confirmed,
  but its physical-device execution, preview cache creation and reconnection
  timings are not verified until the user unlocks the device.
  Log: `build/file-loading-ios-launch.json`.
