# Workspace Suggested and Recent views

## Ribbon and selection contract

Suggested and Recent share the Figma File menu and Home, Share, View and
Manage ribbon tabs. Switching tabs or collapsing the ribbon must not change
the current page, search, selection, or agent conversation. The commands are
enabled only when their operation is available for the current selection and
the current Society drive. A submitted file job is not reported as complete
until the daemon confirms its outcome. A changed drive or disconnected client
must not silently replay a request. A filtered or replaced row is removed from
the selection before any action uses it. Recent remains limited to the 500
latest activity entries, independent of the ribbon state.

The Workspace sidebar routes **Suggested** and **Recent** to separate LVUI
views inside the Workspace segment. Suggested shows daemon-owned, indexed
unfinished-work recommendations with a reason; Recent uses the container's
recent-file snapshot. Neither view creates sample files or
claims that a path is a persistent resource identifier. Projects, Calendar,
Activity, guilds and organizations retain their existing destination handlers.

The shared browser provides list and grid presentation, search, sorting,
selection and opening through the existing container file activation path.
The table supports contiguous selection with Shift; the grid supports multiple
selected items. Select all and Clear operate on the visible snapshot. Invert
is available for any grid selection and for table selections whose complement
is contiguous, because the LVUI table currently represents a single range.
Changing the visible row order, replacing a known resource ID at the same path,
or changing the observed version of a row without an ID clears the selection
before an action can use a stale locator. This observation is not a substitute
for an atomic identity check at the time a file job executes. Grid entries use
file labels rather than thumbnails.

The ribbon provides the Figma Home, Share, View and Manage groups and File
menu. Copy exports file URLs to the platform clipboard; Copy path copies
locators as text. Paste requests a destination under `Files/` and submits a
separate copy job for each local clipboard URL. New folder creates a directory
in the `Files/` root. Rename, Move to, Copy to, Duplicate and Delete use the
existing iiSocietyContainer file-job service; Delete moves items to the
container's Deleted area. The properties dialog displays the selected
snapshot, including a resource ID and recommendation reason only when
provided. Copy ID copies the resource ID and is unavailable when the snapshot
does not contain one; paths and hashes are never substituted for it.

Each operation is independently submitted to the daemon. The UI distinguishes
submission, queued and completed states, checks the returned job after
subscription races or reconnects, and keeps errors visible until dismissed.
If the drive changes or a submission becomes uncertain, requests are not
silently retried. At most 64 requests or jobs are tracked at once; the UI
reports when remaining selected files were not submitted. Menu commands that
do not yet have an implementation, including Cut, direct import, sharing,
revision, replication, release and history operations, remain disabled rather
than claiming to perform them. Existing Storage functionality remains separate.

The agent pane displays the selected default, conversation messages, progress,
errors and cancellation using the daemon-owned agent connection described in
[WorkspaceAgent](WorkspaceAgent.md). The available Society agent tools and
their approval boundary are described in [WorkspaceAgentTools](WorkspaceAgentTools.md).
The panel cannot attach or read file contents yet, so its Add control remains
disabled. UI state belongs to the view model, and the desktop shell adapts
backend and input events.

Recent shows at most 500 files, ordered by the most recent observed file
activity. Society records successful file-open requests and completed file
operations in private, daemon-owned storage. The recorded activity follows a
verified native file identity through a rename; a different or ambiguous file
is not merged by name or content hash. The later of recorded activity and the
observed modification time determines the order. Background scans and thumbnail
reads are not counted as user activity. External application opens are not
observed, and earlier activity cannot be reconstructed from modification time.
Directory entries and private control files are excluded. An `.iiproject`
directory package is one logical file; its internal files are not listed.
Its size remains unknown until a provider supplies it; Recent does not traverse
the package simply to calculate a size.
The daemon asynchronously enumerates visible filesystem metadata and caches a
bounded snapshot, so an incomplete index cannot hide files. It does not read
file payloads. File changes appear on the next background refresh.
Temporary location hints are checked against the native identity on every scan;
the path is not treated as identity, and a replacement file cannot inherit it.
If a native identity cannot be verified, the file is shown using its observed
modification time and the snapshot indicates incomplete activity. A file with
multiple hard links has ambiguous identity and is handled the same way. Recent has
its own snapshot; the Dashboard's shorter card list remains separate. The daemon
retains up to 4096 recent native-identity activity records and reports failures
instead of treating a name or content hash as identity.
`society-cli recent status|refresh`, Python `recent_status()` and
`recent_refresh()`, and the daemon's `recent_status` and `recent_refresh` MCP
tools expose the same snapshot independently of the window.
Suggested preserves the daemon's recommendation order until the user selects a
sort. It covers indexed Files descendants rather than browsing a folder. Search
filters the current snapshot without enumerating files on the UI thread.
Guild and organization rows are populated from account-scoped memberships.

`NativeWorkspace.lvui` builds the sidebar, browser and agent pane using LVUI
and iiGUI. `NativeWorkspaceViewModel` owns selection, filtering and view state.
The temporary `LegacyWorkspaceBridge` presents the generated view in the
existing desktop shell and uses `iiSocietyContainer::StorageDirectoryModel` to
activate files. The recommendation and its provenance are described in
[WorkspaceSuggested](WorkspaceSuggested.md). At widths below 760 px the existing mobile layout is
retained. The first application segment returns to Workspace; Hosting retains
its environment route. File mutations are submitted to the daemon rather than
performed by the GUI. The current paint adapter has no display-frame source,
so its LVUI dialogs use the library's non-animated presentation and flat input
fields. The adapter does not claim to support material blur or shadows.

The current desktop paint adapter does not establish full platform input-method
and accessibility parity with native application windows. An accepted run
continues in the background if the window disconnects; its current snapshot
can be read when the client reconnects. Session transcripts are retained by
iiMultimodalAgent, but browsing historical sessions after a daemon restart is
not connected to this panel yet.

The Figma examples include [Suggested](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=18-14),
[Recent](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=957-9435),
the [ribbon](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=1026-6203)
and the [empty agent panel](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=984-5384).
The sample names and conversation are illustrative and are not product data.
