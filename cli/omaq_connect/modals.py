from dataclasses import dataclass


@dataclass(eq=False, slots=True)
class Confirmation:
    action: str
    prompt: str
    at: float
    until: float
    conv: str = ""
    key: str = ""
    instance: str = ""

    @property
    def kept(self):
        return "Invite kept" if self.action == "revoke" else "Chat kept"

    def expired(self, now):
        return now > self.until
