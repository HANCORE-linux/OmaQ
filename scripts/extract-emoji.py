#!/usr/bin/env python3
"""Pull native Noto Color Emoji CBDT PNGs and Lanczos-resize to 64px."""
import subprocess
import sys
from pathlib import Path

FONT = Path("/usr/share/fonts/noto/NotoColorEmoji.ttf")
GLYPHS = [
    "😀", "🙂", "😉", "😍", "😂", "😅", "🙌", "👍",
    "👎", "❤️", "🔥", "✨", "🎉", "🙏", "😮", "😢",
    "😡", "🤔", "👀", "✅", "👋", "💯",
]


def u16(b, o):
    return int.from_bytes(b[o : o + 2], "big")


def u32(b, o):
    return int.from_bytes(b[o : o + 4], "big")


def cps_of(s):
    out = []
    i = 0
    while i < len(s):
        c = ord(s[i])
        if 0xD800 <= c <= 0xDBFF and i + 1 < len(s):
            c = 0x10000 + ((c - 0xD800) << 10) + (ord(s[i + 1]) - 0xDC00)
            i += 2
        else:
            i += 1
        if c != 0xFE0F:
            out.append(c)
    return out


def load_font(path):
    font = path.read_bytes()
    n = u16(font, 4)
    tables = {}
    for i in range(n):
        off = 12 + i * 16
        tables[font[off : off + 4].decode("ascii")] = (u32(font, off + 8), u32(font, off + 12))
    cmap = font[tables["cmap"][0] : tables["cmap"][0] + tables["cmap"][1]]
    cmap12 = None
    nrec = u16(cmap, 2)
    for i in range(nrec):
        off = u32(cmap, 8 + i * 8)
        if u16(cmap, off) == 12:
            cmap12 = off
    cp_to_gid = {}
    ng = u32(cmap, cmap12 + 12)
    o = cmap12 + 16
    for _ in range(ng):
        start, end, startgid = u32(cmap, o), u32(cmap, o + 4), u32(cmap, o + 8)
        o += 12
        for cp in range(start, end + 1):
            cp_to_gid[cp] = startgid + (cp - start)
    cblc = font[tables["CBLC"][0] : tables["CBLC"][0] + tables["CBLC"][1]]
    cbdt = font[tables["CBDT"][0] : tables["CBDT"][0] + tables["CBDT"][1]]
    rec = cblc[8:56]
    arr_off = u32(rec, 0)
    nsub = u32(rec, 8)
    gid_loc = {}
    for i in range(nsub):
        eoff = arr_off + i * 8
        first, last, add = u16(cblc, eoff), u16(cblc, eoff + 2), u32(cblc, eoff + 4)
        st = arr_off + add
        image_format = u16(cblc, st + 2)
        image_data = u32(cblc, st + 4)
        for g in range(first, last + 1):
            oi = g - first
            off1 = u32(cblc, st + 8 + oi * 4)
            off2 = u32(cblc, st + 8 + (oi + 1) * 4)
            if off1 == 0 or off2 == 0 or off2 <= off1:
                continue
            gid_loc[g] = (image_format, image_data + off1, off2 - off1)
    return cp_to_gid, gid_loc, cbdt


def glyph_png(gid_loc, cbdt, gid):
    loc = gid_loc.get(gid)
    if not loc:
        return None
    fmt, off, ln = loc
    blob = cbdt[off : off + ln]
    if fmt != 17:
        raise SystemExit(f"unhandled CBDT format {fmt}")
    png = blob[9 : 9 + u32(blob, 5)]
    if png[:4] != b"\x89PNG":
        raise SystemExit("glyph is not PNG")
    return png


def main():
    root = Path(__file__).resolve().parents[1]
    out = root / "assets" / "emoji"
    out.mkdir(parents=True, exist_ok=True)
    if not FONT.is_file():
        raise SystemExit(f"missing {FONT}")
    cp_to_gid, gid_loc, cbdt = load_font(FONT)
    tmp = Path("/tmp/omaq-emoji-native")
    tmp.mkdir(parents=True, exist_ok=True)
    for e in GLYPHS:
        cps = cps_of(e)
        gid = cp_to_gid.get(cps[0]) if len(cps) == 1 else None
        if gid is None:
            raise SystemExit(f"no glyph for {e!r}")
        png = glyph_png(gid_loc, cbdt, gid)
        name = "-".join(f"{c:x}" for c in cps)
        src = tmp / f"{name}.png"
        src.write_bytes(png)
        dest = out / f"{name}.png"
        subprocess.check_call(
            [
                "magick",
                str(src),
                "-background",
                "none",
                "-gravity",
                "center",
                "-extent",
                "136x136",
                "-filter",
                "Lanczos",
                "-resize",
                "64x64",
                str(dest),
            ]
        )
        print(dest)
    return 0


if __name__ == "__main__":
    sys.exit(main())
