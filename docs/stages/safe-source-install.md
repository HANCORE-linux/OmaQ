# Source installation follow-up

This note records the first-install workflow added after `v0.8.1-beta.2` and the later return to Omarchy's normal plugin acquisition path. The shell-off controller remains available to the bounded exact-commit bootstrap, but it is no longer the primary installation command.

## Installation transaction

The primary command uses `omarchy plugin add <canonical-url> --yes` for Omarchy's clone, validation, installation, and rescan while suppressing both interactive questions and add-time enablement. The user must therefore review the exact URL before execution. It then runs the installed checkout's root `install.sh --yes`. That entry point first requires exactly one disabled third-party OmaQ bar widget, refusing a stale configuration that enabled it during discovery, then installs dependencies through `omarchy pkg add`, builds the ignored helper in the live checkout, and enables OmaQ once. Helper writes may leave a watched-tree reload in flight, while enablement reactively activates OmaQ. The installer does not force a second shell exit while asynchronous plugin Loaders may still be finalizing `IpcHandler` registration. It requires working OmaQ IPC plus matching available and running helper images before success. Keeping `--enable` out of the first command prevents an interrupted enable response from blocking the required helper build. This path trusts Omarchy's Git acquisition and the user's Git transport configuration, so it has no independent pre-execution origin proof or acquisition-size bound.

The optional exact-commit bootstrap still uses a fixed system Git with sanitized credential-free configuration, adds acquisition limits, verifies the checkout commit, branch, origin, and clean status before executing `install.sh`, and delegates to the external shell-off controller. Its optional `--section left|center|right` value is passed only to the single final Omarchy enable operation.

The external controller then:

- refuses root, a locked session, an existing target path, an already discovered OmaQ manifest, or a stale enabled OmaQ shell-config entry
- requires the external tree to be a pristine self-contained full Git clone on `main` with the canonical origin; rejects shallow, sparse, partial, promisor, alternate-object, common-directory, symlink, and gitlink trees; rejects index concealment flags; hashes every tracked working-tree blob against `HEAD`; and checks the complete object graph
- resolves canonical `origin/main` before and after the build and supports complete exact-commit binding
- rejects symlinks, special files, oversized trees, dirty tracked files, and ignored entries before the build
- validates the plugin, parses its literal helper protocol requirement, and builds `helper/omaq` outside the monitored plugin directory
- requires the build to create exactly the ignored `helper/omaq` executable and binds its SHA-256
- checks filesystem device and mount identity and exercises Linux `renameat2(RENAME_NOREPLACE)` outside the monitored tree before stopping the shell
- binds the running supervisor, Quickshell process, recursive watcher, shell IPC, and session-lock state through the build and immediately before the transaction
- stops Quickshell and `omarchy-launch-shell`, proves the watcher and IPC are absent, and repeats that stopped-state check immediately before an atomic no-replace rename
- accepts only a missing persisted shell config or integer schema version 1, rereads it at the stopped rename boundary, and moves that same built checkout into `~/.config/omarchy/plugins/hancore.omaq` without cloning, copying, merging, or overwriting a raced target
- validates the installed commit, manifest, and helper hash and requires the helper runtime to remain absent before restarting the shell
- polls the restarted shell's startup scan while binding that process generation, requires OmaQ to become a disabled third-party bar widget, and calls `omarchy plugin enable` exactly once without a separate `rescanPlugins`
- treats only the exact trusted `omarchy-shell is not responding` enable result as uncertain because the shell-config mutation can reload that IPC handler before its reply arrives; every other command failure remains fail-closed
- binds the restarted process generation, plugin list, persisted shell config, OmaQ IPC target, journal cursor, helper PID, helper hashes, and protocol through final consumer acceptance, including after that exact uncertain result

A failure before activation is possible stops the shell again if necessary, atomically returns the same checkout to its external path, verifies it, and restarts the prior shell without OmaQ. An unbound target raced into place is never removed or discovered automatically; the installer leaves the shell stopped for manual recovery. Once a helper or enabled plugin may be active, a consumer failure keeps the complete installed tree and tries to disable the plugin instead of deleting a potentially active runtime. Both success and cleanup bind the persisted shell config to the observed plugin state. If disablement cannot be proven, the shell is stopped.

The source installer shares the updater's source/helper locks. The final same-user process-creation race remains inside the documented cooperative trust boundary; no unrelated same-user process may restart the shell during installation.

## Regression coverage

Focused offline tests cover the normal disabled Omarchy acquisition command, the root entry point's argument validation, dependency failure, live helper build, exact enable-result handling, delayed plugin-IPC readiness, readiness timeout without a forced restart, helper readiness, and external exact forwarding; Bash and Fish documentation parity; shell-config ambiguity, unsupported versions, and host-coerced ID types; stale enabled locations; fixed interpreter paths without bytecode writes; assume-unchanged and skip-worktree rejection; shallow, sparse, promisor, alternate-object, common-directory, missing-object, and gitlink rejection; no explicit rescan; stopped-state rechecking immediately before rename; real no-replace renames; an existing target; a target raced into place at the syscall boundary and through the complete recovery path; delayed startup discovery; one enable; exact enable-response loss followed by complete consumer verification; rejection of every other enable command failure; checkout restoration before enablement; active and uncertain helper retention; disable-with-tree-retention after an attempted enable; persisted disable verification; and shell stop when disablement fails.

These offline tests do not claim a live shell stop, a visible Wayland result, multi-monitor behavior, or preservation of retained private data across a real uninstall and reinstall.
