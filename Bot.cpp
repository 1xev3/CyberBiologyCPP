#include "Bot.h"
#include "World.h"


void Bot::Mutate() {
    this->mind[RandomInt(0, MIND_SIZE)] = RandomInt(0, MIND_SIZE); // ��������� �������(��������� � ������ ��������)
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
    int dif = 0;    // ������� ������������ � ������
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

//���������� �� �������� ������ �����
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

//����������� �����������
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


    if ((yt < 0) || (yt >= world->height)) {  // ���� ��� ... �����
        return 3;                       // �� ���������� 3
    }

    if (world->matrix[yt][xt] == nullptr) {  // ���� ������ ���� ������,
        MoveTo(xt, yt);    // �� ���������� ����
        return 2; // � ������� ���������� 2
    }
    if (world->matrix[yt][xt]->state == BotState::LV_ORGANIC) { // ���� �� ������ ��������� ��������
        return 4;                       // �� ���������� 4
    }

    if (isRelative(world->matrix[yt][xt])) {  // ���� �� ������ �����
        return 6;                       // �� ���������� 6
    }
    return 5; // �� ������ �����-�� ���
}

int Bot::Eat() {
    int direction = GetParam() % 8;
    int xt = xFromVektorR(direction);
    int yt = yFromVektorR(direction);

    //���� ���� ����������, �� ������� ������ � ��� ����
    if (mask > 0) {
        energy -= EAT_COST / 2.0f;
    }
    else {
        energy -= EAT_COST;
    }

    if ((yt < 0) || (yt >= world->height)) {  // ���� ��� ����� ���������� 3
        return 3;
    }
    if (world->matrix[yt][xt] == nullptr) {  // ���� ������ ������ ���������� 2
        return 2;
    }
    else if (world->matrix[yt][xt]->state == BotState::LV_ORGANIC) {   // ���� ��� ��������� ��������
        float energy2add = (world->matrix[yt][xt]->energy);
        deleteBot(world->matrix[yt][xt]);                        // �� ������� � �� �������
        energy += energy2add;          //�������� ����������� �� 100
        goRed(50);               // ��� ���������
        return 4;                       // ���������� 4
    }


    //����� �� ����, ������� ����� ���.
    Bot* target = world->matrix[yt][xt];

    energy += target->energy;
    mineral += target->mineral;
    goRed(50);
    deleteBot(target);


    //���� � �������� ���� ������ �������, ��� �������� � ���� �� ��� ������� ���.
    //if (energy > (target->energy / 2.0f)) {
    //    //���� � ���� ���� ����������, ��
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

//������ ����� ����� �������
int Bot::Give() {
    int direction = GetParam() % 8;
    int xt = xFromVektorR(direction);
    int yt = yFromVektorR(direction);

    if (yt < 0 || yt >= world->height) {  // ���� ��� ����� ���������� 3
        return 3;
    }
    else if (world->matrix[yt][xt] == nullptr) {  // ���� ������ ������ ���������� 2
        return 2;
    }
    else if (world->matrix[yt][xt]->mask > 0) { // ����������. ������ ��� ����� �������, ��� �� ������ ������ ����.
        return 2;
    }
    else if (world->matrix[yt][xt]->state == BotState::LV_ORGANIC) { // ���� �������� ���������� 4
        return 4;
    }


    //------- ���� �� �����, �� � ������ ����������� ����� ----------
    Bot* target = world->matrix[yt][xt];
    float hlt0 = energy;              // ��� ������ �������� ����� �������
    float hlt = hlt0 / 4;
    energy = hlt0 - hlt;
    target->energy = target->energy + hlt;

    float min0 = mineral;             // ��� ������ �������� ����� ���������
    if (min0 > 3) {                 // ������ ���� �� � ���� �� ������ 4
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
    return 8;       // ��������� ���
}

void Bot::DoubleSelf() {
    energy -= DOUBLE_COST; // ��������� �������.
    if (energy <= 0) return;

    int n = findEmptyDirection();
    if (n == 8) { // ���� ��� �������, �� �� � ����� ��������
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

    //���������� ��� �����
    if (RandomFloat(0, 1) < MUTATION_CHANCE) {
        newbot->Mutate();
    }

    world->matrix[yt][xt] = newbot;
}

int Bot::Care() {
    int direction = GetParam() % 8;
    int xt = xFromVektorR(direction);
    int yt = yFromVektorR(direction);

    if (yt < 0 || yt >= world->height) {  // ���� ��� ����� ���������� 3
        return 3;
    }
    else if (world->matrix[yt][xt] == nullptr) {  // ���� ������ ������ ���������� 2
        return 2;
    }
    else if (world->matrix[yt][xt]->mask > 0) { // ����������. ������ ��� ����� �������, ��� �� ������ ������ ����.
        return 2;
    }
    else if (world->matrix[yt][xt]->state == BotState::LV_ORGANIC) { // ���� �������� ���������� 4
        return 4;
    }

    //------- ���� �� �����, �� � ������ ����������� ����� ----------
    Bot* target = world->matrix[yt][xt];
    float hlt0 = energy;                  // ��������� ���������� ������� � ���������
    float hlt1 = target->energy;  // � ���� � ��� ������
    float min0 = mineral;
    float min1 = target->mineral;
    if (hlt0 > hlt1) {                  // ���� � ���� ������ �������, ��� � ������
        float hlt = (hlt0 - hlt1) / 2;    // �� ������������ ������� �������
        energy = energy - hlt;
        target->energy = target->energy + hlt;
    }
    if (min0 > min1) {                  // ���� � ���� ������ ���������, ��� � ������
        float min = (min0 - min1) / 2;    // �� ������������ �� �������
        mineral = mineral - min;
        target->mineral = target->mineral + min;
    }
    return 5;
}

int Bot::SeeBots() { // �� ����� ������ �� ����, ����������� � ������(������������� ��� ���������� �����������)
    // �� ������  ����� - 2  ����� - 3  ������� - 4  ��� - 5  ����� - 6
    int direction = GetParam() % 8;
    int xt = xFromVektorR(direction);                   // ��������, ���� �� ��� � ����  ����������� (�������������)
    int yt = yFromVektorR(direction);

    if (yt < 0 || yt >= world->height) {              // ���� ��� ����� ���������� 3
        return 3;
    }
    else if (world->matrix[yt][xt] == nullptr) {       // ���� ������ ������ ���������� 2
        return 2;
    }
    else if (world->matrix[yt][xt]->mask > 0) { // ����������. ������ ��� ����� �������, ��� �� ������ ������ ����.
        return 2;
    }
    else if (world->matrix[yt][xt]->state == BotState::LV_ORGANIC) { // ���� ��������
        return 4;
    }
    else if (isRelative(world->matrix[yt][xt])) {   // ���� �����, �� ���������� 5
        return 6;
    }
    else {                                                    // ���� �����-�� ���, �� ���������� 4
        return 5;
    }
}

int Bot::isFullAround() {
    int xt, yt;
    if ((y > 0) && (y < world->height - 1) && (x > 0) && (x < world->width - 1)) {
        if (world->matrix[(int)(y - 1)][(int)(x - 1)] == nullptr) return 2;    // ��� ��� ���� �����������, � ������ ����� ��� �����(((
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

    // ��������������� ������, ��� ����, ��� ������ �������.
    energy += (inverted_y * energy_per_level) / 6; 
    goGreen(5);
}

void Bot::MineralGain() {
    float energy_per_level = PHOTO_ENERGY;

    // ��������������� �������, ��� ������, ��� ������ ���������
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

// ���������� �����, ��������� �� ����������� ��������
int Bot::GetParam() {
    return mind[(adr + 1) % MIND_SIZE];
}

//������� (8 ������)
void Bot::Rotate() {
    direction = (direction + GetParam()) % 8;
}

//���������� �������
void Bot::IncAdr(int amount) {
    adr = (adr + amount) % MIND_SIZE;
}

//��������� ���������� �������
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


void Bot::goGreen(int num) {  // ��������� ������
    c_green = c_green + num;
    if (c_green > 255) c_green = 255;
    num = num / 2;
    // �������� ��������
    c_red = c_red - num;
    if (c_red < 0) c_red = 0;
    // �������� ������
    c_blue = c_blue - num;
    if (c_blue < 0) c_blue = 0;
}
void Bot::goBlue(int num) {
    // ��������� ������
    c_blue = c_blue + num;
    if (c_blue > 255) c_blue = 255;
    num = num / 2;
    // �������� ������
    c_green = c_green - num;
    if (c_green < 0) c_green = 0;
    // �������� ��������
    c_red = c_red - num;
    if (c_red < 0) c_red = 0;
}
void Bot::goRed(int num) {  // ��������� ��������
    c_red = c_red + num;
    if (c_red > 255) c_red = 255;
    num = num / 2;
    // �������� ������
    c_green = c_green - num;
    if (c_green < 0) c_green = 0;
    // �������� ������
    c_blue = c_blue - num;
    if (c_blue < 0) c_blue = 0;
}


//��������� ����
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
            //�������
        case 0: {
            Mutate();
            IncAdr(1);
            //breakflag = 1;
            break;
        }
                //����������� ������� � ����������
        case 8:
            JmpAdr(GetParam());
            break;
            // �����������
            // �����������
        case 16: {
            DoubleSelf();
            IncAdr(1);
            breakflag = 1;
            break;
        }
                //��������� � ����������
        case 23: {
            Rotate();
            IncAdr(2);
            break;
        }
                //��� � ����������
        case 26: {
            JmpAdr(Move());
            breakflag = 1;
            break;
        }
                //����������
        case 32: {
            Photo();
            IncAdr(1);
            breakflag = 1;
            break;
        }
                //��������� ���������
        case 33: {
            MineralGain();
            IncAdr(1);
            breakflag = 1;
            break;
        }
                //������ � ������������� �����������
        case 34: {
            IncAdr(Eat());
            breakflag = 1;
            break;
        }
                //������ �������
        case 36:
        case 37: {
            IncAdr(Give());
            breakflag = 1;
            break;
        }
                //������������ ������� � ������������� �����������:
        case 38:
        case 39: {
            IncAdr(Care());
            breakflag = 1;
            break;
        }
                //���������� � ����������
        case 40: {
            IncAdr(SeeBots());
            break;
        }
                //�������� ������
        case 41: {
            IncAdr(checkLevel());
            break;
        }
                //�������� ������ �������
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
                //������ �� ���?
        case 46: {
            IncAdr(isFullAround());
            break;
        }
                //������������ �����.
        case 52: {
            GeneAttack();
            IncAdr(2);
            breakflag = 1;
            break;
        }

                //���� �� ����� �������� �� �������, �� ��� ����������� �������
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
            //DoubleSelf(); // ������ ����� ������ ��� ��������
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