# Offline build and verification boundaries

## Host tests

On Linux/WSL with Python 3 and a C compiler:

```sh
python3 tools/test_firmware_122.py
```

These tests use mocks and temporary directories. They do not connect to a camera,
establish clean-camera installation support or validate image quality.

Export check on 2026-10-22: all 280 copied source/test/build files matched their
development-tree SHA-256 values. The host runner passed on WSL Ubuntu 22.04:
15 reboot-recovery scenarios, 18 pre-capture reboot scenarios and 11 host suites.
GCC emitted existing unused-write-result and path-truncation warnings; this is
not a warning-free-build claim. No camera was accessed and no production source
was changed as part of this publication.

## Historical component build

With a separately obtained Android NDK r27:

```sh
python3 tools/build_firmware_122.py --ndk /path/to/android-ndk-r27 --out build/components --candidate
```

This builds four components only: integrated-job, integration-service,
shutter.so and reboot-reconcile. The default flags describe the historical
standalone integration, **not the rc4 registration-only deployment**. Do not
install these outputs into a shared-runtime camera or substitute them into a
signed package. The current sources differ from the historical hash baseline;
`--candidate` reports that distinction rather than pretending binary identity.

Production task entry is `src/coordinator/integrated_job.c`, not the helper
snapshot in `src/service/integrated_job.c`. Recovery uses its own local
`exposure_guard.c`, not the capture directory's version.

## Registration-only rc4

The feature-owned backend and QML sources are included for review. A complete
rc4 build additionally requires the matching common-runtime ABI/client adapters,
album/GUI adapters, native link libraries and accepted rendering profiles. Those
external dependencies and the private package builder are not bundled here.
The development builder currently also reuses accepted imaging binaries from an
earlier MOD; a full reproducible source build of that payload is not claimed.

The installer alone owns factory startup configuration. Do not restore old init
scripts as a shortcut to making this source export installable. See the agreed
development rules; its dated progress section is historical, while its ownership
contract remains applicable.
