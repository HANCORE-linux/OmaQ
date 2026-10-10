import time

from .commands import COMMANDS, HINT, KEYS, TIPS
from .protocol import FAILURES, REDEEMED_DIRECT, REDEEMED_GROUP
from .text import (cells, clean, clean_line, clip, clock, day_label, human_size, is_command, progress_bar, shorten,
                   text_width, wrap)


NARROW_BODY = 12
RECEIPTS = ["·", "→", "⇉", "⇉"]
NAME_MAX = 18


class Screen:
    def __init__(self, app):
        self.app = app

    def hint_line(self, cols):
        base = [(" :help   :quit", "dim", "")]
        key, words = TIPS[self.app.tip % len(TIPS)]
        tip = [("   \u00b7   ", "dim", ""), (key, "text", "1"), (" " + words, "dim", "")]
        label = self.zoom_label()
        reserve = text_width(label) + 3 if label else 1
        used = lambda segs: sum(text_width(text) for text, _, _ in segs)
        if label and used(base) + reserve > cols:
            label, reserve = "", 1
        segs = base + tip if used(base + tip) + reserve <= cols else base
        if label:
            segs += [(" " * (cols - used(segs) - text_width(label) - 1), "dim", ""),
                     (label, "dim" if self.app.zoom == 100 else "accent", "")]
        return self.app.palette.row(segs, cols)

    def zoom_label(self):
        return "Text %d%%" % self.app.zoom if self.app.zoom is not None and time.monotonic() < self.app.zoom_until else ""

    def zoom_line(self, text, cols):
        label = self.zoom_label()
        gap = cols - text_width(text) - text_width(label) - 1
        if not label or gap < 2:
            return self.app.palette.paint(text, "dim")
        return self.app.palette.row([(text, "dim", ""), (" " * gap, "dim", ""),
                                 (label, "dim" if self.app.zoom == 100 else "accent", "")], cols)

    def overlay_rows(self, width, height):
        p = self.app.palette
        o = self.app.overlay
        rows = []
        if o["kind"] == "invite":
            left = max(0, self.app.invite.expires - int(time.time()))
            valid = "valid %d h %02d min" % (left // 3600, left % 3600 // 60) if left else "expired; :invite creates a new link"
            rows.append(p.row([(" invite", "name", "1"), ("   %s · esc close" % valid, "dim", "")], width))
            url = clean_line(o["url"])
            step = max(8, width - 2)
            chunks = [url[k:k + step] for k in range(0, len(url), step)] or [""]
            for chunk in chunks:
                rows.append(p.row([(" " + chunk, "text", "")], width))
            qr = o["qr"]
            notes = (self.text_rows("Send the link or let your friend scan the code. They run :redeem <link>,", "dim", width)
                     + self.text_rows("then :requests shows their request here and :accept adds them.", "dim", width))
            if qr and max(text_width(line) for line in qr) + 2 <= width and len(qr) + len(notes) + 2 + len(chunks) <= height:
                for line in qr:
                    rows.append(p.row([(" ", "dim", ""), (line, "qr", "")], width))
            elif not qr:
                rows.extend(self.text_rows("no QR code: qrencode is not installed", "dim", width))
            else:
                rows.extend(self.text_rows("the window is too small for the QR code", "dim", width))
            rows.extend(notes)
        elif o["kind"] == "request" and self.app.request:
            r = self.app.request
            if r.kind == "group":
                rows.append(p.row([(" group invitation", "name", "1"), ("   :accept or :decline · esc close", "dim", "")], width))
                rows.extend(self.text_rows("A friend invited you to a private group. Group chats show in the bar for now.", "dim", width))
            else:
                rows.append(p.row([(" friend request", "name", "1"), ("   :accept or :decline · esc close", "dim", "")], width))
                rows.extend(self.text_rows("Compare this code with your friend before accepting", "dim", width))
                rows.extend(self.code_rows(r.safety, width))
                if r.conflict:
                    rows.extend(self.text_rows("Another device used this invite link", "unread", width))
        elif o["kind"] == "redeemed":
            rows.append(p.row([(" " + (REDEEMED_GROUP if o.get("group") else REDEEMED_DIRECT), "name", "1"),
                               ("   esc close", "dim", "")], width))
            if o.get("safety"):
                rows.extend(self.text_rows("Compare this code with your friend before they accept", "dim", width))
                rows.extend(self.code_rows(o["safety"], width))
        else:
            self.app.overlay = None
            return [" " * width] * height
        rows, o["top"], _ = self.window(rows, width, height, o.get("top", 0))
        return rows

    def code_rows(self, safety, width):
        rows, per = [], max(1, width // 5)
        for n, half in enumerate(safety.split(" / ")):
            groups = half.split()
            if n and len(groups) > per:
                rows.append(" " * width)
            for k in range(0, len(groups), per):
                rows.append(self.app.palette.row([(" " + " ".join(groups[k:k + per]), "text", "")], width))
        return rows

    def text_rows(self, text, role, width):
        return [self.app.palette.row([(" " + line, role, "")], width) for line in wrap(text, width - 2)]

    def window(self, rows, width, height, top, keep=None):
        if len(rows) <= height:
            return rows + [" " * width] * (height - len(rows)), 0, len(rows)
        if height < 3:
            top = max(0, min(top if keep is None else keep, len(rows) - height))
            return rows[top:top + height], top, height
        room = height - 2
        if keep is not None:
            if keep < top:
                top = keep
            elif keep >= top + room:
                top = keep - room + 1
        top = max(0, min(top, len(rows) - room))
        shown = rows[top:top + room]
        above, below = top, len(rows) - top - len(shown)
        head = self.app.palette.row([("   \u2191 %d more \u00b7 pgup" % above, "dim", "")], width) if above else " " * width
        foot = self.app.palette.row([("   \u2193 %d more \u00b7 pgdn" % below, "dim", "")], width) if below else " " * width
        return [head] + shown + [foot], top, len(shown)

    def picker_rows(self, width, height):
        p = self.app.palette
        items = self.app.picker_items()
        per_row = max(1, (width - 1) // 4)
        index = min(self.app.picker["index"], max(0, len(items) - 1))
        self.app.picker["index"] = index
        lines = max(1, -(-len(items) // per_row))
        grid = max(1, min(4, lines, height - 2))
        first = max(0, min(index // per_row - grid + 1, lines - grid))
        rows = [p.row([(" emoji › ", "accent", "1"), (self.app.picker["query"], "text", ""),
                       ("   arrows choose · enter inserts · esc closes", "dim", "")], width)]
        for r in range(first, first + grid):
            segs = [(" ", "dim", "")]
            for k in range(r * per_row, min(len(items), (r + 1) * per_row)):
                segs.append((" %s " % items[k][0], "accent" if k == index else "text", "1;7" if k == index else ""))
            rows.append(p.row(segs, width))
        rows.append(p.row([(" " + (items[index][1] if items else "no emoji found"), "dim", "")], width))
        return rows[:max(1, height)]

    def command_hint(self, cols):
        p = self.app.palette
        pane = self.app.pane or self.app.scratch
        if not is_command(pane.draft):
            return self.hint_line(cols)
        if self.app.completion is not None:
            kind, items, chosen = self.app.completion["kind"], self.app.completion["items"], self.app.completion["index"]
        else:
            kind, _, items = self.app.suggestions(pane.draft)
            chosen = 0
        if not items:
            text = " no matching friend" if kind == "name" else (" no matching command" if kind == "verb" else HINT)
            return p.paint(clip(text, cols)[0], "dim")
        segs = [(" ", "dim", "")]
        for k, item in enumerate(items):
            label = (":" + item) if kind == "verb" else item
            segs.append((label, "name" if k == chosen else "dim", "1;7" if k == chosen else ""))
            segs.append(("  ", "dim", ""))
        return p.row(segs, cols)

    def help_rows(self, width, height):
        p = self.app.palette
        rows = [p.row([(" commands", "name", "1"), ("   ↑↓ select · enter run · esc close", "dim", "")], width)]
        for k, (command, text, needs) in enumerate(COMMANDS):
            chosen = k == self.app.help
            label = command + needs
            rows.append(p.row([(" ▸ " if chosen else "   ", "accent", "1"), (label.ljust(18), "text" if chosen else "name", "7" if chosen else ""),
                               ("  " + text, "text" if chosen else "dim", "")], width))
        rows.append(" " * width)
        rows.append(p.row([(" receipts", "name", "1"), ("   ", "dim", ""), (RECEIPTS[0], "dim", ""), (" sending   ", "dim", ""),
                           (RECEIPTS[1], "dim", ""), (" sent   ", "dim", ""), (RECEIPTS[2], "dim", ""),
                           (" delivered   ", "dim", ""), (RECEIPTS[3], "online", "1"), (" read", "dim", "")], width))
        rows.append(" " * width)
        rows.append(p.row([(" keys", "name", "1")], width))
        for key, text in KEYS:
            rows.append(p.row([("   " + key.ljust(18), "text", ""), ("  " + text, "dim", "")], width))
        rows, self.app.help_top, span = self.window(rows, width, height, self.app.help_top,
                                                1 + self.app.help if self.app.help_follow else None)
        self.app.help_seen = (self.app.help_top, span)
        return rows

    def conn_label(self):
        return {"online": "Online", "locked": "Locked", "reconnecting": "Reconnecting…",
                "helper_down": "Helper not running"}.get(self.app.conn, "Connecting…")

    def peer_status(self, conversation):
        if self.app.conn != "online":
            return ("reconnecting…" if self.app.conn == "reconnecting" else "connecting…"), "dim"
        c = self.app.convs.get(conversation)
        if c and c["typing_until"]:
            return "typing…", "status"
        f = self.app.friend(conversation)
        return ("online" if f and f["online"] else "offline"), "status"

    def friends_bar(self, cols):
        shown = {pane.conv for pane in self.app.layout.panes()}
        bar = [(" omaq ", "accent", "1"), (self.conn_label(), "online" if self.app.conn == "online" else "dim", ""), ("   ", "dim", "")]
        if self.app.request:
            bar.append(("1 request" if self.app.request.kind == "direct" else "1 group invitation", "unread", "1"))
            bar.append(("   ", "dim", ""))
        if self.app.friends_ready and not self.app.friends:
            bar.append(("no friends yet", "dim", ""))
        used = sum(text_width(text) for text, _, _ in bar)
        hot = lambda f: bool(self.app.unread.get(f["id"]) or f["id"] in self.app.offers) and f["id"] not in self.app.visible
        for k, f in enumerate(self.app.friends):
            role = "online" if f["online"] and f["status"] != "afk" else ("away" if f["online"] else "offline")
            name = shorten(f["name"], NAME_MAX)
            group = [(" %s " % name, "unread", "1;7") if hot(f) else (name, "name", "1" if f["id"] in shown else ""),
                     (" ●", role, ""), ("   ", "dim", "")]
            width = sum(text_width(text) for text, _, _ in group)
            if used + (width - 3 if k == len(self.app.friends) - 1 else width + 4) > cols:
                rest = self.app.friends[k:]
                alert = any(hot(h) for h in rest)
                bar.append(("+%d" % len(rest), "unread" if alert else "dim", "1" if alert else ""))
                break
            bar.extend(group)
            used += width
        return self.app.palette.row(bar, cols)

    def tab_label(self, pane):
        f = self.app.friend(pane.conv)
        return f["name"] if f else (pane.conv or "·")

    def header(self, pane, width, focused, tabs=None):
        if tabs:
            label, role = self.peer_status(pane.conv) if pane.conv else ("", "dim")
            room = width - text_width(label) - 3
            names = [("[%s]" % self.tab_label(t)) if t is pane else self.tab_label(t) for t in tabs]
            start = tabs.index(pane) if pane in tabs else 0
            if sum(text_width(n) + 2 for n in names) <= room:
                start = 0
            segs = [(" ‹ " if start else " ", "dim", "")]
            used = text_width(segs[0][0])
            for k in range(start, len(tabs)):
                piece = names[k] + "  "
                if used + text_width(piece) > room and k != start:
                    segs.append(("› ", "dim", ""))
                    break
                segs.append((piece, "name", "1" if tabs[k] is pane else ""))
                used += text_width(piece)
            segs.append((label, role, ""))
            return self.app.palette.row(segs, width)
        f = self.app.friend(pane.conv)
        label, role = self.peer_status(pane.conv) if pane.conv else ("", "dim")
        segs = [("❯ " if focused else "  ", "accent", "1"),
                (f["name"] if f else (pane.conv or "no chat"), "name", "1" if focused else ""), ("  " + label, role, "")]
        return self.app.palette.row(segs, width)

    def pane_rows(self, pane, width, height, focused, tabs=None):
        rows = [self.header(pane, width, focused, tabs)]
        body = height - 1
        if body > 0 and pane.conv:
            self.app.visible.add(pane.conv)
        lines = self.message_lines(pane.conv, width)
        pane.scroll = min(pane.scroll, max(0, len(lines) - body))
        end = len(lines) - pane.scroll
        visible = lines[max(0, end - body):end] if body > 0 else []
        rows.extend([" " * width] * (body - len(visible)))
        rows.extend(self.app.palette.row(segs, width) for segs in visible)
        return rows

    def render(self):
        cols, rows = self.app.size
        p = self.app.palette
        out = ["", p.paint("─" * cols, "rule")]
        area = max(1, rows - 5)
        drawer = []
        if self.app.picker is not None and not self.app.locked and self.app.help is None and self.app.overlay is None and area > 1:
            drawer = self.picker_rows(cols, area - 1)
            area -= len(drawer)
        self.app.visible = set()
        if self.app.locked:
            out.append(p.row([(" Identity is locked", "unread", "")], cols))
            out.extend([" " * cols] * (area - 1))
        elif self.app.help is not None:
            out.extend(self.help_rows(cols, area))
        elif self.app.overlay is not None:
            out.extend(self.overlay_rows(cols, area))
        elif not self.app.layout.columns:
            out.append(p.row([(" " + self.conn_label(), "dim", "")], cols))
            out.extend([" " * cols] * (area - 1))
        else:
            boxes = self.app.layout.geometry(cols, area)
            focused = self.app.pane
            if boxes is None:
                out.extend(self.pane_rows(focused, cols, area, True, tabs=self.app.layout.panes()))
            else:
                widths = {}
                canvas = [[] for _ in range(area)]
                for c, r, pane, x, y, w, h in boxes:
                    widths[x] = w
                    for k, line in enumerate(self.pane_rows(pane, w, h, pane is focused)):
                        canvas[y + k].append((x, line))
                    if r < len(self.app.layout.columns[c]) - 1:
                        canvas[y + h].append((x, p.paint("─" * w, "rule")))
                for y in range(area):
                    line, at = [], 0
                    for x, text in sorted(canvas[y], key=lambda item: item[0]):
                        if x > at:
                            line.append(p.paint("│", "rule") + " " * (x - at - 1))
                        line.append(text)
                        at = x + widths[x]
                    out.append("".join(line))
        drawer_row = len(out) + 1
        out.extend(drawer)
        out[0] = self.friends_bar(cols)
        hint = self.app.confirm["prompt"] if self.app.confirm is not None and not self.app.locked else (self.app.status or "")
        rule = cols - text_width(hint) - 3 if hint else cols
        out.append(p.paint("─" * max(0, rule), "rule") + (p.paint(" " + clip(hint, cols - 2)[0] + " ", "unread") if hint else ""))
        if self.app.locked:
            prompt = " passphrase › "
            shown = "•" * len(self.app.secret)
            out.append(p.paint(prompt, "accent") + p.paint(clip(shown, max(0, cols - len(prompt) - 1))[0], "text"))
            cursor_col = min(cols, len(prompt) + len(shown) + 1)
        else:
            pane = self.app.pane or self.app.scratch
            f = self.app.friend(pane.conv)
            prompt = [(" ", "text", ""), (" %s " % shorten(f["name"], max(1, cols // 3 - 5)), "accent", "1;7")] if f else []
            prompt.append((" ❯ ", "accent", "1"))
            used = sum(text_width(text) for text, _, _ in prompt)
            room = max(1, cols - used - 1)
            shown = clean(pane.draft).replace("\n", "↵")
            prefix = cells(shown[:pane.cursor])
            keep, behind = len(prefix), 0
            while keep and behind + prefix[keep - 1][1] <= room - 1:
                keep -= 1
                behind += prefix[keep][1]
            start = sum(len(chunk) for chunk, _ in prefix[:keep])
            out.append(p.row(prompt + [(clip(shown[start:], room)[0], "text", "")], cols))
            cursor_col = used + text_width(shown[start:pane.cursor]) + 1
        if self.app.locked:
            out.append(self.zoom_line(clip(" enter unlock · esc clear · ctrl+c quit", cols)[0], cols))
        else:
            out.append(self.command_hint(cols))
        frame = ["\x1b[?2026h\x1b[?25l"]
        for i, line in enumerate(out[:rows]):
            frame.append("\x1b[%d;1H%s\x1b[0m\x1b[K" % (i + 1, line))
        cursor_row = max(1, min(rows, len(out)) - 1)
        if drawer:
            cursor_row, cursor_col = drawer_row, text_width(" emoji › " + self.app.picker["query"]) + 1
        frame.append("\x1b[%d;%dH\x1b[?25h\x1b[?2026l" % (cursor_row, max(1, cursor_col)))
        return "".join(frame)

    def message_lines(self, conversation, cols):
        c = self.app.convs.get(conversation)
        if not c:
            return []
        if not c["loaded"] and not c["items"]:
            return [[("   loading…", "dim", "")]] + self.transfer_lines(conversation)
        f = self.app.friend(conversation)
        peer = f["name"] if f else "peer"
        now = time.time()
        lines = []
        shown_day = None
        for m in c["items"]:
            if m.ts:
                local = time.localtime(m.ts)
                day = (local.tm_year, local.tm_yday)
                if day != shown_day:
                    shown_day = day
                    lines.append([(" ── %s ──" % day_label(m.ts, now), "dim", "")])
            when = clock(m.ts)
            who = "you" if m.dir == "out" else ("·" if m.dir == "sys" else (clean_line(m.sender) or peer))
            who = shorten(who, max(4, cols // 3))
            who_role = "text" if m.dir == "out" else ("dim" if m.dir == "sys" else "name")
            pw = text_width(" %s  %s › " % (when, who))
            narrow = cols - pw - 1 < NARROW_BODY
            pad = 3 if narrow else pw
            body = wrap(clean(m.text), max(1, cols - pad - 1))
            extra = ""
            if m.dir == "out" and m.failed:
                label, role = "✕ " + FAILURES.get(m.failed, "Message was not sent"), "unread"
            elif m.dir == "out":
                label = RECEIPTS[min(m.ack, 3)]
                role, extra = ("online", "1") if m.ack >= 3 else ("dim", "")
            else:
                label, role = "", "dim"
            if narrow:
                lines.append([(" %s  " % when, "dim", ""), (who, who_role, ""), (" ›", "dim", "")])
            for k, part in enumerate(body):
                segs = []
                if k == 0 and not narrow:
                    segs.append((" %s  " % when, "dim", ""))
                    segs.append((who, who_role, ""))
                    segs.append((" › ", "dim", ""))
                else:
                    segs.append((" " * pad, "dim", ""))
                segs.append((part, "dim" if m.dir == "sys" else "text", ""))
                if label and k == len(body) - 1:
                    if pad + text_width(part) + 2 + text_width(label) <= cols:
                        segs.append(("  ", "dim", ""))
                        segs.append((label, role, extra))
                    else:
                        lines.append(segs)
                        segs = [(" " * pad, "dim", ""), (label, role, extra)]
                lines.append(segs)
        if not c["items"]:
            lines.append([("   no messages yet", "dim", "")])
        return lines + self.transfer_lines(conversation)

    def transfer_lines(self, conversation):
        lines = []
        offer = self.app.offers.get(conversation)
        if offer and offer.accepted and offer.done is not None:
            full, empty, percent = progress_bar(offer.done, offer.size)
            lines.append([(" \u21e3 ", "accent", "1"), ("Receiving %s  " % clean_line(offer.name), "text", ""),
                          (full, "accent", ""), (empty, "rule", ""), (" %d%%" % percent, "dim", "")])
        elif offer:
            hint = ("  checking\u2026" if offer.unconfirmed else
                    "  receiving\u2026" if offer.accepted else "  :acceptfile or :declinefile")
            lines.append([(" \u21e3 ", "accent", "1"),
                          ("File offer: %s (%s)" % (clean_line(offer.name), human_size(offer.size)), "text", ""),
                          (hint, "dim", "")])
        send = self.app.sends.get(conversation)
        if send and send.done is not None:
            full, empty, percent = progress_bar(send.done, send.size)
            lines.append([(" \u21e1 ", "accent", "1"), ("Sending %s  " % clean_line(send.name), "text", ""),
                          (full, "accent", ""), (empty, "rule", ""), (" %d%%" % percent, "dim", ""),
                          ("  :cancelfile", "dim", "")])
        elif send:
            lines.append([(" \u21e1 ", "accent", "1"), ("Sending %s" % clean_line(send.name), "text", ""),
                          ("  :cancelfile", "dim", "")])
        return lines
