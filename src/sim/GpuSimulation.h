#pragma once
#include <cstdint>
#include "WorldState.h"
#include "SimConfig.h"

namespace cb {

// How the world texture is colorized. Family shows the clan (family) color so
// territories/clans are visible; Environment shows the animated resource field.
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
    void bindBuffers() const;
    void setSimUniforms();

    bool ok_ = false;
    int  width_ = 0, height_ = 0;
    uint64_t tick_ = 0;
    uint64_t seed_ = 0x9E3779B97F4A7C15ull;
    Config cfg_;

    unsigned int simProg_ = 0, colorProg_ = 0;
    unsigned int tex_ = 0;
    unsigned int counter_ = 0;            // atomic alive counter
    enum { kNumBuffers = 7 };
    unsigned int buf_[kNumBuffers] = {0}; // kind,dir,age,energy,mineral,genome,fam
};

} // namespace cb
