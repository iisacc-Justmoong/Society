# 유형별 모델 보관

Society는 iiSocietyContainer 0.11.2의 `ModelStore`를 사용해 `Models/`를 23개 유형 디렉터리로 관리한다. Storage → Models의 기본 화면은 [Figma 39:1242](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=39-1242)의 네 모델 목록이다.

- Image models, Video models, Audio models, Language models 순서로 표시한다. 사이드바는 228 px, 콘텐츠 여백은 24 px, 각 제목 행은 60 px이다. `LV.Card.Model`은 256×280 px이고 카드 간격은 8 px, 제목과 목록 및 카테고리 사이 간격은 24 px다.
- 사이드바는 [Figma 39:1275의 네 그룹과 탐색 객체](StorageNavigation.md)를 사용한다. My storage 제목 아래 Models 행이 현재 선택을 표시하며 모델 콘텐츠 좌표는 그대로 유지한다.
- 각 목록은 독립적으로 가로 스크롤한다. 가로 휠·트랙패드, 드래그, 스크롤바와 방향키·Home/End 탐색을 지원한다. 전체 페이지는 세로 스크롤하며 760 px 미만에서는 사이드바를 숨긴다.
- 카드는 모델명, Architecture, Format, Precision과 실제 저장 용량을 표시한다. 파일 헤더·패키지 설정·부속 JSON을 제한된 크기로 읽으며 텐서를 로드하지 않는다. 메타데이터가 없는 값은 Unknown으로 표시한다. 카드 선택과 메뉴의 Open·Show in folder를 제공한다.
- `StorageModels`는 SDK의 목록·메타데이터 API와 QtConcurrent·QFileSystemWatcher를 재사용한다. 명시적인 `society.modality`/`modelspec.modality`를 우선하고 아키텍처·작업 종류, 기존 모델 유형 순서로 분류한다. Motion은 Video, LLM/VLM은 Language, 이미지 구성 요소는 Image에 표시한다. 근거가 없는 항목은 임의 분류하지 않고 하단에 미분류 개수를 표시한다.
- 하단 `Society / Models`를 누르면 기존 23개 폴더와 미분류 모델을 탐색할 수 있다. 사이드바 Models를 누르면 네 목록으로 돌아온다. 각 Import models… 버튼은 기존 자동 분류 가져오기를 연다. 가져오기 형식과 원본 보존 정책은 아래 계약을 유지한다.
- 모델 추가·삭제·수정, 가져오기·정리 완료 및 화면 재진입 시 목록을 갱신한다. 바뀌지 않은 스냅샷은 갱신 신호를 내보내지 않으며, 실제 변경 시에도 선택 경로와 카테고리별 가로 스크롤을 복원한다. 컨테이너 전환은 이전 비동기 결과를 폐기한다.

`SocietyDriveTests modelCatalogGroupsRealMetadataAndWatchesChanges`는 네 분류·메타데이터·용량·미분류·외부 링크 제외·자동 갱신·이전 조회 폐기를 검사한다. `modelsMatchFigmaAndScrollEachCategoryIndependently`는 실제 사이드바 진입, Figma 좌표·치수, 독립 가로 스크롤, 세로 스크롤, 가져오기·선택 복원·390 px 폭과 폴더 보기 전환을 검사한다. `SOCIETY_MODELS_SCREENSHOT_PATH`로 위쪽과 아래쪽 화면을 저장한다. 아래의 `SOCIETY_MODEL_TYPES_SCREENSHOT_PATH`는 폴더 보기 검증용이다.

`Checkpoint`, `Embedding`, `Hypernetwork`, `Aesthetic Gradient`, `LoRA`, `LyCORIS`, `DoRA`, `Controlnet`, `Upscaler`, `Motion`, `VAE`, `Text Encoder`, `UNet`, `CLIP Vision`, `Poses`, `Wildcards`, `Workflows`, `ComfyUI Workflows`, `Detection`, `VLM`, `CLIP`, `LLM`, `Other`를 제공한다. **Storage → Models**에서 각 폴더를 탐색한다. Finder·파일 앱에는 기존처럼 `Files/`만 공개된다.

컨테이너를 열면 `ModelImporter`가 작업 스레드에서 누락된 유형 폴더를 생성하고 기존 미분류 모델을 정리한다. SDK가 모델 헤더·설정·부속 메타데이터로 유형을 판정하며 근거가 없으면 **Other**로 보관한다. 이미 유형 폴더에 있는 모델은 사용자의 배치를 유지한다. 새 `.safetensor`/`.safetensors` 가져오기는 유형을 판정한 뒤 완성된 복사본을 해당 폴더에 게시하고 원본 파일은 보존한다. 가져오기 중 동일 이름 충돌은 새 번호를 붙여 처리한다.

Anima 원본·파생 모델은 내부 확산 네트워크·LLM 어댑터·입출력 행렬의 조합과 차원을 검사하여 **Checkpoint**로 분류한다. 텍스트 인코더·VAE가 포함된 통합본도 동일하게 처리하며, Anima LoRA는 LoRA로 구분한다. 기존 `Other` 항목은 컨테이너를 다시 열거나 정리를 실행하면 이동 기록을 남기고 재분류한다. 이미 다른 명시적 유형 폴더에 놓인 항목은 내부 판정 확인 후 SDK의 명시적 `place()`로 수정한다.

Safetensors의 모든 텐서 자료형·차원·바이트 범위와 실제 파일 크기를 검증하며 중복 JSON 키, 오버플로, 겹치는 영역, 누락된 데이터를 거부한다. 손상된 파일은 sidecar에 유형이 있어도 정상 모델로 인식하지 않는다. 텐서 값은 로드하지 않아 대용량 모델도 헤더만 읽어 검사한다. [SDK 판정·검증 계약](../../../SDK/iiSocietyContainer/docs/Models.md)에 상세 기준을 기록한다.

macOS 패키지는 CMake가 선택한 iiSocietyContainer를 내장 데몬에도 복사한다. 다른 SDK의 이전 설치 경로를 `macdeployqt`가 먼저 찾더라도 구버전 분류기가 포함되지 않게 한다. `verify_daemon_package.py`는 선택한 원본과 내장 라이브러리의 Mach-O UUID 일치, 번들 내부 의존성, 실제 데몬 시작을 검사한다.

분류 중 Storage 화면 하단에 진행 상태·취소·오류를 표시한다. 이때 열린 모델 선택 메뉴를 닫고 병합 입력을 잠그며 완료 후 목록을 갱신한다. 시작 시 정리는 현재 탭·폴더를 바꾸지 않으며, 가져오기 완료 시에는 기존처럼 Models로 이동한다. 컨테이너를 바꾸면 이전 작업을 취소하고 새 컨테이너를 정리한다. SDK의 `organize()` 또는 앱 `ModelImporter::organizeModels()`로 다시 실행할 수 있다. 외부 프로그램·동기화로 추가된 파일은 다음 컨테이너 열기 또는 정리 호출 때 분류한다.

모델 파일·Diffusers·PEFT·Transformers 패키지는 SDK가 한 단위로 관리한다. 패키지 부속 설정과 미리보기는 분리하지 않는다. 이동 기록 `Models/.model-paths.json`으로 기존 `SharedStorage` 모델 참조를 해석하며, 이 파일은 모델과 함께 동기화할 컨테이너 데이터다. 기록과 실제 모델이 모두 도착한 뒤 이전 경로를 사용할 수 있다. `Other`의 모델도 병합 도구가 지원하는 형식이면 목록에 표시하고, 실행 시 iiLocalDiffusion이 호환성을 확인한다.

병합 드롭다운은 같은 `ModelStore` 목록에서 유형·형식·상대 경로를 표시한다. LoRA/LyCORIS/DoRA로 분류된 항목과 어댑터 폴더는 추가 재료로 선택한다. 자동 출력 경로는 베이스의 유형 폴더이다. 자세한 조작은 [모델 병합](ModelMerge.md)을 참고한다.

분류에는 파일 구조 검사가 포함되지만 가중치 값의 무결성·실행 가능성을 보장하지 않는다. 불명확한 레거시 가중치를 실행하거나 파일명만으로 종류를 추측하지 않는다. 필요한 경우 `model.safetensors.model.json`에 `{"type":"Checkpoint"}`처럼 명시하고 다시 정리하거나 SDK `place(path, ModelType::Checkpoint)`로 수정한다. 앱의 외부 가져오기 확장자 계약은 safetensors 두 종류이며, 이미 Models에 있는 다른 형식과 패키지도 관리 객체의 분류 대상이다.

`Society.ModelImport`는 Anima의 Other 복구·이름을 바꾼 신규 가져오기, 열기 시 정리, 가져오기 유형별 목적지·바이트 보존, 충돌·오류·취소와 컨테이너 전환을 검증한다. `Society.ModelMerge`는 유형별 입력 역할·기본 출력 경로·분류 중 입력 잠금을 검사하고 `Society.Drive`는 23개 폴더 표시와 드롭 후 이동을 검사한다. `SOCIETY_MODEL_TYPES_SCREENSHOT_PATH`로 유형 폴더 화면을 저장할 수 있다. `Society.AppleModelSource`는 파일 공급자의 임시 파일명에서도 헤더를 판정하고 유형별 경로·원본 보존·늦은 콜백 취소를 검사한다. SDK 설치 후 Society 및 동일 SDK를 사용하는 소비 앱을 재빌드한다. 작업 공간 검증은 `SDK/iiSocietyContainer/build/anima-classification/stage`의 패키지를 사용하며 사용자 설치 경로를 갱신하지 않는다.
