pragma Singleton
import QtQuick

// Register the installed dependency in this engine, including after a first
// install into an already running shell. No bundled font or shell restart.
Item {
  id: symbols
  readonly property bool ready: symbolFont.status === FontLoader.Ready &&
    symbolFont.name === "Material Symbols Rounded"
  readonly property string family: ready ? symbolFont.name : "sans-serif"
  // Stable Material Symbols codepoints, checked against the installed font.
  // circle_outline uses circle with FILL=0; remove_reaction uses remove.
  readonly property var glyphs: Object.freeze({
    "add_reaction": "\ue1d3",
    "attach_file": "\ue226",
    "audio_file": "\ueb82",
    "badge": "\uea67",
    "broken_image": "\ue3ad",
    "call": "\ue0b0",
    "call_end": "\ue0b1",
    "chat": "\ue0b7",
    "check": "\ue5ca",
    "checklist": "\ue6b1",
    "chevron_left": "\ue408",
    "chevron_right": "\ue409",
    "circle": "\uef4a",
    "circle_outline": "\uef4a",
    "close": "\ue14c",
    "code": "\ue86f",
    "content_copy": "\ue14d",
    "content_cut": "\ue14e",
    "content_paste": "\ue14f",
    "crown": "\uecb3",
    "delete": "\ue872",
    "draft": "\ue06f",
    "edit": "\ue150",
    "error": "\ue000",
    "expand_more": "\ue5cf",
    "file_open": "\ueaf3",
    "file_upload": "\ue2c6",
    "folder_open": "\ue2c8",
    "format_bold": "\ue238",
    "format_h1": "\uf85d",
    "format_italic": "\ue23f",
    "format_list_bulleted": "\ue241",
    "format_list_numbered": "\ue242",
    "format_quote": "\ue244",
    "format_size": "\ue245",
    "group": "\ue7ef",
    "groups": "\uf233",
    "help": "\ue887",
    "link": "\ue157",
    "lock": "\ue88d",
    "lock_open": "\ue898",
    "logout": "\ue9ba",
    "manage_accounts": "\uf02e",
    "mood": "\ue24e",
    "more_horiz": "\ue5d3",
    "music_note": "\ue3a1",
    "notifications": "\ue7f4",
    "notifications_off": "\ue7f6",
    "palette": "\ue3b7",
    "pause_circle": "\ue035",
    "person": "\ue7fd",
    "person_add": "\ue7fe",
    "person_remove": "\uef66",
    "play_circle": "\ue038",
    "published_with_changes": "\uf232",
    "qr_code_2": "\ue00a",
    "refresh": "\ue5d5",
    "remove_reaction": "\ue15b",
    "reply": "\ue15e",
    "science": "\uea4b",
    "search": "\ue8b6",
    "select_all": "\ue162",
    "send": "\ue163",
    "settings": "\ue8b8",
    "shield": "\ue9e0",
    "shield_person": "\uf650",
    "task_alt": "\ue2e6",
    "text_format": "\ue165",
    "tune": "\ue429",
    "verified": "\ue031",
    "visibility_off": "\ue8f5",
    "warning_amber": "\ue002"
  })

  function glyph(name) {
    var key = String(name || "")
    if (key === "")
      return ""
    // A missing font or unknown name must not paint an overlapping word.
    return ready && Object.prototype.hasOwnProperty.call(glyphs, key)
      ? glyphs[key] : "?"
  }

  FontLoader {
    id: symbolFont
    source: "file:///usr/share/fonts/TTF/MaterialSymbolsRounded%5BFILL,GRAD,opsz,wght%5D.ttf"
  }
}
