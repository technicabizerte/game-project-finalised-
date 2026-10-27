#ifndef LEADERBOARD_H
#define LEADERBOARD_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    char name[20];
    int score;
} Player;

void sortPlayers(Player players[], int count);
int loadPlayers(Player players[]);
int findPlayer(Player players[], int count, char* name, int* outScore);

#endif
