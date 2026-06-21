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
    signal.assign(n, 0.0f);
    mem.assign(n * kNNRecur, 0.0f);
    hp.assign(n, 0.0f);                   // living cells get full HP on spawn/divide
}

void WorldState::clear() {
    std::fill(kind.begin(), kind.end(), (uint8_t)Cell::Empty);
}

void WorldState::makeEmpty(int i) {
    kind[i] = (uint8_t)Cell::Empty;
    signal[i] = 0.0f;
    hp[i] = 0.0f;
    for (int k = 0; k < kNNRecur; ++k) mem[(size_t)i * kNNRecur + k] = 0.0f;
}

void WorldState::moveCell(int from, int to) {
    kind[to]      = kind[from];
    direction[to] = direction[from];
    age[to]       = age[from];
    energy[to]    = energy[from];
    mineral[to]   = mineral[from];
    marker[to]    = marker[from];
    hibernating[to] = hibernating[from];
    signal[to]    = signal[from];
    hp[to]        = hp[from];

    uint8_t*       dst = mindAt(to);
    const uint8_t* src = mindAt(from);
    for (int k = 0; k < kGenomeSize; ++k) dst[k] = src[k];
    for (int k = 0; k < kNNRecur; ++k)
        mem[(size_t)to * kNNRecur + k] = mem[(size_t)from * kNNRecur + k];

    kind[from] = (uint8_t)Cell::Empty;
}

} // namespace cb
