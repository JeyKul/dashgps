// dashgps - GPS track extraction from dashcam video files.
// Plain C++17, no Qt, so the parser can be reused by the CLI and tests.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dashgps {

struct GpsPoint {
    double t = 0;          // seconds from start of the clip
    bool valid = false;    // receiver had a fix ('A')
    double lat = 0;        // decimal degrees, signed
    double lon = 0;        // decimal degrees, signed
    double speedKmh = 0;
    double heading = 0;    // degrees, 0 = north
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0; // UTC
    int gx = 0, gy = 0, gz = 0;  // raw G-sensor values, if present
    bool hasGsensor = false;
};

struct Track {
    std::string format;    // e.g. "novatek-youqing", "novatek"
    std::vector<GpsPoint> points;

    bool hasFix() const;
    // Linear interpolation between samples. Returns false if no valid point.
    bool at(double t, GpsPoint &out) const;
};

// Parse a dashcam file. Returns an empty track (format "") if no GPS was found.
Track parseFile(const std::string &path, std::string *error = nullptr);

// Parse a single freeGPS chunk (exposed for tests / tooling).
bool parseFreeGpsChunk(const uint8_t *c, size_t n, GpsPoint &p, std::string &format);

} // namespace dashgps
