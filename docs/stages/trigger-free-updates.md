# Trigger-free source update follow-up

This note records the shell-off source workflow added after `v0.8.1-beta.1`. It replaces the documented active-tree update command without changing OmaQ's protocol or QML behavior.

## Incident boundary

Omarchy's `PluginRegistry` runs its own recursive `inotifywait` process for the complete user plugin directory. A local plugin event starts a debounced reload that unloads and recreates plugin panels, services, and widgets. `QS_DISABLE_FILE_WATCHER=1` disables Quickshell's watcher, not this registry process.

A 2026-09-01 coredump correlated OmaQ write events with a generic QML loader path ending in `__dynamic_cast`. A shell-off deployment on 2026-09-02 reproduced the same `QQuickLoader::setSource` → `QQmlComponent::create` → `__dynamic_cast` stack when its premature rollback stopped a still-loading shell. The exact unsymbolized Quickshell function remains unknown. The shell supervisor restored the interface in both cases, while OmaQ's detached helper and persisted state remained available.

## Source transaction

The updater now:

- requires a clean Git-managed `main` checkout with the canonical OmaQ `origin`
- uses fixed system Git commands, sanitized Git configuration and transport state, canonical HTTPS, optional exact-commit binding, and a fresh private credential-free `HOME` for each public network operation
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
- tolerates only the Omarchy restart wrapper's exact readiness-timeout result, waits for exactly one ready supervisor, Quickshell process, and recursive watcher, then rechecks the session lock
- binds all three restarted process identities through every consumer poll
- validates `listPlugins`, the OmaQ IPC target, running and available helper hashes, protocol compatibility, and the correlated post-start journal before and after helper activation
- restores the old tree under the same shell-off boundary when a consumer check fails before helper activation, terminating each exact replacement supervisor that appears while the rollback stop is in progress
- returns failure without a tree rollback if final verification fails after the helper process may have changed

The same-user process boundary remains cooperative. A separate same-user process must not start or restart the shell during the transaction. Closing that final process-creation race for uncooperative local processes would require an Omarchy maintenance lock honored by the launcher.

## Helper transaction

The existing runtime doctor remains the only helper activation boundary. The controller copies that tool into its private runtime lock directory before the tree exchange, so an older or replaced live tree cannot redirect it. The doctor copies the running image from `/proc/<pid>/exe` into `.prev`, checks the staged helper with `--expect-sha256`, binds process and socket identity, and sends only `helper.shutdown_if_no_groups`.

A staged QML requirement newer than the running helper aborts before the tree exchange. Active or uncertain groups therefore produce the explicit mixed state `update-pending: old helper, new tree` only when protocol capability gating keeps the new QML compatible. A byte-identical helper can keep running from the retained checkout after a source-only exchange; uninstall binds that relocated process through stable executable metadata and descriptor-bound SHA-256 equality with the live helper before requesting group-safe shutdown.

## Offline regression coverage

The focused tests cover:

- duplicate JSON keys plus protocol decoys in comments, quoted strings, and template strings
- protocol requirements below, equal to, and above the running version
- full `.git` checkout, branch, origin, sanitized Git environment, isolated network homes, `.netrc` exclusion on the real remote-resolution path, clean-tree requirements, and serialization with the helper updater
- symlink and special-file rejection
- same-filesystem exchange and reverse exchange
- no-op source updates without staging or a shell stop
- pre-stop exchange-capability probing and cross-device refusal without copy fallback
- monitored-path refusal, bounded acquisition, descendant-process termination, and retained update storage
- a supervisor in the one-second relaunch backoff, a replacement supervisor appearing during stop, delayed shell readiness after the exact timeout result, lock rechecking after that timeout, and rejection of other nonzero restart results
- shell-stop recovery, interrupt handling, and restarted supervisor, Quickshell, or watcher identity replacement
- a restart injected after an earlier stopped check but before the final exchange check
- `.prev` preservation during hash-bound activation and group-pending retries
- inactive, wrong-hash, and protocol-incompatible activation outcomes
- uninstall acceptance of a stable byte-identical relocated helper and refusal of changed relocated bytes

These tests do not perform a live shell stop or tree exchange, claim visible Wayland acceptance, or simulate an uncooperative same-user process starting in the final syscall window. The Quickshell crash and its Exit-255 crash-handler relaunch symptom still warrant an upstream report.

## Deployment validation

The first authorized primary-machine attempt failed during external remote preflight because the private origin had no noninteractive credential. It stopped before changing the shell, helper, or live checkout. GitHub-scoped in-memory authentication then allowed the shell-off exchange to install `7fde5c2c4b0e74c1717e8ac1baf2809694b2b393`.

The controller initially treated asynchronous shell startup as an immediate failure and entered rollback. Stopping that loading generation reproduced the known `QQuickLoader`/`__dynamic_cast` crash, and a replacement supervisor appeared before the rollback stop could finish. The tree remained on the new commit, and the replacement shell stabilized with one supervisor, Quickshell process, and watcher.

The readiness follow-up installed `5a86d7ea49449934a499213e5d876f41f56d9325` with a clean restart, a bound previous tree, Protocol 14 helper continuity, complete consumer acceptance, and an unchanged-process source no-op. A later acceptance harness imported the live controller without disabling bytecode output. It created an ignored Python cache and triggered one hot reload without a crash; the next full tree exchange removed the cache without deleting from the active tree.

The README and panel-icon follow-up installed `07f43f99f44ccfd80a6c31cbbe9d40234855e6be`. The system-theme compatibility follow-up then installed `f8bea135387d13e65de0032b88470a6490e714f0` on both machines. Both installations passed checkout, previous-tree, plugin-consumer, helper PID, Protocol 14, executable-hash, clean-restart, no-new-coredump, and same-commit no-op checks. A visible primary-machine switch to the semantic Tokyo Night palette confirmed System swatches and rail hover colors; restoring Oxocarbon left the same shell generation healthy.

Machine 2 later updated from `3a90583156b848aead16e14e760d793cd4b31384` to `2ae4a7dd136cecd35fa19a3123927dc6b8417655`. The source-only exchange left the byte-identical Protocol 14 helper running from the retained checkout: the live helper inode was `58:642854`, the running inode was `58:611991`, and both descriptor-bound images had SHA-256 `ee43637be9ac9880bb465408a87c8ace94015c217a1df25dc79ce088308a1fba`. The installed uninstaller bound that relocated image, received the correlated group-safe shutdown acknowledgement, removed the runtime markers, and removed the plugin without a signal fallback. Every retained data root remained present. Orderly shutdown rewrote save data, empty group-registry files, and stdout spool state, so the retained tree was not byte-identical and is not documented as such.

During the observed removal, Omarchy's standard command disabled OmaQ because it was enabled, deleted the Git checkout under the live recursive watcher, and then requested `rescanPlugins`. The bound journal interval contained 335 `Local plugin changed` lines, which do not establish 335 complete reload cycles, and seven `Object or context destroyed during incubation` notices. It contained no crash or loader-failure line, and the bound Quickshell process produced no coredump.
