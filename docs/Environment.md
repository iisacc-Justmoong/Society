<a id="environment"></a>

# 환경

환경은 애플리케이션 기본 설정과 별개인 지속적인 기본 페이지이다. 아래 6개 프레임은 기존 환경 화면의 기준이며, 데스크톱 상단 Apps의 새 화면과 설치 동작은 [Apps 네이티브 화면](NativeApps.md)을 따른다.

- 프레임: 252:954; 페이지: 개요, 호스팅 및 동기화
- 프레임: 252:1324; 페이지: 데스크톱/태블릿/스마트폰으로 그룹화된 장치
- 프레임: 252:2676; 페이지: 앱 / 내 앱
- 프레임: 263:5417; 페이지: 앱 / 모든 앱
- 프레임: 270:5384; 페이지: 앱 / 기타 장치
- 프레임: 270:5472; 페이지: 앱/웹 앱

원본  220px 탐색,  320px 계정 패널,  24px 콘텐츠 인셋,  64px 문자 배지 및  286px 카드들은 설치된  LVRS 라이브러리를 사용하여 구현됩니다. 카드들은  2 로 또는 한 열로 다시 흐름을 조정합니다. 1200px 아래에는 툴바 계정 버튼을 통해 계정 패널이 사용 가능하며,  760px 아래에는 섹션 탐색이 인라인 탭 바로 바뀝니다. 모바일 하단 레이블은 Env 로 줄어듭니다. 440px 아래에는 접근성 이름이 환경으로 유지됩니다. 앱 컬렉션 탭은 필요에 따라 가로로 스크롤합니다. 환경 탐색을 위한  Figma SVG 들은 로컬에 포함됩니다. 프로필 아바타는 계정 데이터이며, 디자인에 표시된 예시 아바타가 아닙니다.

<a id="navigation-and-actions"></a>

## 탐색 및 작업

환경 섹션 탭과 앱 컬렉션 탭은 LVRS의 필수 `iconName` 계약을 따른다. 각 탭은 레이블 왼쪽에 아이콘 하나를 표시하며, 축소 내비게이션은 기존 `home`·`device`·`application` SVG를 재사용한다. 컬렉션의 외부 `iconSource`를 지정할 때도 `iconName`을 전달한다.

- 데스크톱 및 모바일 환경에서 페이지를 선택하세요. 둘 다 기본 설정을 열지 않습니다.
- 환경설정은 애플리케이션 메뉴, Command-comma 및 호스팅 설정을 통해 계속 사용할 수 있습니다. 모바일 환경설정 시트의 제목은 “Preferences”입니다.
- Browse는 연결된 호스트 파일(폴더 탐색, 다운로드, 페이지 매기)의 메인 페이지이다. 기존 기기 경로에서 열며 데스크톱 상단 세그먼트에서는 Environment에 속한다.
- 장치 / 장치를 추가하면 기존 NetworkDevices 모달이 열립니다. 그것의 QR 동작이 PairingPanel를 엽니다. 수신된 페어링 초대와 온보딩은 기존 진입점 및 인증 검사를 유지합니다.
- 디바이스 세부 정보를 선택하고 세부 정보 섹션으로 스크롤하십시오. Browse는 선택한 호스트를 Browse에서 엽니다. 기존 NetworkDriveController를 사용하여 동기화, 자동 연결, 새로 고침 및 연결을 끊고, 사용할 수 없는 작업을 비활성화합니다.
- 데스크톱 상단 Apps는 환경 공급자가 준비되면 LVUI All apps를 열고, 그 전에 다른 화면을 선택하면 대기 중인 이동을 취소한다. 검색과 선택은 현재 컬렉션에 적용된다. 상세와 설치 상태의 범위는 [Apps 네이티브 화면](NativeApps.md)에 정의한다.
- 기존 환경 화면은 HTTPS 웹 바로가기를 URL로 추가·중복 제거해 EnvironmentWebApps 설정에 저장한다. 네이티브 Apps는 기존 바로가기를 읽어 열며, 추가 화면 연결은 아직 남아 있다.

<a id="data-boundaries"></a>

## 데이터 경계

장치 행은 기존 계정 범위 StorageNavigation 스냅샷과 현재 장치에서 나옵니다. 계정 ID와 아바타는 AccountController에서 나옵니다. 데모 장치 수, 저장소 크기, 라이센스 부여 및 소셜 수는 실제 데이터로 주장되지 않습니다. 누락된 소셜 개수는 엠 대시를 사용합니다.

EnvironmentAppsModel 는  Society 자체와 설치된  Dreamscapes / Congregation 번들을  macOS 에서 식별합니다. 일반 제품 이름 (노트, 모션, 스튜디오 등) 은 동일한 이름의 관련 없는 애플리케이션과 매칭되지 않습니다. 9   Figma 카탈로그 항목들은 설명과 시각적 정체성을 유지합니다. 인증된 릴리스 제공자가 없는 경우 설치가 비활성화되며 라이선스는 명시적으로 미검증입니다. 다른 플랫폼은 현재 Society 만 식별합니다. 원격 앱 재고는 별도의 계정 범용 입력이며 연결된 장치 만으로는 특정 앱이 설치되어 있다는 것을 함의하지 않습니다. 계정 웹 앱은 구성된 계정 서비스를 사용합니다. 추가 웹 앱은 발명된 서비스 엔드포인트가 아닌 사용자 제공 URL 입니다.

Windows의 앱 목록은 `resources/distribution/windows-store.json`에서 게시가 확인된 제품의 Microsoft Store 직접 링크를 읽어 설치 동작으로 엽니다. 링크는 HTTPS의 Microsoft Store 제품 경로만 허용하며, 링크 제공이 설치 상태나 계정 라이선스의 확인을 뜻하지는 않습니다. Dreamscapes와 Congregation은 비검색 직접 링크로 배포하도록 준비하지만 현재 Store 심사 전이므로 두 항목의 `published`는 `false`이고 설치 동작도 비활성화됩니다. 각 제품이 인증·게시된 뒤에만 해당 항목을 활성화하여 Society를 다시 빌드합니다. Society 자체는 기존 열기 동작을 유지합니다.

Apple Silicon macOS의 네이티브 Apps는 `resources/distribution/macos-arm64.json`에 게시된 Congregation과 Dreamscapes의 버전, 최소 macOS 버전, 크기, SHA-256 및 주소를 사용한다. 미설치 제품의 Install은 다운로드와 저장본·패키지 검증 뒤 시스템 설치기를 연다. 기존 환경 화면의 Download 및 설치된 제품의 별도 설치 파일 항목은 브라우저 다운로드를 유지한다. 설치기 인계는 완료나 라이선스 소유를 뜻하지 않는다. 허용된 호스트와 제품·플랫폼·버전이 일치하는 HTTPS 패키지 경로만 사용한다.

네이티브 Install은 저장본의 SHA-256과 크기를 확인하며, 브라우저의 일반 다운로드는 이 검사를 수행하지 않는다. 설치된 제품의 설치 파일 항목은 All apps의 항목 수에 별도로 포함된다. `Society.EnvironmentDistribution`과 `Society.EnvironmentStore`는 기존 다운로드 및 Windows Store 경로를 확인한다. 새 설치의 검증 범위와 제한은 [Apps 네이티브 화면](NativeApps.md)을 따른다.

<a id="verification"></a>

## 검증

`SocietyDriveTests environmentPagesPairingAndResponsiveLayout` 는 1440, 1024, 760, 390 및 320px에서 페이지 라우팅, 장치 추가, 브라우징 격리, 컬렉션 전환, 검색, HTTPS 검증 및 데스크톱/모바일 레이아웃을 확인합니다. `environmentCatalogueDoesNotInventInstallationsOrLicenses` 는 알 수 없는 앱에 대한 실제 카탈로그 상태와 출시 거부를 확인합니다. 기존 데스크톱 대시보드, 모바일 내비게이션, 선호도 및 페어링 회귀 사항은 여전히 검증의 일부입니다. `SOCIETY_ENVIRONMENT_CAPTURE_DIR` 를 `build/` 하위의 경로로 설정하여 실제 개요 및 모든 6 채워진 디자인 픽스처 를 포착합니다. 픽스처 스크린샷은 시각적 확인이며 라이브 앱 라이선스 또는 원격 재고의 증거가 아닙니다.
