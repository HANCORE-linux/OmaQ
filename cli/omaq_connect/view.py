import time

from .commands import HINT, RECEIPTS, TIPS, suggestions, typed_path
from .protocol import EDITED, FAILURES, shown_text
from .text import (cells, clean, clean_line, clip, clock, compact_preview, day_label, human_size, is_command,
                   progress_bar, shorten, text_width, wrap)


NARROW_BODY = 12
NAME_MAX = 18
TRANSFER_NAME_MIN = 8


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
        return self.app.overlay.rows(self, width, height)

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

    def select_hint(self, cols):
        segs = [(" \u2191/\u2193", "text", "1"), (" choose \u00b7 ", "dim", ""), ("enter", "text", "1"),
                (" reply \u00b7 ", "dim", ""), ("esc", "text", "1"), (" cancel", "dim", "")]
        return self.app.palette.row(segs, cols)

    def reply_preview(self, conversation, mid):
        c = self.app.chat.convs.get(conversation)
        for m in (c.items if c and mid else []):
            if m.id == mid:
                return clean_line(compact_preview(shown_text(m)))
        return ""

    def reply_row(self, pane, cols):
        label = " \u21a9 Reply: "
        preview = self.reply_preview(pane.conv, pane.reply)
        return self.app.palette.row([(label, "accent", "1"),
                                     (shorten(preview, max(1, cols - text_width(label) - 1)), "text", "")], cols)

    def command_hint(self, cols):
        p = self.app.palette
        pane = self.app.pane or self.app.scratch
        if pane.select is not None:
            return self.select_hint(cols)
        if not is_command(pane.draft):
            return self.hint_line(cols)
        if self.app.completion is not None:
            kind, items, chosen = self.app.completion.kind, self.app.completion.items, self.app.completion.index
        else:
            kind, _, items = suggestions(pane.draft, [f["name"] for f in self.app.friends])
            chosen = 0
        if not items:
            typed = typed_path(pane.draft)
            if typed is not None:
                if self.app.file_picker is None:
                    text = " tab lists folders and files"
                elif self.app.file_picker.room == 0:
                    text = " enter sends the line as typed"
                elif not self.app.file_picker.items(typed):
                    text = " no match · enter sends the line as typed"
                else:
                    text = " enter opens or takes · enter again sends"
            else:
                text = " no matching friend" if kind == "name" else (" no matching command" if kind == "verb" else HINT)
            return p.paint(clip(text, cols)[0], "dim")
        segs = [(" ", "dim", "")]
        for k, item in enumerate(items):
            label = (":" + item) if kind == "verb" else item
            segs.append((label, "name" if k == chosen else "dim", "1;7" if k == chosen else ""))
            segs.append(("  ", "dim", ""))
        return p.row(segs, cols)

    def conn_label(self):
        return {"online": "Online", "locked": "Locked", "reconnecting": "Reconnecting…",
                "helper_down": "Helper not running"}.get(self.app.conn, "Connecting…")

    def peer_status(self, conversation):
        if self.app.conn != "online":
            return ("reconnecting…" if self.app.conn == "reconnecting" else "connecting…"), "dim"
        c = self.app.chat.convs.get(conversation)
        if c and c.typing_until:
            return "typing…", "status"
        f = self.app.friend(conversation)
        return ("online", "status") if f and f["online"] else ("offline", "offline")

    def presence(self, f):
        return "online" if f["online"] and f["status"] != "afk" else ("away" if f["online"] else "offline")

    def friends_bar(self, cols):
        shown = {pane.conv for pane in self.app.layout.panes()}
        bar = [(" omaq ", "accent", "1"), (self.conn_label(), "online" if self.app.conn == "online" else "dim", ""), ("   ", "dim", "")]
        if self.app.invites.request:
            bar.append(("1 request" if self.app.invites.request.kind == "direct" else "1 group invitation", "unread", "1"))
            bar.append(("   ", "dim", ""))
        if self.app.friends_ready and not self.app.friends:
            bar.append(("no friends yet", "dim", ""))
        used = sum(text_width(text) for text, _, _ in bar)
        hot = lambda f: bool(self.app.chat.unread.get(f["id"]) or f["id"] in self.app.files.offers) and f["id"] not in self.app.visible
        for k, f in enumerate(self.app.friends):
            role = self.presence(f)
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

    def name_segs(self, conversation, text, extra):
        f = self.app.friend(conversation)
        if self.app.conn != "online" or not f:
            return [(text, "name", extra)]
        role = self.presence(f)
        return [(text, "name" if role == "offline" else "text", extra), (" ●", role, "")]

    def tab_label(self, pane):
        f = self.app.friend(pane.conv)
        return f["name"] if f else (pane.conv or "·")

    def header(self, pane, width, focused, tabs=None):
        if tabs:
            label, role = self.peer_status(pane.conv) if pane.conv else ("", "dim")
            room = width - text_width(label) - 3
            names = [(("[%s]" % self.tab_label(t)) if t is pane else self.tab_label(t))
                     + "".join(text for text, _, _ in self.name_segs(t.conv, "", "")) for t in tabs]
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
                bold = "1" if tabs[k] is pane else ""
                name, *dot = self.name_segs(tabs[k].conv, self.tab_label(tabs[k]), bold)
                segs.extend(([("[", name[1], bold), name, ("]", name[1], bold)] if tabs[k] is pane else [name]) + dot + [("  ", "dim", "")])
                used += text_width(piece)
            segs.append((label, role, ""))
            return self.app.palette.row(segs, width)
        f = self.app.friend(pane.conv)
        label, role = self.peer_status(pane.conv) if pane.conv else ("", "dim")
        segs = ([("❯ " if focused else "  ", "accent", "1")]
                + self.name_segs(pane.conv, f["name"] if f else (pane.conv or "no chat"), "1" if focused else "")
                + [("  " + label, role, "")])
        return self.app.palette.row(segs, width)

    def pane_rows(self, pane, width, height, focused, tabs=None):
        rows = [self.header(pane, width, focused, tabs)]
        body = height - 1
        if body > 0 and pane.conv:
            self.app.visible.add(pane.conv)
        pinned = self.transfer_lines(pane.conv, width)[:max(0, body)] if pane.conv else []
        room = body - len(pinned)
        lines, span = self.message_block(pane.conv, width, pane.select)
        if span is not None and room > 0:
            first, last = span
            if last >= len(lines) - pane.scroll:
                pane.scroll = len(lines) - 1 - last
            elif first < len(lines) - pane.scroll - room:
                pane.scroll = len(lines) - first - room
        pane.scroll = min(max(0, pane.scroll), max(0, len(lines) - room))
        end = len(lines) - pane.scroll
        visible = lines[max(0, end - room):end] if room > 0 else []
        rows.extend([" " * width] * (room - len(visible)))
        rows.extend(self.app.palette.row(segs, width) for segs in visible + pinned)
        return rows

    def render(self):
        cols, rows = self.app.size
        p = self.app.palette
        out = ["", p.paint("─" * cols, "rule")]
        composer = self.app.pane or self.app.scratch
        replying = bool(composer.reply) and not self.app.locked and self.app.secret_prompt is None
        area = max(1, rows - (6 if replying else 5))
        drawer = []
        modal = self.app.picker if self.app.picker is not None else self.app.file_picker
        if modal is not None and not self.app.locked and self.app.help is None and self.app.overlay is None and area > 1:
            drawer = modal.rows(self, cols, area - 1)
            area -= len(drawer)
        if self.app.file_picker is not None and (modal is not self.app.file_picker or not drawer):
            self.app.file_picker.room = 0
        self.app.visible = set()
        if self.app.locked:
            out.append(p.row([(" Identity is locked", "unread", "")], cols))
            out.extend([" " * cols] * (area - 1))
        elif self.app.help is not None:
            out.extend(self.app.help.rows(self, cols, area))
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
        hint = self.app.confirm.prompt if self.app.confirm is not None and not self.app.locked else (self.app.status or "")
        rule = cols - text_width(hint) - 3 if hint else cols
        out.append(p.paint("─" * max(0, rule), "rule") + (p.paint(" " + clip(hint, cols - 2)[0] + " ", "unread") if hint else ""))
        if replying:
            out.append(self.reply_row(composer, cols))
        if self.app.locked:
            prompt = " passphrase › "
            shown = "•" * len(self.app.secret)
            out.append(p.paint(prompt, "accent") + p.paint(clip(shown, max(0, cols - len(prompt) - 1))[0], "text"))
            cursor_col = min(cols, len(prompt) + len(shown) + 1)
        elif self.app.secret_prompt is not None:
            prompt = " %s › " % self.app.secret_prompt.label
            shown = "•" * len(self.app.secret_prompt.value)
            out.append(p.paint(prompt, "accent") + p.paint(clip(shown, max(0, cols - text_width(prompt) - 1))[0], "text"))
            cursor_col = min(cols, text_width(prompt) + len(shown) + 1)
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
        elif self.app.secret_prompt is not None:
            out.append(self.zoom_line(clip(" enter continues · esc cancels", cols)[0], cols))
        else:
            out.append(self.command_hint(cols))
        frame = ["\x1b[?2026h\x1b[?25l"]
        for i, line in enumerate(out[:rows]):
            frame.append("\x1b[%d;1H%s\x1b[0m\x1b[K" % (i + 1, line))
        cursor_row = max(1, min(rows, len(out)) - 1)
        if drawer and self.app.picker is not None:
            cursor_row, cursor_col = drawer_row, text_width(" emoji › " + self.app.picker.query) + 1
        frame.append("\x1b[%d;%dH\x1b[?25h\x1b[?2026l" % (cursor_row, max(1, cursor_col)))
        return "".join(frame)

    def message_lines(self, conversation, cols):
        return self.message_block(conversation, cols)[0]

    def message_block(self, conversation, cols, selected=None):
        c = self.app.chat.convs.get(conversation)
        if not c:
            return [], None
        if not c.loaded and not c.items:
            return [[("   loading…", "dim", "")]], None
        f = self.app.friend(conversation)
        peer = f["name"] if f else "peer"
        now = time.time()
        lines = []
        span = None
        shown_day = None
        by_id = {m.id: m for m in c.items if m.id}
        for m in c.items:
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
            chosen = bool(selected) and m.id == selected
            body_role = "dim" if m.dir == "sys" or m.deleted else "text"
            parts = [(part, body_role, "1" if chosen else "")
                     for part in wrap(clean(shown_text(m)), max(1, cols - pad - 1))]
            target = by_id.get(m.reply) if m.reply else None
            quote = clean_line(compact_preview(shown_text(target))) if target is not None and target is not m else ""
            if quote:
                parts.insert(0, (shorten("\u21a9 " + quote, max(1, cols - pad - 1)), "accent", "1"))
            tail = [(EDITED, "dim", "")] if m.edited and not m.deleted else []
            if m.dir == "out" and m.failed:
                tail.append(("✕ " + FAILURES.get(m.failed, "Message was not sent"), "unread", ""))
            elif m.dir == "out":
                tail.append((RECEIPTS[min(m.ack, 3)],) + (("online", "1") if m.ack >= 3 else ("dim", "")))
            start = len(lines)
            if narrow:
                lines.append([(" %s  " % when, "dim", ""), (who, who_role, ""), (" ›", "dim", "")])
            for k, part in enumerate(parts):
                segs = []
                if k == 0 and not narrow:
                    segs.append((" %s  " % when, "dim", ""))
                    segs.append((who, who_role, ""))
                    segs.append((" › ", "dim", ""))
                else:
                    segs.append((" " * pad, "dim", ""))
                segs.append(part)
                if tail and k == len(parts) - 1:
                    trailer = [seg for label in tail for seg in (("  ", "dim", ""), label)]
                    if pad + text_width(part[0]) + sum(text_width(text) for text, _, _ in trailer) <= cols:
                        segs.extend(trailer)
                    else:
                        lines.append(segs)
                        segs = [(" " * pad, "dim", "")] + trailer[1:]
                lines.append(segs)
            if chosen:
                span = (start, len(lines) - 1)
                for k in range(start, len(lines)):
                    text, role, extra = lines[k][0]
                    lines[k] = [("\u258c", "accent", "1"), (text[1:], role, extra)] + lines[k][1:]
        if not c.items:
            lines.append([("   no messages yet", "dim", "")])
        return lines, span

    def transfer_line(self, cols, icon, text, name, tail=(), hint=""):
        tail = list(tail)
        head = lambda shown: [icon, (text % shown, "text", "")]
        fixed, tail_w = text_width(icon[0]) + text_width(text % ""), sum(text_width(t) for t, _, _ in tail)
        if fixed + text_width(name) + tail_w + (2 + text_width(hint) if hint else 0) <= cols:
            return [head(name) + tail + ([("  " + hint, "dim", "")] if hint else [])]
        if fixed + tail_w + TRANSFER_NAME_MIN <= cols:
            lines = [head(shorten(name, cols - fixed - tail_w)) + tail]
        else:
            if fixed + TRANSFER_NAME_MIN <= cols:
                lines = [head(shorten(name, cols - fixed))]
            else:
                label = wrap((text % "").strip(), max(1, cols - text_width(icon[0])))
                lines = [[icon, (label[0], "text", "")]] + [[("   ", "dim", ""), (part, "text", "")] for part in label[1:]]
                lines.append([("   ", "dim", ""), (shorten(name, max(1, cols - 3)), "text", "")])
            if tail:
                lines.append([("   ", "dim", ""), (tail[0][0].lstrip(), tail[0][1], tail[0][2])] + tail[1:])
        return lines + [[("   " + part, "dim", "")] for part in (wrap(hint, max(1, cols - 3)) if hint else [])]

    def transfer_lines(self, conversation, cols):
        lines = []
        offer = self.app.files.offers.get(conversation)
        if offer and offer.accepted and offer.done is not None:
            full, empty, percent = progress_bar(offer.done, offer.size)
            lines += self.transfer_line(cols, (" \u21e3 ", "accent", "1"), "Receiving %s  ", clean_line(offer.name),
                                        [(full, "accent", ""), (empty, "rule", ""), (" %d%%" % percent, "dim", "")], ":cancelfile")
        elif offer:
            hint = ("checking\u2026" if offer.unconfirmed else
                    "receiving\u2026  :cancelfile" if offer.accepted else ":acceptfile or :declinefile")
            lines += self.transfer_line(cols, (" \u21e3 ", "accent", "1"), "File offer: %s", clean_line(offer.name),
                                        [(" (%s)" % human_size(offer.size), "text", "")], hint)
        send = self.app.files.sends.get(conversation)
        if send and send.done is not None:
            full, empty, percent = progress_bar(send.done, send.size)
            lines += self.transfer_line(cols, (" \u21e1 ", "accent", "1"), "Sending %s  ", clean_line(send.name),
                                        [(full, "accent", ""), (empty, "rule", ""), (" %d%%" % percent, "dim", "")], ":cancelfile")
        elif send:
            lines += self.transfer_line(cols, (" \u21e1 ", "accent", "1"), "Sending %s", clean_line(send.name), hint=":cancelfile")
        canceled = self.app.files.notice(conversation)
        if not lines and canceled is not None:
            lines += self.transfer_line(cols, (" \u2715 ", "unread", "1"), "File transfer canceled: %s", clean_line(canceled),
                                        hint="esc hides")
        return lines
