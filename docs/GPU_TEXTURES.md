# GPU texture pixels

The Core-only API in `cyuediff/gpu_texture.hpp` decodes an explicitly identified, tightly packed 2D mip. It takes encoded bytes and dimensions; it does not search an asset for plausible image data. Cooked native payload resolution is a separate reader contract.

| Unreal pixel format | Stored samples |
| --- | --- |
| PF_G8 / PF_G16 | Unsigned normalized gray, 8 / 16 bits |
| PF_R8G8 | Two unsigned normalized byte channels; display blue is zero |
| PF_R8G8B8A8 / PF_B8G8R8A8 | RGBA / BGRA byte channels |
| PF_A16B16G16R16 | Four unsigned normalized little-endian 16-bit channels |
| PF_FloatRGBA / PF_A32B32G32R32F | Four little-endian half / float channels |
| PF_DXT1 / PF_DXT3 / PF_DXT5 | BC1 / BC2 / BC3 color and alpha |
| PF_BC4 / PF_BC5 | Unsigned BC4 gray / BC5 RG channels |
| PF_BC6H / PF_BC7 | Unsigned HDR BC6H / byte RGBA BC7 |

```cpp
#include <cyuediff/gpu_texture.hpp>
auto format = cyuediff::unreal_gpu_pixel_format("PF_DXT5");
auto rgba = cyuediff::decode_gpu_rgba8(encoded_mip, width, height, format);
// Preserve all 16-bit UNORM or HDR precision before a separate display tone map:
auto samples = cyuediff::decode_gpu_rgba32f(encoded_mip, width, height, format);
```

The footprint is exact: block formats use ceiling-divided 4 by 4 blocks, including small and partial edge mips. Truncation and surplus bytes are rejected. Positive dimensions, input/output byte limits, pixel limits, overflow, finite floating samples and cancellation are checked. Cancellation remains responsive within a wide single row. Outputs are published only after complete decoding.

No gamma conversion, exposure, color profile, premultiplication, normal-Z reconstruction, array/cube/volume packing or platform swizzle is inferred. Red-only formats are displayed as gray with opaque alpha. BC5 retains RG, zero blue and opaque alpha. HDR requires the floating API and an explicit separate tone map. Reducing 16-bit UNORM to RGBA8 is a preview and cannot prove full-precision equality. Unsupported pixel names fail explicitly.

Block decoding uses pinned bcdec 0.985 under its MIT option; its source, exact hashes and upstream identity are retained. Core has no graphics runtime dependency. CPU BC1/2/3 endpoint interpolation can differ slightly from a particular graphics implementation; comparisons use the same CPU decoder on both sides. Independent D3D11 WARP evidence covers 512 deterministic random blocks of each BC format, with a maximum observed UNORM channel difference of 2/255 and exact BC6H samples in that test. The reference test permits 3/255 for BC1/2/3, 1/255 for BC4/5/7 and bounded half-float rounding for BC6H. This is selected platform evidence rather than a claim of bit identity with every GPU.

Original tests independently specify BC endpoints, selectors, alpha ramps and a complete BC7 mode-6 block. All 15 formats also exercise edge footprints, truncation, surplus bytes, limits and cancellation; half/float checks include subnormals, signed zero and non-finite rejection.
