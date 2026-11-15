# Workspace agent

The Workspace agent panel is a client of the Society background service. The
service owns local model discovery and iiMultimodalAgent sessions; closing the
window does not cancel an accepted conversation. Model loading and generation
must never run on the UI or backend event loop. The user chooses a registered
chat model before submitting a message. A model identifier is a catalog entry,
not a content hash or an inferred file identity. Catalog discovery does not
initialize iiLocalLLM's inference runtime; the runtime is created on the first
accepted conversation that needs inference.

The panel displays the current conversation, generation state, and failures. A
new conversation starts a distinct session and stopping an active run requests
cancellation. A failed or disconnected submission must not silently discard
the draft. Snapshot and event messages must not be broadcast to unrelated
clients. Replies from an older run must not overwrite a newer session.
The client also fences requests by container-selection generation and uses a
fresh subscription token after each selection, including an A → B → A switch.

The selected default is included as a routing preference, not a forced model;
iiDecision can still select another eligible route for a particular turn.
Changing the default retains the current conversation and its session history.
`society-cli agent status|new|cancel`, `agent select MODEL_ID`, and
`agent send --stdin` access the same background session; `send` acknowledges
submission and `status` reads its latest snapshot. The Python `Client` exposes
the corresponding `agent_*` methods. The daemon MCP server offers
`workspace_agent_status`, `workspace_agent_send`, `workspace_agent_new` and
`workspace_agent_cancel`, plus `workspace_agent_select` to set a default
without disabling automatic routing. A lost acknowledgement is uncertain and is not
automatically retried. CLI prompts travel through stdin instead of arguments.

The adapter supports text conversation and selected Society tools. The daemon
provides bounded directory and resource inspection, file jobs, model imports,
their status and connected-window navigation; it captures the selected container for the session. Mutating
actions require approval for the exact tool and arguments through the existing
question interface. A job acknowledgement is not completion. See
[Workspace agent tool contract](WorkspaceAgentTools.md) for scope and failure
behavior. File attachments and reading or editing file contents are not yet
available. Runtime failures and unsupported model formats are reported rather
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
in Society Files. The installed packaging SDK has no public path-selecting
materialization API, so the first Asimov activation reconstructs its verified
package in temporary private storage and retains the exact language-model
component. This can require additional free space and take time; cancellation
removes the adapter's own temporary files. iiLocalLLM rechecks the retained
component before loading it. The source Society model remains unchanged. The
digest in an internal inference-package URI identifies cached bytes; it is not
the logical Society resource ID or evidence that distinct resources are equal.
The reconstructed text-model manifest currently limits the inference context
to 2,048 tokens; longer conversations can hit the runtime context limit.
