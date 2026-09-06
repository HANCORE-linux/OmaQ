# Releasing OmaQ

Source updates delivered by `scripts/update-omaq.sh` are refused unless the
target commit on `main` carries an annotated release tag whose SSH signature
verifies against `scripts/release-signers`. Untagged pushes to `main` are
intentionally not installable by the updater.

The updater reads `scripts/release-signers` from the **already installed**
checkout, never from the freshly fetched clone, so a compromised `main` cannot
introduce its own trust root. OpenPGP and X.509 verification are disabled for
this check, so only the in-tree SSH keys can satisfy it.

## One-time maintainer setup

    ssh-keygen -t ed25519 -C release@omaq -f ~/.ssh/omaq-release

Keep `~/.ssh/omaq-release` offline; only `omaq-release.pub` material appears
in the repository. `scripts/release-signers` holds one line per trusted key
(`#` comments and blank lines are ignored):

    release@omaq namespaces="git" ssh-ed25519 <base64-key>

### Adopting the trust root

The key currently pinned in `scripts/release-signers` was generated when this
verification was introduced. Before the first signed release, the maintainer
must replace it with a key whose private half is held offline by the
maintainer alone: generate as above, replace the `ssh-ed25519 …` line, and
commit that change. Every later rotation follows the rules below.

## Cutting a release

1. Merge the release commit to `main`.
2. Tag and sign it (tag names must match `v<digit>…`, for example `v0.8.2`):

       git -c gpg.format=ssh -c user.signingkey=~/.ssh/omaq-release \
           tag -s v0.8.2 -m "OmaQ 0.8.2"

3. Verify locally before pushing (the same check the updater runs):

       git -c gpg.ssh.allowedSignersFile=scripts/release-signers \
           verify-tag v0.8.2

4. Push atomically: `git push origin main v0.8.2`

Tag immediately after merging. Between the merge and the tag, installations
that already enforce verification correctly refuse to update.

## Key rotation

Add the new key line to `scripts/release-signers` in a release signed by the
old key; remove the old line in a later release signed by the new key. Never
remove the only key in the same release that introduces its replacement.

## Revocation / compromise

Rotate as above from a clean machine. Installations verify against the
signers file of their *installed* tree, so a compromised key can still sign
one malicious release for users who have not yet updated past the rotation —
announce compromises out of band.

## What verification does not cover

- The first install still trusts the initial clone. Bootstrap from an exact
  commit as described in the [installation guide](INSTALLATION.md), and
  compare `scripts/release-signers` against an out-of-band copy when the
  threat model warrants it.
- Installations running a release older than this feature use their old
  updater once; enforcement begins for every update performed after a
  verifying release is installed.
