# Helper updater and validation history

**Status:** merged and deployed

This note records the completed manual helper updater and the validation rounds that preceded the current release audit.

## Helper updater

- The updater requires a running Protocol-9-or-newer helper and copies the executing `/proc/<pid>/exe` image to `helper/omaq.prev` before the normal Makefile build.
- A private lock, descriptor-bound root, tool, and Makefile handles, plus repeated inode checks serialize cooperative phases.
- The post-build hash is bound into optional activation. Failed build validation restores the available path without stopping the running process.
- Activation uses only `helper.shutdown_if_no_groups`. Active groups, uncertain group state, and unsupported activation leave the old process running with an explicit pending result.
- Replacement verification binds executable identity, hash, protocol marker, and a correlated probe. Failed post-checks remain visible as degraded state with `.prev` retained.

## Recorded validation

- The updater regression covered descriptor-bound backup, replaced or missing available paths, status hashes, expected-build-hash binding, lock refusal, root and helper inode changes, rollback special files, blocked activation, automatic Service-style restart, exact replacement verification, failed-build restoration, linked roots, `.prev` symlinks, and missing replacement processes.
- Search and timestamp tests covered per-window isolation, missing confirmed timestamps, local event and history equality for text, membership notices, and Direct and Group attachments.
- Security hardening passed integrated sanitizer tests, QML parser and RichText adversarial fixtures, native group administration, protocol compatibility, file special-path rejection, and Signal prekey restart coverage.
- Protocol 13 passed full and hardened helper builds, no-Signal build-refusal checks, detached-helper reconnect, clipboard ownership and staging, Unicode emoji checks, and Protocol-7 capability compatibility.
- The illustrated guide used 34 neutral QML captures. Its fixture did not read or change live identity, contact, Ratchet, history, invitation, or group data.

## Historical limitations

The Protocol-10 to Protocol-7 encrypted-message run remained inconclusive because both endpoints lost relay connectivity. Native three-peer group-file injection, mixed recipient outcomes, acknowledgement loss, history-write failure, transfer-ID-store crash injection, separate-network, theme, multi-monitor, and floating-versus-tiling acceptance remained open. `qmllint Panel.qml` could exit 255 without diagnostics.
