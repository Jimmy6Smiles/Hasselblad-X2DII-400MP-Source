# Hasselblad X2D II — 400 MP Pixel Shift Source

English | [简体中文](README_CN.md)

Maintained by **Jimmy6Smiles**. Source publication for the unofficial six-frame
400 MP pixel-shift extension for **Hasselblad X2D II 100C, firmware 1.3.16.2**.
This is not an official Hasselblad product and is not intended for X2D or X1D.

## What the extension does

- Six-frame pixel-shift acquisition and in-camera 400 MP reconstruction.
- RAW, JPEG and RAW+JPEG output; HEIF is not supported.
- Native-rendering adapters, file packaging, album integration and assisted zoom/pan playback.
- Mode UI, initial delay, capture-state handling, recovery and source-frame cleanup.
- Shared-runtime integration: feature registration rather than ownership of factory startup files.

The maintainer has tested these features on a development camera. This is not a
guarantee for every lens, storage configuration or third-party MOD combination.
Experimental firmware extensions can cause failed captures, unresponsiveness or
data loss. Do not use them for irreplaceable work.

## Source and build scope

The directory `firmware/pixel-shift/1.2.2` is a **historical directory name**, not
the version number of every file. This snapshot includes subsequent compatibility,
recovery and registration-only 1.2.4-rc4 integration changes.

| Area | Source |
| --- | --- |
| Capture coordination | `src/coordinator/integrated_job.c` |
| Resident state machine | `src/service/integration_service.c` |
| Shutter and half-press handling | `src/shutter/shutter_bridge.c` |
| Reboot recovery | `src/recovery/reboot_reconcile.c` |
| Six-frame reconstruction | `src/merge/` |
| Native rendering / JPEG | `src/native-renderer/`, `src/native-codec/`, `src/native-join/`, `src/jpeg-pack/` |
| Playback | `src/zoom/`, `src/zoom-parser/` |
| UI and feature-owned backend entry | `ui/`, `shared/` |

Paths in the table are relative to `firmware/pixel-shift/1.2.2/`.
Some source directories intentionally contain historical helper snapshots. Do not
choose a similarly named file from another directory as a build replacement.

**This is a source snapshot, not a self-contained installer or a bit-for-bit
reproduction of the complete rc4 MOD.** Installer sources, common-runtime hosts,
factory libraries/profiles, firmware, camera data and binary packages are not
included. The rc4 builder also reuses previously accepted imaging binaries; this
publication does not establish a reproducible source-to-binary mapping for every
one of those binaries. See [build boundaries](docs/BUILD.md).

The registration-only installer/MOD combination was reported working by the
maintainer on 2026-10-21. That device test is distinct from rebuilding this export.
Use the [existing distribution repository](https://github.com/Jimmy6Smiles/Hasselblad-X2DII-Extensions/releases)
for version-specific binaries and warnings. Its historical packages and the new
registration-only runtime are not interchangeable.

## Attribution and chronology

This project adapts ideas and code from the Hasselblad Feature Extensions /
Hasselblad Enhancement Research projects. Existing contributor notices and MIT
notices are retained. We do **not** claim invention of pixel shift or exclusive
authorship of upstream algorithms.

[PROVENANCE.md](PROVENANCE.md) records public release timestamps, this snapshot's
origin and file digests. These records document this implementation; they do not
prove universal priority over other projects. This repository starts with an
honestly dated source-publication commit, not a backdated reconstructed history.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for licensing boundaries.
Public visibility alone is not a blanket open-source license for unlicensed files.

## Development contract

The installer alone manages the common runtime and factory startup integration.
Feature MODs register only their own components and declare ABI dependencies.
They must not overwrite another feature or independently restart a shared capture
service. See [the agreed rules](docs/DEVELOPMENT_RULES_CN.md).

Please report reproducible issues without uploading credentials, private camera
logs, photographs, full firmware images or proprietary libraries.
