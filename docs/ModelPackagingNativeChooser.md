# Society 모델 패키징 네이티브 선택기 명세

Figma의 [모델 선택 창](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=856-21)과
[패키징 화면](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=854-2)을 기준으로 한다.

선택기는 LVUI가 생성한 iiGUI 네이티브 위젯과 별도의 C++ 뷰모델을 사용한다.
현재 Society의 Qt 이벤트 루프 안에 iiGUI Host를 중첩하지 않고 독립 프로세스로 열며,
현재 드라이브의 미리 준비된 모델 목록과 이전 선택을 표준 입력으로 전달한다.
모델 행에 포커스를 둔 뒤 **Add or remove model**로 포함 여부를 바꾼다. 키보드로
행 사이를 이동하는 것만으로는 포함 여부가 바뀌지 않으며, 여러 카테고리의 항목을
선택할 수 있다.
가상 목록은 보이는 행만 구성한다. 확인하면 선택 경로만 반환하고, 취소하면 원본을
변경하지 않는다. 외부 폴더 버튼은 iiGUI의 네이티브 디렉터리 선택을 사용한다.
확인 버튼은 창의 녹색 강조색을 사용하고 나머지 버튼은 LVUI 기본 톤을 사용한다.

Society는 응답을 현재 드라이브 경계와 실제 가용성에 대해 다시 검증하고
iiLocalDiffusion의 선택 입력 검사에 전달한다. 가져오기나 임시 폴더로의 원본 복사는
발생하지 않는다. 창이 닫히거나 실패해도 기존 검사 결과와 원본 파일을 보존한다.
드라이브 전환 중의 오래된 응답은 폐기한다. 실행되는 기존 Qt 화면과 새 네이티브
선택기는 별도의 수명으로 관리하며, 이후 Society 전체의 LVUI 이전과 구분한다.

macOS 패키지는 `SocietyModelChooser` 실행 파일과 iiGUI 글꼴을 Society 번들에 포함한다.
독립 빌드는 LVUI, iiGUI 0.5, iiSDKCore Runtime 및 JSON 라이브러리를 사용한다.
`SOCIETY_NATIVE_LVUI_DIR`, `SOCIETY_NATIVE_IIGUI_PREFIX`,
`SOCIETY_NATIVE_VULKAN_PREFIX`로 호환 SDK 위치를 지정할 수 있다.
전달 크기와 항목 수는 제한되며, 응답 경로는 제공한 목록에 속해야 한다.
선택 상태와 JSON 계약은 창을 열지 않는 단위 테스트로 검증한다.
