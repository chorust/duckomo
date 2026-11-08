# Slim evidence index (product PR) — 2026-11-07

Host: linux_arm64 (`uname -m` = aarch64). Policy: [submission-scope.md](../submission-scope.md), slice: [pr-slice-proposal-20261107.md](../pr-slice-proposal-20261107.md).

This index lists **in-repo keepers** for the single product PR and **hash pointers** for bulky follow-up artifacts that remain on the local disk only. Hashes below were recomputed on 2026-11-07 from the live files; none were invented. No external archive was created in this step.

## Archive TBD (not created)

| field | value |
|---|---|
| `archive_uri` | TBD — not created yet |
| `archive_sha256` | TBD — not created yet |
| `checksum_manifest_sha256` | TBD — not created yet |
| `retrieve_instructions` | TBD — not created yet |

Historical `run-20260803-linux-arm64/` (~1.18G) stays on disk; the directory is gitignored for product PRs. Do not delete, move, compress, or upload it as a substitute for an archive.

## Claim bounds (do not expand)

- **010**: declared-scope PASS only for linux_arm64 × pinned DuckDB pairs v1.5.4 / v1.5.5 / v1.5.6 / v2.0.0 (follow-up tree). Real data limited to IFS025 two-object one-point one-window; other models synthetic equivalence; N160 synthetic; development source identity, not a published package.
- **008**: **not PASS**. SC-006 relaxed / won’t meet; T042/T043 unchecked.
- Community artifact hashes remain TODO in `community/validation.json`.

## In-repo keepers (stage these)

### Policy / reviews / slice

- `specs/010-read-om-series/evidence/submission-scope.md`
- `specs/010-read-om-series/evidence/pr-slice-proposal-20261107.md`
- `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/evidence-index-slim.md` (this file)
- `specs/010-read-om-series/review*.md`, `review-tree*.md`
- `specs/010-read-om-series/evidence/review-fix-*/fix-note.md` (and small non-log companions under those dirs as staged)
- `specs/010-read-om-series/evidence/code-review/` (SQL / JSON / probe; small `.strace` ok; no bulky internals)

### Current follow-up pointer

- `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/report.md`
- `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/manifest.json` (top consolidated; ~27 KiB — kept in full)
- `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/full-regression-status.json`
- `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/official-httpfs-matrix.json`
- `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/focused-python/commands.json`

### Per-pair corrected-source small files

For each of `v1.5.4`, `v1.5.5`, `v1.5.6`, `v2.0.0` under `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/corrected-source/<pair>/`:

- `build/build-manifest.json`, `build/runtime.json` (not `build.log` — already covered by repo `*.log` ignore / hash below)
- `real/report.md`, `real/manifest.json`
- `official-httpfs/result.json`
- `local/report.md`
- `controlled-remote/report.md`
- `local-corrected/report.md` when present (v2.0.0)

### Historical report-level pointers

- `specs/010-read-om-series/evidence/acceptance-20261106-linux-arm64/report.md`
- `specs/010-read-om-series/evidence/acceptance-20261106-linux-arm64/manifest.json`
- `specs/010-read-om-series/evidence/run-20260803-linux-arm64/report.md` (force-added despite directory gitignore; tree body remains local-only)

### 008 slim evidence

- `evidence/008/performance-gates/**`
- `specs/008-duckdb2-async-io/evidence/review-fix-f008-01-linux-arm64/**`

## Explicitly excluded from this PR (local only)

- Entire `specs/010-read-om-series/evidence/run-20260803-linux-arm64/` body except the force-added `report.md` above (~1.18G ignored).
- Follow-up outer duplicate pair trees `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/v1.5.*` / `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/v2.0.0/` (superseded by `corrected-source/`).
- `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/official-httpfs-fixture/` (fixture internals).
- Follow-up bulky manifests / commands / queries / strace / build logs listed in the hash tables below.
- First-acceptance pair execution trees under `specs/010-read-om-series/evidence/acceptance-20261106-linux-arm64/v*/` (~55M) — only top report/manifest kept.
- `.pi/`, nested `.specify/extensions/roadmap`, `specs/009-point-site-nearest-sampling/`, `docs/research/2026-11-06-enso-impact-scenario-coverage.md`.

## Hash pointers — corrected-source bulky (local only / future archive TBD)

| path | size (bytes) | sha256 | note |
|---|---:|---|---|
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/corrected-source/v1.5.4/controlled-remote/manifest.json` | 23180933 | `41e5a0ccd5457ee22ec4074736dab485c964754ef0b642fb3f4c4bb3d8fe96d2` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/corrected-source/v1.5.4/controlled-remote/commands.json` | 8727282 | `345244b38d5e9275d179f2e83f44e2bec7e51c77ee3b52e682692a035afffe9c` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/corrected-source/v1.5.4/local/manifest.json` | 6159195 | `48470c989d7cc733d0f581b18540b1555c453e871041f339ce0b1c0900ab0550` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/corrected-source/v1.5.4/local/commands.json` | 2575207 | `8d9bab6fc17808b7651a47801e72deaf8c7a466f862e58c34001592504c6b10a` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/corrected-source/v1.5.4/build/build.log` | 195827 | `5ad043f7cd1d211ca2aae9c6c0124ba1ff144ed78bb9f9e7dba7bf075e7ee726` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/corrected-source/v1.5.5/controlled-remote/manifest.json` | 23180725 | `bcbff382b1f641ea2aad858978651fdb75f1dfae0e135b13fe16e78ae86be1ff` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/corrected-source/v1.5.5/controlled-remote/commands.json` | 8726848 | `c7e97a80b33402f61d3981a9e6f949aad9c0589f156c7ddfb5bbb7a2aafab891` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/corrected-source/v1.5.5/local/manifest.json` | 6159035 | `a71966c29bba595872f7e6760d837310693341ef203abfed0a32f4d33ee9124b` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/corrected-source/v1.5.5/local/commands.json` | 2575103 | `59f775343f103ebff41c10f694c563749441356260c3693a28945de1595b1296` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/corrected-source/v1.5.5/build/build.log` | 195827 | `adeab46cf2e59b638f4c781b8be3104deefda90e6d5a08c7c70cacd86e848fb6` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/corrected-source/v1.5.6/controlled-remote/manifest.json` | 23166817 | `c08b5ce3a3685528d91ca1986cdbb3d49c38433a34d7a1ccd33734ad28953d75` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/corrected-source/v1.5.6/controlled-remote/commands.json` | 8727078 | `6dba15c32bdb325998ce85b961f1071c37824a0df8cf75fe936383fde7501a3c` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/corrected-source/v1.5.6/local/manifest.json` | 6158991 | `ae7284565469c94153207efd03749b7ec4a290324489ad221dfacc140acf0b17` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/corrected-source/v1.5.6/local/commands.json` | 2575081 | `9ac48970728b909349caa0dc29045f87382cf97bdd88755863dce28a1c4d2eb1` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/corrected-source/v1.5.6/build/build.log` | 195945 | `d831af3d98c877c6fd5fc370ea6b0b5856576124682555fbcb9c258f5090e133` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/corrected-source/v2.0.0/controlled-remote/manifest.json` | 23175111 | `9dad8ac1bf49dcc6b02a9d56d422b7aa7ecd745daaf0f25d2a82a8c0a4a82273` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/corrected-source/v2.0.0/controlled-remote/commands.json` | 8730144 | `81b2fb8424654e0776b772d31362a547afb1521557dc75a91146fd9763216886` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/corrected-source/v2.0.0/local/manifest.json` | 6164078 | `f5df507366f1891e97307f27ea06b45379ae6a2f0151cd793e4a9f574ab60e9f` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/corrected-source/v2.0.0/local/commands.json` | 2576825 | `8fa109f0194d74a4aea5e545155ad9a1d4c9d72519e32b673ac713370b3231c6` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/corrected-source/v2.0.0/build/build.log` | 293856 | `b0beb9b2da49dba14ff156b9b0c4b0f3445281348f01950685f4671998538025` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/corrected-source/v2.0.0/local-corrected/manifest.json` | 6168034 | `af6fad7aa6c5dc7a6c6173d15a945a04efd2b194b5ac39f172cbd3de8393ca0c` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/corrected-source/v2.0.0/local-corrected/commands.json` | 2579118 | `e558c8a9a6c04334c0a531562f5b2ca7347055f713557fdd2d69edf587babd54` | local only / future archive TBD |

## Hash pointers — outer follow-up pair duplicates (local only; not staged)

| path | size (bytes) | sha256 | note |
|---|---:|---|---|
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/v1.5.4/local/manifest.json` | 6151011 | `6ef53e53c6c7d4112f1fa2379d0438ddfd55b4af43e2353e3d7a43e308156e63` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/v1.5.4/controlled-remote/manifest.json` | 23171779 | `a476893b7cec997d9d908235c3df63c1d9f397561ddb84d177ae39ec0d75eff1` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/v1.5.4/local-corrected/manifest.json` | 6154952 | `e011cee6bb0b6b5e0a64f1825c912a35a37d72005af046bb479949bdaf4fb6a9` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/v1.5.4/local/commands.json` | 2570556 | `6e3e72de629d0f8072c01ed8d9b7061e2c9f639d493c51d20af9ad4457b8aae8` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/v1.5.4/controlled-remote/commands.json` | 8720639 | `77738bdd9809685d68f09db9c1c1e728b7fda5b9b22b92936f3105ecb7d8fcf2` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/v1.5.4/local-corrected/commands.json` | 2572812 | `761b432a4c76c0d1c1cf587ceee7d2e76b563205c405d3ccc79d47e16794ce33` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/v1.5.5/local/manifest.json` | 6150359 | `b6112dfbb17078112a0ba16477e64337f174977e7078ee31f0640952e84c7a30` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/v1.5.5/local/commands.json` | 2570266 | `1350d45d68019ea22a3e45b75f7ab43ab167d851db5d60ef74bfeacac60481cd` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/v1.5.6/local/manifest.json` | 6151002 | `c35b24f90288e56bf390845904c4675001534c785d6e6325669126f7d63a1ed4` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/v1.5.6/local/commands.json` | 2570658 | `b51089559812a076e461c7d7ca40592a31ac04db58f87c49f11759cdc0e85a23` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/v2.0.0/local/manifest.json` | 6155436 | `6636cac53d4aac79f6f87f20efa458e2a73cdb8bc2e848d1c6c11ccc649f93c7` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/v2.0.0/local/commands.json` | 2572153 | `7d98e1bf38f7c3c9a2a0936a2590f27e091e7510769a4327b79f80a7c0339512` | local only / future archive TBD |

## Hash pointers — historical run report/manifest (tree gitignored)

| path | size (bytes) | sha256 | note |
|---|---:|---|---|
| `specs/010-read-om-series/evidence/run-20260803-linux-arm64/report.md` | 59221 | `1603ae82c2e7f1f0d6fcaca091e98a61ea0f702984c5fe3cb5a750408532df05` | local only / future archive TBD |
| `specs/010-read-om-series/evidence/run-20260803-linux-arm64/manifest.json` | 153260 | `a6de366f694565f8412db9d67d30080b8bc3f4be4b8d470d253099e466e09c97` | local only / future archive TBD |

## Notes

- Top follow-up `manifest.json` is **kept** (size 27410 bytes &lt; 5 MiB threshold).
- Controlled-remote manifests (~22 MiB each) and large local(/-corrected) manifests (~6 MiB) are **hash-only** in this PR.
- `commands.json` under local/controlled-remote (~2.5–8.7 MiB) likewise hash-only.
- `build.log` files are listed for traceability; repo-wide `*.log` ignore already keeps them out of git.
