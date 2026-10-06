# QR Code generator

Generation at QR is not implemented directly but uses the C++ library of Project Nayuki. It is MIT licensed and does not depend on other libraries. This library handles only QR encoding, and iPhone camera decoding is handled by Apple AVFoundation.

- Original: https://github.com/nayuki/QR-Code-generator
- Fixed commit: `3c6d0b3cefb4e049dc337e82237c9644399716a8`
- Original path: `cpp/qrcodegen.cpp`, `cpp/qrcodegen.hpp`
- qrcodegen.cpp SHA-256: `8948b57053deb5d132bfc675ca2688b7abef9f03ec633c0de59770c945a66fc9`
- qrcodegen.hpp SHA-256: `b779c3b156cf7a57ce789d6fee4fc991ccc2913774d26c909d22bb8f26b2a793`

The two source files are exactly as they are. The license is in each file and `LICENSE`, and is also included in app resources. When updating, change the original commit and hash together, and Society. Perform the actual QR image decoding check of Pairing.

`.gitattributes` applies whitespace inspection exceptions only to these two files, preserving the original bytes and SHA-256.
