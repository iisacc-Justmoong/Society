# Suggested unfinished work

Suggested is a read-only recommendation view over the selected Society drive's
Files catalog. It is not a directory listing. Recommendations refer to existing
resources and retain their catalog identity and content version; a file path is
only the current locator. Opening a recommendation uses the container's normal
validated file activation path.

The daemon evaluates indexed resident files in the background. Explicit,
recorded workflow status takes precedence: draft, work in progress and
needs revision are candidates; complete, approved, released and published are
excluded. Open Markdown checkboxes and explicit TODO/FIXME markers in a bounded
text prefix are additional signals, as are clear draft markers in file names.
Modification time alone never proves incompleteness. A missing, stale or
conflicting status is unknown, not a completed file. Private metadata and
control files are not candidates. The supported status fields are
`workflow.status`, `workflow:status` and `status` in iiFileProvider
classification. Status fields marked with AI/model/agent provenance are not
treated as rule evidence; conflicting statuses are deferred. An annotation
whose metadata publication is pending is not used as a recommendation.

Only unresolved candidates with usable descriptive evidence go to the local
iiMultimodalAgent model. The model's estimate and explanation are uncalibrated
and must not be recorded as authored facts. iiDecision evaluates the proposed
recommendation using the estimate; absent, invalid or insufficient evidence
defers instead of inventing a result. The model receives a bounded text/metadata
projection, does not gain tool or file-write permissions, and does not claim to
have examined image pixels, audio or video. Recommendations from rules and
model estimates remain distinguishable in the view. The application reports
indexing, unavailability and partial results honestly, and retains rule-based
recommendations if the model is unavailable.

When more than one text model is available, the same iiDecision model-routing
policy used by the workspace agent selects the assessment model. In the
absence of a host policy, iisaccson supplies uncalibrated routing estimates;
Asimov or another compatible local model can be selected if the decision
supports it. The routing request contains only the bounded file evidence, not
the user's chat history. If no route meets the policy, assessment is deferred.

The scanner reads at most 4,096 indexed entries per refresh and presents at most
200 recommendations; it marks the result partial when a limit is reached or
an indexed entry cannot yet be read or matched to a resource safely.
Rule-based results have priority; if they already fill the visible limit, the
daemon does not load a model to evaluate items that cannot be shown.
Each refresh assesses at most 64 previously uncached uncertain files in
batches of four. Further files are marked partial and can be assessed in later
refreshes as successful estimates enter the version-scoped cache.
For `.txt`, `.md` and `.markdown` it reads at most 4 KiB of valid UTF-8; other
files use classification metadata and a verified one-byte identity check.
No image pixels, canvas contents or file payloads of other formats are sent to
the model. Files with insufficient descriptive evidence remain unassessed.
An index that does not yet exist is reported as pending, rather than built by
the GUI or treated as an empty completed inventory.

Evaluation does not run on the GUI or file-operation thread. The first
Suggested subscription activates it; the daemon refreshes after successful
file jobs, on a debounced file-list change, and periodically when idle. A
user-requested chat cancels lower-priority model assessment using the SDK's
cooperative cancellation token. A runtime that does not promptly honor
cancellation can delay the next model call, but not the GUI or file jobs.
The in-memory cache is tied to resource identity, observed working-copy version,
metadata revision and committed revision; it is not persisted as metadata.
Results are bounded, invalidated on container changes and rechecked against the
observed content version before publication. A late result from an old scan
must not replace a newer scan, or another container's state. Result caches are
advisory and do not become asset identity, ownership, lifecycle state, or a new
content revision.

The Suggested table adds a **Reason** column in the Society
[Figma view](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=18-14).
It uses the LVUI table cell component. Recent remains a separate chronological
file view.

`society-cli suggested status|refresh`, the Python client's
`suggested_status()` / `suggested_refresh()`, and the daemon's
`suggested_status` / `suggested_refresh` MCP tools access the same snapshot.
On builds with the daemon agent backend enabled (currently Unix platforms),
only verified local application peers can subscribe; a refresh schedules work
and returns the current snapshot without waiting for the model. Both MCP tools
are declared non-read-only because they can schedule indexing or model
preparation, and the daemon's tool policy explicitly permits those operations.

The current macOS agent runtime also exports an older `FileCatalog` ABI. The
daemon and test hosts link the current catalog before the agent library so a
catalog object's constructors and destructor resolve to one implementation.
`Society.SuggestedCatalogAbi` checks the resulting Mach-O bindings; rebuilding
or replacing either SDK requires keeping that check green.
