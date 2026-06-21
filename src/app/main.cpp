#include <cstdio>
#include <cmath>
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
#include "GridRenderer.h"
#include "GpuSimulation.h"
#include "WorldIO.h"

using namespace cb;

namespace {

constexpr const char* kSaveDir = "saves";

// Round up to a multiple of 3 so the 9-phase sublattice tiles the grid evenly
// (required once the update is parallelized).
int roundTo3(int v) { return ((v + 2) / 3) * 3; }

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

int countAlive(const WorldState& w) {
    int alive = 0;
    for (uint8_t k : w.kind) alive += (k == (uint8_t)Cell::Alive);
    return alive;
}

void error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

} // namespace

int main() {
    glfwSetErrorCallback(error_callback);
    if (!glfwInit()) return 1;

    const int screenW = ScrW();
    const int screenH = ScrH();
    GLFWwindow* window = InitImgui((int)(screenW * 0.8f), (int)(screenH * 0.8f));
    if (!window) return 1;
    if (gl3wInit()) { fprintf(stderr, "gl3w init failed\n"); return 1; }

    ImPlot::CreateContext();

    // World is now decoupled from on-screen pixel size: pick a large cell grid
    // and display it scaled into the view.
    const int gridW = roundTo3(600);
    const int gridH = roundTo3(400);
    Simulation sim(gridW, gridH);
    sim.generate();

    GridRenderer renderer;
    renderer.resize(gridW, gridH);

    GpuSimulation gpu;
    bool gpuMode = false;

    float  zoom = 1.0f;          // 1.0 = fit grid to view
    ImVec2 pan(0.0f, 0.0f);      // pixel offset of the grid within the view

    bool        paused        = true;
    int         stepsPerFrame = 1;
    DisplayMode mode          = DisplayMode::Work;
    int         maxAlive      = 0;

    // Editing tools
    enum class Tool { None, Spawn, Erase };
    Tool tool       = Tool::None;
    int  brushRadius = 4;

    std::string saveName = "world.cbw";
    saveName.reserve(255);

    ScrollingBuffer popBuf;
    float plotT = 0.0f;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (IsKeyPressedOnce(GLFW_KEY_SPACE)) paused = !paused;

        const bool useGpu = gpuMode && gpu.available();
        int alive;
        unsigned int gridTex;
        if (useGpu) {
            gpu.setConfig(sim.cfg);
            if (!paused) gpu.step(stepsPerFrame);
            alive   = gpu.colorize(mode, sim.cfg.maxAge);
            gridTex = gpu.textureId();
        } else {
            if (!paused)
                for (int s = 0; s < stepsPerFrame; ++s) sim.step();
            alive = countAlive(sim.world);
            renderer.update(sim.world, mode, sim.cfg.maxAge);
            gridTex = renderer.textureId();
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

        // Zoom on the wheel, keeping the cell under the cursor pinned.
        if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
            float oldScale = fit * zoom;
            float gx = (m.x - viewPos.x - pan.x) / oldScale;
            float gy = (m.y - viewPos.y - pan.y) / oldScale;
            zoom = clampf(zoom * powf(1.15f, ImGui::GetIO().MouseWheel), 0.25f, 64.0f);
            float newScale = fit * zoom;
            pan.x = m.x - viewPos.x - gx * newScale;
            pan.y = m.y - viewPos.y - gy * newScale;
        }
        // Pan: middle-drag always, or left-drag when no brush tool is active.
        bool panDrag = ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
                       (tool == Tool::None && ImGui::IsMouseDragging(ImGuiMouseButton_Left));
        if (hovered && panDrag) { pan.x += ImGui::GetIO().MouseDelta.x; pan.y += ImGui::GetIO().MouseDelta.y; }

        float scale = fit * zoom;
        ImVec2 imgSize(gridW * scale, gridH * scale);
        // Keep the grid in view: center when smaller than the viewport, else clamp.
        pan.x = imgSize.x <= avail.x ? (avail.x - imgSize.x) * 0.5f : clampf(pan.x, avail.x - imgSize.x, 0.0f);
        pan.y = imgSize.y <= avail.y ? (avail.y - imgSize.y) * 0.5f : clampf(pan.y, avail.y - imgSize.y, 0.0f);

        ImVec2 imgPos(viewPos.x + pan.x, viewPos.y + pan.y);
        ImGui::SetCursorScreenPos(imgPos);
        ImGui::Image((ImTextureID)(intptr_t)gridTex, imgSize);

        // Hover -> cell, and apply the active brush on left drag (CPU mode only).
        int cx = (int)((m.x - imgPos.x) / scale);
        int cy = (int)((m.y - imgPos.y) / scale);
        bool overGrid = sim.world.inBounds(cx, cy) && hovered;
        if (!useGpu && overGrid && tool != Tool::None && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
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
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        std::string overlay = "Alive: " + std::to_string(alive);
        dl->AddText(ImVec2(7, 7), IM_COL32(0, 0, 0, 255), overlay.c_str());
        dl->AddText(ImVec2(6, 6), IM_COL32(255, 255, 255, 255), overlay.c_str());
        ImGui::End();
        ImGui::PopStyleVar();

        // ---- Controls ------------------------------------------------------
        ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoFocusOnAppearing);

        if (ImGui::Button(paused ? "Resume" : "Pause")) paused = !paused;
        ImGui::SameLine();
        if (ImGui::Button("Regenerate")) {
            sim.generate(); maxAlive = 0;
            if (gpuMode && gpu.available()) gpu.upload(sim.world);
        }

        if (ImGui::Checkbox("GPU (compute shader)", &gpuMode)) {
            if (gpuMode) {
                if (!gpu.available()) { if (!gpu.init(sim.world, sim.cfg)) gpuMode = false; }
                else gpu.upload(sim.world);
            } else if (gpu.available()) {
                gpu.download(sim.world);  // sync GPU state back to CPU
            }
        }
        if (gpuMode && !gpu.available()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "GPU init failed (needs GL 4.3)");
        else ImGui::TextDisabled(useGpu ? "Running on GPU" : "Running on CPU (brush tools enabled)");

        ImGui::SeparatorText("Info");
        ImGui::Text("Grid: %d x %d  (%d cells)", gridW, gridH, gridW * gridH);
        ImGui::Text("Alive: %d", alive);
        ImGui::Text("%.1f FPS (%.2f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
        ImGui::SliderInt("Steps / frame", &stepsPerFrame, 1, 32);

        ImGui::Text("Zoom: %.0f%%", zoom * 100.0f);
        ImGui::SameLine();
        if (ImGui::Button("Reset view")) { zoom = 1.0f; pan = ImVec2(0, 0); }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Wheel = zoom, middle-drag (or left-drag with no tool) = pan");

        if (ImGui::CollapsingHeader("Parameters")) {
            ImGui::SliderFloat("Photosynthesis", &sim.cfg.photoEnergy, 0.0f, 4.0f);
            ImGui::SliderFloat("Mutation chance", &sim.cfg.mutationChance, 0.0f, 1.0f);
            ImGui::SliderFloat("Live cost", &sim.cfg.liveCost, 1.0f, 50.0f);
            ImGui::SliderInt("Max age", &sim.cfg.maxAge, 1, 10000);
            ImGui::SliderFloat("Spawn chance", &sim.cfg.spawnChance, 0.0f, 1.0f);
        }

        if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen)) {
            int m0 = (int)mode;
            ImGui::RadioButton("Work", &m0, (int)DisplayMode::Work);
            ImGui::RadioButton("Energy", &m0, (int)DisplayMode::Energy);
            ImGui::RadioButton("Relative", &m0, (int)DisplayMode::Relative);
            ImGui::RadioButton("Age", &m0, (int)DisplayMode::Age);
            ImGui::RadioButton("None", &m0, (int)DisplayMode::None);
            mode = (DisplayMode)m0;
        }

        if (ImGui::CollapsingHeader("Tools")) {
            int t0 = (int)tool;
            ImGui::RadioButton("None##t", &t0, (int)Tool::None);
            ImGui::RadioButton("Spawn", &t0, (int)Tool::Spawn);
            ImGui::RadioButton("Erase", &t0, (int)Tool::Erase);
            tool = (Tool)t0;
            ImGui::SliderInt("Brush radius", &brushRadius, 0, 40);
        }

        if (ImGui::CollapsingHeader("Save / Load")) {
            ImGui::InputText("File", saveName.data(), saveName.capacity());
            if (ImGui::Button("Save")) {
                if (useGpu) gpu.download(sim.world);
                saveWorld(sim.world, std::string(kSaveDir) + "/" + saveName.c_str());
            }
            ImGui::SameLine();
            if (ImGui::Button("Load")) {
                if (loadWorld(sim.world, std::string(kSaveDir) + "/" + saveName.c_str())) {
                    renderer.resize(sim.world.width, sim.world.height);
                    if (gpuMode && gpu.available()) gpu.upload(sim.world);
                }
            }
        }

        if (ImGui::CollapsingHeader("Population")) {
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
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
