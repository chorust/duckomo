# US3 resource and zero-value evidence

> 当前修复后验证见 [2026-10-14 review 回归](review-fixes-20261014.md)：三版本 P0–P4、完整本地回归和每版16/16 sanitizer通过。本文后续为早期历史记录，不代表当前产物的gate状态；旧work字段曾混入区域输入，不用于当前资源结论。

The final local validation passed on DuckDB v1.5.4 (`08e34c44`) with the matching spatial extension. The archived run requested P0, P3, and P4 together and recorded all three as pass. Its manifest is [`local-p0-p3-p4-verified-20261011/manifest.json`](local-p0-p3-p4-verified-20261011/manifest.json), SHA-256 `6ac9b46816a4ec8a117d5ac3138d38a68fbed2ceaaaf1a2d5f765d4d6e31f4a6`. The P3 and P4 report hashes match the manifest; every captured command output hash also verified.

The P3 resource pair used the same five-vertex, 93-byte region WKB (SHA-256 `a14c2d8af9f6dcf96841e94afef57b91126c968142bb4f2e62064a92da44d2a2`) and a fixed 32×32 chunk shape. The spatial point count grew 16× while retaining one matching pair, one result row, and the same value result.

| Probe | Points | Value bytes / chunks | Exact pairs | DuckOMO owned peak / upper bound | Process peak RSS |
|---|---:|---:|---:|---:|---:|
| Small | 65,536 | 421 / 1 | 1 | 549,193 / 641,456 B | 50,040,832 B |
| Large | 1,048,576 | 421 / 1 | 1 | 549,195 / 1,045,806 B | 49,704,960 B |

Owned-work peak grew by 1.000004× and isolated-process RSS by 0.993288×, both within the 2× limit. The reports mark exact and owned accounting complete. Spatial-internal peak memory remains unknown (`null`, `complete=false`); RSS includes DuckDB and extension dependencies. The P3 report is [`P3-resources.json`](local-p0-p3-p4-verified-20261011/P3-resources.json), SHA-256 `8826a53ee7e6173dbf825d9341206a50373bb11166c80aa8325cf6eeeaa81b46`.

P4 also passed its lifecycle and resource bundle. Its paired 16× run measured 1.000004× owned-work peak and 0.994136× process RSS. It covered zero-value I/O and filters, 1/2/4 workers, fallback selection, legacy metrics, input and SQL boundaries, cancellation and recovery, dependency isolation, and ordinary-read regression. The P4 report is [`P4-lifecycle.json`](local-p0-p3-p4-verified-20261011/P4-lifecycle.json), SHA-256 `d33c512b1fd1f89a103f5476d89683e65912c237a4a52f5f5c62be6a6b57e7aa`.

The command was `scripts/validate.sh build/release --local-only --regions --spatial "$PWD/build/regions-runtime/v1.5.4/official/spatial.duckdb_extension" --region-gates P0,P3,P4`. `domain_reference_test` remained not-run because `DUCKOMO_DOMAIN_FILE` and `DUCKOMO_DOMAIN_REFERENCE` were not supplied. Remote P5, cross-version P6, and independent P7 remain unrun.
