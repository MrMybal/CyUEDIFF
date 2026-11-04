# HDR source textures

The Core exposes `cyuediff/hdr.hpp` independently of the UI. `decode_texture_hdr` reads finite raw source samples from single-block, single-layer, single-slice 2D textures with explicit mip selection. Supported formats are RGBA16F, RGBA32F, R16F, R32F, BGRE8 and deprecated RGBE8. Single-channel formats replicate the scalar to RGB and use alpha one. RGBE follows the saved channel ordering, shared exponent and mantissa midpoint convention. Bulk decompression remains an explicit host-supplied codec when required.

`ImageRgba32f` retains the decoded values, including negatives, values above one and half-float subnormals. It applies no exposure or color-space conversion. Allocation and payload limits are enforced before decoding. Non-finite samples, encoded PNG/JPEG HDR variants, volume/cube textures, multiple source blocks/layers, missing/virtualized payloads and unknown layouts are rejected with explicit errors.

`compare_hdr_images` compares these source samples with a non-negative absolute threshold, reports changed pixels, maximum sample delta and mean absolute error, and optionally produces an RGBA8 binary mask. Alpha participates in the comparison. Different dimensions are incomparable. This comparison can detect changes hidden by an 8-bit preview.

`tone_map_hdr` is a separate display operation: RGB is multiplied by `2^exposureEV`, negative values are clamped, positive Reinhard `x/(1+x)` is applied, and linear-to-sRGB transfer produces RGBA8 display values. Alpha is clamped independently. Exposure must be finite and within -32..32 EV. `decode_texture` uses this display conversion at zero EV for HDR sources; the floating-point API retains the original samples.

```powershell
cyuediff preview T_CloudPattern.uasset --codec cyuediff_oodle.dll --exposure 1 --out preview.png
cyuediff hdr-diff before.uasset after.uasset --codec cyuediff_oodle.dll --threshold 0.001 --heatmap mask.png
```

`hdr-diff` thresholds are source-sample values, distinct from the byte thresholds in `image-diff`. `image-diff` refuses HDR input and points to `hdr-diff`. The desktop compares floating-point samples before uploading the tone-mapped source previews and mask. Mixed HDR/LDR inputs require an explicit common sample space and are reported as incomparable. The UI is available in English and French.

Validation includes half-float normal/subnormal/maximum values, negative and float32 values, RGBE exponents, mip offsets, exposure, budget/error/cancellation cases, and a change hidden by identical RGBA8 display values. Real UE 5.3 sources `T_Profile_08` (RGBA16F, 256²), `T_CloudPattern` and `T_CloudMask` (RGBA32F, 1024²) were decoded with the explicitly supplied local Oodle adapter. Native desktop/CLI HDR reports agree and original source hashes remain unchanged. No assets or codec binaries are redistributed.
