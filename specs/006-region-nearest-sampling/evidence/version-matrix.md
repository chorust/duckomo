# Feature 006 current three-version validation

Current Linux AArch64 artifacts were rebuilt with `scripts/sampling_build.py` and recorded under the existing matrix release locations for DuckDB v1.5.4, v1.5.5 and v1.5.6. Existing directory build IDs are allocation identifiers; they are not substituted for the freshly recorded source digest/artifact hashes in `sampling-build-manifest.json`.

The final execution package is `build/evidence/sampling/private-n0-n5-final/`. Its `inputs.json` records three complete build identities, including current extension source SHA-256, exact DuckDB source commits, build manifest SHA-256 and hashes for the native checks, SQL runner and extensions. The default v1.5.4 build also records the instrumented sampling test and sanitizer extension.

Each version executes the registered N0–N5 scope: frozen points rows/logical costs and physical reuse ceilings; locator/scientific/capability references; SQL/input/compatibility; projection/residual filters and I/O; worker/lifecycle and isolated memory scaling; real and synthetic private controlled-transport comparisons. The v1.5.4 sampling ASan/UBSan registration is bound to the verified matching spatial package. N5 additionally records CLI, spatial and official HTTPFS identities for every command.

No older successful binary run is treated as current-source proof. Raw GFS objects and credential-bearing service configuration remain outside tracked evidence. Exact current results and output hashes are in the package manifest. N6 separately audits this completed package; this report does not close N7 or overall acceptance.
