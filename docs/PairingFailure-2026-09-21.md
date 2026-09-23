# Society iPhone 호스트 연결 실패 기록 — 2026-09-21

기록 시각: 2026-09-21 10:31 KST. 사용자 요청으로 추가 수정, 빌드 및 연결 진단을 중단하였다. 이 문서는 완료 보고가 아니라 실패 원인과 중단 시점의 증거 기록이다.

## 증상과 판정

iPhone에서 `Retry host connection` 버튼이 깜빡이며 반복되는 증상이 보고되었다. 이 문구는 호스트 검증 중 상태와 재시도 가능 상태가 전환될 때 바뀐다. 자동화가 해당 버튼을 반복 클릭했다는 증거는 없다.

초기 실패의 직접적인 원인은 Mac 컨테이너의 동기화 저널에 남아 있던 계정 범위가 현재 인증 계정과 달라 `container_account_binding_mismatch`가 발생한 것이다. 이를 복구한 후에는 실제 LAN 연결과 새 컨테이너 선택까지 진행되었으나, iPhone의 초기 동기화 완료는 확인되지 않았다. 마지막 확인된 iPhone 미러는 `complete: false`이다. 따라서 페어링·전송 연결 자체와 컨테이너 검증·초기 동기화 실패를 구분해야 한다.

## 확정된 사실

### 1. Mac의 기존 동기화 저널이 다른 계정 범위를 유지하였다

- 현재 선택된 디스크: `/Volumes/Storage/Society.sparsebundle`.
- 실제 데이터 루트: `/Volumes/Society Data`.
- 현재 컨테이너: `41e92f9a-7836-4cca-aba6-409752c56296`.
- 기존 Mac 저널의 계정 범위: `fefb35b4ef173d828efb9027f8cb774fe614f4a8d8fc269afdb31bfdc084d3c5`.
- 현재 인증 계정 및 iPhone 미러의 계정 범위: `eac6233b127f576c42901c870fb9d05b94ccfaea0abb93bc0034a7147fb85d01`.
- SDK 진단 로그에서 `Society sync error: container_account_binding_mismatch`가 반복 확인되었다.

계정 범위가 다르면 저장소 열기가 거부되므로, 네트워크 소켓이 연결되어도 정상적인 컨테이너 동기화를 진행할 수 없다. 기존 저널에 다른 범위가 기록된 과거 경위는 이번 조사에서 확정하지 않았다. 이 오류가 사용자 계정 자체가 다른 사람의 계정이라는 뜻은 아니다.

증거: `build/account-host-trace-console.log`.

### 2. iPhone에는 이전 컨테이너의 완료된 미러가 남아 있었다

초기 iPhone 미러는 컨테이너 `9e1843e5-8d3b-4e1b-a1a1-d41f86489a59`에 대해 `complete: true`였다. 이는 현재 Mac의 컨테이너와 다르다. 이전 사본의 완료 상태만으로 현재 호스트 연결 성공을 판단할 수 없는 상태였다.

계정에 등록된 호스트와 컨테이너를 모두 검증하는 조건 때문에, 이 사본을 현재 작업 공간으로 그대로 열 수 없도록 차단되었다. 마지막 관측에서는 새 컨테이너로 전환되었으나 새 초기 동기화가 아직 완료되지 않았다.

### 3. 계정 인증과 호스트 등록은 진행되었다

사용자가 Mac 키체인 대화상자를 처리한 뒤 실제 계정 로그인 화면을 확인하였다. Preferences에서 현재 드라이브를 계정에 저장한 성공 문구도 확인하였다. 복구 후 현재 드라이브 등록을 다시 갱신하자 Mac의 LAN 호스트가 열렸고 iPhone과 TCP 연결이 성립하였다.

이후 iPhone의 미러에 현재 Mac과 같은 계정 범위, 호스트 ID 및 컨테이너 ID가 기록되었다. 단순 TCP 연결뿐 아니라 미러 바인딩 전환도 확인된 것이다. 다만 이것은 초기 동기화 완료 증거는 아니다.

## 이미 수행된 복구와 데이터 보존

Mac 앱을 종료하고 데이터베이스 사용 핸들이 없는 상태에서 기존 `.society-sync` 디렉터리를 같은 볼륨의 아래 경로로 이름 변경하여 보존하였다.

`/Volumes/Society Data/.society-sync-account-recovery-20260921T102121`

이후 현재 인증 계정으로 새로운 동기화 메타데이터가 생성되었다. 사용자 파일 내용은 삭제하지 않았다. 복구 작업 기록은 `build/account-host-metadata-recovery.json`에 있다.

iPhone의 이전 컨테이너 사본은 앱의 정상적인 호스트 전환 절차로 다음 상대 경로에 분리 보존되었다.

`.society-sync/detached/9e1843e5-8d3b-4e1b-a1a1-d41f86489a59-a6280c08-71b5-4bef-a498-e2a849ef8330`

계정 검증, 호스트 검증 또는 컨테이너 검증 조건을 우회하지 않았다.

## 중단 시점의 미해결 사항

### 1. 초기 동기화 미완료

마지막으로 읽은 iPhone 미러 정보는 다음과 같다.

```json
{
  "complete": false,
  "container": "41e92f9a-7836-4cca-aba6-409752c56296",
  "host": "874679ab597a517a372d729ba6e6fb5d69c57087989100e0185b16491852cd45",
  "scope": "eac6233b127f576c42901c870fb9d05b94ccfaea0abb93bc0034a7147fb85d01"
}
```

증거: `build/ios-device/account-host-recovered-mirror.json`. Mac의 새 `.society-sync/primary.json`과 위 식별자들이 일치하였다. 그러나 `complete: true` 및 iPhone 작업 공간 진입은 확인하지 못하였다.

### 2. 저장소 잠금 충돌

새 Mac 저널을 초기화한 뒤 `sync_store_busy`가 반복 기록되었다. 이 오류는 `.society-sync/operation.lock` 획득 실패를 뜻한다. 현재 구현은 호스트 파일 인덱싱과 동기화 요청이 같은 작업 잠금을 사용하며, 일부 작업은 대기 없이 잠금을 시도한다.

파일 목록 재구성 중의 잠금 경합이 재시도에 영향을 줄 가능성이 있으나, iPhone의 최종 실패 원인이 이 오류 하나라고 확정하지 않았다. 실제 관측에서 Mac 저널의 파일 항목은 5,511개였으며 파일 목록이 재구성 중이었다. Mac UI에서는 `Container sync is active.`와 재시도 상태가 관측되었다.

증거: `build/account-host-recovered-console.log`, `SDK/iiSocietySync/src/Replica.cpp`의 잠금 처리. 해당 SDK 경로는 Workspace 기준이다.

### 3. iPhone의 상세 실패 코드 미확보

iPhone 콘솔에는 동기화 종료 `success: false`가 있었으나 당시 설치본에는 이번에 추가한 상세 오류 로깅이 없었다. iPhone에서도 SDK 오류 코드를 확인하기 위해 진단 빌드를 준비하던 중 사용자가 작업 종료를 요청하였다. 따라서 최종 단계의 iPhone 오류 코드는 확보하지 못하였다.

추가로 `Cannot create the shared observation directory.` 경고가 Mac 콘솔에 반복되었다. 이 경고와 호스트 동기화 실패 사이의 인과관계는 확인하지 않았으므로 확정 원인에 포함하지 않는다.

## 중단한 작업과 남아 있는 실행 상태

- 진행 중이던 추가 iOS 앱 빌드를 중단하였다. 중단 당시 Qt QML import scanner가 실행 중이었다. 이번 진단 앱의 최종 빌드·설치 성공으로 보고해서는 안 된다.
- iPhone 콘솔 수집 프로세스를 종료하였다. 이후 기기 실행 상태는 추가 확인하지 않았다.
- Mac의 Society 앱은 종료하거나 다시 설치하지 않았다. 마지막 실행 프로세스는 진단용 `SOCIETY_SYNC_TRACE=1` 및 `DYLD_LIBRARY_PATH=/Volumes/Storage/Workspace/SDK/iiSocietySync/build`를 사용한 상태이다. 정상 패키지 실행으로 되돌린 검증은 수행하지 않았다.
- 앱 자체의 자동 동기화 설정은 유지하였다. 작업 종료가 앱의 자동 동기화 설정 변경을 의미하지는 않는다.
- 이번에 추가한 opt-in 오류 로깅의 소스·테스트·README 변경은 작업 트리에 남아 있다. 네이티브 Controller 테스트는 통과하였고 iOS SDK 라이브러리 빌드 및 스테이지 설치도 완료하였다. 이후 iOS 앱 빌드는 중단하였다.
- 기존 사용자 변경 및 앞선 계정·호스트 검증 변경을 되돌리지 않았다. 이 기록 작성 이후 추가 코드 수정이나 연결 시도를 수행하지 않았다.

기존 전체 검증 이력은 `build/account-host-validation.md`에 있으며, 그 문서의 이전 키체인 대기 및 기기 연결 불가 항목은 과거 상태이다. 현재 실패 판단은 본 문서의 중단 시점 기록을 기준으로 한다.
