# QR Code generator

QR 생성은 직접 구현하지 않고 Project Nayuki의 C++ 라이브러리를 사용한다.
MIT 라이선스이며 다른 라이브러리에 의존하지 않는다. 이 라이브러리는 QR 인코딩만 담당하고,
iPhone 카메라 디코딩은 Apple AVFoundation이 담당한다.

- 원본: https://github.com/nayuki/QR-Code-generator
- 고정 커밋: `3c6d0b3cefb4e049dc337e82237c9644399716a8`
- 원본 경로: `cpp/qrcodegen.cpp`, `cpp/qrcodegen.hpp`
- qrcodegen.cpp SHA-256: `8948b57053deb5d132bfc675ca2688b7abef9f03ec633c0de59770c945a66fc9`
- qrcodegen.hpp SHA-256: `b779c3b156cf7a57ce789d6fee4fc991ccc2913774d26c909d22bb8f26b2a793`

두 소스 파일은 원본 그대로이다. 라이선스는 각 파일과 `LICENSE`에 있으며 앱 리소스에도 포함한다.
업데이트할 때 원본 커밋과 해시를 함께 바꾸고 Society.Pairing의 실제 QR 이미지 해독 검사를 실행한다.

`.gitattributes`는 위 두 파일에만 공백 검사 예외를 적용하여 원본 바이트와 SHA-256을 유지한다.
