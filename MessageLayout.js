.pragma library

function codePointLength(codePoint) {
  return codePoint > 0xffff ? 2 : 1
}

function isRegionalIndicator(codePoint) {
  return codePoint >= 0x1f1e6 && codePoint <= 0x1f1ff
}

function isGraphemeExtension(codePoint) {
  return (codePoint >= 0x0300 && codePoint <= 0x036f) ||
    (codePoint >= 0x1ab0 && codePoint <= 0x1aff) ||
    (codePoint >= 0x1dc0 && codePoint <= 0x1dff) ||
    (codePoint >= 0x20d0 && codePoint <= 0x20ff) ||
    (codePoint >= 0xfe00 && codePoint <= 0xfe0f) ||
    (codePoint >= 0xfe20 && codePoint <= 0xfe2f) ||
    (codePoint >= 0x1f3fb && codePoint <= 0x1f3ff) ||
    (codePoint >= 0xe0100 && codePoint <= 0xe01ef) ||
    (codePoint >= 0xe0020 && codePoint <= 0xe007f)
}

function nextGraphemeEnd(source, start) {
  var first = source.codePointAt(start)
  var offset = start + codePointLength(first)
  if (isRegionalIndicator(first) && offset < source.length) {
    var regional = source.codePointAt(offset)
    if (isRegionalIndicator(regional))
      return offset + codePointLength(regional)
  }
  while (offset < source.length) {
    var codePoint = source.codePointAt(offset)
    if (isGraphemeExtension(codePoint)) {
      offset += codePointLength(codePoint)
      continue
    }
    if (codePoint === 0x200d) {
      var joined = offset + 1
      if (joined >= source.length)
        return joined
      codePoint = source.codePointAt(joined)
      offset = joined + codePointLength(codePoint)
      continue
    }
    break
  }
  return offset
}

function compactReplyPreview(value, maximumGraphemes) {
  var source = String(value || "").replace(/\s+/g, " ").trim()
  var limit = Math.max(1, Math.floor(Number(maximumGraphemes || 0)))
  var offset = 0
  var count = 0
  while (offset < source.length && count < limit) {
    offset = nextGraphemeEnd(source, offset)
    count++
  }
  return offset < source.length ? source.slice(0, offset) + "…" : source
}

function replySizingText(value, replyPreview) {
  var message = String(value || "")
  var preview = String(replyPreview || "")
  return preview ? "↩ " + preview + "\n" + message : message
}
