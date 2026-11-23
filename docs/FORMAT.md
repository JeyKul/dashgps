# Dashcam GPS format notes

How the GPS data of RedTiger (and related) dashcams is stored, and how the
official "RedTiger Player" reads it. Everything here was worked out from the
camera's own files and the official player (version 1.8.1, macOS and Windows
builds), for interoperability: the vendor offers no Linux player and no format
documentation.

## The camera

- SoC: **Novatek** (the file layout is the usual Novatek `freeGPS` scheme).
- Firmware vendor tag: `YOUQINGGPS` / `YSKJ` inside each GPS record.
- Files: `CARDV/Movie_F/YYYYMMDDhhmmss_NNNNNNF.MP4` (front) and
  `CARDV/Movie_R/YYYYMMDDhhmmss_NNNNNNR.MP4` (rear), 1-minute clips,
  HEVC (`hvc1`) video + AAC audio. Front and rear clips of the same minute
  have the same timestamp (± a second or two) but different sequence numbers.
- Both front and rear files carry the full GPS track.

## Container layout

```
ftyp
skip            ("tima", "ver " firmware date)
moov
  mvhd
  trak (video)
  trak (audio)
  udta
  gps            <- index of GPS chunks
skip
mdat            <- video/audio samples, GPS chunks interleaved
```

### `moov/gps ` atom

Big-endian:

| offset | size | meaning                          |
|-------:|-----:|----------------------------------|
| 0      | 4    | version (`0x00000101`)           |
| 4      | 4    | number of entries *n*            |
| 8      | 8·n  | *n* × (u32 file offset, u32 size)|

One entry per second of video. Each points at a 16 KiB chunk inside `mdat`.

### GPS chunk (Youqing layout)

All multi-byte values little-endian unless noted.

| offset | type      | meaning |
|-------:|-----------|---------|
| 0x00   | u32 BE    | chunk size (`0x4000`) |
| 0x04   | char[8]   | `freeGPS ` |
| 0x0C   | u32       | payload length (`0x90`) |
| 0x10   | char[10]  | `YOUQINGGPS` |
| 0x28   | float32   | latitude, NMEA `ddmm.mmmm` |
| 0x2C   | float32   | longitude, NMEA `dddmm.mmmm` |
| 0x30   | u32 × 6   | UTC hour, minute, second, year−2000, month, day |
| 0x48   | char      | status: `A` = fix, `V`/0 = no fix |
| 0x49   | char      | `N` / `S` |
| 0x4A   | char      | `E` / `W` |
| 0x50   | float64   | latitude, **obfuscated** (see below) |
| 0x58   | float64   | longitude, **obfuscated** |
| 0x60   | float32   | speed in knots |
| 0x64   | float32   | course over ground, degrees |
| 0x68   | char[16]  | `1234567890123456` (placeholder serial) |
| 0x78   | char[4]   | `YSKJ` |
| 0x7C   | i32 × 3   | G-sensor X, Y, Z (raw) |
| 0x88   | u32 × 6   | local time: hour, minute, second, year, month, day |

A chunk where everything after the header is zero means the camera had no
GPS fix yet.

Decimal degrees = `floor(v/100) + (v - 100·floor(v/100)) / 60`, negated for
`S` / `W`.

## The "hash": what the official player does

The float64 pair at 0x50/0x58 jumps around randomly from second to second;
that is what makes the coordinates look "encrypted". The official player is an
Electron app (`app.asar`, author `www.dvplayer.net`) whose JavaScript is run
through javascript-obfuscator. After deobfuscating it:

- GPS parsing lives in a WebAssembly module embedded as a base64 data URL.
  It exports `parseKey`, `parseMP4`, `parseNovatekTS`, `parseMstarTS`,
  `parseQZTS`, `parseXJTS`, `parseOL8629TS` and so on — one player binary
  for many white-label brands (RedTiger, WolfBox, and others).
- `parseKey` reads a key and a "head" position from the file and hands them,
  together with an app/client ID, to the vendor's web API
  (`api.dvplayer.net`). The server answers with a token, and only with that
  token does `parseMP4` decode the high-precision coordinates.

So the vendor tied coordinate decoding to an online check.

**However**, the firmware also writes the plain float32 copy at 0x28/0x2C.
Checked against the speed (0x60) and course (0x64) fields over thousands of
clips, those floats are the real position: the distance between consecutive
seconds matches the recorded speed, and the bearing between them matches the
recorded course. float32 resolution at these magnitudes is about 1 m (latitude)
and better than 0.1 m (longitude), which is as good as a consumer GPS receiver anyway.

dashgps reads the plaintext floats and needs no network access, account or token.

## Other layouts handled

`src/gpsparser.cpp` also tries the classic Novatek layout (time at 0x10,
status at 0x28, lat/lon/speed/course float32 at 0x2C…0x38) and the same
record shifted by 0x20. If the `moov/gps ` index is missing, the file is
scanned for `freeGPS ` markers. If your camera isn't recognised, please open
an issue with the output of `dashgps-cli --info yourfile.MP4` and a hex dump
of one GPS chunk. **Don't post video files or real coordinates.**
