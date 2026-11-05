# Distribution

The original code is MIT licensed. CMake installation includes `LICENSE`, `THIRD_PARTY_NOTICES.md` and the bundled dependency licenses. No engine, project asset, Git object store or Oodle binary belongs in a distribution.

## Local archives

Build and test first, then run CPack in that build directory:

```powershell
python scripts/build.py --desktop --cyui D:/Projects/CyUI
cd build-native
cpack -C Release -G ZIP
```

Run `python scripts/validate_distribution.py build-native/CyUEDiff-VERSION-Windows-AMD64-desktop.zip --consumer` to check an extracted archive and compile/run the Core consumers for catalogue/container/cooked/schema/image/collision/skeleton/animation/poses/curves, plus the Git history and Git-bound animation pose consumers when the Git SDK is included. Use `--static-runtime` when validating the static-CRT CI archives. For a desktop archive it also saves and reopens a blank workspace through the extracted WARP host. The check retains its extraction under an ignored build directory.

The ZIP contains the selected CLI/desktop executables, the reusable Core SDK (plus Git when enabled), public Core headers, CMake import targets and documentation/notices. UI headers/library are integrated from source by hosts; they are not advertised as an installed standalone UI SDK. The optional Oodle module is deliberately excluded even if it was built locally.

Windows archives built with the default dynamic MSVC runtime require the matching Microsoft Visual C++ x64 runtime. Configure `-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` before building a self-contained CRT variant, and use the same runtime when linking the static SDK. Direct3D 11 is provided by Windows; Git functionality requires a separate Git 2.45+ installation. Linux archives target the build runner's libc/libstdc++ versions. No signing certificate or installer is embedded.

## Continuous integration

`.github/workflows/ci.yml` builds/tests Core+CLI on Windows 2022 and Ubuntu 24.04, each with Git enabled and disabled. It also installs the SDK and compiles external Core-only catalogue, container and cooked Zen consumers. CPack archives are downloadable as workflow artifacts. A separate job runs Gitleaks 8.30.1 over all publishable files and the complete fetched Git history. Actions use commit-pinned official implementations; the Gitleaks download has a pinned SHA-256 verified against the upstream release metadata.

The hosted workflow takes effect only after these files are pushed. Local validation does not certify a hosted run. This workflow does not publish a GitHub Release or change repository visibility. For a release, inspect the CI results and archives, then attach the selected archives to a draft release with the corresponding version tag.

The desktop requires the host-provided CyUI checkout and is tested locally by the native smoke/capture tools. Its installation preserves CyUI, ImGui, stb and syntax-data licenses from the exact checkout selected by the host. No private dependency credentials are stored in the workflow.

The pinned official Actions releases are [checkout v7.0.1](https://github.com/actions/checkout/releases/tag/v7.0.1) and [upload-artifact v7.0.2](https://github.com/actions/upload-artifact/releases/tag/v7.0.2), both using Node 24. Their immutable commit refs were verified when the workflow was prepared.


For caller-supplied historical animation data, `examples/git_animation_pose_consumer` builds against the installed `CyUEDiff::Git` target and uses Core poses without CyUI. `scripts/validate_git_desktop_poses.py` compares that installed executable against the native desktop and original explicit animation/Skeleton files. The distribution validator compiles and starts the consumer; a real/fixture corpus run remains separate and never adds host-owned assets to the ZIP.


The installed animation pose and Git animation pose examples also compute stored socket origins from prepared source affines. The Core example constructs its own socket definition and checks a known offset/displacement; the Git example retains both historical Skeleton materializations while projecting their stored socket definitions. `--sockets` enables the additional original/native/installed checks in the pose validators.
