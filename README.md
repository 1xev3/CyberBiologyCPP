# Cyberbiology CPP

An interactive cellular-automaton life simulator inspired by the "Evolution"
bot-world videos. Each cell hosts a *bot* driven by a small genome (a 64-command
virtual machine); bots photosynthesize, eat, share energy, reproduce and mutate,
producing emergent evolutionary behavior. Rendering and controls are built with
Dear ImGui + ImPlot on top of GLFW/OpenGL.

## Project layout

```
src/
  app/        application entry point and main loop
  sim/        simulation core (Bot VM, World grid, config)
  platform/   windowing, input and helper utilities (GLFW/ImGui glue)
third_party/  vendored Dear ImGui, ImPlot, gl3w and prebuilt GLFW
fonts/        UI fonts copied next to the executable at build time
saves/ bots/  on-disk world and bot snapshots
```

## Building (Windows, Visual Studio 2022)

Requires CMake 3.20+ and the MSVC v143 toolset.

```sh
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The executable is written to `build/bin/Release/CyberBiology.exe`, with `fonts/`,
`saves/` and `bots/` placed alongside it automatically.

## Simulation backends

The world is a flat structure-of-arrays grid (`WorldState`) advanced by a
race-free **9-phase** update: each tick runs nine passes over the cells of a 3×3
sublattice, so every acting cell only touches its own disjoint Moore
neighborhood. Two interchangeable backends implement it:

- **CPU** (`Simulation`) — parallelized across cores with
  `std::execution::par` over the write-disjoint sublattice rows. Toggle off the
  "GPU" checkbox to use it; brush editing tools are available here.
- **GPU** (`GpuSimulation`) — the genome VM ported to an OpenGL compute shader
  with all state in SSBOs; nine `glDispatchCompute` calls per tick and a second
  compute pass that colorizes straight into the display texture (no CPU
  round-trip). Enable the "GPU (compute shader)" checkbox. **Requires an OpenGL
  4.3 core context.**

The grid is drawn as a single texture (one texel per cell), so rendering cost is
one draw call regardless of world size.

## Roadmap

All four refactor stages are complete:

- **A. Foundation** — CMake build, modular `src/` layout, vendored deps isolated.
- **B. SoA core** — flat world state, race-free 9-phase update, texture rendering.
- **C. CPU parallelism** — parallel 9-phase update (~6× on 20 cores).
- **D. GPU compute** — genome VM as a compute shader for very large grids.
