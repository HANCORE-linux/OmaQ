import fcntl
import os
import re
import select
import struct
import termios
import time

from .text import WIDTHS


def pixel_size(fd):
    try:
        return struct.unpack("HHHH", fcntl.ioctl(fd, termios.TIOCGWINSZ, b"\0" * 8))
    except OSError:
        return None


def measure_widths(fd, out, timeout=0.3):
    out.write("\r\x1b[2K\u2764\ufe0f\x1b[6n\r\x1b[2K\U0001F469\u200d\U0001F4BB\x1b[6n\r\x1b[2K")
    out.flush()
    buf, cols, end = b"", [], time.monotonic() + timeout
    while len(cols) < 2:
        left = end - time.monotonic()
        if left <= 0:
            break
        ready, _, _ = select.select([fd], [], [], left)
        if not ready:
            break
        chunk = os.read(fd, 1024)
        if not chunk:
            break
        buf += chunk
        cols = [int(m.group(1)) for m in re.finditer(rb"\x1b\[\d+;(\d+)R", buf)]
    if len(cols) >= 2:
        WIDTHS["vs16"] = 1 if cols[0] - 1 < 2 else 2
        WIDTHS["cluster"] = cols[1] - 1 == 2
    return re.sub(rb"\x1b\[\d+;\d+R", b"", buf)
