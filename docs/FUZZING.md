# Fuzzing

The Core reads untrusted files. `tests/fuzz/package_fuzzer.cpp` is a coverage-guided libFuzzer target for loose package reading, with AddressSanitizer. It is built only on request, in its own build tree.

## Contract checked

For any input, the harness:

1. opens it from memory as a package and an optional `.uexp` companion (`Package::open(PackageSources)`);
2. produces the semantic snapshot;
3. compares the package with itself;
4. decodes its texture and mesh source payloads with small limits.

5. serializes the self-comparison report (`write_json`) and the package JSON.

A finding is any sanitizer report, a timeout, an out-of-memory, an exception other than `cyuediff::Error`, or a self-comparison that reports changes.

Input layout: `[u32 little-endian n][n package bytes][remaining bytes: .uexp]`. When `n` does not fit, the whole input is the package.

`tests/fuzz/package_diff_fuzzer.cpp` compares two different packages: `[u32 n][n bytes: before][remaining bytes: after]`. It serializes the report and checks the self-comparison of the second package. This exercises array alignment, typed-name correspondence and coverage.

## Findings fixed

- **Non-ASCII narrow strings without an explicit encoding.** `Archive::string()` kept raw high bytes, which became invalid UTF-8 in the package JSON. Every later `dump()` then threw `nlohmann::json::type_error` (316) instead of `cyuediff::Error`. Narrow FStrings now require ASCII when no encoding is selected, as the schema and layout readers already did. The property becomes opaque with reason `string-encoding`. Regression: `string_encoding_tests.cpp`.

## Build (Windows, MSVC 2022 17.9 or later)

```bash
cmake -S . -B build-fuzz -DCYUEDIFF_BUILD_FUZZERS=ON -DBUILD_TESTING=OFF -DCYUEDIFF_BUILD_CLI=OFF -DCYUEDIFF_BUILD_GIT=OFF
```

```bash
cmake --build build-fuzz --config RelWithDebInfo --target cyuediff_package_fuzzer cyuediff_package_diff_fuzzer cyuediff_package_fuzz_seeds
```

Build only these targets: the option instruments Core, and other executables would link it without libFuzzer. Clang builds use `-fsanitize=address,fuzzer`.

At run time, the ASan DLL (`clang_rt.asan_dynamic-x86_64.dll`) must be on `PATH`. It ships in `VC\Tools\MSVC\<version>\bin\Hostx64\x64`.

## Run

```bash
build-fuzz/RelWithDebInfo/cyuediff_package_fuzz_seeds build-fuzz/corpus build-fuzz/corpus-diff
```

```bash
build-fuzz/RelWithDebInfo/cyuediff_package_fuzzer build-fuzz/corpus -jobs=4 -workers=4 -max_total_time=900 -max_len=16384 -timeout=10 -rss_limit_mb=2048 -artifact_prefix=build-fuzz/findings/
```

The seeds come from the synthetic test fixtures: package versions 0 to 1018, inline and split into `.uexp`, plus a UE 5.4 package with arrays of builtin structs, tagged structs, names and object references.

To reproduce a finding, pass the artifact file alone: `cyuediff_package_fuzzer build-fuzz/findings/crash-<hash>`. Each fixed finding gets a regression check in the ordinary test suite.
