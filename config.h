#pragma once
void DefaultStyle();

extern const char* SAVES_WORLD_FOLDER;
extern const char* SAVES_BOTS_FOLDER;


extern const float CELL_SIZE;
extern const float CELL_GAP;
extern const int MIND_SIZE;

extern float START_ENERGY;
extern float PHOTO_ENERGY;
extern float DOUBLE_COST;
extern float LIVE_COST;
extern float EAT_COST;
extern float GENE_ATTACK_COST;
extern int MAX_GENE_DIFFERENCE;
extern int MAX_AGE;
extern float MAX_ENERGY;
extern float MAX_MINERAL;

//Masking
extern int MASK_CYCLES;
extern float MASK_ENERGY_COST;

extern float MUTATION_CHANCE;

//int MAX_ENERGY = 2000;
//int COMMAND_SIZE = 64;
//int MAX_REPLICATIONS = 5;
//int INITIAL_ENERGY = 50;
//double MUTATION_CHANCE = 0.03;
//int ENERGY_CONSUME_PER_CYCLE = 12;
//
//int ENERGY_PHOTO = 4;
//
//int ENERGY_REPLICATION_COST = 20;