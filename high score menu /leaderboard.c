#include "leaderboard.h"

void sortPlayers(Player players[], int count){
    for(int i = 0; i < count - 1; i++){
        for(int j = 0; j < count - i - 1; j++){
            if(players[j].score < players[j+1].score){
                Player temp = players[j];
                players[j] = players[j+1];
                players[j+1] = temp;
            }
        }
    }
}

int loadPlayers(Player players[]){
    FILE* file = fopen("test.txt", "r");
    if(!file){
        printf("Cannot open test.txt\n");
        return 0;
    }
    int count = 0;
    while(count < 100 && fscanf(file, "%s %d", players[count].name, &players[count].score) == 2){
        count++;
    }
    fclose(file);
    return count;
}

int findPlayer(Player players[], int count, char* name, int* outScore){
    for(int i=0; i<count; i++){
        if(strcasecmp(players[i].name, name) == 0){
            *outScore = players[i].score;
            return i;
        }
    }
    return -1;
}
