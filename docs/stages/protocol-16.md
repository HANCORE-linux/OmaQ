# Protocol 16: direct invite fingerprints

Protocol 16 exposes the pending direct requester's Tox identity before acceptance and reports a second valid claimant without changing the first-writer-wins pending state.

## Events

A pending direct request emits:

```json
{"event":"request","kind":"direct","key":"<64 lowercase hex>","safety":"<canonical two-party safety code>"}
```

`key` is copied from the exact public key stored in `g_pending_invite`. `safety` is produced by `omaq_safety_code(self_pk, peer_pk)`, the same helper function used by the later correlated `safety.get` operation. A status request re-announces the pending request from that same stored key. Group request events are unchanged.

A successful direct redemption emits the same pair identity to the correlated requester before the invitation owner decides:

```json
{"event":"invite.redeemed","kind":"direct","request":"<operation ID>","key":"<issuer 64-hex public key>","safety":"<canonical two-party safety code>"}
```

The redeemer and invitation owner therefore receive byte-identical codes computed by their helpers from the same two public keys. Protocol 16 QML rejects a malformed direct redemption event instead of presenting it as verified. Group redemption events remain unchanged.

After the issued ID, Ratchet-key syntax, and expiry have all passed validation, a different public key that attempts to claim an already claimed direct invitation can emit once per bounded, rate-limited attempting key:

```json
{"event":"request.conflict","kind":"direct","key":"<attempting 64-hex public key>"}
```

The event does not replace or mutate the pending public key or Ratchet key. A retry from the existing claimant does not create another conflict. If group authorization temporarily delays the request card, the helper records a conflict and emits it only after the request event; the conflict can never precede its card. A status projection replays the stored request first and then its stored conflict keys so a shell reload cannot hide the warning; this is state replay, not a new conflict. If the bounded conflict ledger is full, the helper suppresses additional conflict keys rather than evicting an earlier key and producing duplicate warnings.

Protocol 16 direct accept and decline operations carry the displayed public key and are immediate-only in QML. The helper rejects a missing, malformed, or stale key without changing the pending request. QML keeps the request card until the helper's empty `invite` event authoritatively confirms the clear; a transport write alone does not hide it. Accepting or declining a direct request completely clears the invitation and then rotates the Tox nospam value. The public key and existing contacts do not change. The issued ID is already invalid even if the best-effort persisted rotation reports `nospam_rotate_failed`.

Creating an untargeted invitation is rejected while a claim, group authorization, group request, or uncommitted group-binding acceptance is pending. An established group-binding proof awaiting only peer acknowledgement does not block new invitations. Every successful untargeted issue clears the prior invitation type, pending claim, and conflict ledger before publishing the replacement.

## Compatibility and UI

The QML capability is enabled only when `activeHelperProtocol >= 16`. Protocol 15 helpers remain accepted and show the existing request card without a pre-acceptance code or conflict banner. Protocol 14 keeps its legacy Clear Chat behavior, while Protocol 15 call control and correlated Clear Chat remain unchanged.

The pending card shows **Friend request**, the canonical safety code in two identity groups, and **Compare this code with your friend before accepting**. The redeemer's correlated success card shows the same code with **Compare this code with your friend before they accept**. A conflict adds **Another device used this invite link**. The direct decline action is labelled **Decline and revoke link**. No raw 76-character Tox address is displayed, and no request is accepted automatically.

This safety code identifies the pair of Tox public keys only. It does not authenticate the Signal Ratchet identity and does not change the Ratchet trust boundary.

## Validation

C coverage verifies first-writer state, one conflict result per different key, unchanged pending public key and Ratchet key, key-bound decisions, complete invitation reset, busy issue rejection, request and redemption event formatting, and bounded output. IPC regression coverage verifies the Protocol 16 request, conflict, and redeemed schemas. QML fixtures verify Protocol 15 fallback and accessibility text, Protocol 16 schema gating on both request sides, code and conflict presentation, rejection without optimistic hiding, authoritative clear projection, and the immediate, key-bound revoking decline decision. Phase 6 compares the request key with the redeemer's real Tox public key, requires byte-identical safety codes on both devices before acceptance and from `safety.get` after acceptance, exercises same-key status re-announcement and conflict-state replay, rejects issue replacement and a stale key-bound decision without mutating pending state, and verifies that a second identity creates one conflict without becoming a contact.

Automated fixtures do not replace native Wayland, multi-monitor, or separate-network acceptance.
