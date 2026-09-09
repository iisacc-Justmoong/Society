# Android QR scanner dependencies

- `com.journeyapps:zxing-android-embedded:4.3.0` from Maven Central, [upstream release](https://github.com/journeyapps/zxing-android-embedded/tree/v4.3.0), Apache 2.0.
- Its pinned transitive decoder is `com.google.zxing:core:3.4.1`, [upstream tag](https://github.com/zxing/zxing/tree/zxing-3.4.1), Apache 2.0.
- The matching license texts are included here and in the app under `:/licenses/zxing/`.

The wrapper uses QR-only decoding, an in-app camera dialog and memory-only callbacks. It never requests microphone permission, saves camera frames, starts an external scanner app or sends frames to a server. The dependency includes AndroidX. The upstream stable release is old; changing the target SDK or decoder requires camera lifecycle/permission and real scan checks.

`tests/DecodeAndroidQr.java` runs this exact decoder against the Qt-rendered desktop screenshot and verifies that an empty camera frame is rejected. Compile it under `build/` using the decoder JAR resolved by Gradle, then pass `build/local-pairing-desktop.png`. APK compilation/lint validates the Android bridge; physical camera capture remains a separate check.
