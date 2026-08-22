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

/* Palettes: System (live Omarchy color0–7) + GitHub-Traffic-Board six. */
var CHAT_THEME_IDS = [
    "system",
    "gruvbox", "rose-pine", "everforest",
    "gruvbox-light", "catppuccin-latte", "tokyo-night-light"
]

var CHAT_THEMES = {
    "system": {
        name: "System",
        bg: "", fg: "", accent: "", unread: "",
        colors: []
    },
    "gruvbox": {
        name: "gruvbox",
        bg: "#0c0d0f", fg: "#ebdbb2", accent: "#fe8019", unread: "#fb4934",
        colors: ["#0c0d0f", "#fb4934", "#b8bb26", "#fabd2f", "#83a598", "#d3869b", "#8ec07c", "#ebdbb2"]
    },
    "rose-pine": {
        name: "rose pine",
        bg: "#191724", fg: "#e0def4", accent: "#c4a7e7", unread: "#eb6f92",
        colors: ["#191724", "#eb6f92", "#9ccfd8", "#f6c177", "#31748f", "#c4a7e7", "#ebbcba", "#e0def4"]
    },
    "everforest": {
        name: "everforest",
        bg: "#1e2326", fg: "#d3c6aa", accent: "#a7c080", unread: "#e67e80",
        colors: ["#1e2326", "#e67e80", "#a7c080", "#dbbc7f", "#7fbbb3", "#d699b6", "#83c092", "#d3c6aa"]
    },
    "gruvbox-light": {
        name: "gruvbox light",
        bg: "#f9f5d7", fg: "#3c3836", accent: "#af3a03", unread: "#9d0006",
        colors: ["#f9f5d7", "#9d0006", "#79740e", "#b57614", "#076678", "#8f3f71", "#427b58", "#3c3836"]
    },
    "catppuccin-latte": {
        name: "catppuccin latte",
        bg: "#eff1f5", fg: "#4c4f69", accent: "#8839ef", unread: "#d20f39",
        colors: ["#eff1f5", "#d20f39", "#40a02b", "#df8e1d", "#1e66f5", "#8839ef", "#179299", "#4c4f69"]
    },
    "tokyo-night-light": {
        name: "tokyo night light",
        bg: "#d5d6db", fg: "#343b59", accent: "#34548a", unread: "#8c4351",
        colors: ["#d5d6db", "#8c4351", "#485e30", "#965027", "#34548a", "#5a4a78", "#166775", "#343b59"]
    }
}

function themeFor(name) {
    if (name && CHAT_THEMES[name])
        return CHAT_THEMES[name]
    return CHAT_THEMES.system
}

function themeName(id) {
    var t = themeFor(id)
    return t.name || id
}

function themeColors(id) {
    var t = themeFor(id)
    return t.colors || []
}

function decodeEntities(s) {
    if (typeof s !== "string")
        return ""
    return s
        .replace(/&amp;/g, "&")
        .replace(/&lt;/g, "<")
        .replace(/&gt;/g, ">")
        .replace(/&quot;/g, "\"")
        .replace(/&#39;/g, "'")
        .replace(/&apos;/g, "'")
}

function parseOmarchyNews(html) {
    if (typeof html !== "string" || html.indexOf("news-card") < 0)
        return []
    var out = []
    var re = /<article class="news-card">[\s\S]*?datetime="([^"]*)"[\s\S]*?<h2 class="news-card__title">([\s\S]*?)<\/h2>[\s\S]*?class="news-card__link" href="(\/news\/[^"]+)"/g
    var m
    while ((m = re.exec(html)) !== null && out.length < 1) {
        var title = decodeEntities(String(m[2]).replace(/<[^>]+>/g, "").replace(/\s+/g, " ").trim())
        var path = m[3]
        if (!title || !path)
            continue
        out.push({
            date: m[1] || "",
            title: title,
            href: "https://omarchy.org" + path
        })
    }
    return out
}
