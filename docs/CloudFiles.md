# Society 클라우드 Files

기본 iisacc 계정의 무료 공간은 십진수 5 GB, 즉 5,000,000,000바이트다.
기존 `Free` 멤버십을 포함하며 로컬 또는 S3에 5 GB를 미리 할당하지 않는다.
논리적 경로는 `Society.societycontainer/Files`다. 서비스 계약과 서버 쿼터 구현은
`Service/iisacc.com/docs/SOCIETY_FILES.md`에 정의한다.

Storage actions의 `Cloud Files · 5 GB free`에서 용량, 파일 목록, 진행 중 업로드를
조회한다. 사용자가 Files 내부 상대 경로를 입력하여 업로드하거나, 원격 항목의
현재 revision을 선택하여 로컬 파일로 교체한다. 다운로드는 기존 로컬 파일을
덮어쓰지 않고 같은 상대 경로로 저장한다. 상위 로컬 폴더는 미리 있어야 한다.
기존 사용자 파일을 자동으로 업로드하거나 동기화하지 않는다.

Main이 소유한 `CloudFilesController`는 SocietyView를 통해 StorageView에
전달한다. EnvironmentView에는 파일 전송 의존성을 추가하지 않는다.
모바일 PreferencesContent도 전송 컨트롤러를 소비하지 않는다.
`cloudFilesControllerReachesStorage`는 실제 Main을 로드하여 Storage가 같은
컨트롤러를 받는지 확인한다. 잘못된 화면에 없는 속성을 대입하면 앱 시작 자체가
실패하므로 패키지 실행·MCP 검사에서도 이 경계를 검증한다.

`CloudFilesController`는 계정 SDK의 검증된 세션을 빌려 같은 origin의 GraphQL을
호출한다. 계정·세션·컨테이너가 바뀌면 이전 콜백과 화면 상태를 폐기한다. S3
전송은 계정 cookie/bearer 없는 별도 전송기를 사용하며 HTTPS S3 주소만 허용하고
redirect를 따르지 않는다. 업로드는 8 MiB 단위로 읽고 SHA-256과 정확한 길이를
서명에 결합한다. 파일이 바뀌면 완료하지 않는다. 중단된 업로드는 서버에서
확인하여 취소하거나 이미 업로드된 부분을 완료할 수 있다.

로컬 경로는 `iiSocietyContainer::SharedStorage::filePath(Files, ...)`를 통해 검증한다.
다운로드는 같은 디렉터리에 임시 파일을 작성하고 iiFileProvider의
`File::publish(..., false)`로 기존 파일을 대체하지 않고 게시한다. 길이와 현재
경로를 다시 검사하고 관리 파일 기록은 작업 스레드에서 갱신한다. Models,
Photos, private application state는 이 전송의 대상이 아니다.

`Society.CloudFiles`는 임시 `.societycontainer`, loopback 계정 fixture 및 가짜
S3 전송기로 경로 격리, 8 MiB 분할, 세션 credential 비전달, 다운로드·기존 파일
보존을 검증한다. 실제 AWS 업로드, 설치 앱 교체, 배포 검증과는 구분한다.
현재 기존 AWS 로그인은 만료되어 실제 버킷 권한·보안 상태와 전송은 검증하지
못했다. 운영 배포, 새 유료 리소스, IAM 또는 lifecycle 변경은 별도 승인이 필요하다.

2026-10-26 로컬 검증: 최종 `Society.CloudFiles` 테스트 통과(15.12초),
정통 `build/bin/Society.app` 빌드·서명·512 MiB 번들 검사 통과.
기존 설치 앱을 교체하거나 실행하지 않았고 운영 배포도 하지 않았다.
서비스 검증은 Rails 81개, 기존 backend GraphQL 129개, frontend GraphQL
110개, Files HTTP 13개 조건이 통과했다. 실제 AWS 전송과 다른 플랫폼 빌드는
이번 실행에서 수행하지 않았다.
