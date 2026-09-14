# 로컬 MCP 앱 제어

데스크톱 POSIX 빌드는 iiLocalLLM 0.10.0으로 실행 중인 Society의 MCP 서버를 제공한다. 서버는 같은 기기의 `127.0.0.1`에만 바인딩하고 앱을 시작할 때마다 생성한 비밀 토큰으로 인증한다. 기존 Society 동기화와 별개의 로컬 제어 경로이다.

| 도구 | 동작 |
|---|---|
| `status` | 실제 DriveController의 컨테이너 ID·루트·현재 경로·섹션·동기화 가용 상태 |
| `open_section(key)` | status에서 반환한 섹션을 앱에서 연다 |
| `navigate(path)` | 현재 컨테이너 내부의 절대 디렉터리로 이동한다 |
| `refresh` | 현재 드라이브를 디스크에서 갱신한다 |
| `list_entries(limit=100)` | 현재 디렉터리의 이름·경로·종류를 최대 200개 반환한다. 파일 내용을 읽지 않는다 |

입력은 JSON Schema로 검증한다. 컨트롤러 호출은 Qt 주 스레드로 전달하고, 목록의 디렉터리 순회는 작업 스레드에서 수행한다. 목록은 한 번의 탐색 상태를 기준으로 하며 그 후 파일 시스템 변경까지 고정하는 snapshot은 아니다. 타임아웃이나 연결 단절 뒤에는 status를 조회하여 실제 상태를 확인한다. 생성·삭제·동기화 설정 변경·앱 시작 기능은 제공하지 않는다.

앱은 `IILOCALLLM_APP_ENDPOINTS` 절대 경로 또는 Qt GenericDataLocation의 `iisacc/AgentEndpoints` 폴더에 등록한다. 현재 사용자 소유의 0700 폴더와 0600 파일만 사용한다. `IILOCALLLM_DISABLE_APP_MCP=1`이면 서버를 열지 않는다. 같은 사용자 권한의 프로그램은 토큰을 읽을 수 있으며 이 등록은 코드 서명 검증이 아니다. 등록 폴더를 에이전트가 수정하는 workspace 안에 두지 않는다. 토큰을 로그나 모델 프롬프트에 전달하지 않는다.

iiLocalLLMD의 agent API와 iillm-mcp는 실행 중인 앱을 자동 발견한다. 호스트의 도구 권한은 별도로 적용하며, `agent.mcp.status`의 `source: local_application`과 `app_id: com.iisacc.society`로 연결을 확인한다. 개별 인스턴스 이름은 재시작 때 달라진다. 모바일 및 Windows에는 이 listener를 추가하지 않는다.

`Society.Mcp`는 실제 Society 실행 파일을 별도 저장소·helper·등록 폴더에서 시작한다. 컨테이너 ID, 탐색, 바깥 경로 거절, 제한된 목록, 인증 실패, iiLocalLLM 자동 발견/제거, 비활성 설정을 확인한다. 로그는 테스트 build의 `mcp-society-*`에 남는다. 이는 사용자의 기존 저장소나 물리 디바이스 UI 검증을 대체하지 않는다.

`SOCIETY_MCP_TEST_GGUF`에 SDK 검증용 Qwen2.5 0.5B Q4_K_M 경로를 지정하면 같은 테스트에서 네이티브 추론도 실행한다. 발견한 status 도구 하나를 eager로 제공하고 명시적으로 허용한 뒤, 모델이 실제 도구를 호출하고 새 컨테이너 ID를 응답에서 소비했는지 확인한다. ID는 모델 프롬프트에 넣지 않는다. 여러 도구의 자율 선택이나 ToolSearch 후 연속 실행의 증거로 확대하지 않는다.
