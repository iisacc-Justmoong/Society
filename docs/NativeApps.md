# Apps 네이티브 화면과 설치

데스크톱 상단 Apps는 [All apps](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=169-2431), [My apps](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=1134-471), [Web apps](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=1134-538), [Extension](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=1134-404), [Elements](https://www.figma.com/design/vzGhdYpJ2GeyNfwXADJeAu/Society?node-id=1257-1204)를 기준으로 LVUI와 iiGUI에서 구성한다. 기존 창 위에 놓는 호스트는 입력과 그리기 명령을 전달하고 `NativeAppsViewModel`이 선택, 검색, 컬렉션, 상세와 진행 표시를 관리한다. 좁은 화면에서는 컬렉션을 가로로 스크롤할 수 있다.

All apps는 기존 `EnvironmentAppsModel`이 관찰한 이 기기의 다른 앱과 게시된 배포 동작을 표시한다. 디자인용 예시 제품은 설치나 배포 동작이 확인되지 않으면 표시하지 않는다. My apps는 계정 전체의 소유권이 아니라 이 기기에서 관찰한 설치 항목이다. Web apps는 기존 환경의 계정 및 사용자 HTTPS 바로가기를 읽어 열며, 새 바로가기 추가 화면은 아직 이 네이티브 뷰에 연결되지 않았다. Extension과 Elements에는 연결된 공급자가 없어 그 사실을 표시한다. 업데이트 확인, 자동 업데이트, 기기 전환, 제거와 저장소 접근 설정은 아직 연결되지 않았다.

Apple Silicon macOS에서 미설치 Congregation 또는 Dreamscapes의 Install은 패키지 정보를 `resources/distribution/macos-arm64.json`의 검증된 항목에서 받는다. iiNetwork로 허용된 HTTPS 주소에서 크기가 제한된 파일을 받고, 저장된 파일을 다시 읽어 정확한 크기와 SHA-256을 검사한다. iiUpdateManager의 설치기 검증에 제품·버전·서명 및 시스템 정책 확인을 맡긴 다음, 대상 앱이 이미 존재하지 않는지 다시 확인하고 macOS 시스템 설치기를 연다. 해당 조건을 통과하지 못하면 다음 단계를 실행하지 않는다. 설치된 동일 버전 SDK가 최신 공개 패키지 식별자보다 오래되었을 수 있으므로 빌드는 Workspace의 현재 `SDK/iiUpdateManager/Native` 소스를 사용한다.

설치기 인계는 설치 성공이 아니다. 사용자는 시스템 설치기에서 설치를 완료해야 하며 Society는 다시 활성화될 때 기존 카탈로그를 새로 읽는다. 설치 판별은 현재 `/Applications`와 사용자의 `Applications`에서 알려진 번들 경로를 관찰하는 범위이다. 인계된 검증 파일은 설치기가 나중에 읽을 수 있도록 전용 캐시에 남는다. 이미 설치된 앱은 Open을 유지하며 별도 설치 파일 항목의 Download는 기존 브라우저 다운로드 경로를 사용한다. 실행 중 앱의 업데이트·롤백에는 아직 독립 설치 조정자가 없고 이 첫 설치 경로는 이를 대신하지 않는다.

Windows는 게시된 Microsoft Store 항목만 기존 Store 경로로 연다. 현재 배포 목록의 두 Windows 항목은 미게시이며 다른 플랫폼의 직접 설치 제공자는 연결되지 않았다. 카탈로그 또는 설치기의 이용 가능성은 라이선스 소유를 뜻하지 않는다.

`Society.NativeAppsViewModel`, `Society.NativeAppsView`, `Society.NativeAppsSurface`, `Society.InstallRelease`, `Society.InstallArtifact`, `Society.InstallWorkflow`, `Society.InstallPackageIdentity`는 화면 상태와 레이아웃, 기존 셸·공급자 연결, 배포 정보, 저장본 검증, 단계별 실패 차단과 SDK의 공개 패키지 식별자 규칙을 자동 검사한다. 설치기와 외부 배포 서비스는 이 테스트에서 실행하지 않는다. 실제 OS 창의 시각적 일치, 실제 다운로드와 시스템 설치 완료 및 다른 플랫폼 동작은 별도 검증이 필요하다. 2026-11-26 macOS 빌드에서는 Society 실행 파일과 Apps·설치 관련 대상이 빌드되고 이들의 자동 검사가 통과했다. 전체 빌드는 현재 iiSocietyContainer 통합 헤더가 코어 소비자에게 `ModelStore` 선언을 노출하지 않아 기존 모델 카탈로그와 모델 가져오기 대상에서 중단되었다.
