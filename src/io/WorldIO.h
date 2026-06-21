#pragma once
#include <string>
#include "WorldState.h"

namespace cb {

// Binary snapshot of the SoA world. Replaces the old text format: it stores the
// full grid (including empty cells) as raw attribute arrays, which is fast and
// trivially matches the in-memory layout.
bool saveWorld(const WorldState& world, const std::string& path);
bool loadWorld(WorldState& world, const std::string& path);

} // namespace cb
