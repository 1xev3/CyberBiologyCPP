#pragma once
#include <cstdint>
#include "WorldState.h"
#include "SimConfig.h"
#include "GridRenderer.h"  // DisplayMode

namespace cb {

// GPU backend: the whole world lives in OpenGL shader-storage buffers and one
// simulation tick is nine compute dispatches (one per 3x3 sublattice phase),
// mirroring the CPU Simulation. A second compute pass colorizes the state
// straight into a texture, so the grid never round-trips through the CPU during
// normal running. Requires an OpenGL 4.3 core context.
class GpuSimulation {
public:
    ~GpuSimulation();

    // Compile shaders and upload `seed` as the initial state. Returns false (and
    // leaves available()==false) if compute shaders are unavailable.
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
    enum { kNumBuffers = 10 };
    unsigned int buf_[kNumBuffers] = {0}; // kind,adr,dir,mask,age,energy,mineral,genome,col,fam
};

} // namespace cb
