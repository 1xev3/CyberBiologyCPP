#pragma once
#include "Bot.h"

class World
{
public:
    int width;
    int height;
    int cell_size = CELL_SIZE;
    vector<vector<Bot*>> matrix;
public:
    World(int size_x, int size_y) {
        cell_size = CELL_SIZE * DPI();
        width = size_x;
        height = size_y;
        matrix.resize(size_y, vector<Bot*>(size_x, nullptr));
    }
    ~World() {
        for (auto mr : matrix) {
            mr.clear();
        }
        matrix.clear();
    };

    pair<int, int> GetPosByMouse(float mx, float my);

    void ClearSelf();

    bool SaveToFile(string FileName);

    bool LoadFromFile(string FileName);

};

void GenerateMap(World* world);

void drawCircleFilled(int centerX, int centerY, int radius, World* world, ImVec2 pos, ImDrawList* dl);

void DrawPixel(int col, int row, World* world, ImVec2 pos, ImDrawList* dl);

bool InGrid(int col, int row, int max_width, int max_height);

void drawCircle(int centerX, int centerY, int radius, World* world, ImVec2 pos, ImDrawList* dl);

vector<pair<int, int>> GetCoordsInRadius(int centerX, int centerY, int radius, int max_width, int max_height);

vector<Bot*> GetBotsInRadius(int centerX, int centerY, int radius, World* world);
