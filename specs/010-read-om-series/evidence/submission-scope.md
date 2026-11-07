# Evidence submission scope

The 010 evidence set contains three immutable run histories: the historical `run-20260803-linux-arm64/` tree, the first 2026-11-06 tree `acceptance-20261106-linux-arm64/`, and the current follow-up tree `acceptance-20261106-followup-linux-arm64/`. The follow-up report and manifest are the current evidence pointer; the two earlier reports remain historical snapshots and must not be presented as current status.

The historical `run-20260803-linux-arm64/` tree is a local evidence source, not a raw product-PR payload. It contains 5,078 files and 1,181,103,584 bytes (about 1.18G). Preserve it and its failed, blocked, and superseded records; **do not include this raw tree in a product PR**, and do not rewrite its history to make a summary look cleaner. Preserve the first and follow-up 2026-11-06 trees as well; their reports/manifests retain distinct run identities and source bindings.

## Product PR contents

Keep the reviewable evidence set small and readable:

- The current follow-up report with declared-scope gate outcomes and limitations.
- A slim manifest identifying the source revision, host/platform, pinned engine pairs, inputs, and artifacts.
- Input, source, and artifact hashes, plus commands needed to reproduce the reported checks.
- Representative failure and fix records, including enough paths and hashes to trace each summary back to its detailed run evidence.
- Snapshot-specific historical outcomes for the first 2026-11-06 and 2026-08-03 runs; do not carry their blocked/intermediate statuses forward as the current outcome or omit unresolved scope limits.

The current report is [the 010 follow-up report](acceptance-20261106-followup-linux-arm64/report.md), with [its manifest](acceptance-20261106-followup-linux-arm64/manifest.json). Product-PR hash pointers for bulky local-only follow-up artifacts are in [evidence-index-slim.md](acceptance-20261106-followup-linux-arm64/evidence-index-slim.md). The [first 2026-11-06 report](acceptance-20261106-linux-arm64/report.md) and the [2026-08-03 run report](run-20260803-linux-arm64/report.md) remain historical; each describes its own source snapshot and execution state. The separate F008-01 follow-up remains under the 008 evidence tree.

## External archive contents

Full execution history, repeated expanded manifests, detailed command output, large logs, traces, and intermediate run artifacts belong in a separate archive with a checksum manifest. A product-PR evidence index should provide the immutable archive location, archive SHA-256, checksum-manifest SHA-256, and retrieval instructions. The archive must retain failed and blocked records and preserve relative paths so the report and slim manifest remain traceable.

No archive was created or uploaded as part of this documentation fix. Do not delete, move, compress, upload, or rewrite the historical run tree as a substitute for this policy.

## Local agent and roadmap files

Keep `.pi/` local agent files and the nested `.specify/extensions/roadmap` configuration out of a DuckOMO product PR. Their exclusion from product scope does not authorize deleting them; preserve the local files in the working tree.

This policy describes evidence packaging only. It does not claim signed community-install evidence, artifact hashes that are still marked TODO in `community/validation.json`, or new multi-platform execution evidence.
