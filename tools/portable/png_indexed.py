"""Read and write 8-bit indexed and 8-bit greyscale PNG images (stdlib only).

Track folders store tile indices, raster values and palette indices as exact
pixel values, so these helpers never convert colours: an indexed image keeps
its palette order and a greyscale image keeps its byte values.
"""
import struct
import zlib

SIGNATURE = b'\x89PNG\r\n\x1a\n'
GREY = 0
INDEXED = 3


class PngError(ValueError):
    pass


def _chunk(kind, data):
    return (struct.pack('>I', len(data)) + kind + data +
            struct.pack('>I', zlib.crc32(kind + data) & 0xFFFFFFFF))


def write(path, width, height, pixels, palette=None):
    """Write width*height pixel bytes; palette (list of (r, g, b)) selects indexed."""
    if not 0 < width <= 0x7FFFFFFF or not 0 < height <= 0x7FFFFFFF:
        raise PngError(f'{path}: invalid image size {width}x{height}')
    if len(pixels) != width * height:
        raise PngError(f'{path}: expected {width * height} pixels, got {len(pixels)}')
    if palette is not None:
        if not 1 <= len(palette) <= 256:
            raise PngError(f'{path}: palette must hold 1..256 colours')
        if max(pixels) >= len(palette):
            raise PngError(f'{path}: pixel index outside the {len(palette)}-colour palette')
    raw = bytearray()
    for row in range(height):
        raw.append(0)
        raw += pixels[row * width:(row + 1) * width]
    data = SIGNATURE + _chunk(b'IHDR', struct.pack(
        '>IIBBBBB', width, height, 8, GREY if palette is None else INDEXED, 0, 0, 0))
    if palette is not None:
        data += _chunk(b'PLTE', b''.join(bytes((r, g, b)) for r, g, b in palette))
    data += _chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + _chunk(b'IEND', b'')
    with open(path, 'wb') as handle:
        handle.write(data)


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def read(path):
    """Return (width, height, pixels, palette); palette is None for greyscale."""
    with open(path, 'rb') as handle:
        data = handle.read()
    if data[:8] != SIGNATURE:
        raise PngError(f'{path}: not a PNG file')
    offset, header, palette, compressed = 8, None, None, bytearray()
    while True:
        if offset + 8 > len(data):
            raise PngError(f'{path}: truncated chunk')
        length, kind = struct.unpack_from('>I4s', data, offset)
        body = data[offset + 8:offset + 8 + length]
        if len(body) != length or offset + 12 + length > len(data):
            raise PngError(f'{path}: truncated {kind!r} chunk')
        crc, = struct.unpack_from('>I', data, offset + 8 + length)
        if crc != zlib.crc32(kind + body) & 0xFFFFFFFF:
            raise PngError(f'{path}: CRC mismatch in {kind!r}')
        offset += 12 + length
        if kind == b'IHDR':
            header = struct.unpack('>IIBBBBB', body)
        elif kind == b'PLTE':
            if length % 3:
                raise PngError(f'{path}: malformed palette')
            palette = [tuple(body[i:i + 3]) for i in range(0, length, 3)]
        elif kind == b'IDAT':
            compressed += body
        elif kind == b'IEND':
            break
    if header is None:
        raise PngError(f'{path}: missing IHDR')
    width, height, depth, colour, _, _, interlace = header
    if depth != 8 or colour not in (GREY, INDEXED) or interlace:
        raise PngError(f'{path}: expected a non-interlaced 8-bit indexed or greyscale PNG '
                       f'(got depth {depth}, colour type {colour}, interlace {interlace})')
    if colour == INDEXED and palette is None:
        raise PngError(f'{path}: indexed PNG without a palette')
    try:
        raw = zlib.decompress(bytes(compressed))
    except zlib.error as error:
        raise PngError(f'{path}: corrupt image data ({error})') from None
    if len(raw) != height * (width + 1):
        raise PngError(f'{path}: image data size mismatch')
    pixels = bytearray(width * height)
    previous = bytearray(width)
    for row in range(height):
        start = row * (width + 1)
        kind, line = raw[start], bytearray(raw[start + 1:start + 1 + width])
        for x in range(width):
            left = line[x - 1] if x else 0
            up = previous[x]
            corner = previous[x - 1] if x else 0
            if kind == 1:
                line[x] = (line[x] + left) & 255
            elif kind == 2:
                line[x] = (line[x] + up) & 255
            elif kind == 3:
                line[x] = (line[x] + ((left + up) >> 1)) & 255
            elif kind == 4:
                line[x] = (line[x] + _paeth(left, up, corner)) & 255
            elif kind:
                raise PngError(f'{path}: unknown filter {kind}')
        pixels[row * width:(row + 1) * width] = line
        previous = line
    if palette is not None and colour == INDEXED and max(pixels, default=0) >= len(palette):
        raise PngError(f'{path}: pixel index outside the palette')
    return width, height, bytes(pixels), (palette if colour == INDEXED else None)


def bgr555_to_rgb(colour):
    """Expand a GBA colour so rgb_to_bgr555 recovers its low 15 bits exactly."""
    r, g, b = colour & 31, colour >> 5 & 31, colour >> 10 & 31
    return (r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2)


def rgb_to_bgr555(rgb):
    r, g, b = rgb
    return r >> 3 | (g >> 3) << 5 | (b >> 3) << 10
