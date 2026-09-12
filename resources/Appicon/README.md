# Society 앱 아이콘

승인된 원본은 이 디렉터리의 `app-icon-full-bleed.ai`와 1024×1024 `Artboard 1.png`이다. 원본은 수정하지 않으며, PNG에서 모든 플랫폼 자산을 생성한다. PNG에 있는 약한 투명도는 원본 배경색 `#082014` 위에 합성한다. iOS와 스토어 PNG에는 알파 채널이 없다.

| 플랫폼 | 생성 자산 | 앱 연결 |
| --- | --- | --- |
| macOS | 16–1024px ICNS, 1×/2× iconset, 투명 여백·둥근 모서리·약한 그림자 | CFBundleIconFile, 번들 Resources, Qt 런타임 아이콘 |
| iPhone/iPad | 알파 없는 20–1024px AppIcon.appiconset, App Store 1024px | Xcode ASSETCATALOG_COMPILER_APPICON_NAME=AppIcon |
| Android | ldpi–xxxhdpi legacy/round PNG, 108dp adaptive foreground/background, API 33 monochrome, Play 512px | android:icon / android:roundIcon, provider 패키지의 res에 병합 |
| Windows | 16·20·24·32·40·48·64·96·128·256px ICO | 실행 파일의 RC 리소스, Qt 런타임 아이콘 |
| Linux | 16–1024px hicolor PNG | desktop 항목의 Icon, desktopFileName, install 규칙 |
| WebAssembly | favicon ICO/PNG, Apple touch 180px, 192/512px 일반·maskable 아이콘 | Qt가 생성한 Society.html에 링크 삽입, webmanifest |

Android는 108dp 레이어 안에 원본을 66dp로 배치해 시스템 마스크와 이동 효과에 필요한 여백을 둔다. monochrome은 원본의 명도를 알파 마스크로 변환한다. iOS는 원본의 사각형을 유지하고 모서리는 운영체제가 처리한다. macOS ICNS는 자체 모서리와 여백을 포함한다. Windows/Linux/일반 웹 PNG는 원본의 전체 구도를 유지한다.

재생성에는 Pillow 12.3을 사용한다. 이미 작업 환경에 제공된 이미지 변환 라이브러리이며 앱의 런타임 의존성에는 추가하지 않는다. 다른 개발 환경에서는 Pillow를 개발 도구로 설치한 후 실행한다.

```sh
python3 tools/generate_app_icons.py
python3 -B tests/test_app_icons.py
cmake --build build
ctest --test-dir build -R AppIcons --output-on-failure
```

`generated/manifest.json`은 원본 SHA-256과 모든 생성 파일의 크기·색상 모드·해시를 기록한다. 원본 변경 뒤 재생성을 빠뜨리거나 생성 파일이 손상되면 테스트가 실패한다. 일반 앱 빌드는 이미 생성된 자산을 사용하므로 Pillow가 필요하지 않다. Illustrator 수정 시 PNG도 동일한 1024×1024 전체 아트보드로 내보낸 뒤 재생성한다.

Android 빌드는 이 manifest의 변경을 CMake 재구성 의존성으로 추적하므로 아이콘 재생성 후 다음 빌드에서 패키지의 `res`도 갱신한다. 웹 패키징은 기존 Qt 부트스트랩을 보존하며 반복 빌드해도 아이콘 링크를 중복 삽입하지 않는다.

Windows/Linux/WebAssembly의 아이콘 연결은 각 타깃 빌드 시 적용된다. 아이콘 자산 및 패키징 연결 검증과 해당 운영체제에서의 전체 앱 실행 검증은 구분한다.

2026-09-12 적용 검증에서는 생성 자산 82개와 자동 검사 6개를 확인했다. macOS 번들의 ICNS·서명·Finder 표시, iPhone/iPad 실기기 설치 및 실행, Android 16 arm64 에뮬레이터의 설치·QML 루트 로딩을 검증했다. Windows RC는 실제 COFF 리소스로 컴파일했고 Linux 설치 규칙 및 웹의 Qt HTML 연결도 실행해 확인했다. Windows/Linux/WebAssembly 전체 앱의 구동은 해당 실행 환경에서 별도 검증해야 한다. 공용 iOS 빌드에는 기기별로 축소하지 않은 AppIcon 렌디션 19개가 포함된다.

이번 로컬 빌드의 상세 결과는 `build/icon-audit/verification.json`과 같은 디렉터리의 로그에 기록한다. Android 검증용 APK는 개발용 서명을 사용하며 스토어 배포용 서명과는 별개이다.

규격 근거: [Apple 에셋 카탈로그](https://developer.apple.com/documentation/xcode/configuring-your-app-icon), [Android adaptive icons](https://developer.android.com/develop/ui/compose/system/icon_design_adaptive), [Qt 애플리케이션 아이콘](https://doc.qt.io/qt-6.8/appicon.html), [Microsoft 아이콘 크기](https://learn.microsoft.com/windows/apps/design/style/iconography/app-icon-construction), [freedesktop hicolor](https://specifications.freedesktop.org/icon-theme/latest/).

Illustrator `.ai` 원본은 `.gitattributes`에서 바이너리로 지정하여 Git의 줄바꿈 정규화·텍스트 병합 대상에서 제외한다.
