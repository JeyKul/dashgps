#include "exporters.h"

#include <cstdio>

namespace dashgps {
namespace {

std::string iso(const GpsPoint &p) {
    char b[32];
    std::snprintf(b, sizeof b, "%04d-%02d-%02dT%02d:%02d:%02dZ", p.year, p.month, p.day, p.hour, p.minute, p.second);
    return b;
}

std::string num(double v, int prec) {
    char b[48];
    std::snprintf(b, sizeof b, "%.*f", prec, v);
    return b;
}

std::string xmlEscape(const std::string &s) {
    std::string r;
    for (char c : s) {
        switch (c) {
        case '&': r += "&amp;"; break;
        case '<': r += "&lt;"; break;
        case '>': r += "&gt;"; break;
        case '"': r += "&quot;"; break;
        default: r += c;
        }
    }
    return r;
}

} // namespace

void writeGpx(std::ostream &o, const std::vector<NamedTrack> &tracks) {
    o << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         "<gpx version=\"1.1\" creator=\"dashgps\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n";
    for (auto &nt : tracks) {
        o << " <trk><name>" << xmlEscape(nt.name) << "</name><trkseg>\n";
        for (auto &p : nt.track.points) {
            if (!p.valid) continue;
            o << "  <trkpt lat=\"" << num(p.lat, 7) << "\" lon=\"" << num(p.lon, 7) << "\"><time>" << iso(p)
              << "</time><course>" << num(p.heading, 1) << "</course><speed>" << num(p.speedKmh / 3.6, 2)
              << "</speed></trkpt>\n";
        }
        o << " </trkseg></trk>\n";
    }
    o << "</gpx>\n";
}

void writeCsv(std::ostream &o, const std::vector<NamedTrack> &tracks) {
    o << "file,second,utc,fix,lat,lon,speed_kmh,heading,gx,gy,gz\n";
    for (auto &nt : tracks)
        for (auto &p : nt.track.points) {
            o << nt.name << ',' << num(p.t, 0) << ',' << (p.year ? iso(p) : "") << ',' << (p.valid ? 1 : 0) << ',';
            if (p.valid) o << num(p.lat, 7) << ',' << num(p.lon, 7) << ',' << num(p.speedKmh, 1) << ',' << num(p.heading, 1);
            else o << ",,,";
            o << ',';
            if (p.hasGsensor) o << p.gx << ',' << p.gy << ',' << p.gz;
            else o << ",,";
            o << '\n';
        }
}

void writeGeoJson(std::ostream &o, const std::vector<NamedTrack> &tracks) {
    o << "{\"type\":\"FeatureCollection\",\"features\":[";
    bool firstF = true;
    for (auto &nt : tracks) {
        if (!nt.track.hasFix()) continue;
        if (!firstF) o << ',';
        firstF = false;
        o << "\n{\"type\":\"Feature\",\"properties\":{\"name\":\"" << nt.name
          << "\"},\"geometry\":{\"type\":\"LineString\",\"coordinates\":[";
        bool first = true;
        for (auto &p : nt.track.points) {
            if (!p.valid) continue;
            if (!first) o << ',';
            first = false;
            o << '[' << num(p.lon, 7) << ',' << num(p.lat, 7) << ']';
        }
        o << "]}}";
    }
    o << "\n]}\n";
}

void writeKml(std::ostream &o, const std::vector<NamedTrack> &tracks) {
    o << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<kml xmlns=\"http://www.opengis.net/kml/2.2\"><Document>\n";
    for (auto &nt : tracks) {
        if (!nt.track.hasFix()) continue;
        o << "<Placemark><name>" << xmlEscape(nt.name) << "</name><LineString><coordinates>\n";
        for (auto &p : nt.track.points)
            if (p.valid) o << num(p.lon, 7) << ',' << num(p.lat, 7) << ",0\n";
        o << "</coordinates></LineString></Placemark>\n";
    }
    o << "</Document></kml>\n";
}

bool writeAny(const std::string &fmt, std::ostream &o, const std::vector<NamedTrack> &tracks) {
    if (fmt == "gpx") writeGpx(o, tracks);
    else if (fmt == "csv") writeCsv(o, tracks);
    else if (fmt == "geojson" || fmt == "json") writeGeoJson(o, tracks);
    else if (fmt == "kml") writeKml(o, tracks);
    else return false;
    return true;
}

} // namespace dashgps
