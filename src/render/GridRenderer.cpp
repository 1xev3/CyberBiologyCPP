#include "GridRenderer.h"
#include "GL/gl3w.h"

namespace cb {

namespace {
constexpr uint32_t kEmptyColor = 0xFF1E1E1E;  // ABGR: dark gray background

inline uint32_t rgba(uint8_t r, uint8_t g, uint8_t b) {
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;  // little-endian RGBA
}
} // namespace

GridRenderer::~GridRenderer() {
    if (tex_) glDeleteTextures(1, &tex_);
}

void GridRenderer::resize(int width, int height) {
    width_  = (unsigned)width;
    height_ = (unsigned)height;
    pixels_.assign((size_t)width_ * height_, kEmptyColor);
    if (tex_) { glDeleteTextures(1, &tex_); tex_ = 0; }
    ensureTexture();
}

void GridRenderer::ensureTexture() {
    if (tex_ || width_ == 0 || height_ == 0) return;
    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
}

void GridRenderer::update(const WorldState& world, DisplayMode mode, int maxAge) {
    if ((unsigned)world.width != width_ || (unsigned)world.height != height_)
        resize(world.width, world.height);
    ensureTexture();

    const int n = world.size();
    const float invMaxAge = maxAge > 0 ? 1.0f / maxAge : 0.0f;

    for (int i = 0; i < n; ++i) {
        if (world.kind[i] == (uint8_t)Cell::Empty) { pixels_[i] = kEmptyColor; continue; }

        switch (mode) {
            case DisplayMode::Work:
                pixels_[i] = rgba(world.cr[i], world.cg[i], world.cb[i]);
                break;
            case DisplayMode::Energy: {
                float e = world.energy[i];
                uint8_t r = (uint8_t)(255.0f * (e / 1000.0f > 1 ? 1 : e / 1000.0f));
                uint8_t g = (uint8_t)(165.0f * (e / 2000.0f > 1 ? 1 : e / 2000.0f));
                pixels_[i] = rgba(r, g, 0);
                break;
            }
            case DisplayMode::Relative:
                if (world.kind[i] == (uint8_t)Cell::Organic)
                    pixels_[i] = rgba(world.cr[i], world.cg[i], world.cb[i]);
                else
                    pixels_[i] = rgba(world.fr[i], world.fg[i], world.fb[i]);
                break;
            case DisplayMode::Age: {
                uint8_t a = (uint8_t)(255.0f * (world.age[i] * invMaxAge > 1 ? 1 : world.age[i] * invMaxAge));
                pixels_[i] = rgba(a, 0, a);
                break;
            }
            case DisplayMode::None:
                pixels_[i] = kEmptyColor;
                break;
        }
    }

    glBindTexture(GL_TEXTURE_2D, tex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_,
                    GL_RGBA, GL_UNSIGNED_BYTE, pixels_.data());
}

} // namespace cb
