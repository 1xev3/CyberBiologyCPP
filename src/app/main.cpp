#include <cstdio>
#include <cmath>
#include <cstring>
#include <chrono>
#include <thread>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include "GL/gl3w.h"
#include "GLFW/glfw3.h"

#include "Helpers.h"
#include "Simulation.h"
#include "GpuSimulation.h"
#include "WorldIO.h"

using namespace cb;

namespace {

constexpr const char* kSaveDir = "saves";

// Round up to a multiple of 3 so the 9-phase sublattice tiles the grid evenly.
int roundTo3(int v) { return ((v + 2) / 3) * 3; }

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

} // namespace

int main(int argc, char** argv) {
    // Headless-ish benchmark: --bench [ticks] [gridW gridH]. Runs the GPU sim for
    // a fixed number of ticks (no GUI render) and prints ms/tick + cells/s, then
    // exits. Lets us measure simulation throughput objectively.
    bool bench = false;
    int  benchTicks = 300, benchW = 0, benchH = 0;
    for (int a = 1; a < argc; ++a) {
        if (std::strcmp(argv[a], "--bench") == 0) {
            bench = true;
            if (a + 1 < argc) benchTicks = atoi(argv[a + 1]);
            if (a + 3 < argc) { benchW = atoi(argv[a + 2]); benchH = atoi(argv[a + 3]); }
        }
    }

    glfwSetErrorCallback(error_callback);
    if (!glfwInit()) return 1;

    const int screenW = ScrW();
    const int screenH = ScrH();
    GLFWwindow* window = InitImgui((int)(screenW * 0.8f), (int)(screenH * 0.8f));
    if (!window) return 1;
    if (gl3wInit()) { fprintf(stderr, "gl3w init failed\n"); return 1; }

    // Vsync off: we pace the frame ourselves to kMaxFps below. Without the cap an
    // idle/paused window would spin the GPU at thousands of fps for nothing.
    glfwSwapInterval(0);
    constexpr double kMaxFps = 240.0;

    ImPlot::CreateContext();

    const int gridW = roundTo3(bench && benchW > 0 ? benchW : 800);
    const int gridH = roundTo3(bench && benchH > 0 ? benchH : 600);
    Simulation sim(gridW, gridH);   // CPU side: seeding, save/load, editing only
    sim.generate();

    GpuSimulation gpu;
    const bool gpuOk = gpu.init(sim.world, sim.cfg);
    bool worldSynced = true;        // does sim.world mirror the live GPU state?

    if (bench) {
        if (!gpuOk) { fprintf(stderr, "bench: GPU init failed\n"); return 1; }
        auto countAlive = [&]() {
            gpu.download(sim.world);
            int n = 0; for (uint8_t k : sim.world.kind) if (k == (uint8_t)Cell::Alive) ++n;
            return n;
        };
        gpu.step(2); gpu.colorize(DisplayMode::Family, sim.cfg.maxAge); glFinish();  // warm up

        printf("BENCH grid=%dx%d (%d cells)\n", gridW, gridH, gridW * gridH);

        // (1) colorize ALONE (no stepping): isolates the per-frame render-compute
        // cost so we can see whether drawing, not simulating, is what costs time.
        {
            const int reps = 120;
            glFinish();
            auto k0 = std::chrono::high_resolution_clock::now();
            for (int r = 0; r < reps; ++r) gpu.colorize(DisplayMode::Family, sim.cfg.maxAge);
            glFinish();
            auto k1 = std::chrono::high_resolution_clock::now();
            double kms = std::chrono::duration<double, std::milli>(k1 - k0).count() / reps;
            printf("BENCH colorize-only: %.3f ms/call  (%.1f fps if this were the only cost)\n",
                   kms, 1000.0 / kms);
        }

        // (2) sim ms/tick measured on the FIRST few ticks, at full population.
        {
            int a0 = countAlive();
            glFinish();
            auto s0 = std::chrono::high_resolution_clock::now();
            gpu.step(32); glFinish();
            auto s1 = std::chrono::high_resolution_clock::now();
            double sms = std::chrono::duration<double, std::milli>(s1 - s0).count() / 32.0;
            printf("BENCH sim @high-pop: %.3f ms/tick  (alive~%d)\n", sms, a0);
        }

        // (3) Realistic frame cost: step(spf) + colorize per frame, as the GUI loop
        // runs it (still no ImGui draw / swap / vsync).
        const int spf = 32, frames = 8;
        int aliveFrameStart = countAlive();
        auto c0 = std::chrono::high_resolution_clock::now();
        for (int fr = 0; fr < frames; ++fr) {
            gpu.step(spf);
            gpu.colorize(DisplayMode::Family, sim.cfg.maxAge);
        }
        glFinish();
        auto c1 = std::chrono::high_resolution_clock::now();
        int aliveFrameEnd = countAlive();
        double fms = std::chrono::duration<double, std::milli>(c1 - c0).count() / frames;
        printf("BENCH frame:  steps/frame=%d  alive~%d->%d  %.2f ms/frame  %.1f fps  (sim+colorize, no GUI/vsync)\n",
               spf, aliveFrameStart, aliveFrameEnd, fms, 1000.0 / fms);

        // Pure per-tick simulation throughput (population keeps falling here).
        int aliveBefore = countAlive();
        auto t0 = std::chrono::high_resolution_clock::now();
        gpu.step(benchTicks); glFinish();
        auto t1 = std::chrono::high_resolution_clock::now();
        int aliveAfter = countAlive();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        double msPerTick = ms / benchTicks;
        double avgAlive = 0.5 * (aliveBefore + aliveAfter);
        printf("BENCH sim:    ms/tick=%.3f  ticks/s=%.1f  alive=%d->%d  cell-updates/s=%.2fM\n",
               msPerTick, 1000.0 / msPerTick, aliveBefore, aliveAfter,
               avgAlive * (1000.0 / msPerTick) / 1e6);
        ImPlot::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    }

    float  zoom = 1.0f;
    ImVec2 pan(0.0f, 0.0f);
    bool   recenter = true;

    bool        paused        = true;
    int         stepsPerFrame = 1;
    DisplayMode mode          = DisplayMode::Family;
    int         maxAlive      = 0;

    enum class Tool { None, Spawn, Erase };
    Tool tool        = Tool::None;
    int  brushRadius = 4;

    std::string saveName = "world.cbw";
    saveName.reserve(255);

    ScrollingBuffer popBuf;
    float plotT = 0.0f;

    // Pull the live GPU world into sim.world so it can be edited/saved.
    auto syncDown = [&]() { if (!worldSynced) { gpu.download(sim.world); worldSynced = true; } };

    double nextFrameEnd = glfwGetTime();   // frame-cap pacing accumulator

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (IsKeyPressedOnce(GLFW_KEY_SPACE)) paused = !paused;

        int alive = 0;
        unsigned int gridTex = 0;
        if (gpuOk) {
            gpu.setConfig(sim.cfg);
            if (!paused) { gpu.step(stepsPerFrame); worldSynced = false; }
            alive   = gpu.colorize(mode, sim.cfg.maxAge);
            gridTex = gpu.textureId();
        }
        if (alive > maxAlive) maxAlive = alive;

        // ---- World view ----------------------------------------------------
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("World", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::SetWindowPos(ImVec2(0, 0));
        ImGui::SetWindowSize(ImVec2(screenW * 0.8f, screenH * 0.8f));

        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImVec2 viewPos = ImGui::GetCursorScreenPos();
        float fit = avail.x / gridW < avail.y / gridH ? avail.x / gridW : avail.y / gridH;
        if (fit <= 0.0f) fit = 1.0f;

        ImVec2 m = ImGui::GetMousePos();
        bool hovered = ImGui::IsWindowHovered();

        if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
            float oldScale = fit * zoom;
            float gx = (m.x - viewPos.x - pan.x) / oldScale;
            float gy = (m.y - viewPos.y - pan.y) / oldScale;
            zoom = clampf(zoom * powf(1.15f, ImGui::GetIO().MouseWheel), 0.25f, 64.0f);
            float newScale = fit * zoom;
            pan.x = m.x - viewPos.x - gx * newScale;
            pan.y = m.y - viewPos.y - gy * newScale;
        }
        bool panDrag = ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
                       (tool == Tool::None && ImGui::IsMouseDragging(ImGuiMouseButton_Left));
        if (hovered && panDrag) { pan.x += ImGui::GetIO().MouseDelta.x; pan.y += ImGui::GetIO().MouseDelta.y; }

        float scale = fit * zoom;
        ImVec2 imgSize(gridW * scale, gridH * scale);
        if (recenter) { pan = ImVec2((avail.x - imgSize.x) * 0.5f, (avail.y - imgSize.y) * 0.5f); recenter = false; }
        float marginX = imgSize.x * 3.0f, marginY = imgSize.y * 3.0f;
        pan.x = clampf(pan.x, avail.x - imgSize.x - marginX, marginX);
        pan.y = clampf(pan.y, avail.y - imgSize.y - marginY, marginY);

        ImVec2 imgPos(viewPos.x + pan.x, viewPos.y + pan.y);
        ImGui::SetCursorScreenPos(imgPos);
        if (gridTex) ImGui::Image((ImTextureID)(intptr_t)gridTex, imgSize);

        // Brush editing: edit CPU world, then re-upload to the GPU.
        int cx = (int)((m.x - imgPos.x) / scale);
        int cy = (int)((m.y - imgPos.y) / scale);
        bool overGrid = sim.world.inBounds(cx, cy) && hovered;
        if (gpuOk && overGrid && tool != Tool::None && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            syncDown();
            int r2 = brushRadius * brushRadius;
            for (int dy = -brushRadius; dy <= brushRadius; ++dy)
                for (int dx = -brushRadius; dx <= brushRadius; ++dx) {
                    if (dx * dx + dy * dy > r2) continue;
                    int x = cx + dx, y = cy + dy;
                    if (!sim.world.inBounds(x, y)) continue;
                    int i = sim.world.index(x, y);
                    if (tool == Tool::Erase) sim.world.makeEmpty(i);
                    else if (tool == Tool::Spawn && sim.world.kind[i] == (uint8_t)Cell::Empty)
                        sim.spawnBot(i);
                }
            gpu.upload(sim.world);
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        std::string overlay = gpuOk ? "Alive: " + std::to_string(alive)
                                    : "GPU init failed (needs OpenGL 4.3)";
        dl->AddText(ImVec2(7, 7), IM_COL32(0, 0, 0, 255), overlay.c_str());
        dl->AddText(ImVec2(6, 6), IM_COL32(255, 255, 255, 255), overlay.c_str());
        ImGui::End();
        ImGui::PopStyleVar();

        // ---- Controls ------------------------------------------------------
        ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoFocusOnAppearing);

        if (!gpuOk) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "GPU init failed (needs GL 4.3)");

        if (ImGui::Button(paused ? "Resume" : "Pause")) paused = !paused;
        ImGui::SameLine();
        if (ImGui::Button("Regenerate")) {
            sim.generate(); maxAlive = 0; worldSynced = true;
            if (gpuOk) gpu.upload(sim.world);
        }

        ImGui::SeparatorText("Info");
        ImGui::Text("Grid: %d x %d  (%d cells)", gridW, gridH, gridW * gridH);
        ImGui::Text("Alive: %d", alive);
        ImGui::Text("Tick: %llu", (unsigned long long)gpu.ticks());
        ImGui::Text("%.1f FPS (%.2f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
        ImGui::SliderInt("Steps / frame", &stepsPerFrame, 1, 32);

        ImGui::Text("Zoom: %.0f%%", zoom * 100.0f);
        ImGui::SameLine();
        if (ImGui::Button("Reset view")) { zoom = 1.0f; recenter = true; }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Wheel = zoom, middle-drag (or left-drag with no tool) = pan");

        if (ImGui::BeginTabBar("tabs")) {
            if (ImGui::BeginTabItem("Life")) {
                ImGui::SliderFloat("Photosynthesis", &sim.cfg.photoEnergy, 0.0f, 20.0f);
                ImGui::SliderFloat("Mineral rate", &sim.cfg.mineralRate, 0.0f, 20.0f);
                ImGui::SliderFloat("Metabolism", &sim.cfg.metabolism, 0.0f, 5.0f);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Energy paid every tick just to stay alive");
                ImGui::SliderFloat("Divide cost", &sim.cfg.divideCost, 10.0f, 500.0f);
                ImGui::SliderFloat("Attack damage", &sim.cfg.attackDamage, 0.0f, 200.0f);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("HP removed per attack; blunted by the target's kin wall");
                ImGui::SliderInt("Max age", &sim.cfg.maxAge, 100, 20000);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Evolution")) {
                ImGui::SliderFloat("Mutation rate", &sim.cfg.mutationChance, 0.0f, 1.0f);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Environment")) {
                ImGui::SliderFloat("Day/night speed", &sim.cfg.dayNightSpeed, 0.0f, 0.005f, "%.5f");
                ImGui::SliderFloat("Day length", &sim.cfg.dayFraction, 0.5f, 0.9f, "%.2f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Share of each cycle that is daylight.\n0.5 = day == night; 0.67 = day 2x night; 0.75 = day 3x night");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("World")) {
                ImGui::SliderFloat("Spawn chance", &sim.cfg.spawnChance, 0.0f, 1.0f);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("View")) {
                int m0 = (int)mode;
                ImGui::RadioButton("Family / clan", &m0, (int)DisplayMode::Family);
                ImGui::RadioButton("Energy", &m0, (int)DisplayMode::Energy);
                ImGui::RadioButton("Age", &m0, (int)DisplayMode::Age);
                ImGui::RadioButton("Environment", &m0, (int)DisplayMode::Environment);
                ImGui::RadioButton("Signal / pheromone", &m0, (int)DisplayMode::Signal);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pheromone each cell emits (the channel colonies coordinate over)");
                mode = (DisplayMode)m0;
                ImGui::Separator();
                int t0 = (int)tool;
                ImGui::TextUnformatted("Brush");
                ImGui::RadioButton("None##t", &t0, (int)Tool::None); ImGui::SameLine();
                ImGui::RadioButton("Spawn", &t0, (int)Tool::Spawn); ImGui::SameLine();
                ImGui::RadioButton("Erase", &t0, (int)Tool::Erase);
                tool = (Tool)t0;
                ImGui::SliderInt("Brush radius", &brushRadius, 0, 40);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Data")) {
                ImGui::InputText("File", saveName.data(), saveName.capacity());
                if (ImGui::Button("Save")) {
                    syncDown();
                    saveWorld(sim.world, std::string(kSaveDir) + "/" + saveName.c_str());
                }
                ImGui::SameLine();
                if (ImGui::Button("Load")) {
                    if (loadWorld(sim.world, std::string(kSaveDir) + "/" + saveName.c_str())) {
                        worldSynced = true;
                        if (gpuOk) gpu.upload(sim.world);
                    }
                }
                ImGui::Separator();
                plotT += ImGui::GetIO().DeltaTime;
                popBuf.AddPoint(plotT, (float)alive);
                static float history = 10.0f;
                ImGui::SliderFloat("History", &history, 1, 60, "%.0f s");
                if (ImPlot::BeginPlot("##pop", ImVec2(-1, 150))) {
                    ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, 0);
                    ImPlot::SetupAxisLimits(ImAxis_X1, plotT - history, plotT, ImGuiCond_Always);
                    ImPlot::SetupAxisLimits(ImAxis_Y1, 0, maxAlive + 100, ImPlotCond_Always);
                    ImPlot::PlotLine("Population", &popBuf.Data[0].x, &popBuf.Data[0].y,
                                     popBuf.Data.size(), 0, popBuf.Offset, 2 * sizeof(float));
                    ImPlot::EndPlot();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();

        ImGui::Render();
        int dw, dh;
        glfwGetFramebufferSize(window, &dw, &dh);
        glViewport(0, 0, dw, dh);
        glClearColor(0.12f, 0.12f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);

        // Frame cap (vsync is off): pace to at most kMaxFps. The wait is never more
        // than one frame (~4 ms at 240), so a short spin is accurate and cheap; when
        // the sim already runs slower than the cap this never engages. The
        // accumulator keeps long-run pacing exact without catch-up bursts.
        nextFrameEnd += 1.0 / kMaxFps;
        double tNow = glfwGetTime();
        if (tNow < nextFrameEnd) {
            while (glfwGetTime() < nextFrameEnd) std::this_thread::yield();
        } else {
            nextFrameEnd = tNow;   // running behind the cap; drop the accrued debt
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
