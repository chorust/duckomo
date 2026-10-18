# N1/N2 validation refresh

Command package status: **all 27 recorded invocations exited zero**. This is local profile evidence, not an N1 or N2 acceptance pass.

Captured commands, exact runtimes, output hashes, input-manifest hashes, and artifact identities are in `manifest.json`. The native capability matrix covers 368 finite, content-pinned cases across 11 observed rejection classes, but does not establish completeness of all binary64 rejection classes or independently prove that the classes are exhaustive. The GFS025 profile checks 12 targets against exhaustive 1440×721 source enumeration and Decimal distance references. SQL checks cover the registered synthetic profiles and newly added rejection cases, but do not close the full input and supported-grid audit.

The official-value and reference files are independently hash-pinned by `scripts/check-sampling-fixtures.py`; the substitution negative case is in `test/tools/sampling_evidence_test.py`. This package does not modify the historical validator package, task gate checkboxes, or roadmap governance records.
