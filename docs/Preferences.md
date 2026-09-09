# Society Preferences

데스크톱 상단의 **Preferences…** 또는 **⌘+,**(macOS), **Ctrl+,**(Windows/Linux)로 독립 설정 창을 연다. 창 폭이 좁으면 상단 버튼을 설정 아이콘으로 표시한다. Devices의 **Preferences…** 버튼도 같은 창을 연다. LVRS의 설치된 `LV.Window`, `LV.RadioButton`, `LV.PushButton`을 사용하며 외부 의존성을 추가하지 않았다.

설정 창은 최초 요청 때만 생성한다. 반복해서 열면 기존 창을 활성화하며, 메인 창을 계속 사용할 수 있다. **Done**, Escape, OS의 창 닫기 버튼·단축키로 숨기고 다시 열면 현재 선택을 표시한다. 메인 창을 닫으면 설정 창도 닫는다. 창은 560×440으로 시작하며 360×320까지 줄일 수 있다. 본문은 스크롤하고 하단 버튼은 창 내부에 유지된다.

**Client mode**는 다른 기기의 Files 탐색·다운로드를 제공한다. **Host mode**는 이 기능에 더해 현재 Society 컨테이너의 `Files/`를 같은 계정의 기기에 공개한다. 선택은 앱의 단일 `NetworkDriveController.mode`에 즉시 적용한다. 별도의 설정용 연결이나 인증 세션을 만들지 않는다. 이미 연결된 상태에서는 기존 호스팅을 종료한 뒤 새 모드로 재연결하며, 진행 중 다운로드를 취소하고 기존 목적지 파일을 보존한다.

설정 창은 실제 호스팅·접속 상태와 연결 오류를 표시한다. 유효한 컨테이너가 없는 호스트 모드에서는 컨테이너 열기를 안내한다. **Devices…** 버튼으로 로그인과 기기 탐색 화면을 열 수 있다. 모드는 현재 앱 실행 동안 유지되며 앱을 다시 시작하면 클라이언트가 기본값이다. 선택만으로 로그인하지 않는다.

iOS/Android에는 Preferences 진입 버튼과 단축키를 활성화하지 않는다. 설정 창의 열기 함수도 모바일 정책을 확인하며, 호스트 라디오 동작을 직접 호출해도 C++의 클라이언트 전용 제한을 우회할 수 없다.

`Society.Drive`는 Preferences를 실제 클릭하여 열고 인증 fixture 중계에 연결한 앱을 호스트/클라이언트로 전환한다. 다른 Peer의 호스트 목록에 나타나고 사라지는지, 중복 창 없이 닫기·단축키 재열기·Devices 왕복이 가능한지, 외부 모드 변경이 라디오 선택에 반영되는지 검사한다. 작은 창과 큰 창에서 하단 버튼의 위치도 확인한다. `Society.ClientOnlyNetwork`는 모바일 정책에서 Preferences 열기 및 호스트 선택을 차단하는지 검사한다.

```sh
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
cmake --build build --target Society_qmllint

env -u QT_QPA_PLATFORM -u QT_QUICK_BACKEND \
  QSG_RHI_BACKEND=metal QML_DISABLE_DISK_CACHE=1 \
  SOCIETY_PREFERENCES_SCREENSHOT_PATH="$PWD/build/preferences-native.png" \
  build/bin/SocietyDriveTests preferencesControlsHostingAndReusesItsWindow
```

실제 모바일 기기 실행과 공용 중계 배포 검증은 이 호스트 환경의 UI·정책 테스트와 별도로 수행한다. 로컬/원격 전송 및 공개 범위의 세부 계약은 [NetworkDrive.md](NetworkDrive.md)를 따른다.
