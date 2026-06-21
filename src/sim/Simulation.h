#pragma once
#include <cstdint>
#include "WorldState.h"
#include "SimConfig.h"

namespace cb {

// CPU-side owner of the world. The per-tick simulation runs entirely on the GPU
// (see GpuSimulation); this class only holds the grid, seeds fresh worlds, and
// is the staging area for save/load and brush editing (which re-upload to GPU).
class Simulation {
public:
    WorldState world;
    Config     cfg;

    explicit Simulation(int w = 0, int h = 0, uint64_t seed = 0x9E3779B97F4A7C15ull);

    void resize(int w, int h);
    void generate();            // seed a fresh random world from cfg.spawnChance
    void spawnBot(int i);       // spawn one cell (random or instinct-wired brain)

private:
    uint64_t rng_;
    uint32_t nextU32();
    int   randInt(int maxExclusive);     // [0, maxExclusive)
    float randFloat();                   // [0, 1)

    // Fill cell i's genome with weights. `instinct` wires a simple reflex
    // (photosynthesize while low on energy, divide when high) so a fresh world
    // reliably bootstraps before evolution takes over.
    void seedWeights(int i, bool instinct);
};

} // namespace cb
