# Install, update, or remove OmaQ

This guide covers source installation, shell-off updates, helper status, rollback, removal, retained data, and optional package cleanup.

[![OmaQ panel after installation](images/guide/01-panel-home.png)](images/guide/01-panel-home.png)

## Install OmaQ

> [!IMPORTANT]
> OmaQ is not published in the Arch User Repository (AUR) yet. Until an OmaQ package is available, install it through Omarchy's normal plugin command.

Run the installation as your desktop user while the Omarchy shell is running and the session is unlocked:

```bash
omarchy plugin add https://github.com/HANCORE-linux/OmaQ.git --yes &&
~/.config/omarchy/plugins/hancore.omaq/install.sh --yes
```

Review the exact repository URL before running the command: `--yes` suppresses Omarchy's clone and enable questions, so the normal path leaves the checkout disabled for the next step. Omarchy clones, validates, and installs that checkout. The installed `install.sh` verifies that OmaQ is a disabled third-party bar widget before installing packages; it refuses a stale shell-layout reference that enabled OmaQ during discovery. It then installs the required dependencies through `omarchy pkg add`, builds the Signal-enabled helper, and enables OmaQ. Helper writes may leave a watched-tree reload in flight, while enablement reactively activates the plugin. The installer therefore does not force a second `omarchy restart shell` while asynchronous plugin Loaders may still be finalizing `IpcHandler` registration. It succeeds only after activation exposes working OmaQ IPC and matching available and running helper images. It accepts `--section left`, `center`, or `right`; omission uses the manifest's `right` default. Keeping enablement inside `install.sh` ensures the helper exists before activation and avoids treating an interrupted `omarchy plugin add --enable` response as a failed clone. The bar may reload briefly during activation.

A package, build, enable, plugin-IPC, or helper-readiness error stops the command and retains the plugin checkout for inspection. A pre-build enabled-state error must be corrected with `omarchy plugin disable hancore.omaq` before retrying. Use the [update section](#update-omaq) for an existing installation.

The normal command uses Omarchy's Git acquisition and therefore trusts the user's Git configuration and environment, including URL rewrites, proxies, credential helpers, and TLS settings. It does not independently prove before execution that the checkout came from public GitHub. Use the following bootstrap when acquisition must ignore user Git configuration, bind a reviewed commit before execution, and enforce resource limits.

<details>
<summary>Pin a reviewed commit and limit acquisition</summary>

### Use the bounded exact-commit bootstrap

Start a Bash session, set `install_section` to the preferred bar section, then set `expected_commit` to a reviewed 40-character commit hash. Leave the commit empty to select current canonical `main`:

```bash
(
  set -euo pipefail; umask 077
  mode=install # Use mode=update only for the older-update bootstrap below.
  expected_commit="" # Or set one complete reviewed lowercase 40-hex commit.
  install_section=right # Use left, center, or right.
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
  cleanup_bootstrap() {
    status=$?
    trap - EXIT
    if (( status == 0 )); then
      /usr/bin/rm -rf -- "$bootstrap" "${bootstrap}.network-home" || status=$?
    else
      if [[ -d $bootstrap ]]; then
        printf 'Bootstrap failed; retained temporary checkout at %s\n' \
          "$bootstrap" >&2 || :
      else
        printf '%s\n' \
          'Bootstrap failed after checkout placement; follow the installer recovery diagnostic above.' >&2 || :
      fi
      if [[ -d ${bootstrap}.network-home ]]; then
        printf 'Retained isolated network home at %s\n' \
          "${bootstrap}.network-home" >&2 || :
      fi
    fi
    exit "$status"
  }
  trap cleanup_bootstrap EXIT
  /usr/bin/python3 -I - "$bootstrap" "$expected_commit" <<'PY'
import os, re, resource, shutil, signal, stat, subprocess, sys, time

root = sys.argv[1]
expected = sys.argv[2]
network_home = root + ".network-home"
os.mkdir(network_home, mode=0o700)
maximum_output = 1024 * 1024
maximum_entries = 50000
maximum_tree = 512 * 1024 * 1024
network_parent = os.path.dirname(network_home)
if os.pathsep in network_parent:
    raise SystemExit("Bootstrap path contains the Git path separator")
env = {
    "HOME": network_home,
    "PATH": "/usr/bin:/bin",
    "GIT_CEILING_DIRECTORIES": network_parent,
    "GIT_ATTR_NOSYSTEM": "1",
    "GIT_CONFIG_GLOBAL": "/dev/null",
    "GIT_CONFIG_NOSYSTEM": "1",
    "GIT_NO_REPLACE_OBJECTS": "1",
    "GIT_OPTIONAL_LOCKS": "0",
    "GIT_TERMINAL_PROMPT": "0",
}
git = [
    "/usr/bin/git", "-c", "core.hooksPath=/dev/null",
    "-c", "credential.helper=", "-c", "http.extraHeader=",
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
            env=env, cwd=network_home,
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
try:
    os.rmdir(network_home)
except OSError as error:
    raise SystemExit(f"Bootstrap network home changed: {error}")
PY
  case "$mode" in
    install)
      if [[ -n $expected_commit ]]; then
        "$bootstrap/install.sh" \
          --expect-commit "$expected_commit" \
          --section "$install_section" --yes
      else
        "$bootstrap/install.sh" --section "$install_section" --yes
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

The bootstrap uses an isolated credential-free home for anonymous public GitHub access, limits clone size and command output, and terminates the process group after a timeout or limit failure. It rejects a changed canonical ref before cloning or executing downloaded code. The installer repeats the commit check before and after the build. Successful runs remove the temporary checkout and network home. Failed runs retain and print any temporary paths that still exist; remove them manually after inspection. If failure happens after checkout placement, follow the installer's live-tree recovery diagnostic instead.

</details>

The external installer used by that bootstrap validates the complete Git checkout, builds the helper outside the monitored plugin tree, stops the shell, and places the checkout with `renameat2(RENAME_NOREPLACE)`. It then restarts the shell, waits for startup discovery, and enables OmaQ once. See the [source installation transaction](stages/safe-source-install.md) for security boundaries and failure recovery.

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

Do not run `omarchy plugin update hancore.omaq --yes` or include OmaQ in an all-plugins `omarchy plugin update --yes` operation. Omarchy's generic updater fast-forwards the source checkout but has no OmaQ lifecycle hook to rebuild and verify the native helper. Use only the OmaQ updater above.

The update requires an enabled OmaQ plugin, a clean Git checkout on `main`, the canonical OmaQ `origin`, an unlocked session, and a running Protocol-9-or-newer helper. `XDG_RUNTIME_DIR` and `XDG_STATE_HOME` must resolve outside `~/.config/omarchy/plugins/`. The updater refuses symlinked roots, local source changes, non-fast-forward history, malformed manifests, ambiguous protocol declarations, and a staged QML requirement newer than the running helper.

### Bootstrap an older installation

Installations that predate `update-omaq.sh` use the [bounded exact-commit bootstrap](#use-the-bounded-exact-commit-bootstrap) shown above. Change only the first assignment from `mode=install` to `mode=update`. The update branch validates the external plugin and invokes `update-omaq.sh`; it does not modify the live checkout before that controller stops the shell.

To bind an announced release or reviewed revision, set `expected_commit` in the shared block. Both the bootstrap and updater abort if canonical `origin/main` differs.

### What happens during an update

The updater clones and builds outside the monitored plugin tree. When the remote commit differs, it verifies exchange support, stops the shell, atomically exchanges the complete checkouts, restarts the shell, and checks the plugin and helper. The old checkout remains at the printed `previous tree` path for manual recovery.

An active private group can keep the compatible old helper running under the new plugin tree. The updater reports that state as pending instead of forcing a restart. Do not run `omarchy restart shell` during an update. See the [shell-off update transaction](stages/trigger-free-updates.md) for the complete sequence and security boundaries.

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

After removing the plugin, an interactive wrapper run offers each existing OmaQ directory separately:

- `~/.local/share/omaq/`: identity, contacts, groups, avatars, history, Ratchet state, and managed custom sounds
- `~/.local/state/omaq/`: identity recovery, preferences, unread state, receipts, surfaces, and journals
- `~/Downloads/omaq/`: received files
- `~/.local/state/omaq-deploy-backups/`: deployment backups, when present
- `~/.local/state/omaq-source-updates/`: retained source-update trees, when present
- `~/.omaq-source-install/`: a retained legacy source checkout, when present
- the exact Omarchy plugin backup path reported during removal, when present

Absolute `OMAQ_HOME`, `OMAQ_STATE`, `OMAQ_DOWNLOAD_DIR`, `XDG_DOWNLOAD_DIR`, and `XDG_STATE_HOME` overrides are offered at their configured locations when their paths are canonical and owner-controlled.

Every destructive answer defaults to No. A confirmed parent is retained when it contains another candidate that was declined or withheld as unsafe. Each remaining accepted directory is renamed within its owner-controlled parent, fully checked for ownership, permissions, device and mount boundaries, and entry count, and only then passed to symlink-resistant recursive deletion. Top-level symlink candidates and unsafe trees are retained without deletion; nested symlinks are unlinked without following their targets. An unexpected deletion error reports that cleanup may be partial, identifies the parent where a hidden quarantine remainder may exist, and stops before processing another selected path. `uninstall-omaq.sh --yes` is non-interactive and retains every data directory.

## Remove unused packages

OmaQ never removes dependency packages automatically because the uninstaller cannot reliably determine whether they were installed for another application. After removal, the wrapper prints this optional command:

```bash
sudo pacman -R toxcore libsignal-protocol-c libpulse libpng libjpeg-turbo libwebp \
  ttf-material-symbols-variable qrencode
```

Run it only after confirming that none of those packages predated OmaQ and no other application needs them. `pacman -R` is intentionally used without `-s`: it targets only the listed packages and refuses the complete operation when another installed package requires one. If Pacman reports a dependency conflict, leave the packages installed rather than forcing removal. `zbar` is not an OmaQ installation dependency and is not included.
