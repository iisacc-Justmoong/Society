# Society 소스 배치

앱 진입점은 `App/main.cpp`이고 기능별 C++·QML 소스는 `App/Society/`에 있다.
데몬은 `App/Daemon/`, 운영체제별 앱 리소스와 Android Java 소스는
`App/platform/`에 있다. 프로젝트 헤더 검색 기준은 `App/`이므로 앱 기능은
`Society/...`, 데몬은 `Daemon/...` 경로로 포함한다.

실제 QML 파일이 `App/Society/`에 있더라도 Qt 리소스 별칭은 기존의
`qrc:/qt/qml/Society/App/`을 유지한다. 이 경로는 실행 중인 앱과 번들 리소스
검증의 계약이므로 소스 경로 이동과 함께 변경하지 않는다. Android 패키징은
`App/platform/android/src/`의 Society Java 소스와 iiPhotoLibrary SDK 브리지를
생성 패키지에 복사한다.

`Society.SourceLayout`은 프로젝트 헤더와 상대 QML import가 실제 파일을 가리키는지,
생성된 QML 리소스 별칭이 기존 주소와 일치하는지, 플랫폼 패키징이 참조하는
소스가 존재하는지 검사한다.
`Society.PhotoLibraryPackaging`은 Android Java 소스의 패키징을 검증한다.
SDK의 `tests/test_account_manager_name.py`와 `tests/test_module_names.py`도 Society의
현재 소비자 경로를 확인한다. macOS에서는 `Society.MacRuntimeLaunch`로 번들된 앱의
QML 실행을 검증할 수 있다. 앱 실행 검증은 요청된 경우에만 수행한다.
macOS 빌드는 기본적으로 앱을 실행하지 않으며, 빌드 직후 실행 검증이 명시적으로
필요한 경우에만 `-DSOCIETY_VERIFY_RUNTIME_ON_BUILD=ON`으로 설정한다.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure -R 'Society\.(SourceLayout|PhotoLibraryPackaging)'
```
