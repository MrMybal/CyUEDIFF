# Fuzzing

The Core reads untrusted files. Four coverage-guided libFuzzer targets with AddressSanitizer exercise it. They are built only on request, in their own build tree, and run briefly in CI on every push.

## Targets

| Target | Input | Exercises |
|---|---|---|
| `package_fuzzer` | `[u32 n][n package bytes][remaining bytes: .uexp]`; when `n` does not fit, the whole input is the package | loose package reading from memory, semantic snapshot, self-comparison and its report, texture and mesh payload decoding |
| `package_diff_fuzzer` | `[u32 n][n bytes: before][remaining bytes: after]` | comparison of two packages: array alignment, typed-name correspondence, coverage, report serialization |
| `zen_fuzzer` | `[u8 profile][Zen export-bundle bytes]`; bit 0 selects UE 5.5, else 5.3 | cooked Zen package parsing, snapshot, self-comparison |
| `container_fuzzer` | `[u8 kind][body]`; bit 0 clear: a `.pak`; set: `[u32 n][n bytes .utoc][remaining bytes .ucas]` | Pak and IoStore indexes, member reads, self-comparison. Containers are opened by path, so each input is written to a per-process temporary folder |

A finding is any sanitizer report, a timeout, an out-of-memory, an exception other than `cyuediff::Error`, or a self-comparison that reports changes.

## Findings fixed

- **Non-ASCII narrow strings without an explicit encoding.** `Archive::string()` kept raw high bytes, which became invalid UTF-8 in the package JSON. Every later `dump()` then threw `nlohmann::json::type_error` (316) instead of `cyuediff::Error`. Narrow FStrings now require ASCII when no encoding is selected, as the schema and layout readers already did. The property becomes opaque with reason `string-encoding`. Regression: `string_encoding_tests.cpp`.
- **Non-ASCII narrow container strings.** Pak and IoStore readers kept raw high bytes in narrow FStrings and fixed compression method names, with the same failure in container reports. Unreal stores non-ASCII text as UTF-16, so these strings must be ASCII; otherwise opening fails with `container-string-encoding`. Regression: `container_tests.cpp`.

## Build

Windows, MSVC 2022 17.9 or later:

```bash
cmake -S . -B build-fuzz -DCYUEDIFF_BUILD_FUZZERS=ON -DBUILD_TESTING=OFF -DCYUEDIFF_BUILD_CLI=OFF -DCYUEDIFF_BUILD_GIT=OFF
```

```bash
cmake --build build-fuzz --config RelWithDebInfo --target cyuediff_package_fuzzer cyuediff_package_diff_fuzzer cyuediff_zen_fuzzer cyuediff_container_fuzzer cyuediff_package_fuzz_seeds
```

Build only these targets: the option instruments Core, and other executables would link it without libFuzzer. Clang builds use `-fsanitize=address,fuzzer` (CI: Clang 18 with `libclang-rt-18-dev`).

At run time on Windows, the ASan DLL (`clang_rt.asan_dynamic-x86_64.dll`) must be on `PATH`. It ships in `VC\Tools\MSVC\<version>\bin\Hostx64\x64`.

## Seeds and runs

The seeds come from the synthetic test fixtures:

```bash
build-fuzz/RelWithDebInfo/cyuediff_package_fuzz_seeds build-fuzz/corpus build-fuzz/corpus-diff
```

```bash
build-fuzz/RelWithDebInfo/cyuediff_zen_fuzzer --write-seeds build-fuzz/corpus-zen
```

```bash
build-fuzz/RelWithDebInfo/cyuediff_container_fuzzer --write-seeds build-fuzz/corpus-container
```

- **Packages:** versions 0 to 1018, inline and split into `.uexp`, plus a UE 5.4 package with arrays of builtin structs, tagged structs, names and object references.
- **Pairs:** value changes, reordered names and identical packages.
- **Zen:** UE 5.3 and 5.5 bundles, with and without versioning, original and reordered names.
- **Containers:** seven Pak layouts (plain, compressed, version 10, wide, single, unsafe paths, no directory) and IoStore TOCs for a package and for script objects.

A campaign, for example:

```bash
build-fuzz/RelWithDebInfo/cyuediff_package_fuzzer build-fuzz/corpus -jobs=4 -workers=4 -max_total_time=900 -max_len=16384 -timeout=10 -rss_limit_mb=2048 -artifact_prefix=build-fuzz/findings/
```

CI runs each target for two minutes from the seeds and uploads `fuzz-findings` when a run fails. Longer local campaigns keep their corpus in `build-fuzz/` (ignored by Git) and reuse it.

To reproduce a finding, pass the artifact file alone, for example `cyuediff_package_fuzzer build-fuzz/findings/crash-<hash>`. Each fixed finding gets a regression check in the ordinary test suite.
