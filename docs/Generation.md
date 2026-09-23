# 호스트 우선 이미지 생성

클라이언트가 모델 원본을 아직 보유하지 않았으면 `society.generation` 요청을 기존 인증 연결에서 처리한다. NetworkDriveController는 계정·페어링·호스트 역할을 확인하고 iiSocietyGeneration::Host에 위임한다. 실제 큐·모델 참조 검증·iiLocalDiffusion 실행·결과 청크 전송은 SDK 책임이다. 모바일 Society는 호스트 추론을 제공하지 않는다.

요청 접수와 모델 복제는 독립적이다. Dreamscapes가 접수 확인 후 StorageMap 다운로드 요청을 남기므로 복제 완료 전에도 결과를 받을 수 있다. 복제 요청은 생성 취소나 실패 후에도 유지한다. 호스트 임시 결과는 `Models/.society-runtime/iiSocietyGeneration/`에 두며 이 경로는 동기화 대상에서 제외된다. 클라이언트가 최종 이미지를 Generation History에 공개한 뒤 기존 Society 복제가 처리한다.

검증: SDK 프로토콜 테스트와 실제 loopback TLS의 Dreamscapes LocalSociety 통합 테스트가 모델 전송을 보류한 상태의 생성 완료, 후속 다운로드, 다운로드 완료 후 오프라인 로컬 생성을 확인한다. 주입 엔진의 테스트 결과와 실제 기기·모델 추론 증거는 별도로 취급한다.

Society의 동기화와 모델 소유권은 유지한다. Dreamscapes UI에는 별도 호스트 주소나 인증 설정을 추가하지 않는다. 수동 QR만으로 모델 사용 권한을 부여하지 않으며 계정 및 인증된 호스트 연결이 필요하다. 완전히 내려받은 모델과 리소스는 클라이언트의 기존 로컬 엔진으로 오프라인에서도 실행할 수 있다.

Generation History는 [공통 갤러리](Gallery.md)로 표시한다. 생성 중 파일은 Society의 기기 전용 runtime에 두고 검증된 완성 이미지만 공개한다.
