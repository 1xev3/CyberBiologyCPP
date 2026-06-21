#include "Helpers.h"

bool IsKeyPressedOnce(int key) {
    static bool keys[GLFW_KEY_LAST] = { false };
    GLFWwindow* window = glfwGetCurrentContext();
    int state = glfwGetKey(window, key);
    if (state == GLFW_PRESS && !keys[key]) {
        keys[key] = true;
        return true;
    }
    else if (state == GLFW_RELEASE) {
        keys[key] = false;
    }
    return false;
}

bool IsMouseButtonPressedOnce(int button) {
    static bool buttons[GLFW_MOUSE_BUTTON_LAST] = { false };
    GLFWwindow* window = glfwGetCurrentContext();
    int state = glfwGetMouseButton(window, button);
    if (state == GLFW_PRESS && !buttons[button]) {
        buttons[button] = true;
        return true;
    }
    else if (state == GLFW_RELEASE) {
        buttons[button] = false;
    }
    return false;
}

bool IsKeyDown(int key) {
    GLFWwindow* window = glfwGetCurrentContext();
    int state = glfwGetKey(window, key);
    if (state == GLFW_PRESS) {
        return true;
    }
    return false;
}

bool IsMouseKeyDown(int button) {
    GLFWwindow* window = glfwGetCurrentContext();
    int state = glfwGetMouseButton(window, button);
    if (state == GLFW_PRESS) {
        return true;
    }
    return false;
}

std::random_device rd;
std::mt19937 gen(rd());

float RandomFloat(float min, float max) {
    std::uniform_real_distribution<float> dst(min, max);
    return dst(gen);
}

int RandomInt(int min, int max) {
    std::uniform_int_distribution<int> dst(min, max);
    return dst(gen);
}

std::vector<std::string> getAllFilesInFolder(const std::string& folderPath) {
    std::vector<std::string> filenames;

    // Check if the folder exists, and if not, create it.
    if (!fs::exists(folderPath)) {
        try {
            fs::create_directories(folderPath);
        }
        catch (const std::filesystem::filesystem_error& ex) {
            //ex
            return filenames; // Empty vector if there was an error creating the folder.
        }
    }

    // Iterate through the directory and retrieve filenames.
    for (const auto& entry : fs::directory_iterator(folderPath)) {
        if (entry.is_regular_file()) {
            filenames.push_back(entry.path().filename().string());
        }
    }

    return filenames;
}

ImVec4 RandomColor() {
    return ImVec4(RandomFloat(0,256), RandomFloat(0,256), RandomFloat(0,256), 255);
}

const GLFWvidmode* GetVideoMode() {
    return glfwGetVideoMode(glfwGetPrimaryMonitor());
}

int ScrW() {
    return GetVideoMode()->width;
}

int ScrH() {
    return GetVideoMode()->height;
}

float DPI() {
    return ScrW() / 1920.0f;
}


GLFWwindow* InitImgui(int width, int height) {

#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GL ES 2.0 + GLSL 100
    const char* glsl_version = "#version 100";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
    // GL 3.2 + GLSL 150
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // Required on Mac
#else
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    //glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    //glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
#endif


    //Create window with context
    // Create window with graphics context
    GLFWwindow* window = glfwCreateWindow(width, height, "", nullptr, nullptr);
    if (window == nullptr)
        return nullptr;
    glfwSetWindowAttrib(window, GLFW_RESIZABLE, false);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();


    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);


    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    ImFont* custom_font = io.Fonts->AddFontFromFileTTF("fonts/amsterdam.ttf", 16.0f * DPI());
    if (!custom_font)
    {
        printf("Failed to load font");
        return nullptr;
    }


    // Set the loaded font as the default font for ImGui
    io.FontDefault = custom_font;

    ImguiChangeStyle();

    return window;
}

void ImguiChangeStyle() {
    ImGuiStyle style = ImGui::GetStyle();
    style.FramePadding = ImVec2(10.0f, 4.0f);


    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.11f, 0.12f, 0.73f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.05f, 0.05f, 0.10f, 0.90f);
    colors[ImGuiCol_Border] = ImVec4(0.26f, 0.26f, 0.26f, 1.00f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.30f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.90f, 0.80f, 0.80f, 0.40f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.90f, 0.65f, 0.65f, 0.45f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.09f, 0.09f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.91f, 0.91f, 0.95f, 0.20f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.27f, 0.27f, 0.27f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.52f, 0.52f, 0.52f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.90f, 0.90f, 0.90f, 0.50f);
    colors[ImGuiCol_SliderGrab] = ImVec4(1.00f, 1.00f, 1.00f, 0.30f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.80f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.75f, 0.75f, 0.75f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.60f, 0.60f, 0.70f, 1.00f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.70f, 0.70f, 0.90f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(1.00f, 1.00f, 1.00f, 0.30f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.60f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.90f);
    colors[ImGuiCol_Tab] = ImVec4(0.36f, 0.36f, 0.36f, 0.86f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.19f, 0.19f, 0.19f, 0.80f);
    colors[ImGuiCol_TabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.07f, 0.10f, 0.15f, 0.97f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.26f, 0.42f, 1.00f);
    colors[ImGuiCol_PlotLines] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.31f, 0.31f, 0.35f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.00f, 0.00f, 1.00f, 0.35f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
}