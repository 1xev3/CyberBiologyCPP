#pragma once
#include <cstdint>

namespace cb {

// --- Neural-network "brain" topology ---------------------------------------
// A living cell's genome is the weight vector of a small fixed fully-connected
// network: senses -> hidden (tanh) -> action logits. Evolution acts on these
// weights. Sizes are compile-time so the genome is a fixed length that maps
// cleanly onto GPU buffers and the GLSL forward pass.
constexpr int kNNInputs  = 23;
constexpr int kNNHidden  = 12;
constexpr int kNNOutputs = 11;   // 10 action logits + 1 turn signal

// Genome length, in bytes (one quantized signed weight per byte):
//   W1(in*hid) + b1(hid) + W2(hid*out) + b2(out)
constexpr int kGenomeSize =
    kNNInputs * kNNHidden + kNNHidden +
    kNNHidden * kNNOutputs + kNNOutputs;          // 23*12+12+12*11+11 = 431

// Quantization: stored signed byte b in [-128,127] -> weight b * kWeightScale.
constexpr float kWeightScale = 1.0f / 32.0f;


// Action ids (must match the GLSL dispatch and the instinct seeding).
enum Action {
    ActIdle = 0, ActMove, ActRotate, ActPhoto, ActMineral,
    ActEat, ActGive, ActDivide, ActAttack, ActHibernate
};

// All tunable simulation parameters in one place.
struct Config {
    float startEnergy       = 120.0f;
    float photoEnergy       = 4.5f;    // max photosynthesis gain (x light field)
    float mineralRate       = 3.0f;    // max mineral->energy gain (x mineral field)
    float metabolism        = 1.0f;    // energy cost of being alive, per tick
    float hibernationMetab  = 0.05f;   // energy/tick while hibernating (aging is frozen)
    float actionCost        = 0.5f;    // cost of any active action (move/eat/give/divide/attack)
    float divideCost        = 120.0f;  // extra energy required/spent to reproduce
    int   kinMarkerDist     = 40;      // kin if scent-marker RGB L1 distance <= this
    int   maxAge            = 6000;
    float maxEnergy         = 1000.0f;
    float maxMineral        = 1000.0f;

    // Evolution
    float mutationChance    = 0.4f;    // chance a division mutates the child
    int   mutationCount     = 2;       // weights nudged per mutation event
    int   mutationDelta     = 12;      // +/- magnitude of a weight nudge (bytes)
    int   markerDrift       = 2;       // +/- per-channel drift of the scent marker on a mutating birth

    // Environment (animated fBm fields sampled in-shader)
    float envScale          = 2.0f;    // spatial frequency of resource patches
    float envDrift          = 0.0002f; // how fast patches move (per sim tick)
    float dayNightSpeed     = 0.0003f; // global light cycle speed (0 = off)

    // World generation
    float spawnChance       = 0.15f;
    float instinctFraction  = 0.5f;    // fraction of seeds wired to photo+divide
};

} // namespace cb
