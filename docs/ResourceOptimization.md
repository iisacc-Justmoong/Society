# 모듈 의존성과 모델 목록 작업량

## 의존성 소유권

Society의 MCP 도구는 `iiMultimodalAgent`의 도구·질문·로컬 서버 인터페이스를
호출한다. 앱은 해당 연결 때문에 `iiLocalLLM`, `NativeImages`, 전체
`iiLocalDiffusion` 런타임을 직접 링크하지 않는다. 추론은 GenerationHost와
SDK가 소유하고 모델 패키징 기능은 별도 ModelPackaging 컴포넌트를 사용한다.
SDK가 실제 사용하는 전이 의존성과 모델 생성 기능은 유지한다.
동기화 수명을 담당하는 SocietyDiscovery는 GenerationHost를 공개 링크하지
않는다. 추론 호스트는 실제 NetworkDriveController의 데스크톱 호스트 구현을
컴파일하는 앱·데몬·통합 검사에서만 직접 링크한다. 클라이언트 전용 구성은
호스트 멤버·호출도 컴파일하지 않는다. 관련 없는 Discovery 실행 파일에
iiLocalDiffusion이 들어오던 경로와 클라이언트의 불필요한 호스트 생성을 제거한다.
`NativeImages`·`iiLocalLLM` 등의 macOS 런타임 탐색 입력은 직접 링크와
구분한다. 동적 이미지 플러그인은 실제 배포 입력이므로 제거하지 않는다.
SDK 목록과 앱 링크 양쪽에 있던 iiPhotoLibrary의 중복 직접 참조는 하나로
통합한다. 최종 생성 링크 목록에서 정확히 한 번 선언되는지 검사한다.

현재 Helper와 같은 iiSharedCanvas 0.32.0을 사용한다. 이름 변경 이전의
`iiAcountManager`를 요구하는 오래된 빌드 캐시를 재사용하지 않는다. 앱 버전과
패키지·ABI 선택은 별개의 값이며 SDK 전역 설치를 덮어쓰지 않는다.
배포기는 선택한 Core 바이너리를 명시적인 탐색 입력으로 받아, 이전 번들의
0.31 계열 라이브러리를 새 0.32 의존성의 대체물로 사용하지 않는다.

`LIBRARY_PATH` 때문에 SDK 디렉터리가 CMake의 암묵적 링크 경로가 되면
자동 실행 경로가 생략될 수 있다. 설치 패키지의 호환성 파사드만 실행 경로를
전파하는 경우에도 Core 소비자가 독립적으로 실행되도록, Society 연결부는
Core의 실제 라이브러리 디렉터리를 해당 컴포넌트의 링크 옵션으로 전달한다.
하위 컴포넌트에 GUI·파사드 링크를 추가하거나 전역 DYLD 설정으로 가리지 않는다.
`Society.ComponentRuntimeBoundary`는 Mach-O 실행 경로와 실제 헤드리스
프로세스 시작을 loader 환경 변수 없이 확인한다.
이 검사와 `Society.ClientRuntimeBoundary`는 추론 라이브러리가 직접 링크되지
않는지도 확인한다. 실제 호스트의 작업 처리는 기존 NetworkDrive 회귀가 담당한다.

## 선택적 ONNX 런타임의 시작 경계

Pose와 iiDecision의 ONNX·Laya 추론은 `iiOnnxRuntime`을 통해 최초 명시적
요청에 백엔드를 로딩한다. 기능 가용성 조회와 GUI·데몬 상태 확인은 실제
ONNX 공유 라이브러리를 초기화하지 않는다. 공통 로더는 모델이나 작업
스케줄러를 소유하지 않으며, Pose와 의사결정 API의 기존 결과 계약을 유지한다.

macOS 배포는 `iiOnnxRuntime_RUNTIME_LIBRARIES`를 명시적인 runtime plugin
입력으로 전달한다. 따라서 강한 링크가 없어도 선택적 백엔드와 그 의존성을
포함·서명하고, compact 정리가 이 파일을 삭제하지 않는다. 이전 번들의
사용하지 않는 ONNX 버전은 기존 의존성 정리 규칙으로 제외한다.
`verify_macos_runtime.py`는 개발 로더 경로를 제거한 상태·부트스트랩 검사에서
SDK가 선언한 선택적 ONNX 백엔드가 이미 로딩되어 있으면 실패한다. SDK의 별도 실제 추론·설치 위치
변경 검사는 첫 사용 이후의 기능을 확인하며, 이 시작 검사를 대신하지 않는다.

음성 합성용 Piper는 별도의 ONNX 1.22를 실제 사용한다. 이 런타임은 음성
기능의 의존성으로 보존하며, Pose·의사결정용 지연 백엔드의 파일 이름을 SDK
메타데이터에서 검증기에 명시적으로 전달한다. 같은 접두사를 가진 다른 런타임을
지연 백엔드로 오인하여 삭제하거나 검증을 실패시키지 않는다.

## macOS 서비스 상태 조회

실제 서비스 상태 검사에서 `SMAppService.status`의 ServiceManagement XPC 응답을
기다리는 스택을 확인했다. GUI의 기존 서비스 등록 경로는 이미 활성화된 서비스도
같은 호출 안에서 상태를 세 번 조회했다. 서비스별 최초 상태를 한 번 읽고, 실제
등록·해제 요청을 보낸 경우에만 결과 상태를 다시 읽는다. 이미 활성화된 두 서비스를
확인하는 경로는 여섯 번에서 두 번의 조회로 줄어든다.

상태 문자열·성공 여부·오류·서비스 이름과 등록·해제 동작을 유지한다. `status`
명령은 실제 OS 상태를 한 번 조회하므로 OS 서비스 응답 지연은 별도 경계이다.
동일 호출의 상태 재사용을 프로세스 간 캐시나 오래된 상태의 반환으로 확장하지
않는다. `Society.ServiceRegistrationPolicy`는 OS 접근 없이 96개 상태·명령·실패
조합의 결과, 변경 호출 횟수와 상태 조회 한도를 확인한다. 이 검사에서 로그인
서비스를 등록·해제하거나 실제 데몬을 종료하지 않는다.

## 목록 새로고침의 자원 한도


`MergeModelCatalog`는 수락한 새로고침 횟수와 관계없이 실행 중이거나 작업
풀에 제출된 스캔 하나만 보유한다. 추가 요청은 현재 작업에 취소 의도를
전달하고 최신 디렉터리·세대 하나로 합친다. 이전 작업의 완료가 전달된 뒤에만
최신 작업을 시작한다. 숨겨진 이전 목록이나 중간 디렉터리의 결과를 공개하지
않는다. 빈 디렉터리는 현재 작업을 취소하고 목록과 loading을 즉시 비운다.

헤더 읽기, 디렉터리 순회와 모델 분류는 기존 작업 풀에서 실행한다. 취소를
헤더 읽기 전후·텐서 항목 순회 중에도 확인하여 이미 오래된 요청의 나머지
분류를 생략한다. 진행 중인 OS 파일 읽기의 즉각적인 중단까지 보장하지는
않는다. 소유자 제거는 작업자 완료를 GUI에서 기다리지 않는다.

## 검증

`Society.ModelCatalog`는 실제 공용 작업 풀을 제어하여 202개의 새로고침 의도
동안 제출된 스캔 하나를 확인한다. 풀을 재개하면 최신 디렉터리의 모델만
한 번 공개해야 한다. 빈 경로 전환과 작업 대기 중 소유자 제거도 검사한다.
이 검사는 Python 생성기·Torch·LVRS를 요구하지 않는 일반 실행 파일이다.
Qt Core 저장소와 헤더 전용 QML 등록 경계만 사용한다. macOS의
`Society.ModelCatalogBoundary`는 실제 실행 파일의 Mach-O 링크에서 Qt
Quick/QML·LVRS·추론 라이브러리가 없는지도 확인한다.

`Society.Dependencies`는 최종 생성된 링크 속성을 검사하고,
`Society.Mcp`와 패키지 실행 검사는 실제 앱 연결을 검증한다. 전후 로그와
소스 기준선은 `build/optimization-20261027/`에 둔다. 목록 작업의 자원 한도와
패키지 링크 검증을 전체 앱 RSS·실제 모델 추론 성능과 구분한다.

의존성 이동 전후 Discovery 검사 실행 파일을 별도 프로세스로 세 번씩 교대로
실행하여 동일한 QtTest 함수 목록을 확인했다. 최대 RSS 중앙값은
21,757,952바이트에서 17,825,792바이트로 3.75MiB 감소했다. 제거된 직접
동적 링크는 iiLocalDiffusion과 QtConcurrent이며 새 링크는 없다. 이 측정은
함수 목록을 출력하는 검사 프로세스의 로더·초기화 범위이다. 전체 Society 앱의
사용 중 메모리, 네트워크 탐색 또는 추론 성능을 대신하지 않는다. 원자료는
`discovery-link-comparison.json`, `discovery-process-comparison.json` 및
`discovery-paired-*.log`이다.

현재 구현의 기능·실행 경계는 다음 명령으로 다시 확인한다. 시간 측정값을
통과 조건으로 사용하지 않고 작업 개수와 실제 결과·동적 링크를 단언한다.

```sh
ctest --test-dir build --output-on-failure -R '^Society\.(ModelCatalog|ModelCatalogBoundary|ComponentRuntimeBoundary|ClientRuntimeBoundary|Discovery|ClientOnlyNetwork|Dependencies)$'
```

## 2026-10-27 검증 범위

macOS arm64 Release에서 정식 앱 빌드·배포기 서명 검증과 단일 앱 감사를
수행했다. 모델 목록, 목록 링크 경계, Core 실행 경계 두 개, Discovery,
NetworkDrive, ClientOnlyNetwork 및 실제 앱의 MCP 검사가 통과했다. 직접
의존성 선언 검사는 전체 묶음과 분리한 함수 실행에서도 통과했다.

ONNX 지연 로딩과 서비스 상태 조회 변경을 포함한 최종 전체 CTest 결과는
44개 묶음 중 37개 통과, 7개 실패이다. ModelCatalog와 관련 링크·실행 경계,
새 ServiceRegistrationPolicy 검사는 통과했다. Pairing, Account, Daemon,
ModelMerge, Drive의 실패·시간 초과는 이전 전체 실행에서도 발생했다.
MacRuntimeLaunch와 Mcp는 이전 전체 실행에서는 통과했으나 이번에는
각각 서비스 상태 조회와 실제 앱 시작 대기에서 제한 시간을 초과했다.
따라서 일곱 실패 전부를 기존 실패로 분류하지 않는다.

독립 진단에서 서비스 상태 조회는 `SMAppService.status`의 XPC 응답을
기다렸으며, MCP의 25초 앱 시작 재현은 로그 출력과 `main` 진입 이전의
dyld 매핑 단계에서 제한 시간에 도달했다. 이 관찰은 해당 진단 프로세스의
실패 위치를 나타낸다. 전체 회귀 통과나 모든 시작 지연의 해결을 의미하지
않는다. 제한 시간을 늘리거나 저장소의 내구성 설정을 낮추지 않았다.

이전 전체 결과는 43개 중 35개 통과, 8개 실패였으며
`full-ctest-final.xml`에 보존한다. 최신 전체 원자료는
`build/optimization-20261027/full-ctest-lazy-onnx-final.xml`과
`full-ctest-lazy-onnx-final.log`이고, 비교 결과는
`whole-regression-lazy-comparison.json`에 있다. 빌드 후보는
`build/bin/Society.app` 하나이며 버전·빌드는 모두 0.1.0이다. 설치 앱 교체와
기존 사용자 프로세스 재시작은 수행하지 않았다.
