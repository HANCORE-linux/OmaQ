#!/usr/bin/env python3
"""Check icon coverage; optionally compare every mapping with native HarfBuzz."""
import ctypes as c
import ctypes.util
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
FONT = Path('/usr/share/fonts/TTF/MaterialSymbolsRounded[FILL,GRAD,opsz,wght].ttf')
ALIASES = {'circle_outline': 'circle', 'remove_reaction': 'remove'}


def parse_mapping(source):
    match = re.search(r'readonly property var glyphs: Object\.freeze\(\{(.*?)\}\)', source, re.S)
    assert match, 'missing immutable glyph map'
    result = {}
    for line in match[1].strip().splitlines():
        entry = re.fullmatch(r'\s*"([a-z][a-z0-9_]*)": "\\u([0-9a-f]{4})",?\s*', line)
        assert entry, 'nonliteral icon mapping'
        name, number = entry.groups()
        assert name not in result, 'duplicate icon name'
        value = int(number, 16)
        assert 0xe000 <= value <= 0xf8ff, 'icon outside the private-use range'
        result[name] = chr(value)
    assert 0 < len(result) <= 128, 'unexpected map size'
    return result


def source_checks():
    source = (ROOT / 'MaterialSymbols.qml').read_text()
    mapping = parse_mapping(source)
    assert 'symbolFont.status === FontLoader.Ready' in source
    assert 'symbolFont.name === "Material Symbols Rounded"' in source
    assert 'readonly property string family: ready ? symbolFont.name : "sans-serif"' in source
    assert 'return ready && Object.prototype.hasOwnProperty.call(glyphs, key)' in source
    assert '? glyphs[key] : "?"' in source
    assert 'file:///usr/share/fonts/TTF/MaterialSymbolsRounded%5BFILL,GRAD,opsz,wght%5D.ttf' in source
    assert 'singleton MaterialSymbols 1.0 MaterialSymbols.qml' in (ROOT / 'qmldir').read_text()
    names = set()
    for path in ('Panel.qml', 'ChatSurface.qml', 'pages/ChatPage.qml'):
        text = (ROOT / path).read_text()
        # Existing semantic icon properties remain names, not display text.
        expressions = [m.group(1) for m in re.finditer(
            r'(?m)^\s*(?:materialIcon|iconText):\s*(.*(?:\n[ \t]+(?:\?|:|"|\().*)*)', text)]
        expressions += re.findall(r'OmaQ\.MaterialSymbols\.glyph\((.*?)\)', text, re.S)
        for expression in expressions:
            expression = re.sub(r'===?\s*"[^"]*"', '', expression)
            names.update(re.findall(r'"([a-z][a-z_0-9]*)"', expression))
        assignments = re.sub(r'===\s*"Material Symbols Rounded"', '', text)
        assert not re.search(r'\b(?:font\.family|fontFamily):[^\n]*"Material Symbols Rounded"', assignments), path
    assert names <= mapping.keys(), f'unmapped icon names: {sorted(names - mapping.keys())}'
    for path, expected in (('ChatSurface.qml', ['call', 'call', 'call_end', 'call_end']),
                           ('Panel.qml', ['call'])):
        literals = re.findall(r'OmaQ\.MaterialSymbols\.ready\s*\?\s*"\\u([0-9a-f]{4})"\s*:\s*"\?"',
                              (ROOT / path).read_text())
        assert [chr(int(value, 16)) for value in literals] == [mapping[name] for name in expected]
    panel = (ROOT / 'Panel.qml').read_text()
    for needle in ('OmaQ.MaterialSymbols.glyph(tokenButton.iconText)',
                   'OmaQ.MaterialSymbols.glyph(railIcon.materialIcon)'):
        assert needle in panel
    assert (ROOT / 'SafeText.qml').read_text() == 'import QtQuick\n\nText {\n  textFormat: Text.PlainText\n}\n'
    for body in ('"x": "\\u0041"', '"x": "\\ue001",\n"x": "\\ue002"', '"x": service.value'):
        try:
            parse_mapping('readonly property var glyphs: Object.freeze({' + body + '})')
        except AssertionError:
            pass
        else:
            raise AssertionError('malformed map accepted')
    return mapping


def native_checks(mapping):
    assert FONT.is_file() and 0 < FONT.stat().st_size <= 32 * 1024 * 1024
    library = ctypes.util.find_library('harfbuzz')
    assert library, 'HarfBuzz unavailable'
    hb = c.CDLL(library)

    class Info(c.Structure):
        _fields_ = [(name, c.c_uint32) for name in ('codepoint', 'mask', 'cluster', 'var1', 'var2')]

    class Feature(c.Structure):
        _fields_ = [('tag', c.c_uint32), ('value', c.c_uint32), ('start', c.c_uint32), ('end', c.c_uint32)]

    class Variation(c.Structure):
        _fields_ = [('tag', c.c_uint32), ('value', c.c_float)]

    def api(name, result, *args):
        func = getattr(hb, name)
        func.restype, func.argtypes = result, args
        return func

    blob_create = api('hb_blob_create_from_file_or_fail', c.c_void_p, c.c_char_p)
    face_create = api('hb_face_create', c.c_void_p, c.c_void_p, c.c_uint)
    font_create = api('hb_font_create', c.c_void_p, c.c_void_p)
    font_funcs = api('hb_ot_font_set_funcs', None, c.c_void_p)
    variations = api('hb_font_set_variations', None, c.c_void_p, c.POINTER(Variation), c.c_uint)
    buffer_create = api('hb_buffer_create', c.c_void_p)
    buffer_add = api('hb_buffer_add_utf8', None, c.c_void_p, c.c_char_p, c.c_int, c.c_uint, c.c_int)
    guess = api('hb_buffer_guess_segment_properties', None, c.c_void_p)
    shape = api('hb_shape', None, c.c_void_p, c.c_void_p, c.POINTER(Feature), c.c_uint)
    infos = api('hb_buffer_get_glyph_infos', c.POINTER(Info), c.c_void_p, c.POINTER(c.c_uint))
    destroy = {kind: api('hb_' + kind + '_destroy', None, c.c_void_p)
               for kind in ('blob', 'face', 'font', 'buffer')}
    tag = lambda name: int.from_bytes(name.encode('ascii'), 'big')
    blob = blob_create(bytes(FONT))
    assert blob
    face = font = None
    try:
        face = face_create(blob, 0)
        font = font_create(face)
        assert face and font
        font_funcs(font)

        def shaped(text, liga):
            data = text.encode('utf-8')
            buf = buffer_create()
            assert buf
            try:
                buffer_add(buf, data, len(data), 0, len(data))
                guess(buf)
                tags = ('liga', 'clig', 'rlig', 'calt', 'rclt')
                features = (Feature * len(tags))(*(Feature(tag(key), liga, 0, 0xffffffff) for key in tags))
                shape(font, buf, None if liga else features, 0 if liga else len(tags))
                count = c.c_uint()
                result = infos(buf, c.byref(count))
                return [result[i].codepoint for i in range(count.value)]
            finally:
                destroy['buffer'](buf)

        for fill in (0, 1):
            for weight in (400, 500, 600):
                for size in (20, 24):
                    axes = (Variation * 3)(Variation(tag('FILL'), fill),
                                           Variation(tag('wght'), weight), Variation(tag('opsz'), size))
                    variations(font, axes, 3)
                    for name, glyph in mapping.items():
                        expected = shaped(ALIASES.get(name, name), 1)
                        actual = shaped(glyph, 1)
                        assert len(expected) == 1 and expected[0] != 0 and actual == expected, (name, fill, weight, size)
                        unshaped = shaped(glyph, 0)
                        assert len(unshaped) == 1 and unshaped[0] != 0, (name, 'disabled shaping')
        assert len(shaped('person', 0)) > 1, 'disabled-ligature negative control did not fail'
        assert len(shaped(mapping['person'], 0)) == 1
    finally:
        if font:
            destroy['font'](font)
        if face:
            destroy['face'](face)
        destroy['blob'](blob)


if __name__ == '__main__':
    assert sys.argv[1:] in ([], ['--font']), 'unsupported arguments'
    values = source_checks()
    if sys.argv[1:]:
        native_checks(values)
    print('material-symbols: ' + ('source and native glyph equivalence' if sys.argv[1:] else 'source') + ': ok')
