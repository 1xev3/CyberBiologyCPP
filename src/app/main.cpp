#include <cstdio>
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
#include "WorldIO.h"

using namespace cb;

namespace {

constexpr const char* kSaveDir = "saves";

// Round up to a multiple of 3 so the 9-phase sublattice tiles the grid evenly
// (required once the update is parallelized).
int roundTo3(int v) { return ((v + 2) / 3) * 3; }

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

        if (!paused)
            for (int s = 0; s < stepsPerFrame; ++s) sim.step();

        const int alive = countAlive(sim.world);
        if (alive > maxAlive) maxAlive = alive;
        renderer.update(sim.world, mode, sim.cfg.maxAge);

        // ---- World view ----------------------------------------------------
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("World", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::SetWindowPos(ImVec2(0, 0));
        ImGui::SetWindowSize(ImVec2(screenW * 0.8f, screenH * 0.8f));

        ImVec2 avail = ImGui::GetContentRegionAvail();
        float scale = avail.x / gridW < avail.y / gridH ? avail.x / gridW : avail.y / gridH;
        if (scale <= 0.0f) scale = 1.0f;
        ImVec2 imgSize(gridW * scale, gridH * scale);
        ImVec2 imgPos = ImGui::GetCursorScreenPos();
        ImGui::Image((ImTextureID)(intptr_t)renderer.textureId(), imgSize);

        // Hover -> cell, and apply the active brush on left drag.
        ImVec2 m = ImGui::GetMousePos();
        int cx = (int)((m.x - imgPos.x) / scale);
        int cy = (int)((m.y - imgPos.y) / scale);
        bool overGrid = sim.world.inBounds(cx, cy) && ImGui::IsWindowHovered();
        if (overGrid && tool != Tool::None && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
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
        if (ImGui::Button("Regenerate")) { sim.generate(); maxAlive = 0; }

        ImGui::SeparatorText("Info");
        ImGui::Text("Grid: %d x %d  (%d cells)", gridW, gridH, gridW * gridH);
        ImGui::Text("Alive: %d", alive);
        ImGui::Text("%.1f FPS (%.2f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
        ImGui::SliderInt("Steps / frame", &stepsPerFrame, 1, 32);

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
            if (ImGui::Button("Save")) saveWorld(sim.world, std::string(kSaveDir) + "/" + saveName.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Load")) {
                if (loadWorld(sim.world, std::string(kSaveDir) + "/" + saveName.c_str()))
                    renderer.resize(sim.world.width, sim.world.height);
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
