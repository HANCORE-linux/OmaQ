import os
import subprocess


BAR_PROBE = "/usr/bin/omarchy-shell"


def qr_lines(text):
    binary = "/usr/bin/qrencode" if os.access("/usr/bin/qrencode", os.X_OK) else ""
    if not binary:
        return []
    try:
        result = subprocess.run([binary, "-t", "UTF8", "-m", "2", "-o", "-", "--", text], stdin=subprocess.DEVNULL,
                                capture_output=True, timeout=5)
    except (OSError, subprocess.TimeoutExpired):
        return []
    if result.returncode != 0:
        return []
    allowed = set(" \u2580\u2584\u2588")
    lines = result.stdout.decode("utf-8", "replace").splitlines()
    return lines if lines and all(set(line) <= allowed for line in lines) else []
