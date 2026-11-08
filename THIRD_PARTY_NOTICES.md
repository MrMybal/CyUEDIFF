# Third-party notices

- **BLAKE3 1.8.2**, commit `df610ddc3b93841ffc59a87e3da659a15910eb46` — CC0-1.0 (selected from the upstream CC0/Apache-2.0 dual license). Unmodified portable C sources, both licenses, pinned-file hashes and official test vectors are in `third_party/blake3`. Private symbol prefixes are applied at compilation; no SIMD assembly or external runtime is used. Upstream: https://github.com/BLAKE3-team/BLAKE3/tree/1.8.2.
- **bcdec 0.985**, commit `80859ed3b7afb1c527a2a99d70c61457bea72d0c` — dual MIT/Unlicense; distributed here under the MIT option. Unmodified CPU block decoders, license and pinned-file hashes are in `third_party/bcdec`. Symbols stay private to Core; no GPU runtime is required. Upstream: https://github.com/iOrange/bcdec/tree/80859ed3b7afb1c527a2a99d70c61457bea72d0c.
- **nlohmann/json 3.12.0** — MIT. Source: `third_party/nlohmann/json.hpp`; license: `third_party/nlohmann/LICENSE.MIT`. Upstream: https://github.com/nlohmann/json/tree/v3.12.0.
- **stb_image and stb_image_write**, revision `d14d46f6b44456eb88beac58cba833a4135d4c89` — dual MIT/public domain; distributed here under the MIT option. Sources and license are in `third_party/stb`. Upstream: https://github.com/nothings/stb/tree/d14d46f6b44456eb88beac58cba833a4135d4c89.
- The optional UI integration uses **CyUI** (MIT) and its dependencies from a host-provided checkout: https://github.com/MrMybal/CyUI. No CyUI source files are copied into this repository.

No Unreal Engine source code, binaries or assets are redistributed. CyUEDiff's original code is licensed under the MIT License; see `LICENSE`. This grant does not replace the licenses of third-party components.

The optional Oodle adapter is built from a host-provided Data SDK. This repository contains no SDK files. Locally built adapter binaries incorporate that library and are excluded from the Core SDK installation; see `docs/CODECS.md`.
