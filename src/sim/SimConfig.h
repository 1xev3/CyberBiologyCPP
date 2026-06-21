#pragma once
#include <cstdint>

namespace cb {

// --- Neural-network "brain" topology ---------------------------------------
// A living cell's genome is the weight vector of a small fixed fully-connected
// network: senses -> hidden (tanh) -> action logits. Evolution acts on these
// weights. Sizes are compile-time so the genome is a fixed length that maps
// cleanly onto GPU buffers and the GLSL forward pass.
constexpr int kNNInputs  = 16;
constexpr int kNNHidden  = 12;
constexpr int kNNOutputs = 10;   // 9 action logits + 1 turn signal

// Genome length, in bytes (one quantized signed weight per byte):
//   W1(in*hid) + b1(hid) + W2(hid*out) + b2(out)
constexpr int kGenomeSize =
    kNNInputs * kNNHidden + kNNHidden +
    kNNHidden * kNNOutputs + kNNOutputs;          // 16*12+12+12*10+10 = 334

// Quantization: stored signed byte b in [-128,127] -> weight b * kWeightScale.
constexpr float kWeightScale = 1.0f / 32.0f;

// Action ids (must match the GLSL dispatch and the instinct seeding).
enum Action {
    ActIdle = 0, ActMove, ActRotate, ActPhoto, ActMineral,
    ActEat, ActGive, ActDivide, ActAttack
};

// All tunable simulation parameters in one place.
struct Config {
    float startEnergy       = 120.0f;
    float photoEnergy       = 4.0f;    // max photosynthesis gain (x light field)
    float mineralRate       = 4.0f;    // max mineral->energy gain (x mineral field)
    float metabolism        = 1.0f;    // energy cost of being alive, per tick
    float actionCost        = 0.5f;    // cost of any active action (move/eat/give/divide/attack)
    float divideCost        = 120.0f;  // extra energy required/spent to reproduce
    int   kinColorDist      = 24;      // kin if family-color L1 distance <= this
    int   maxAge            = 6000;
    float maxEnergy         = 1000.0f;
    float maxMineral        = 1000.0f;

    // Evolution
    float mutationChance    = 0.5f;    // chance a division mutates the child
    int   mutationCount     = 2;       // weights nudged per mutation event
    int   mutationDelta     = 12;      // +/- magnitude of a weight nudge (bytes)

    // Environment (animated fBm fields sampled in-shader)
    float envScale          = 2.0f;    // spatial frequency of resource patches
    float envDrift          = 0.0002f; // how fast patches move (per sim tick)
    float dayNightSpeed     = 0.0015f; // global light cycle speed (0 = off)

    // World generation
    float spawnChance       = 0.15f;
    float instinctFraction  = 0.5f;    // fraction of seeds wired to photo+divide
};

} // namespace cb
