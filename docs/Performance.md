# Runtime efficiency and dependency ownership

## Startup, tab loading and shutdown

Society creates its Helper and the daemon's presence-only Helper with deferred
file-system discovery. The app binds the Helper's file system when its selected
drive becomes available; constructing either presence owner no longer discovers
the same drive eagerly. Runtime-directory discovery and validation are dispatched
to a worker, with a generation check so an old result cannot reopen a drive after
suspension or selection changes. On desktop, Helper presence setup and cleanup
also use its asynchronous lifecycle; opening an inbox follows successful setup.
An in-progress validation retains its own state and cannot hold the global Qt
thread pool open at process exit. On macOS, login-service registration also runs
outside the GUI thread. These changes require an iiSocietyHelper build exposing
`Helper(FileSystem::Initialization::Deferred)` and its matching headers.

The Dashboard is built at startup. Tools, Storage and Environment are incubated
when selected, show a loading state while being created, and stay alive once
ready so navigation does not discard edits. If the user changes tabs before
creation finishes, the abandoned creation is cancelled. Calendar storage is opened on
the first Calendar or Activity visit instead of on the Dashboard's first frame.
The Calendar and Projects panes are also incubated asynchronously on entry,
with a loading indication; unfinished creation is abandoned on navigation.
The completed Projects pane is retained across navigation, as is Calendar when
switching top-level tabs while its page remains selected.
LVRS `InputField` requests threaded Canvas rasterization for its inset shadows;
this preserves the field's material while moving the blur work off the GUI
thread on renderers that support that strategy.
Existing drive, account and navigation controllers remain available independently
of those views. The container-path binding initiates the first Dashboard file
scan; completion, visibility and activation do not queue another scan while it
is already running.

### Navigation and view-model preparation

Dashboard and Storage share one metadata-only directory model. When both point
to the same folder, entering Storage reuses its existing snapshot instead of
starting a second directory scan. The displayed rows belong to the visible
browser; a browser used independently still creates its own model. A different
folder replaces the shared snapshot, so this is not an unbounded directory cache.

After Dashboard has settled, a single model-catalog scan can start during an
active, idle session. It waits for the initial file work and any import to
finish and does not start while the user is searching or using a Dashboard
subpage. The Storage Models page reuses that snapshot on its first visit;
explicit refresh and subsequent visits still update it. If the user opens Models
before preparation begins, the requested page starts its scan immediately.
The idle delay is a scheduling policy, not a guarantee that every later change
or user-initiated operation will avoid concurrent work.

Tools prepares its merge-model metadata when its list is opened and other
initial scans are idle. Selecting the merge editor takes priority if preparation
has not started. Merge and packaging editors are created asynchronously only
when selected; if navigation moves away before an editor or the Models page
finishes its initial construction, the unfinished construction is cancelled.
Completed editors are retained, preserving unsaved form state. Their metadata
preparation does not construct both editor interfaces. Calendar and
project data still open on demand because opening their stores may create or
register persistent data.

A Storage action requested while its page is still being created is delivered
when that page is ready, provided the user is still on Storage. On a catalog
update, category selection and scroll positions are restored after the new
rows have reached the view rather than against the previous snapshot. If the
page is still being constructed, restoration waits for its category items.

These measures avoid duplicate snapshots and defer expensive screen objects;
they do not impose a fixed process-memory limit. Memory can still grow with the
size of a catalog and with editors the user has visited. Navigation tests check
model reuse and deferred creation; they are not a measurement of peak resident
memory for an installed application or a particular workload.

The inbox opens and closes its thread-affine delivery database on a private worker.
`start()` reports that a request was accepted; `connected` becomes true when the
open has completed. A stop invalidates pending replies and clears the old
snapshot immediately; subsequent opens and closes are serialized. Explicit
`readAfter` and `acknowledge` remain synchronous APIs. The worker owns its SQL
connection until its active operation returns, even if the UI is gone; its
thread does not hold the application or the global pool open at final exit.
Validation futures also no longer join their storage tasks in the controller destructor.

Normal desktop ownership handoff still drains the synchronization worker before
unlocking. Final application quit instead retains the PID-marked owner and
foreground leases for the rest of the process lifetime and requests asynchronous
worker shutdown. The daemon cannot acquire them while the retiring process is
alive; after it exits, normal stale-owner recovery permits the daemon to take
over. This avoids waiting for the sync worker on the quit event loop without
allowing two active owners. This exit-only path deliberately leaves locked lease
objects to OS process cleanup and can leave stale lock files until recovery.

Desktop final shutdown explicitly retires the photo controller and generation
host. Their active work owns the resources it needs and cannot post back into
destroyed controllers; queued photo work is discarded and generation output is
discarded when cancellation is observed before atomic commit. A commit already
in progress can still complete. Ordinary SDK destruction and mobile ownership
handoff retain their completion guarantees. Model-import cancellation, the mobile
receiver's final drain, explicit inbox commands and other active task destructors
can still wait on storage. A kernel I/O operation cannot be forcibly cancelled.
The controlled tests and the isolated native-window measurement below do not
establish a universal bound for every workload or storage device.

For a product-local macOS Helper package, install the matching SDK build under
`resources/dependencies/macos-runtime/iiSocietyHelper` and point the existing
`build/` configuration's `iiSocietyHelper_DIR` at its `lib/cmake/iiSocietyHelper`.
New configurations prefer this package when present; an explicit package choice
still takes precedence. The same rule applies to the matching `iiPhotoLibrary`
package under `resources/dependencies/macos-runtime/iiPhotoLibrary`. Do not mix
headers with older SDK binaries. The product-local Qt LVRS package can likewise
be installed under `resources/dependencies/macos-runtime/LVRS`; an explicit
`LVRS_DIR` selection still takes precedence.

The application leaves Qt's QML and graphics pipeline cache policy at its
normal default. It no longer forces both caches off on every production launch.
Explicit caller environment overrides remain available for diagnostics. The
packaged runtime launch and real-peer tests exercise the actual application;
the peer fixture disables unrelated native-drive, photo and radio services.
Compact bundles remove Qt framework `Headers`, `PrivateHeaders` and `Modules`
directories, including version aliases, before signing. Framework binaries,
runtime resources and license files remain. A real Mach-O fixture still loads
after the development files and original SDK binary have been removed.

Society links the capabilities called by its product code. Document, licensing,
update, voice-over and what's-new SDK compatibility probes do not implement
product features and no longer make those packages direct product requirements.
The canvas Core library is owned by the storage/helper dependency graph. The
generation host owns the inference runtime; Society's model tools link the
separate model-packaging component.
Unix 데스크톱 MCP는 iiMultimodalAgent를 링크하며 네이티브 추론 플러그인은
SDK 런타임 소유의 명시적인 배포 입력으로 유지한다.
제한된 새로고침 계약과 회귀 검증은 [모델 목록 작업량](ResourceOptimization.md)에 있다.

`Society.Dependencies` reads the final generated `LINK_LIBRARIES` property,
including platform-specific additions, and rejects these unused direct links.
Its packaged-process test still verifies real Helper peer observation. Unused
integration scaffolds are not compiled into the dependency test. SDKs may retain
their own required transitive dependencies.

## Calendar snapshots

Month counts precompute each civil day's start/end once. Each scheduled item
updates a difference array using two boundary searches; a final pass produces
the visible counts. This replaces a complete event scan for every day with
`O(events * log(days) + days)` work and `O(days)` scratch storage.

Counts retain the native iiCalendar overlap rules: nonempty events exclude their
ending midnight, point events belong to their containing day, and a skipped civil
day has no events. The native timezone resolver still supplies boundaries for
23-hour, 25-hour and skipped days. Event/task details, recurrence, persistent
completion, asynchronous storage epochs and explicit refresh remain unchanged.

## Presence and build packages

Society requires iiSocietyHelper 0.9.3 or later. Its recurring presence and
automatic-history work run on native workers instead of committing disk writes
on the application event loop. Use a matching package graph for iiFileProvider,
iiSocietyContainer, iiSharedCanvas, iiWebServer and iiSocietySync. A stale build cache pointing
at a pre-0.7.1 file provider cannot satisfy the current presentation API.

The selected package is recorded in `build/CMakeCache.txt`. A product-owned
Helper installation may be placed in `resources/dependencies/macos-runtime/iiSocietyHelper`, and the
presentation-aware iiWebServer in `resources/dependencies/macos-runtime/iiWebServer`, without
replacing shared SDK installations. The ServerHost version alone does not
distinguish an old binary built before the presentation filter was introduced.
`fileProjectionHidesProviderControlFiles` therefore checks the consumed library's
actual list/stat behavior, retaining public files while rejecting private preview
and lease files. Build output remains under `build/` and
the only application bundle remains `build/bin/Society.app`.

## Verification

```sh
cmake --build build --target Society SocietyDependencyTests SocietyCalendarTests --parallel 4
ctest --test-dir build -R 'Society\.(Dependencies|Calendar|Drive|Daemon|ModelMerge|ModelPackaging|NetworkDrive|MacRuntimeLaunch|MacBundleFootprint)' --output-on-failure
```

`monthCountsMatchNativeOverlap` compares all 42 month cells with the native
overlap implementation for ordinary days, both daylight-saving transitions and
the 2011 Pacific/Apia date-line skip. Its fixtures include exact midnights,
point events and spans crossing multiple days. Existing tests cover recurrence,
tasks, persistent completion and stale container results.
The overlap fixture concentrates durable writes at grid edges, weekly intervals
and the transition date plus adjacent days. All 42 results are still compared
with the native reference, including empty days, without hundreds of redundant
disk commits per dataset.

Legacy encrypted-state fixtures copy a raw prior-version ciphertext from a
private seed directory. Creating the supposed legacy file through the current
asset writer could schedule preview companions after its initial inventory was
captured. The migration regression retains byte-for-byte source checks and an
unchanged source inventory, starting with the single raw legacy record.
Pairing fixtures retain their backing directories until the controllers and
their background indexers have stopped; removing the directory first can race
an otherwise valid scan during test teardown.
Manual pairing interactions also wait for the sheet's `opened` state. Its
`onOpened` initialization must finish before selecting Nearby or accepting an
invitation, while the verification-code and public-file assertions remain intact.

Live-process samples and candidate build/test records are separate evidence.
An application already running retains its loaded executable and SDKs until its
next launch. Source/build results do not claim that a preserved user session has
adopted the new code, nor that all memory owned by an open document or inference
model is a leak.

In isolated offscreen Dashboard navigation runs, Qt reported 23 to 32 objects
in creation when the test engine was destroyed. In one diagnostic run, the
engine's incubation controller reported zero active incubations both before and
after closing the window. The warning's cause remains unresolved; these test
observations neither measure nor establish a bound on an installed app's peak
memory.

### Native lifecycle measurements, 2026-11-01

An opt-in probe in `tests/LifecycleRuntimeProbe.cpp` records the first native
frame swap, a 20 ms GUI heartbeat, and `aboutToQuit` when
`SOCIETY_LIFECYCLE_RUNTIME_PROBE` is built and an isolated fixture is explicitly
enabled. `tests/measure_native_lifecycle.py` starts the canonical bundle with
an empty container under `resources/.state`; account restore, the system drive
mount, Bluetooth discovery, login-service registration and local MCP registration
are disabled. The local account broker and graphics caches use the fixture.
Photos are not exercised as an authenticated library. The operator uses the
actual Cocoa window to navigate and quit; the harness observes process exit.

Two runs of the final Developer ID signed bundle took 3.847 s and 2.633 s from
process launch to the first Metal frame. Time before `main()` was 2.220 s and
0.334 s respectively. Quitting after opening Projects took 0.423 s; requesting
quit immediately after selecting Projects took 0.214 s. Exit times include the
harness polling interval of up to 50 ms. Both processes exited normally.

Before the threaded input material, two Projects openings produced heartbeat
gaps of 0.908 s and 1.229 s. After the change, the first measured Projects
opening produced no gap of 150 ms or more during the observed transition; the
largest gap in that run was 0.344 s immediately after the first frame. These
are individual observations, not a statistical benchmark. A process sample
before the fix located the main-thread work in Qt Canvas inset-shadow blur.

Cold startup remains a limitation. Earlier runs of this signed build family
took 34.156 s and 22.513 s to the first frame; one spent 24.966 s before
`main()`, with a process sample inside the macOS dynamic loader's file mapping.
An earlier ad-hoc signed build also generated macOS code-signature validation
messages. Using a Developer ID removed those messages in the sampled period but
did not eliminate the cold delay. These isolated runs do not establish launch
or shutdown latency for a signed-in account, populated storage, active imports,
or another machine. No installed application was replaced.

### Verification record, 2026-10-17

Both source compilation and the canonical bundle deployment completed. The
packaged native launch, footprint, pairing, daemon, network-drive, dependency
ownership and MCP checks passed. The calendar regression passed after removing
redundant durable fixture writes, and the encrypted-state migration regression
passed with its isolated legacy fixture. Reports and logs are in
`build/optimization-20261017/verification-summary.json`.

The complete regression run is not fully green. The Drive and QuickGenerate
groups still report the mobile search-close assertion in
`mobileViewsShareDesktopContentAndKeepState`, also recorded in the earlier
bundle-footprint work. These UI behavior failures are retained in the report;
passing targeted performance and package checks does not supersede them.
The build candidate was not copied over the installed application, and the
user's existing application processes were preserved.
