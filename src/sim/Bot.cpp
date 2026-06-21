#include "Bot.h"
#include "World.h"


void Bot::Mutate() {
    this->mind[RandomInt(0, MIND_SIZE)] = RandomInt(0, MIND_SIZE); // Случайная мутация(исправить в случае комманды)
    c_family.x = clamp(c_family.x + RandomFloat(-6.0f, 6.0f), 0.0f, 255.0f);
    c_family.y = clamp(c_family.y + RandomFloat(-6.0f, 6.0f), 0.0f, 255.0f);
    c_family.z = clamp(c_family.z + RandomFloat(-6.0f, 6.0f), 0.0f, 255.0f);
}

void Bot::deleteBot(Bot* bot) {
    if (bot && world->matrix[bot->y][bot->x]) {
        world->matrix[bot->y][bot->x] = nullptr;
        delete bot;
    }
}

bool Bot::isRelative(Bot* bot1) {
    if (mask > 0) {
        return true;
    }
    int dif = 0;    // счетчик несовпадений в геноме
    for (int i = 0; i < MIND_SIZE; i++) {
        if (mind[i] != bot1->mind[i]) {
            dif = dif + 1;
            if (dif > MAX_GENE_DIFFERENCE) {
                return false;
            }
        }
    }
    return true;
}

//Маскировка от проверок других ботов
void Bot::Mask() {
    mask = MASK_CYCLES;
    energy -= MASK_ENERGY_COST;
    goRed(5);
    goGreen(5);
    goBlue(5);
}

int Bot::xFromVektorR(int n) {
    int xt = x;
    n = n + direction;
    if (n >= 8) n = n - 8;
    if (n == 0 || n == 6 || n == 7) {
        xt--;
        if (xt < 0) xt = world->width - 1;
    }
    else if (n >= 2 && n <= 4) {
        xt++;
        if (xt >= world->width) xt = 0;
    }
    return xt;
}

int Bot::yFromVektorR(int n) {
    int yt = y;
    n = n + direction;
    if (n >= 8) n = n - 8;
    if (n <= 2) {
        yt--;
    }
    else if (n >= 4 && n <= 6) {
        yt++;
    }
    return yt;
}

//Безусловное перемещение
void Bot::MoveTo(int xt, int yt) {
    world->matrix[yt][xt] = this;
    world->matrix[y][x] = nullptr;
    x = xt;
    y = yt;
}

int Bot::Move() {
    int direction = GetParam() % 8;
    int xt = xFromVektorR(direction);
    int yt = yFromVektorR(direction);


    if ((yt < 0) || (yt >= world->height)) {  // если там ... стена
        return 3;                       // то возвращаем 3
    }

    if (world->matrix[yt][xt] == nullptr) {  // если клетка была пустая,
        MoveTo(xt, yt);    // то перемещаем бота
        return 2; // и функция возвращает 2
    }
    if (world->matrix[yt][xt]->state == BotState::LV_ORGANIC) { // если на клетке находится органика
        return 4;                       // то возвращаем 4
    }

    if (isRelative(world->matrix[yt][xt])) {  // если на клетке родня
        return 6;                       // то возвращаем 6
    }
    return 5; // на клетке какой-то бот
}

int Bot::Eat() {
    int direction = GetParam() % 8;
    int xt = xFromVektorR(direction);
    int yt = yFromVektorR(direction);

    //Если есть маскировка, то затраты меньше в два раза
    if (mask > 0) {
        energy -= EAT_COST / 2.0f;
    }
    else {
        energy -= EAT_COST;
    }

    if ((yt < 0) || (yt >= world->height)) {  // если там стена возвращаем 3
        return 3;
    }
    if (world->matrix[yt][xt] == nullptr) {  // если клетка пустая возвращаем 2
        return 2;
    }
    else if (world->matrix[yt][xt]->state == BotState::LV_ORGANIC) {   // если там оказалась органика
        float energy2add = (world->matrix[yt][xt]->energy);
        deleteBot(world->matrix[yt][xt]);                        // то удаляем её из списков
        energy += energy2add;          //здоровье увеличилось на 100
        goRed(50);               // бот покраснел
        return 4;                       // возвращаем 4
    }


    //Дошли до сюда, впереди живой бот.
    Bot* target = world->matrix[yt][xt];

    energy += target->energy;
    mineral += target->mineral;
    goRed(50);
    deleteBot(target);


    //Если у текущего бота больше энергии, чем половины у цели то бот сьедает его.
    //if (energy > (target->energy / 2.0f)) {
    //    //Если у цели есть маскировка, то
    //    if ((target->mask > 0)) {
    //        energy = (energy - (target->energy / 2.0f));
    //        target->energy = target->energy / 2.0f - EAT_COST;
    //        target->mineral = target->mineral / 3.0f;
    //        goRed(50);
    //        return 5;
    //    }


    //    energy += target->energy;
    //    mineral += target->mineral;
    //    goRed(200);
    //    deleteBot(target);
    //}
    //else {
    //    energy = energy / 2.0f;
    //}

    return 5;
}

//Отдать часть своей энергии
int Bot::Give() {
    int direction = GetParam() % 8;
    int xt = xFromVektorR(direction);
    int yt = yFromVektorR(direction);

    if (yt < 0 || yt >= world->height) {  // если там стена возвращаем 3
        return 3;
    }
    else if (world->matrix[yt][xt] == nullptr) {  // если клетка пустая возвращаем 2
        return 2;
    }
    else if (world->matrix[yt][xt]->mask > 0) { // маскировка. другой бот будет считать, что на клетке ничего нету.
        return 2;
    }
    else if (world->matrix[yt][xt]->state == BotState::LV_ORGANIC) { // если органика возвращаем 4
        return 4;
    }


    //------- если мы здесь, то в данном направлении живой ----------
    Bot* target = world->matrix[yt][xt];
    float hlt0 = energy;              // бот отдает четверть своей энергии
    float hlt = hlt0 / 4;
    energy = hlt0 - hlt;
    target->energy = target->energy + hlt;

    float min0 = mineral;             // бот отдает четверть своих минералов
    if (min0 > 3) {                 // только если их у него не меньше 4
        float min = min0 / 4;
        mineral = min0 - min;
        target->mineral = target->mineral + min;
        if (target->mineral > 999) {
            target->mineral = 999;
        }
    }
    return 5;
}

int Bot::findEmptyDirection() {
    int xt, yt;
    for (int i = 0; i < 8; i++) {
        xt = xFromVektorR(i);
        yt = yFromVektorR(i);
        if ((yt >= 0) && (yt < world->height)) {
            if (world->matrix[yt][xt] == nullptr) return i;
        }
    }
    return 8;       // свободных нет
}

void Bot::DoubleSelf() {
    energy -= DOUBLE_COST; // потратить энергию.
    if (energy <= 0) return;

    int n = findEmptyDirection();
    if (n == 8) { // если бот окружен, то он в муках погибает
        energy = START_ENERGY;
        return;
    }

    int xt = xFromVektorR(n);
    int yt = yFromVektorR(n);


    Bot* newbot = new Bot(xt, yt, world);
    for (int i = 0; i < MIND_SIZE; i++) {
        newbot->mind[i] = this->mind[i];
    }

    newbot->energy = energy / 2;
    energy /= 2;
    newbot->mineral = mineral / 2;
    mineral /= 2;

    newbot->c_red = c_red;
    newbot->c_green = c_green;
    newbot->c_blue = c_blue;
    newbot->c_family = c_family;

    newbot->direction = direction;// RandomInt(1, 9);

    //мутировать при шансе
    if (RandomFloat(0, 1) < MUTATION_CHANCE) {
        newbot->Mutate();
    }

    world->matrix[yt][xt] = newbot;
}

int Bot::Care() {
    int direction = GetParam() % 8;
    int xt = xFromVektorR(direction);
    int yt = yFromVektorR(direction);

    if (yt < 0 || yt >= world->height) {  // если там стена возвращаем 3
        return 3;
    }
    else if (world->matrix[yt][xt] == nullptr) {  // если клетка пустая возвращаем 2
        return 2;
    }
    else if (world->matrix[yt][xt]->mask > 0) { // маскировка. другой бот будет считать, что на клетке ничего нету.
        return 2;
    }
    else if (world->matrix[yt][xt]->state == BotState::LV_ORGANIC) { // если органика возвращаем 4
        return 4;
    }

    //------- если мы здесь, то в данном направлении живой ----------
    Bot* target = world->matrix[yt][xt];
    float hlt0 = energy;                  // определим количество энергии и минералов
    float hlt1 = target->energy;  // у бота и его соседа
    float min0 = mineral;
    float min1 = target->mineral;
    if (hlt0 > hlt1) {                  // если у бота больше энергии, чем у соседа
        float hlt = (hlt0 - hlt1) / 2;    // то распределяем энергию поровну
        energy = energy - hlt;
        target->energy = target->energy + hlt;
    }
    if (min0 > min1) {                  // если у бота больше минералов, чем у соседа
        float min = (min0 - min1) / 2;    // то распределяем их поровну
        mineral = mineral - min;
        target->mineral = target->mineral + min;
    }
    return 5;
}

int Bot::SeeBots() { // на входе ссылка на бота, направлелие и флажок(относительное или абсолютное направление)
    // на выходе  пусто - 2  стена - 3  органик - 4  бот - 5  родня - 6
    int direction = GetParam() % 8;
    int xt = xFromVektorR(direction);                   // выясняем, есть ли что в этом  направлении (относительном)
    int yt = yFromVektorR(direction);

    if (yt < 0 || yt >= world->height) {              // если там стена возвращаем 3
        return 3;
    }
    else if (world->matrix[yt][xt] == nullptr) {       // если клетка пустая возвращаем 2
        return 2;
    }
    else if (world->matrix[yt][xt]->mask > 0) { // маскировка. другой бот будет считать, что на клетке ничего нету.
        return 2;
    }
    else if (world->matrix[yt][xt]->state == BotState::LV_ORGANIC) { // если органика
        return 4;
    }
    else if (isRelative(world->matrix[yt][xt])) {   // если родня, то возвращаем 5
        return 6;
    }
    else {                                                    // если какой-то бот, то возвращаем 4
        return 5;
    }
}

int Bot::isFullAround() {
    int xt, yt;
    if ((y > 0) && (y < world->height - 1) && (x > 0) && (x < world->width - 1)) {
        if (world->matrix[(int)(y - 1)][(int)(x - 1)] == nullptr) return 2;    // это все ради оптимизации, я плакал когда это писал(((
        if (world->matrix[(int)(y + 1)][(int)(x + 1)] == nullptr) return 2;
        if (world->matrix[(int)(y - 1)][(int)(x + 1)] == nullptr) return 2;
        if (world->matrix[(int)(y + 1)][(int)(x - 1)] == nullptr) return 2;
        if (world->matrix[y][(int)(y - 1)] == nullptr) return 2;
        if (world->matrix[y][(int)(y + 1)] == nullptr) return 2;
        if (world->matrix[(int)(y - 1)][x] == nullptr) return 2;
        if (world->matrix[(int)(y + 1)][x] == nullptr) return 2;
    }
    else {
        for (int i = 0; i < 8; i++) {
            xt = xFromVektorR(i);
            yt = yFromVektorR(i);
            if ((yt >= 0) && (yt < world->height)) {
                if (world->matrix[yt][xt] == nullptr) return 2;
            }
        }
    }
    return 1;
}

void Bot::GeneAttack() {
    int direction = GetParam() % 8;
    int xt = xFromVektorR(direction);
    int yt = yFromVektorR(direction);

    if (yt < 0 || yt >= world->height) {
        return;
    }
    else if (world->matrix[yt][xt] == nullptr) {
        return;
    }
    else if (world->matrix[yt][xt]->state == BotState::LV_ORGANIC) {
        return;
    }

    energy -= GENE_ATTACK_COST;

    Bot* target = world->matrix[yt][xt];
    if (target) {
        if (target->state == BotState::LV_ALIVE) {
            int target_cmd = RandomInt(0, MIND_SIZE);
            target->mind[target_cmd] = this->mind[target_cmd];
        }
    }
}

//Photosynthesis
void Bot::Photo() {
    float energy_per_level = PHOTO_ENERGY;
    int inverted_y = world->height - y;

    // Пропорционально высоте, чем выше, тем больше энергии.
    energy += (inverted_y * energy_per_level) / 6; 
    goGreen(5);
}

void Bot::MineralGain() {
    float energy_per_level = PHOTO_ENERGY;

    // Пропорционально глубине, чем глубже, тем больше минералов
    //mineral += (y * energy_per_level) / 10;

    energy += (y * energy_per_level) / 6;
    goBlue(5);
}

void Bot::BotToOrganic() {
    if (state != BotState::LV_ORGANIC) {
        state = BotState::LV_ORGANIC;
        c_red = 100;
        c_green = 100;
        c_blue = 100;
    }
}

int Bot::checkEnergy() {
    if (energy < MAX_ENERGY * GetParam() / MIND_SIZE) return 2;
    return 3;
}

int Bot::checkMineral() {
    if (mineral < MAX_MINERAL * GetParam() / MIND_SIZE) return 2;
    return 3;
}

int Bot::checkLevel() {
    if (y < world->height * GetParam() / MIND_SIZE) return 2;
    return 3;
}

int Bot::checkAge() {
    if (age < MAX_AGE * GetParam() / MIND_SIZE) return 2;
    return 3;
}

// возвращает число, следующее за выполняемой командой
int Bot::GetParam() {
    return mind[(adr + 1) % MIND_SIZE];
}

//Поворот (8 сторон)
void Bot::Rotate() {
    direction = (direction + GetParam()) % 8;
}

//Увеличение адресса
void Bot::IncAdr(int amount) {
    adr = (adr + amount) % MIND_SIZE;
}

//Косвенное увеличение адресса
void Bot::JmpAdr(int a) {
    IncAdr(mind[(adr + a) % MIND_SIZE]);
}

float Bot::GetEnergy() {
    return energy;
}
int Bot::GetX() {
    return x;
}
int Bot::GetY() {
    return y;
}
BotState Bot::GetState() {
    return state;
}

int Bot::Red() {
    return c_red;
}
int Bot::Green() {
    return c_green;
}
int Bot::Blue() {
    return c_blue;
}
ImVec4 Bot::GetFamily() {
    return c_family;
}
int Bot::GetAge() {
    return age;
}
void Bot::SetEnergy(float a) {
    energy = a;
}
void Bot::SaveToFile(string FileName) {
    ofstream outFile((SAVES_BOTS_FOLDER + ("/" + FileName)));
    outFile << (*this);
    outFile.close();
}


void Bot::goGreen(int num) {  // добавляем зелени
    c_green = c_green + num;
    if (c_green > 255) c_green = 255;
    num = num / 2;
    // убавляем красноту
    c_red = c_red - num;
    if (c_red < 0) c_red = 0;
    // убавляем синеву
    c_blue = c_blue - num;
    if (c_blue < 0) c_blue = 0;
}
void Bot::goBlue(int num) {
    // добавляем синевы
    c_blue = c_blue + num;
    if (c_blue > 255) c_blue = 255;
    num = num / 2;
    // убавляем зелень
    c_green = c_green - num;
    if (c_green < 0) c_green = 0;
    // убавляем красноту
    c_red = c_red - num;
    if (c_red < 0) c_red = 0;
}
void Bot::goRed(int num) {  // добавляем красноты
    c_red = c_red + num;
    if (c_red > 255) c_red = 255;
    num = num / 2;
    // убавляем зелень
    c_green = c_green - num;
    if (c_green < 0) c_green = 0;
    // убавляем синеву
    c_blue = c_blue - num;
    if (c_blue < 0) c_blue = 0;
}


//Жизненный цикл
void Bot::step() {
    if (mask > 0) {
        mask--;
    }


    if (state == BotState::LV_ORGANIC) {
        return;
    }


    int breakflag;
    int command;
    for (int cyc = 0; cyc < 15; cyc++) {
        command = mind[adr];
        breakflag = 0;
        switch (command)
        {
            //мутация
        case 0: {
            Mutate();
            IncAdr(1);
            //breakflag = 1;
            break;
        }
                //Безусловный переход с параметром
        case 8:
            JmpAdr(GetParam());
            break;
            // Размножение
            // Размножение
        case 16: {
            DoubleSelf();
            IncAdr(1);
            breakflag = 1;
            break;
        }
                //Повернуть с параметром
        case 23: {
            Rotate();
            IncAdr(2);
            break;
        }
                //шаг с параметром
        case 26: {
            JmpAdr(Move());
            breakflag = 1;
            break;
        }
                //фотосинтез
        case 32: {
            Photo();
            IncAdr(1);
            breakflag = 1;
            break;
        }
                //Получение минералов
        case 33: {
            MineralGain();
            IncAdr(1);
            breakflag = 1;
            break;
        }
                //Сьесть в относительном направлении
        case 34: {
            IncAdr(Eat());
            breakflag = 1;
            break;
        }
                //Отдать энергию
        case 36:
        case 37: {
            IncAdr(Give());
            breakflag = 1;
            break;
        }
                //распределить энергию в относительном направлении:
        case 38:
        case 39: {
            IncAdr(Care());
            breakflag = 1;
            break;
        }
                //Посмотреть с параметром
        case 40: {
            IncAdr(SeeBots());
            break;
        }
                //Проверка высоты
        case 41: {
            IncAdr(checkLevel());
            break;
        }
                //Проверка уровня энергии
        case 42: {
            IncAdr(checkEnergy());
            break;
        }
        case 43: {
            IncAdr(checkMineral());
            break;
        }
        case 44: {
            IncAdr(checkAge());
            break;
        }
                //Окружён ли бот?
        case 46: {
            IncAdr(isFullAround());
            break;
        }
                //Генетическая атака.
        case 52: {
            GeneAttack();
            IncAdr(2);
            breakflag = 1;
            break;
        }

                //Если ни одной комманды не найдено, то это безусловный переход
        default:
            IncAdr(command);
            break;
        }

        if (breakflag == 1) break;

    }


    if (state == BotState::LV_ALIVE) {

        if (energy >= MAX_ENERGY) {
            energy = MAX_ENERGY;
            //Mutate();
            //energy = 800;
            //DoubleSelf(); // пришло время родить или помереть
        }

        energy -= LIVE_COST;
        age++;

        if (energy <= 0) {
            //BotToOrganic();
            //energy = START_ENERGY;
            World* wrd = world;
            int dx = x;
            int dy = y;

            delete wrd->matrix[dy][dx];
            wrd->matrix[dy][dx] = nullptr;


            return;
        }

        if (age > MAX_AGE) {
            BotToOrganic();
        }

    }
}