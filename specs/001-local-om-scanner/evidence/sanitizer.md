# Native Sanitizer Evidence

Status: passed on Linux AArch64 with GCC 13.3.

| Item | Value |
| --- | --- |
| Command | `make sanitizer-test` |
| Exit | 0 |
| Instrumentation | AddressSanitizer + UndefinedBehaviorSanitizer, `-O1 -g -UNDEBUG -fno-omit-frame-pointer` |
| CTest cases | 4/4 passed |

The sanitizer target builds instrumented copies of the extension's C++ implementation and native test executables, plus a separate instrumented loadable extension for lifecycle tests. It covers batch bounds, raw-reader corrupt-input/oracle behavior, metadata/schema validation, and lifecycle cancellation, corrupt-chunk recovery, and 100 valid/error descriptor checks. The measured native C OM dependency remains the normal uninstrumented pinned library; release-performance evidence is collected separately from these sanitizer runs.

| CTest case | Result |
| --- | ---: |
| `duckomo_sanitizer_batch_test` | pass |
| `duckomo_sanitizer_raw_reader_test` | pass |
| `duckomo_sanitizer_schema_test` | pass |
| `duckomo_sanitizer_lifecycle` | pass |

The sanitizer extension is staged under `build/release/extension/duckomo/test/sanitizer_runtime/`; the release extension remains at its normal path. The staged artifact links `libasan.so.8` and `libubsan.so.1`.
