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

## Roadmap

The codebase is being refactored toward much larger simulations:

- **A. Foundation** — CMake build, modular `src/` layout, vendored deps isolated. *(done)*
- **B. SoA core** — flat structure-of-arrays world state, a race-free 9-phase
  (3×3 sublattice) update, and texture-based rendering instead of per-cell draws.
- **C. CPU parallelism** — parallelize the 9-phase update across cores.
- **D. GPU compute** — port the genome VM to a compute shader (state in SSBOs)
  to reach very large grids.
