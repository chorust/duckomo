# 005 P5/P7 cost acceptance amendment — 2026-10-20

The owner selected option 1 in the current session: permit equal bytes after decoder range reuse. The owner confirmed “1”. This amendment supersedes the earlier requirement that each of all three cost measures strictly decrease for every HTTP/S3 target. It applies to current P5, P6 audit and independent P7 interpretation.

For every frozen target's HTTP and signed-S3 baseline/region pair:

- Value data bytes, decoded blocks, and independently audited service-sent body bytes must each be known, complete and no greater for the region query than the baseline.
- At least one of the three measures must strictly decrease for each pair. All three equal fails; any increase fails even if another measure decreases.
- Five objects, polygon WKB, larger window, projected columns, full axis coverage and exact SQL remain frozen. Result equality, source positions, NULL/multiplicity, cold client settings, fresh matching processes, three-version identity and independent verification requirements are unchanged.
- No cancelled full-domain baseline is reinstated. No general throughput or client-received transport-byte claim is added.

Reason and retained failure: the current-source development P5 preflight under the original rule is retained at `build/evidence/regions/current-20261020/dev-P5-ready/`. NCEP baseline/region both require 8,620 value data bytes and 13,432 service-sent bytes while decoded chunks decrease 135→45. Existing decoder range reuse improved the baseline to the same necessary compressed bytes. Disabling that optimization to manufacture strict byte savings would misrepresent current behavior.

The earlier expired-certificate failure and fixture-not-ready result are separately retained in `dev-P5/` and `dev-P5-tls-fixed/`. A new loopback certificate is used with verification enabled. Revised runs use new output directories and declare `savings_policy=non_increasing_all_strict_one_20261020`; historical reports and their policies are not rewritten.
