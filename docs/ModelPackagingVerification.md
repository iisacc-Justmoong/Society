# Model Packaging 실행 검증

검증일은 2026-10-02이다. Figma Society `297:511`을 기준으로 구현한
Tools → Model Packaging과 Qt 비의존 C++23 SDK를 검증하였다.
산출물 경로는 Society의 `build/model-packaging-verification/`이다.

## 설치된 macOS 앱

`/Applications/Society.app`을 Developer ID로 서명한 빌드로 갱신하였다.
`codesign --verify --strict`가 통과하였으며 실제 실행된 앱에서 다음을 확인하였다.

- Tools의 Model merge 옆 Model Packaging 카드와 실제 화면 진입.
- macOS 폴더 대화상자로 원본 및 출력 폴더 선택.
- 자동 구성요소 감지, 키보드 파일명 편집, 선택 업스케일러 제외.
- 모든 파일과 중복·인증 실패의 상세 사유 표시.
- 실제 Package model 실행 및 Saved and verified 완료 표시.
- Copy details JSON과 독립 SHA-256·파일 크기의 일치.
- Open folder를 통한 Finder의 실제 출력 파일 표시와 Done 닫기.

UI 원본은 실제 소형 Safetensors/GGUF를 사용하는 `UI fixture/`이다.
출력 `Society UI verified package.safetensors`는 7,482바이트이며 구성요소
6개와 입력 파일 3개를 포함한다. SHA-256은
`defdbaa8da7e54db6a5a45e49599b807fd2d03ba050cf899888fc238039c0cf1`이다.
복사한 실제 완료 보고서는 `ui-create-report.json`이다.

독립 실행 검사는 개발 환경의 라이브러리·QML 경로를 제거하여 실행하였다.
`canonical-runtime.json`의 GUI 778개, 도우미 773개, QML 817개 로딩 경로는
모두 앱 번들 또는 시스템 내부이다. GUI 루트 로딩과 컨테이너 미생성도 통과하였다.
처음 빌드 후 검사에서는 서비스 상태 실행이 30초 제한을 초과하였다.
동일한 번들의 120초 진단에서는 약 35초 후 정상 JSON 응답을 반환하였으며,
소스 변경 없이 기존 30초 검사를 재실행하여 GUI 검사까지 통과하였다.
이후 최종 `cmake --build build --target Society --parallel 6`도 서명·서비스 실행
검사를 포함하여 종료 코드 0으로 완료하였다. 최종 GUI 독립 실행 검사도 통과하였다.
설치본을 이 최종 번들과 일치시킨 뒤 서명을 다시 검증하고 실제 앱의
Tools → Model Packaging 진입을 재확인하였다. 최종 설치본 실행 파일과 빌드의
SHA-256은 모두 `fec37ef2452ec4120b02221a3979a75b564ebac67559c227e688ead75483d393`이다.
근거는 `final-canonical-runtime.json`, `final-installed-artifact.json`이다.

## 실제 LTX 폴더

`/Volumes/Storage/ltx-uncensored`를 읽어
`ltxv23_uncensored_v1.4_bundle.safetensors`를 생성하였다.
SDK가 출력 전체를 다시 읽어 각 텐서와 원본 파일의 SHA-256을 검증하고
`verified: true`를 반환한 뒤 최종 경로로 저장하였다.

| 항목 | 결과 |
| --- | --- |
| 출력 크기 | 37,463,275,686바이트 |
| 구성요소 | 6개 |
| 포함 파일 | 메인 체크포인트, Gemma GGUF, 선택 업스케일러 3개 |
| 중복 제외 | video VAE, audio VAE, projection 3개 |
| 중복 데이터 절감 | 4,129,196,430바이트 |
| 잘못된 파일 | 32바이트 인증 실패 파일 1개 |
| SHA-256 | `f9c0e70c6519efafd084cb32dc8c5e1f86b059dcea048c85ef75863a88b1fe41` |

SDK 보고서는 `ltx-create-report.json`, 단계별 실제 진행은 `ltx-create.ndjson`이다.
추가 독립 검사에서는 Python의 별도 헤더 리더와 Rust `safetensors.safe_open`으로
출력 텐서 8,484개를 정상적으로 읽었다. 모든 원본·중복 매핑 9,987개에 대해
shape, dtype, 메타데이터와 각 텐서의 앞·뒤 데이터 표본을 대조하였다.
GGUF 원본 크기와 데이터 표본도 일치하였다. 결과는
`ltx-independent-verification.json`이며 전체 바이트 해시는 SDK 검사로 검증하였다.
실제 설치된 Society UI의 폴더 검사에서도 구성요소 6개, 포함 파일 3개,
중복 3개, 인증 실패 1개, 37.46 GB 및 중복 절감 4.13 GB를 확인하였다.
해당 화면은 `installed-society-ltx-packaging.png`에 저장하였다.
이 결과는 패키징·무결성 검증이며 LTX 2.3 추론 완료를 의미하지 않는다.
GGUF는 원본 양자화 파일의 U8 데이터로 보존되므로 전체 구성요소 로딩에는
manifest를 해석하는 리더 또는 SDK 원본 추출이 필요하다.

## 회귀 검사 범위

- SDK ModelPackaging의 12개 통합 검사 통과.
- Society.ModelPackaging의 컨트롤러·실제 QML 조작·360 px 배치 검사 통과.
- Cocoa 네이티브 하니스의 3개 검사 통과.
- Tools 진입·뒤로 가기·기존 탭/프롬프트 유지 관련 검사 7개 통과.

전체 Society.Drive 검사에서는 Models 화면의 위치 기대값 `(12, 247)`과
실제값 `(12, 207)`이 달랐고, 전체 실행이 300초 제한에 도달하였다.
따라서 전체 Society 회귀 검사 통과로 보고하지 않는다. 해당 Models 화면의
구현은 이번 패키징 작업에서 변경하지 않았다.
