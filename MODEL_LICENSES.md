# Society 모델 라이선스 고지

확인일: 2026-11-26

[iisacc EULA](https://iisacc.com/BuisnessInfo/EULA)는 앱에 적용된다. 이 고지는 아래 세 모델의 상류 약관과 실제 Society 자산의 확인 상태를 별도로 기록한다. 제품 표시명이나 이 고지가 제3자 가중치·LoRA·인코더·VAE의 약관을 대체하거나 새 권리를 부여하지 않는다. 생성물 이용 허용은 생성물에 대한 저작권·독점권·제3자 권리 비침해의 보장을 뜻하지 않는다.

## iisacc Standard Artwork Model

- **원본 라이선스:** Krea 2 계열의 [Krea 2 Community License Agreement](https://www.krea.ai/krea-2-licensing).
- **상업 이용:** §2.3은 모델·파생 모델·생성물의 상업 이용을 최근 12개월 전체 매출 미화 100만 달러 미만인 이용자에게 허용한다. 모든 매출원과 공통 소유·지배하의 계열사를 합산한다. 기준 이상이면 Krea의 별도 Enterprise License가 필요하다.
- **재배포:** §3–4에 따라 약관 사본·출처 고지를 동봉하고 수령자에게 약관을 적용한다. 해당 모델 이름에는 Krea를 앞에 표시하며, 파생본 수정 고지·콘텐츠 필터·허용 사용 정책도 적용한다. 이 문서의 제품 표시명은 상류 명명 의무를 대체하지 않는다.
- **실제 Society 자산:** 로컬 iisacc Standard Artwork Model.safetensors 헤더에는 Krea 2 계열 체크포인트와 여러 LoRA의 병합 이력이 있다. Anima 계열로 명명된 LoRA 입력도 있으나 이름만으로 그 파일의 정확한 출처나 허용 조건을 확정할 수 없다. **전체 병합본의 상업 이용·재배포 권한은 미확인이다.** Krea 2의 조건만으로 병합본이 허가되지는 않는다.

## iisacc Photo Stock Model

- **원본 라이선스:** 공식 [OpenMOSS MOSS-Image-SFT](https://huggingface.co/OpenMOSS-Team/MOSS-Image-SFT)의 가중치와 동봉 추론 코드는 [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0.html)이다.
- **상업 이용·재배포:** Apache 2.0이 적용되는 공식 원본 구성 요소에는 매출 상한이나 비상업 제한이 없다. 재배포 시 약관 사본과 해당 고지를 보존하고, 수정 파일은 수정 사실을 표시하며, 해당되는 NOTICE를 동봉한다.
- **실제 Society 자산:** 로컬 iisacc Standard Photo Stock Corrected.safetensors의 매니페스트에는 MOSS-Image 계열 병합 트랜스포머, mossImage_sft_txt_int8.safetensors 인코더, flux2-vae.safetensors가 들어 있다. 트랜스포머에는 별도 체크포인트·LoRA의 병합 이력이 있다. **전체 패키지의 상업 이용·재배포 권한은 미확인이다.** 공식 MOSS-Image-SFT에 적용되는 Apache 2.0을 나머지 구성품에 일괄 적용하지 않는다.

## iisacc Standard Video Model

- **원본 라이선스:** 기준 모델은 [Lightricks LTX 2.3](https://huggingface.co/Lightricks/LTX-2.3)이며, 공개 가중치에는 [LTX-2 Community License Agreement](https://github.com/Lightricks/LTX-2/blob/main/LICENSE-2)(2026-01-05)가 적용된다.
- **상업 이용:** §2는 연매출 미화 1,000만 달러 이상인 이용 주체가 모델·파생 모델을 상업적으로 사용하려면 유료 라이선스가 필요하다고 규정한다. 매출 기준은 공통 지배하의 계열사를 합산한다. 부속서 A.20에 따라 Lightricks 상용 제품·서비스와 직접 경쟁하거나 이를 대체하도록 설계된 제품의 모델 사용에는 매출과 별개로 상업 라이선스가 필요하다. §5에 따라 Lightricks는 약관에 정한 예외를 제외하고 생성물에 권리를 주장하지 않으며, 생성물 사용에도 약관이 적용된다.
- **재배포:** §3에 따라 약관 원문·부속서·사용 정책을 제공하고 수령자에게 사용 제한을 적용한다. 변경 파일에는 변경 사실을 표시하며 기존의 적용 가능한 고지를 보존한다. 부속서 A.19에 따라 모델에 구현된 안전 기능·워터마크 등 제한을 우회하거나 비활성화하지 않는다.
- **실제 Society 자산:** 로컬의 LTX 2.3 계열 체크포인트 헤더는 버전 2.3.0과 2026-01-05자 LTX-2 약관을 표기한다. 헤더만으로는 제품에 배포할 파일의 정확한 출처, 파생본 변경 내역 또는 상업 이용·재배포 권한이 확인되지 않는다. **최종 배포 파일과 직접 경쟁 조항의 적용 여부는 미확인이다.**

## 공개 배포 전 확인

최종 배포 파일마다 상류 배포처, 버전 또는 SHA-256, 상업 이용과 재배포 범위, 동봉할 약관과 고지를 확인한다. 미확인 자산을 이 고지만 근거로 기본 탑재하거나 상업 납품 가능하다고 표시하지 않는다. 무료 베타도 같은 기준을 적용한다.
