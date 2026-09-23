# Environment desktop concept

2026-09-18 · Society / Environment의 LVRS 기반 Figma 시안이다. 앱 소스 구현과 실제 기기·서버·설치 상태 변경은 이 작업에 포함하지 않는다.

## 네 화면

| 화면 | 역할 | Figma |
| --- | --- | --- |
| Devices & hosting | 기기 4대의 상태, 기본 호스트, 이 기기의 설치 앱을 요약하고 상세 화면으로 이동한다. | [관리 개요](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=169-2308) |
| Devices | 기기 카드와 선택 기기의 역할·호스트·앱·자동 동기화 정보를 함께 표시한다. | [디바이스](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=169-2349) |
| Hosting | 기본 호스트, 컨테이너, 로컬 연결·자체 서버, 연결된 클라이언트와 동기화 상태를 표시한다. | [호스팅 정보](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=169-2390) |
| Apps | 선택 기기의 설치 앱, 업데이트 가능 앱, 설치 가능한 앱, 설치 진행 영역과 앱 상세 정보를 표시한다. | [앱 목록](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=169-2431) |

## 정보와 기능

- 공통 탐색은 Overview, Devices, Hosting, Apps이다. 기존 상단 Dashboard → Tools → Storage → Browse → Environment 순서를 유지한다.
- 같은 iisacc 계정의 기기를 관리한다. 데스크톱은 호스트·클라이언트 역할을 가질 수 있으며 모바일은 클라이언트이다.
- 예시의 현재 기기는 MacBook Pro 클라이언트, 기본 호스트는 Studio Mac이다. iPad는 온라인 클라이언트, iPhone은 오프라인 클라이언트로 표시한다.
- 기기 카드의 용량은 사용량/전체 용량과 남은 용량을 수치로 표시한다. 오프라인 기기는 마지막 확인 시각을 표시한다.
- 호스팅 정보는 Society 컨테이너를 제공하는 호스트를 대상으로 한다. 로컬 네트워크 연결과 자체 호스팅 서버 경로를 함께 표시한다.
- 예시 서버 주소는 `wss://studio.example.com/society`이다. 실제 운영 주소가 아니다.
- 앱 목록은 선택한 기기를 기준으로 한다. Society와 Dreamscapes는 설치됨, Dreamscapes는 업데이트 가능, Vincent는 설치 가능 상태의 예시이다.
- 앱 검색, 기기 선택, 설치, 실행, 업데이트 확인, 자동 업데이트, 제거와 진행 목록을 관리 기능으로 정의하였다. 이 설치 관리 기능들은 이번 시안의 제안이며 구현 완료를 뜻하지 않는다.
- 프로토타입은 네 화면 사이의 이동을 제공한다. 검색·설치·업데이트·연결 설정 변경을 실행하지 않는다.
- 모든 기기명, 용량, 연결 상태, 동기화 시각과 앱 상태는 설명용 데이터이다.

## 구성

- 네 화면 모두 1440 × 900이다.
- 도구 모음 높이 56, 사이드바 너비 220, 본문 여백 24이다.
- 상세 화면은 가변 너비의 주 영역과 400 너비의 우측 상세 영역으로 구성한다.
- LVRS 원본 Device 카드, ListItem Navigation/Action/Toggle, PushButton, DropdownButton, TextField, LabelSegmentedControl과 아이콘을 인스턴스로 재사용한다.
- Pretendard 및 LVRS 입력의 기존 SF Pro 시스템 기호를 유지한다. 강조색은 Society의 녹색이며, 색상·간격·모서리·텍스트 스타일을 기존 라이브러리에 연결하였다.
- 기존 Dashboard, Tools, Storage의 캔버스는 변경하지 않았다. Environment 페이지에는 요청한 네 화면만 추가하였다.

## 근거와 검증

현행 `docs/SelfHosting.md`, `docs/Preferences.md`, `docs/Dashboard.md`, `README.md` 및 기존 Society / LVRS Figma 원본을 확인하였다. 현재 앱의 Environment는 설정 진입점이므로, 이 네 페이지는 후속 구현을 위한 새 화면 구성안이다.

1440 및 1280 너비에서 네 화면의 자동 배치를 검사하였다. 보이는 요소의 부모 영역 초과, 잘못된 글꼴, 미치환 예시 레이블, 검은색 텍스트 오류, 남은 작업 표시가 모두 0개이다. 화면 이동 40개의 목적지가 유효함을 확인하였다. 전체 화면과 주요 상세 영역의 렌더링을 확인하였다.

상태와 검증 기록은 `build/figma-environment/state.json`에 저장하였다.
