# H1 development-build comparison diagnostic

Recorded: 2026-10-07. This was a diagnostic run with `build/release-vcpkg`; it is not a fixed version-matrix result and does not pass or fail the matrix H1 gate.

The runner used sample manifest SHA-256 `72af60d4769c2fc7b262864f1fb8e4f434e8a89ec463824f482c39103dc1939f`, DuckDB CLI SHA-256 `3747a415e6e9d0b0c24bebab031683e4e7c5762738fe3d049e5d040affb4d015`, and DuckOMO extension SHA-256 `4c64b043fdbeb08e5b51d35030b2bb8efe47c7b9a02a3c6d27d39a50867ca2f2`. The H1 comparison summary SHA-256 was `969954d041dc01d3058924850bd32d2b4902db905709c1d0f2ba9ccf6a85efec`. The full local output is under `build/evidence-h1-dev-20261007-continue3/`.

Available projected-sample comparisons reported:

- Rotated GEM coordinates exceeded the `0.0001°` tolerance at spatial index 977561: `0.00011500519278229149°`.
- CHMI Lambert value comparison reported a source-position mismatch at row 2: `120 != 1`.
- Stereographic GEM coordinates exceeded the `0.0001°` tolerance at spatial index 663385: `0.0001030527648140378°`.
- N160, full N320, and N320-region lacked the required coordinate and full-value references.

The diagnostic command exited 1 with H1 `fail`; full H1 remained `not-run`. These results need investigation against the sample mapping and coordinate definitions before any fixed-matrix H1 run can be interpreted.
