# 소비 앱의 로컬 이미지 생성

Society는 Society끼리 컨테이너를 동기화한다. 이미지 생성은 모델을 소비하는 각 기기의 Dreamscapes가 담당한다.

데스크톱 Society의 `Models/` → iiSocietySync → 아이폰 Society의 준비된 App Group 컨테이너 → iiSocietyHelper/SharedStorage → 아이폰 Dreamscapes의 로컬 iiLocalDiffusion 순서이다. Dreamscapes에는 호스트 주소·페어링 링크·네트워크 모델 목록·원격 생성 요청을 전달하지 않는다.

`GenerationBridge`와 호스트의 Dreamscapes worker 실행 경로를 제거했다. 수동 페어링 링크는 Society 클라이언트를 연결할 때만 사용한다. 전체 컨테이너 동기화에는 기존 계정 인증이 필요하고 수동 QR만으로 Models 접근 권한을 주지 않는다.

Dreamscapes는 생성 중 파일을 앱 임시 저장소에 두고 완성 PNG만 로컬 `Generation History/`에 게시한다. 이 파일을 다른 기기로 복제하는 책임은 Society의 다음 동기화에 있다. 오프라인 생성도 이미 내려받은 모델과 해당 기기의 추론 엔진으로 수행한다.

검증은 [Dreamscapes.LocalSociety](../../Dreamscapes/App/Generation/tst_LocalSociety.cpp), Society 네트워크 테스트, iOS 번들 검사를 함께 사용한다. 작은 fixture의 생성은 저장 계약만 검사하며 실제 모델 추론은 별도이다. 과거 Workspace `build/iphone-remote-generation/`의 PNG 수신 기록은 아이폰 자체 추론 성공을 뜻하지 않는다.
