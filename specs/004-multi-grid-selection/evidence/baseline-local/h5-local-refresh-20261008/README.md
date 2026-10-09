# Diagnostic run

This directory records a preflight-only attempt to run `--cases H5`. The gate remained `not-run` with the reason `US2 local checks require core_functions from the same DuckDB build`; no H5 local command was executed.

The runner helper searched `release/repository` for the matching `core_functions` extension, while this build stages it under `release/extension/core_functions`. The discovery helper was corrected to search both locations. The corrected H5 run is written to a separate empty evidence directory; this diagnostic is preserved and is not acceptance evidence.
