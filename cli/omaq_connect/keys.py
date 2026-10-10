import codecs


class Keys:
    SPECIAL = {"A": "up", "B": "down", "C": "right", "D": "left", "H": "home", "F": "end", "Z": "backtab",
               "I": "focus-in", "O": "focus-out", "P": "f1"}
    TILDE = {"1": "home", "3": "delete", "4": "end", "5": "pgup", "6": "pgdn", "7": "home", "8": "end",
             "11": "f1", "200": "paste-start", "201": "paste-end"}
    MODS = {"2": "shift-", "3": "alt-", "4": "alt-shift-", "5": "ctrl-"}

    def __init__(self):
        self.pending = b""
        self.decoder = codecs.getincrementaldecoder("utf-8")("replace")
        self.paste = None

    def feed(self, data, final=False):
        self.pending += data
        keys = []
        buf, i = self.pending, 0
        while i < len(buf):
            b = buf[i]
            if self.paste is not None:
                end = buf.find(b"\x1b[201~", i)
                if end < 0:
                    if final:
                        self.paste += self.decoder.decode(buf[i:])
                        i = len(buf)
                    else:
                        keep = max(i, len(buf) - 5)
                        self.paste += self.decoder.decode(buf[i:keep])
                        i = keep
                    break
                self.paste += self.decoder.decode(buf[i:end], final=True)
                keys.append(("paste", self.paste.replace("\r\n", "\n").replace("\r", "\n")))
                self.paste = None
                i = end + 6
                continue
            if b == 0x1B:
                if i + 1 >= len(buf):
                    if final:
                        keys.append(("key", "esc"))
                        i += 1
                    break
                nxt = buf[i + 1]
                if nxt in (0x5B, 0x4F):
                    j = i + 2
                    while j < len(buf) and not (0x40 <= buf[j] <= 0x7E):
                        j += 1
                    if j >= len(buf):
                        if final:
                            i = len(buf)
                        break
                    params, last = buf[i + 2:j].decode("ascii", "replace"), chr(buf[j])
                    i = j + 1
                    fields = params.split(";")
                    mod = self.MODS.get(fields[1] if len(fields) > 1 else "", "")
                    if last == "~":
                        name = self.TILDE.get(fields[0])
                        if name == "paste-start":
                            self.paste = ""
                        elif name:
                            keys.append(("key", mod + name))
                    elif last == "u":
                        if fields[0] == "13":
                            keys.append(("key", (mod or "shift-") + "enter"))
                    elif last in self.SPECIAL:
                        keys.append(("key", mod + self.SPECIAL[last]))
                    continue
                if nxt == 0x1B:
                    keys.append(("key", "esc"))
                    i += 1
                    continue
                if nxt in (0x0D, 0x0A):
                    keys.append(("key", "alt-enter"))
                else:
                    ch = self.decoder.decode(bytes([nxt]))
                    if ch:
                        keys.append(("alt", ch))
                i += 2
                continue
            if b in (0x0D, 0x0A):
                keys.append(("key", "enter"))
            elif b in (0x7F, 0x08):
                keys.append(("key", "backspace"))
            elif b == 0x09:
                keys.append(("key", "tab"))
            elif b < 0x20:
                keys.append(("ctrl", chr(b + 0x60)))
            else:
                j = i
                while j < len(buf) and buf[j] >= 0x20 and buf[j] != 0x7F and buf[j] != 0x1B:
                    j += 1
                text = self.decoder.decode(buf[i:j])
                if text:
                    keys.append(("text", text))
                i = j
                continue
            i += 1
        self.pending = buf[i:]
        return keys
