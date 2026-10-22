# Source provenance and public chronology

Source publication prepared on **2026-10-22 (America/New_York)** for Jimmy6Smiles.
No original private repository history is uploaded and no commit dates are backdated.

## Independently visible release records

The following `published_at` values were read from the GitHub Releases API during
preparation. Times below are UTC, not inferred from local file modification times.

| Public version | GitHub publication time | Record |
| --- | --- | --- |
| Pixel Shift 1.2.4-rc2 | 2026-10-18 05:31:03 UTC | [Release](https://github.com/Jimmy6Smiles/Hasselblad-X2DII-Extensions/releases/tag/pixel-shift-1.2.4-rc2) |
| Pixel Shift 1.2.4-rc3 | 2026-10-20 08:54:49 UTC | [Release](https://github.com/Jimmy6Smiles/Hasselblad-X2DII-Extensions/releases/tag/pixel-shift-1.2.4-rc3) |

These are public release records, not a claim that development began on those
dates or proof of priority over every other developer. They do not establish
that this later source snapshot exactly reproduces either release.

## Export identity

- Development repository revision at export: `b0865f6c8d5fcf6ef0a2ff74e4b2a9808229ca56`.
- Latest commit affecting the exported pixel-shift source subtree:
  `25baf91` (2026-10-21), shared-runtime ownership integration.
- Exported implementation: `firmware/pixel-shift/1.2.2/{src,ui,shared,tests}`,
  selected offline build/test tools and the historic binary hash baseline.
- `SOURCE_SHA256.json` identifies exact source bytes copied in this export.
  It excludes publication prose and itself. Hashes identify content, not authorship.
- Existing validated processing and packaging logic has not been edited for this
  publication. Other modules and uncommitted installer/filter work were not exported.
- The 1.2.2 hash baseline remains historical; it is not substituted with new
  hashes to imply rc4 reproducibility.

## 2026-10-22 firmware adaptation update

The two `auto_prepare.c` copies were synchronized against the frozen
1.2.5-rc3 build-source snapshot for firmware 1.3.18.1. Other exported `src/`
and `shared/` files matched that snapshot. The maintainer reported successful
capture; this does not establish full format/recovery acceptance. Package
identity and build limitations are recorded in [the adaptation note](docs/FIRMWARE_1_3_18_1.md).
The initial export revision above remains historical, not the revision of this update.

## Deliberate exclusions (unchanged)

No installer source/binaries, startup replacement scripts, common runtime hosts,
factory profiles/libraries, full firmware, photographs, device logs, credentials,
private keys, diagnostic dumps, build caches or other MOD implementations.

The original contributor names and applicable third-party licenses remain intact.
Source visibility documents the implementation and allows inspection; it does not
make copying technically impossible or replace the licensing terms.
