# Targeted review fix: P2 documentation, checklist, and evidence scope

Host platform: `uname -m` returned `aarch64` (`linux_arm64`). This note covers only F010-01, FCOM-01, F009-01, and FMISC-01 from `review-tree.md`.

## Files changed

- `README.md`
- `README.en.md`
- `docs/spec.md`
- `docs/community-extensions.md`
- `community/duckomo/description.yml.in`
- `specs/009-point-site-nearest-sampling/checklists/requirements.md`
- `specs/010-read-om-series/review-tree.md` (additive fix-status pointers; original findings and recommendations remain)
- `specs/010-read-om-series/evidence/submission-scope.md`
- This fix note

## Finding verification

### F010-01 — public validation wording

Compared the public wording with the final-source reconciliation in `evidence/run-20260803-linux-arm64/report.md`, `tasks.md`, and `evidence/review-fix-linux-arm64/fix-note.md`. The edited guides state that final-source linux_arm64 cases ran for DuckDB v1.5.4, v1.5.5, v1.5.6, and v2.0.0; S0, S1, and S3–S7 passed for those cases; S2 / SC-002 / SC-007 remain Blocked. They limit real producer evidence to two adjacent IFS025 `temperature_2m` chunks at one domain point, identify N160 verification as synthetic, and keep `read_om_series` development-source-only rather than a released feature identity. The DuckDB 2.0 wording now matches the existing prerelease/compatibility wording in release and community documentation; the Feature 004 validation exclusion is explicitly scoped to that acceptance result.

No historical run log or report was rewritten, and no platform or production-grid support claim was added.

### FCOM-01 — community descriptor generation

Moved both placeholders into the actual YAML fields: `extension.version` and `repo.ref`. The documented procedure now substitutes both, parses the rendered YAML with PyYAML, and asserts both parsed values.

Executed the generation and parse assertions with PyYAML 6.0.3, `DUCKOMO_COMMUNITY_VERSION=0.3.5-rc.2` as an example target, and `git rev-parse HEAD` value `80ebe4bac2fa7b2fc29fd1c44cb55985baa6b9f9`. The parsed descriptor reported:

- `extension.version`: `0.3.5-rc.2`
- `repo.ref`: `80ebe4bac2fa7b2fc29fd1c44cb55985baa6b9f9`

This confirms the new SHA lands in the YAML `repo.ref` field, not only in a comment. The generated descriptor is a local procedure check, not a submitted or published community upgrade. The currently published ref remains `55d66f3fbd3199d7471b8cd569c43cc91a8cec7e`. `community/validation.json` still marks artifact hashes TODO. No artifact hashes, signed-install results, or multi-platform install evidence were invented or claimed.

### F009-01 — readiness checklist

The spec still has exactly one `[NEEDS CLARIFICATION]` marker in User Story 10 concerning `legacy_v2` / `legacy_v3`. The checklist now leaves both “No markers remain” and “Requirements are testable and unambiguous” unchecked and records that single unresolved decision. The spec status remains Draft. No plan or tasks were created, and no point-site or summary API was implemented.

### FMISC-01 — evidence submission policy

Added `evidence/submission-scope.md` with product-PR include/exclude guidance and a checksummed external archive policy. It requires readable final outcomes, a slim manifest, input/source/artifact hashes, rerun commands, representative failure/fix evidence, and traceable Blocked records in the PR; full history and large logs belong in an external checksummed archive referenced by an index. It also keeps `.pi/` and `.specify/extensions/roadmap` out of product PR scope without deleting local files.

The historical tree measured 5,078 files and 1,181,103,584 bytes in this session, matching the size recorded in the original review. No operation targeted that tree: it was not deleted, moved, compressed, uploaded, or rewritten. No external archive was created.

## Not performed

- No full 008 or 010 suite was run. This note does not claim 008 PASS or 010 PASS; S2 / SC-002 / SC-007 remain Blocked.
- SC-006 remains relaxed and does not meet the original >=20% gate; T042/T043 remain unchecked.
- No production code was changed. The F008-01 `planned_cache` fix and `specs/008-duckdb2-async-io/evidence/review-fix-f008-01-linux-arm64/` evidence were left untouched.
- No other architecture runner was used.
- No evidence was deleted. No commit, push, amend, rebase, reset, or stash was performed.
