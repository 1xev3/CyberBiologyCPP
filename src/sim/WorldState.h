#pragma once
#include <cstdint>
#include <vector>
#include "SimConfig.h"

namespace cb {

enum class Cell : uint8_t {
    Empty   = 0,
    Alive   = 1,
    Organic = 2,
};

// Structure-of-arrays world grid. Every per-cell attribute lives in its own
// flat, contiguous array indexed by `y * width + x`. This layout is cache
// friendly and maps directly onto GPU SSBOs.
//
// `kind` is the source of truth for occupancy. The genome holds the cell's
// neural-network weights (one signed byte each, kGenomeSize of them).
class WorldState {
public:
    int width  = 0;
    int height = 0;

    std::vector<uint8_t> kind;       // Cell
    std::vector<uint8_t> direction;  // facing 0..7
    std::vector<int32_t> age;
    std::vector<float>   energy;
    std::vector<float>   mineral;
    std::vector<uint8_t> genome;      // width*height*kGenomeSize quantized weights
    std::vector<uint8_t> fr, fg, fb;  // family ("clan") color

    WorldState() = default;
    WorldState(int w, int h) { resize(w, h); }

    void resize(int w, int h);
    void clear();                     // mark every cell Empty (keeps capacity)

    int  index(int x, int y) const { return y * width + x; }
    int  size()  const { return width * height; }
    bool inBounds(int x, int y) const {
        return x >= 0 && x < width && y >= 0 && y < height;
    }

    uint8_t*       mindAt(int i)       { return &genome[(size_t)i * kGenomeSize]; }
    const uint8_t* mindAt(int i) const { return &genome[(size_t)i * kGenomeSize]; }

    // Move all per-cell attributes from cell `from` to cell `to` and mark
    // `from` Empty.
    void moveCell(int from, int to);
    void makeEmpty(int i);
};

} // namespace cb
