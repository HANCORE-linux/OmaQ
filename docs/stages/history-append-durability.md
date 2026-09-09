# History append durability — unreleased

Base: `33f3b8d5b84b34e7a96dbd373286487de9afedd3`; branch `fix/history-append-sync`.

## Behavior

`omaq_store_append` checks writing, `fflush`, file `fsync`, and `fclose`. After closing, it syncs the conversation directory, history directory, and existing home directory, bottom-up. Only then does it publish the message ID to the in-memory index and return success. Directory access uses the existing descriptor-based no-follow traversal.

All three directories are synced on every successful append. A prior failed directory sync can leave an existing but uncommitted name, so testing only whether a directory was just created would miss retries. This covers first history creation and the existing 2 MiB rotation rename without changing paths, JSONL format, permissions, or retention.

Failure may leave complete or partial bytes and may occur after rotation. It is not rollback or permission to resend automatically. This change does not make Ratchet state and history transactional, alter rewrite/clear operations, encrypt local history, or establish remote delivery. Success relies on the filesystem and device honoring sync requests; the tests are not power-loss certification. The home directory is an existing caller-owned prerequisite.

## Verification on 2026-09-09

- `make PKG_CONFIG=false tests/store_append_test` and `ASAN_OPTIONS=detect_leaks=0 ./tests/store_append_test`: passed with AddressSanitizer and UndefinedBehaviorSanitizer. LeakSanitizer is disabled because this environment does not support its process inspection.
- Covers append, first creation, permissions, child-process reopen, rotation, descriptor closure, and 15 injected failures/retries: write, flush, file sync, close, each of three directory syncs on fresh/existing history, plus directory sync failure after rotation. Failure tests model loss of unconfirmed bytes; they do not model an actual filesystem crash.
- The same focused test compiled against the base `store.c` fails its successful-append sync assertion, demonstrating regression sensitivity.
- `make PKG_CONFIG=false arch` and `./tests/no-signal-build.sh`: passed.
- Full `make PKG_CONFIG=false test`: blocked at existing avatar decoder-dependent unused-function errors with image development dependencies unavailable.
- `make PKG_CONFIG=false helper`: blocked by required Signal dependency. `phase2.sh` and `phase8.sh`: report no helper.
- `qmllint`, `omarchy`, and `pkg-config` are unavailable. Native Omarchy validation and full CI remain outstanding; no QML changed.
- Independent code review found no blocking defects; its suggested rotation failure test was added.

The focused test uses GNU linker wrapping for deterministic libc failures. Only this test target disables fortified `fprintf` substitution so wrapping works; production hardening flags remain unchanged. The target is included in `test`, `test-ci`, and `clean`.

## Latency and remaining acceptance

One optimized local probe used 1,000 sequential appends with 256-byte synthetic text and unique IDs on container overlayfs. No rotation occurred in this probe.

| Revision | Mean | Median | p95 | Maximum |
|---|---:|---:|---:|---:|
| Base | 18.7 µs | 14.0 µs | 21.6 µs | 705.8 µs |
| Patch | 26.6 µs | 23.2 µs | 37.1 µs | 748.9 µs |

These measurements establish local overhead only. Four synchronous sync calls per append may stall the networking helper on slower storage. Before merge, run full CI and the documented native gates on supported Arch/Omarchy, measure append p95 and worst-case helper responsiveness on actual storage, and exercise sustained direct/group text and rotation with synthetic identities. Any batching or asynchronous persistence design is a separate change.
