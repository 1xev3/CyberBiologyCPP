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

        // Пример создания окна в ImGui
        ImGui::Begin("Hello, ImGui!");
        ImGui::Text("This is an example window!");
        ImGui::End();

        ImguiRender(window);
    }

    ImguiShutdown(window);

    return 0;
}
