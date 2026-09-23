# Tools · Model merge

## Figma 인터페이스

[Society의 모델 병합 본문 121:752](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=121-752)을 기존 `ModelMergeTool`에 적용했다. Tools 카드 목록, 상단 시스템 탐색, 병합 실행기와 계산 계약은 유지한다.

데스크탑은 왼쪽 **Input models / Merge settings**, 오른쪽 **Output / Input check**의 두 열이다. 오른쪽 열은 348 px, 열 사이는 24 px, 카드 사이는 20 px, 본문 여백은 32 px이다. `LV.Card`, `LV.ListItem`, `LV.LabelSegmentedControl`, `LV.InputField`, `LV.LabelButton`, `LV.Tooltip`과 LVRS 색상·글꼴을 사용한다. **All tools**는 본문 제목 위에 배치하며 설정과 실행 중 작업을 유지한 채 목록으로 돌아간다.

너비 1000 px 미만에서는 한 열로 쌓는다. 일반 좁은 창은 24 px, 760 px 미만 터치 화면은 16 px 여백이며 터치 조작은 최소 44 px 높이를 유지한다. 내용은 세로로 스크롤하고 키보드로 초점을 옮기거나 창 크기를 바꾸면 해당 조작을 화면 안으로 보여 준다.

가중치 방식은 **Automatic / Shared weight / Per-model** 선택으로 전환한다. 방식 전환으로 기존 가중치를 지우지 않는다. 캐시와 실행기·Python 경로는 **Advanced settings**를 펼쳐 편집하며 접어도 값을 보존한다. 입력 변경 시 이전 검사를 현재 입력의 검사 성공으로 표시하지 않는다. 실행 중에는 입력·출력 설정을 잠그고 검사 영역에서 취소·경과 시간·결과 폴더·보고서를 제공한다.

Figma의 모델명은 예시이며 앱의 입력은 비어 있는 상태로 시작한다. **Save to**에서 폴더를 선택하고 그 아래에 결과 파일명을 표시한다. 전체 출력 경로는 파일명의 LVRS 툴팁과 접근성 설명으로 확인한다. 필수 첫 재료의 **Remove material**은 선택만 비우며, 추가 재료의 **Remove**는 해당 행을 제거한다.


**Model name** is required. The selected method adds `.iildmodel` for Unified or `.safetensors` for weighted arithmetic, without duplicating an entered extension. Empty names, path separators, hidden names and control characters are rejected before checking or merging. The controller requires an explicit output path even for inspection; inspection does not create it.

**베이스와 모든 재료는 병합 후에도 같은 경로에 그대로 보존한다.** 모델을 이동하거나 삭제하지 않으며 원본과 기존 출력의 덮어쓰기를 거부한다. 재료 행의 **Remove**는 병합 목록에서만 선택을 제거한다. 성공·검사 실패·병합 실패·취소 시 원본 바이트 보존을 회귀 검사한다.

**Check inputs**는 SDK `--inspect`로 전체 가중치 해시 전에 구조와 공통 레이어 투영, LoRA 대상·강도를 확인한다. Society의 weighted 병합은 `--checkpoint-policy common-layer`를 항상 전달한다. 키·shape·아키텍처 메타데이터가 다른 Flux/Krea 계열도 베이스 텐서 레이아웃을 출력 규격으로 삼아 역할·블록 깊이·shape가 가장 가까운 재료 텐서를 연결한다. INT8 텐서는 동반 `weight_scale`을 적용하고, 크기가 다르면 선형 crop/zero-pad하며, 대응할 같은 종류의 텐서가 없으면 해당 베이스 값을 보존한다. 이는 학습된 의미나 이미지 품질을 보장하지 않지만 정상 safetensors 입력에 대해서는 실행 가능한 베이스 규격 출력을 만드는 정책이다. 손상·미완료 safetensors, NaN/Inf와 읽을 수 없는 입력은 계속 실패한다. 실제 텐서 값과 원본 해시는 실제 병합 시 검사한다. **Copy full report**에는 출처·실제 가중치·공통 레이어 매핑 요약·파일 해시가 포함된다. SDK 실행 파일 선택은 환경 변수, 앱 옆 실행기, CMake 지정 실행기, 사용자 설치 경로, PATH 순서이다.

Anima의 `model.diffusion_model.*`·`net.*` 등 동등한 저장 접두사는 SDK가 전체 텐서 목록의 일대일 대응을 확인한 뒤 맞춰 읽는다. 크기·자료형·예측 설정 검사는 유지하며, 결과에는 베이스의 텐서 이름을 보존한다. 원본 파일을 변환하거나 이름을 바꾸는 처리는 아니다.

[Tools 카드 목록](Tools.md)의 **Model merge**를 눌러 진입한다. **All tools**로 목록에 돌아가도 설정과 실행 중 작업을 유지한다.

Society 데스크톱의 **Tools → Model merge**는 iiLocalDiffusion의 설치된 `iild-merge`를 사용하여 로컬 체크포인트와 LoRA를 새 모델로 병합한다. 기존 Qt의 `QProcess`가 실행과 취소·결과를 관리하고 LVRS가 입력 UI를 제공한다. 텐서 연산, 모델 형식 검사, LoRA 적용, 원본 검증과 출력 저장은 SDK에 위임한다. 새 라이브러리나 서비스는 추가하지 않는다.

베이스는 단일 전체 체크포인트 파일이고, 첫 추가 모델은 필수이다. 추가 모델은 단일 체크포인트 또는 LoRA 파일·어댑터 폴더이며 **Add material**로 개수 제한 없이 늘리고 **Remove**로 제거한다. **입력 모델은 현재 컨테이너의 `Models/` 목록에서 선택한다.** LVRS `ListItem` 모델 행을 누르거나 우클릭하면 `ContextMenu`가 열리며, 파일 경로를 입력하거나 운영체제 파일·폴더 선택기를 열 필요가 없다. Society의 시스템 파일 공급자가 `Files/`만 노출하더라도 Models의 모델을 선택할 수 있다.

`MergeModelCatalog`는 Qt Concurrent에서 iiSocietyContainer의 `ModelStore`로 Models 하위 폴더를 비동기 조회한다. `.safetensors`, `.safetensor`, `.ckpt`, `.pt`, `.pth`, `.bin`, `.iildmodel` 파일을 표시하며, `adapter_config.json`이 있는 어댑터 폴더는 내부 가중치를 중복 나열하지 않고 하나의 항목으로 표시한다. 일반 Diffusers 폴더와 그 내부 구성원은 단일 출력 체크포인트를 보장하기 위해 병합 선택 목록에서 제외한다. 단일 체크포인트를 내장한 `.iildmodel` 패키지는 아래 검증 조건에 따라 하나의 입력으로 표시한다. 유형·저장 형식을 함께 표시하며, 어댑터 폴더와 LoRA/LyCORIS/DoRA로 분류된 파일은 추가 재료 목록에만 표시한다. 설정 파일 없는 단일 LoRA도 헤더에서 판정할 수 있다. 빈 파일, 숨김·스테이징 항목, 심볼릭 링크와 지원하지 않는 확장자는 목록에서 제외한다. 파일의 실제 체크포인트/LoRA 종류와 모델 호환성은 SDK가 병합 시 판별하며 목록 조회는 텐서를 로드하지 않는다.

동일 이름은 메뉴와 선택 항목 아래의 Models 상대 경로로 구분한다. 긴 목록은 스크롤하며 방향키·Home·End·Enter로 선택하고 Escape로 닫는다. 메뉴를 열거나 Tools 탭으로 돌아올 때, **Refresh models**를 누를 때, 병합이 완료될 때 목록을 갱신한다. 변경 없는 조회는 선택을 유지하며 삭제된 모델의 선택만 해제한다. 열린 컨테이너를 바꾸거나 사용할 수 없게 되면 이전 모델 선택과 출력 이름·사용자 지정 저장 폴더를 비운다. 이전 컨테이너에서 진행 중이던 조회 결과는 적용하지 않는다. 모델이 없으면 Storage에서 가져오도록 안내하며, 베이스와 모든 재료를 선택하고 유효한 출력 이름을 입력하기 전에는 실행 버튼을 비활성화한다. 탭 전환은 설정을 유지하고 실행 중에는 편집을 잠근다. 컨테이너의 자동 분류 중에도 입력을 잠그며 완료 후 목록을 갱신한다. 폴더 분류와 기존 모델 이동은 [Models 관리](Models.md)를 따른다.

## 노출하는 입력

| 화면 | SDK 입력 | 동작 |
| --- | --- | --- |
| Base model (A) | `--base-model` | Models 드롭다운에서 단일 전체 체크포인트 파일 선택 |
| Material 1…N | 반복 `--additional-model` | Models 드롭다운에서 필수 첫 재료와 추가 체크포인트·LoRA 선택 |
| Unified | `--mode unified` | Ordered image refinement with architecture-specific checkpoints and LoRA routing |
| Weighted sum / Weighted difference | `--mode` | 호환 가중치의 합·차, 기본값은 합 |
| Common layer | `--checkpoint-policy common-layer` | 베이스 레이아웃으로 이종 체크포인트를 결정론적으로 투영하며 품질 동등성은 보장하지 않음 |
| Automatic weights | `--weights` 생략 | SDK가 모델 종류를 검사한 뒤 가중치를 결정 |
| Shared weight | `--weights`에 값 1개 | 모든 추가 모델에 동일한 가중치 적용 |
| Per-model weights | `--weights`에 N개 | 재료 순서대로 모든 가중치를 직접 입력 |
| Model name / Save to | `--output` | New `.iildmodel` package (Unified) or `.safetensors` file (weighted arithmetic) |
| Conversion cache | `--cache-dir` | 레거시 체크포인트 변환 캐시 경로 |
| Check inputs | `--inspect` | 구조·예측 설정·LoRA 대상과 강도를 검사하여 JSON 표시 |
| iiLocalDiffusion executable | 실행 프로그램 | 설치된 `iild-merge` 자동 탐색 또는 직접 지정 |
| Python executable | `IILD_PYTHON_EXECUTABLE` | 선택적 Python 실행 파일. 비우면 SDK의 기본 환경 사용 |

가중치는 유한한 0 이상의 수이며 소수와 과학적 표기법을 지원한다. Per-model에서는 모든 재료의 가중치가 필요하다. `NaN`, 무한대, 음수와 쉼표를 포함하는 숫자는 거부한다. LoRA 강도와 차 방식 가중치를 임의로 1 이하로 제한하지 않는다.

저장 폴더를 비우면 열린 Society 컨테이너의 베이스 모델 유형 폴더(`Models/Checkpoint/` 등, 미확정은 `Models/Other/`)를 사용한다. **Save to**에서 저장 폴더를 바꿔도 입력한 출력 이름은 유지한다. 출력 이름은 필수이며 폴더만 지정해서는 실행할 수 없다. 캐시를 비우면 Society의 운영체제별 애플리케이션 캐시 아래 `model-merge`를 사용한다.

## 합·차의 의미

추가 전체 체크포인트를 `Bᵢ`, 가중치를 `wᵢ`, LoRA 델타를 `Dⱼ`, 강도를 `sⱼ`라 할 때 다음 식을 사용한다.

- 합: `(1 − Σwᵢ) A + Σ(wᵢ Bᵢ) + Σ(sⱼ Dⱼ)`
- 차: `A − Σ(wᵢ Bᵢ) − Σ(sⱼ Dⱼ)`

자동 합은 베이스와 추가 체크포인트에 동등한 비중을 주고, 자동 차는 추가 체크포인트마다 `0.5`를 뺀다. LoRA의 자동 강도는 두 방식 모두 `1`이며 베이스의 체크포인트 비중을 차감하지 않는다. 합에서 추가 체크포인트 가중치 합은 1 이하여야 하며 이 검사는 SDK의 모델 종류 판별 후 수행된다.

베이스 계수, LoRA alpha/rank 스케일, 누적 정밀도와 출력 자료형은 SDK가 결정하며 사용자 입력 파라미터가 아니다. 현재 SDK는 CPU FP32/FP64로 누적하고 각 베이스 텐서의 자료형을 보존한다. GPU 선택·출력 dtype·블록별 가중치·임의 alpha/rank 재정의는 지원하는 API가 없어 입력 컨트롤을 제공하지 않는다.

## 실행과 결과

**Check inputs**는 가중치 헤더와 LoRA 스케일 설정으로 구조를 검사한다. 레거시 포맷은 안전한 변환 캐시가 필요하다. 실제 **Merge models**에서 비유한 값·오버플로·출처 해시를 추가로 검사한다. 성공하면 저장 위치와 SDK 보고서를 제공하며 **Open output folder**로 결과 부모를 연다. **Copy full report**는 출처·실제 강도·LoRA 대상·출력 해시를 포함한 전체 JSON을 복사한다. 화면 보고서는 처음 16,000자만 표시한다.

실행은 비동기이며 경과 시간과 실행 상태를 표시한다. SDK는 텐서별 진행률을 내보내지 않으므로 추정 퍼센트를 표시하지 않는다. Unix의 **Cancel**은 해당 자식 프로세스에 SIGINT를 전달하여 SDK의 정리 구문이 실행되게 한다. 현재 네이티브 연산이 끝나야 취소가 처리될 수 있다. Windows에서는 종료 요청 후 필요하면 해당 프로세스를 종료한다. 창을 닫으면 실행 중인 자식도 정리한다. 비정상 강제 종료는 SDK의 임시 디렉터리를 남길 수 있으나 Society가 원본이나 이미 게시된 출력을 삭제하지는 않는다. 완료와 취소가 경합하여 출력이 나타난 경우에는 확인이 필요하다는 상태를 표시한다.

SDK는 기존 출력과 원본 덮어쓰기를 거부하고 스테이징이 완료된 결과를 게시한다. Society는 셸 명령 문자열 없이 프로그램·인자 배열을 전달하여 공백·따옴표·셸 문자가 있는 파일명도 데이터로 취급한다. 모바일에서는 기존 Storage 화면을 유지한다.

## 설치와 검증

SDK와 호환되는 PyTorch·safetensors 환경이 필요하며 LoRA 이름 변환에는 기존 Diffusers 환경을 사용한다. Python 패키지를 자동 설치하거나 모델을 다운로드하지 않는다. 설치형 런처의 기본 가상 환경 또는 화면의 Python 실행 파일을 사용한다.

`Society.ModelMerge`는 실제 설치된 `iild-merge`를 실행하여 다음을 검사한다.

- 전체 파라미터 전달, 설정 검증의 무출력 동작, 공백·따옴표·셸 문자가 포함된 출력 경로.
- 공식 safetensors 직렬화로 만든 작은 체크포인트·LoRA에 대한 합·차와 자동·공통·개별 가중치의 독립 PyTorch 계산 비교, 원본 바이트와 정수 버퍼 보존.
- 단일 Safetensors 출력 강제, 이름·출력 경로 누락 거부, Diffusers 베이스와 통합 모드 거부, 호환되지 않는 텐서의 공통 레이어 투영·베이스 규격 출력, 손상 입력의 오류·무출력, 원본·기존 결과 덮어쓰기 차단, 취소 후 원본 보존.
- 컨테이너 내부 재귀 목록, 유형별 입력 역할·출력 경로, 동일 이름 구분, 패키지 단위 선택, 숨김·외부 링크 제외, 변경 없는 목록 보존과 컨테이너 교체 중 이전 조회 무시.
- LVRS 드롭다운의 클릭·우클릭, 긴 목록 스크롤과 키보드 선택, 새 모델 발견·삭제 시 선택 해제, 비어 있는 컨테이너와 작은 창의 메뉴 경계.
- 필수 이름 입력·확장자 중복 방지·출력 경로 표시, 모델 선택에서 설정 검증·실제 병합 버튼 실행까지의 연결, 재료 추가·제거, 1440/760/360px와 최소 360×320 창 배치. `SOCIETY_MERGE_SCREENSHOT_PATH`와 `SOCIETY_MERGE_MENU_SCREENSHOT_PATH`로 테스트 데이터가 있는 실제 macOS 화면을 저장할 수 있다.

텐서 테스트에는 설치된 SDK의 Python 환경이 필요하다. 런타임이 없으면 해당 검사만 명시적으로 skip되며 경로·인자·QML 검증은 계속한다. `Society.Drive`는 Tools·Dashboard·Storage 왕복 시 병합 설정과 기존 폴더·프롬프트 보존을 검사한다. 모델의 미적 품질과 다중 GB 실사용 체크포인트의 실행 시간은 이 작은 텐서 검증이 보장하지 않는다.

모바일 Tools 탭도 카드 목록에서 Model merge를 선택하면 동일한 `ModelMergeTool`을 표시한다. 좁은 화면은 16 px 여백과 한 열의 스크롤 폼을 사용하고 터치 입력·버튼의 높이를 확보한다. 플랫폼이 로컬 병합을 지원하지 않으면 기존 `ModelMergeController.supported`에 따라 실행을 비활성화하고 설명을 표시한다. 모바일용 병합 백엔드를 새로 제공하는 변경은 아니다. 설정은 다른 탭이나 화면 방향으로 이동해도 유지한다.

Figma 화면 회귀는 1212 px 본문의 348 px 요약 열, 카드 간격, 기본 접힘 상태, 가중치 전환과 설정 보존, 실행 중 편집 잠금·취소, 출력 파일명·전체 경로 툴팁, 좁은 창의 초점 스크롤을 검사한다. `SOCIETY_MERGE_DESIGN_SCREENSHOT_PATH`로 입력 완료 상태의 화면을 저장한다.

## 단일 체크포인트 패키지 입력

Storage에서 하나의 모델로 보관하는 `.iildmodel`도 베이스와 추가 재료에 하나의 항목으로 표시한다. 신규 패키지는 ZIP64 stored 단일 파일이며 Society는 그 파일 경로 자체를 병합기에 전달한다. SDK가 중앙 디렉터리와 manifest를 검사하고, `iild-unified-model-v1`의 단일 stage·strength 1·별도 LoRA 없음 조건을 확인한 다음 격리 캐시에 물질화한다. 내부 체크포인트는 별도 모델로 노출하지 않는다. 여러 stage, 별도 LoRA 적용, 잘못된 manifest, 누락·변조 파일, 중복·압축 엔트리와 패키지 밖 경로는 단일 체크포인트로 취급하지 않는다. 과거 디렉터리형 `.iildmodel`은 읽기 호환을 위해 기존 내부 체크포인트 해석을 유지한다. 일반 Diffusers 폴더 제외 규칙은 유지한다. `packagedModelIsPassedWholeToTheSdkAndCanBeMerged`와 `wrappedCheckpointsAppearOnceAndResolveForExecution`에서 각각 단일 파일의 실제 산술 병합과 레거시 디렉터리 호환을 검증한다.

## Unified input normalization

Unified is the default and writes one `.iildmodel` package. Different architectures retain their networks and refine images sequentially. Each LoRA is routed only to a checkpoint with matching targets and dimensions. Missing base networks cannot be reconstructed from a LoRA or fixed by arbitrary reshaping. Advanced settings can specify a LoRA compatibility checkpoint; the SDK also discovers registered local checkpoints. Weighted sum/difference remain available for matching architectures and write `.safetensors`.

Input checking never executes a merge or writes its output. `unifiedInspectionPreservesArchitecturesWithoutWritingOutput` verifies the unified request, stages, LoRA routing, explicit compatibility paths, output naming, and no output/cache creation.

The installed SDK launcher selects an explicit Python override first, then `reference/runtime-python.json`, then its bundled `.venv` or launch interpreter. It rejects Python older than 3.10 before model imports and explains how to select a valid environment. A configured SDK environment works without entering Python in Society each session.

### LoRA alias normalization

LoRA projection pairs are normalized to the checkpoint's actual tensor address after shape, rank, alpha and orientation checks. Some exports include both SGM and Diffusers names for the same address. Identical projection tensors with identical dtype, scale and orientation are applied once. Distinct projection pairs are retained as additive deltas on that address, including each pair's own alpha/rank scale. No target is silently dropped or arbitrarily reshaped. Reports expose `lora_alias_policy` as `identical-projections-once; distinct-projections-additive`. Input inspection may read the duplicate LoRA projections to establish equality, but does not materialize full checkpoint tensors or produce a merged model.

The LoRA regression suite verifies duplicate aliases apply once, distinct projections sum correctly, and different alpha values preserve their individual scaling, using tiny fixture tensors.
