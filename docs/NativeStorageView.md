# Society의 LVUI 파일 화면 이관 경계

기존 [Figma Storage 파일 표](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=39-1045)를
네이티브 LVUI `Table`과 C++ 뷰모델로 옮기는 작업이다. 컨테이너 목록은
[데몬 목록 계약](NativeMigrationBackend.md)으로 읽으며 화면이 파일시스템을 직접
열거하거나 수정하지 않는다. Qt GUI, QML 또는 LVRS를 네이티브 뷰에서 사용하지 않는다.

브로커 연결은 iiNetwork 로컬 채널과 iiJson을 사용한다. 한 프레임은 4바이트
빅엔디언 길이와 최대 1 MiB JSON 객체로 구성하며 분할·연속 프레임, 잘못된 길이,
잘못된 JSON과 전송 한도를 처리한다. Unix는 연결 상대의 사용자 식별자를 확인한다.
Windows의 기존 데몬 채널은 명명된 파이프이므로 AF_UNIX로 대체된 것으로 가정하지
않으며 별도 어댑터가 완성되기 전에는 지원되지 않는 상태를 반환한다.

네이티브 파일 뷰모델은 새 폴더 요청의 식별자와 서버의 폴더 세대·리비전을 분리한다.
이전 폴더나 이전 리비전의 늦은 응답은 버린다. 항목의 상대 경로는 현재 위치의
선택 키이며 자원의 영구 ID나 내용 해시로 취급하지 않는다. 조회·페이지·선택은
데몬의 작업을 취소하지 않고 명시적인 오류를 표시한다. 화면 크기와 콘텐츠는 LVUI
토큰과 재사용 위젯으로 구성한다.
격자 선택 상태는 앱에서 import한 C++ 연결을 통해 iiGUI의 컬렉션 탐색 구현이
관리하며, LVUI는 탐색 상태와 동작을 화면에 선언적으로 연결한다.

`App/Society/Native/`에는 LVUI AOT 파일 표, C++ 뷰모델, iiNetwork 연결 및
이를 결합하는 별도 이관용 실행 파일이 있다. 파일 표는 한 번에 최대 64개 항목을
보여 주며 Files·Models·Deleted 영역, 하위 폴더, 페이지, 빈 상태와 연결 오류를
다룬다. 목록은 데몬의 선택된 컨테이너에서 오며, 내부 JSON 메타데이터는 컨테이너
모델의 필터를 따른다. 선택한 파일의 이름 변경·휴지통 이동·복원은 컨테이너의
지속 작업에 위임한다. 수락·기록·완료는 각각 표시하며 연결 복원 시 식별자를 받은
작업은 다시 조회한다. 연결이 끊기기 전에 응답을 받지 못한 요청은 결과 확인이
필요함을 표시하고 자동 재전송하지 않는다. 오류를 동반한 완료 통지는 작업 ID와
실제 결과 경로를 유지하며, 이후 조회에서 작업 상태를 확인할 수 있다. 완료 전에
보낸 오래된 상태 조회가 완료 결과를 덮어쓰지는 않는다.

파일 복제, 대상 폴더로의 이동과 현재 위치에 새 폴더를 만드는 동작도 컨테이너 작업으로 위임한다.
이동 대상은 컨테이너 기준 상대 폴더이며, 선택이 변경되면 열린 이동 입력을 취소한다.
Deleted에서 영구 삭제는 선택 항목을 명시적으로 확인한 뒤에만 요청한다. 내부
메타데이터와 미리보기는 독립 항목으로 조작하지 않고 컨테이너의 파일 조작 규칙을
따른다.

현재 페이지에서 연속된 여러 행을 선택하면 각 파일을 별도 지속 작업으로 제출한다.
열기와 이름 변경은 단일 선택에서만 제공하며, 영구 삭제 확인 중 선택이 바뀌면
확인을 취소한다. 각 작업의 거절·실패 및 전송 결과가 불확실한 항목은 나중의 성공에
가려지지 않도록 별도로 남긴다. 이 선택 범위는 현재 페이지의 연속 행이며, 비연속
선택과 페이지를 넘나드는 선택은 표에서 아직 제공하지 않는다. 오류 표시는 최근
128건을 탐색할 수 있으며 오래된 오류가 한도를 초과하면 생략된 건수를 표시한다.

목록과 가상 그리드는 동일한 현재 페이지를 사용하며, 그리드는 여러 항목을
개별 선택할 수 있다. 표시 형식이나 페이지를 바꿀 때 이전 선택은 해제한다.
그리드는 파일명·종류·크기를 표시하며 이미지 미리보기는 아직 제공하지 않는다.

Models에서는 운영체제 파일 선택기로 여러 safetensors 모델을 고를 수 있다. 선택한
경로는 데몬의 기존 모델 가져오기 작업에 전달하고 원본은 그대로 둔다. 작업 수락과
진행·완료를 구분하며, 선택기나 연결이 열린 동안 대상 드라이브가 바뀌면 제출하지
않는다. Files에서는 운영체제 선택기로 고른 일반 파일을 현재 폴더에 복사하도록
데몬에 요청한다. 파일별로 별도 지속 작업을 만들며 원본과 내부 부속 자료의 처리는
컨테이너가 담당한다. 선택기가 열려 있는 동안 드라이브 또는 폴더가 바뀌면 요청하지
않는다. 지원되는 원본의 정규 경로를 사용하며, 원본을 삭제하거나 이동하지 않는다.

현재 폴더 검색은 입력 후 Search로 적용하며, 이름·날짜·종류·크기 버튼을 다시 누르면
정렬 방향이 바뀐다. 필터와 정렬은 데몬의 비동기 디렉터리 모델에 전달되고 페이지
전체에 적용된다. 다른 폴더로 이동하면 검색어는 지운다.

macOS의 Open은 데몬이 선택 파일의 목록 소속과 가용성을 확인한 결과를 운영체제에
전달한다. 원격 원본 준비는 컨테이너 모델의 기존 요청을 사용하며, 사용자가 다른
폴더로 이동한 뒤 도착한 결과는 열지 않는다. 이 요청의 수락은 전문 앱에서의 저장
또는 판본 확정을 뜻하지 않는다. 자동 시험은 OS 파일 열기를 모의 콜백으로 대체한다.

macOS Society 빌드에는 이 실행 파일이 포함되며 기존 앱의 `View > Files (LVUI
preview)`에서 별도 창으로 열 수 있다. 기존 Storage 진입점은 유지된다. 데몬을 찾을
때 `SOCIETY_LOCAL_BROKER_DIR`을 사용하고 지정되지 않으면 사용자 홈의 기존
브로커 경로를 사용한다. 연결이 끊기면 Reconnect로 다시 연결할 수 있으며 자동
재접속은 아직 제공하지 않는다.

목록의 비연속 다중 선택·미리보기 등 기존 파일 화면의
나머지 기능은 이관용 화면에 아직 없다. 이 실행 파일은 기존 Society의 기본
진입점이나 설치 대체물이 아니다. 브로커와 뷰모델의 시험은 격리된 소켓과
임시 컨테이너를 사용하며, 운영체제 창을 열거나 사용자의 실제 파일을 조작하지 않는다.

## macOS 이관 구성 빌드

아래 명령은 현재 작업공간의 네이티브 선택기용 LVUI·iiGUI 의존성과 로컬 iiSDK를
사용한다. 실제 제품 설치나 GUI 실행은 포함하지 않는다.

```sh
cmake -S App/Society/Native -B build/native-society \
  -DLVUI_DIR="$PWD/resources/dependencies/lvui-native-model-chooser/lib/cmake/LVUI" \
  -DiiGUI_DIR="$PWD/resources/dependencies/iigui-native-model-chooser/lib/cmake/iiGUI" \
  -DiiSDKCore_DIR="$HOME/.local/SDK/iiSDKCore/lib/cmake/iiSDKCore" \
  -DiiNetwork_DIR="$HOME/.local/SDK/iiNetwork/lib/cmake/iiNetwork" \
  -DiiJson_DIR="$HOME/.local/SDK/iiJson/lib/cmake/iiJson" \
  -DiiSocietyContainer_DIR="$PWD/../../SDK/iiSocietyContainer/build/lvui-app-stage/lib/cmake/iiSocietyContainer" \
  -DiiFileProviderCatalog_DIR="$PWD/resources/dependencies/catalog-compat/lib/cmake/iiFileProviderCatalog" \
  -DQt6_DIR="/Volumes/Storage/Qt/6.8.3/macos/lib/cmake/Qt6" \
  -Dlunasvg_DIR="$PWD/resources/dependencies/iigui-native-model-chooser/lib/cmake/lunasvg" \
  -Dplutovg_DIR="$PWD/resources/dependencies/iigui-native-model-chooser/lib/cmake/plutovg" \
  -DCMAKE_PREFIX_PATH="$HOME/.local/SDK/iiFileProvider;$HOME/.local/SDK/iiJob;$HOME/.local/SDK/Vulkan;$HOME/.local/SDK/iiColorManager"
cmake --build build/native-society
ctest --test-dir build/native-society --output-on-failure
```

위 `iiSocietyContainer_DIR`은 `FileJobService`가 포함되도록 작업공간에서 빌드하고
스테이징한 SDK 패키지를 가리킨다. 이 테스트 구성에서만 데몬 측 Qt Core·Network를
함께 링크한다. LVUI 실행 파일 자체는 해당 Qt 테스트 의존성을 링크하지 않는다.

이 문서에 기술된 단계가 존재하는 것만으로 기존 Society 전체의 전환, 기존 기능과의
동등성, 설치 또는 실제 운영체제의 GUI 검증이 끝난 것은 아니다. 기존 제품 경로는
필요한 화면 및 데이터 경계가 연결되고 검증될 때까지 보존한다.
