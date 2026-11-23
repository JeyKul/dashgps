# DashGPS

Play your dashcam videos with the GPS track on a map, with speed, time and
front/rear camera, on **Linux, Windows and macOS**. Offline and open source:
no account, no vendor server.

Made for **RedTiger** dashcams, whose official player is Windows/macOS only and
needs an online token to show coordinates. It should also work with other
Novatek-based cameras that write `freeGPS` data (many WolfBox, Viofo, Azdome and
generic "GPS Player" cams). Reports for more cameras are welcome.

## Features

- Open the SD card (or any folder) and get all clips, sorted by time
- Video with a live map: route, car position and heading, follow mode, click
  the route to jump to that moment
- Speed (km/h or mph), date/time and coordinates
- Front, rear or both cameras side by side, kept in sync
- Plays clip after clip automatically, at 0.5×–8× speed
- Export the route to **GPX**, **KML** (Google Earth), **GeoJSON** or **CSV**
- `dashgps-cli` for scripts: `dashgps-cli -f gpx -o trip.gpx CARDV/Movie_F/*.MP4`

Keys: `Space` play/pause, `←/→` ±5 s, `PgUp/PgDn` previous/next clip.

## Download

Get the build for your system from
[Releases](https://github.com/JeyKul/dashgps/releases):

- **Windows**: unzip and run `dashgps.exe`
- **macOS**: open the `.dmg` and drag DashGPS to Applications
- **Linux**: `chmod +x DashGPS-*.AppImage` and run it

## Build from source

Needs CMake ≥ 3.21, a C++17 compiler and Qt ≥ 6.5 (Widgets, Multimedia,
Network). On Linux, HEVC playback uses Qt's FFmpeg backend
(`qt6-multimedia-ffmpeg` on Arch).

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/dashgps
```

Only the command-line tool, without Qt: `cmake -B build -DDASHGPS_GUI=OFF`.

## How it works

See [docs/FORMAT.md](docs/FORMAT.md) for the reverse-engineered file format and
what the official player does with its "encrypted" coordinates.

`tools/make_sample.py` creates a synthetic test clip (test pattern plus a fake
route), so you can test without real footage.

## Privacy

Everything runs locally. The only network traffic is map tiles from
OpenStreetMap, cached on disk. Your videos and positions never leave your
computer.

## License

MIT. Map data © OpenStreetMap contributors. Not affiliated with RedTiger,
WolfBox or dvplayer.net.
