// Headless throughput benchmark: compares the serial and parallel 9-phase
// update on a large grid. Validates that the parallel step scales and reports
// cells/second. Not part of the GUI app.
#include <cstdio>
#include <chrono>
#include "Simulation.h"

using namespace cb;
using Clock = std::chrono::steady_clock;

static double run(Simulation& sim, int steps, bool parallel) {
    auto t0 = Clock::now();
    for (int i = 0; i < steps; ++i) parallel ? sim.step() : sim.stepSerial();
    auto t1 = Clock::now();
    return std::chrono::duration<double>(t1 - t0).count();
}

int main(int argc, char** argv) {
    int w = argc > 1 ? atoi(argv[1]) : 1200;
    int h = argc > 2 ? atoi(argv[2]) : 1200;
    int steps = argc > 3 ? atoi(argv[3]) : 60;
    w = ((w + 2) / 3) * 3;
    h = ((h + 2) / 3) * 3;

    printf("Grid %dx%d (%d cells), %d steps\n", w, h, w * h, steps);

    Simulation a(w, h, 12345);
    a.generate();
    double ts = run(a, steps, false);

    Simulation b(w, h, 12345);
    b.generate();
    double tp = run(b, steps, true);

    double cells = (double)w * h * steps;
    printf("serial   : %.3f s  (%.1f Mcells/s)\n", ts, cells / ts / 1e6);
    printf("parallel : %.3f s  (%.1f Mcells/s)\n", tp, cells / tp / 1e6);
    printf("speedup  : %.2fx\n", ts / tp);
    return 0;
}
