# US1 local evidence

Status: **synthetic correctness checks passed; complete P1 not-run**. The five frozen real targets have not yet been compared with full `read_om` results.

Runtime: DuckDB v1.5.4, Linux AArch64; DuckOMO extension SHA-256 `569f4c0eaddee3e7a2d88b28a836de4757edf522eac62ba92ade24c6857f02d5`; matching official spatial package SHA-256 `af2547875834496f3112261631cc8d8c96ae67912edab6b4e3d5f4f7545e547a`.

The validator run is `build/evidence/region-validator-YPVK1J/`. Its P0 checks passed: C++ adapter/worker-state checks, the ordinary `read_om.test` process without spatial (110 assertions), the isolated missing-spatial dependency test (5 assertions), and `regions.test` (77 assertions). The synthetic P1 checks also passed: strict input native tests, multi-window region scan tests, and SQL row semantics. The validator keeps P1 `not-run` because it does not yet execute all five frozen real-target coordinate and value comparisons.

The teaching fixture has 438 expected region-record pairs. `regions.test` checks both `EXCEPT ALL` directions against the complete `read_om` result and reports zero differences. The frozen independent membership oracle is `test/data/regions/regions-membership.reference.csv` (SHA-256 `5d2777324982402abfe6aad9f9dc1b8df4ec93c153dcfe45afe5f761108c59b4`). Supplementary checks cover duplicate coordinates at distinct source positions, duplicate region IDs, overlap, holes and boundaries, MultiPolygon, an empty middle region, reordered axes, multiple value columns, long windows, and no-time input.

The validator run records exit codes and output hashes for every synthetic command. Its overall exit code is 2 because complete P1 is still `not-run`; this is not a correctness failure.
