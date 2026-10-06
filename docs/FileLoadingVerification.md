<a id="file-loading-verification--2026-09-28"></a>

# 파일 로딩 확인 — 2026-09-28

아래 측정치는 앱 시작 또는 렌더링 프레임 시간이지 모델 수준 시간입니다. 머신은 10 논리 CPU / 32 GiB RAM 를 보고합니다. 파일은 외부 저장소에 있으며, 빌드 활동과 OS 페이지 캐시는 첫 실행 시간에 영향을 미칩니다. 원본은 실제 라이브러리 벤치마크를 위해 수정되지 않았습니다.

<a id="current-turn-regression-rerun"></a>

## 현재 턴 회귀 재실행

- `iiSocietyContainerPreviewCacheTests`, `iiSocietyContainerDashboardTests` 및 `iiSocietyContainerSharedStorageTests` 를 현재 SDK 에 대해 재귀적으로 다시 구축했습니다.
- 미리보기 캐시는 43.40 에 전달되고 대시보드는 25.90 에 전달됩니다.
- 첫 번째 공유 저장소 런은 23 의 24 사례 중 픽스처 에서 일반적인 5초 QtTest 대기 시간을 초과하여 7 초 후에 완료되었습니다. 준비 상태 주장은 30 초를 허용하며 런타임 동작을 변경하지 않습니다. SDK 테스트는 다시 구축하고 공유 저장소를 통해 1/1 를 10.10 초 만에 통과시켰습니다.
- 이는 관찰된 저장소 하중 하에서 회귀 커버리지를 확인하며, 콜드 캐시, 전체 볼륨 처리량 또는 물리적 iPhone 성능 결과가 아닙니다.

<a id="follow-up-validation-current-storage-contention-and-concurrency-check"></a>

## 후속 검증: 현재 스토리지 경합 및 동시성 확인

이후의 재검증은 완전히 친환경적이지 않으며 아래의 과거 측정을 새로운 성능 주장으로 대체하지 않습니다.

- 설치된 macOS GUI 라이브러리는 여전히 `PreviewCache` / `PreviewProvider`를 내보내며, 두 앱 캐시 디렉터리 모두 소유자 전용 권한을 가지고 존재합니다.
- 프리뷰 캐시와 공유 저장소 테스트 대상이 다시 통과했다(77.85 / 96.58초). Dashboard는 SIGTRAP로 종료되었으며, 네이티브 충돌 보고서는 병렬 섹션 스캔 중 힙의 해제 블록 손상을 보고한다. 원인은 검토 중이며, 별도의 힙 계측 빌드와 반복 섹션 격리 테스트를 추가했다. 로그: `SDK/iiSocietyContainer/build/file-loading-current-tests.log`.
- 재구성한 GUI SDK 및 그 대시보드 테스트를 함께 수행한 후, 정상 대시보드 스위트는 5 연속 실행을 통과했습니다 (총 86.84 초). 별도의 AddressSanitizer 빌드가 5 연속 실행을 통과했습니다 (총 89.80 초). 각 실행에는 기존 편집/삭제/감시 계약에 더하여 열두 개의 세 부분 새로고침이 포함됩니다. 이전 단일 충돌은 이 실행에서 재현되지 않았으며, 확인된 손상 수정이나 범용 충돌 없는 보장은 주장되지 않습니다. 로그: `file-loading-dashboard-stress.log`, `file-loading-asan-tests.log` 에서 `SDK/iiSocietyContainer/build/`.
- 한계가 설정된 스트리밍 감시 발견은 동기화 SDK 에서 전체 디렉토리 구체화를 대체합니다. 독립형/ CMake 계약은 통과했습니다 (최종 CTest 0.52 초) 이나, 컨트롤러 스위트는 시작 중 60초 시간 제한을 초과했습니다. 기존 설치된 SDK 도 격리된 시작 확인에서 실패했으며, 이는 새 코드가 원인이었거나 통합이 검증되었다는 것을 입증하지 않습니다.
- 1 MiB 블록으로 네이티브 16 MiB 원본 읽기를 수행하는 데 88.75초가 걸렸다. 이는 관측 당시의 심각한 저장소 지연 증거이며 디스크 손상 진단은 아니다. 백그라운드 경합을 줄이기 위해 이전 일괄 패키저를 정상적으로 취소했다(종료 코드 130, 커밋된 가져오기 0개). 원본은 수정하지 않았다.
- 추가 감시 발견 변경사항은 새 앱 번들로 제공되지 않았습니다. 아래 역사적 설치 캐시 증거를 이 후속 패치 배포나 관찰된 대시보드 충돌 해결과 혼동하지 마십시오.
- 최종 시안 SDK 빌드와 집중된 CTest 통과 ( 0.60 초), 그리고 `cmake --install build` 가 표준 `~/.local/SDK/iiSocietySync` 설치 업데이트함. 이는 실행 중인 Society 앱의 내장 라이브러리 교체 아님, SDK 설치임. 로그: 동기화 SDK 빌드의 `watch-paths-delivery-build.log` , `watch-paths-delivery-tests.log` , `watch-paths-install.log`

<a id="repeat-navigation"></a>

## 탐색 반복

|데이터 세트|이전 첫 번째 행 / 준비됨|캐시된 첫 번째 행|실시간 조정 준비됨|
| --- | --- | --- | --- |
|실제 생성 기록, 94 항목, 2–3|216 / 212 ms|2 / 2 ms|11 / 11 ms|
|합성 텍스트 디렉터리, 2,000 항목, 실행 2–3|23 / 22 ms|25 / 24 ms|87 / 87 ms|

실제 라이브러리 온 경로가 현저히 개선되었습니다. 간단한 텍스트 픽스처 는 첫 행 지연을 개선하지 않았으며, 스냅숏 조정은 준비 경로에 작업을 추가합니다. 보편적 속도 향상을 주장하지 마십시오. 첫 실행은 제어된 콜드 캐시 비교가 아니며, 속도 향상 계산에 의도적으로 사용되지 않습니다.

원시 보고서는 `SDK/iiSocietyContainer/build/`: `loading-real-baseline.json`, `loading-watch-real.json`, `loading-baseline-warm.json`, `loading-watch-fixture.json` 아래에 있습니다.

미리보기 회귀 는 21.9 – 156.1 ms 에 2400×1600 테스트 PNG 를 디코딩/해시/저장하고, 신선한 캐시 객체에서 지속된 512픽셀 미리보기를 로드하는 데 약 0.7 – 0.8 ms 를 측정합니다. 이는 하나의 테스트 이미지 결과이며, 전체 라이브러리 처리량이 아닙니다.

<a id="checks"></a>

## 수표

- SDK의 프리뷰·대시보드·공유 저장소 CTest 대상이 통과했다(3/3).
- 소스 덮어쓰기/삭제, 임시 스냅샷 조정, 감시자 새로 고침, 콘텐츠 SHA-256, 동시 캐시 요청, 손상된 미리보기, 취소 및 이전 대시보드 `previewSource` 호환성이 포함됩니다.
- 제품 QML는 검증된 렌더링된 `Image.Ready`, 이미지 교체/캐시 업데이트, 선택/스크롤 보존, 대시보드 추가/편집/제거 업데이트 및 새로운 썸네일 필드를 사용한 인터랙티브 대시보드 카드를 테스트합니다 (8 통과; 최종 호환성 빌드).
- macOS와 iOS의 SDK 빌드/설치가 통과했다.

<a id="application-delivery"></a>

## 신청서 전달

- macOS 애플리케이션 컴파일/링크가 완료되었습니다. 배포가 중단되어 `QtNetwork.framework`가 완성되지 않았으며, 누락된 리소스와 버전 심볼릭 링크가 동일한 Qt 6.8.3 설치에서 복원된 후 서명이 재개되었습니다.
- 외부 저장 후보의 표준 런타임 프로브가 변경되지 않은 30초 제한을 초과했습니다. 이것은 실패한 시작 프로브이며, 통과하는 build/runtime 검사가 아닙니다. 로그: `build/file-loading-macos-source-runtime.log`.
- 수리된 macOS 후보가 `codesign --verify --deep --strict`를 통과했으며, `/Applications/Society.app`에 설치되었고, 설치된 번들은 동일한 서명 검사와 표준 런타임 프로브를 통과했습니다 (776 GUI -loader / 771 데몬-loader 라이브러리; 번들/시스템 위치 외에는 없음). 로그: `build/file-loading-sign-recovery.log` , `build/file-loading-macos-installed-runtime.json` .
- 설치된 GUI 가 `bootstrap.entry.root-loaded` 에 도달함; 그 생성 이력은 94 행과 실제 이미지 미리보기, 이동 후/뒤로 돌아온 후 포함됨. `~/Library/Caches/Society/society` 에서 관찰된 애플리케이션 캐시는 하나의 디렉토리 스냅샷과 45 개의 PNG /manifest 쌍 (그 샘플의 13,834,190 바이트) 을 포함함. 한 미리보기는 512x512 로 모드 0600 와 64자리의 SHA-256 필드를 가짐. 그 manifest 의 원래 파일을 독립적으로 해싱하면 저장된 다이제스트와 일치함. 이 관찰은 렌더링된 프레임 지연 시간이나 차가운 앱 시작을 측정하지 않음.
- iOS 릴리스 빌드가 성공했습니다. 앱, File Provider 및 Live Activity는 연결된 iPhone 15 Pro Max에 대한 서명된 번들/프로비저닝 검사를 통과했으며, 이후 `devicectl`가 데이터를 제거하거나 재설정하지 않고 `com.iisacc.society`를 성공적으로 설치했습니다. 로그: `build/file-loading-ios-build.log` , `build/file-loading-ios-bundle.json` , `build/file-loading-ios-install.json` .
- 새로운 iPhone 설치가 `FBSOpenApplicationErrorDomain / Locked`와 함께 거부되었습니다. 새 설치가 확인되었지만, 물리적 장치 실행, 미리보기 캐시 생성 및 재연결 타이밍은 사용자가 장치를 잠금 해제할 때까지 확인되지 않습니다. 로그: `build/file-loading-ios-launch.json`.
