# Trigger-free source update follow-up

This note records the shell-off source workflow added after `v0.8.1-beta.1`. It replaces the documented active-tree update command without changing OmaQ's protocol or QML behavior.

## Incident boundary

Omarchy's `PluginRegistry` runs its own recursive `inotifywait` process for the complete user plugin directory. A local plugin event starts a debounced reload that unloads and recreates plugin panels, services, and widgets. `QS_DISABLE_FILE_WATCHER=1` disables Quickshell's watcher, not this registry process.

A 2026-09-01 coredump correlated OmaQ write events with a generic QML loader path ending in `__dynamic_cast`. The exact unsymbolized Quickshell function remains unknown. The shell supervisor restored the interface, while OmaQ's detached helper and persisted state remained available.

## Source transaction

The updater now:

- requires a clean Git-managed `main` checkout with the canonical OmaQ `origin`
- uses fixed system Git commands, sanitized Git configuration and transport state, canonical HTTPS, optional exact-commit binding, and a GitHub-scoped in-memory header from an authenticated GitHub CLI when private access requires it
- refuses runtime or state staging paths that resolve inside the monitored plugin directory
- resolves canonical `origin/main` first and skips staging plus the shell stop when the live commit already matches
- retries a pending group-safe helper activation during that source no-op without stopping the shell
- bounds clone and build writes, retained tree count and bytes, and required free space; limit failures terminate the complete staging process group
- clones the complete replacement checkout and builds its helper below private user state
- validates the plugin before and after the external build
- binds the target commit, helper SHA-256, running helper protocol, and literal staged QML requirement
- checks `st_dev` plus the `/proc/self/mountinfo` mount ID and exercises the exact no-copy exchange with disposable external directories before any shell stop
- refuses a locked session and stops both Quickshell and its `omarchy-launch-shell` supervisor
- checks the supervisor, shell, recursive watcher, and shell IPC immediately before the exchange
- exchanges complete trees with `mv -T --exchange --no-copy`
- retains the old checkout outside the monitored directory as the rollback tree
- binds the restarted supervisor and Quickshell process identity through every consumer poll
- validates `listPlugins`, the OmaQ IPC target, running and available helper hashes, protocol compatibility, and the correlated post-start journal before and after helper activation
- restores the old tree under the same shell-off boundary when a consumer check fails before helper activation
- returns failure without a tree rollback if final verification fails after the helper process may have changed

The same-user process boundary remains cooperative. A separate same-user process must not start or restart the shell during the transaction. Closing that final process-creation race for uncooperative local processes would require an Omarchy maintenance lock honored by the launcher.

## Helper transaction

The existing runtime doctor remains the only helper activation boundary. The controller copies that tool into its private runtime lock directory before the tree exchange, so an older or replaced live tree cannot redirect it. The doctor copies the running image from `/proc/<pid>/exe` into `.prev`, checks the staged helper with `--expect-sha256`, binds process and socket identity, and sends only `helper.shutdown_if_no_groups`.

A staged QML requirement newer than the running helper aborts before the tree exchange. Active or uncertain groups therefore produce the explicit mixed state `update-pending: old helper, new tree` only when protocol capability gating keeps the new QML compatible.

## Offline regression coverage

The focused tests cover:

- duplicate JSON keys plus protocol decoys in comments, quoted strings, and template strings
- protocol requirements below, equal to, and above the running version
- full `.git` checkout, branch, origin, sanitized Git environment, private GitHub authentication without argument or checkout persistence, clean-tree requirements, and serialization with the helper updater
- symlink and special-file rejection
- same-filesystem exchange and reverse exchange
- no-op source updates without staging or a shell stop
- pre-stop exchange-capability probing and cross-device refusal without copy fallback
- monitored-path refusal, bounded acquisition, descendant-process termination, and retained update storage
- a supervisor in the one-second relaunch backoff
- shell-stop recovery, interrupt handling, and restarted-shell identity replacement
- a restart injected after an earlier stopped check but before the final exchange check
- `.prev` preservation during hash-bound activation and group-pending retries
- inactive, wrong-hash, and protocol-incompatible activation outcomes

These tests do not perform a live shell stop or tree exchange, claim visible Wayland acceptance, or simulate an uncooperative same-user process starting in the final syscall window. The Quickshell crash and its Exit-255 crash-handler relaunch symptom still warrant an upstream report.
