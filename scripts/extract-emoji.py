#!/usr/bin/env python3
"""Extract pinned Noto Color Emoji CBDT PNGs and resize them to 64px."""
import hashlib
import os
import resource
import signal
import stat
import subprocess
import sys
import tempfile
from pathlib import Path

FONT = Path("/usr/share/fonts/noto/NotoColorEmoji.ttf")
FONT_VERSION = "2.051"
FONT_COMMIT = "8998f5dd683424a73e2314a8c1f1e359c19e8742"
FONT_SHA256 = "72a635cb3d2f3524c51620cdde406b217204e8a6a06c6a096ff8ed4b5fd6e27b"
FONT_SIZE = 10_673_480
MAX_EMOJI_OUTPUT = 1_048_576
MAGICK_TIMEOUT = 30.0
FONT_SOURCE = (
    "https://github.com/googlefonts/noto-emoji/blob/"
    f"{FONT_COMMIT}/fonts/NotoColorEmoji.ttf"
)
GLYPHS = [
    "😀", "🙂", "😉", "😍", "😂", "😅", "🙌", "👍",
    "👎", "❤️", "🔥", "✨", "🎉", "🙏", "😮", "😢",
    "😡", "🤔", "👀", "✅", "👋", "💯",
]
SOURCE_SHA256 = {
    "1f389.png": "ccdba3c569ee5cee47ebc384cf14cae46d7a9b4d505dee5a63737e3d5757f393",
    "1f440.png": "b95e5e9d2ef86b6bfb9c95c50cf840ba7c49b43f03d6953af7161d64738a70ef",
    "1f44b.png": "72d87529dfcd68a79c237620009b3754e0c8b48788cdf98799a09036e63d9f09",
    "1f44d.png": "7b08f9d9faaa5aba12d436392aafdef323745056e00cc9be411b3f1733801cf5",
    "1f44e.png": "94b4f7a1a3ff160eae0dee9f82ff184f635cc28f6ff86ac1445f6f5670a3c252",
    "1f4af.png": "1225ab6d9cf921f2ed5e645884a9990e2484d1801b4e0bf43d9a4355d8adda1a",
    "1f525.png": "e86e8f9e81c724b821f217c32a0a54eee077bbae04d61e7c17c360053628f71d",
    "1f600.png": "10d6305ce9241ddf4a53ee4f130999922b1da72115b44809231d660da57c18b2",
    "1f602.png": "b96bc25e1b703b4d02824ebea92cc3aee17bd37b048629dfcf52c373b81ddc00",
    "1f605.png": "c2e82e8c259242be618aa8912ae84b6dba1b82aeaadecaaf203a0053a3517dc8",
    "1f609.png": "3dca1f7f42014a66529c6c6c507d8d8fc6c0882e8ef58f0ff49badbf8a229c2d",
    "1f60d.png": "f5520fbddae21d9b3126f54f438fac420b5b698167c9ca33bd74f3ec23fff424",
    "1f621.png": "253e3ab5fd2a6dba8176eac069a724203a0af10944426a056924357d1345454e",
    "1f622.png": "9391b8218ffb5d903c771398c59bef27751acf1b49c7247c63d4252b842ba4cc",
    "1f62e.png": "193e2427f3159e28d15c5e2bf347e2d016c9ce90b47e458472fbe1d0c8d6302e",
    "1f642.png": "9c296a35ac6cbe028a460b7683e4726bb96cd0a20b771df643e10f03ab263fd7",
    "1f64c.png": "da9355a11041138aab6f33359543b8ae83092780b8ca0439a52245c58bcf0455",
    "1f64f.png": "0d0e64df9289619a592cc6346c184d93d1ef3023c38d772d86eb059367ecce49",
    "1f914.png": "2ff20ef612fa6031d56564fb0e7cfb50cd90722145a98b8944661625e9ede569",
    "2705.png": "8e3b1175d22f98ac3732eb67282f3f99951061de833be7c6e1bab05977158afc",
    "2728.png": "d50b1f3aad1a9e72a26cfabfcd179f9a4bc29f4379101ff27f20247dacf858dd",
    "2764.png": "200134e6ecf39657dca13bc32b1be433305e98d2d460c68b413707f89a7bdc1f",
}
OUTPUT_SHA256 = {
    "1f389.png": "fd1d97d9d108c6c471416f2e3da6b851ef5818b504ca5140b9f20758d0e9a48a",
    "1f440.png": "6d8faa5624b97bb261160a43c92b14631a81809cd70947c269c982c999c2125b",
    "1f44b.png": "7cf812a16517074d30479e093364305b59fb2a059e059d74f07a594aa7647ac9",
    "1f44d.png": "bae2fb9c29aee988b379b798c8663ede84fc849155536538f4c264faf8052575",
    "1f44e.png": "fe557ff3970c425d6c5fe6b0406a7906dac9be91ea2d94f30b3f0f6fa9e4c94a",
    "1f4af.png": "07e2c3c67027473977b533a46a4a7330d9e9556a984e901fbf3499f78c2d8ea8",
    "1f525.png": "39895867d7c8bf3d1cd2ee360dac7ef65684283e438c840df97796afd37ad94c",
    "1f600.png": "0dcc80477d4edd14f7c0b7a541604cf54895244e4035f5726280ff7196794ca3",
    "1f602.png": "f822343d9115fa8c0ec20f6b187461cd767086af770e60b964b1012b0b21849d",
    "1f605.png": "00f8eec69c4863360ed2f8a7780438811bb7db7ad79167bafbd2aa13aff1eb6d",
    "1f609.png": "746ccdf91f2e1f94f1f75c8e1a247867b3cf6c1ed835c18ef5238af3e2cba2cb",
    "1f60d.png": "1fb44611b55088526867ae5892969ad681b7da0f036b184ac2d776b5b9f24a5f",
    "1f621.png": "4f42446ba766351c32ae85ea2acc64523bfd90b2c2d9e946a14acbb1c23263a4",
    "1f622.png": "0423a85ef1dab4920187fc52dd77889c8d65da43d7e5ac75c47a476f495331fb",
    "1f62e.png": "8d82a8674a4219b4ac3db452cede3f8bc02710e05fc287d44fdb7caf64b6b69c",
    "1f642.png": "9a3c8cb6f598068e326fa0bc31d9cd2b954eecdc6bc45869d7df357aca6bc751",
    "1f64c.png": "1d5ff643951a9feef5289d48922cbac8228e777717cd9dcc28ccc0ad64d4c91d",
    "1f64f.png": "36a487bb6ea6d272b87c2347706016504e74a270b44f80d5dfd6f2a29c046b83",
    "1f914.png": "c02843630ff2f09bc9300fa347fe6bceaf4cafe03cc31ad860aaf37d1e3ae91d",
    "2705.png": "a51e760d324a3a289e54be7e816f14af5bfb88df0643f517b30b77aaa235cdc3",
    "2728.png": "646dbb274b6b3724b88eec494c06412c9cadce643b496367277f813124efe097",
    "2764.png": "fc2a26e20b4449789bac9404e9e808937034f27866a4b403203d71f214b876a2",
}


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


def load_font(font):
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


def read_regular(path, *, max_size, exact_size=None, expected_owner=None):
    flags = os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK
    try:
        descriptor = os.open(path, flags)
    except OSError as error:
        raise SystemExit(f"cannot safely open {path}: {error}") from error
    try:
        before = os.fstat(descriptor)
        if not stat.S_ISREG(before.st_mode) or before.st_nlink != 1:
            raise SystemExit(f"unsafe file type or link count for {path}")
        if expected_owner is not None and before.st_uid != expected_owner:
            raise SystemExit(f"unexpected owner for {path}")
        if exact_size is not None and before.st_size != exact_size:
            raise SystemExit(
                f"unexpected size for {path}: expected {exact_size}, got {before.st_size}"
            )
        if before.st_size < 0 or before.st_size > max_size:
            raise SystemExit(f"file exceeds the {max_size}-byte limit: {path}")
        chunks = []
        remaining = before.st_size
        while remaining:
            chunk = os.read(descriptor, min(65_536, remaining))
            if not chunk:
                raise SystemExit(f"file changed while reading: {path}")
            chunks.append(chunk)
            remaining -= len(chunk)
        if os.read(descriptor, 1):
            raise SystemExit(f"file grew while reading: {path}")
        after = os.fstat(descriptor)
        identity = lambda info: (
            info.st_dev,
            info.st_ino,
            info.st_mode,
            info.st_nlink,
            info.st_uid,
            info.st_gid,
            info.st_size,
            info.st_mtime_ns,
            info.st_ctime_ns,
        )
        if identity(before) != identity(after):
            raise SystemExit(f"file changed while reading: {path}")
        return b"".join(chunks)
    finally:
        os.close(descriptor)


def run_magick(command):
    def limit_output_size():
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        resource.setrlimit(
            resource.RLIMIT_FSIZE,
            (MAX_EMOJI_OUTPUT, MAX_EMOJI_OUTPUT),
        )

    try:
        process = subprocess.Popen(
            command,
            stdin=subprocess.DEVNULL,
            start_new_session=True,
            preexec_fn=limit_output_size,
        )
    except OSError as error:
        raise SystemExit(f"cannot start ImageMagick: {error}") from error
    try:
        returncode = process.wait(timeout=MAGICK_TIMEOUT)
    except subprocess.TimeoutExpired as error:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()
        raise SystemExit(
            f"ImageMagick exceeded the {MAGICK_TIMEOUT:g}-second limit"
        ) from error
    except BaseException:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()
        raise
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    if returncode != 0:
        raise SystemExit(f"ImageMagick failed with status {returncode}")


def open_owned_directory(name, *, parent=None):
    flags = os.O_RDONLY | os.O_CLOEXEC | os.O_DIRECTORY | os.O_NOFOLLOW
    try:
        descriptor = os.open(name, flags, dir_fd=parent)
    except OSError as error:
        raise SystemExit(f"cannot safely open directory {name}: {error}") from error
    info = os.fstat(descriptor)
    if (
        not stat.S_ISDIR(info.st_mode)
        or info.st_uid != os.geteuid()
        or info.st_mode & 0o022
    ):
        os.close(descriptor)
        raise SystemExit(f"unsafe directory: {name}")
    return descriptor


def open_output_directory(root):
    root_directory = open_owned_directory(root)
    try:
        assets_directory = open_owned_directory("assets", parent=root_directory)
    finally:
        os.close(root_directory)
    try:
        try:
            os.mkdir("emoji", mode=0o755, dir_fd=assets_directory)
        except FileExistsError:
            pass
        return open_owned_directory("emoji", parent=assets_directory)
    finally:
        os.close(assets_directory)


def validate_destinations(directory, names):
    for name in names:
        try:
            info = os.stat(name, dir_fd=directory, follow_symlinks=False)
        except FileNotFoundError:
            continue
        if (
            not stat.S_ISREG(info.st_mode)
            or info.st_nlink != 1
            or info.st_uid != os.geteuid()
        ):
            raise SystemExit(f"unsafe existing emoji destination: {name}")


def publish_outputs(directory, out, generated):
    staged = []
    try:
        for index, (data, name) in enumerate(generated):
            temporary = f".{name}.tmp.{os.getpid()}.{index}"
            flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW
            created = False
            try:
                descriptor = os.open(temporary, flags, 0o600, dir_fd=directory)
                created = True
                try:
                    view = memoryview(data)
                    while view:
                        written = os.write(descriptor, view)
                        if written <= 0:
                            raise OSError("short write")
                        view = view[written:]
                    os.fchmod(descriptor, 0o644)
                    os.fsync(descriptor)
                finally:
                    os.close(descriptor)
            except OSError as error:
                if created:
                    try:
                        os.unlink(temporary, dir_fd=directory)
                    except FileNotFoundError:
                        pass
                raise SystemExit(f"cannot stage emoji output {name}: {error}") from error
            staged.append((temporary, name))
        while staged:
            temporary, name = staged[0]
            try:
                os.replace(
                    temporary,
                    name,
                    src_dir_fd=directory,
                    dst_dir_fd=directory,
                )
            except OSError as error:
                raise SystemExit(f"cannot publish emoji output {name}: {error}") from error
            staged.pop(0)
            print(out / name)
        os.fsync(directory)
    finally:
        for temporary, _name in staged:
            try:
                os.unlink(temporary, dir_fd=directory)
            except FileNotFoundError:
                pass


def generate(root, font_path=FONT):
    out = root / "assets" / "emoji"
    font = read_regular(font_path, max_size=FONT_SIZE, exact_size=FONT_SIZE)
    font_sha256 = hashlib.sha256(font).hexdigest()
    if font_sha256 != FONT_SHA256:
        raise SystemExit(
            f"unsupported Noto Color Emoji font; expected {FONT_VERSION} "
            f"({FONT_SHA256}), got {font_sha256}. Source: {FONT_SOURCE}"
        )
    cp_to_gid, gid_loc, cbdt = load_font(font)
    expected_names = {
        "-".join(f"{codepoint:x}" for codepoint in cps_of(emoji)) + ".png"
        for emoji in GLYPHS
    }
    if expected_names != set(SOURCE_SHA256) or expected_names != set(OUTPUT_SHA256):
        raise SystemExit("emoji provenance tables do not match the glyph list")
    output_directory = open_output_directory(root)
    try:
        validate_destinations(output_directory, expected_names)
        with tempfile.TemporaryDirectory(prefix="omaq-emoji-native.") as temporary:
            tmp = Path(temporary)
            generated = []
            for emoji in GLYPHS:
                cps = cps_of(emoji)
                gid = cp_to_gid.get(cps[0]) if len(cps) == 1 else None
                if gid is None:
                    raise SystemExit(f"no glyph for {emoji!r}")
                png = glyph_png(gid_loc, cbdt, gid)
                name = "-".join(f"{codepoint:x}" for codepoint in cps) + ".png"
                if png is None:
                    raise SystemExit(f"no embedded PNG for {emoji!r}")
                source_sha256 = hashlib.sha256(png).hexdigest()
                if source_sha256 != SOURCE_SHA256[name]:
                    raise SystemExit(f"embedded source PNG changed for {name}")
                src = tmp / f"source-{name}"
                candidate = tmp / f"resized-{name}"
                src.write_bytes(png)
                run_magick(
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
                        "-strip",
                        str(candidate),
                    ]
                )
                candidate_bytes = read_regular(
                    candidate,
                    max_size=MAX_EMOJI_OUTPUT,
                    expected_owner=os.geteuid(),
                )
                candidate_sha256 = hashlib.sha256(candidate_bytes).hexdigest()
                if candidate_sha256 != OUTPUT_SHA256[name]:
                    raise SystemExit(
                        f"ImageMagick output changed for {name}; use the pinned "
                        "generation version or review and update the provenance ledger"
                    )
                generated.append((candidate_bytes, name))
            publish_outputs(output_directory, out, generated)
    finally:
        os.close(output_directory)


def main():
    generate(Path(__file__).resolve().parents[1])
    return 0


if __name__ == "__main__":
    sys.exit(main())
