#pragma once

#include "gpsparser.h"

#include <ostream>
#include <string>
#include <vector>

namespace dashgps {

struct NamedTrack {
    std::string name;
    Track track;
};

void writeGpx(std::ostream &o, const std::vector<NamedTrack> &tracks);
void writeCsv(std::ostream &o, const std::vector<NamedTrack> &tracks);
void writeGeoJson(std::ostream &o, const std::vector<NamedTrack> &tracks);
void writeKml(std::ostream &o, const std::vector<NamedTrack> &tracks);

// "gpx", "csv", "geojson", "kml" -> writer; returns false for unknown format
bool writeAny(const std::string &fmt, std::ostream &o, const std::vector<NamedTrack> &tracks);

} // namespace dashgps
