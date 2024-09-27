#pragma once
#include "GL/gl3w.h"
#include "GLFW/glfw3.h"

GLFWwindow* InitImgui(int width, int height);
void ImguiRender(GLFWwindow* window);
void ImguiShutdown(GLFWwindow* window);
int ScrW();
int ScrH();