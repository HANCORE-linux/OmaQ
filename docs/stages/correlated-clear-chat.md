# Correlated Clear Chat

This follow-up extends Protocol 15 additively without changing its published call wire format.

## Capability and rollout

The helper advertises `historyClear:2` in every status snapshot. This capability is independent of the protocol integer so a new QML process can safely run beside an older Protocol-15 helper during an update. New QML disables Clear Chat until the capability is present. The new Protocol-15 helper rejects an older QML process's uncorrelated clear request, so an update overlap can temporarily make Clear Chat unavailable but cannot execute a stale confirmation. Protocol-14 builds retain the legacy clear response and advertise capability version 1, including when a legacy request contains an ID.

## Correlation and failure handling

A capable client sends `history.clear` immediately with a bounded request ID and the current stable Direct key or Group ID. The operation is never stored in the reconnect queue. The helper revalidates the current conversation namespace immediately before the operation and returns `history.clear.succeeded` or `history.clear.failed` with the helper instance, request, conversation, and Direct key when applicable.

QML accepts a result only when every identity and request field matches its live pending operation. Unread and receipt state must persist before history deletion begins. A storage failure that might have removed only part of the live history is reported as `result_unknown`; the UI keeps its projection and blocks another clear until a correlated history reload reconciles it instead of claiming success. Disconnect, helper replacement, identity replacement, incompatibility, and timeout likewise resolve the UI as unknown or failed without claiming success.

An accepted clear request invalidates any older pending history read without clearing the visible projection. Only a confirmed result clears local message and attachment rows. Delayed legacy clear events and stale history responses are ignored. Generic history errors no longer combine one conversation's error with a remembered file path from another event.

## Validation

Focused source, helper IPC, and offscreen QML fixtures cover capability negotiation, immediate non-queued execution, exact Direct and Group result correlation, Direct-to-Group and Group-to-Direct rejection, stale history rejection, pre-delete persistence failure, partial storage deletion, reload-gated retry, malformed request shape, and Protocol-15 rejection of uncorrelated clears. Protocol 14 compiles and retains its legacy response.

These checks do not replace native Wayland or multi-machine acceptance. In this environment, `qmllint` exits with status 255 without diagnostics. The independent review agent was unavailable because the local pi harness referenced a missing executable; neither limitation is being presented as successful validation.
