#include "Simulation.h"
#include <algorithm>
#include <execution>

namespace cb {

namespace {
inline uint8_t addClamp(int v, int d) { v += d; return (uint8_t)(v > 255 ? 255 : (v < 0 ? 0 : v)); }

// SplitMix64 finalizer: mixes a counter into a well-distributed 64-bit seed.
inline uint64_t splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
}

Simulation::Simulation(int w, int h, uint64_t seed)
    : rng_(seed ? seed : 1), seed_(seed ? seed : 1) {
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

Rng Simulation::cellRng(int i) const {
    uint64_t s = splitmix64(seed_ ^ ((uint64_t)i * 0xD1B54A32D192ED03ull) ^ (tick_ * 0x2545F4914F6CDD1Dull));
    return Rng{ s ? s : 1 };
}

// --- world generation ------------------------------------------------------
void Simulation::spawnBot(int i) {
    world.kind[i]      = (uint8_t)Cell::Alive;
    world.adr[i]       = 0;
    world.direction[i] = 1;
    world.mask[i]      = 0;
    world.age[i]       = 0;
    world.energy[i]    = cfg.startEnergy;
    world.mineral[i]   = cfg.startEnergy * 0.5f;
    world.cr[i] = 150; world.cg[i] = 150; world.cb[i] = 150;
    world.fr[i] = (uint8_t)randInt(256);
    world.fg[i] = (uint8_t)randInt(256);
    world.fb[i] = (uint8_t)randInt(256);
    uint8_t* m = world.mindAt(i);
    for (int k = 0; k < kGenomeSize; ++k) m[k] = (uint8_t)randInt(kGenomeSize);
}

void Simulation::generate() {
    world.clear();
    const int n = world.size();
    for (int i = 0; i < n; ++i)
        if (randFloat() < cfg.spawnChance) spawnBot(i);
}

// --- neighbor addressing ---------------------------------------------------
int Simulation::neighborX(int x, uint8_t facing, int n) const {
    n += facing; if (n >= 8) n -= 8;
    int xt = x;
    if (n == 0 || n == 6 || n == 7) { if (--xt < 0) xt = world.width - 1; }
    else if (n >= 2 && n <= 4)      { if (++xt >= world.width) xt = 0; }
    return xt;
}
int Simulation::neighborY(int y, uint8_t facing, int n) const {
    n += facing; if (n >= 8) n -= 8;
    int yt = y;
    if (n <= 2) yt--;
    else if (n >= 4 && n <= 6) yt++;
    return yt;
}

// --- VM primitives ---------------------------------------------------------
uint8_t Simulation::param(int i) const { return world.mindAt(i)[(world.adr[i] + 1) % kGenomeSize]; }
void Simulation::incAdr(int i, int amount) { world.adr[i] = (uint8_t)((world.adr[i] + amount) % kGenomeSize); }
void Simulation::jmpAdr(int i, int a) { incAdr(i, world.mindAt(i)[(world.adr[i] + a) % kGenomeSize]); }

void Simulation::goGreen(int i, int n) {
    world.cg[i] = addClamp(world.cg[i], +n); n /= 2;
    world.cr[i] = addClamp(world.cr[i], -n);
    world.cb[i] = addClamp(world.cb[i], -n);
}
void Simulation::goBlue(int i, int n) {
    world.cb[i] = addClamp(world.cb[i], +n); n /= 2;
    world.cg[i] = addClamp(world.cg[i], -n);
    world.cr[i] = addClamp(world.cr[i], -n);
}
void Simulation::goRed(int i, int n) {
    world.cr[i] = addClamp(world.cr[i], +n); n /= 2;
    world.cg[i] = addClamp(world.cg[i], -n);
    world.cb[i] = addClamp(world.cb[i], -n);
}

void Simulation::mutate(int i, Rng& r) {
    world.mindAt(i)[r.i(kGenomeSize)] = (uint8_t)r.i(kGenomeSize);
    world.fr[i] = addClamp(world.fr[i], r.i(13) - 6);
    world.fg[i] = addClamp(world.fg[i], r.i(13) - 6);
    world.fb[i] = addClamp(world.fb[i], r.i(13) - 6);
}

bool Simulation::isRelative(int self, int other) const {
    if (world.mask[self] > 0) return true;
    const uint8_t* a = world.mindAt(self);
    const uint8_t* b = world.mindAt(other);
    int diff = 0;
    for (int k = 0; k < kGenomeSize; ++k)
        if (a[k] != b[k] && ++diff > cfg.maxGeneDifference) return false;
    return true;
}

void Simulation::toOrganic(int i) {
    world.kind[i] = (uint8_t)Cell::Organic;
    world.cr[i] = 100; world.cg[i] = 100; world.cb[i] = 100;
}

// --- actions ---------------------------------------------------------------
int Simulation::move(int i, int x, int y) {
    uint8_t f = world.direction[i];
    int n = param(i) % 8;
    int xt = neighborX(x, f, n), yt = neighborY(y, f, n);
    if (yt < 0 || yt >= world.height) return 3;
    int j = world.index(xt, yt);
    if (world.kind[j] == (uint8_t)Cell::Empty) { world.moveCell(i, j); return 2; }
    if (world.kind[j] == (uint8_t)Cell::Organic) return 4;
    if (isRelative(i, j)) return 6;
    return 5;
}

int Simulation::eat(int i, int x, int y) {
    uint8_t f = world.direction[i];
    int n = param(i) % 8;
    int xt = neighborX(x, f, n), yt = neighborY(y, f, n);

    world.energy[i] -= (world.mask[i] > 0) ? cfg.eatCost * 0.5f : cfg.eatCost;

    if (yt < 0 || yt >= world.height) return 3;
    int j = world.index(xt, yt);
    if (world.kind[j] == (uint8_t)Cell::Empty) return 2;
    if (world.kind[j] == (uint8_t)Cell::Organic) {
        world.energy[i] += world.energy[j];
        world.makeEmpty(j);
        goRed(i, 50);
        return 4;
    }
    world.energy[i]  += world.energy[j];
    world.mineral[i] += world.mineral[j];
    goRed(i, 50);
    world.makeEmpty(j);
    return 5;
}

int Simulation::give(int i, int x, int y) {
    uint8_t f = world.direction[i];
    int n = param(i) % 8;
    int xt = neighborX(x, f, n), yt = neighborY(y, f, n);
    if (yt < 0 || yt >= world.height) return 3;
    int j = world.index(xt, yt);
    if (world.kind[j] == (uint8_t)Cell::Empty)   return 2;
    if (world.mask[j] > 0)                       return 2;
    if (world.kind[j] == (uint8_t)Cell::Organic) return 4;

    float h = world.energy[i] * 0.25f;
    world.energy[i] -= h;
    world.energy[j] += h;
    if (world.mineral[i] > 3) {
        float m = world.mineral[i] * 0.25f;
        world.mineral[i] -= m;
        world.mineral[j] += m;
        if (world.mineral[j] > 999) world.mineral[j] = 999;
    }
    return 5;
}

int Simulation::care(int i, int x, int y) {
    uint8_t f = world.direction[i];
    int n = param(i) % 8;
    int xt = neighborX(x, f, n), yt = neighborY(y, f, n);
    if (yt < 0 || yt >= world.height) return 3;
    int j = world.index(xt, yt);
    if (world.kind[j] == (uint8_t)Cell::Empty)   return 2;
    if (world.mask[j] > 0)                       return 2;
    if (world.kind[j] == (uint8_t)Cell::Organic) return 4;

    if (world.energy[i] > world.energy[j]) {
        float h = (world.energy[i] - world.energy[j]) * 0.5f;
        world.energy[i] -= h; world.energy[j] += h;
    }
    if (world.mineral[i] > world.mineral[j]) {
        float m = (world.mineral[i] - world.mineral[j]) * 0.5f;
        world.mineral[i] -= m; world.mineral[j] += m;
    }
    return 5;
}

int Simulation::seeBots(int i, int x, int y) {
    uint8_t f = world.direction[i];
    int n = param(i) % 8;
    int xt = neighborX(x, f, n), yt = neighborY(y, f, n);
    if (yt < 0 || yt >= world.height) return 3;
    int j = world.index(xt, yt);
    if (world.kind[j] == (uint8_t)Cell::Empty)   return 2;
    if (world.mask[j] > 0)                       return 2;
    if (world.kind[j] == (uint8_t)Cell::Organic) return 4;
    return isRelative(i, j) ? 6 : 5;
}

int Simulation::findEmptyDirection(int x, int y, uint8_t facing) const {
    for (int n = 0; n < 8; ++n) {
        int yt = neighborY(y, facing, n);
        if (yt < 0 || yt >= world.height) continue;
        int xt = neighborX(x, facing, n);
        if (world.kind[world.index(xt, yt)] == (uint8_t)Cell::Empty) return n;
    }
    return 8;
}

void Simulation::doubleSelf(int i, int x, int y, Rng& r) {
    world.energy[i] -= cfg.doubleCost;
    if (world.energy[i] <= 0) return;

    int n = findEmptyDirection(x, y, world.direction[i]);
    if (n == 8) { world.energy[i] = cfg.startEnergy; return; }

    int xt = neighborX(x, world.direction[i], n);
    int yt = neighborY(y, world.direction[i], n);
    int j = world.index(xt, yt);

    world.kind[j]      = (uint8_t)Cell::Alive;
    world.adr[j]       = 0;
    world.direction[j] = world.direction[i];
    world.mask[j]      = 0;
    world.age[j]       = 0;
    const uint8_t* src = world.mindAt(i);
    uint8_t*       dst = world.mindAt(j);
    for (int k = 0; k < kGenomeSize; ++k) dst[k] = src[k];

    world.energy[i]  *= 0.5f; world.energy[j]  = world.energy[i];
    world.mineral[i] *= 0.5f; world.mineral[j] = world.mineral[i];
    world.cr[j] = world.cr[i]; world.cg[j] = world.cg[i]; world.cb[j] = world.cb[i];
    world.fr[j] = world.fr[i]; world.fg[j] = world.fg[i]; world.fb[j] = world.fb[i];

    if (r.f() < cfg.mutationChance) mutate(j, r);
}

int Simulation::isFullAround(int x, int y, uint8_t facing) const {
    for (int n = 0; n < 8; ++n) {
        int yt = neighborY(y, facing, n);
        if (yt < 0 || yt >= world.height) continue;
        int xt = neighborX(x, facing, n);
        if (world.kind[world.index(xt, yt)] == (uint8_t)Cell::Empty) return 2;
    }
    return 1;
}

void Simulation::geneAttack(int i, int x, int y, Rng& r) {
    uint8_t f = world.direction[i];
    int n = param(i) % 8;
    int xt = neighborX(x, f, n), yt = neighborY(y, f, n);
    if (yt < 0 || yt >= world.height) return;
    int j = world.index(xt, yt);
    if (world.kind[j] != (uint8_t)Cell::Alive) return;

    world.energy[i] -= cfg.geneAttackCost;
    int g = r.i(kGenomeSize);
    world.mindAt(j)[g] = world.mindAt(i)[g];
}

void Simulation::photo(int i, int y) {
    int invertedY = world.height - y;
    world.energy[i] += (invertedY * cfg.photoEnergy) / 6.0f;
    goGreen(i, 5);
}
void Simulation::mineralGain(int i, int y) {
    world.energy[i] += (y * cfg.photoEnergy) / 6.0f;
    goBlue(i, 5);
}
void Simulation::rotate(int i) { world.direction[i] = (uint8_t)((world.direction[i] + param(i)) % 8); }

int Simulation::checkEnergy(int i)        const { return world.energy[i]  < cfg.maxEnergy  * param(i) / kGenomeSize ? 2 : 3; }
int Simulation::checkMineral(int i)       const { return world.mineral[i] < cfg.maxMineral * param(i) / kGenomeSize ? 2 : 3; }
int Simulation::checkLevel(int i, int y)  const { return y < world.height * param(i) / kGenomeSize ? 2 : 3; }
int Simulation::checkAge(int i)           const { return world.age[i]     < cfg.maxAge     * param(i) / kGenomeSize ? 2 : 3; }

// --- one bot's program -----------------------------------------------------
void Simulation::stepCell(int x, int y) {
    int i = world.index(x, y);
    if (world.kind[i] == (uint8_t)Cell::Empty) return;
    if (world.mask[i] > 0) world.mask[i]--;
    if (world.kind[i] == (uint8_t)Cell::Organic) return;

    Rng r = cellRng(i);
    for (int cyc = 0; cyc < 15; ++cyc) {
        int cmd = world.mindAt(i)[world.adr[i]];
        bool brk = false;
        switch (cmd) {
            case 0:  mutate(i, r); incAdr(i, 1); break;
            case 8:  jmpAdr(i, param(i)); break;
            case 16: doubleSelf(i, x, y, r); incAdr(i, 1); brk = true; break;
            case 23: rotate(i); incAdr(i, 2); break;
            case 26: jmpAdr(i, move(i, x, y)); brk = true; break;
            case 32: photo(i, y); incAdr(i, 1); brk = true; break;
            case 33: mineralGain(i, y); incAdr(i, 1); brk = true; break;
            case 34: incAdr(i, eat(i, x, y)); brk = true; break;
            case 36: case 37: incAdr(i, give(i, x, y)); brk = true; break;
            case 38: case 39: incAdr(i, care(i, x, y)); brk = true; break;
            case 40: incAdr(i, seeBots(i, x, y)); break;
            case 41: incAdr(i, checkLevel(i, y)); break;
            case 42: incAdr(i, checkEnergy(i)); break;
            case 43: incAdr(i, checkMineral(i)); break;
            case 44: incAdr(i, checkAge(i)); break;
            case 46: incAdr(i, isFullAround(x, y, world.direction[i])); break;
            case 52: geneAttack(i, x, y, r); incAdr(i, 2); brk = true; break;
            default: incAdr(i, cmd); break;
        }
        if (brk) break;
    }

    if (world.kind[i] != (uint8_t)Cell::Alive) return;  // may have died/moved
    if (world.energy[i] >= cfg.maxEnergy) world.energy[i] = cfg.maxEnergy;
    world.energy[i] -= cfg.liveCost;
    world.age[i]++;

    if (world.energy[i] <= 0) { world.makeEmpty(i); return; }
    if (world.age[i] > cfg.maxAge) toOrganic(i);
}

// --- nine-pass tick --------------------------------------------------------
//
// Within a pass, acting cells share no Moore-neighborhood cell, so the acting
// rows (spaced three apart) are processed in parallel. This is conflict-free as
// long as the width is a multiple of 3 (otherwise the horizontal wrap seam can
// bring two acting columns within one cell of each other); callers size grids
// accordingly.
void Simulation::step() {
    ++tick_;
    for (int phase = 0; phase < 9; ++phase) {
        const int px = phase % 3, py = phase / 3;

        rows_.clear();
        for (int y = py; y < world.height; y += 3) rows_.push_back(y);

        std::for_each(std::execution::par, rows_.begin(), rows_.end(), [&](int y) {
            for (int x = px; x < world.width; x += 3) stepCell(x, y);
        });
    }
}

void Simulation::stepSerial() {
    ++tick_;
    for (int phase = 0; phase < 9; ++phase) {
        const int px = phase % 3, py = phase / 3;
        for (int y = py; y < world.height; y += 3)
            for (int x = px; x < world.width; x += 3)
                stepCell(x, y);
    }
}

} // namespace cb
