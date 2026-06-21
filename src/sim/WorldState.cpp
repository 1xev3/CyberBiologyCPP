#include "WorldState.h"

namespace cb {

void WorldState::resize(int w, int h) {
    width  = w;
    height = h;
    const size_t n = (size_t)w * h;

    kind.assign(n, (uint8_t)Cell::Empty);
    genome.assign(n * kGenomeSize, 0);
    adr.assign(n, 0);
    direction.assign(n, 1);
    mask.assign(n, 0);
    age.assign(n, 0);
    energy.assign(n, 0.0f);
    mineral.assign(n, 0.0f);
    cr.assign(n, 0); cg.assign(n, 0); cb.assign(n, 0);
    fr.assign(n, 0); fg.assign(n, 0); fb.assign(n, 0);
}

void WorldState::clear() {
    std::fill(kind.begin(), kind.end(), (uint8_t)Cell::Empty);
}

void WorldState::makeEmpty(int i) {
    kind[i] = (uint8_t)Cell::Empty;
}

void WorldState::moveCell(int from, int to) {
    kind[to]      = kind[from];
    adr[to]       = adr[from];
    direction[to] = direction[from];
    mask[to]      = mask[from];
    age[to]       = age[from];
    energy[to]    = energy[from];
    mineral[to]   = mineral[from];
    cr[to] = cr[from]; cg[to] = cg[from]; cb[to] = cb[from];
    fr[to] = fr[from]; fg[to] = fg[from]; fb[to] = fb[from];

    uint8_t*       dst = mindAt(to);
    const uint8_t* src = mindAt(from);
    for (int k = 0; k < kGenomeSize; ++k) dst[k] = src[k];

    kind[from] = (uint8_t)Cell::Empty;
}

} // namespace cb
