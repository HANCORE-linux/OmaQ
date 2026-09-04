# Shell-off source installation follow-up

This note records the first-install workflow added after `v0.8.1-beta.2`. It replaces the earlier add-then-build sequence, which made the plugin tree visible before its ignored local helper existed and wrote that helper while Omarchy's recursive plugin watcher was active.

## Installation transaction

The primary source command atomically creates a private source directory directly under `$HOME`, clones the current `main` branch with the user's Git client, and runs the checkout's root `install.sh`. That entry point validates its arguments, runs the checkout, target, session, and shell preflight, installs the required packages through `omarchy pkg add`, and delegates to the shell-off controller, which repeats the preflight before building. It trusts the user's Git configuration and environment, which can rewrite the nominal GitHub URL, and has no pre-execution origin proof or acquisition-size bound. The optional exact-commit bootstrap uses a fixed system Git with sanitized credential-free configuration, adds acquisition limits, and verifies the checkout commit, branch, origin, and clean status before executing `install.sh`. Both paths accept `--section left|center|right` and pass it only to the single final Omarchy enable operation; no additional placement mutation occurs.

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
- treats only the exact trusted `omarchy-shell is not responding` enable result as uncertain because the shell-config mutation can reload that IPC handler before its reply arrives; every other command failure remains fail-closed
- binds the restarted process generation, plugin list, persisted shell config, OmaQ IPC target, journal cursor, helper PID, helper hashes, and protocol through final consumer acceptance, including after that exact uncertain result

A failure before activation is possible stops the shell again if necessary, atomically returns the same checkout to its external path, verifies it, and restarts the prior shell without OmaQ. An unbound target raced into place is never removed or discovered automatically; the installer leaves the shell stopped for manual recovery. Once a helper or enabled plugin may be active, a consumer failure keeps the complete installed tree and tries to disable the plugin instead of deleting a potentially active runtime. Both success and cleanup bind the persisted shell config to the observed plugin state. If disablement cannot be proven, the shell is stopped.

The source installer shares the updater's source/helper locks. The final same-user process-creation race remains inside the documented cooperative trust boundary; no unrelated same-user process may restart the shell during installation.

## Deployment validation

The first authorized Machine 2 attempt reached the single enable operation after atomically placing the checkout. That operation returned exit code `1`, empty stdout, and exactly `omarchy-shell is not responding` followed by one LF on stderr, consistent with the persisted layout change reloading the responding IPC handler. The then-current installer treated the result as a failure, disabled OmaQ, and retained the complete installed tree instead of reporting success. This observed tuple motivated the exact uncertain-result handling described above; adjacent exit codes, output, and line endings remain failures.

After that fix merged, the public source path cloned and installed exact commit `af8ee93184ca115115302cbceaa5acd8ad598e33`. The controller moved the complete checkout out of `~/.omaq-source-install`, enabled one third-party `hancore.omaq` bar widget in the right section, and accepted one launcher, one Quickshell process, one watcher, ready shell and OmaQ IPC, Protocol 14, and matching available and running helper SHA-256 `ee43637be9ac9880bb465408a87c8ace94015c217a1df25dc79ce088308a1fba`. The correlated journal check found no crash, loader, or plugin-failure entry. The successful retry does not establish that response loss recurred; offline regressions cover the exact uncertain-result branch.

The official same-commit update then returned `source: current` and `helper: current` without changing the checkout inode, helper PID, launcher PID, Quickshell PID, watcher PID, retained update-tree count, or the acceptance harness's scoped baseline of 50 Quickshell coredumps. Six `python3.14` coredumps occurred earlier that day, all before installation. A later host- and timestamp-bound read-only query found no retained coredump record after the post-acceptance log timestamp. Every retained data root survived the complete update, uninstall, and reinstall cycle, but orderly helper shutdown changed the aggregate fingerprint by rewriting save data, empty group-registry files, and stdout spool state. The cycle therefore proves root retention, not byte-for-byte retained-tree identity. These checks were remote plugin-consumer acceptance and did not claim visible Wayland or multi-monitor acceptance.

## Regression coverage

Focused offline tests cover the root entry point's argument validation, dependency failure, and exact forwarding; Bash and Fish documentation parity; shell-config ambiguity, unsupported versions, and host-coerced ID types; stale enabled locations; fixed interpreter paths without bytecode writes; assume-unchanged and skip-worktree rejection; shallow, sparse, promisor, alternate-object, common-directory, missing-object, and gitlink rejection; no explicit rescan; stopped-state rechecking immediately before rename; real no-replace renames; an existing target; a target raced into place at the syscall boundary and through the complete recovery path; delayed startup discovery; one enable; exact enable-response loss followed by complete consumer verification; rejection of every other enable command failure; checkout restoration before enablement; active and uncertain helper retention; disable-with-tree-retention after an attempted enable; persisted disable verification; and shell stop when disablement fails.

These offline tests do not claim a live shell stop, a visible Wayland result, or preservation of retained private data across a real uninstall and reinstall. The separate deployment validation above covers a remote shell stop and retained-root presence, but not visible Wayland, multi-monitor behavior, or byte-for-byte retained-tree identity.
