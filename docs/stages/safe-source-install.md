# Shell-off source installation follow-up

This note records the first-install workflow added after `v0.8.1-beta.2`. It replaces the earlier add-then-build sequence, which made the plugin tree visible before its ignored local helper existed and wrote that helper while Omarchy's recursive plugin watcher was active.

## Installation transaction

The primary source command atomically creates a private source directory directly under `$HOME`, clones the current `main` branch with the user's Git client, and runs the checkout's root `install.sh`. That entry point validates its arguments, runs the checkout, target, session, and shell preflight, installs the required packages through `omarchy pkg add`, and delegates to the shell-off controller, which repeats the preflight before building. It trusts the user's Git configuration and environment, which can rewrite the nominal GitHub URL, and has no pre-execution origin proof or acquisition-size bound. The optional exact-commit bootstrap uses a fixed system Git with sanitized credential-free configuration, adds acquisition limits, and verifies the checkout commit, branch, origin, and clean status before executing `install.sh`. Both paths accept `--section left|center|right` and pass it only to the single final Omarchy enable operation; no interactive placement or Shibumi-specific mutation occurs. Shibumi V2 consumes the resulting Omarchy layout entry directly, while V1 adoption remains a Shibumi compatibility boundary.

The controller then:

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
- binds the restarted process generation, plugin list, OmaQ IPC target, journal cursor, helper PID, helper hashes, and protocol through final consumer acceptance

A failure before activation is possible stops the shell again if necessary, atomically returns the same checkout to its external path, verifies it, and restarts the prior shell without OmaQ. An unbound target raced into place is never removed or discovered automatically; the installer leaves the shell stopped for manual recovery. Once a helper or enabled plugin may be active, a consumer failure keeps the complete installed tree and tries to disable the plugin instead of deleting a potentially active runtime. Both success and cleanup bind the persisted shell config to the observed plugin state. If disablement cannot be proven, the shell is stopped.

The source installer shares the updater's source/helper locks. The final same-user process-creation race remains inside the documented cooperative trust boundary; no unrelated same-user process may restart the shell during installation.

## Regression coverage

Focused offline tests cover the root entry point's argument validation, dependency failure, and exact forwarding; Bash and Fish documentation parity; shell-config ambiguity, unsupported versions, and host-coerced ID types; stale enabled locations; fixed interpreter paths without bytecode writes; assume-unchanged and skip-worktree rejection; shallow, sparse, promisor, alternate-object, common-directory, missing-object, and gitlink rejection; no explicit rescan; stopped-state rechecking immediately before rename; real no-replace renames; an existing target; a target raced into place at the syscall boundary and through the complete recovery path; delayed startup discovery; one enable; checkout restoration before enablement; active and uncertain helper retention; disable-with-tree-retention after an attempted enable; persisted disable verification; and shell stop when disablement fails.

These tests do not claim a live shell stop, a visible Wayland result, or preservation of retained private data across a real uninstall and reinstall. Those remain separate machine acceptance steps.
