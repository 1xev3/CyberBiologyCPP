#pragma once
#include <cstdint>
#include "WorldState.h"
#include "SimConfig.h"

namespace cb {

// Small, fast PRNG (xorshift64). A fresh instance is seeded per cell per tick so
// the parallel update is free of shared RNG state and deterministic regardless
// of how work is scheduled across threads.
struct Rng {
    uint64_t s;
    uint32_t u32() { uint64_t x = s; x ^= x << 13; x ^= x >> 7; x ^= x << 17; s = x; return (uint32_t)(x >> 32); }
    int   i(int maxExclusive) { return maxExclusive <= 0 ? 0 : (int)(u32() % (uint32_t)maxExclusive); }
    float f() { return (u32() >> 8) * (1.0f / 16777216.0f); }
};

// CPU reference implementation of the bot world.
//
// One simulation tick advances the grid in nine passes. Pass p acts only on the
// cells of the 3x3 sublattice (x % 3 == p % 3, y % 3 == p / 3). Because every
// acting cell only ever reads/writes its own 3x3 Moore neighborhood, and acting
// cells within a pass are spaced three apart, their neighborhoods never overlap.
// This makes the in-place update free of write conflicts and embarrassingly
// parallel, and maps one-to-one onto nine GPU compute dispatches later.
class Simulation {
public:
    WorldState world;
    Config     cfg;

    explicit Simulation(int w = 0, int h = 0, uint64_t seed = 0x9E3779B97F4A7C15ull);

    void resize(int w, int h);
    void generate();            // seed a fresh random world from cfg.spawnChance
    void step();                // advance one tick (nine passes, parallel)
    void stepSerial();          // same tick, single-threaded reference

    // Run the virtual machine of the single bot at (x, y) in place. Public so a
    // future parallel scheduler can dispatch it per sublattice cell.
    void stepCell(int x, int y);

    // Spawn a fresh random bot at cell i (used by generate / editing tools).
    void spawnBot(int i);

private:
    uint64_t rng_;                       // RNG for single-threaded contexts
    uint64_t seed_;                      // base seed for per-cell RNG
    uint64_t tick_ = 0;
    std::vector<int> rows_;              // scratch: acting rows of current phase
    uint32_t nextU32();
    int   randInt(int maxExclusive);     // [0, maxExclusive)
    float randFloat();                   // [0, 1)
    Rng   cellRng(int i) const;          // independent stream for cell i this tick

    // Neighbor in lattice direction n, relative to the bot's facing. x wraps
    // horizontally; y is clamped by callers via bounds checks (never wraps).
    int neighborX(int x, uint8_t facing, int n) const;
    int neighborY(int y, uint8_t facing, int n) const;

    // --- genome VM primitives, all operating on cell index i ---
    uint8_t param(int i) const;
    void incAdr(int i, int amount);
    void jmpAdr(int i, int a);

    void goRed(int i, int n);
    void goGreen(int i, int n);
    void goBlue(int i, int n);

    void mutate(int i, Rng& r);
    bool isRelative(int self, int other) const;
    void toOrganic(int i);

    int  move(int i, int x, int y);
    int  eat(int i, int x, int y);
    int  give(int i, int x, int y);
    int  care(int i, int x, int y);
    int  seeBots(int i, int x, int y);
    int  findEmptyDirection(int x, int y, uint8_t facing) const;
    void doubleSelf(int i, int x, int y, Rng& r);
    int  isFullAround(int x, int y, uint8_t facing) const;
    void geneAttack(int i, int x, int y, Rng& r);
    void photo(int i, int y);
    void mineralGain(int i, int y);
    void rotate(int i);

    int  checkEnergy(int i) const;
    int  checkMineral(int i) const;
    int  checkLevel(int i, int y) const;
    int  checkAge(int i) const;
};

} // namespace cb
