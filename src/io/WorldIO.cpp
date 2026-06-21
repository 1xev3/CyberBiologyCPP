#include "WorldIO.h"
#include <fstream>
#include <cstdint>

namespace cb {

namespace {
constexpr char kMagic[4] = {'C', 'B', 'W', '3'};

template <class T>
void writeVec(std::ofstream& o, const std::vector<T>& v) {
    o.write(reinterpret_cast<const char*>(v.data()), (std::streamsize)(v.size() * sizeof(T)));
}
template <class T>
void readVec(std::ifstream& in, std::vector<T>& v) {
    in.read(reinterpret_cast<char*>(v.data()), (std::streamsize)(v.size() * sizeof(T)));
}
} // namespace

bool saveWorld(const WorldState& w, const std::string& path) {
    std::ofstream o(path, std::ios::binary);
    if (!o) return false;
    o.write(kMagic, 4);
    int32_t dims[2] = {w.width, w.height};
    o.write(reinterpret_cast<const char*>(dims), sizeof(dims));
    writeVec(o, w.kind);   writeVec(o, w.direction); writeVec(o, w.age);
    writeVec(o, w.energy); writeVec(o, w.mineral);   writeVec(o, w.genome);
    writeVec(o, w.fr); writeVec(o, w.fg); writeVec(o, w.fb);
    return (bool)o;
}

bool loadWorld(WorldState& w, const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    char magic[4];
    in.read(magic, 4);
    for (int i = 0; i < 4; ++i) if (magic[i] != kMagic[i]) return false;
    int32_t dims[2];
    in.read(reinterpret_cast<char*>(dims), sizeof(dims));
    w.resize(dims[0], dims[1]);
    readVec(in, w.kind);   readVec(in, w.direction); readVec(in, w.age);
    readVec(in, w.energy); readVec(in, w.mineral);   readVec(in, w.genome);
    readVec(in, w.fr); readVec(in, w.fg); readVec(in, w.fb);
    return (bool)in;
}

} // namespace cb
