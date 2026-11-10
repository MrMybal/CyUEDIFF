# Distribution

The original code is MIT licensed. CMake installation includes `LICENSE`, `THIRD_PARTY_NOTICES.md` and the bundled dependency licenses. No engine, project asset, Git object store or Oodle binary belongs in a distribution.

## Local archives

Build and test first, then run CPack in that build directory:

```powershell
python site/tools/build.py --output build-manual/pages
python scripts/build.py --desktop --cyui D:/Projects/CyUI
cmake -S . -B build-native -DCYUEDIFF_OFFLINE_MANUAL_DIR="$PWD/build-manual/pages"
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


## Local 0.182 development archive

The local development archive packages the implemented post-0.181 readers and EN/FR desktop. It has a distinct version/file name so the previous local 0.181 ZIP remains unchanged. Preparing and checking this archive does not publish it. Current hosted evidence remains identified separately in the version notes.

`share/CyUEDiff/build-info.json` records the version, development channel, license, platform/architecture, installed components and desktop workspace schema. The extracted CLI `--version` and desktop report must agree with that metadata. CMake supplies `CyUEDiffConfigVersion.cmake`; consumers can request the exact version, with compatibility limited to the same minor version of this development API. Source-only view headers are excluded from the installed Core SDK, including newly added inspectors.

The verifier checks every installed CyUEDiff include dependency, compiles all public headers together against the exact extracted Core package and runs original focused vertex-attribute/overlap queries. Existing examples are also built from their extracted copies; source-checkout examples do not stand in for missing archive contents. Workspace save/reopen uses the declared current schema rather than a stale hard-coded version. Archives without build metadata use the legacy workspace check; the new metadata/version contract applies to the new package. Notices, forbidden host payloads, startup, session/preferences, corrupted-session preservation and independent SDK consumers remain checked. Real Engine corpus references are separate local evidence and never enter the ZIP.

The Windows desktop archive includes `START_HERE.txt` in English/French and the existing bilingual HTML reader under `share/CyUEDiff/manual`. Its native/context/offline chapters are current for 0.182; all runtime resources are local and content-hashed. The two added cooked attribute captures use original encoded fixtures. Existing captures retain their recorded versions and hashes. The desktop archive verifier requires its prepared manual; a Core-only SDK may omit it. The manual build must not include a nested ZIP download.

Git distribution checks create their own bounded, original two-commit repository with disabled hooks/templates and known bytes, then verify that history/delta reads leave every fixture file unchanged. They do not depend on the source checkout’s ownership, history, user settings or remotes. Original IoStore fixtures now contain matching required BLAKE3-160 metadata; a changed payload with unchanged metadata must be rejected rather than bypassing integrity.

The verifier writes `validation.json` beside the retained extraction only after all requested checks pass. It identifies the archive hash, extracted prefix, version, header count, manual/desktop components and whether SDK consumers were run. The site builder accepts a copied version-matching successful record via `--local-build-record`; download inclusion remains explicit and local.

## Runtime identity and helper selection — 0.192

New build metadata records `msvcRuntime` as `static`, `dynamic`, `host-configured` or `not-applicable`. The bilingual startup file states the matching requirement. A Release consumer of a static-runtime SDK must also configure `-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`; the archive verifier checks that its `--static-runtime` selection agrees with declared metadata before compiling consumers. Static runtime linkage does not embed Windows/Direct3D or a Git installation.

Repository helper scripts and script-based tests require Python 3.10 or newer. The build helper passes its validated interpreter to CMake; direct CMake configuration selects a compatible interpreter. Unreal's separate embedded Python used by the optional Engine reference adapter is not substituted for this tooling requirement.
