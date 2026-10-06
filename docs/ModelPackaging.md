# Model Packaging

Tools → Model Packaging은 [Figma 297:511](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=297-511)을 기준으로 구현하였다.
로컬 모델 폴더를 검증된 단일 `.safetensors` 파일로 패키징한다.
넓은 화면에서는 Model merge 옆에 카드가 배치된다. 두 도구 모두 목록으로
돌아가거나 앱 탭을 전환해도 입력과 작업 상태를 유지한다.

원본 폴더 선택, 자동 감지된 구성요소 확인, 파일명과 저장 위치 지정 후
**Package model**을 실행한다. 컨테이너 기본 저장 위치는 `Models/Checkpoint`이다.
**View all files and skipped reasons**에서 모든 입력과 제외 사유를 확인한다.
선택 구성요소는 제외할 수 있으며 필수 구성요소는 제외할 수 없다.
선택에 따라 파일·구성요소 수와 예상 크기가 갱신된다.
원본 dtype과 GGUF 양자화를 유지하며 원본 파일은 읽기만 한다.

본문은 설치된 LVRS의 Card, Label, ListItem, InputField, ToggleSwitch,
ProgressBar와 버튼을 사용한다. Figma의 어두운 패널과 녹색 강조를 적용하였다.
1440 px 화면의 사이드바는 228 px, 콘텐츠 여백은 32 px, 열 간격은 24 px,
출력 열은 348 px이다. 좁은 화면에서는 사이드바를 숨기고 하나의 스크롤 열로
배치한다. 탐색 개수는 실제 구현된 도구 두 개를 반영한다.

`ModelPackagingController`는 QtConcurrent를 통해 네이티브 SDK 작업을 비동기로
실행하고 GUI 상태·선택·취소를 관리한다. 재사용 가능한 Qt 비의존 C++23
`iiLocalDiffusion::ModelPackaging` 모듈이 형식 검사, 스트리밍 복사, SHA-256 검사,
원본 복원과 원자적 저장을 담당한다. macOS 빌드는 해당 SDK 모듈을 필수로
요구한다. 모듈이 없는 다른 플랫폼 빌드는 이용 불가 상태를 표시하고 실행을 막는다.

검사는 Safetensors/GGUF 구조, 샤드 인덱스와 필수 구성요소를 확인한다.
중복 VAE·projection은 shape, dtype과 실제 내용이 모두 일치해야 제외한다.
충돌이 있으면 생성을 막으며 다운로드 실패와 잘못된 파일의 정확한 사유를 표시한다.
생성 시 입력을 다시 검사한다. 검사·복사·검증 진행 상태는 실제 작업자가 전달한다.
출력 전체를 다시 읽어 검증한 후 덮어쓰기 없이 원자적으로 저장한다.
취소 시 미완성 출력을 제거하고 기존 파일은 교체하지 않는다.
성공 창은 SDK의 저장 완료·검증 보고서가 있어야 표시되며 실제 경로·크기·SHA-256과
**Copy details**, **Open folder**를 제공한다. 컨테이너 출력은 모델 정리와 파일
목록 갱신을 실행하며 외부 출력은 지정한 경로에서 연다.

패키지는 메인 텐서 이름, 구성요소 네임스페이스, GGUF·에셋의 U8 원본 데이터,
원본 헤더·메타데이터와 역할 manifest를 보존한다. 구성요소를 함께 담는 번들이며
체크포인트 평균이나 LoRA 가중치 융합을 수행하지 않는다. 전체 구성요소를 로딩하려면
manifest를 이해하는 리더가 필요하다. 이 패키징 구현 자체는 기존 생성 백엔드에
LTX 2.3 추론 지원을 추가하지 않는다. SDK의 `verifyModelPackage`,
`extractModelPackage`와 `iild-model-package verify/extract` CLI는 패키지를 검증하고
폴더 기반 소비자를 위해 원본 구성요소 파일을 복원한다.

빌드와 검사:

```sh
cmake -S . -B build
cmake --build build --target Society SocietyModelPackagingTests SocietyDriveTests
ctest --test-dir build -R 'Society\.(ModelPackaging|Drive|MacRuntimeLaunch)$' --output-on-failure
```

`Society.ModelPackaging`은 실제 소형 모델 형식을 네이티브 컨트롤러로 처리하여
선택 구성요소, 필수 구성요소 누락, 이식 가능한 파일명, 비로컬 URL 차단,
취소 정리, 검증된 출력, 덮어쓰기 방지, QML 조작과 360 px 반응형 배치를 검사한다.
SDK 테스트는 샤드 체크포인트, 바이트 단위 원본 복원, 손상과 텐서 충돌도 검사한다.
Cocoa 플랫폼의 테스트 하니스에서 `SOCIETY_PACKAGING_SCREENSHOT_PATH`를 지정하면
네이티브 화면을 저장한다. 실제 LTX 산출물과 로그는
`build/model-packaging-verification/`에 보관하며 앱 리소스에는 포함하지 않는다.
현재 실행 검증과 대형 모델 산출물의 근거는 [ModelPackagingVerification.md](ModelPackagingVerification.md)에 기록한다.
