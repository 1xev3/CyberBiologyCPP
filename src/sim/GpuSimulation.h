#pragma once
#include <cstdint>
#include "WorldState.h"
#include "SimConfig.h"

namespace cb {

// How the world texture is colorized. Family shows the scent-marker clan color
// so territories/clans are visible; Environment shows the animated resource field.
enum class DisplayMode { Family, Energy, Age, Environment };

// GPU backend: the whole world lives in OpenGL shader-storage buffers and one
// simulation tick is nine compute dispatches (one per 3x3 sublattice phase).
// Each living cell runs a small neural network (genome = weights) that chooses
// one action; resources come from animated noise fields sampled in-shader. A
// second compute pass colorizes the state into a texture. Requires GL 4.3.
class GpuSimulation {
public:
    ~GpuSimulation();

    bool init(const WorldState& seed, const Config& cfg);
    bool available() const { return ok_; }

    void setConfig(const Config& cfg) { cfg_ = cfg; }
    void step(int ticks);                          // advance ticks * 9 dispatches
    int  colorize(DisplayMode mode, int maxAge);   // update texture; returns alive count

    void download(WorldState& out) const;          // copy SSBOs back to CPU (save)
    void upload(const WorldState& in);              // replace state from CPU (load)

    unsigned int textureId() const { return tex_; }
    int width()  const { return width_; }
    int height() const { return height_; }
    uint64_t ticks() const { return tick_; }

private:
    void bindBuffers() const;             // bind the 7 state SSBOs (slots 0..6)
    void setSimUniforms();
    void cacheUniformLocations();         // query+store all uniform locations once

    bool ok_ = false;
    int  width_ = 0, height_ = 0;
    uint64_t tick_ = 0;
    uint64_t seed_ = 0x9E3779B97F4A7C15ull;
    Config cfg_;

    unsigned int simProg_ = 0, colorProg_ = 0;
    unsigned int tex_ = 0;
    // Two atomic alive-counters used round-robin: each frame we read the one
    // written last frame (already complete, so no GPU stall) while the current
    // frame writes the other.
    unsigned int counter_[2] = {0, 0};
    int          curCounter_ = 0;
    bool         counterPrimed_ = false;
    int          lastAlive_ = 0;
    enum { kNumBuffers = 8 };
    unsigned int buf_[kNumBuffers] = {0}; // kind,dir,age,energy,mineral,genome,marker,hib

    // Cached uniform locations (GLint stored as int to keep GL out of the header).
    struct SimLoc {
        int W, H, phase, actW, actH, tick, seed, time;
        int photo, mineralRate, metab, hibMetab, actionCost, divide;
        int maxEnergy, maxMineral, startEnergy, mutChance;
        int envScale, envDrift, dayNight;
        int kinDist, maxAge, mutCount, mutDelta, markerDrift;
    } sl_{};
    struct ColLoc {
        int W, H, mode, maxAge, time, envScale, envDrift, dayNight;
    } cl_{};
};

} // namespace cb
