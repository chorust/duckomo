# N1/N2 scoped acceptance refresh — 2026-10-19 (final source snapshot)

Status: **pass**

Runtime: DuckDB v1.5.4 package; matrix commands exercised v1.5.4, v1.5.5, and v1.5.6.

## Gate disposition

- **N1: pass** — synthetic regular spherical grids and NCEP GFS025 scope. Seven commands passed across the three pinned builds. The contract uses a finite representative capability matrix and fixed target sample; it does not require arbitrary binary64 or full target-space exhaustion.
- **N2: pass** — same profile scope. Twenty-four SQL/native/inherited-parser commands passed across all three runtimes. Four precedence edges and the isolated aggregate-WKB cap are covered. Exhaustive parser and precedence cross-products are outside the declared contract.
- N5 (real/remote inputs), N6 (full version audit), and N7 (independent reproduction) remain incomplete; overall 006 acceptance is not complete.

## Pinned inputs and source

- Capability reference: `41a82525730b52ea8d1c7fc98bd1435cab8e33a02aa233e93667d3d311154e39`
- Reference manifest: `08798b8365a1b03591865477273049807f2a50b4b50556ff804a62dffd5e8cfd`
- N1 comparator source: `f76f9904fd9cc3b17b81b595e8d2168b26204d428744c322f6e11142ad2af4b9`
- N2 parser test: `d519ab7c368f297d3bb4d6a24da4bcbaaa5c63431b312688988d993d2829934b`
- N2 SQL test: `2acb08e3d5cf9f320554dfaab336ca111ce8990e1acfa2ca558af67fb024aa57`
- Runtime matrix: `159c00b3aff2c90d8ff10d0522e9488e998ee4f71375be3f49fc4ff43d786753`
- Commands: `fa8cb7f6995956436e4dbd57984edd8f43e6acf6495caa134210f756c4d6d848`
- Inputs: `bf1697a356511ea7d96424126fe860db441fda1f9fe5f7f817ce033918f7fb5a`
- Manifest: `8e322313bcbe5b53d9363ed6722991d6a6945fe72c8bd532dd019881650fde8f`

## Command counts

- N1: 7, all exit 0
- N2: 24, all exit 0
