# Install, update, or remove OmaQ

This guide covers the supported source installation, safe update order, helper status checks, rollback, removal, retained data, and optional package cleanup.

[![OmaQ panel after installation](images/guide/01-panel-home.png)](images/guide/01-panel-home.png)

## Install OmaQ

Arch User Repository (AUR) packaging remains paused. Install the required packages, add the plugin, and build the local helper:

```bash
omarchy pkg add \
  toxcore libsignal-protocol-c libpulse libpng libjpeg-turbo libwebp \
  ttf-material-symbols-variable qrencode &&
omarchy plugin add \
  https://github.com/HANCORE-linux/OmaQ.git --enable &&
make -C ~/.config/omarchy/plugins/hancore.omaq helper
```

The repository does not contain a generated `helper/omaq` binary. The local build requires Signal support and refuses direct plaintext fallback.

Verify the plugin and helper after installation:

```bash
omarchy plugin validate ~/.config/omarchy/plugins/hancore.omaq
~/.config/omarchy/plugins/hancore.omaq/scripts/update-helper.sh --status
```

## Update OmaQ

Update the source first. Restart the complete Omarchy shell immediately, then build and activate the helper:

```bash
omarchy plugin update hancore.omaq --yes &&
omarchy restart shell &&
~/.config/omarchy/plugins/hancore.omaq/scripts/update-helper.sh --activate
```

The shell restart clears hot-reload state for every shell plugin before helper activation. This prevents a burst of source-file changes from leaving a fail-closed monitor, such as the network status monitor, locked until a later restart.

The helper updater requires an already running Protocol-9-or-newer helper. First installation uses the separate `make helper` command instead.

### Check update status

Check the running and available helper without building or stopping anything:

```bash
~/.config/omarchy/plugins/hancore.omaq/scripts/update-helper.sh --status
```

The status command reports one of these states:

- **Current**: the running and available helper hashes match
- **Update pending**: the running and available helper hashes differ
- **Inactive**: no helper is running; the next Service start uses the available binary

### Understand activation outcomes

An activation command can report one of these successful outcomes:

- `activated`: the group-free restart and replacement verification completed
- `current`: the running helper already matches the available binary
- `inactive`: the helper exited before activation; the next Service start uses the available binary
- `update-pending`: the running helper remains unchanged and the output includes a reason

A pending result has one of these details:

- `active_groups`: leave every private group, then retry activation
- `group_state_uncertain`: resolve the reported group cleanup or identity state, then retry activation
- `activation_unsupported`: leave the old helper running and use a later full user-session restart to adopt the available binary

Pending results return success because the running helper remains unchanged. OmaQ never signals the helper or uses a legacy unsafe stop.

Activation can fail with a visible `degraded` error when restart verification fails. The retained `.prev` image remains available for rollback, while a later status check may report `inactive`.

### Recover from a degraded activation

Restore the retained helper image through the same locked boundary, restart the shell, and check status:

```bash
~/.config/omarchy/plugins/hancore.omaq/scripts/update-helper.sh --rollback &&
omarchy restart shell &&
~/.config/omarchy/plugins/hancore.omaq/scripts/update-helper.sh --status
```

Do not run another build or plugin update concurrently. The updater serializes its own operations but cannot lock unrelated write commands.

<details>
<summary>How group-safe helper activation works</summary>

Before the normal `make helper` build, the updater copies the verified running `/proc/<pid>/exe` image to `helper/omaq.prev`. A build or synchronous validation failure restores the available path without stopping the running process.

Activation sends only `helper.shutdown_if_no_groups`. The helper confirms durable group-free state before stopping. Service starts the available binary, and the updater verifies its hash, executable identity, protocol marker, and correlated probe.

A group-free restart creates a short offline window. In-flight messages become `delivery_unknown`, while active transfers, calls, and invitations fail visibly. No signal fallback, updater-owned download, staging build, binary hot-swap, or crash journal is used.

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
