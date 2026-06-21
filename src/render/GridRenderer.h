#pragma once
#include <cstdint>
#include <vector>
#include "WorldState.h"

namespace cb {

enum class DisplayMode { Work, Energy, Relative, Age, None };

// Renders the world as a single GPU texture (one texel per cell) instead of
// issuing one draw command per cell. Each frame the visible attributes are
// packed into an RGBA buffer and uploaded with glTexSubImage2D; the caller then
// draws the texture with ImGui::Image at any zoom. This is O(cells) memory
// traffic with a single draw call, and scales to very large grids.
class GridRenderer {
public:
    ~GridRenderer();

    // (Re)allocate the GPU texture to match the world dimensions.
    void resize(int width, int height);

    // Repack `world` into the pixel buffer and upload it.
    void update(const WorldState& world, DisplayMode mode, int maxAge);

    unsigned int textureId() const { return tex_; }
    int width()  const { return width_; }
    int height() const { return height_; }

private:
    void ensureTexture();

    unsigned int width_  = 0;
    unsigned int height_ = 0;
    unsigned int tex_    = 0;
    std::vector<uint32_t> pixels_;  // RGBA8, row-major
};

} // namespace cb
