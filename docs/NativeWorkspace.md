# Workspace Suggested and Recent views

## 리본과 선택 계약

파일 첨부는 사용자가 선택한 파일을 명시적으로 추가한 경우에만 처리한다.
데몬은 선택된 Society 드라이브의 `Files/` 안에 있는 일반 파일인지 확인하고,
지원되는 텍스트와 미디어를 크기 제한 안에서 읽어 에이전트 요청의 실제 내용으로
전달한다. 경로 이탈, 심볼릭 링크, 파일 변경, 지원하지 않는 형식과 크기 초과는
조용히 생략하지 않고 해당 요청의 오류로 보고한다. 파일 내용은 UI 스레드에서
읽지 않는다. 첨부는 선택한 실행의 입력이며 도구 실행 권한을 부여하지 않는다.

목록과 그리드는 같은 파일 선택 상태를 사용한다. 일반 클릭, 수정키를 누른
불연속 선택, Shift 범위 선택, 전체 선택과 선택 반전은 화면에 표시된 선택과
파일 작업 대상이 일치해야 한다. 필터나 데이터 교체 후 더 이상 유효하지 않은
선택은 지운다. 목록과 그리드는 모든 파일을 LVUI 종류별 아이콘으로 표시하며,
미리보기가 없는 파일에도 기본 아이콘을 사용한다. 별도 Preview 명령은 유지한다.

리본의 열기는 선택된 파일의 부모 디렉터리를 데몬에서 열고 현재 세대와 판본을
확인한 뒤 데몬의 파일 준비 결과를 받아 실행한다. 준비 수락만으로 파일을 열었다고
표시하지 않는다. 준비 중 저장소 연결 또는 선택된 드라이브가 바뀌면 예전 응답으로
다른 파일을 열지 않는다. 파일 이동·복사·이름 변경·휴지통 이동과 폴더 생성은
각각 컨테이너 데몬에 제출하고, 실제 완료 또는 실패를 작업 결과로 알린다. 리본에서는 각 그룹의 아이콘과 텍스트를 왼쪽 정렬하고, 각 명령의 아이콘과 텍스트를 하나의 무테 버튼으로 제공한다. 그룹은 내용 너비와 LVUI 32px 간격을 유지한다. 네 줄 높이의 명령 영역 아래에 LVUI Description 스타일로 그룹 이름을 가운데 표시한다. 리본은 항상 펼쳐져 있으며, 리본 탭은 방향키로 포커스를 이동하고 Enter 또는 Space로 선택한다. 폴더 생성과 가져오기는 Home의 New 그룹에서 실행한다.

Suggested and Recent share the Home, Share, View, Manage, Add and Search ribbon tabs.
The ribbon remains expanded, and switching tabs must not
change the current page, search, selection, or agent conversation. The commands are
enabled only when their operation is available for the current selection and
the current Society drive. A submitted file job is not reported as complete
until the daemon confirms its outcome. A changed drive or disconnected client
must not silently replay a request. A filtered or replaced row is removed from
the selection before any action uses it. Recent remains limited to the 500
latest activity entries, independent of the ribbon state. Each ribbon group aligns
its command icons and labels at the leading edge and reserves space for four
command rows. The group name uses the LVUI Description style and sits centered
beneath that area, so groups with fewer commands still share a caption row.
Groups retain their content widths and use LVUI 32px spacing. Each command has
one borderless click target for its icon and label. Disabled commands stay
transparent with dimmed content. Arrow keys move focus between ribbon tabs.
Enter or Space selects the focused tab. Activating the selected tab keeps the
ribbon visible. Home's New group provides New folder and Import.

The Workspace sidebar routes **Suggested** and **Recent** to separate LVUI
views inside the Workspace segment. Suggested shows daemon-owned, indexed
unfinished-work recommendations with a reason; Recent uses the container's
recent-file snapshot. Neither view creates sample files or
claims that a path is a persistent resource identifier. Projects, Calendar,
Activity, guilds and organizations retain their existing destination handlers.

The separate controls row beneath the ribbon is removed. The View ribbon owns
list/grid presentation, sorting and navigation. The Search ribbon contains the
filename input, current view and type indicators, reset, and image, audio/video,
document, other and all-file filters. Search filters the already loaded
Suggested or Recent snapshot; it is not a whole-container index query. Query
and type filters combine, remain active when changing ribbon tabs, and apply to
the newly selected snapshot when switching between Suggested and Recent. Reset
clears both without changing the current view or sort order.

The shared browser provides list and grid presentation, search, sorting,
selection and opening through the existing container file activation path.
The list and grid use the same LVUI multiple-selection collection. A modified
click toggles an individual item, Shift selects a range, and Select all, Clear
and Invert operate on the visible snapshot. Switching between list and grid
retains the selection. View offers type filters, compact rows, extension
visibility and a status toggle. Filtering uses file extensions as a presentation
classification; it does not change the asset's stored category or metadata.
Back and Forward navigate the visited Workspace destinations, discard the
forward branch after a new destination, and retain at most 100 entries. They
do not represent filesystem directory history.
Changing the visible row order, replacing a known resource ID at the same path,
or changing the observed version of a row without an ID clears the selection
before an action can use a stale locator. This observation is not a substitute
for an atomic identity check at the time a file job executes. List and grid always
show LVUI type icons, including a fallback for unknown or extensionless files.
The original filename determines the icon even when extensions are hidden.
The explicit Preview command uses the bounded background preview cache.

The ribbon provides the Figma Home, Share, View, Manage, Add and Search tabs.
Copy exports file URLs to the platform clipboard; Copy path copies
locators as text. Paste requests a destination under `Files/` and submits a
separate copy job for each local clipboard URL. Cut marks selected files from
the current drive and Paste submits move jobs; a changed clipboard or drive is
never silently treated as authorization to move. Cut items remain in the
clipboard until each individual move is confirmed complete. A rejected item
can be submitted again; an uncertain or partially failed operation requires
the user to inspect the file and cut it again. Import accepts an
absolute local file path and a `Files/` destination, then submits a copy job;
the native file chooser is not yet connected. New folder creates a directory
in the `Files/` root. Rename, Move to, Copy to, Duplicate and Delete use the
existing iiSocietyContainer file-job service; Delete moves items to the
container's Deleted area. The properties dialog displays the selected
snapshot, including a resource ID and recommendation reason only when
provided. Inspector and Locations also request the daemon's last observed
resource, revision and location data; unknown availability is not reported as
verified protection. Copy ID copies the resource ID and is unavailable when the snapshot
does not contain one; paths and hashes are never substituted for it.
The Add tab groups creation, imports, references and project operations. New
project asks for a name and an explicit goal; new canvas asks for a name and
dimensions, creates a real `.iisc` document and reports if indexing remains
pending. Existing names are never overwritten. Import files accepts one or
more absolute paths; Import folder accepts a single absolute source path;
both use container copy jobs into `Files/`. Import models submits a model
import job. Paths are entered as text; a native chooser is not connected.
Paste and New folder use the existing file commands. The Reference group
records a user-named relationship from one selected resource to another
managed Files or Models resource, an external file, or an HTTP(S) URL; data is not
copied. Project commands add selected or explicitly named resources by
reference and record the stated purpose, or create a relationship between
existing project objects using a project schema type. Project names must be
unambiguous; project and graph IDs can be used instead. Project updates use
the loaded revision and reject conflicting changes. Multiple selections
can partially succeed, and their result says to review before retrying.
Creation and relationship requests run on a daemon worker, but are not
resumable jobs after daemon restart; a disconnected or timed-out client must
inspect the destination before retrying an uncertain operation.
Project references to Files are bound to the Files catalog when available.
External files and Models resources currently remain path references, so a
later external move or model relocation is not guaranteed to follow them.
The build requires an iiSocietyContainer package advertising
`iiSocietyContainer_HAS_WAITING_FILE_JOBS`; independent file jobs wait on the
container commit lock on workers instead of failing after a short timeout.
An existing matching reference keeps its relationship metadata. A bounded
retry handles unrelated graph revision changes without overwriting an edge.
Internal container control files cannot be selected as resources or disguised
as external files. External paths must resolve directly without redirection.

Each operation is independently submitted to the daemon. The UI distinguishes
submission, queued, recorded and completed states, listens for daemon job
events, and checks known jobs after reconnect or an explicit refresh. A job
that has not yet been recorded is not reported as failed merely because its
status is not available. Errors remain visible until dismissed.
If the drive changes or a submission becomes uncertain, requests are not
silently retried. At most 64 requests or jobs are tracked at once; the UI
reports when remaining selected files were not submitted. Menu commands that
do not yet have an implementation, including sharing,
revision, replication, release and history operations, remain disabled rather
than claiming to perform them. Existing Storage functionality remains separate.

The agent pane displays the selected default, conversation messages, progress,
errors and cancellation using the daemon-owned agent connection described in
[WorkspaceAgent](WorkspaceAgent.md). The available Society agent tools and
their approval boundary are described in [WorkspaceAgentTools](WorkspaceAgentTools.md).
The current preparation, memory-wait, routing or response state remains above
the transcript. Failures also appear in the conversation with their full reason,
and Stop cancels a queued request. Context attachments and the model menu
reserve vertical space so that the message composer remains reachable.
The transcript follows new messages and streamed text when the user is at the
bottom; scrolling up to read earlier messages preserves that position. The Qt host converts system-adjusted wheel and trackpad deltas into iiGUI scroll offsets, including horizontal movement and precise pixel deltas.
The panel adds selected files as explicit attachments and permits individual
removal. A request admits up to eight files and 512 KiB total; supported UTF-8
text files are limited to 32 KiB each, while PNG, JPEG, BMP, GIF, WAV, MP3 and
FLAC are passed as native media. Unsupported, redirected, changed, empty and
oversized files fail the request. Attachments are captured on the daemon worker
using iiFileProvider's stable source snapshot; temporary captures are removed
after use. Their contents become part of the agent session transcript. Actual
media interpretation depends on a compatible routed model. UI state belongs
to the view model, and the desktop shell adapts backend and input events.

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
existing desktop shell. It requests directory state and file preparation from
the daemon, which uses `iiSocietyContainer::StorageDirectoryModel`; the GUI
opens only the exact path confirmed by the current request. The recommendation and its provenance are described in
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

## 채팅 텍스트의 픽셀 품질

네이티브 작업 공간의 채팅 본문은 읽기 전용 LVUI 입력 필드로, 보조 문구는 레이블로 표시된다. 두 경로 모두 LVUI가 글꼴 스타일을 전달하고 iiGUI가 한글을 조판해 화면 배율에 맞는 픽셀을 만든다. 전환용 Qt 호스트는 이 픽셀을 배치해 합성할 때 같은 크기의 글자 래스터를 다시 보간하지 않는다. 보조 문구는 내용, 스타일, 배율과 글꼴이 같으면 제한된 캐시에서 글자 래스터를 재사용한다. 사진과 아이콘의 보간은 기존과 같다.

`Society.PaintDevicePixels`는 창을 열지 않고 한글과 영문이 섞인 레이블의 자간과 잘림, 입력 필드 비트맵의 소수점 위치 보정과 반투명 픽셀 합성을 1×·1.25×·2×·3×에서 확인한다.
