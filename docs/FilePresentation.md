# File information and keyboard previews

The file inspector loads persistent iiFileProvider 0.7 details and content
previews asynchronously. It shows real source information rather than placeholder
file data. The preview remains part of the portable file asset through its native
attribute and hidden companion, with source identity/change validation.

Select a local file in the list or grid and press **Space** to open the LVRS
preview surface. Press Space or Escape to close it. The Open action retains the
existing application/file activation behavior. Image previews preserve aspect
and alpha; text previews escape markup; unsupported content shows an explicit
information card. IISC previews use iiSocietyHelper's registered iiSharedCanvas
compositor. Remote entries are inspected after local materialization; selecting
one never silently downloads it for metadata.

The selected child must belong directly to the validated visible folder and must
not be redirected. Late asynchronous results cannot replace a newer selection.
Changing folders clears the inspector and closes the preview. Native provider
control documents remain excluded from listings and dashboards.

Inside a Society container the inspector consumes the common
`SharedStorage::fileInformation` projection. The selected-file details now include
published titles, descriptions, keywords, 0–5 ratings, color labels, collection
names, custom fields, MIME type and supported format-specific properties such as
audio rate/channels/duration, camera capture/exposure and text encoding. Missing
classification is distinct from invalid classification; damaged annotations do
not hide a readable preview. Classification parsing belongs to iiFileProvider,
and inspection does not initialize another catalog or rewrite the original.
The SDK's asynchronous host inspection API also exposes the same data without
materializing a remote original. This browser's existing local-folder selection
uses local inspection; its remote open/download interaction remains explicit.

New file saves use provider completion hooks. External files arriving in the
Society drive are handled by the native intake monitor while a storage consumer
or daemon owns that drive. See
[the SDK contract](../../../SDK/iiFileProvider/docs/FILE_PRESENTATION.md) for
embedding, codec support and limits. The keyboard surface described here is in
Society; it does not itself register a Finder extension for a custom file type.

Source regression verifies Space/Escape behavior, actual preview data, existing
file activation, native path boundaries and table/grid behavior. The richer
container-selection regression also checks the actual rendered inspector rows
for title, keywords, rating, color and custom fields, plus a decoded Provider
text preview. SDK stage and
test-consumer validation are separate from deployment of a running app.
