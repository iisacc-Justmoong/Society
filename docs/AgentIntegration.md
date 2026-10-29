# 설치된 iiMultimodalAgent 연결

지원하는 Unix 데스크톱의 에이전트·MCP·질문 UI는 `iiMultimodalAgent 0.1` 패키지를
사용한다. Society는 `iiMultimodalAgent::iiMultimodalAgent`를 링크하고, 추론은 이
모듈의 의존성인 `iiLocalLLM 0.56`에 맡긴다. 앱에 모델 로더나 생성 루프를 추가하지
않는다. 기존 `SocietyMcp`는 앱 상태와 도구를 SDK에 연결하는 얇은 어댑터이다.
공유 서비스가 직접 사용하는 iiWebServer도 명시적으로 링크하여 앱과 데몬이
동일한 설치 패키지의 헤더와 라이브러리를 상속한다.

SDK는 `~/.local/SDK/iiMultimodalAgent`와 `~/.local/SDK/iiLocalLLM`에 설치한다.
iiDecision·iiAudioGen·iiVoiceOver·iiLocalDiffusion 및 Native 0.8.2 이상의
iiFileProvider도 해당 패키지의 의존성으로 필요하다. 실제 선택은 각 패키지의
`*_DIR` 또는 CMake prefix로 결정하며, SDK 소스를 앱 빌드에 포함하지 않는다.

현재 Society는 Canvas 0.31.4를 사용하므로, iiWebServer 인터페이스를 제공하는
Helper 0.11도 같은 Canvas 버전으로 빌드하여
`~/.local/SDK/iiSocietyHelper/profiles/webserver-canvas31`에 설치한다. 이 프로필은
`IISOCIETYHELPER_CANVAS_VERSION=0.31.4`로 구성하고, 설치 패키지가 그 버전을
정확히 요구하도록 기록한다. 기존 전역 Helper 설치본은 보존한다.

기존 macOS 빌드 설정을 유지하면서 설치된 모듈을 선택하는 명령은 다음과 같다.

```sh
cmake -S . -B build \
  -DiiMultimodalAgent_DIR="$HOME/.local/SDK/iiMultimodalAgent/lib/cmake/iiMultimodalAgent" \
  -DiiLocalLLM_DIR="$HOME/.local/SDK/iiLocalLLM/lib/cmake/iiLocalLLM" \
  -DiiLocalDiffusion_DIR="$HOME/.local/SDK/iiLocalDiffusion/profiles/native/lib/cmake/iiLocalDiffusion" \
  -DiiSocietyHelper_DIR="$HOME/.local/SDK/iiSocietyHelper/profiles/webserver-canvas31/lib/cmake/iiSocietyHelper" \
  -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@4 \
  -DOPENSSL_INCLUDE_DIR=/opt/homebrew/opt/openssl@4/include \
  -DOPENSSL_CRYPTO_LIBRARY=/opt/homebrew/opt/openssl@4/lib/libcrypto.4.dylib \
  -DOPENSSL_SSL_LIBRARY=/opt/homebrew/opt/openssl@4/lib/libssl.4.dylib \
  -DSOCIETY_MAC_RUNTIME_SEARCH_PATHS="$HOME/.local/SDK/OpenSSL-3.6.4/lib"
cmake --build build --target Society SocietyMcpTests SocietyDependencyTests --parallel 4
ctest --test-dir build -R 'Society\.(Dependencies|Mcp|MacRuntimeDeployment|MacRuntimeLaunch|MacBundleFootprint)' --output-on-failure
```

질문 화면의 QML은 SDK가 설치한 `IIMULTIMODALAGENT_QML_DIR`에서 읽는다. 기존
리소스 URL `/iiLocalLLM/UserQuestionsSheet.qml`, 환경 변수와 MCP wire method는
저장된 설정과 클라이언트 호환성을 위해 유지한다. C++ 헤더와 네임스페이스는
`iiMultimodalAgent/agent`, `iiMultimodalAgent/mcp`를 사용한다.

macOS 단일 앱 배포기는 설치 패키지의 공유 라이브러리 대상을 수집한다.
`iiMultimodalAgent::NativeImages`는 지연 로드하는 이미지 어댑터 파일을 노출한다.
이를 배포기의 `--runtime-plugin`으로 전달하여 의존성과 함께 복사하고, compact
정리에서도 보존한다. ONNX Runtime은 iiDecision이 선택한 API 버전을 포함한다.
Society가 선택한 기존 LibTorch 제외
이미지 SDK 프로필을 유지한다. 모델 가중치와 사용자 컨테이너는 앱 번들에 넣지 않는다.
이 프로필은 `~/.local/SDK/iiLocalDiffusion/profiles/native`에 설치하고, 기존 범용
iiLocalDiffusion 설치본은 보존한다. OpenSSL 3.6.4 Crypto도 SDK 경로에 설치하여
삭제된 Homebrew 설치나 다른 제품의 빌드 폴더에 의존하지 않게 한다.
이 네이티브 프로필은 MLX의 `libmlx.dylib`와 해당 빌드의 `mlx.metallib`를 같은
`lib/` 디렉터리에 설치해야 한다. 배포기는 커널 파일을 앱 `Resources/`로 복사하고
`Frameworks/mlx.metallib` 심볼릭 링크를 만들어 MLX가 번들 안에서 찾도록 한다.
macOS 제품은 같은 iiLocalDiffusion 빌드에서 나온 라이브러리와 Metal 파일을
`SOCIETY_MLX_RUNTIME_DIR`로 명시적으로 선택할 수 있다. 두 파일이 모두 존재해야
하며 빌드 시 같은 쌍이 링크와 번들에 사용된다. SDK 소스의 `build/`에서
`iild_mlx_runtime_ready`를 빌드하면 소비할 수 있는 쌍이 생성된다.
`Society.MacMlxDeployment`는 앱과 MLX의 Mach-O 최소 macOS 버전 및 선택한
Metal 파일의 번들 일치 여부를 실행 없이 검사한다.
라이브러리의 동적 로드만으로는 커널 누락을 확인할 수 없으므로 번들 커널 경로를
명시한 실제 GPU 배열 계산도 별도로 검증한다.
네트워크 SDK가 사용하는 OpenSSL 4는 해당 설치본을 링크하고, 기존 SDK의 Crypto 3
의존성은 위 호환 런타임 경로에서 해석한다. 테스트의 추가 라이브러리 검색도 두
Crypto 디렉터리로 제한하여 다른 SDK 버전을 섞지 않는다.

`Society.Dependencies`는 에이전트 모듈을 직접 링크하고 이전 iiLocalLLM 에이전트
facade를 직접 링크하지 않는지 검사한다. `Society.Mcp`는 실제 번들을 격리된
컨테이너로 실행하여 MCP와 질문 연결을 검증한다. 런타임 검사는 개발 환경의
라이브러리 검색 경로를 제거하고 번들 내부의 의존성만 로드되는지 확인한다.
MCP 테스트의 앱 프로세스는 `SOCIETY_DISABLE_PHOTOS=1`을 지정하여 관련 없는
실제 사진 라이브러리 초기화가 격리된 컨테이너의 기동 검사에 개입하지 않게 한다.
제품 앱의 사진 라이브러리 설정과 기동 제한 시간은 변경하지 않는다.

이 연결은 SDK 설치와 앱 링크·번들 빌드의 범위이다. 앞선 Nemotron 인사 검증의
모델별 메모리 설정은 iiLocalLLM의 호스트 API에 있으며, 이번 연결만으로 새로운
앱 채팅 화면이나 자동 대형 모델 로드를 추가하지 않는다.
