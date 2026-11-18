<a id="society-앱-아이콘"></a>

# Society app icon

The approved originals are `app-icon-full-bleed.ai` and 1024×1024 `Artboard 1.png` in this directory. The originals are not modified, and all platform assets are generated from PNG. Weak transparency in PNG is composited over the original background color `#082014`. iOS and store PNG have no alpha channel.

|Platform|Generated assets|App connection|
| --- | --- | --- |
| macOS |16 – 1024px ICNS, 1×/2× iconset, transparent padding, rounded corners, and weak shadow|CFBundleIconFile, bundled Resources, Qt runtime icons|
| iPhone/iPad |Alpha-free 20 – 1024px AppIcon .appiconset, App Store 1024px| Xcode ASSETCATALOG_COMPILER_APPICON_NAME=AppIcon |
| Android | ldpi–xxxhdpi legacy/round PNG, 108dp adaptive foreground/background, API 33 monochrome, Play 512px |android:icon / android: roundIcon, merged into res in the provider package|
| Windows | 16·20·24·32·40·48·64·96·128·256px ICO |Executable file RC resources, Qt runtime icon|
| Linux | 16–1024px hicolor PNG |Icon, desktopFileName, install rule of desktop item|
| WebAssembly |favicon ICO / PNG, Apple touch 180px, 192/512px regular·maskable icon|Qt-generated Society .html link insertion, webmanifest|

Android places the original 66dp inside 108dp layer, leaving margins needed for system mask and move effects. monochrome converts the original's brightness to alpha mask. iOS maintains the original's rectangle and the OS handles the corners. macOS ICNS includes its own corners and margins. Windows / Linux /regular web PNG maintains the original's entire composition.

Regeneration uses Pillow 12.3, an image-conversion library already provided in the working environment. It is not added to the app's runtime dependencies. In other development environments, install Pillow as a development tool before running.

```sh
python3 tools/generate_app_icons.py
python3 -B tests/test_app_icons.py
cmake --build build
ctest --test-dir build -R AppIcons --output-on-failure
```

`generated/manifest.json` records the size, color mode, and hash of the original SHA-256 and all generated files. If regeneration is missed after the original changes or if generated files are damaged, the test fails. Regular app builds use already generated assets so Pillow is not needed. When modifying Illustrator, also export PNG as the same 1024×1024 full artboard and then regenerate.

Android build tracks changes to this manifest as CMake rebuild dependency, so after icon regeneration, the package's `res` is also updated in the next build. Web packaging preserves the existing Qt bootstrap and does not duplicate insert icon links even with repeated builds.

Icon connections for Windows / Linux / WebAssembly are applied at each target build. Icon asset and packaging connection verification and full app execution verification on the respective operating system are separate.

2026-09-12 verification check confirmed 82 generated assets and 6 automatic checks. macOS bundle's ICNS ·signature·Finder display, iPhone / iPad device installation and execution, Android 16 arm64 emulator installation and QML root loading were verified. Windows RC compiled with actual COFF resources and executed and checked Linux install rules and web's Qt HTML connections. Windows / Linux / WebAssembly full app operation must be separately verified in the respective execution environment. Public iOS build includes AppIcon 19 renditions not scaled per device.

Detailed results of this local build are recorded in logs of a directory like `build/icon-audit/verification.json`. Android verification APK uses development signature and is separate from store distribution signature.

Specification references: [Apple asset catalogs](https://developer.apple.com/documentation/xcode/configuring-your-app-icon), [Android adaptive icons](https://developer.android.com/develop/ui/compose/system/icon_design_adaptive), [Qt application icons](https://doc.qt.io/qt-6.8/appicon.html), [Microsoft icon sizes](https://learn.microsoft.com/windows/apps/design/style/iconography/app-icon-construction), [freedesktop hicolor](https://specifications.freedesktop.org/icon-theme/latest/).

The original `.ai`  is specified as binary in `.gitattributes`  and excluded from Git line-ending normalization and text merge targets.
