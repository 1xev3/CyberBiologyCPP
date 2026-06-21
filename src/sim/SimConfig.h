#pragma once

namespace cb {

// Length of a bot's genome / the virtual-machine program, in commands.
constexpr int kGenomeSize = 64;

// All tunable simulation parameters in one place. Replaces the previous set of
// mutable global `extern` variables so the engine is self-contained and the UI
// can edit a single owned instance.
struct Config {
    float startEnergy      = 100.0f;
    float photoEnergy      = 0.65f;
    float doubleCost       = 200.0f;
    float liveCost         = 10.0f;
    float eatCost          = 4.0f;
    float geneAttackCost   = 5.0f;
    int   maxGeneDifference = 2;
    int   maxAge           = 2000;
    float maxEnergy        = 1000.0f;
    float maxMineral       = 1000.0f;

    // Camouflage ("mask")
    int   maskCycles       = 3;
    float maskEnergyCost   = 15.0f;

    float mutationChance   = 0.1f;

    // Fraction of cells seeded with a bot when generating a fresh world.
    float spawnChance      = 0.2f;
};

} // namespace cb
