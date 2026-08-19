.pragma library

/* UX precheck only. The helper is authoritative for redeem. */

var TOX_ADDR_LEN = 76

function parseInvite(url) {
    if (typeof url !== "string")
        return null
    if (url.indexOf("omaq://invite/") !== 0)
        return null
    var rest = url.slice(14)
    if (rest.length < TOX_ADDR_LEN + 1)
        return null
    var addr = rest.slice(0, TOX_ADDR_LEN)
    if (!/^[0-9a-fA-F]+$/.test(addr) || addr.length !== TOX_ADDR_LEN)
        return null
    if (rest.charAt(TOX_ADDR_LEN) !== "?")
        return null
    var q = rest.slice(TOX_ADDR_LEN + 1)
    var parts = q.split("&")
    var seen = {}
    var out = { id: "", expiry: "", kind: "", group: "", role: "", rk: "" }
    for (var i = 0; i < parts.length; i++) {
        var eq = parts[i].indexOf("=")
        if (eq < 1)
            return null
        var k = parts[i].slice(0, eq)
        var v = parts[i].slice(eq + 1)
        if (seen[k])
            return null
        seen[k] = true
        if (k === "i")
            out.id = v
        else if (k === "e")
            out.expiry = v
        else if (k === "k")
            out.kind = v
        else if (k === "g")
            out.group = v
        else if (k === "r")
            out.role = v
        else if (k === "rk")
            out.rk = v
        else
            return null
    }
    if (!out.id || !out.expiry || !out.kind)
        return null
    if (out.kind !== "direct" && out.kind !== "group")
        return null
    if (out.kind === "direct" && (out.group || out.role))
        return null
    if (out.rk && (out.rk.length !== 64 || !/^[0-9a-fA-F]+$/.test(out.rk)))
        return null
    if (out.kind === "group" && (out.rk || !out.group))
        return null
    return out
}
