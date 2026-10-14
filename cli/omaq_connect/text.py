import os
import re
import time
import unicodedata


BIDI = frozenset(list(range(0x202A, 0x202F)) + list(range(0x2066, 0x206A)))


EMOJI = [
    "😀", "🙂", "😉", "😍", "😂", "😅", "🙌", "👍", "👎", "❤\ufe0f", "🔥",
    "✨", "🎉", "🙏", "😮", "😢", "😡", "🤔", "👀", "✅", "👋", "💯", "🤟",
]


EMOTICONS = {
    ":)": "🙂", ":-)": "🙂", ":D": "😀", ":-D": "😀", ";)": "😉",
    ";-)": "😉", ":(": "🙁", ":-(": "🙁", ":'(": "😢", ":P": "😛",
    ":p": "😛", ":-P": "😛", ":-p": "😛", ";P": "😜", ";p": "😜",
    ":O": "😮", ":o": "😮", ":-O": "😮", ":-o": "😮", ":*": "😘",
    ":-*": "😘", ":/": "😕", ":-/": "😕", ":|": "😐", ":-|": "😐",
    "xD": "😆", "XD": "😆", "^^": "😊", "<3": "❤\ufe0f", "(y)": "👍",
}


EMOTICON_ANY = "|".join(re.escape(k) for k in sorted(EMOTICONS, key=len, reverse=True))


EMOTICON_DONE = re.compile(r"(?<!\S)(%s)(?=\s)" % EMOTICON_ANY)


EMOTICON_END = re.compile(r"(?<!\S)(%s)(?=\s|$)" % EMOTICON_ANY)


EMOJI_CATALOG = []


def clean(text):
    out = []
    for ch in str(text):
        code = ord(ch)
        if ch == "\n":
            out.append(ch)
        elif ch == "\t":
            out.append(" ")
        elif code < 0x20:
            out.append(chr(0x2400 + code))
        elif code == 0x7F:
            out.append(chr(0x2421))
        elif 0x80 <= code <= 0x9F or code in BIDI:
            out.append(chr(0xFFFD))
        else:
            out.append(ch)
    return "".join(out)


def clean_line(text):
    return clean(text).replace("\n", " ")


def utf8(text):
    try:
        text.encode("utf-8")
    except UnicodeEncodeError:
        return False
    return True


def outgoing(text):
    text = str(text).replace("\r\n", "\n").replace("\r", "\n")
    return "".join(ch for ch in text if ch in "\n\t" or ord(ch) >= 0x20)


def char_width(ch):
    if unicodedata.combining(ch) or unicodedata.category(ch) in ("Mn", "Me", "Cf"):
        return 0
    return 2 if unicodedata.east_asian_width(ch) in ("W", "F") else 1


WIDTHS = {"vs16": 2, "cluster": False}


def cells(text):
    out, join = [], False
    for ch in text:
        w = char_width(ch)
        if out and (join or (WIDTHS["cluster"] and 0x1F3FB <= ord(ch) <= 0x1F3FF)):
            out[-1] = (out[-1][0] + ch, out[-1][1])
            join = False
        elif w == 0 and out:
            base, bw = out[-1]
            out[-1] = (base + ch, WIDTHS["vs16"] if ch == "\ufe0f" and bw == 1 else bw)
            join = ch == "\u200d" and WIDTHS["cluster"]
        else:
            out.append((ch, w))
    return out


def human_size(size):
    size = float(size or 0)
    for unit in ("B", "KB", "MB", "GB"):
        if size < 1000 or unit == "GB":
            return "%d B" % size if unit == "B" else "%.1f %s" % (size, unit)
        size /= 1000


def progress_bar(done, size, width=10):
    filled = min(width, int(width * done / size)) if size else 0
    return "\u2588" * filled, "\u2591" * (width - filled), (int(done * 100 / size) if size else 0)


def home_path(path):
    home = os.path.expanduser("~")
    path = str(path or "")
    return "~" + path[len(home):] if home and path.startswith(home + "/") else path


def text_width(text):
    return sum(w for _, w in cells(text))


def clip(text, width):
    out, used = [], 0
    for chunk, w in cells(text):
        if used + w > width:
            break
        out.append(chunk)
        used += w
    return "".join(out), used


def stops(text):
    marks, at = [0], 0
    for chunk, _ in cells(text):
        at += len(chunk)
        marks.append(at)
    return marks


def prev_stop(text, i):
    return max([mark for mark in stops(text) if mark < i] or [0])


def next_stop(text, i):
    return min([mark for mark in stops(text) if mark > i] or [len(text)])


def emojify(text, final=True):
    return (EMOTICON_END if final else EMOTICON_DONE).sub(lambda m: EMOTICONS[m.group(1)], text)


def is_command(draft):
    return draft.startswith(":") and (draft.split(None, 1) or [""])[0] not in EMOTICONS


def emoji_name(emoji):
    return unicodedata.name(emoji[0], "").lower()


def emoji_catalog():
    if not EMOJI_CATALOG:
        for cp in (list(range(0x2300, 0x2400)) + list(range(0x2600, 0x27C0)) + list(range(0x2B00, 0x2C00))
                   + [0x303D, 0x3297, 0x3299] + list(range(0x1F000, 0x1FB00))):
            ch = chr(cp)
            if unicodedata.category(ch) == "So" and unicodedata.east_asian_width(ch) == "W" and emoji_name(ch):
                EMOJI_CATALOG.append((ch, emoji_name(ch)))
    return EMOJI_CATALOG


def shorten(text, width):
    if text_width(text) <= width:
        return text
    return clip(text, max(0, width - 1))[0] + "…"


PREVIEW_SPACE = re.compile("[\t\n\x0b\x0c\r \u00a0\u1680\u2000-\u200a\u2028\u2029\u202f\u205f\u3000\ufeff]+")


def grapheme_extension(code):
    return (0x0300 <= code <= 0x036F or 0x1AB0 <= code <= 0x1AFF or 0x1DC0 <= code <= 0x1DFF
            or 0x20D0 <= code <= 0x20FF or 0xFE00 <= code <= 0xFE0F or 0xFE20 <= code <= 0xFE2F
            or 0x1F3FB <= code <= 0x1F3FF or 0xE0100 <= code <= 0xE01EF or 0xE0020 <= code <= 0xE007F)


def grapheme_end(text, start):
    regional = lambda code: 0x1F1E6 <= code <= 0x1F1FF
    end = start + 1
    if regional(ord(text[start])) and end < len(text) and regional(ord(text[end])):
        return end + 1
    while end < len(text):
        code = ord(text[end])
        if grapheme_extension(code):
            end += 1
        elif code == 0x200D:
            end += 2
        else:
            break
    return min(end, len(text))


def compact_preview(text, limit=120):
    source = PREVIEW_SPACE.sub(" ", str(text or "")).strip(" ")
    end = count = 0
    while end < len(source) and count < max(1, limit):
        end = grapheme_end(source, end)
        count += 1
    return source[:end] + "…" if end < len(source) else source


def wrap(text, width):
    width = max(1, width)
    lines = []
    for para in text.split("\n"):
        line, used = "", 0
        for word in para.split(" "):
            ww = text_width(word)
            if line and used + 1 + ww <= width:
                line, used = line + " " + word, used + 1 + ww
                continue
            if line:
                lines.append(line)
                line, used = "", 0
            while ww > width:
                part, _ = clip(word, width)
                if not part:
                    part = word[0]
                lines.append(part)
                word = word[len(part):]
                ww = text_width(word)
            line, used = word, ww
        lines.append(line)
    return lines


def clock(ts):
    return time.strftime("%H:%M", time.localtime(ts)) if ts else "--:--"


def day_label(ts, now=None):
    local = time.localtime(ts)
    today = time.localtime(now if now is not None else time.time())
    day = (local.tm_year, local.tm_yday)
    if day == (today.tm_year, today.tm_yday):
        return "today"
    yesterday = time.localtime(time.mktime((today.tm_year, today.tm_mon, today.tm_mday - 1, 12, 0, 0, 0, 0, -1)))
    if day == (yesterday.tm_year, yesterday.tm_yday):
        return "yesterday"
    label = "%s %d %s" % (time.strftime("%a", local), local.tm_mday, time.strftime("%b", local))
    return label if local.tm_year == today.tm_year else "%s %d" % (label, local.tm_year)
