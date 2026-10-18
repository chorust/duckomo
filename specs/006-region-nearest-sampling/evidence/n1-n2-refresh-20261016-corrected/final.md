# Corrected N1/N2 validation refresh

Status: **N1 blocked; N2 blocked.** This supplemental rerun corrects the capability oracle boundary-case name and records the current pinned fixture identities. It does not modify historical evidence packages.

## Oracle correction and identity

The named capability case previously called `reject-latitude-above-domain` has inputs `lat0=89.0`, `dlat=1.0000000000000002`, `ny=2`. Under the contract's binary64 coordinate rule, `89.0 + 1.0000000000000002` rounds to exactly `90.0`, so the inclusive endpoint is valid and the case is accepted. The reference generator and native comparator now call it `accepted-latitude-upper-endpoint`.

The capability matrix still has 368 rows: 201 accepted and 167 rejected across 11 rejection classes. Current identity pins are:

- capability reference CSV: `e9f8e62a5fe4f2b54626acad3f7d943e6cfd570d7abf4f2dcfe3addbbe626b22`
- reference manifest: `e01488fd341f0086baaa099fc03dd4d073b24e01d91b6271f51455ff7ca7003b`
- locator reference CSV: `9b8d49d3bf61543c1e385a3dff9beb764340f25331cfee6e556a174d7f94558d`
- scientific reference CSV: `d835001d8c6d1c61768ee70a3872fe6a9f8011969cc8b7dc5636db953fa411db`
- GFS025 profile CSV: `b5b0724444d4d57fb42510309042d7dbe36631617a488ff1464559baa6f4485a`

The rebuilt locator native binary passed on the pinned v1.5.4, v1.5.5 and v1.5.6 build trees. Fixture verification passed for nine geometries, 62 locator rows, 16 OM fixtures and 29 variables. The evidence checker passed 12 tests, including external-pin substitution negatives.

## Cross-version SQL/native execution

The complete corrected validator run is archived in [`validator-run`](validator-run/). It records 22 command invocations: seven N1 locator/geometry/layout commands and 15 N2 SQL/native commands. Every recorded command exited zero. N2 commands include the selected nearest SQL matrix, points compatibility SQL, both spatial-dependency SQL profiles and native scan parity, on all three pinned DuckDB/spatial runtime pairs. The archived manifest reports overall `blocked`, with N1 and N2 each `blocked`. Each command output file is retained and was rehashed against its manifest `output_sha256`.

Run identities:

- [`inputs.json`](validator-run/inputs.json) SHA-256: `81479c32ce7bed9d666941211e21d38d2e75e84587e50b005f645b4847f03c1f`
- [`manifest.json`](validator-run/manifest.json) SHA-256: `458c81ee6f99a71d572ff972602c05f638a237347b8e95b04badbf52f7f22c13`
- [`commands.jsonl`](validator-run/commands.jsonl) SHA-256: `3a3fd3b559f7f2edafe88facdf9be8c98541bf83dded2911700d4b25b2fb855c`
- [`final.md`](validator-run/final.md) SHA-256: `cd6b9761aec87f9585f883ea625b431a8a040d54c3bd14eb35ed3a433a671b20`

The local SQL and native profiles pass. The run manifest correctly retains N1 and N2 as blocked because their full contract and profile inventories remain open. This corrected refresh preserves the historical packages unchanged; the run artifacts are now archived in this directory.

## Supported-profile audit result

The source-derived registry declares 68 regular-domain names. Applying the current numeric qualification conditions yields 65 nominally eligible names (31 global, 10 latitude-band and 24 regional) and three profiles explicitly configured to allow out-of-range latitude, which nearest rejects. The 65 names collapse to 31 distinct numeric coordinate profiles (five global, nine latitude-band and 17 regional). Per-name classifications and computed count summaries are in [`registered-domain-audit.json`](registered-domain-audit.json), SHA-256 `3293247477adb515cd5567d7c47192bcd317da4568fbf03b0373731b4cbd9e20`, computed from `src/grid/domain_registry.cpp` SHA-256 `69cb698878afa042fb2f1af99fcfcc100b2a2661e557c133f2dd6061cb65cdff`.

This is historical inventory evidence, not the acceptance profile list. The user has explicitly scoped per-profile acceptance to synthetic regular spherical grids and NCEP GFS025; the other registered domains are excluded from the acceptance claim. Typed projected and reduced-Gaussian definitions remain unsupported and are rejected by family; formula-only records and producer definitions are not treated as regular-grid candidates. The archived run predates the scope decision and retains its original blocked reasons and status. Subsequent contract mapping found finite N1 qualification/oracle gaps and N2 SQL input-contract partitions still to address; see the current `N1-locator.md` and `N2-sql-matrix.md` reviews.
