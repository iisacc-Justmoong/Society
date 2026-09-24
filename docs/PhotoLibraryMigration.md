# iiPhotoLibrary 소비 전환

2026-09-24에 Society의 사진 기능을 독립 SDK `iiPhotoLibrary 0.1` 소비 구조로 전환했다.

## 실행 경로

- 앱과 데몬이 공유하는 `NetworkDriveController`는 `iiPhotoLibrary::PhotoController`를 생성한다.
- 사진 저장·영구 인덱스·네이티브 보관함·원본 전송 코드는 설치된 SDK 정적 라이브러리에서 연결된다.
- `StorageView`는 `import iiPhotoLibrary 1.0 as Photos`를 사용한다.
- `cmake/PhotoLibrary.cmake`가 SDK의 qmldir와 Photos/Gallery QML을 `/qt/qml/iiPhotoLibrary`에 넣는다.
  앱·사진 테스트·Drive 통합 테스트가 같은 리소스 구성으로 실행된다.
- Android Activity는 SDK Java 클래스에 권한 및 휴지통 결과를 전달한다.
  패키징은 SDK Java 파일을 복사하고 이전 빌드의 생성된 SocietyPhotoLibrary.java만 제거한다.
- iOS/Android 빌드 도구와 iOS preset에 타깃 ABI용 iiPhotoLibrary 설치 경로를 추가했다.

기존 `src/App/Photos`의 C++/QML과 원본 `SocietyPhotoLibrary.java`는 수정하지 않고 보존한다.
이 파일들은 사진 기능의 현재 빌드·런타임 경로가 아니다. 공용 `App/Gallery`는 파일 갤러리에서도
사용하므로 Society에 그대로 남긴다. 기존 저장 형식과 인증·동기화 채널은 변경하지 않는다.

## 재현

```sh
cmake -S . -B build -DiiPhotoLibrary_DIR=<SDK-설치본>/lib/cmake/iiPhotoLibrary
cmake --build build --target Society SocietyPhotoTests SocietyNetworkTests SocietyClientOnlyNetworkTests SocietyDriveTests --parallel 4
ctest --test-dir build -R '^Society\.(Photos|PhotoLibraryPackaging|PhotoLibraryIntegration|PhotosNavigation)$' --output-on-failure
```

SDK 설치본에는 C++ 헤더/라이브러리뿐 아니라 QML·Android 소스가 필요하다.
호스트 개발용 `SDK/iiPhotoLibrary/build/install`은 기본 검색 후보이며 교차 컴파일은 ABI가 맞는
설치본을 명시해야 한다. `Society.Photos`는 SDK를 링크하고 리소스 포함·TLS·갤러리 동작을 검사한다.
`Society.NetworkDrive`는 실제 SDK 컨트롤러 타입·미인증 요청 차단·기존 비활성화 변수도 검사한다.
`Society.PhotoLibraryPackaging`은 생성된 Android 패키지와 iOS 빌드 순서를 검증한다.
Android 패키징 테스트는 타깃 등록만 모의하며 실제 CMake 파일 복사·Manifest 처리를 실행한다.

일반 앱 테스트는 기존 `SOCIETY_DISABLE_PHOTOS=1`을 유지해 실제 사용자 보관함을 활성화하지 않는다.
사진 전용 테스트는 별도 fixture 보관함을 주입한다. 최초 갤러리/전송용 저장소 준비는 외장 스토리지
I/O를 포함하므로 완료 대기 한도를 15초로 둔다. 검증 조건과 제품 동작을 완화하지 않는다.

네트워크 및 Drive 테스트는 기존 generation SDK도 연결하므로 macOS에서는
`SOCIETY_MAC_RUNTIME_SEARCH_PATHS`를 BUILD_RPATH에도 반영한다. 이렇게 하면
DYLD 환경 변수로 SDK 선택을 덮어쓰지 않고 json-c 등 구성에 지정된 네이티브 의존성을 찾는다.

검증 산출물은 `build/photo-migration/`, 렌더링 이미지는 `build/photos/`에 둔다.
실제 사용자 보관함 등록·삭제와 iOS/Android/Windows 실기기 검증은 별도 범위이다.

## 확인된 테스트 결과

사진 회귀 35개, SDK 연결·비활성화·미인증 요청 검사, 실제 StorageView의 Photos 진입·모바일 조작,
모바일 패키징 구성 검사가 통과했다. 실행 파일 심벌에서도 SDK 컨트롤러가 연결되고 기존
`society::photos` 컨트롤러가 없음을 확인했다. 원본 사진 구현 11개의 해시도 보존되어 있다.

별도로 실행한 전체 NetworkDrive/ClientOnlyNetwork 스위트에는 각각 7개의 실패가 남았다.
기존 테스트는 현재의 계정·호스트 등록 요건 없이 연결을 시도하며, 모바일 버튼 문구도 현재 UI와
다르게 기대한다. 인증 구현은 작업 전 스냅샷과 동일하다. 이 전환을 위해 인증 조건을 완화하거나
해당 실패를 통과로 취급하지 않았다. 상세 결과는 `build/photo-migration/verification.json`,
`tests-final.log`, `integration-tests.log`, `drive-tests-final.log`에서 확인한다.

최종 Society 앱·데몬 및 관련 테스트 실행 파일 빌드가 완료되었다. 빌드 과정의
`codesign --verify --deep --strict`가 통과했으며, 개발용 DYLD/QML 경로를 제거한 별도 실행 검증에서
GUI 776개·데몬 765개·QML 실행 813개 라이브러리의 로드 경로가 번들 및 시스템 내부임을 확인했다.
빈 설정으로 QML 루트가 로드되었고 사용자 컨테이너를 새로 생성하지 않았다.
검증한 앱은 `build/bin/Society.app`이다. 실제 기기 보관함 등록·삭제 검증을 의미하지 않는다.
