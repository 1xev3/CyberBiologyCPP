#pragma once

#include "config.h"
#include "Helpers.h"
#include <filesystem>
#include <fstream>


class World;

class Bot
{
public:
    int x;
    int y;
    BotState state = BotState::LV_ALIVE;
    char mind[64];
    int adr = 0;
    int direction = 1;

    float energy = START_ENERGY;
    float mineral = START_ENERGY / 2;
    int age = 0;
    int mask = 0; //маскировка от проверок других ботов.

    int c_red = 150;
    int c_green = 150;
    int c_blue = 150;
    ImVec4 c_family = RandomColor();

    World* world;
public:
    // Функция сериализации класса в поток данных
    friend std::ostream& operator<<(std::ostream& os, const Bot& obj) {

        //x,y
        os << obj.x << " " << obj.y << " ";

        // state
        os << static_cast<int>(obj.state) << " ";

        //mind
        os << MIND_SIZE << " "; // Сначала сохраняем размер массива
        for (int i = 0; i < MIND_SIZE; i++) {
            os << (int)obj.mind[i] << " ";
        }

        float f1, f2, f3;
        f1 = obj.c_family.x;
        f2 = obj.c_family.y;
        f3 = obj.c_family.z;

        //all other
        os << obj.adr << " " << obj.direction << " " << obj.energy << " " << obj.mineral << " ";
        os << obj.age << " " << obj.c_red << " " << obj.c_green << " " << obj.c_blue << " " << f1 << " " << f2 << " " << f3;

        return os;
    };

    // Функция десериализации класса из потока данных
    friend std::istream& operator>>(std::istream& os, Bot& obj) {
        //x,y
        int x, y;
        os >> x >> y;
        obj.x = x;
        obj.y = y;

        // state
        int enumValue;
        os >> enumValue;
        obj.state = static_cast<BotState>(enumValue);

        //mind
        int size;
        os >> size;

        for (int i = 0; i < size; i++) {
            int val;
            os >> val;
            obj.mind[i] = (char)val;
        }

        //all other
        float en, miner;
        int adr, dir, age, c_red, c_green, c_blue;
        float f1, f2, f3;
        os >> adr >> dir >> en >> miner >> age >> c_red >> c_green >> c_blue >> f1 >> f2 >> f3;
        obj.adr = adr;
        obj.direction = dir;
        obj.energy = en;
        obj.mineral = miner;
        obj.age = age;
        obj.c_red = c_red;
        obj.c_green = c_green;
        obj.c_blue = c_blue;
        obj.c_family = ImVec4(f1, f2, f3, 255);

        return os;
    };

    Bot(int xi, int yi, World* wrd) {
        x = xi;
        y = yi;
        world = wrd;
        for (int i = 0; i < MIND_SIZE; i++) {
            mind[i] = RandomInt(0, MIND_SIZE);
        }
        c_family = RandomColor();
    };

    ~Bot() {
        //free(mind);
    };

    void Mutate();

    void deleteBot(Bot* bot);

    bool isRelative(Bot* bot1);

    void Mask();

    int xFromVektorR(int n);

    int yFromVektorR(int n);

    void MoveTo(int xt, int yt);

    int Move();

    int Eat();

    int Give();

    int findEmptyDirection();

    void DoubleSelf();

    int Care();

    int SeeBots();

    int isFullAround();

    void GeneAttack();

    void Photo();

    void MineralGain();

    void BotToOrganic();

    int checkEnergy();

    int checkMineral();

    int checkLevel();

    int checkAge();

    int GetParam();

    void Rotate();

    void IncAdr(int amount);

    void JmpAdr(int a);

    float GetEnergy();

    int GetX();

    int GetY();

    BotState GetState();

    int Red();

    int Green();

    int Blue();

    ImVec4 GetFamily();

    int GetAge();

    void SetEnergy(float a);

    void SaveToFile(string FileName);

    void goGreen(int num);

    void goBlue(int num);

    void goRed(int num);

    void step();

};