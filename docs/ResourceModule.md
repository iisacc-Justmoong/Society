# 공용 iiResource 모델과 Society 저장소 연결

`iiResource`는 Resource, Content Revision, Location, Replica의 독립적인 식별과
연결을 표현하는 C++23 SDK다. 해시와 경로는 논리 자원 ID를 대체하지 않는다.
Society는 이 SDK와 iiSocietyContainer를 각각 소비하고 앱 계층의
`ContainerResourceAdapter`로 기존 `FileRepository` 관찰 결과를 변환한다.
두 SDK는 서로 의존하지 않는다. 파일 I/O와 저장·이력 관리 책임은 기존
iiFileProvider 및 iiSocietyContainer에 남는다.

어댑터는 기존 자원 ID와 마지막으로 관찰된 내용 판본을 각각의 공용 타입으로
보존한다. 파일 이름과 경로는 표시값이며 실제 저장 locator는 별도의 Location에
담는다. 기존 저장소가 안정적인 Location ID를 제공하지 않으면 비워 둔다.
기존 복제 기록은 자원·판본과 위치 힌트를 보존하지만, 원격 확인을 수행하지
않았으므로 Replica의 상태는 `unknown`이다. 저장된 해시는 과거 관찰값이며
이 조회만으로 현재 파일이나 원격 사본이 검증되었다고 주장하지 않는다.

기존 `ResourceRecord`는 iiSocietyContainer의 호환성 투영이며 새 공용 모델의
정본이 아니다. 저장된 경로나 메타데이터를 이 변경만으로 다시 쓰지 않는다.
Project의 기존 `files-catalog-v1` 참조는 기존 Catalog 기반 식별자를 유지하고
`iiResource::ResourceId` 타입으로 노출한다. 이는 저장소 ObjectStore에서
관찰한 자원 ID와 같은 값이라고 자동 가정하지 않는다. 두 식별 체계의 전역
연결·기존 데이터 이전은 이 변경의 범위가 아니다.

`society-cli resource inspect Files/PATH [--container DIR]` 또는 `Models/PATH`, Python
`Client.resource_inspect(path, container=...)`, 데몬 MCP `resource_inspect`는
선택한 컨테이너의 마지막 관찰 스냅샷을 읽는다. GUI가 닫혀 있어도 조회할 수
있으며 파일 내용이나 이력을 수정하지 않는다. 조회 시 기록된 파일시스템 관찰값과
현재 파일의 식별 정보를 비교한다. 외부 변경·교체 등으로 일치하지 않거나 기록이
없으면 같은 경로라는 이유로 자원을 연결하지 않고 재색인이 필요하다고 알린다.
원격 연결 정보는 여전히 오래되었을 수 있다. 안정적 자원 ID만으로
전체 기기에서 조회하거나, 이 조회가 복제·판본 확정·접근 권한을 보장하지 않는다.

Society의 Suggested 경로와 프로젝트 자원 참조가 공용 타입을 사용한다. 새 SDK의
`ResourceSource`는 저장소 제공자가 구현할 수 있는 인터페이스이며, 이 변경은
모든 기존 Society 작업을 그 인터페이스로 이전했다고 주장하지 않는다. 격리된
테스트는 기존 파일의 이름 변경 전후 자원·판본 보존, 미확인 복제 상태와
CLI·Python·MCP 조회, 다른 자원으로 잘못 연결되는 경우를 검사한다.

## 동일 내용 후보 조회 계약

선택한 컨테이너의 `Files` 또는 `Models`에 있는 관찰된 파일 하나를 기준으로
같은 SHA-256 관찰값을 가진 관리 자원을 페이지 단위로 찾는다. 결과는 서로 다른
자원 ID와 각 관찰 판본을 보존한다. 기준 파일과 후보마다 기록된 파일시스템 관찰값을
현재 항목과 비교하며, 검색에서 전달된 행 중 확인할 수 없는 후보는 결과에서 빼고
`skipped`에 센다. SDK 검색이 앞서 제외한 항목은 이 수에 포함되지 않는다. 기준
파일을 확인할 수 없으면 조회를 거절한다. 다음 페이지는 컨테이너, 기준 자원·판본과
색인 cursor에 묶이며, 기준 또는 색인이 바뀌면 처음부터 다시 조회해야 한다.

이 조회는 마지막 관찰의 해시를 비교한다. 현재 바이트의 일치, 원본의 유일성,
삭제 가능성 또는 복구 가능성을 증명하지 않는다. 색인되지 않은 파일, 다른
컨테이너·기기, 공개 범위 밖의 항목과 미지원 형식이 빠질 수 있으므로 빈 결과도
전체에 중복이 없다는 뜻이 아니다. 조회는 파일·자원 관계를 수정하지 않으며,
물리적 중복 제거와 유사 콘텐츠 연결은 별도의 기능이다. CLI, Python, MCP와
Workspace 에이전트는 같은 범위와 한계를 반환해야 한다.

`society-cli resource duplicates Files/PATH --limit 5 --container DIR`, Python의
`Client.resource_duplicates(path, limit=5, cursor=..., container=...)`, 데몬 MCP와
Workspace 에이전트의 `resource_duplicate_candidates`로 조회한다. `limit`은 반환
후보 수가 아니라 읽는 색인 페이지의 최대 행 수이며 1부터 20까지이다. 따라서
후보가 없는 페이지에도 다음 cursor가 있을 수 있다. 응답의 `next`가 빈 객체이면
해당 검색의 마지막 페이지이고, 그렇지 않으면 그대로 다음 요청에 전달한다.
`evidence`는 `matching_last_observed_sha256`, `completeness`는 `unknown`이다.

## 관찰 시 저장소 열기

리소스 관찰, 마지막 관찰 해시의 중복 후보, 색인 검색은 `SharedStorage::inspect`로 이미 존재하는 컨테이너를 연다. 이 경로는 레거시 레이아웃을 자동 이관하거나 유지 관리 감시를 새로 등록하지 않는다. 이관이 필요한 상태나 준비되지 않은 저장소에서는 결과 대신 오류를 반환한다. 다른 작업이 이미 시작한 유지 관리는 별도로 계속될 수 있다.

## 제한된 원본 바이트 대조 계약

두 개의 `Files` 또는 `Models` 파일에 대해 현재 관찰된 정체성을 확인하고 SDK가
각각 임시로 확보한 스냅샷을 바이트 단위로 비교한다. 개별 입력 크기는 64 MiB로
제한하며 이 비교의 동시 실행은 데몬에서 한 건으로 제한한다. 원본의 내용과 분류·관계
기록은 수정하지 않고 임시 스냅샷을 사용 후 정리한다. 두 스냅샷은 별도의 시점에
확보되므로 원자적 공동 시점이나 이후 원본 상태를 보장하지 않는다. 운영체제에 따라 원본의 접근 시각은 갱신될 수 있다.

스냅샷 확보 전후에 파일과 컨테이너 관찰이 달라지거나, 파일의 안정적 확보가
불가능하거나, 관찰된 자원 정체성이 없으면 결과 대신 오류를 반환한다. 비교 결과는
두 스냅샷의 바이트가 같은지에 한정되며, 삭제·병합 권한이나 종속성의 부재,
복구 가능한 복제본이 있음을 의미하지 않는다.

## 대조 인터페이스와 임시 데이터

`society-cli resource compare-bytes Files/A Models/B --container DIR`, Python의 `Client.resource_compare_bytes(first_path, second_path, container=...)`, 데몬 MCP와 Workspace 에이전트의 `resource_compare_bytes`로 사용한다. 응답의 `comparison`은 `equal` 또는 `different`, `evidence`는 `separate_temporary_snapshots`, `snapshot`은 `non_atomic`이며 `deduplicationAuthorized`는 항상 거짓이다. 두 파일은 각각 관찰된 별도 자원이어야 한다.

스냅샷은 시스템 임시 디렉터리의 비공개 하위 디렉터리에 만들고 정상 종료 또는 오류 시 정리한다. 클라이언트 연결이 끊기거나 시간이 초과되어도 데몬의 작업은 끝날 때까지 계속될 수 있다. 프로세스가 강제로 종료되면 임시 자료가 남을 수 있으며, 이 자료는 복구용 복제본이 아니다. Python 기본 제한 시간은 15초이므로 큰 파일이나 느린 저장소에서는 `Client(timeout=120)`처럼 호출 측에서 늘릴 수 있다.

macOS의 현재 관리 저장소는 파일 등록에 실제 경로와 같은 유니코드 정규화 표기를 요구할 수 있다. 이 비교는 요청 경로를 파일 시스템에서 확인한 뒤 실제 경로 표기에 연결된 관찰 기록을 찾는다. 해당 기록을 찾지 못하면 미확인 오류로 남기며, 같은 자원이라고 임의로 합치지 않는다. 기존의 다른 자원 조회 경로가 모든 정규화 형태를 지원한다는 뜻은 아니다.
