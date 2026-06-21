#pragma once
#include <random>
#include "GL/gl3w.h"
#include "GLFW/glfw3.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <filesystem>
#include <vector>

enum class Display
{
    dENERGY,
    dWORK,
    dRELATIVE,
    dAGE,
    dNODRAW,
};

enum class BotState {
    LV_ALIVE,
    LV_ORGANIC
};

enum class Tool {
    NONE,
    ERASE,
    MUTATE,
    PASTE_BOT
};

namespace fs = std::filesystem;
using namespace std;

bool IsKeyPressedOnce(int key);
bool IsKeyDown(int key);

bool IsMouseButtonPressedOnce(int button);
bool IsMouseKeyDown(int button);

float RandomFloat(float min, float max);
int RandomInt(int min, int max);
ImVec4 RandomColor();

const GLFWvidmode* GetVideoMode();

int ScrW();

int ScrH();

float DPI();

GLFWwindow* InitImgui(int width, int height);

void ImguiChangeStyle();

vector<string> getAllFilesInFolder(const string& folderPath);

struct ScrollingBuffer {
    int MaxSize;
    int Offset;
    ImVector<ImVec2> Data;
    ScrollingBuffer(int max_size = 2000) {
        MaxSize = max_size;
        Offset = 0;
        Data.reserve(MaxSize);
    }
    void AddPoint(float x, float y) {
        if (Data.size() < MaxSize)
            Data.push_back(ImVec2(x, y));
        else {
            Data[Offset] = ImVec2(x, y);
            Offset = (Offset + 1) % MaxSize;
        }
    }
    void Erase() {
        if (Data.size() > 0) {
            Data.shrink(0);
            Offset = 0;
        }
    }
};