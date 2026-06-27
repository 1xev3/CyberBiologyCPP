#pragma once
#include <cstdint>

namespace cb {

// --- Neural-network "brain" topology ---------------------------------------
// A living cell's genome is the weight vector of a small fixed fully-connected
// network: senses -> hidden (tanh) -> outputs. Evolution acts on these weights.
// Sizes are compile-time so the genome is a fixed length that maps cleanly onto
// GPU buffers and the GLSL forward pass.
//
// The net is *recurrent*: kNNRecur of its outputs are tanh-squashed and fed back
// as inputs on the next tick (per-cell `mem`). This gives cells internal state -
// timers, latched roles, oscillators - so behaviour is no longer pure reflex.
// One more output is a pheromone the cell emits into a shared `signal` field that
// neighbours read, enabling coordination/communication between cells.
constexpr int kNNRecur   = 4;    // recurrent memory units (output -> next input)

constexpr int kNNInputs  = 24;
constexpr int kNNHidden  = 12;

// Action ids (must match the GLSL dispatch and the instinct seeding). The first
// kActCount outputs are the action logits the argmax chooses among.
enum Action {
    ActIdle = 0, ActMove, ActRotate, ActPhoto, ActMineral,
    ActEat, ActGive, ActDivide, ActAttack, ActHibernate
};
constexpr int kActCount   = 10;                 // ActIdle..ActHibernate

// Output layout: [ kActCount action logits ][ turn ][ kNNRecur memory ][ signal ]
constexpr int kOutTurn    = kActCount;          // 10  rotate direction signal
constexpr int kOutRecur   = kOutTurn + 1;       // 11  first recurrent-memory output
constexpr int kOutSignal  = kOutRecur + kNNRecur; // 15  pheromone emit
constexpr int kNNOutputs  = kOutSignal + 1;     // 16

// Genome length, in bytes (one quantized signed weight per byte):
//   W1(in*hid) + b1(hid) + W2(hid*out) + b2(out)
constexpr int kGenomeSize =
    kNNInputs * kNNHidden + kNNHidden +
    kNNHidden * kNNOutputs + kNNOutputs;          // 23*12+12+12*16+16 = 496

// Quantization: stored signed byte b in [-128,127] -> weight b * kWeightScale.
constexpr float kWeightScale = 1.0f / 32.0f;

// All tunable simulation parameters in one place.
struct Config {
    float startEnergy       = 120.0f;
    float photoEnergy       = 4.0f;    // max photosynthesis gain (x light field)
    float mineralRate       = 4.0f;    // max mineral->energy gain (x mineral field)
    float metabolism        = 1.0f;    // energy cost of being alive, per tick
    float hibernationMetab  = 0.05f;   // energy/tick while hibernating (aging is frozen)
    float actionCost        = 0.5f;    // cost of any active action (move/eat/give/divide/attack)
    float divideCost        = 120.0f;  // extra energy required/spent to reproduce
    float giveFraction      = 0.10f;   // fraction of energy handed to a needier kin in front
    float attackDamage      = 120.0f;  // HP removed per attack: one-shots a lone full-HP cell
                                       // (> maxHp), but the target's kin wall can blunt it enough to survive
    float maxHp             = 100.0f;  // full health; cells die (-> corpse) at hp <= 0
    float regenRate         = 0.5f;    // HP regrown per tick when wounded
    float regenCost         = 1.0f;    // energy spent per HP regrown
    int   kinMarkerDist     = 40;      // kin if scent-marker RGB L1 distance <= this
    int   maxAge            = 6000;
    float maxEnergy         = 1000.0f;
    float maxMineral        = 1000.0f;

    // Evolution
    float mutationChance    = 0.4f;    // chance a division mutates the child
    int   mutationCount     = 2;       // weights nudged per mutation event
    int   mutationDelta     = 12;      // +/- magnitude of a weight nudge (bytes)
    int   markerDrift       = 2;       // +/- per-channel drift of the scent marker on a mutating birth

    // Environment (static fBm fields sampled in-shader)
    float envScale          = 1.0f;    // spatial frequency of resource patches (lower = bigger zones)
    float envDrift          = 0.0f;    // patches are static (0 = no drift)
    float dayNightSpeed     = 0.0f;    // global light cycle off (0 = constant daylight)
    float dayFraction       = 0.7f;    // share of each cycle that is daylight (0.5 = equal; >0.5 = longer days)

    // World generation
    float spawnChance       = 0.15f;
    float instinctFraction  = 0.5f;    // fraction of seeds wired to photo+divide
};

} // namespace cb
