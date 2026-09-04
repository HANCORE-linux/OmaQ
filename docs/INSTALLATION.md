# Install, update, or remove OmaQ

This guide covers source installation, shell-off updates, helper status, rollback, removal, retained data, and optional package cleanup.

[![OmaQ panel after installation](images/guide/01-panel-home.png)](images/guide/01-panel-home.png)

## Install OmaQ

Arch User Repository (AUR) packaging remains paused. The source installer requires an authenticated GitHub CLI session for the private repository, an unlocked Omarchy session, and no existing `~/.config/omarchy/plugins/hancore.omaq` path. Install the dependencies first:

```bash
omarchy pkg add \
  toxcore libsignal-protocol-c libpulse libpng libjpeg-turbo libwebp \
  ttf-material-symbols-variable qrencode
```

Then acquire one complete external checkout, verify it against canonical `origin/main`, and run its installer:

```bash
(
  set -euo pipefail; umask 077
  mode=install # Use mode=update only for the older-update bootstrap below.
  expected_commit="" # Or set one complete reviewed lowercase 40-hex commit.
  state_home=${XDG_STATE_HOME:-$HOME/.local/state}
  case "$state_home" in
    /*) ;;
    *) echo "State staging path must be absolute" >&2; exit 1 ;;
  esac
  plugins=$(/usr/bin/realpath -m -- "$HOME/.config/omarchy/plugins")
  state_home=$(/usr/bin/realpath -m -- "$state_home")
  case "$state_home/" in
    "$plugins/"*) echo "State staging is inside the plugin tree" >&2; exit 1 ;;
  esac
  bootstrap=$(/usr/bin/mktemp -d \
    "$state_home/omaq-source-bootstrap.XXXXXX")
  trap '/usr/bin/rm -rf -- "$bootstrap"' EXIT
  /usr/bin/gh auth status --hostname github.com
  token=$(/usr/bin/gh auth token --hostname github.com)
  authorization=$(builtin printf 'x-access-token:%s' "$token" | /usr/bin/base64 -w0)
  export OMAQ_BOOTSTRAP_AUTH="Authorization: Basic $authorization"
  unset token authorization
  /usr/bin/python3 -I - "$bootstrap" "$expected_commit" <<'PY'
import os, re, resource, shutil, signal, stat, subprocess, sys, time

root = sys.argv[1]
expected = sys.argv[2]
auth = os.environ.pop("OMAQ_BOOTSTRAP_AUTH")
maximum_output = 1024 * 1024
maximum_entries = 50000
maximum_tree = 512 * 1024 * 1024
env = {
    "HOME": os.path.expanduser("~"),
    "PATH": "/usr/bin:/bin",
    "GIT_ATTR_NOSYSTEM": "1",
    "GIT_CONFIG_GLOBAL": "/dev/null",
    "GIT_CONFIG_NOSYSTEM": "1",
    "GIT_NO_REPLACE_OBJECTS": "1",
    "GIT_OPTIONAL_LOCKS": "0",
    "GIT_TERMINAL_PROMPT": "0",
    "GIT_CONFIG_COUNT": "1",
    "GIT_CONFIG_KEY_0": "http.https://github.com/.extraHeader",
    "GIT_CONFIG_VALUE_0": auth,
}
git = [
    "/usr/bin/git", "-c", "core.hooksPath=/dev/null",
    "-c", "http.sslVerify=true", "-c", "http.followRedirects=false",
    "-c", "protocol.file.allow=never",
]

def limit_output_size():
    resource.setrlimit(resource.RLIMIT_FSIZE, (maximum_output, maximum_output))

def limit_tree_file_size():
    resource.setrlimit(resource.RLIMIT_FSIZE, (maximum_tree, maximum_tree))

def check_tree():
    entries = total = 0
    stack = [root]
    while stack:
        current = stack.pop()
        try:
            children = os.scandir(current)
        except FileNotFoundError:
            continue
        with children:
            for child in children:
                entries += 1
                if entries > maximum_entries:
                    raise RuntimeError("Bootstrap checkout exceeds 50000 entries")
                try:
                    info = child.stat(follow_symlinks=False)
                except FileNotFoundError:
                    continue
                if stat.S_ISLNK(info.st_mode):
                    raise RuntimeError("Bootstrap checkout contains a symlink")
                if stat.S_ISDIR(info.st_mode):
                    stack.append(child.path)
                elif stat.S_ISREG(info.st_mode):
                    total += info.st_size
                    if total > maximum_tree:
                        raise RuntimeError("Bootstrap checkout exceeds 512 MiB")
                else:
                    raise RuntimeError("Bootstrap checkout contains a special file")

def run(args, timeout, monitor=False):
    output_path = root + ".output"
    if monitor:
        output_fd = os.open("/dev/null", os.O_RDWR | os.O_CLOEXEC)
    else:
        output_fd = os.open(
            output_path,
            os.O_RDWR | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC,
            0o600,
        )
    try:
        process = subprocess.Popen(
            args, stdin=subprocess.DEVNULL, stdout=output_fd, stderr=output_fd,
            env=env,
            preexec_fn=limit_tree_file_size if monitor else limit_output_size,
            start_new_session=True,
        )
        deadline = time.monotonic() + timeout
        try:
            while process.poll() is None:
                if monitor:
                    check_tree()
                if time.monotonic() >= deadline:
                    raise RuntimeError(f"Command timed out: {args[1]}")
                time.sleep(0.05)
            if monitor:
                check_tree()
        except BaseException:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
            raise
        try:
            os.killpg(process.pid, 0)
        except ProcessLookupError:
            pass
        else:
            os.killpg(process.pid, signal.SIGKILL)
            raise RuntimeError(f"Command left a child process: {args[1]}")
        if process.returncode != 0:
            raise RuntimeError(
                f"Command failed ({process.returncode}): {args[1]}"
            )
        if monitor:
            return b""
        size = os.fstat(output_fd).st_size
        if size > maximum_output:
            raise RuntimeError(f"Command output exceeds 1 MiB: {args[1]}")
        os.lseek(output_fd, 0, os.SEEK_SET)
        output = os.read(output_fd, maximum_output + 1)
        if len(output) != size:
            raise RuntimeError(f"Command output changed while reading: {args[1]}")
        return output
    finally:
        os.close(output_fd)
        if not monitor:
            os.unlink(output_path)

if shutil.disk_usage(os.path.dirname(root)).free < 2 * maximum_tree:
    raise SystemExit("State staging has less than 1 GiB free")
if expected and not re.fullmatch(r"[0-9a-f]{40}", expected):
    raise SystemExit("Expected commit must be complete lowercase 40-hex")
origin = "https://github.com/HANCORE-linux/OmaQ.git"
remote = run(
    [*git, "ls-remote", "--exit-code", origin, "refs/heads/main"], 60
).decode("utf-8", "strict").split()
if (len(remote) != 2 or remote[1] != "refs/heads/main"
        or not re.fullmatch(r"[0-9a-f]{40}", remote[0])):
    raise SystemExit("Canonical origin returned an invalid main ref")
if expected and remote[0] != expected:
    raise SystemExit("Canonical origin/main differs from the expected commit")
run(
    [*git, "clone", "--branch", "main", "--single-branch",
     "--no-hardlinks", "--", origin, root],
    300, monitor=True,
)
head = run([*git, "-C", root, "rev-parse", "HEAD"], 30).decode().strip()
url = run([*git, "-C", root, "remote", "get-url", "origin"], 30).decode().strip()
branch = run(
    [*git, "-C", root, "symbolic-ref", "--short", "HEAD"], 30
).decode().strip()
status = run(
    [*git, "-C", root, "status", "--porcelain=v1", "-z"], 30
)
if (head != remote[0] or (expected and head != expected)
        or url != origin or branch != "main" or status):
    raise SystemExit("Bootstrap checkout identity mismatch")
PY
  unset OMAQ_BOOTSTRAP_AUTH
  case "$mode" in
    install)
      if [[ -n $expected_commit ]]; then
        "$bootstrap/scripts/install-omaq.sh" \
          --expect-commit "$expected_commit" --yes
      else
        "$bootstrap/scripts/install-omaq.sh" --yes
      fi
      ;;
    update)
      /usr/bin/env -i HOME="$HOME" PATH=/usr/bin:/bin \
        /usr/bin/omarchy plugin validate "$bootstrap"
      if [[ -n $expected_commit ]]; then
        "$bootstrap/scripts/update-omaq.sh" \
          --expect-commit "$expected_commit" --yes
      else
        "$bootstrap/scripts/update-omaq.sh" --yes
      fi
      ;;
    *) echo "Unknown bootstrap mode" >&2; exit 1 ;;
  esac
)
```

The bootstrap exports the GitHub authorization header only inside its subshell and does not store a token in the checkout. It limits command output to 1 MiB, checks the clone during acquisition against the 50,000-entry and 512 MiB tree limits, requires 1 GiB free first, and terminates the complete process group on a timeout or limit failure. To bind a reviewed revision, set `expected_commit` near the top of the block to its complete lowercase 40-character hash. The bootstrap rejects a different canonical ref before cloning or executing downloaded code, and the installer repeats that binding before and after the build.

The repository intentionally omits the generated `helper/omaq` binary. The installer validates a pristine self-contained full Git clone and rejects shallow, sparse, partial, alternate-object, common-directory, symlink, and gitlink trees. It rejects index concealment flags, compares every tracked file's mode and Git blob hash with `HEAD`, and checks the complete Git object graph. It also accepts only a missing shell config or the supported integer schema version 1. It then builds the Signal-enabled helper outside the monitored plugin directory, stops the shell supervisor and watcher, rechecks persisted enablement at the rename boundary, and uses Linux `renameat2(RENAME_NOREPLACE)` to place that same checkout without copying or overwriting another path. The restarted shell performs the only discovery scan. The installer waits for that scan to expose a disabled OmaQ entry, then calls `omarchy plugin enable`; that command changes plugin state without requesting another rescan.

A failure before enablement moves the checkout back to its external path and restarts the shell. Once a helper or enabled plugin may be active, a failure keeps the complete installed tree and disables OmaQ instead of deleting a potentially active runtime. Successful enablement and failure cleanup both reread `shell.json` without following symlinks and require the persisted state to match the shell's projection. If disablement cannot be proven, the installer stops the shell and leaves the tree in place for recovery.

Verify the plugin and helper after installation:

```bash
omarchy plugin validate ~/.config/omarchy/plugins/hancore.omaq
~/.config/omarchy/plugins/hancore.omaq/scripts/update-helper.sh --status
```

## Update OmaQ

The supported updater keeps every source fetch and helper build outside the monitored plugin directory. Run it from an installation that already contains the command:

```bash
~/.config/omarchy/plugins/hancore.omaq/scripts/update-omaq.sh --yes
```

The update requires an enabled OmaQ plugin, a clean Git checkout on `main`, the canonical OmaQ `origin`, an unlocked session, and a running Protocol-9-or-newer helper. Private GitHub access also requires an authenticated GitHub CLI session from `gh auth login`; the updater passes its token only as a GitHub-scoped in-memory Git header. `XDG_RUNTIME_DIR` and `XDG_STATE_HOME` must resolve outside `~/.config/omarchy/plugins/`. The updater refuses symlinked roots, local source changes, non-fast-forward history, malformed manifests, ambiguous protocol declarations, and a staged QML requirement newer than the running helper.

### Bootstrap an older installation

Installations that predate `update-omaq.sh` use the same bounded [external controller acquisition](#install-omaq) shown above. Change only the first assignment from `mode=install` to `mode=update`. The update branch validates the external plugin and invokes `update-omaq.sh`; it does not modify the live checkout before that controller stops the shell.

To bind an announced release or reviewed revision, set `expected_commit` in the shared block. Both the bootstrap and updater abort if canonical `origin/main` differs.

### Understand the shell-off sequence

The updater performs these steps in order:

1. Lock source updates outside the replaceable plugin tree.
2. Copy the controller's hardened `helper-runtime.py` into the private runtime lock directory so a tree exchange cannot replace it.
3. Bind the clean live checkout, canonical `origin`, running helper identity, protocol marker, and hashes.
4. Resolve canonical `origin/main`. If it matches the live commit, skip staging and the shell stop; retry only a pending group-safe helper activation.
5. Clone the complete `main` checkout, including `.git`, below `~/.local/state/omaq-source-updates/`.
6. Validate the staged plugin, build its helper, validate it again, and bind the commit and helper SHA-256.
7. Verify matching filesystem and mount identities, then perform a disposable external `mv -T --exchange --no-copy` capability probe.
8. Refuse a locked session, stop Quickshell, then terminate the exact `omarchy-launch-shell` supervisor if it remains in backoff.
9. Verify that the supervisor, Quickshell, plugin watcher, and shell IPC are all absent.
10. Back up the running `/proc/<pid>/exe` image through the bound runtime tool.
11. Repeat the stopped-state check and exchange the staged and live directories with `mv -T --exchange --no-copy`.
12. Validate the live plugin and create `helper/omaq.prev` from the still-running old image.
13. Start the shell, wait for exactly one ready supervisor, Quickshell process, and watcher when the restart wrapper reports only its readiness timeout, recheck that the session is unlocked in that case, then bind their PIDs, start times, parent relationship, and session path through every consumer check.
14. Require `listPlugins`, the `hancore.omaq` IPC target, running and available helper hashes, protocol compatibility, and the correlated shell journal to pass.
15. Activate the new helper through `helper-runtime.py --expect-sha256`.
16. Recheck the bound shell plus the running helper hash and protocol; an inactive or incompatible replacement fails the update.

The updater checks filesystem device, mount identity, and exchange capability before stopping the shell, then repeats the boundary checks during activation. The exchange requires one filesystem and never falls back to a copy. Clone and build monitoring enforce 50,000 entries, 512 MiB per tree, 2 GiB across retained update trees, and at least 1 GiB free before acquisition. A limit or timeout terminates the complete staging process group. OmaQ keeps at most eight staged or previous trees before requiring manual inspection and cleanup.

The old complete Git checkout moves to the external path printed as `previous tree`. OmaQ retains that directory for inspection; it does not delete source backups automatically.

The final process check detects a cooperative restart before the exchange and aborts without renaming either tree. During a rollback stop, every exact replacement supervisor observed before the deadline is terminated so the launcher's backoff cannot strand a mixed tree. Another process running as the same user remains inside OmaQ's documented trust boundary. Do not run `omarchy restart shell` concurrently with an update.

### Check update status

After the command finishes, check NetworkManager and the helper state:

```bash
nmcli -t -f STATE general
~/.config/omarchy/plugins/hancore.omaq/scripts/update-helper.sh --status
```

The helper status reports one of these states:

- **Current**: the running and available helper hashes match
- **Update pending**: the running and available helper hashes differ
- **Inactive**: no helper is running; the next Service start uses the available binary

### Understand activation outcomes

Use the activation result to choose the next step:

| Result | Meaning | Next step |
|---|---|---|
| `activated` | The group-free replacement passed hash and process verification | Continue with the final checks |
| `current` | The running and available helper already match | Continue with the final checks |
| `update-pending: old helper, new tree` with `active_groups` | Private groups keep the old helper running under the new compatible QML tree | Leave every private group, then rerun the updater |
| `update-pending: old helper, new tree` with `group_state_uncertain` | The helper cannot prove group-free state | Resolve the reported state, then rerun the updater |
| `update-pending: old helper, new tree` with `activation_unsupported` | The helper cannot perform correlated safe shutdown | Use a later full user-session restart |
| `degraded` | Replacement verification failed after the new tree passed consumer checks | Run the helper rollback below |

Pending outcomes return success because the running helper remains unchanged. Protocol capability gating permits this mixed state only when the staged QML requirement does not exceed the running helper protocol.

### Recover from an update failure

A staged build or pre-exchange check leaves the live tree unchanged. A post-exchange schema, shell, plugin, IPC, or helper-consumer failure stops the shell and exchanges the retained old tree back before helper activation. The command returns an error and states whether automatic restoration passed.

The final activation check can fail after the new tree passed its pre-activation consumer checks. The updater leaves that tree in place because the helper process may already have changed, returns a failure, and never treats an inactive helper as success. Check helper status first. Restore `.prev` if the hashes differ or the error reports an inactive, substituted, protocol-incompatible, or degraded helper:

```bash
~/.config/omarchy/plugins/hancore.omaq/scripts/update-omaq.sh \
  --rollback-helper --yes
```

The rollback uses `helper-runtime.py restore`, starts the shell, repeats the consumer checks, and activates the restored hash through the same group-safe operation. It never writes `helper/omaq` while the plugin watcher is running.

<details>
<summary>How group-safe helper activation works</summary>

Before the directory exchange, the runtime doctor copies the verified running `/proc/<pid>/exe` image to the old tree's `helper/omaq.prev`. After the exchange, it copies that same bound image into the new tree's `.prev`. The available helper remains the externally built staging image.

Activation sends only `helper.shutdown_if_no_groups`. The helper confirms durable group-free state before stopping. Service starts the available binary, and the updater verifies its hash, executable identity, protocol marker, and correlated probe. A pre-existing `.prev` does not participate in activation; only `restore` reads it.

A group-free restart creates a short offline window. In-flight messages become `delivery_unknown`, while active transfers, calls, and invitations fail visibly. The workflow has no signal fallback, binary hot-swap, or crash journal.

</details>

## Remove OmaQ

Run the verified wrapper:

```bash
~/.config/omarchy/plugins/hancore.omaq/scripts/uninstall-omaq.sh
```

When a helper is running, the wrapper binds it to its process, executable, socket, and instance. It removes the plugin only after a correlated group-free shutdown acknowledgment and never uses a signal fallback. When no helper runtime exists, it locks helper startup before removal instead.

Removal stops when any of these conditions applies:

- an active private group exists
- native and registered group state disagree
- group cleanup or identity loading remains pending
- runtime identity or the correlated acknowledgment cannot be verified
- helper startup overlaps removal

Omarchy deletes a Git-managed plugin directory, including local source changes inside it. A plain plugin directory is moved to the exact hidden backup path printed by the wrapper. Review or copy any source changes before removal.

## Review retained data

Plugin removal intentionally retains these locations:

- `~/.local/share/omaq/`: identity, contacts, groups, avatars, history, Ratchet state, and managed custom sounds
- `~/.local/state/omaq/`: identity recovery, preferences, unread state, receipts, surfaces, and journals
- `~/Downloads/omaq/`: received files
- `~/.local/state/omaq-deploy-backups/`: deployment backups, when present
- the exact Omarchy plugin backup path printed by the wrapper, when present

Keep retained data if you may reinstall OmaQ or still need the identity or history. Inspect every path before deleting it:

```bash
rm -rf -- "$HOME/.local/share/omaq"                 # Identity and chats
rm -rf -- "$HOME/.local/state/omaq"                 # Preferences and state
rm -rf -- "$HOME/Downloads/omaq"                    # Received files
rm -rf -- "$HOME/.local/state/omaq-deploy-backups"  # Deployment backups
```

Each deletion is irreversible. Run only the individual commands for data you intend to erase.

## Remove unused packages

OmaQ leaves dependency packages installed because another application may use them. This includes `toxcore`, `libsignal-protocol-c`, `libpulse`, `libpng`, `libjpeg-turbo`, `libwebp`, `ttf-material-symbols-variable`, and `qrencode`.

The optional `zbar` verification package also remains when installed for testing. Inspect each package with `pacman -Qi` before removing it. The wrapper prints one combined `omarchy pkg drop` command; edit that command to include only dependencies confirmed unused.
