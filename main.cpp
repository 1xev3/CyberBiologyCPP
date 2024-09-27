#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "GL/gl3w.h"
#include "GLFW/glfw3.h"

#include "ImguiHelpers.h"

#include <iostream>

using namespace std;

int main(int, char**)
{
    if (!glfwInit() || !gl3wInit())
        return 1;

    const int screen_wide = ScrW();
    const int screen_height = ScrH();


    GLFWwindow* window = InitImgui(screen_wide * 0.7f, screen_height * 0.7f);

    // Основной цикл рендеринга
    while (!glfwWindowShouldClose(window))
    {
        // Обновление окна
        glfwPollEvents();

        // Запуск нового кадра ImGui
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::ShowDemoWindow();

        {
      /*      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);*/
            ImGui::Begin("MainDraw", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);
            ImGui::SetWindowPos(ImVec2(0, 0));

            ImGui::SetWindowSize(ImVec2(screen_wide, screen_height));

            static ImDrawList* dl = ImGui::GetWindowDrawList();
            static ImVec2 pos = ImGui::GetWindowPos();
            static ImVec2 size = ImGui::GetWindowSize();

            dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImColor(30, 30, 30), 0.0f);

            ImGui::End();
        }

        // Пример создания окна в ImGui
        ImGui::Begin("Hello, ImGui!");
        ImGui::Text("This is an example window!");
        ImGui::End();

        ImguiRender(window);
    }

    ImguiShutdown(window);

    return 0;
}
