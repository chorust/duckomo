# H0 registry/query regeneration audit

Query generation: **pass**.

Generated sample-view binding: **pass**.

Full H0 coordinate/value oracle: **not-run**. The frozen input currently lacks required N160/N320/N320-region producer objects and independent coordinate references. ECMWF HRES O1280 is retained as supplemental value-only evidence.

Reason: two sample-query generations match byte-for-byte, checked-in outputs pass --check, and all generated sample views bind with the supplied DuckDB CLI/extension pair; full H0 coordinate/value oracle remains not-run
