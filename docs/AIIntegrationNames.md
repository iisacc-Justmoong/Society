# AI 통합 단위의 이름

`App/Society/AI/`의 공급자별 C++ 단위는 `HuggingFaceIntegration`,
`CivitaiIntegration`, `OllamaIntegration`으로 표기한다. 헤더의 파일명과
선언된 클래스명이 일치한다. 현재 이 세 단위는 비어 있는 이행용 선언이며,
이름 교정으로 공급자 기능이나 새로운 SDK 의존성을 추가하지 않는다.

`tests/integration_names/`는 Qt나 앱 실행 없이 세 공개 선언을 같은 C++23
번역 단위에서 포함·인스턴스화한다. 실제 AI 기능 검사는 현재 앱과
iiMultimodalAgent의 별도 검증 경로를 사용한다.
