# 유형별 모델 보관

Society는 iiSocietyContainer의 `ModelStore`를 사용해 `Models/`를 23개 유형 디렉터리로 관리한다. Storage → Models도 같은 23개 유형을 같은 순서로 표시한다. 유형 이름과 순서는 SDK `allModelTypes()` → `StorageModelCatalog.categories`를 공유하며 화면에 별도 분류표를 두지 않는다.

- Checkpoint부터 Other까지 아래 23개 유형 순서로 표시한다. Image·Video·Audio·Language 미디어 그룹은 사용하지 않는다. 사이드바는 228 px, 콘텐츠 여백은 24 px, 각 제목 행은 60 px이다. `LV.Card.Model`은 256×280 px이고 카드 간격은 8 px, 제목과 목록 및 카테고리 사이 간격은 24 px다. 빈 유형은 44 px 안내 행으로 표시한다.
- 사이드바는 [Figma 39:1275의 네 그룹과 탐색 객체](StorageNavigation.md)를 사용한다. My storage 제목 아래 Models 행이 현재 선택을 표시하며 모델 콘텐츠 좌표는 그대로 유지한다.
- 각 목록은 독립적으로 가로 스크롤한다. 가로 휠·트랙패드, 드래그, 스크롤바와 방향키·Home/End 탐색을 지원한다. 전체 페이지는 세로 스크롤하며 760 px 미만에서는 사이드바를 숨긴다.
- 동기화된 저장소의 카드는 로컬에 캐시한 호스트 맵에서 이름·경로·형식·크기·보유 상태를 읽는다. 목록 표시를 위해 원본 모델 헤더를 열거나 패키지 파일을 순회하지 않는다. 맵에 없는 Architecture·Precision은 Unknown으로 표시한다. 호스트가 확정한 실제 유형 폴더를 그대로 사용한다. Motion, VLM, LLM은 독립된 유형이고 파일 확장자나 미디어 메타데이터로 다시 묶지 않는다.
- `StorageModels`는 SDK `StorageModelCatalog`의 QML 등록용 래퍼이다. 파일 조회와 분류는 SDK 작업 스레드에서 실행한다. 호스트 맵이 없는 로컬 전용 저장소도 실제 유형 폴더를 사용한다. 유형 폴더 밖의 항목은 정리 전까지 Other에 표시한다. `society.modality`는 분류를 덮어쓰지 않는다. 카탈로그 감시는 Models와 내부 메타데이터 디렉터리 두 곳으로 제한하고 30초 폴백 갱신을 둔다.
- 미보유 모델 카드를 누르면 그 파일 또는 패키지만 다운로드하며 카드에 진행 상태를 표시한다. Open은 검증된 다운로드 완료 후 파일을 열거나 패키지로 이동한다. 목록 조회·새로고침은 원본 요청을 만들지 않는다. 숨겨진 파일 그리드는 폴더 조회를 중단한다.
- 일반 터치는 모델 선택·다운로드만 실행한다. 우클릭 메뉴 처리는 마우스·터치패드로 제한하고, 터치 화면의 메뉴는 길게 누르기 또는 메뉴 버튼으로 연다. 단일 터치 뒤 의도하지 않은 메뉴가 다음 탭 이동을 막지 않는지 실제 터치 이벤트로 검사한다.
- 하단 `Society / Models`를 누르면 기존 23개 폴더와 미분류 모델을 탐색할 수 있다. 사이드바 Models를 누르면 유형별 목록으로 돌아온다. 각 Import models… 버튼은 기존 자동 분류 가져오기를 연다. 가져오기 형식과 원본 보존 정책은 아래 계약을 유지한다.
- 모델 추가·삭제·수정, 가져오기·정리 완료 및 화면 재진입 시 목록을 갱신한다. 바뀌지 않은 스냅샷은 갱신 신호를 내보내지 않으며, 실제 변경 시에도 선택·키보드 탐색 항목을 경로로 찾아 복원한다. 카테고리별 가로 스크롤과 페이지 세로 스크롤을 함께 유지하고, 카드 재배치 중 포커스가 카메라를 이동시키지 않는다. 연속 변경은 최초 저장한 위치로 합치며 컨테이너 전환은 이전 비동기 결과를 폐기한다.

`modelCatalogUsesHostIdentificationBeforeLocalHeaders`는 호스트 식별 정보를 원본 헤더보다 우선하는지 검사한다. SDK `shared_storage`는 8,000개 원격 항목의 비동기 조회, 폴더 전환의 오래된 결과 폐기, 미선택 원본 부재와 선택 파일의 버전 고정 요청을 검사한다. iOS `testModelsShowsMetadataAndRemainsResponsive`와 `testModelsDownloadsOnlyTheTappedCard`는 설치 앱의 Models 진입·재탐색과 카드 선택을 검사한다. 두 테스트 사이에 App Group 원본 부재를, 선택 뒤 파일 해시와 미선택 원본 부재를 별도 검증한다.

`testModelsBrowseInstalledLibrary`는 검증 파일을 정리한 뒤에도 기존 저장소의 모델 카드를 표시하는지 확인한다. 이 테스트는 모델이 있는 설치 앱을 사용하며 파일을 내려받거나 변경하지 않는다.

`SocietyDriveTests modelCatalogGroupsPhysicalTypesAndWatchesChanges`는 23개 물리 유형·메타데이터·용량·Other 표시·외부 링크 제외·유형 간 파일 이동·자동 갱신·이전 조회 폐기를 검사한다. `modelsShowTypesAndScrollEachCategoryIndependently`는 실제 사이드바 진입, 23개 유형 제목과 기존 카드 치수, 독립 가로 스크롤, 세로 스크롤, 가져오기·선택 복원·390 px 폭과 폴더 보기 전환을 검사한다. `SOCIETY_MODELS_SCREENSHOT_PATH`로 위쪽과 아래쪽 화면을 저장한다. 아래의 `SOCIETY_MODEL_TYPES_SCREENSHOT_PATH`는 폴더 보기 검증용이다.

카탈로그는 가중치 외에도 Poses·Workflows·ComfyUI Workflows의 JSON, Wildcards의 텍스트와 Other의 미확인 파일을 표시한다. 부속 메타데이터·미리보기는 별도 카드로 중복 표시하지 않으며 Diffusers·PEFT·Transformers 패키지는 하나의 항목으로 유지한다. 원격 조회에도 같은 규칙을 적용하고 원본 다운로드를 요청하지 않는다.

`Checkpoint`, `Embedding`, `Hypernetwork`, `Aesthetic Gradient`, `LoRA`, `LyCORIS`, `DoRA`, `Controlnet`, `Upscaler`, `Motion`, `VAE`, `Text Encoder`, `UNet`, `CLIP Vision`, `Poses`, `Wildcards`, `Workflows`, `ComfyUI Workflows`, `Detection`, `VLM`, `CLIP`, `LLM`, `Other`를 제공한다. **Storage → Models**에서 각 폴더를 탐색한다. Finder·파일 앱에는 기존처럼 `Files/`만 공개된다.

컨테이너를 열면 `ModelImporter`가 작업 스레드에서 누락된 유형 폴더를 생성하고 기존 미분류 모델을 정리한다. SDK가 모델 헤더·설정·부속 메타데이터로 유형을 판정하며 근거가 없으면 **Other**로 보관한다. 이미 유형 폴더에 있는 모델은 사용자의 배치를 유지한다. 새 `.safetensor`/`.safetensors` 가져오기는 유형을 판정한 뒤 완성된 복사본을 해당 폴더에 게시하고 원본 파일은 보존한다. 가져오기 중 동일 이름 충돌은 새 번호를 붙여 처리한다.

Anima 원본·파생 모델은 내부 확산 네트워크·LLM 어댑터·입출력 행렬의 조합과 차원을 검사하여 **Checkpoint**로 분류한다. 텍스트 인코더·VAE가 포함된 통합본도 동일하게 처리하며, Anima LoRA는 LoRA로 구분한다. 기존 `Other` 항목은 컨테이너를 다시 열거나 정리를 실행하면 이동 기록을 남기고 재분류한다. 이미 다른 명시적 유형 폴더에 놓인 항목은 내부 판정 확인 후 SDK의 명시적 `place()`로 수정한다.

Safetensors의 모든 텐서 자료형·차원·바이트 범위와 실제 파일 크기를 검증하며 중복 JSON 키, 오버플로, 겹치는 영역, 누락된 데이터를 거부한다. 손상된 파일은 sidecar에 유형이 있어도 정상 모델로 인식하지 않는다. 텐서 값은 로드하지 않아 대용량 모델도 헤더만 읽어 검사한다. [SDK 판정·검증 계약](../../../SDK/iiSocietyContainer/docs/Models.md)에 상세 기준을 기록한다.

macOS 패키지는 CMake가 선택한 iiSocietyContainer를 내장 데몬에도 복사한다. 다른 SDK의 이전 설치 경로를 `macdeployqt`가 먼저 찾더라도 구버전 분류기가 포함되지 않게 한다. `verify_daemon_package.py`는 선택한 원본과 내장 라이브러리의 Mach-O UUID 일치, 번들 내부 의존성, 실제 데몬 시작을 검사한다.

분류 중 Storage 화면 하단에 진행 상태·취소·오류를 표시한다. 이때 열린 모델 선택 메뉴를 닫고 병합 입력을 잠그며 완료 후 목록을 갱신한다. 시작 시 정리는 현재 탭·폴더를 바꾸지 않으며, 가져오기 완료 시에는 기존처럼 Models로 이동한다. 컨테이너를 바꾸면 이전 작업을 취소하고 새 컨테이너를 정리한다. SDK의 `organize()` 또는 앱 `ModelImporter::organizeModels()`로 다시 실행할 수 있다. 외부 프로그램·동기화로 추가된 파일은 다음 컨테이너 열기 또는 정리 호출 때 분류한다.

모델 파일·Diffusers·PEFT·Transformers 패키지는 SDK가 한 단위로 관리한다. 패키지 부속 설정과 미리보기는 분리하지 않는다. 이동 기록 `Models/.model-paths.json`으로 기존 `SharedStorage` 모델 참조를 해석하며, 이 파일은 모델과 함께 동기화할 컨테이너 데이터다. 기록과 실제 모델이 모두 도착한 뒤 이전 경로를 사용할 수 있다. `Other`의 모델도 병합 도구가 지원하는 형식이면 목록에 표시하고, 실행 시 iiLocalDiffusion이 호환성을 확인한다.

병합 드롭다운은 같은 `ModelStore` 목록에서 유형·형식·상대 경로를 표시한다. LoRA/LyCORIS/DoRA로 분류된 항목과 어댑터 폴더는 추가 재료로 선택한다. 자동 출력 경로는 베이스의 유형 폴더이다. 자세한 조작은 [모델 병합](ModelMerge.md)을 참고한다.

분류에는 파일 구조 검사가 포함되지만 가중치 값의 무결성·실행 가능성을 보장하지 않는다. 불명확한 레거시 가중치를 실행하거나 파일명만으로 종류를 추측하지 않는다. 필요한 경우 `model.safetensors.model.json`에 `{"type":"Checkpoint"}`처럼 명시하고 다시 정리하거나 SDK `place(path, ModelType::Checkpoint)`로 수정한다. 앱의 외부 가져오기 확장자 계약은 safetensors 두 종류이며, 이미 Models에 있는 다른 형식과 패키지도 관리 객체의 분류 대상이다.

`Society.ModelImport`는 Anima의 Other 복구·이름을 바꾼 신규 가져오기, 열기 시 정리, 가져오기 유형별 목적지·바이트 보존, 충돌·오류·취소와 컨테이너 전환을 검증한다. `Society.ModelMerge`는 유형별 입력 역할·기본 출력 경로·분류 중 입력 잠금을 검사하고 `Society.Drive`는 23개 폴더 표시와 드롭 후 이동을 검사한다. `SOCIETY_MODEL_TYPES_SCREENSHOT_PATH`로 유형 폴더 화면을 저장할 수 있다. `Society.AppleModelSource`는 파일 공급자의 임시 파일명에서도 헤더를 판정하고 유형별 경로·원본 보존·늦은 콜백 취소를 검사한다. SDK 설치 후 Society 및 동일 SDK를 사용하는 소비 앱을 재빌드한다. 이번 유형 카탈로그 변경은 `~/.local/SDK/iiSocietyContainer` 설치본과 Society의 `build/bin/Society.app`에 반영하여 검증한다.

## 생성용 보정 모델의 단일 보관

일반 모델 임포터는 입력 하나를 복사·분류할 뿐 `-complete` 파생본을 자동 생성하지 않는다.
2026-09-20 Dreamscapes 실생성 검증에서 Anima 체크포인트에 기존 Qwen 텍스트 인코더와 VAE를
합친 완성본을 별도로 게시하여 원본과 완성본이 각각 카드로 표시되었다.

이처럼 하나의 체크포인트를 실행하기 위해 구성요소를 보충한 경우 Society에는 실행 가능한 패키지
하나만 게시한다. 원본 체크포인트 가중치와 텍스트 인코더·VAE가 통합된 safetensors를 패키지 내부
`model.safetensors`로 내장하고 `model_index.json`에 상대 경로·크기·SHA-256과 출처를 기록한다.
원본 denoiser가 내장 가중치에 이미 포함되어 있으므로 패키지 안에 동일 원본을 다시 중복 저장하지 않는다.

기존 `iild-unified-model-v1` / `IILDUnifiedCascade` 규격의 단일 stage(strength 1)를 사용한다.
여러 모델을 합성하는 추가 추론 단계는 없으며 기존 네이티브 실행기가 내장 체크포인트를 실행한다.
패키지 확장자는 SDK의 정식 표기인 `.iildmodel`을 사용하며, ZIP64 stored 컨테이너의 단일 파일이다.
Society는 `.iildmodel` 파일 자체를 한 모델 및 병합 인자로 관리하고 내부 구성요소를 별도 카드로 노출하지 않는다.
SDK는 중앙 디렉터리와 `model_index.json`, 구성요소 크기·CRC·SHA-256을 검사한 뒤 로컬 캐시에 안전하게 물질화한다.
과거 디렉터리형 `.iildmodel`은 읽기 호환만 유지하며 새 병합 결과는 항상 단일 파일로 게시한다.
패키지 내부 가중치는 카탈로그에 별도 모델로 표시되지 않는다.

보정 전 원본과 조립 검증 기록은 Models 밖의 작업 공간에 보관한다. 파일명만으로 자동 병합하거나
삭제하지 않으며, 원본 출처 해시 및 내장 가중치 무결성을 확인한 뒤 게시한다.
이전 safetensors 및 complete 경로는 `.model-paths.json`의 별칭으로 패키지에 연결한다.

현재 패키지는 `chosenIrisesMix_v20Anima.iildmodel`과 `miaomiaoRealskin_anima13.iildmodel`이다.
패키징 검증은 `build/model-packaging/`, 앞선 원본 백업은 `build/model-consolidation/originals/`에 있다.

## 변경 없는 재검사와 문서 파일

Storage Models의 최초 조회만 로딩 문구를 표시한다. 이후 컨테이너·파일 감시·주기적 재검사는 기존 카드를 유지하며, 표시할 행이나 오류 상태가 실제로 달라졌을 때만 변경 신호를 전달한다. 같은 목록을 반복해서 읽는 동안 빈 카테고리 문구, 카드 객체, 선택과 스크롤을 재설정하지 않는다.

VAE 등 가중치 유형 폴더에서는 지원하는 가중치 확장자의 파일과 모델 패키지만 카드로 표시한다. README·LICENSE·NOTICE·독립 설정 JSON은 모델 카드에서 제외하며 원본은 삭제하거나 이동하지 않는다. Wildcards의 텍스트, Workflows·Poses의 JSON/이미지, Other의 미분류 파일은 각 용도에 맞게 유지한다. 동일 기준을 SDK의 로컬 조회와 다운로드 전 원격 카탈로그 조회에 적용한다.

모델 가져오기 동안 영구 상태 및 취소 컨트롤 은 모델 스크롤 영역 위에 유지됩니다. 대형 체크포인트 복사 는 따라서 모든 카테고리 스크롤 위치에서 진행 상황을 노출합니다. Krea2 분류는 iiSocietyContainer 에서 제공되며 이전에 가져온 Krea2 가중치는 Other 에서 컨테이너 열 때 재구성됩니다. 로컬 safetensors 소스는 복사 전에 구조적으로 유효성 검사를 거치고, 공급자 복사는 게시 전에 유효성 검사를 거칩니다. 단축 다운로드 는 명시적인 재시도 후 다운로드 오류를 보고하며, 결코 모델 행이 되지 않습니다. 테스트 는 실제 safetensors 프레임을 사용합니다. 공식 호스트 에서 새로 가져온 카테고리 파일 은 동기화 해싱이 완료되기 전에 표시됩니다; 동기화 메타데이터 및 패키지 탐색 은 가져오기 완료와 독립적으로 유지됩니다. 리플라 탐색 은 여전히 호스트의 카탈로그를 사용합니다.
