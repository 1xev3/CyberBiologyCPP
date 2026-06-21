
#include "World.h"


pair<int, int> World::GetPosByMouse(float mx, float my) {
    // Рассчитываем номер строки и столбца квадрата, над которым находится мышь
    int row = (int)my / cell_size;
    int col = (int)mx / cell_size;

    // Проверяем, что квадрат находится в пределах сетки
    if (row >= 0 && row < height && col >= 0 && col < width) {
        return make_pair(col, row);
    }

    // Если мышь находится за пределами сетки, возвращаем -1 (или другое значение, которое обозначает некорректный квадрат)
    return make_pair(-1, -1);
}

void World::ClearSelf() {
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            matrix[row][col] = nullptr;
        }
    }
}

bool World::LoadFromFile(string FileName) {
    ifstream outFile(FileName);
    if (outFile.is_open()) {
        ClearSelf();
        int num_bots = 0;
        outFile >> width >> height >> num_bots;

        for (int i = 0; i < num_bots; i++) {
            Bot temp(0, 0, this);
            outFile >> temp;
            matrix[temp.y][temp.x] = new Bot(temp);
        }
        return true;
    }
    return false;
}

bool World::SaveToFile(string FileName) {
    ofstream outFile(FileName);
    if (outFile.is_open()) {
        outFile << width << " " << height << " ";

        int num_bots = 0;
        for (int row = 0; row < height; row++) {
            for (int col = 0; col < width; col++) {
                Bot* target = matrix[row][col];
                if (target) {
                    num_bots++;
                }
            }
        }
        outFile << num_bots << " ";

        for (int row = 0; row < height; row++) {
            for (int col = 0; col < width; col++) {
                Bot* target = matrix[row][col];
                if (target) {
                    outFile << *matrix[row][col] << " ";
                }
            }
        }
        return true;
    }
    return false;
}

void GenerateMap(World* world) {
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            world->matrix[y][x] = nullptr;
        }
    }

    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            if (RandomFloat(0, 1) < 0.2) {
                world->matrix[y][x] = new Bot(x, y, world);
            }
        }
    }
}

void drawCircleFilled(int centerX, int centerY, int radius, World* world, ImVec2 pos, ImDrawList* dl) {
    int x = radius;
    int y = 0;
    int radiusError = 1 - x;

    while (x >= y) {
        for (int i = centerX - x; i <= centerX + x; i++) {
            DrawPixel(i, centerY + y, world, pos, dl);
            DrawPixel(i, centerY - y, world, pos, dl);
        }
        for (int i = centerX - y; i <= centerX + y; i++) {
            DrawPixel(i, centerY + x, world, pos, dl);
            DrawPixel(i, centerY - x, world, pos, dl);
        }

        y++;
        if (radiusError < 0) {
            radiusError += 2 * y + 1;
        }
        else {
            x--;
            radiusError += 2 * (y - x) + 1;
        }
    }
}


// Функция для установки точки в массиве
void DrawPixel(int col, int row, World* world, ImVec2 pos, ImDrawList* dl) {
    if (col >= 0 && col < world->width && row >= 0 && row < world->height) {
        dl->AddRectFilled(
            ImVec2(pos.x + col * world->cell_size + CELL_GAP, pos.y + row * world->cell_size + CELL_GAP),
            ImVec2(pos.x + col * world->cell_size + world->cell_size - CELL_GAP, pos.y + row * world->cell_size + world->cell_size - CELL_GAP),
            IM_COL32(255, 255, 255, 255)
        );
    }
}

bool InGrid(int col, int row, int max_width, int max_height) {
    return (col >= 0 && col < max_width && row >= 0 && row < max_height);
}

void drawCircle(int centerX, int centerY, int radius, World* world, ImVec2 pos, ImDrawList* dl) {
    int x = radius;
    int y = 0;
    int radiusError = 1 - x;

    while (x >= y) {
        DrawPixel(centerX + x, centerY + y, world, pos, dl);
        DrawPixel(centerX - x, centerY + y, world, pos, dl);
        DrawPixel(centerX + x, centerY - y, world, pos, dl);
        DrawPixel(centerX - x, centerY - y, world, pos, dl);
        DrawPixel(centerX + y, centerY + x, world, pos, dl);
        DrawPixel(centerX - y, centerY + x, world, pos, dl);
        DrawPixel(centerX + y, centerY - x, world, pos, dl);
        DrawPixel(centerX - y, centerY - x, world, pos, dl);

        y++;
        if (radiusError < 0) {
            radiusError += 2 * y + 1;
        }
        else {
            x--;
            radiusError += 2 * (y - x) + 1;
        }
    }
}

vector<pair<int, int>> GetCoordsInRadius(int centerX, int centerY, int radius, int max_width, int max_height) {
    int x = radius;
    int y = 0;
    int radiusError = 1 - x;

    vector<pair<int, int>> results;

    while (x >= y) {
        for (int i = centerX - x; i <= centerX + x; i++) {
            if (InGrid(i, centerY + y, max_width, max_height)) {
                results.push_back(make_pair(i, centerY + y));
            }
            if (InGrid(i, centerY - y, max_width, max_height)) {
                results.push_back(make_pair(i, centerY - y));
            }
        }
        for (int i = centerX - y; i <= centerX + y; i++) {
            if (InGrid(i, centerY + x, max_width, max_height)) {
                results.push_back(make_pair(i, centerY + x));
            }
            if (InGrid(i, centerY - x, max_width, max_height)) {
                results.push_back(make_pair(i, centerY - x));
            }
        }

        y++;
        if (radiusError < 0) {
            radiusError += 2 * y + 1;
        }
        else {
            x--;
            radiusError += 2 * (y - x) + 1;
        }
    }
    return results;
}

vector<Bot*> GetBotsInRadius(int centerX, int centerY, int radius, World* world) {
    vector<pair<int, int>> cords = GetCoordsInRadius(centerX, centerY, radius, world->width, world->height);
    vector<Bot*> results;
    for (auto c : cords) {
        if (world->matrix[c.second][c.first]) {
            results.push_back(world->matrix[c.second][c.first]);
        }
    }
    return results;
}
