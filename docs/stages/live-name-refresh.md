# Live name refresh

Tox friend-name callbacks now trigger a fresh bounded Friend projection. Existing Group peer-name callbacks continue to refresh the Group projection. DirectChat headers and every open DirectChat surface read the Friend projection reactively. GroupChat member rows, message sender labels, search sender labels, and typing labels read the Group projection reactively.

No shell restart or polling path is added. The helper remains authoritative for names and emits the same bounded projections used at startup.

Focused source checks bind both native callbacks to their helper projections. An offscreen QML fixture replaces Direct and Group names while preserving their stable keys and verifies that the projected names change. These checks do not replace native Wayland or multi-machine acceptance. The independent review agent was unavailable because the local pi harness referenced a missing executable; this limitation is not being presented as successful validation.
