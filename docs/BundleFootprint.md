# macOS bundle footprint

Society ships the native storage, synchronization, calendar, photo, account,
model packaging, local MCP, and host image-generation features in one application.
Model weights and user data belong to the selected Society container, outside
the application bundle.

## Causes of the previous footprint

The inspected 2026-10-15 installed bundle contained 1,024,582,540 file bytes
(977.12 MiB) in 9,925 files. Frameworks accounted for 681.38 MiB; the QML runtime
tree accounted for 243.14 MiB. `libtorch_cpu.dylib` alone occupied 320.40 MiB.
The QML tree also included dSYM bundles, designer metadata, alternate Controls
styles, virtual keyboard modules, and other child modules that Society did not
import. Repeated deployment retained old Frameworks entries.

The host image-generation feature genuinely depends on iiLocalDiffusion. Its
native image API uses the native checkpoint backend, including Metal. The
separate optional LibTorch linear-compute backend is not used by this API.
Society therefore consumes a dedicated iiLocalDiffusion package with
`IILD_ENABLE_LIBTORCH=OFF`. Native diffusion, MLX, Core ML, and pose support stay
enabled. Global SDK installations and other applications retain their own SDK
profiles.

## Measured result

The optimized arm64 Release application installed on 2026-10-15 contains
363,557,012 file bytes (346.71 MiB) in 4,068 files. This saves 630.40 MiB, or
64.52 percent of the previous bundle. File count falls by 59.01 percent.

| Area | Before | After |
| --- | ---: | ---: |
| Entire application | 977.12 MiB | 346.71 MiB |
| Native frameworks and libraries | 681.38 MiB | 290.39 MiB |
| Filesystem QML runtime | 243.14 MiB | 6.21 MiB |

The remaining large components include native image generation (53.26 MiB),
the Society executable (36.25 MiB), LVRS with embedded resources (35.21 MiB),
ONNX Runtime (27.36 MiB), local LLM support (17.86 MiB), MLX (17.71 MiB), and
the Qt runtime. Removing these requires a separate feature or dependency change.

The installed application passed deep signature verification and matched the
canonical build's recorded hashes. Dashboard, Tools, and Storage were inspected
in the newly launched installed application. The selected container identity and
the running container monitor instance were preserved.

## Prepare and build

From the Society repository:

```sh
python3 tools/build_macos_inference_sdk.py --sdk-source ../../SDK/iiLocalDiffusion
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DiiLocalDiffusion_DIR="$PWD/build/runtime-sdk/iiLocalDiffusion/install/lib/cmake/iiLocalDiffusion"
cmake --build build --target Society --parallel 6
```

The preparation tool builds and tests the existing SDK options, installs them
only below `build/runtime-sdk/iiLocalDiffusion/install`, and records `profile.json`.
A standalone product checkout can instead consume a prebuilt package with the
same profile through `iiLocalDiffusion_DIR`. The configure step rejects a runtime
that loads LibTorch so a broad development SDK cannot silently inflate releases.

The current file-creation migration also requires iiFileProvider 0.6. Install
that SDK normally, or select its matching `iiFileProvider_DIR`,
`iiFileProviderCatalog_DIR`, and `iiFileProviderTemporary_DIR` packages. Do not
combine the 0.6 Qt package with an older Catalog package. The verified Workspace
build used `SDK/iiFileProvider/build/ownership-migration/install` without changing
the user's global SDK installation.

## Deployment contract

The shared deployment policy is vendored under `cmake/single-app/`. Society opts
into compact deployment with its actual Basic Controls style:

- Copy imported QML modules separately; do not recursively copy unrelated child modules.
- Keep QML, JavaScript, images, fonts, shaders, and native plugins.
- Preserve private QML directories whose `qmldir` only specifies a resource
  preference; they are not separate imported modules.
- Exclude dSYM bundles, `.qmltypes` editor descriptions, and designer directories.
- Keep Basic and its implementation modules; omit optional alternate styles.
- Use the fonts, icons, and images already embedded in the LVRS shared library
  instead of copying another filesystem set. Preserve accompanying filesystem
  licenses. `Society.BundledResources` loads the actual bundled Qt Core and LVRS
  and checks every omitted asset through its public Qt resource URL.
- Remove stale native libraries/frameworks outside the dependency closure of the
  app, plain helper executables, and runtime plugins.
- Strip local/debug symbols in Release and MinSizeRel while preserving exported
  symbols required by dynamic linking and plugins. Debug symbols remain in the
  original SDK/build artifacts.
- Relocate dependencies and sign the final self-contained bundle.

The canonical output remains `build/bin/Society.app`. The embedded
`SocietyDaemon` and `SocietyContainerDaemon` remain plain executables in
`Contents/MacOS`; no second application bundle is created below `build/`.

## Verification

```sh
python3 tests/test_macos_runtime.py
cmake --build build --target SocietyBundledResourcesTests --parallel 6
python3 tests/verify_macos_footprint.py build/bin/Society.app
python3 tests/verify_macos_runtime.py build/bin/Society.app --gui
ctest --test-dir build -R 'Society\.(BundledResources|MacBundleFootprint|MacRuntimeDeployment|MacRuntimeLaunch|Mcp|ModelPackaging|ModelMerge|Network|Daemon)' --output-on-failure
```

`Society.MacBundleFootprint` enforces a 512 MiB file-size budget and checks required
helpers, LVRS, Basic Controls, and native image-generation runtime. LibTorch,
Qt3D, virtual keyboard modules, alternate styles, and development resources are
rejected. Build-time footprint validation runs after runtime deployment.

The SDK profile tests cover native generation output/error contracts and model
metadata. Product tests additionally cover packaging, merge tooling, host/peer
transport, daemon behavior, MCP, and GUI loading. These are separate from a new
large-model generation run. Installed-bundle signatures, runtime libraries,
container identity, and real application views must also be checked after an
installation update.

For this change, all ten selected packaging/runtime CTest groups passed, with
the dependency-presence group passing an isolated retry after its first
30-second timeout; its deadline and assertions were unchanged. The Cocoa/Metal
GUI checks passed eight cases. Shared deployment policy checks passed twelve
tests, native deployment checks passed nine, and native SDK profile checks passed
three. The bundled-resource check validated all 2,886 embedded LVRS assets.
Initial timeout failures and retry results are retained in the build report.

The broader QuickGenerate group still reports the simulated iOS/Android search
toggle visibility assertion in `mobileViewsShareDesktopContentAndKeepState`.
The same assertion was reproduced with the full development inference SDK.
It is tracked separately from the package footprint checks; no mobile-device
validation or new large-model generation run is claimed here.
