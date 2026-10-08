"""Deterministic import metadata; stdlib only so it also runs inside editor Python.

The small PNG decoder reads the original indexed ASCII sheet's alpha for bitmap
provider advance widths. It never writes or transforms an original image.
"""
import hashlib
import json
import math
import struct
import zlib
from pathlib import Path


def read_ascii_alpha(path):
    data = path.read_bytes()
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    offset, packed, alpha = 8, b'', b''
    while offset < len(data):
        length = struct.unpack('>I', data[offset:offset + 4])[0]
        kind, chunk = data[offset + 4:offset + 8], data[offset + 8:offset + 8 + length]
        if kind == b'IHDR':
            width, height, depth, color, _, _, interlace = struct.unpack('>IIBBBBB', chunk)
        elif kind == b'IDAT':
            packed += chunk
        elif kind == b'tRNS':
            alpha = chunk
        offset += length + 12
    assert color == 3 and depth in (1, 2, 4, 8) and interlace == 0, 'Unexpected pinned ASCII PNG format'
    stride = (width * depth + 7) // 8
    raw, rows, previous = zlib.decompress(packed), [], bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        filter_type = raw[start]
        row = bytearray(raw[start + 1:start + 1 + stride])
        for x in range(stride):
            left, up, corner = row[x - 1] if x else 0, previous[x], previous[x - 1] if x else 0
            if filter_type == 1:
                prediction = left
            elif filter_type == 2:
                prediction = up
            elif filter_type == 3:
                prediction = (left + up) // 2
            elif filter_type == 4:
                base = left + up - corner
                distances = [abs(base - v) for v in (left, up, corner)]
                prediction = (left, up, corner)[distances.index(min(distances))]
            else:
                assert filter_type == 0
                prediction = 0
            row[x] = (row[x] + prediction) & 255
        indices = [(row[(x * depth) // 8] >> (8 - depth - (x * depth) % 8)) & ((1 << depth) - 1) for x in range(width)]
        rows.append([alpha[i] if i < len(alpha) else 255 for i in indices])
        previous = row
    return width, height, rows


def build(reference, texture_path):
    manifest = json.loads((reference / 'manifest.json').read_text(encoding='utf-8'))
    records = []
    for entry in manifest['files']:
        if entry['type'] != 'png':
            continue
        source = reference / entry['path']
        assert hashlib.sha256(source.read_bytes()).hexdigest() == entry['sha256'], str(source)
        meta_path = Path(str(source) + '.mcmeta')
        meta = json.loads(meta_path.read_text()) if meta_path.exists() else {}
        size = [entry['width'], entry['height']]
        frames, times, frame_size = [0], [1], size
        if 'animation' in meta:
            anim = meta['animation']
            default = min(size)
            frame_size = [anim.get('width', size[0] if 'height' in anim else default),
                          anim.get('height', size[1] if 'width' in anim else default)]
            seq = anim.get('frames', list(range((size[0] // frame_size[0]) * (size[1] // frame_size[1]))))
            frames = [v if isinstance(v, int) else v['index'] for v in seq]
            times = [anim.get('frametime', 1) if isinstance(v, int) else v.get('time', anim.get('frametime', 1)) for v in seq]
        relative = entry['path'].split('assets/minecraft/textures/', 1)[1][:-4]
        preset = ('blurred_overlay' if meta.get('texture', {}).get('blur') else
                  'tiled_animation' if relative.startswith('block/') else
                  'camera_overlay' if entry['group'] == 'Camera / item overlays' else 'pixel_ui')
        records.append(dict(id=relative, source_path=entry['path'], source_sha256=entry['sha256'],
                            asset=texture_path(relative), native_size=size, frame_size=frame_size,
                            frames=frames, frame_ticks=times, metadata=meta,
                            role=entry['group'] + ': ' + entry['purpose'], import_preset=preset))
    assert len(records) == 194 and len({r['asset'] for r in records}) == 194
    provider = next(v for v in json.loads((reference / 'assets/minecraft/font/include/default.json').read_text())['providers']
                    if v.get('file') == 'minecraft:font/ascii.png')
    width, height, alpha = read_ascii_alpha(reference / 'assets/minecraft/textures/font/ascii.png')
    cell_w, cell_h = width // len(provider['chars'][0]), height // len(provider['chars'])
    font_height = provider.get('height', 8)
    glyphs = []
    for row, chars in enumerate(provider['chars']):
        for col, char in enumerate(chars):
            if not 32 <= ord(char) <= 126:
                continue
            x, y = col * cell_w, row * cell_h
            ink = max((dx + 1 for dx in range(cell_w) for dy in range(cell_h) if alpha[y + dy][x + dx]), default=0)
            advance = math.floor(ink * font_height / cell_h + .5) + 1
            if char == ' ':  # The earlier space provider overrides the empty bitmap cell.
                space = json.loads((reference / 'assets/minecraft/font/include/space.json').read_text())
                advance = int(next(v['advances'][' '] for v in space['providers'] if ' ' in v.get('advances', {})))
            glyphs.append(dict(code=ord(char), origin=[x, y], ink_width=ink, advance=advance))
    assert len(glyphs) == 95
    return dict(schema=1, client_sha1='e877b6a07acd633fb3bb475002175cec036e7b87', sprites=records,
                glyphs=glyphs, font_height=font_height, font_ascent=provider['ascent'])
