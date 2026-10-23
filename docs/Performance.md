# Runtime efficiency and dependency ownership

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
separate model-packaging component. The Unix desktop MCP endpoint retains its
explicit iiLocalLLM dependency.

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
Helper installation may be placed in `build/runtime-sdk/iiSocietyHelper`, and the
presentation-aware iiWebServer in `build/runtime-sdk/iiWebServer`, without
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
