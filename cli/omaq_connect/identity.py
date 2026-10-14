import os
import secrets
import time

from .commands import IDENTITY_ACTIONS, IDENTITY_PATH_ACTIONS
from .protocol import FAILURES, passphrase_ok
from .text import clean_line

IDENTITY_SECONDS = 20
PATH_BYTES = 512
BUNDLE_NAME = "omaq-identity.save"
USAGE = "Usage: :identity, or :identity protect, unprotect, export [path], validate <path> or import <path>"
TIMED_OUT = "The identity action timed out. Check OmaQ status and try again."
UNKNOWN = "Identity action result unknown. Check OmaQ status and try again."
MISMATCH = "Passphrases do not match."
NOT_KNOWN = "Your identity's protection is not known yet"
NEW_RULE = "Use at least 8 characters and at most 128 bytes."
OLD_RULE = "A passphrase has 1 to 128 bytes."
PROTECT_PROMPT = "Protect your identity? This passphrase cannot be recovered if lost. y/n"
UNPROTECT_PROMPT = "Remove the identity passphrase? The identity is then stored unencrypted. y/n"
IMPORT_PROMPT = "Import %s as the active identity? Existing direct contacts will require a fresh invite. y/n"
EXPORT_PROMPT = "Export your identity to %s? y/n"
REPLACE_PROMPT = "Replace %s with your identity? y/n"
DONE = {
    "protect": "Identity protection enabled.",
    "unprotect": "Identity protection removed.",
    "validate": "Identity bundle validated. Use :identity import to activate it.",
    "import": "Identity imported. Existing direct contacts must be removed on both sides and re-invited before messaging.",
}
WORKING = {
    "protect": "Protecting identity…",
    "unprotect": "Removing identity protection…",
    "export": "Exporting identity…",
    "validate": "Checking identity bundle…",
    "import": "Importing identity…",
}
FORBIDDEN = {
    "protect": "The passphrase could not be saved.",
    "unprotect": "The current identity passphrase is incorrect.",
    "export": "The identity could not be exported to that path.",
    "validate": "Identity file or passphrase is invalid.",
    "import": "Identity file or passphrase is invalid.",
}
ERRORS = {
    "identity_passphrase_required": "Enter the imported identity's passphrase first.",
    "identity_import_failed": "Identity file or passphrase is invalid.",
    "busy": "Finish active calls, transfers, invites, or read receipts first.",
    "unsupported": "This identity action is not supported by the running helper.",
    "locked": "Unlock your identity to continue.",
    "identity_changed": "Identity changed. Pending actions were discarded.",
    "identity_backup_failed": "Could not create a safe identity backup.",
    "identity_backup_cleanup_failed": "An old identity recovery backup could not be removed.",
    "runtime_marker_failed": "Your identity change is saved, but OmaQ could not update its runtime file. Free some disk space, then restart OmaQ; until then, quitting may leave it running.",
    "identity_state_archive_failed": "Could not archive the previous identity's local data.",
    "identity_rollback_failed": "Identity restore failed. The backup was kept in the OmaQ data folder.",
    "identity_missing": "Your existing OmaQ identity is unavailable. No replacement identity was created. Restore or import the original identity.",
    "identity_mismatch": "The active identity does not match OmaQ's protected identity record. No contact state was changed.",
    "identity_guard_invalid": "OmaQ could not verify the protected identity state and stopped before creating a replacement.",
    "identity_recovery_degraded": "Your active identity is saved, but its additional recovery copy could not be updated. Export an identity bundle before relying on automatic recovery.",
    "identity_primary_uncertain": "OmaQ could not confirm that the latest identity/contact change reached disk. The helper stopped using that identity; restart OmaQ and verify the result before retrying.",
    "group_registry_failed": "Could not safely save private group state.",
    "direct_state_migration_failed": "Legacy direct-chat state could not be migrated safely.",
    "direct_state_reinvite_required": "Your OmaQ identity and contacts are intact. Some older direct-chat encryption state was safely archived and requires fresh invites.",
}


def path_ok(path):
    if not path.startswith("/") or ".." in path:
        return False
    if any(ord(ch) < 0x20 or ord(ch) == 0x7F for ch in path):
        return False
    try:
        return len(path.encode("utf-8")) < PATH_BYTES
    except UnicodeEncodeError:
        return False


def given_path(arg):
    given = arg.strip()
    if len(given) > 1 and given[0] == given[-1] and given[0] in "'\"":
        given = given[1:-1]
    if not given:
        return ""
    try:
        return os.path.abspath(os.path.expanduser(given))
    except (OSError, ValueError):
        return None


def export_folder():
    folder = os.environ.get("XDG_DOWNLOAD_DIR") or ""
    return folder if folder.startswith("/") else os.path.join(os.path.expanduser("~"), "Downloads")


class Identity:
    def __init__(self, send, say, ask, confirm, failed):
        self.send, self.say, self.ask, self.confirm, self.failed = send, say, ask, confirm, failed
        self.protected = None
        self.action, self.request, self.until, self.path = "", "", 0.0, ""
        self.replace = False
        self.export_version = 0
        self.first = ""

    def busy(self):
        return bool(self.action)

    def protect_blocker(self):
        if self.protected is None:
            return NOT_KNOWN
        if self.protected:
            return "Your identity already has a passphrase; :identity unprotect removes it first"
        return ""

    def command(self, arg):
        parts = arg.split(None, 1)
        action = parts[0] if parts else ""
        rest = parts[1] if len(parts) > 1 else ""
        if not action:
            if self.protected is None:
                self.say(NOT_KNOWN)
            elif self.protected:
                self.say("Your identity has a passphrase; :identity unprotect removes it")
            else:
                self.say("Your identity has no passphrase; :identity protect sets one")
        elif action not in IDENTITY_ACTIONS or (rest and action not in IDENTITY_PATH_ACTIONS):
            self.say(USAGE)
        elif self.busy():
            self.say("Another identity action is running")
        elif action == "protect":
            blocked = self.protect_blocker()
            if blocked:
                self.say(blocked)
            else:
                self.ask("new passphrase", "new")
        elif action == "unprotect":
            if self.protected is None:
                self.say(NOT_KNOWN)
            elif self.protected is False:
                self.say("Your identity has no passphrase")
            else:
                self.ask("current passphrase", "current")
        else:
            self.path_action(action, rest)

    def path_action(self, action, rest):
        path = given_path(rest)
        if action == "export" and path == "":
            path = os.path.join(export_folder(), BUNDLE_NAME)
        elif action == "export" and path and os.path.isdir(path):
            path = os.path.join(path, BUNDLE_NAME)
        if path == "":
            self.say("Usage: :identity %s <path>" % action)
        elif path is None or not path_ok(path):
            self.say("This path cannot be used: %s" % clean_line(rest.strip() or path))
        elif action == "export" and os.path.lexists(path):
            self.confirm("overwrite", REPLACE_PROMPT % clean_line(path), path)
        elif action == "export" and self.export_version < 2:
            self.say("Helper update required")
        elif action == "export":
            self.confirm("export", EXPORT_PROMPT % clean_line(path), path)
        elif not os.path.isfile(path):
            self.say("No identity bundle at %s" % clean_line(path))
        elif action == "validate":
            self.start("validate", {"op": "identity.inspect", "path": path}, path)
        else:
            self.confirm("import", IMPORT_PROMPT % clean_line(path), path)

    def confirmed(self, action, path, secret=""):
        if self.busy():
            self.say("Another identity action is running")
        elif action == "protect":
            blocked = self.protect_blocker()
            if blocked:
                self.say(blocked)
            else:
                self.start("protect", {"op": "identity.protect", "passphrase": secret}, "")
        elif action == "unprotect":
            self.start("unprotect", {"op": "identity.unprotect", "passphrase": secret}, "")
        elif action in ("export", "overwrite"):
            self.start("export", {"op": "identity.export", "path": path, "replace": action == "overwrite"}, path)
        elif action == "import":
            op = {"op": "identity.import", "path": path, "replace": True}
            if secret:
                op["passphrase"] = secret
            self.start("import", op, path)

    def on_secret(self, purpose, value):
        if purpose == "new":
            if not passphrase_ok(value, new=True):
                self.say(NEW_RULE)
            else:
                self.first = value
                self.ask("repeat passphrase", "repeat")
        elif purpose == "repeat":
            first, self.first = self.first, ""
            blocked = MISMATCH if value != first else self.protect_blocker()
            if blocked:
                self.say(blocked)
            else:
                self.confirm("protect", PROTECT_PROMPT, "", value)
        elif not passphrase_ok(value):
            self.say(OLD_RULE)
        elif purpose == "current":
            self.confirm("unprotect", UNPROTECT_PROMPT, "", value)
        elif purpose == "validate" and self.path and not self.action:
            self.start("validate", {"op": "identity.inspect", "path": self.path, "passphrase": value}, self.path)
        elif purpose == "import" and self.path and not self.action:
            path, self.path = self.path, ""
            self.confirm("import", IMPORT_PROMPT % clean_line(path), path, value)

    def forget_secret(self):
        self.first = ""
        if not self.action:
            self.path = ""

    def cancel_secret(self):
        self.forget_secret()
        self.say("Canceled")

    def start(self, action, op, path):
        request = "cli-identity-" + secrets.token_hex(8)
        if not self.send(dict(op, id=request)):
            self.failed()
            return
        self.action, self.request, self.path = action, request, path
        self.replace = op.get("replace") is True
        self.until = time.monotonic() + IDENTITY_SECONDS
        self.say(WORKING[action], IDENTITY_SECONDS)

    def finish(self):
        self.action, self.request, self.until = "", "", 0.0

    def on_snapshot(self, ev):
        if "protected" in ev:
            self.protected = ev.get("protected") is True

    def on_identity(self, ev):
        if "protected" in ev:
            self.protected = ev.get("protected") is True
        if not (self.request and str(ev.get("request") or "") == self.request):
            return
        action, path = self.action, self.path
        self.finish()
        self.path = ""
        if action == "export":
            shown = clean_line(str(ev.get("path") or path))
            if self.protected is False:
                self.say("Identity exported to %s. It has no passphrase; transfer it securely and delete extra copies."
                         % shown, 20)
            else:
                self.say("Identity exported to %s. Transfer a bundle securely and delete extra copies." % shown, 20)
        else:
            self.say(DONE[action], 20)

    def on_global_error(self, code):
        if code != "runtime_marker_failed":
            return False
        self.say(ERRORS[code], 30)
        return True

    def on_correlated_error(self, code, request):
        if not (self.request and request == self.request):
            return False
        action, path = self.action, self.path
        self.finish()
        if code == "identity_passphrase_required" and action in ("validate", "import"):
            self.ask("bundle passphrase", action)
            return True
        self.path = ""
        if code == "forbidden" and action == "export" and not self.replace and os.path.lexists(path):
            self.say("%s already exists. Export again to replace it." % clean_line(path), 15)
        elif code == "forbidden" and action == "protect" and self.protected:
            self.say(self.protect_blocker(), 15)
        elif code == "forbidden":
            self.say(FORBIDDEN[action], 15)
        else:
            self.say(ERRORS.get(code) or FAILURES.get(code) or code or "The identity action failed.", 15)
        return True

    def tick(self, now):
        if self.action and now > self.until:
            self.finish()
            self.path = ""
            self.say(TIMED_OUT, 15)

    def drop(self):
        self.first = ""
        if self.action:
            self.finish()
            self.say(UNKNOWN, 15)
        self.path = ""

    def reset(self):
        if self.action:
            self.say(UNKNOWN, 15)
        self.protected = None
        self.first, self.path = "", ""
        self.finish()
