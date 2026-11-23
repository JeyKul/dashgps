// dashgps-cli: extract GPS tracks from dashcam files without the GUI.
#include "exporters.h"

#include <cstring>
#include <fstream>
#include <iostream>

using namespace dashgps;

static int usage() {
    std::cerr << "usage: dashgps-cli [-f gpx|csv|geojson|kml] [-o out] [--info] FILE...\n";
    return 2;
}

int main(int argc, char **argv) {
    std::string fmt = "gpx", out;
    bool info = false;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "-f") && i + 1 < argc) fmt = argv[++i];
        else if (!std::strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (!std::strcmp(argv[i], "--info")) info = true;
        else if (argv[i][0] == '-') return usage();
        else files.push_back(argv[i]);
    }
    if (files.empty()) return usage();

    std::vector<NamedTrack> tracks;
    int rc = 0;
    for (auto &f : files) {
        std::string err;
        Track t = parseFile(f, &err);
        size_t fixes = 0;
        for (auto &p : t.points) fixes += p.valid;
        if (info || t.format.empty())
            std::cerr << f << ": " << (t.format.empty() ? err : t.format) << ", " << t.points.size()
                      << " samples, " << fixes << " with fix\n";
        if (t.format.empty()) { rc = 1; continue; }
        std::string name = f.substr(f.find_last_of("/\\") + 1);
        tracks.push_back({name, std::move(t)});
    }
    if (info) return rc;

    std::ofstream fo;
    if (!out.empty()) {
        fo.open(out, std::ios::binary);
        if (!fo) { std::cerr << "cannot write " << out << "\n"; return 1; }
    }
    if (!writeAny(fmt, out.empty() ? std::cout : fo, tracks)) return usage();
    return rc;
}
