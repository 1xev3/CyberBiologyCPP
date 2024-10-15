#include <iostream>
#include <vector>
#include <random>
#include <string>
#include <thread>
#include <future>

#include <fstream>
#include <filesystem>

#include <atomic> 
#include <mutex>

// Графика
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include "GL/gl3w.h"
#include "GLFW/glfw3.h"

#include "World.h"

#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#define DEBUG_NEW new(_NORMAL_BLOCK, __FILE__, __LINE__)
#define new DEBUG_NEW


using namespace std;

std::mutex mtx;
const int NUM_THREADS = thread::hardware_concurrency();




static void error_callback(int error, const char* description)
{
    fprintf(stderr, "Error %d: %s\n", error, description);
}

void SimulationThread(World& world, atomic<bool>& terminate, bool& pause, int& simulation_speed)
{
    int cur_simulation = 0;
    while (!terminate)
    {
        if (!pause)
        {
            mtx.lock();
            Bot* target;
            for (int row = 0; row < world.height; row++) {
                for (int col = 0; col < world.width; col++) {
                    target = world.matrix[row][col];
                    if (target) {
                        if (cur_simulation >= simulation_speed) {
                            target->step();
                            cur_simulation = 0;
                        }
                        else {
                            cur_simulation++;
                        }
                    }
                    
                }
            }
            mtx.unlock();
        }
    }
}


bool SELECTION_CAN_CHANGE = false;
int SELECTION_RADIUS = 0;
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);

    if (SELECTION_CAN_CHANGE) {
        SELECTION_RADIUS = clamp(SELECTION_RADIUS + (int)yoffset, 0, 100);
    }
}



int main() {
    std::cin.exceptions(std::istream::failbit);

    if (!glfwInit() || !gl3wInit())
        return 1;

    const int screen_wide = ScrW();
    const int screen_height = ScrH();

    ImVec2 grid_size = { screen_wide * 0.8f, screen_height * 0.8f };

    GLFWwindow* window = InitImgui((int)grid_size.x, (int)grid_size.y);

   
    //vars
    bool pause = true;
    int simulation_speed = 1;
    int draw_speed = 1;

    float cell_size = CELL_SIZE * DPI();

    //Creating render frame
    int grid_w = (int)(grid_size.x / cell_size);
    int grid_h = (int)(grid_size.y / cell_size);


    World world(grid_w, grid_h);
    cout << "width: " << world.width << " height: " << world.height;
    //world.matrix[5][5] = new Bot(5, 5, &world);
    GenerateMap(&world);

    Display current_display = Display::dWORK;
    Tool current_tool = Tool::NONE;
    vector<string> world_filenames = getAllFilesInFolder(SAVES_WORLD_FOLDER);
    vector<string> bot_filenames = getAllFilesInFolder(SAVES_BOTS_FOLDER);

    atomic<bool> terminate(false);
    std::thread sim_thread(SimulationThread, std::ref(world), std::ref(terminate), std::ref(pause), std::ref(simulation_speed));
    
    int display_w, display_h;
    double mx, my;
    int max_alive = 0;

    vector<thread*> threads;
    //for (int i = 0; i < 1; i++) {
    //    threads.push_back(new thread(SimulationThread, std::ref(world), std::ref(terminate), std::ref(pause), std::ref(simulation_speed)));
    //}

    pair<int, int> mouse_square;
    bool mouse_clicked = false;
    Bot selected_bot(-1,-1,&world);
    Bot copied_bot(-1, -1, &world);
    string selected_filename = "world.txt";
    selected_filename.reserve(255);
    string bot_filename = "bot.txt";
    bot_filename.reserve(255);


    glfwSetScrollCallback(window, scroll_callback);
    // Main loop
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        //ImGui::ShowDemoWindow();


        glfwGetFramebufferSize(window, &display_w, &display_h); //Calculation window size
        glfwGetCursorPos(window, &mx, &my); // Позиция курсора
        mouse_square = world.GetPosByMouse((float)mx, (float)my);

        //click test (RMB)
        if (mouse_clicked)
        {
            if (mouse_square.first != -1 && mouse_square.second != -1) {
                if (world.matrix[mouse_square.second][mouse_square.first]) {
                    selected_bot = *world.matrix[mouse_square.second][mouse_square.first];
                }
            }
            else {
                selected_bot.x = -1;
                selected_bot.y = -1;
            }
            //cout << "Mouse clicked" << endl;
        }



        //glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        //glClear(GL_COLOR_BUFFER_BIT);

        int alive = 0;
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f,0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::Begin("MainDraw", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);
            ImGui::SetWindowPos(ImVec2(0, 0));

            //ImGui::ShowTestWindow();

            if (IsKeyPressedOnce(GLFW_KEY_SPACE)) {
                pause ^= 1;
            }
            
            
            
            ImGui::SetWindowSize(grid_size);

            static ImDrawList* dl = ImGui::GetWindowDrawList();
            static ImVec2 pos = ImGui::GetWindowPos();
            static ImVec2 size = ImGui::GetWindowSize();

            dl->AddRectFilled(pos, ImVec2(pos.x+ size.x, pos.y+size.y), ImColor(30, 30, 30),0.0f);

            
            for (int row = 0; row < world.height; row++) {
                for (int col = 0; col < world.width; col++) {

                    Bot* target = world.matrix[row][col];
                    if (target) {
                        if (target->GetState() == BotState::LV_ALIVE) {
                            alive++;
                        }

                        if (max_alive < alive) {
                            max_alive = alive;
                        }

                        //Bot display
                        switch (current_display)
                        {
                        case Display::dENERGY: {
                            float enrg = (float)target->GetEnergy();
                            ImVec4 clr = { enrg / 1000.0f ,enrg / 2000.0f, 0.0f,1.0f };
                            dl->AddRectFilled(

                                ImVec2(pos.x + col * cell_size + CELL_GAP, pos.y + row * cell_size + CELL_GAP),
                                ImVec2(pos.x + col * cell_size + cell_size - CELL_GAP, pos.y + row * cell_size + cell_size - CELL_GAP),
                                IM_COL32(255 * clr.x, 165 * clr.y, clr.z, 255 * clr.w)
                            );
                            break;
                        }
                        case Display::dWORK:
                            dl->AddRectFilled(
                                ImVec2(pos.x + col * cell_size + CELL_GAP, pos.y + row * cell_size + CELL_GAP),
                                ImVec2(pos.x + col * cell_size + cell_size - CELL_GAP, pos.y + row * cell_size + cell_size - CELL_GAP),
                                IM_COL32(target->Red(), target->Green(), target->Blue(), 255)
                            );
                            break;
                        case Display::dRELATIVE: {
                            ImVec4 family = target->GetFamily();
                            dl->AddRectFilled(
                                ImVec2(pos.x + col * cell_size-1, pos.y + row * cell_size),
                                ImVec2(pos.x + col * cell_size + cell_size, pos.y + row * cell_size + cell_size+1),
                                IM_COL32((int)family.x, (int)family.y, (int)family.z, (int)family.w)
                            );
                            //dl->AddRectFilled(
                            //    ImVec2(pos.x + col * cell_size + CELL_GAP, pos.y + row * cell_size + CELL_GAP),
                            //    ImVec2(pos.x + col * cell_size + cell_size - CELL_GAP, pos.y + row * cell_size + cell_size - CELL_GAP),
                            //    IM_COL32(20, 0, 0, 255)
                            //);
                            break;
                        }
                        case Display::dAGE: {
                            float family = (float)(target->GetAge() / MAX_AGE);
                            dl->AddRectFilled(
                                ImVec2(pos.x + col * cell_size + CELL_GAP, pos.y + row * cell_size + CELL_GAP),
                                ImVec2(pos.x + col * cell_size + cell_size - CELL_GAP, pos.y + row * cell_size + cell_size - CELL_GAP),
                                IM_COL32(255 * family, 0, 255 * family, 255)
                            );
                            break;
                        }
                        case Display::dNODRAW: {
                            break;
                        }

                        default:
                            break;
                        }
                    } 
                }
            }

            //If no display
            if (current_display == Display::dNODRAW) {
                dl->AddText(ImVec2(50, 50), IM_COL32(255, 255, 255, 255), "No draw enabled!");
                //ImGui::Text("No draw enabled!");
            }

            //Draw selection
            if (mouse_square.first > 0 && mouse_square.second > 0) {
                drawCircle(mouse_square.first, mouse_square.second, SELECTION_RADIUS, &world, pos, dl);

                //if focused
                if (ImGui::IsWindowFocused()) {
                    if (ImGui::IsMouseClicked(GLFW_MOUSE_BUTTON_2)) { //right click
                        mouse_clicked = true;
                    }
                    else if (mouse_clicked) {
                        mouse_clicked = false;
                    }

                    //Если зажата левая кнопка мыши.
                    if (IsMouseKeyDown(GLFW_MOUSE_BUTTON_1) && (mouse_square.first > 0 && mouse_square.second > 0) && SELECTION_CAN_CHANGE) {
                        bool old_value = pause;
                        pause = true;

                        switch (current_tool)
                        {
                        case Tool::ERASE: {
                            mtx.lock();
                            vector<Bot*> res = GetBotsInRadius(mouse_square.first, mouse_square.second, SELECTION_RADIUS, &world);
                            for (vector<Bot*>::iterator it = res.begin(); it < res.end(); it++) {
                                Bot* target = (*it);
                                if (!target) continue;
                                target->world->matrix[target->y][target->x] = nullptr;
                            }
                            mtx.unlock();
                            break;
                        }
                        case Tool::MUTATE: {
                            mtx.lock();
                            vector<Bot*> res = GetBotsInRadius(mouse_square.first, mouse_square.second, SELECTION_RADIUS, &world);
                            for (vector<Bot*>::iterator it = res.begin(); it < res.end(); it++) {
                                Bot* target = (*it);
                                if (!target) continue;
                                target->Mutate();
                            }
                            mtx.unlock();
                            break;
                        }
                        case Tool::PASTE_BOT: {
                            mtx.lock();
                            vector<pair<int,int>> cords = GetCoordsInRadius(mouse_square.first, mouse_square.second, SELECTION_RADIUS, world.width, world.height);
                            for (vector<pair<int, int>>::iterator it = cords.begin(); it < cords.end(); it++) {
                                int x = (*it).first;
                                int y = (*it).second;
                                world.matrix[y][x] = new Bot(copied_bot);
                                world.matrix[y][x]->x = x;
                                world.matrix[y][x]->y = y;
                            }
                            mtx.unlock();
                            break;
                        }
                        default: //tool none
                            break;
                        }

                        pause = old_value;
                    }
                    SELECTION_CAN_CHANGE = true;
                }
                else if (SELECTION_CAN_CHANGE) {
                    SELECTION_CAN_CHANGE = false;
                }
            }

            dl->AddText(ImVec2(6.0f, 6.0f), IM_COL32(5, 5, 5, 255), ("ALIVE: " + to_string(alive)).c_str());
            dl->AddText(ImVec2(5.0f, 5.0f), IM_COL32(255, 255, 255, 255), ("ALIVE: " + to_string(alive)).c_str());
            

            ImGui::End(); 
            ImGui::PopStyleVar();
            ImGui::PopStyleVar();
        }

        //ImPlot::ShowDemoWindow();

        //settings tab
        {
            ImGui::Begin("Settings", NULL, ImGuiWindowFlags_NoFocusOnAppearing);

            if (ImGui::Button(pause ? "Unpause" : "Pause")) pause ^= 1;
            ImGui::SameLine();
            if (ImGui::Button("Reload")) {
                GenerateMap(&world);
            }

            ImGui::SeparatorText("Information");

            ImGui::Text("Alive: %d", alive);
            ImGui::Text("Application average %.3f ms/frame\n(%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

            ImGui::SeparatorText("Other options");
            
            //ImGui::ShowStyleEditor();
            

            if (ImGui::CollapsingHeader("Load options")) {
                
                char* mod = selected_filename.data();
                ImGui::InputText("Filename", mod, selected_filename.capacity());
                ImGui::SeparatorText("Files");
                for (auto name : world_filenames) {
                    if (name == selected_filename) {
                        if (ImGui::Button(("[Selected] " + name).c_str(), ImVec2(-1,0))) {
                            selected_filename = name;
                        }
                    }
                    else {
                        if (ImGui::Button(name.c_str(), ImVec2(-1, 0))) {
                            selected_filename = name;
                        }
                    }
                }
                ImGui::Separator();
                
                if (ImGui::Button("Save to file")) {
                    bool old_val = pause;
                    pause = true;
                    world.SaveToFile(SAVES_WORLD_FOLDER + ("/" + selected_filename));
                    world_filenames.clear();
                    world_filenames = getAllFilesInFolder(SAVES_WORLD_FOLDER);
                    pause = old_val;
                }
                ImGui::SameLine();
                if (ImGui::Button("Load from file")) {
                    bool old_val = pause;
                    pause = true;
                    world.LoadFromFile(SAVES_WORLD_FOLDER + ("/" + selected_filename));
                    pause = old_val;
                }
                ImGui::SameLine();
                if (ImGui::Button("Update files")) {
                    world_filenames.clear();
                    world_filenames = getAllFilesInFolder(SAVES_WORLD_FOLDER);
                }
                ImGui::Separator();
            }

            if (ImGui::CollapsingHeader("Main")) {
                ImGui::Text("Main");
                ImGui::SliderInt("Simulate every N frames", &simulation_speed, 1, 144);
                ImGui::SliderFloat("Photosyntesis energy", &PHOTO_ENERGY, 0, 4);
                ImGui::SliderFloat("Mutation chance", &MUTATION_CHANCE, 0, 1);
                ImGui::SliderFloat("Live cost energy", &LIVE_COST, 1, 50);
                ImGui::SliderInt("Max age", &MAX_AGE, 1, 10000);
                ImGui::Separator();
            }

            if (ImGui::CollapsingHeader("Color display")) {
                if (ImGui::RadioButton("Work", current_display == Display::dWORK)) {
                    current_display = Display::dWORK;
                }
                if (ImGui::RadioButton("Energy", current_display == Display::dENERGY)) {
                    current_display = Display::dENERGY;
                }
                if (ImGui::RadioButton("Relative", current_display == Display::dRELATIVE)) {
                    current_display = Display::dRELATIVE;
                }
                if (ImGui::RadioButton("Age", current_display == Display::dAGE)) {
                    current_display = Display::dAGE;
                }
                if (ImGui::RadioButton("No draw", current_display == Display::dNODRAW)) {
                    current_display = Display::dNODRAW;
                }
                ImGui::Separator();
            }
            if (ImGui::CollapsingHeader("Tools")) {
                if (ImGui::RadioButton("None", current_tool == Tool::NONE)) {
                    current_tool = Tool::NONE;
                }
                else if (ImGui::RadioButton("Erase", current_tool == Tool::ERASE)) {
                    current_tool = Tool::ERASE;
                }
                else if (ImGui::RadioButton("Mutate", current_tool == Tool::MUTATE)) {
                    current_tool = Tool::MUTATE;
                }
                else if (ImGui::RadioButton("Paste", current_tool == Tool::PASTE_BOT)) {
                    current_tool = Tool::PASTE_BOT;
                }

                ImGui::Separator();

                if (current_tool == Tool::PASTE_BOT) {
                    ImGui::SeparatorText("Load bot from file");

                    for (string name : bot_filenames) {
                        ImGui::Text(name.c_str());
                        ImGui::SameLine();
                        if (ImGui::Button("Copy to clipboard")) {
                            ifstream fi(SAVES_BOTS_FOLDER + ("/" + name));
                            fi >> selected_bot;
                            fi.close();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Remove")) {
                            remove((SAVES_BOTS_FOLDER + ("/" + name)).c_str());
                        }
                    }

                    ImGui::Separator();
                }
            }

            if (ImGui::CollapsingHeader("Graphics")) {
                ImGui::BulletText("Move your mouse to change the data!");
                ImGui::BulletText("This example assumes 60 FPS. Higher FPS requires larger buffer size.");
                static ScrollingBuffer sdata1;


                ImVec2 mouse = ImGui::GetMousePos();
                static float t = 0;
                t += ImGui::GetIO().DeltaTime;
                sdata1.AddPoint(t, alive);


                static float history = 10.0f;
                ImGui::SliderFloat("History", &history, 1, 30, "%.1f s");
                //rdata1.Span = history;
                //rdata2.Span = history;

                static ImPlotAxisFlags flags = ImPlotAxisFlags_NoTickLabels;

                if (ImPlot::BeginPlot("##Scrolling", ImVec2(-1, 150))) {
                    ImPlot::SetupAxes(nullptr, nullptr, flags, flags);
                    ImPlot::SetupAxisLimits(ImAxis_X1, t - history, t, ImGuiCond_Always);
                    ImPlot::SetupAxisLimits(ImAxis_Y1, 0, max_alive+100, ImPlotCond_Always);
                    ImPlot::SetNextFillStyle(IMPLOT_AUTO_COL, 0.5f);
                    //ImPlot::PlotShaded("Mouse X", &sdata1.Data[0].x, &sdata1.Data[0].y, sdata1.Data.size(), -INFINITY, 0, sdata1.Offset, 2 * sizeof(float));
                    ImPlot::PlotLine("Population", &sdata1.Data[0].x, &sdata1.Data[0].y, sdata1.Data.size(), 0, sdata1.Offset, 2 * sizeof(float));
                    ImPlot::EndPlot();
                }
            }

            //ImGui::EndMenu();
           


            ImGui::End();
        }

        //If bot selected
        if (selected_bot.GetX() > 0 && selected_bot.GetY() > 0) {
            ImGui::Begin("Selected Bot", NULL, ImGuiWindowFlags_NoFocusOnAppearing);
            ImGui::InputText("Filename", bot_filename.data() , bot_filename.capacity());

            if (ImGui::Button("Close")) {
                selected_bot.x = -1;
                selected_bot.y = -1;
            }
            ImGui::SameLine();
            if (ImGui::Button("Copy to clipboard")) {
                copied_bot = selected_bot;
            }
            ImGui::SameLine();
            if (ImGui::Button("Save to file")) {
                selected_bot.SaveToFile(bot_filename);

                selected_bot.x = -1;
                selected_bot.y = -1;
            }

            ImGui::SeparatorText("Info");
            ImGui::Text("X:%d Y:%d\nEnergy:%.2f\nMineral %.2f", selected_bot.GetX(), selected_bot.GetY(), selected_bot.GetEnergy(), selected_bot.mineral);

            ImGui::SeparatorText("Mind");
            if (ImGui::BeginTable("split", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedSame))
            {
                for (int i = 0; i < MIND_SIZE; i++) {
                    char cmd = selected_bot.mind[i];
                    ImGui::TableNextColumn(); ImGui::Text(to_string(cmd).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::End();
        }

        /*if (alive < 1) {
            RandomFill(grid);
        }*/

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    terminate = true;
    sim_thread.join();
    /*for (auto a : threads) {
        (*a).join();
    }*/


    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    ImPlot::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}