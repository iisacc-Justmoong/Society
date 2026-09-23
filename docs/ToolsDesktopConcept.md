# Tools desktop concept

2026-09-18 · Figma 시안 및 기능 제안. 후속 구현은 모델 병합 상세 본문 121:752에 한정한다. 도구 카탈로그의 확장안은 구현 완료를 뜻하지 않는다.

## 산출물

- [도구 목록 · 1440 × 900](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=115-454)
- [Model merge 상세 · 1440 × 900](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=121-728)
- [설계 설명](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=127-2482)
- [ToolCard 컴포넌트](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=118-2085)
- 검토 이미지와 상태 기록: `build/figma-tools-desktop/`

## 제품 방향

Society의 자산 보관·준비·전달 역할을 기준으로 도구를 정의한다.
현재 소스의 Tools 목록에는 Model merge 한 종류가 있다. 나머지 다섯 도구는 이 시안의 확장 제안이다.
이미지 생성은 Dreamscapes 등의 소비 앱에 두며 Society의 기기 동기화 역할과 구분한다.

| 도구 | 상태 | 입력과 기능 | 산출물 및 경계 |
| --- | --- | --- | --- |
| Model merge | 기존 기능 | Models에서 베이스 체크포인트와 추가 체크포인트·LoRA를 선택한다. Weighted sum / Weighted difference, 자동·공통·개별 가중치를 설정하고 입력을 검사한다. | 명시적으로 입력한 이름의 새 단일 .safetensors. 원본과 기존 출력을 덮어쓰지 않는다. |
| Model inspector | 독립 도구 제안 | 모델 구조·형식·메타데이터를 표시하고 대상 모델/앱과의 호환성을 검사한다. 기존 SDK의 검사 기능을 활용할 수 있다. | 호환성 및 준비 상태 보고서. 확인되지 않은 아키텍처는 추정으로 성공 처리하지 않는다. |
| Batch rename | 신규 제안 | 선택한 파일에 문자열 치환, 접두·접미사, 연속 번호를 적용하고 변경 전후 이름을 미리 본다. | 사용자가 확인한 이름 변경. 충돌·허용되지 않는 이름을 실행 전에 차단하며 보호된 루트 폴더는 대상에서 제외한다. |
| Export assets | 신규 제안 | 선택한 이미지의 포맷과 크기를 지정한다. 첫 범위는 이미지로 한정한다. | 원본을 보존하는 별도 출력 파일. 이미지 생성 기능이 아니다. |
| Build a package | 신규 제안 | 관련 파일을 직접 선택하고 상대 경로와 파일 목록을 유지한 채 전달 묶음을 구성한다. | 파일·매니페스트를 포함한 패키지. 외부 업로드나 자동 게시를 포함하지 않는다. |
| Integrity check | 신규 제안 | 로컬 자산의 누락·손상·참조 문제를 검사한다. 신뢰할 수 있는 기준 해시가 있는 경우에만 해시 일치를 판정한다. | 검사 보고서. 자동 삭제·재분류·복구를 수행하지 않는다. 기준 정보가 없으면 확인할 수 없는 항목으로 구분한다. |

## 화면과 동작

- 기존 창 도구 모음과 Dashboard → Tools → Storage → Browse → Environment 순서를 유지한다.
- 좌측 탐색은 All tools, Pinned, Models, Files, Packages, Storage, Recent activity, Saved presets로 구성한다.
- 도구 검색은 이름·설명·범주를 대상으로 한다. 상단 Search Society는 제품 전체 검색이다.
- 도구 카드는 3열 × 2행이다. 제목, 설명, 범주, 아이콘을 컴포넌트 속성으로 제공한다.
- Pinned, Saved presets, Recent activity는 공통 기능 제안이다. 실제 기능 구현 여부와 분리한다.
- Model merge 카드와 상세 화면의 All tools 버튼에 양방향 화면 이동을 연결했다.
- 상세 화면은 Input models / Merge settings / Output / Input check로 구분한다.
- 입력은 현재 컨테이너의 Models 목록에서 선택한다. 출력 이름은 필수이며 이름이 입력된 상태를 시안으로 표현했다.
- 기본 모드는 Weighted sum, 기본 가중치 방식은 Automatic이다. Advanced settings는 지원되는 캐시·실행기 설정을 담을 접힌 진입점이다.
- 실제 병합 실행 상태는 SDK가 제공하는 상태와 경과 시간을 사용한다. 추정 진행률을 새로 제안하지 않는다.
- 파일 작업은 사용자가 선택한 자산에 한정한다. 원격 자산의 로컬 준비 상태를 실행 전에 구분한다.

## 시각 구성

- 1440 × 900, 창 도구 모음 56 px, 사이드바 228 px, 본문 좌우 여백 32 px.
- 372 × 176 도구 카드, 카드 사이 16 px, 본문 영역 사이 24 px.
- LVRS Card.Model의 표면·테두리, ListItem, PushButton, TextField, LabelSegmentedControl 및 기존 창 컨트롤을 재사용한다.
- ToolCard의 기본·마우스 올림 2개 상태를 만들고 120 ms 전환을 설정했다.
- Pretendard 텍스트 스타일과 Society Green #57965C를 사용한다. LVRS 입력 내부 시스템 기호는 기존 글꼴을 유지한다.
- 모델명, 파일명, 최근 작업 시각·건수와 고정 도구 수는 설명용 데이터이다. 사용자 계정의 실제 활동이 아니다.

## 확인 근거

현행 `docs/Tools.md`, `docs/ModelMerge.md`, `docs/Files.md`, `src/App/Tools/ToolsView.qml`,
`src/App/Tools/ToolCard.qml`, `src/App/Tools/ModelMergeTool.qml` 및 기존 Figma Storage 화면을 확인했다.
Figma 디자인에는 현재 소스 계약과 신규 제안을 구분해 기록했다.

두 화면과 설명 보드의 렌더링을 확인했다. 자동 배치 컨테이너 밖으로 벗어나는 요소는 0개이며,
도구 카드 6개, 상태 2개, 화면 이동 목적지와 텍스트·아이콘 속성을 읽어 검증했다.
Figma 프로토타입은 화면 이동 예시이며 실제 모델 처리·검색·파일 작업을 수행하지 않는다.

최초 시안 제작 단계의 저장소 변경은 이 설계 문서뿐이었다. 해당 단계에는 앱 소스 수정과 빌드·테스트·재설치를 수행하지 않았다. 후속 구현 범위는 다음 절과 같다.

## 모델 병합 본문 구현

후속 요청으로 121:752의 Model merge 본문을 기존 앱에 적용했다. 입력·설정·출력·검사 패널, 본문 내 All tools, 접힌 고급 설정과 좁은 창 한 열 배치를 제공한다. 기존 SDK 실행과 원본 보존 계약은 유지한다. 도구 카탈로그, 제안한 5개 신규 도구와 사이드바는 구현 범위에 포함하지 않는다. 상세 계약과 검증은 [ModelMerge.md](ModelMerge.md)를 참조한다. 재설치는 수행하지 않는다.
