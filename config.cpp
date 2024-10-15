#include "config.h"

const char* SAVES_WORLD_FOLDER = "./saves";
const char* SAVES_BOTS_FOLDER = "./bots";

const float CELL_SIZE = 4.0f;
const float CELL_GAP = 0.5f;
const int MIND_SIZE = 64;

float START_ENERGY = 100.0f;
float PHOTO_ENERGY = 0.65f;
float DOUBLE_COST = 200.0f;
float LIVE_COST = 10.0f;
float EAT_COST = 4.0f;
float GENE_ATTACK_COST = 5.0f;
int MAX_GENE_DIFFERENCE = 2;
int MAX_AGE = 500;
float MAX_ENERGY = 1000.0f;
float MAX_MINERAL = 1000.0f;

// Masking
int MASK_CYCLES = 3;
float MASK_ENERGY_COST = 15.0f;

float MUTATION_CHANCE = 0.20f;