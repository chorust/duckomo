# 005 Linux AArch64 version matrix

> 当前修复后验证见 [2026-10-14 review 回归](review-fixes-20261014.md)：三版本 P0–P4、完整本地回归和每版16/16 sanitizer通过。本文后续为早期历史记录，不代表当前产物的gate状态；旧work字段曾混入区域输入，不用于当前资源结论。

Evidence run: 2026-10-12. The three isolated builds used the same DuckOMO source snapshot (`extension_source_sha256=7713f740908a2ab595f64b127733eefe698469acf5033869dcaecc015686c05e`). The official engine, OM C, CI tools, and spatial runtime pins remain version-specific as recorded in `test/data/regions/runtime-matrix.json`.

| DuckDB | Build ID | DuckOMO artifact SHA-256 | Build and 16 ASan/UBSan checks | SQL/native/Python regression | P0–P4 | P5 |
|---|---|---|---|---|---|---|
| v1.5.4 | `c7072702e601649375077619b5d55f8abfa79d034d11f06d6c69402fd95855b2` | `05ad213929eb13d76276b0992e9580fab5fb1f076167bcb1c575fff01f2d3a0c` | pass; 16/16 | pass; `domain_reference_test` not-run because `DUCKOMO_DOMAIN_FILE` and `DUCKOMO_DOMAIN_REFERENCE` were not provided | pass | not-run |
| v1.5.5 | `032dbd1de8ae2af817e2bdecabf23a1a6f2f30d40beb3b6fb8a35c8c2c00e55f` | `a97d55dc0c8e8b2ce3c3c39259d9d5dba9fb150ac789976f432cf42a234a2f32` | pass; 16/16 | pass; `domain_reference_test` not-run because `DUCKOMO_DOMAIN_FILE` and `DUCKOMO_DOMAIN_REFERENCE` were not provided | pass | not-run |
| v1.5.6 | `99891269f32c6c78985747ad25afdea2d097a89a5c68aa4c3b7bd5baa69127d3` | `e423fadc5057e263634ff88ff3e51beb74b7dc21d9a4bfa6ac25a48ea7631007` | pass; 16/16 | pass; `domain_reference_test` not-run because `DUCKOMO_DOMAIN_FILE` and `DUCKOMO_DOMAIN_REFERENCE` were not provided | pass | not-run |

P0–P4 evidence was recorded with the official CLI, the matching DuckOMO build, and the spatial package whose source identity and SHA-256 are pinned in the runtime matrix. P6 recomputed the manifests and hashes and found no evidence errors. P5 was explicitly recorded as `not-run` for all three versions because no generated remote `run.env` or controlled-service log directory was available. This leaves the full P0–P5 version matrix open.

| Version | P0–P4 manifest SHA-256 | P5 manifest SHA-256 | spatial artifact SHA-256 |
|---|---|---|---|
| v1.5.4 | `12094b25c9224336eb994e82946a404b39c393017826e057ba374295a12b96e4` | `e691d0493217b5691966c74c3d9c6cd48de541d3b3e430365fe6bb4d0e062c68` | `af2547875834496f3112261631cc8d8c96ae67912edab6b4e3d5f4f7545e547a` |
| v1.5.5 | `f59c116549c525ea6ae98a3fa76f14944f219055b98d9b7e837fb79c50f85a6c` | `79aab79bba93b7f29f11d173a037c5bf5bd6e3bf6a79a1ea9c21f0e9bb5d39ec` | `b9129a7da7fde8eb7fb951d99a115cbf2e6fa34c8d41671086169174a1f5725d` |
| v1.5.6 | `2fe814f81f0f9f3102a0b144a09bb95cd6c44b1d22ef0bfe9235499012ed8883` | `c0f77f2f8039a67e8050ee549a05364b5f37c91fa193a7e57a1125dfc95daa0d` | `96b8d4b5d735e9ebe91da7badee4328dfbd07068ce4885d1d9b6de8c22370b14` |

Frozen input hashes used by P6:

- `test/data/regions/manifest.json`: `14253140fa569b1b8ae7f69911a66103db16bc98651566eee8be7aebf7ff9a24`
- `test/data/regions/real-targets.json`: `03c2bcc84b4a0081cbf636d0aace7347b5039eb45ad6bc6a132cf78d9988fd80`
- `test/data/regions/runtime-matrix.json`: `d6b3d9d9211c03d6b3c9efaa22036577eefe366fbf4788734bcec0d3912c27ff`

Raw build, test, and gate records are under `build/evidence/regions/p6-20261012-final/<version>/`. The P6 audit record is under `build/evidence/regions/p6-audit-20261012-final-r2/`. P7 remains a separate independent reproduction.
