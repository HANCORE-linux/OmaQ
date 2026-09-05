# Protocol 15: confirmed local hangup

Protocol 15 makes call teardown helper-authoritative and fail-closed. It confirms the local stop without claiming that the peer received a cancellation over an unavailable network.

## Call identity and control

Each endpoint assigns a random 16-hex `callId` to its local view of a call. Call IDs are local correlation values; the two peers do not share one ID.

`call.start` requires the stable Direct key and a client request ID. `call.answer`, `call.stop`, and `call.lease` also require the current local `callId`. A Protocol-15 call operation is never placed in the QML retry queue when helper IPC is unavailable.

The socket that starts or answers a call becomes its controller. It renews a four-second lease with the original start or answer request ID. The helper begins local teardown when that socket disconnects or its lease expires. An unanswered incoming call is also canceled when the last UI socket disconnects.

## Teardown sequence

The helper performs teardown in this order:

1. Stop capture and playback, wake and join the audio thread, and clear both bounded PCM rings.
2. Record the terminal result and expose `call.state` as `ending`.
3. Destroy the old ToxAV context so it cannot send another frame.
4. Create a fresh ToxAV context for later calls.
5. Emit `call.stopped` and then `call.state` as `ended`.

`call.stopped` includes:

- `localStopped:true` only after the helper verifies that no call, capture thread, or buffered frame remains;
- `transportClosed:true` only after the old ToxAV context is destroyed;
- `cancelAttempted` and `cancelAccepted`, which describe the local ToxAV cancellation call;
- `audioAvailable`, which reports whether the replacement ToxAV context is ready;
- the local `callId`, teardown reason, and the controlling action or stop request ID when available.

The helper retains a bounded ring of nine recent per-request terminal results and replays them in order during a same-instance status handshake. Concurrent stop requests for the same call receive separately correlated terminal records, and the controlling start or answer request is retained alongside a different stop request. An unresolved start or answer request survives transient IPC loss until its control-loss terminal record arrives. A stale call ID or mismatched request cannot complete pending QML call control.

## UI behavior

The chat disables call actions and shows **Ending…** while teardown is pending. It shows **Call ended** only after an authoritative terminal event. A failed local cancellation request is reported as a locally ended call whose peer notification was not confirmed. IPC loss and lease expiry are reported as locally completed control-loss teardown.

A network failure can prevent delivery of `TOXAV_CALL_CONTROL_CANCEL`. Protocol 15 therefore guarantees only that OmaQ has stopped local capture and transmission; it does not claim peer receipt.

## Validation

The focused C tests verify local call-state, thread, and buffer shutdown even when the cancellation call fails. The QML fixture verifies capability gating, non-queued call control, strict request and call-ID correlation, duplicate-action blocking, unresolved-action preservation across same-instance reconnect, explicit reset after a helper-instance change or incompatibility result, terminal feedback, and lease failure behavior. The two-client phase 6 test covers request-correlated decline and hangup, replay of multiple terminal results, ToxAV replacement, owner-socket loss, lease expiry, and silence after confirmed local teardown using isolated PulseAudio null sinks.

These automated checks do not replace live microphone, speaker, separate-network, or native Wayland acceptance.
