# Invite redemption feedback

The Add contact field clears programmatically after a successful invitation redemption. Its success feedback now remains visible after that clear and is dismissed only when the user edits the field again.

The field uses `onTextEdited` rather than `onTextChanged` so a binding-driven update cannot look like new user input. Request correlation and helper-authoritative invitation validation are unchanged.

A focused source check and offscreen QML fixture verify both the programmatic clear and the later user edit. These checks do not replace native Wayland acceptance. In this environment, `qmllint` exits with status 255 without diagnostics. The independent review agent was unavailable because the local pi harness referenced a missing executable; neither limitation is being presented as successful validation.
