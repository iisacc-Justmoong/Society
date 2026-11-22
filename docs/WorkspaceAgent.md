# Workspace agent

## Shared iisaccson residency

For the selected Society drive, the daemon prepares and actually loads iisaccson
in the background, then pins the loaded model for the daemon's lifetime or until
the drive changes. Package readiness is distinct from runtime residency. A
loading or load-error state is reported explicitly. On a drive change or daemon
shutdown, in-flight app requests are cancelled and the old runtime is released.
Applications use the same-user daemon endpoint for independent, bounded,
stateless requests; responses and cancellation are scoped to the originating
connection and request. A client does not receive the shared Society workspace
transcript. No model weights are sent to clients. Readiness changes are events,
not a reason to poll. Absence, unsupported models and memory limits are errors,
not claims that inference is ready.
Pending stream events are bounded; a client that cannot keep up has its own
request cancelled and receives a terminal error rather than causing unbounded
daemon memory growth.

The Workspace agent panel is a client of the Society background service. The
service owns local model discovery and iiMultimodalAgent sessions; closing the
window does not cancel an accepted conversation. Model loading and generation
must never run on the UI or backend event loop. The user chooses a registered
chat model before submitting a message. A model identifier is a catalog entry,
not a content hash or an inferred file identity. Catalog discovery alone does
not mean that inference is ready: the daemon separately prepares, loads and
pins the iisaccson runtime in its background worker.

The panel displays the current conversation, generation state, and failures. A
new conversation starts a distinct session and stopping an active run requests
cancellation. A failed or disconnected submission must not silently discard
the draft. Snapshot and event messages must not be broadcast to unrelated
clients. Replies from an older run must not overwrite a newer session.
An accepted message waits for model preparation and loading instead of being
discarded; the snapshot distinguishes queued preparation, residency, routing,
generation, cancellation and failures. An empty completion is a retryable
failure, not an answer. A streamed answer remains valid when the terminal
event has no final text. The client keeps this status visible above the
transcript while preserving the composer and cancellation controls.
Stale preparation events from a cancelled model selection cannot fail or
overwrite a newer request after the user selects that model again.
When iiLocalLLM reports insufficient available memory, the daemon waits and
retries admission after memory increases, with a periodic retry even when a
change cannot be measured. It does not bypass memory or integrity checks. Stopping a queued
request removes it without cancelling other clients' shared model preparation.
Other load failures are reported with their reason and can be retried after the
underlying condition is corrected. The shared service retains one KV context:
its scheduler executes one inference at a time, and other sessions may rebuild
their context when needed.
The client also fences requests by container-selection generation and uses a
fresh subscription token after each selection, including an A → B → A switch.

The selected default is included as a routing preference, not a forced model;
iiDecision can still select another eligible route for a particular turn.
Changing the default retains the current conversation and its session history.
`society-cli agent status|new|cancel`, `agent select MODEL_ID`, and
`agent send --stdin [--attach FILE]... [--container DIR]` access the same background session; `send` acknowledges
submission and `status` reads its latest snapshot. The Python `Client` exposes
the corresponding `agent_*` methods. The daemon MCP server offers
`workspace_agent_status`, `workspace_agent_send`, `workspace_agent_new` and
`workspace_agent_cancel`, plus `workspace_agent_select` to set a default
without disabling automatic routing. A lost acknowledgement is uncertain and is not
automatically retried. CLI prompts travel through stdin instead of arguments.

## Space agent sessions

Space uses the same daemon, model scheduler, decision policy and Society tool
permissions as Workspace, but each Space session has its own persistent
iiMultimodalAgent transcript. Space transcripts are stored separately from
Workspace transcripts under the container's private runtime state. The
`SpaceStore` graph retains the binding, and a bounded excerpt of its saved ideas
is sent as untrusted context. Only one Workspace or Space run occupies the
shared runtime at a time. Session and container generations and subscription
tokens prevent late events from being displayed in another session.

The user may select up to eight local text documents or photos for a Space turn,
including files outside Society Files. The daemon snapshots the selected files;
it does not modify their originals. Supported text is UTF-8 and limited to 32 KiB
per document. Supported photos are PNG, JPEG, BMP or GIF; WAV, MP3 and FLAC
attachments also require a model that supports audio input. The combined prepared
attachments are limited to 512 KiB and each selected source is limited to 32 MiB.
When needed to share that budget, photos are decoded, reduced to at most 1280
pixels per side, and compressed as JPEG. Unsupported, changed or
oversize files produce a visible error. Encoded attachment contents become part
of the private agent transcript, so earlier media can also consume the model's
input limit on later turns. This explicit Space selection does not expand the
Workspace attachment scope.

Image analysis requires a chat model package with a verified projector and an
`image` or `vision` capability in its catalog manifest. The runtime still checks
that the projector supports vision. The reconstructed iisaccson and Asimov
routes currently provide text only; if no eligible image route is available,
the request fails or is deferred rather than silently omitting the photo.
Reasoning profiles bound generation and agent turns: `low` is 128 tokens / 4
turns, `standard` 256 / 8, and `high` 512 / 16. These limits do not guarantee
answer quality. Voice input and its platform availability are documented in
[Space discovery view](GraphNodes.md#space-discovery-view).

The same session API is available to local automation through
`society-cli space status|cancel --session ID`, and
`society-cli space send --stdin --session ID [--attach FILE]... [--effort low|standard|high] [--container DIR]`.
Status and cancel also accept `--container DIR`. Python exposes `space_status`,
`space_send` and `space_cancel`; daemon MCP exposes `space_agent_status`,
`space_agent_send` and `space_agent_cancel`. An accepted send acknowledges
submission, not completion. If acknowledgement is lost, inspect the session
before deciding whether to retry.

The adapter supports text conversation and selected Society tools. The daemon
provides bounded directory and resource inspection, file jobs, model imports,
their status and connected-window navigation; it captures the selected container for the session. Mutating
actions require approval for the exact tool and arguments through the existing
question interface. A job acknowledgement is not completion. See
[Workspace agent tool contract](WorkspaceAgentTools.md) for scope and failure
behavior. The caller may explicitly attach Society Files items; the daemon
captures supported bounded content and passes it to iiMultimodalAgent. The
Python `agent_send` method accepts an optional attachment list and the MCP
`workspace_agent_send` tool accepts an `attachments` array. Attachment format,
size, staging and transcript retention are described in [Native Workspace](NativeWorkspace.md).
This does not grant the agent general file-content editing or read access.
Runtime failures and unsupported model formats are reported rather
than represented as answers. The model instruction cannot guarantee that a
model will never make an incorrect claim.

The inference runtime is iiLocalLLM; iiMultimodalAgent owns the session and run
orchestration. iisaccson is the preferred conversational entry point; model
selection and escalation are decided dynamically through iiDecision. Asimov is
an additional reasoning route, not a model invoked on a fixed schedule.
iiDecision remains enabled. With no host assessment policy, the user-approved
bootstrap asks iisaccson for relative success, gain, loss and cost estimates
for the available text routes. These are explicitly labelled **uncalibrated
model estimates** in the interface; they are not measurements or verified
facts. The same selected-route estimate is used for the user-requested text
inference admission. It does not grant a tool, filesystem or external action.
Missing, malformed, cancelled or ineligible estimates defer or fail explicitly.
The same decision gate evaluates each proposed tool using separate uncalibrated
estimates; this evaluation does not grant approval for changes.
Routing considers a bounded excerpt of the current request, at most four recent bounded messages,
and each candidate's package-reported architecture and size where available.
Those package descriptions remain unverified claims until component validation;
the estimated cost is not a live measurement of time, energy or memory.
The panel retains a bounded recent snapshot, while the agent transcript is
stored by iiMultimodalAgent.

An operator can instead provide a private `agent/decision.json` under the
selected container's Society runtime state. Its schema is the iiMultimodalAgent
`decisionOptionsFromJson` format, with host-assessed `run` and
`model:iisaccson` / `model:asimov` profiles, plus profiles or assessments for
the actual tool names when tools are used. The adapter retains the SDK's
thresholds and reloads the assessments at each decision; policy threshold
changes take effect when the backend reconnects to the container. The container
owns this file; neither the chat message nor model output can edit it. No
sample probabilities or outcome values are installed as facts.

Models must be verified iiLocalLLM catalog packages with chat support; a Society
model storage file is not automatically such a package. In particular, the
Asimov package contains a draft component, a language model and a projector,
and its legacy roles do not uniquely identify the language model. The adapter
selects an unambiguous language component from its manifest and verifies it
before loading; discovery alone does not prove that inference will succeed.
The Asimov route uses its embedded Nemotron text model only; its LiLiCorr
draft model and multimodal projector are not attached by this adapter.
Catalog and session data are private runtime state, not files displayed
in Society Files. The packaging SDK materializes the selected embedded
language-model component by its verified package path, without reconstructing
unrelated components. This can require additional free space and take time;
cancellation removes the adapter's own temporary files. iiLocalLLM rechecks the retained
component before loading it. The source Society model remains unchanged. The
digest in an internal inference-package URI identifies cached bytes; it is not
the logical Society resource ID or evidence that distinct resources are equal.
The reconstructed text-model manifest currently limits the inference context
to 2,048 tokens; longer conversations can hit the runtime context limit.

The daemon records the app-selected model through iiSocietyContainer's private
model cache and restores that preference when the same resource and revision
are still available. iisaccson remains the default and the model used for
uncalibrated routing assessments. Selecting another preference does not disable
iiDecision or force every request to that model. The selected package is prepared
on the agent worker before it is requested; the GUI and container file workers
do not perform the conversion. Changing the selection or container cancels the
previous preparation. The agent snapshot reports cache state, progress, reuse and
errors. The selected cache is queued before separate resident model loading.
Cache preparation does not run inference. The separate resident state
reports whether iiLocalLLM actually loaded and pinned the weights.

The cache retains the existing private `agent/models` catalog so installed model
entries are not lost. A valid prepared component is reused without extracting it
again. The original Society model is never moved or replaced. A provider resource
ID is used when available; otherwise the reference is explicitly path-scoped and
is not treated as a stable resource identity. iiLocalLLM still verifies model
contents before loading, and a cache on the same device does not guarantee faster
physical reads. No automatic cache eviction or storage limit is currently set.

Building the workspace agent requires an iiSocietyContainer SDK that exports
`iiSocietyContainer_HAS_MODEL_CACHE`; use matching headers and libraries from
the rebuilt SDK. A successful local build does not update an installed app.
The iiLocalLLM runtime must also include the layer-aware GGUF KV reservation
fix to avoid over-reserving iisaccson's per-layer head arrays. Older runtimes
retain the conservative fallback and may continue waiting for more memory.

A ready cache has an initialized inference-service delegate, but does not
establish successful runtime loading. The resident status reports that result.
If the model cannot be loaded, the daemon reports the error; preparation itself
does not run inference. On desktop Windows and Linux the backend requires the
matching iiLocalDiffusion model-packaging, iiMultimodalAgent and iiLocalLLM SDK
packages. Their native build and runtime behavior must be verified separately.
The local desktop broker is not a mobile background-service implementation.

The daemon reacts to Society model-import, organization and recategorization
events affecting `iisaccson.safetensors`. A background metadata check keeps
the current loaded model when the component digest is unchanged. A new or
unavailable component reinitializes the selected drive agent and cancels its
in-flight requests; an import can also retry an earlier load failure. Society
retains the workspace conversation and its persisted session when the same
drive is refreshed. Interrupted workspace responses remain visible and retryable.
Arbitrary external filesystem changes are not observed by this import-event path.

### Per-device llama execution policy

The backend optionally reads `iisacc/Society/agent-runtime.json` from the
platform's per-user generic configuration directory when creating its local
inference service. The optional `llama.maxGpuLayers` integer is `-1` for
automatic device selection, `0` for CPU, or a positive GPU layer limit. If the
file or field is absent, the existing automatic policy remains unchanged. An
unreadable, malformed, or invalid policy is reported as a preparation error.
For example, `{"llama":{"maxGpuLayers":0}}` selects CPU for this device.
The policy belongs to this device and user, not to the Society container or a
model manifest. It changes neither model routing nor the memory and integrity
checks. Restart the backend after changing it. CPU execution is a recovery
option when the native GPU runtime cannot complete a request; it can be slower.
When this policy explicitly selects CPU, the resident model disables llama.cpp's
optional CPU weight repacking. This keeps the existing mapped weights instead of
creating a second, eagerly repacked copy. Other execution policies keep their
existing defaults; CPU token generation may be slower without the optimized copy.
