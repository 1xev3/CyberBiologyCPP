#include "Simulation.h"

namespace cb {

namespace {
// Weight genome layout (must match the GLSL forward pass):
//   [ W1: NI*NH ][ b1: NH ][ W2: NH*NO ][ b2: NO ]
constexpr int kOffW1 = 0;
constexpr int kOffB1 = kNNInputs * kNNHidden;
constexpr int kOffW2 = kOffB1 + kNNHidden;
constexpr int kOffB2 = kOffW2 + kNNHidden * kNNOutputs;

inline int idxW1(int in, int hid) { return kOffW1 + in * kNNHidden + hid; }
inline int idxB1(int hid)         { return kOffB1 + hid; }
inline int idxW2(int hid, int out){ return kOffW2 + hid * kNNOutputs + out; }
inline int idxB2(int out)         { return kOffB2 + out; }

// Store a signed weight (clamped to int8 range) as a two's-complement byte.
inline void setW(uint8_t* w, int idx, int s) {
    s = s > 127 ? 127 : (s < -128 ? -128 : s);
    w[idx] = (uint8_t)(s & 0xFF);
}
} // namespace

Simulation::Simulation(int w, int h, uint64_t seed) : rng_(seed ? seed : 1) {
    if (w > 0 && h > 0) resize(w, h);
}

void Simulation::resize(int w, int h) { world.resize(w, h); }

// --- RNG (xorshift64) ------------------------------------------------------
uint32_t Simulation::nextU32() {
    uint64_t x = rng_;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    rng_ = x;
    return (uint32_t)(x >> 32);
}
int   Simulation::randInt(int maxExclusive) { return maxExclusive <= 0 ? 0 : (int)(nextU32() % (uint32_t)maxExclusive); }
float Simulation::randFloat() { return (nextU32() >> 8) * (1.0f / 16777216.0f); }

// --- weight seeding --------------------------------------------------------
void Simulation::seedWeights(int i, bool instinct) {
    uint8_t* w = world.mindAt(i);
    for (int k = 0; k < kGenomeSize; ++k)
        setW(w, k, randInt(17) - 8);   // small random weights in [-8, 8]

    if (!instinct) return;

    // Reflex wiring. Hidden neuron 0 becomes an "energy high?" detector driven
    // solely by the energy sense (input 1). Its sign then routes the output:
    //   low energy  -> ActPhoto wins,  high energy -> ActDivide wins.
    // (Weight value = byte * kWeightScale; 1/32 here, so 127 ~ +4.0.)
    for (int in = 0; in < kNNInputs; ++in) setW(w, idxW1(in, 0), 0);
    setW(w, idxW1(1, 0), 127);   // energyNorm -> h0 (strong)
    setW(w, idxB1(0), -40);      // threshold ~ energyNorm 0.3

    for (int out = 0; out < kNNOutputs; ++out) {
        setW(w, idxW2(0, out), 0);
        setW(w, idxB2(out), -32);          // baseline: most actions unlikely
    }
    setW(w, idxB2(ActPhoto),  96);         // photo wins by default...
    setW(w, idxW2(0, ActPhoto), -127);     // ...until energy is high
    setW(w, idxB2(ActDivide), -96);
    setW(w, idxW2(0, ActDivide), 127);     // high energy -> divide
}

// --- world generation ------------------------------------------------------
void Simulation::spawnBot(int i) {
    world.kind[i]      = (uint8_t)Cell::Alive;
    world.direction[i] = (uint8_t)randInt(8);
    world.age[i]       = 0;
    world.energy[i]    = cfg.startEnergy;
    world.mineral[i]   = cfg.startEnergy * 0.5f;
    world.fr[i] = (uint8_t)randInt(256);
    world.fg[i] = (uint8_t)randInt(256);
    world.fb[i] = (uint8_t)randInt(256);
    seedWeights(i, randFloat() < cfg.instinctFraction);
}

void Simulation::generate() {
    world.clear();
    const int n = world.size();
    for (int i = 0; i < n; ++i)
        if (randFloat() < cfg.spawnChance) spawnBot(i);
}

} // namespace cb
