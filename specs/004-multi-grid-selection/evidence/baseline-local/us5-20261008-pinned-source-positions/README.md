# Interrupted H7 attempt

This H7 run was interrupted with exit 130 after the `ORDER BY logical_index LIMIT 4` public-source query began scanning the full projected sample. It produced only partial files; no H7 result or gate status should be inferred from this directory. The follow-up uses a bounded natural scan prefix and checks its source positions explicitly in a separate empty output directory.
