<a id="tools"></a>

# 도구

기존 Tools 화면은 데스크톱 상단의 두 번째 **Tasks** 세그먼트로 연다. Tasks의 개편안이 결정되기 전까지 같은 Tools 화면과 도구를 사용하며, View → Tools 메뉴와 단축키, 모바일 Tools 탭도 유지한다. 화면은 최상단에 **QuickGenerate**를 표시하고 그 아래에 도구 제목·검색과 **Model merge**, **Model Packaging**, **Model calibrator** 카드를 제공한다. QuickGenerate는 대시보드에서 이동했으며 입력·선택 상태와 기존 생성 요청 동작을 유지한다. 이름·설명·종류 검색과 All tools, 뒤로 가기 단축키, 모바일 왼쪽 가장자리 제스처를 지원한다.

카드는 LVRS Card를 사용하며 넓은 화면에서는 최대 너비 856 px의 두 열, 좁은 화면에서는 한 열로 배치한다. 목록 복귀나 Dashboard·Storage 탭 전환으로 모델 선택·가중치·출력 설정과 실행 중 작업이 초기화되지 않는다.

모델 병합은 기존 iiLocalDiffusion 실행기와 파라미터를 사용한다. 지원 형식·실행 조건·결과 저장은 [모델 병합](ModelMerge.md)을 참조한다. 추가 라이브러리나 서비스 의존성은 없다.

Society.Drive는 도구 카드, 기존 도구의 실제 카드 클릭·목록 복귀·탭 전환 후 설정 유지와 모바일 화면 크기별 배치를 검사한다. Society.TasksNavigation은 세 카드의 순서와 교정기 검색을 확인한다. Society.ModelMerge는 기존 병합 실행을 검증한다. Society.ModelPackaging은 네이티브 패키징과 UI 연결을 검증한다. iOS UI 테스트도 모델 병합 카드로 진입하고 목록에 돌아오는 흐름을 사용한다.

Model Packaging은 [Figma 297:511](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=297-511)에 따라 구현하였다. Model Merge와 공유하는 목록에서 Society 드라이브의 모델을 고르거나 외부 폴더를 선택할 수 있다. 선택 구성요소, 실측 진행과 취소, 단일 Safetensors 저장·재검증은 [모델 패키징](ModelPackaging.md)을 참조한다.

Model calibrator는 [모델 교정기](ModelCalibration.md)에서 지원 범위와 제한을 확인한다. macOS에서는 별도 LVUI·iiGUI 네이티브 창을 열며, 다른 장치에서 실행 파일을 찾지 못하면 오류를 표시한다.

모델 병합 상세 본문은 [Figma 121:752](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=121-752)의 카드형 두 열 배치로 교체했다. All tools 버튼도 상세 본문에 포함한다. 이 교체는 Tools 목록의 다른 신규 도구나 시안의 사이드바를 추가하지 않는다.
