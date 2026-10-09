# Evidence-audit diagnostic run

The H3 and H7 local subchecks both completed with `local_synthetic_status=pass`, including the new H3 public source/info checks. The top-level manifest audit returned exit 1 because the H3 audit record used `public_source_command` while the evidence contract expects the normalized `command` field. This directory is retained as a diagnostic only; its top-level evidence audit failed and it is not an acceptance record. The corrected runner was rerun to a separate empty output directory.
