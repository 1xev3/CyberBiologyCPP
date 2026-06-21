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

int main() {
    glfwSetErrorCallback(error_callback);
    if (!glfwInit()) return 1;

    const int screenW = ScrW();
    const int screenH = ScrH();
    GLFWwindow* window = InitImgui((int)(screenW * 0.8f), (int)(screenH * 0.8f));
    if (!window) return 1;
    if (gl3wInit()) { fprintf(stderr, "gl3w init failed\n"); return 1; }

    ImPlot::CreateContext();

    const int gridW = roundTo3(1200);
    const int gridH = roundTo3(800);
    Simulation sim(gridW, gridH);   // CPU side: seeding, save/load, editing only
    sim.generate();

    GpuSimulation gpu;
    const bool gpuOk = gpu.init(sim.world, sim.cfg);
    bool worldSynced = true;        // does sim.world mirror the live GPU state?

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
                ImGui::SliderFloat("Action cost", &sim.cfg.actionCost, 0.0f, 5.0f);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Energy paid for move / eat / give / divide / attack");
                ImGui::SliderFloat("Divide cost", &sim.cfg.divideCost, 10.0f, 500.0f);
                ImGui::SliderInt("Max age", &sim.cfg.maxAge, 100, 20000);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Evolution")) {
                ImGui::SliderFloat("Mutation rate", &sim.cfg.mutationChance, 0.0f, 1.0f);
                ImGui::SliderInt("Mutation size", &sim.cfg.mutationDelta, 1, 64);
                ImGui::SliderInt("Kin distance", &sim.cfg.kinColorDist, 0, 128);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Environment")) {
                ImGui::SliderFloat("Patch scale", &sim.cfg.envScale, 1.0f, 24.0f);
                ImGui::SliderFloat("Patch drift", &sim.cfg.envDrift, 0.0f, 0.01f, "%.4f");
                ImGui::SliderFloat("Day/night speed", &sim.cfg.dayNightSpeed, 0.0f, 0.02f, "%.4f");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("World")) {
                ImGui::SliderFloat("Spawn chance", &sim.cfg.spawnChance, 0.0f, 1.0f);
                ImGui::SliderFloat("Instinct fraction", &sim.cfg.instinctFraction, 0.0f, 1.0f);
                ImGui::TextDisabled("(applied on Regenerate)");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("View")) {
                int m0 = (int)mode;
                ImGui::RadioButton("Family / clan", &m0, (int)DisplayMode::Family);
                ImGui::RadioButton("Energy", &m0, (int)DisplayMode::Energy);
                ImGui::RadioButton("Age", &m0, (int)DisplayMode::Age);
                ImGui::RadioButton("Environment", &m0, (int)DisplayMode::Environment);
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
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
