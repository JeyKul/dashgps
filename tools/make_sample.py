#!/usr/bin/env python3
"""Create a synthetic dashcam clip with a fake GPS track for testing.

Generates a test-pattern video with ffmpeg and embeds Novatek "freeGPS"
chunks (Youqing layout, as written by RedTiger cameras) plus the moov/"gps "
index atom, exactly like the real camera does. No real footage involved.

usage: make_sample.py OUT.MP4 [seconds] [lat lon]
"""
import math
import struct
import subprocess
import sys
import tempfile


def deg_to_nmea(d):
    d = abs(d)
    deg = int(d)
    return deg * 100 + (d - deg) * 60


def chunk(t, lat, lon, kn, hdg, when):
    c = bytearray(0x400)
    struct.pack_into('>I', c, 0, len(c))
    c[4:12] = b'freeGPS '
    struct.pack_into('<I', c, 12, 0x90)
    c[16:26] = b'YOUQINGGPS'
    struct.pack_into('<ff', c, 0x28, deg_to_nmea(lat), deg_to_nmea(lon))
    y, mo, d, h, mi, s = when
    struct.pack_into('<6I', c, 0x30, h, mi, s, y - 2000, mo, d)
    c[0x48:0x4b] = b'A' + (b'N' if lat >= 0 else b'S') + (b'E' if lon >= 0 else b'W')
    # the real firmware stores obfuscated float64 copies here; we leave zeros
    struct.pack_into('<ff', c, 0x60, kn, hdg)
    c[0x68:0x78] = b'1234567890123456'
    c[0x78:0x7c] = b'YSKJ'
    struct.pack_into('<3i', c, 0x7c, int(10 * math.sin(t)), int(5 * math.cos(t)), 0)
    return bytes(c)


def main():
    out = sys.argv[1]
    secs = int(sys.argv[2]) if len(sys.argv) > 2 else 30
    lat0, lon0 = (float(sys.argv[3]), float(sys.argv[4])) if len(sys.argv) > 4 else (52.5145, 13.3501)

    with tempfile.NamedTemporaryFile(suffix='.mp4') as tmp:
        subprocess.run(['ffmpeg', '-v', 'error', '-y',
                        '-f', 'lavfi', '-i', f'testsrc2=size=1280x720:rate=25:duration={secs}',
                        '-f', 'lavfi', '-i', f'sine=frequency=440:duration={secs}',
                        '-c:v', 'libx264', '-preset', 'veryfast', '-pix_fmt', 'yuv420p',
                        '-c:a', 'aac', '-shortest', tmp.name], check=True)
        data = bytearray(open(tmp.name, 'rb').read())

    # find moov (ffmpeg writes it last without +faststart)
    off = 0
    moov = None
    while off < len(data):
        sz, typ = struct.unpack('>I4s', data[off:off + 8])
        if typ == b'moov':
            moov = off
        off += sz
    assert moov is not None and moov + struct.unpack('>I', data[moov:moov + 4])[0] == len(data)

    # a circular drive, ~50 km/h
    r = 300.0  # metres
    v = 14.0   # m/s
    chunks = []
    for i in range(secs):
        a = v * i / r
        lat = lat0 + (r * math.cos(a)) / 111320
        lon = lon0 + (r * math.sin(a)) / (111320 * math.cos(math.radians(lat0)))
        hdg = (math.degrees(a) + 90) % 360
        chunks.append(chunk(i, lat, lon, v * 3.6 / 1.852, hdg, (2026, 1, 1, 12, i // 60, i % 60)))

    n = len(chunks)
    gps_atom_size = 8 + 8 + 8 * n
    new_moov_end = len(data) + gps_atom_size
    free_hdr = 8
    base = new_moov_end + free_hdr
    gps = struct.pack('>I4sII', gps_atom_size, b'gps ', 0x101, n)
    for i, c in enumerate(chunks):
        gps += struct.pack('>II', base + i * len(c), len(c))
    moov_size = struct.unpack('>I', data[moov:moov + 4])[0] + gps_atom_size
    struct.pack_into('>I', data, moov, moov_size)
    data += gps
    body = b''.join(chunks)
    data += struct.pack('>I4s', 8 + len(body), b'free') + body
    open(out, 'wb').write(data)
    print(f'wrote {out}: {secs}s, {n} GPS samples around {lat0},{lon0}')


if __name__ == '__main__':
    main()
