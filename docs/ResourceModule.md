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
