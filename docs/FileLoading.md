# File loading

Society registers the SDK `society-preview` asynchronous image provider at engine
startup. Files and Gallery use `fileThumbnailUrl`, and Dashboard uses the same cache
through `thumbnailSource` (falling back to the original `previewSource`). Original-file activation and on-demand download routes do
not change. The UI continues using installed LVRS components.

The application cache contains directory snapshots, source SHA-256 manifests and
512-pixel previews. No originals, credentials or pairing data are moved into it.
Cached rows paint before live reconciliation; they do not authorize opening until
the model is Ready. The previous per-second whole-directory scan becomes a debounced
filesystem watcher plus a ten-second fallback. Dashboard sections and requested
preview jobs run in parallel with explicit decoder memory limits.

Implementation limits, invalidation and test commands are in
[the SDK contract](../../../SDK/iiSocietyContainer/FileLoading.md).
Source/SDK test results, installed application validation and real-device behavior
must be reported separately; a benchmark of warm cache hits is not cold-start proof.

The synchronization SDK also uses bounded streaming watch discovery. It prioritizes
section roots, stops at its descriptor/inspection budget, and checks cancellation
between entries rather than eagerly sorting every file in a large preview folder.
This reduces background filesystem contention; it does not make stalled physical
storage fast or remove the host-ownership shutdown barrier. See
[watch discovery](../../../SDK/iiSocietySync/NearbySync.md#bounded-filesystem-watch-discovery).
