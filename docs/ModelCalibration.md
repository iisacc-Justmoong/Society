# 모델 교정기 작업 명세

Tasks에서 Model merge와 Model Packaging 다음에 Model calibrator를 제공한다.
설계 기준은 [도구 목록](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=115-454)과
[교정 화면](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=872-2628)이다.

macOS의 카드는 LVUI로 작성하고 iiGUI가 소유하는 독립 네이티브 창을 연다. 파일 선택은
현재 Society 모델 폴더에서 시작한다. 모델 선택 후 **Analyze model**은 iiLocalDiffusion으로
메타데이터와 가중치를 검사하고 패키지의 경우 해시를 확인한다. 전체 가중치 검사는 시간이
걸릴 수 있으며 작업 중에도 창은 응답하고 취소할 수 있다. 패키지 검증에 실패한 원인은
표시되며 패키지 구조 교정이 활성화되지 않는다.

검증된 iiLocalDiffusion Safetensors 패키지는 구성 요소를 재분류하여 새 파일로 저장할 수
있다. 독립 Safetensors 파일은 검사에서 비정상 수치가 발견되고 사용자가 **Replace
non-finite values with zero**를 선택한 경우에만 새 파일로 교정한다. 같은 옵션을 패키지에
적용할 수 있다. F16·BF16·F32·F64 이외의 텐서는 검사하지 않은 개수로 표시한다.
원본과 기존 출력은 덮어쓰지 않으며 패키지와 독립 파일의 결과는 게시 전에 각각 재검증한다.
비정상 수치를 0으로 치환하면 가중치가 변하며 생성 품질이 개선된다는 보장은 없다.

생성 결과를 기준 자료와 비교하거나 학습하여 품질을 자동 조정하는 평가기·학습기는 아직
연결되어 있지 않다. 화면은 이 제한을 표시하며 실제 품질 개선을 성공으로 주장하지 않는다.
다른 운영체제의 독립 교정기 번들 구성도 아직 제공하지 않는다.

구현은 `NativeModelCalibratorViewModel`과 iiSDKCore 작업 실행기를 사용하며 UI 스레드에서
모델 전체를 읽지 않는다. `Society.NativeCalibratorViewModel`,
`Society.TasksNavigation`과 iiLocalDiffusion `ModelPackagingTests.py`는 각각 상태 전이,
도구 목록·검색, 합성 텐서의 보존·수치 교정을 검증한다. 실제 사전학습 모델의 추론이나
생성 품질은 이 테스트의 대상이 아니다. 개발용 번들은 SDK의 `ModelPackaging` 컴포넌트를
프로젝트 리소스에 준비한 뒤 빌드한다. SDK와 번들의 API 세대가 달라지지 않도록 SDK를
다시 빌드한 후 동일하게 스테이징한다.

```sh
cmake --build ../../SDK/iiLocalDiffusion/build --target iild-model-package
cmake --install ../../SDK/iiLocalDiffusion/build \
  --prefix "$PWD/resources/dependencies/model-calibrator-iild" --component ModelPackaging
cmake --build build --target Society
ctest --test-dir build/native-model-calibrator -R '^Society.NativeCalibratorViewModel$' --output-on-failure
ctest --test-dir build -R '^Society.TasksNavigation$' --output-on-failure
```

별도 SDK 스테이지를 사용할 때는 `SOCIETY_CALIBRATOR_IILD_PREFIX`를 지정한다.
