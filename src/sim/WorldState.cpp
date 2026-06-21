#include "WorldState.h"
#include <algorithm>

namespace cb {

void WorldState::resize(int w, int h) {
    width  = w;
    height = h;
    const size_t n = (size_t)w * h;

    kind.assign(n, (uint8_t)Cell::Empty);
    direction.assign(n, 1);
    age.assign(n, 0);
    energy.assign(n, 0.0f);
    mineral.assign(n, 0.0f);
    genome.assign(n * kGenomeSize, 0);   // byte 0 == signed weight 0
    marker.assign(n, 0);
    hibernating.assign(n, 0);
}

void WorldState::clear() {
    std::fill(kind.begin(), kind.end(), (uint8_t)Cell::Empty);
}

void WorldState::makeEmpty(int i) {
    kind[i] = (uint8_t)Cell::Empty;
}

void WorldState::moveCell(int from, int to) {
    kind[to]      = kind[from];
    direction[to] = direction[from];
    age[to]       = age[from];
    energy[to]    = energy[from];
    mineral[to]   = mineral[from];
    marker[to]    = marker[from];
    hibernating[to] = hibernating[from];

    uint8_t*       dst = mindAt(to);
    const uint8_t* src = mindAt(from);
    for (int k = 0; k < kGenomeSize; ++k) dst[k] = src[k];

    kind[from] = (uint8_t)Cell::Empty;
}

} // namespace cb
