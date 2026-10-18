# N5 private-use decision

## Operator decision

The project owner confirmed in the interactive acceptance session:

1. The fixed GFS raw object will not be publicly redistributed.
2. Internal validation and private retention are allowed.
3. Authorized subsequent validators can obtain the same file.

This is an owner-provided authorization and access attestation, not an independent legal determination or a grant of public redistribution rights. Existing public-source/license research remains historical evidence. Public redistribution permission is not a prerequisite for this privately scoped acceptance.

## Fixed object checked when recording this decision

- Existing private workspace copy: `build/manual-region-queries/gfs025-20261014T0000.om`.
- Size: 112,068,504 bytes.
- SHA-256: `676742c9fe81dd7af7472c7f41d3651d8bd3ff5bcaf1c47b965d415310472375`.
- The file exists and its size/hash were rechecked by the assistant after the owner's confirmation.
- No raw data was uploaded, published, added to Git or copied to a public archive.

## Acquisition and retention boundary

Authorized validators request the exact object from the owner through the owner's private access process, then verify the size/hash above before running validation. Do not assume the expiring original public URL remains available. Do not place credentials, signed URLs or private storage access details in tracked evidence.

A separate content-addressed private archive has now been created under `/home/blizhan/.local/share/duckomo/private-inputs/sha256/676742c9fe81dd7af7472c7f41d3651d8bd3ff5bcaf1c47b965d415310472375/`. The archive directory is mode 0700, the object and manifest mode 0600. The copy was flushed/fsynced, then copied back to a temporary retrieval location and verified against the frozen SHA-256. Retention is until the owner explicitly authorizes removal. Authorized validators obtain it through the owner; no public link is needed. This is a persistent local archive outside build cleanup, not an independent hardware backup or disaster-recovery guarantee.

## Acceptance effect

The decision resolves the need to seek public redistribution authorization and records the owner's permission for the intended internal use/private retention. The private archival/retrieval requirement now has recorded evidence. N5 execution and N6 audit status are recorded in the subsequent current-artifact package; this authorization decision alone does not promote either gate. Historical real/controlled-transport subchecks remain historical and are not promoted to current-source acceptance by this decision. N6 still requires the complete N0–N5 evidence audit.
