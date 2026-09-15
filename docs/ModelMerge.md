# Tools · Model merge

Society 데스크톱의 **Tools → Model merge**는 iiLocalDiffusion의 설치된 `iild-merge`를 사용하여 로컬 체크포인트와 LoRA를 새 모델로 병합한다. 기존 Qt의 `QProcess`가 실행과 취소·결과를 관리하고 LVRS가 입력 UI를 제공한다. 텐서 연산, 모델 형식 검사, LoRA 적용, 원본 검증과 출력 저장은 SDK에 위임한다. 새 라이브러리나 서비스는 추가하지 않는다.

베이스는 전체 체크포인트 파일 또는 Diffusers 폴더이고, 첫 추가 모델은 필수이다. 추가 모델은 체크포인트 또는 LoRA 파일·폴더이며 **Add material**로 개수 제한 없이 늘리고 **Remove**로 제거한다. **입력 모델은 현재 컨테이너의 `Models/` 목록에서 선택한다.** LVRS `LabelMenuButton`을 누르거나 우클릭하면 `ContextMenu`가 열리며, 파일 경로를 입력하거나 운영체제 파일·폴더 선택기를 열 필요가 없다. Society의 시스템 파일 공급자가 `Files/`만 노출하더라도 Models의 모델을 선택할 수 있다.

`MergeModelCatalog`는 Qt Concurrent에서 iiSocietyContainer 0.11.0의 `ModelStore`로 Models 하위 폴더를 비동기 조회한다. `.safetensors`, `.safetensor`, `.ckpt`, `.pt`, `.pth`, `.bin` 파일을 표시하며, `model_index.json`이 있는 Diffusers 폴더와 `adapter_config.json`이 있는 어댑터 폴더는 내부 가중치를 중복 나열하지 않고 하나의 항목으로 표시한다. 유형·저장 형식을 함께 표시하며, 어댑터 폴더와 LoRA/LyCORIS/DoRA로 분류된 파일은 추가 재료 목록에만 표시한다. 설정 파일 없는 단일 LoRA도 헤더에서 판정할 수 있다. 빈 파일, 숨김·스테이징 항목, 심볼릭 링크와 지원하지 않는 확장자는 목록에서 제외한다. 파일의 실제 체크포인트/LoRA 종류와 모델 호환성은 SDK가 병합 시 판별하며 목록 조회는 텐서를 로드하지 않는다.

동일 이름은 메뉴와 선택 항목 아래의 Models 상대 경로로 구분한다. 긴 목록은 스크롤하며 방향키·Home·End·Enter로 선택하고 Escape로 닫는다. 메뉴를 열거나 Tools 탭으로 돌아올 때, **Refresh models**를 누를 때, 병합이 완료될 때 목록을 갱신한다. 변경 없는 조회는 선택을 유지하며 삭제된 모델의 선택만 해제한다. 열린 컨테이너를 바꾸거나 사용할 수 없게 되면 이전 모델 선택과 사용자 지정 출력 경로를 비운다. 이전 컨테이너에서 진행 중이던 조회 결과는 적용하지 않는다. 모델이 없으면 Storage에서 가져오도록 안내하며, 베이스와 모든 재료를 선택하기 전에는 실행 버튼을 비활성화한다. 탭 전환은 설정을 유지하고 실행 중에는 편집을 잠근다. 컨테이너의 자동 분류 중에도 입력을 잠그며 완료 후 목록을 갱신한다. 폴더 분류와 기존 모델 이동은 [Models 관리](Models.md)를 따른다.

## 노출하는 입력

| 화면 | SDK 입력 | 동작 |
| --- | --- | --- |
| Base model (A) | `--base-model` | Models 드롭다운에서 전체 체크포인트 또는 Diffusers 폴더 선택 |
| Material 1…N | 반복 `--additional-model` | Models 드롭다운에서 필수 첫 재료와 추가 체크포인트·LoRA 선택 |
| Weighted sum / Weighted difference | `--mode` | 가중합 또는 직접 가중차 |
| Automatic weights | `--weights` 생략 | SDK가 모델 종류를 검사한 뒤 가중치를 결정 |
| Shared weight | `--weights`에 값 1개 | 모든 추가 모델에 동일한 가중치 적용 |
| Per-model weights | `--weights`에 N개 | 재료 순서대로 모든 가중치를 직접 입력 |
| New model path | `--output` | 체크포인트는 새 `.safetensors`/`.safetensor`, Diffusers는 새 폴더 |
| Conversion cache | `--cache-dir` | 레거시 체크포인트 변환 캐시 경로 |
| Check inputs | `--print-config` | 경로와 파라미터를 검증하고 설정 JSON 표시 |
| iiLocalDiffusion executable | 실행 프로그램 | 설치된 `iild-merge` 자동 탐색 또는 직접 지정 |
| Python executable | `IILD_PYTHON_EXECUTABLE` | 선택적 Python 실행 파일. 비우면 SDK의 기본 환경 사용 |

가중치는 유한한 0 이상의 수이며 소수와 과학적 표기법을 지원한다. Per-model에서는 모든 재료의 가중치가 필요하다. `NaN`, 무한대, 음수와 쉼표를 포함하는 숫자는 거부한다. LoRA 강도와 차 방식 가중치를 임의로 1 이하로 제한하지 않는다.

출력을 비우면 열린 Society 컨테이너의 베이스 모델 유형 폴더(`Models/Checkpoint/` 등, 미확정은 `Models/Other/`)에 `<base>-sum.safetensors` 또는 `<base>-difference.safetensors`를 제안한다. Diffusers 베이스에는 확장자 없는 새 폴더를 제안한다. 출력·캐시·실행 환경은 경로 입력과 파일·폴더 선택기를 유지하며 절대 경로, `~/`, 로컬 파일 URL을 정규화한다. **Choose parent folder**로 출력 부모를 고른 뒤 새 이름을 수정할 수 있다. 캐시를 비우면 Society의 운영체제별 애플리케이션 캐시 아래 `model-merge`를 사용한다. UI 기본 경로를 명시적인 SDK 인자로 전달하므로 SDK 설치 디렉터리에 결과를 기록하지 않는다.

## 합·차의 의미

추가 전체 체크포인트를 `Bᵢ`, 가중치를 `wᵢ`, LoRA 델타를 `Dⱼ`, 강도를 `sⱼ`라 할 때 다음 식을 사용한다.

- 합: `(1 − Σwᵢ) A + Σ(wᵢ Bᵢ) + Σ(sⱼ Dⱼ)`
- 차: `A − Σ(wᵢ Bᵢ) − Σ(sⱼ Dⱼ)`

자동 합은 베이스와 추가 체크포인트에 동등한 비중을 주고, 자동 차는 추가 체크포인트마다 `0.5`를 뺀다. LoRA의 자동 강도는 두 방식 모두 `1`이며 베이스의 체크포인트 비중을 차감하지 않는다. 합에서 추가 체크포인트 가중치 합은 1 이하여야 하며 이 검사는 SDK의 모델 종류 판별 후 수행된다.

베이스 계수, LoRA alpha/rank 스케일, 누적 정밀도와 출력 자료형은 SDK가 결정하며 사용자 입력 파라미터가 아니다. 현재 SDK는 CPU FP32/FP64로 누적하고 각 베이스 텐서의 자료형을 보존한다. GPU 선택·출력 dtype·블록별 가중치·임의 alpha/rank 재정의는 지원하는 API가 없어 입력 컨트롤을 제공하지 않는다.

## 실행과 결과

**Check inputs**는 모델 텐서를 로드하거나 결과·캐시를 만들지 않는다. 이 검증만으로 모델끼리 호환된다고 판단하지 않는다. 실제 **Merge models**에서 SDK가 텐서 키·크기·구조·비유한 값과 오버플로를 검사한다. 성공하면 저장 위치, 병합 텐서 수·모델 수·베이스 계수와 SDK 보고서를 제공하며 **Open output folder**로 결과 부모를 연다. **Copy full report**는 원본 해시·실제 가중치·LoRA 종류·출력 해시를 포함한 전체 JSON을 복사한다. 화면 보고서는 처음 16,000자만 표시한다.

실행은 비동기이며 경과 시간과 실행 상태를 표시한다. SDK는 텐서별 진행률을 내보내지 않으므로 추정 퍼센트를 표시하지 않는다. Unix의 **Cancel**은 해당 자식 프로세스에 SIGINT를 전달하여 SDK의 정리 구문이 실행되게 한다. 현재 네이티브 연산이 끝나야 취소가 처리될 수 있다. Windows에서는 종료 요청 후 필요하면 해당 프로세스를 종료한다. 창을 닫으면 실행 중인 자식도 정리한다. 비정상 강제 종료는 SDK의 임시 디렉터리를 남길 수 있으나 Society가 원본이나 이미 게시된 출력을 삭제하지는 않는다. 완료와 취소가 경합하여 출력이 나타난 경우에는 확인이 필요하다는 상태를 표시한다.

SDK는 기존 출력과 원본 덮어쓰기를 거부하고 스테이징이 완료된 결과를 게시한다. Society는 셸 명령 문자열 없이 프로그램·인자 배열을 전달하여 공백·따옴표·셸 문자가 있는 파일명도 데이터로 취급한다. 모바일에서는 기존 Storage 화면을 유지한다.

## 설치와 검증

기본 실행 파일 탐색은 `SOCIETY_MODEL_MERGE_EXECUTABLE` 환경 변수, 앱 실행 디렉터리, `~/.local/SDK/iiLocalDiffusion/bin/iild-merge`, CMake가 찾은 설치 경로, PATH 순서이다. SDK와 호환되는 PyTorch·safetensors 환경이 필요하며 LoRA 이름 변환에는 기존 Diffusers 환경을 사용한다. Python 패키지를 자동 설치하거나 모델을 다운로드하지 않는다. 설치형 런처의 기본 가상 환경 또는 화면의 Python 실행 파일을 사용한다.

`Society.ModelMerge`는 실제 설치된 `iild-merge`를 실행하여 다음을 검사한다.

- 전체 파라미터 전달, 설정 검증의 무출력 동작, 공백·따옴표·셸 문자가 포함된 출력 경로.
- 공식 safetensors 직렬화로 만든 작은 체크포인트·LoRA에 대한 합·차와 자동·공통·개별 가중치의 독립 PyTorch 계산 비교, 원본 바이트와 정수 버퍼 보존.
- Diffusers 폴더 저장과 메타데이터, 호환되지 않는 텐서의 오류·무출력, 덮어쓰기 차단, 실행 취소.
- 컨테이너 내부 재귀 목록, 유형별 입력 역할·출력 경로, 동일 이름 구분, 패키지 단위 선택, 숨김·외부 링크 제외, 변경 없는 목록 보존과 컨테이너 교체 중 이전 조회 무시.
- LVRS 드롭다운의 클릭·우클릭, 긴 목록 스크롤과 키보드 선택, 새 모델 발견·삭제 시 선택 해제, 비어 있는 컨테이너와 작은 창의 메뉴 경계.
- 모델 선택에서 설정 검증·실제 병합 버튼 실행까지의 연결, 재료 추가·제거, 1440/760/360px와 최소 360×320 창 배치. `SOCIETY_MERGE_SCREENSHOT_PATH`와 `SOCIETY_MERGE_MENU_SCREENSHOT_PATH`로 테스트 데이터가 있는 실제 macOS 화면을 저장할 수 있다.

텐서 테스트에는 설치된 SDK의 Python 환경이 필요하다. 런타임이 없으면 해당 검사만 명시적으로 skip되며 경로·인자·QML 검증은 계속한다. `Society.Drive`는 Tools·Dashboard·Storage 왕복 시 병합 설정과 기존 폴더·프롬프트 보존을 검사한다. 모델의 미적 품질과 다중 GB 실사용 체크포인트의 실행 시간은 이 작은 텐서 검증이 보장하지 않는다.

모바일 Tools 탭도 동일한 `ModelMergeTool`을 표시한다. 좁은 화면은 16 px 여백과 한 열의 스크롤 폼을 사용하고 터치 입력·버튼의 높이를 확보한다. 플랫폼이 로컬 병합을 지원하지 않으면 기존 `ModelMergeController.supported`에 따라 실행을 비활성화하고 설명을 표시한다. 모바일용 병합 백엔드를 새로 제공하는 변경은 아니다. 설정은 다른 탭이나 화면 방향으로 이동해도 유지한다.
