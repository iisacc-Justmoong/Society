# Society Model License Notice

Verified on: 2026-11-26

The [iisacc EULA](LICENSE) governs the application. This notice records upstream model terms and the verification status of three Society assets. The iisacc display names do not identify the upstream rights holders or replace the terms for third-party weights, adapters, encoders, or VAEs. Permission to use an output is not a guarantee of copyright, exclusivity, or non-infringement.

## iisacc Standard Artwork Model

- **Upstream terms:** Krea 2 components are governed by the [Krea 2 Community License Agreement](https://www.krea.ai/krea-2-licensing).
- **Commercial use:** Section 2.3 permits commercial use of the model, derivatives, and outputs under the community terms only if total revenue is below USD 1 million over the trailing 12 months. All revenue sources and entities under common ownership or control count. At or above the threshold, a separate Krea Enterprise License is required.
- **Redistribution:** Sections 3–4 require an agreement copy, attribution, binding recipient obligations, and applicable model naming, modification notices, content filtering, and acceptable-use conditions. The iisacc display name in this notice does not replace upstream naming requirements.
- **Society asset:** The header of the local `iisacc Standard Artwork Model.safetensors` records a Krea 2-family checkpoint merged with multiple LoRAs. One input name suggests Anima, but its name does not establish its origin or license. **Commercial use and redistribution rights for the complete merge are not verified.**

## iisacc Photo Stock Model

- **Upstream terms:** The official [OpenMOSS MOSS-Image-SFT](https://huggingface.co/OpenMOSS-Team/MOSS-Image-SFT) weights and bundled inference code are licensed under [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0.html).
- **Commercial use and redistribution:** Apache 2.0 does not impose a revenue threshold or non-commercial restriction on the covered upstream components. Redistribution requires a license copy, preservation of applicable notices, modification notices for changed files, and the applicable NOTICE material.
- **Society asset:** The local `iisacc Standard Photo Stock Corrected.safetensors` manifest includes a merged MOSS-Image-family transformer, `mossImage_sft_txt_int8.safetensors`, and `flux2-vae.safetensors`. The transformer has additional checkpoint and LoRA inputs. **Commercial use and redistribution rights for the complete package are not verified.** The two companion entries in the local package manifest match the size and SHA-256 published for the INT8 Qwen encoder and the full FLUX.2 VAE at the [pinned Comfy-Org revision](https://huggingface.co/Comfy-Org/MOSS-Image/tree/f7a9772e063542422b66dd492c4e17aa64f7797b). This establishes a metadata match, not a fresh hash of the embedded bytes. The full VAE is not the small decoder listed for the official OpenMOSS SFT package. This does not clear the merged transformer or its inputs.

## iisacc Standard Video Model

- **Upstream terms:** The specified base is [Lightricks LTX 2.3](https://huggingface.co/Lightricks/LTX-2.3). The public weights use the [LTX-2 Community License Agreement](https://github.com/Lightricks/LTX-2/blob/main/LICENSE-2), dated January 5, 2026.
- **Commercial use:** Section 2 requires a paid commercial license for commercial use by entities with annual revenue of at least USD 10 million, with entities under common control aggregated. Attachment A.20 separately requires a commercial license for model use in products that directly compete with or are designed to replace Lightricks’ offerings, regardless of revenue. Section 5 states that Lightricks generally does not claim rights in outputs, but output use must comply with the agreement.
- **Redistribution:** Section 3 requires providing the agreement, attachments, and applicable use policies, and passing use restrictions through to recipients. Preserve applicable notices, identify modifications, and do not bypass the model protections covered by Attachment A.19.
- **Society asset:** A local LTX-family checkpoint header identifies version 2.3.0 and the January 5, 2026 agreement. That header does not establish the exact origin or modification history of the intended release asset, or clear it for commercial use or redistribution. **The final release asset and the applicability of the competition clause are not verified.**

## Release verification

Before distributing any model or describing it as cleared for commercial client work, identify each exact checkpoint, adapter, and auxiliary model by upstream source and version or SHA-256. Verify the intended use, redistribution rights, and required terms and notices. Apply the same checks to free betas.
