// See docs/FORMAT.md for the byte layouts handled here.
#include "gpsparser.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

#ifdef _WIN32
#define fseeko _fseeki64
#define ftello _ftelli64
#endif

namespace dashgps {
namespace {

struct File {
    FILE *f = nullptr;
    uint64_t size = 0;
    explicit File(const std::string &path) {
        f = std::fopen(path.c_str(), "rb");
        if (!f) return;
        if (fseeko(f, 0, SEEK_END) == 0) size = (uint64_t)ftello(f);
    }
    ~File() { if (f) std::fclose(f); }
    bool read(uint64_t off, void *buf, size_t n) {
        if (fseeko(f, (int64_t)off, SEEK_SET) != 0) return false;
        return std::fread(buf, 1, n, f) == n;
    }
};

uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
uint64_t be64(const uint8_t *p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }
uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
int32_t lei32(const uint8_t *p) { return (int32_t)le32(p); }
float lef32(const uint8_t *p) { uint32_t u = le32(p); float f; std::memcpy(&f, &u, 4); return f; }

// NMEA ddmm.mmmm -> decimal degrees
double nmeaToDeg(double v) {
    double deg = std::floor(v / 100.0);
    return deg + (v - deg * 100.0) / 60.0;
}

bool plausibleDate(int y, int mo, int d, int h, int mi, int s) {
    return y >= 2000 && y < 2100 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31 &&
           h >= 0 && h < 24 && mi >= 0 && mi < 60 && s >= 0 && s < 61;
}

// Fills time/status/position/speed from a classic Novatek record.
// tOff: offset of hour,min,sec,year,month,day (6 x u32)
// sOff: offset of status, N/S, E/W bytes
// latOff/lonOff/spdOff/hdgOff: float32 fields
bool fillRecord(const uint8_t *c, size_t n, size_t tOff, size_t sOff, size_t latOff, size_t lonOff,
                size_t spdOff, size_t hdgOff, GpsPoint &p) {
    size_t need = std::max({tOff + 24, sOff + 3, latOff + 4, lonOff + 4, spdOff + 4, hdgOff + 4});
    if (n < need) return false;
    p.hour = (int)le32(c + tOff);
    p.minute = (int)le32(c + tOff + 4);
    p.second = (int)le32(c + tOff + 8);
    p.year = (int)le32(c + tOff + 12);
    p.month = (int)le32(c + tOff + 16);
    p.day = (int)le32(c + tOff + 20);
    if (p.year < 100) p.year += 2000;
    char st = (char)c[sOff], ns = (char)c[sOff + 1], ew = (char)c[sOff + 2];
    if (st != 'A') { p.valid = false; return true; }  // chunk present, no fix
    if ((ns != 'N' && ns != 'S') || (ew != 'E' && ew != 'W')) return false;
    if (!plausibleDate(p.year, p.month, p.day, p.hour, p.minute, p.second)) return false;
    double lat = nmeaToDeg(lef32(c + latOff));
    double lon = nmeaToDeg(lef32(c + lonOff));
    if (!(lat >= 0 && lat <= 90 && lon >= 0 && lon <= 180)) return false;
    p.lat = ns == 'S' ? -lat : lat;
    p.lon = ew == 'W' ? -lon : lon;
    p.speedKmh = lef32(c + spdOff) * 1.852;  // knots
    p.heading = lef32(c + hdgOff);
    p.valid = true;
    return true;
}

} // namespace

bool parseFreeGpsChunk(const uint8_t *c, size_t n, GpsPoint &p, std::string &format) {
    if (n < 0x40 || std::memcmp(c + 4, "freeGPS ", 8) != 0) return false;

    // Youqing firmware (RedTiger, WolfBox, ...): plaintext float32 position at 0x28,
    // vendor-obfuscated float64 copies at 0x50 (ignored, see docs/FORMAT.md).
    if (std::memcmp(c + 16, "YOUQINGGPS", 10) == 0) {
        if (!fillRecord(c, n, 0x30, 0x48, 0x28, 0x2c, 0x60, 0x64, p)) return false;
        if (n >= 0x88 && std::memcmp(c + 0x78, "YSKJ", 4) == 0) {
            p.gx = lei32(c + 0x7c); p.gy = lei32(c + 0x80); p.gz = lei32(c + 0x84);
            p.hasGsensor = true;
        }
        format = "novatek-youqing";
        return true;
    }

    // Classic Novatek layout (Viofo, Azdome, many generic cams):
    // time at 0x10, status at 0x28, lat/lon/speed/heading floats at 0x2c.
    {
        GpsPoint q;
        if (fillRecord(c, n, 0x10, 0x28, 0x2c, 0x30, 0x34, 0x38, q)) {
            p = q; format = "novatek"; return true;
        }
    }
    // Same record shifted by 0x20 (newer Novatek firmwares with a 32-byte preamble).
    {
        GpsPoint q;
        if (fillRecord(c, n, 0x30, 0x48, 0x4c, 0x50, 0x54, 0x58, q)) {
            p = q; format = "novatek"; return true;
        }
    }
    // Empty chunk: header present but no data written (camera had no fix yet).
    bool empty = true;
    for (size_t i = 16; i < std::min<size_t>(n, 0x80); ++i) if (c[i]) { empty = false; break; }
    if (empty) { p.valid = false; format = "novatek"; return true; }
    return false;
}

bool Track::hasFix() const {
    for (auto &p : points) if (p.valid) return true;
    return false;
}

bool Track::at(double t, GpsPoint &out) const {
    const GpsPoint *a = nullptr, *b = nullptr;
    for (auto &p : points) {
        if (!p.valid) continue;
        if (p.t <= t) a = &p;
        else { b = &p; break; }
    }
    if (!a && !b) return false;
    if (!a) { out = *b; return true; }
    if (!b || b->t - a->t > 5.0) { out = *a; return true; }
    double f = (t - a->t) / (b->t - a->t);
    out = *a;
    out.t = t;
    out.lat = a->lat + (b->lat - a->lat) * f;
    out.lon = a->lon + (b->lon - a->lon) * f;
    out.speedKmh = a->speedKmh + (b->speedKmh - a->speedKmh) * f;
    double dh = std::fmod(b->heading - a->heading + 540.0, 360.0) - 180.0;
    out.heading = std::fmod(a->heading + dh * f + 360.0, 360.0);
    return true;
}

namespace {

// Locate moov/"gps " atom. Returns list of (offset,size) chunk pointers.
bool findGpsIndex(File &f, std::vector<std::pair<uint64_t, uint32_t>> &idx) {
    uint64_t off = 0;
    uint8_t h[16];
    while (off + 8 <= f.size) {
        if (!f.read(off, h, 8)) return false;
        uint64_t sz = be32(h);
        uint32_t hl = 8;
        if (sz == 1) { if (!f.read(off + 8, h + 8, 8)) return false; sz = be64(h + 8); hl = 16; }
        if (sz == 0) sz = f.size - off;
        if (sz < hl) return false;
        if (std::memcmp(h + 4, "moov", 4) == 0) {
            uint64_t p = off + hl, end = off + sz;
            while (p + 8 <= end) {
                uint8_t b[8];
                if (!f.read(p, b, 8)) return false;
                uint32_t s2 = be32(b);
                if (s2 < 8) break;
                if (std::memcmp(b + 4, "gps ", 4) == 0 && s2 >= 16) {
                    std::vector<uint8_t> d(s2 - 8);
                    if (!f.read(p + 8, d.data(), d.size())) return false;
                    uint32_t count = be32(d.data() + 4);
                    for (uint32_t i = 0; i < count && 16 + i * 8 <= d.size(); ++i)
                        idx.emplace_back(be32(d.data() + 8 + i * 8), be32(d.data() + 12 + i * 8));
                    return true;
                }
                p += s2;
            }
            return false;
        }
        off += sz;
    }
    return false;
}

// Fallback: scan the whole file for "freeGPS " markers.
void scanForChunks(File &f, std::vector<std::pair<uint64_t, uint32_t>> &idx) {
    const size_t BUF = 1 << 20;
    std::unique_ptr<uint8_t[]> buf(new uint8_t[BUF + 16]);
    for (uint64_t off = 0; off < f.size; off += BUF) {
        size_t n = (size_t)std::min<uint64_t>(BUF + 16, f.size - off);
        if (!f.read(off, buf.get(), n)) break;
        for (size_t i = 0; i + 8 <= n && i < BUF; ++i) {
            if (buf[i] == 'f' && std::memcmp(buf.get() + i, "freeGPS ", 8) == 0 && i + off >= 4)
                idx.emplace_back(off + i - 4, 0x1000);
        }
    }
}

} // namespace

Track parseFile(const std::string &path, std::string *error) {
    Track tr;
    File f(path);
    if (!f.f) { if (error) *error = "cannot open file"; return tr; }

    std::vector<std::pair<uint64_t, uint32_t>> idx;
    if (!findGpsIndex(f, idx) || idx.empty()) scanForChunks(f, idx);
    if (idx.empty()) { if (error) *error = "no GPS data found"; return tr; }

    std::vector<uint8_t> c(0x400);
    for (size_t i = 0; i < idx.size(); ++i) {
        GpsPoint p;
        p.t = (double)i;  // Novatek writes one chunk per second of video
        auto [o, s] = idx[i];
        size_t n = std::min<size_t>(c.size(), s ? s : c.size());
        if (o != 0 && o + n <= f.size && f.read(o, c.data(), n)) {
            std::string fmt;
            if (parseFreeGpsChunk(c.data(), n, p, fmt) && tr.format.empty()) tr.format = fmt;
        }
        p.t = (double)i;
        tr.points.push_back(p);
    }
    if (tr.format.empty()) { if (error) *error = "unknown GPS format"; tr.points.clear(); }
    return tr;
}

} // namespace dashgps
