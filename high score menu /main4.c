#include "leaderboard.h"

int main(int argc, char* argv[]){
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    IMG_Init(IMG_INIT_PNG);
    if(TTF_Init()!=0){ printf("TTF_Init error: %s\n", TTF_GetError()); return 1; }

    if(Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0){
        printf("SDL_mixer error: %s\n", Mix_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Leaderboard",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 600, SDL_WINDOW_SHOWN);
    SDL_Renderer* renderer = SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED);

    SDL_Surface* bgSurf = IMG_Load("background3.png");
    if(!bgSurf){ printf("Cannot load background3.png\n"); return 1; }
    SDL_Texture* bgTex = SDL_CreateTextureFromSurface(renderer,bgSurf);
    SDL_FreeSurface(bgSurf);

    SDL_Surface* lbBgSurf = IMG_Load("background4.png");
    if(!lbBgSurf){ printf("Cannot load background4.png\n"); return 1; }
    SDL_Texture* lbBgTex = SDL_CreateTextureFromSurface(renderer,lbBgSurf);
    SDL_FreeSurface(lbBgSurf);

    TTF_Font* font = TTF_OpenFont("arial.ttf",24);
    if(!font){ printf("Cannot load font\n"); return 1; }
    TTF_Font* smallFont = TTF_OpenFont("arial.ttf",18);
    if(!smallFont){ printf("Cannot load small font\n"); return 1; }


    Mix_Music* victoryMusic = Mix_LoadMUS("music.mp3");
    if(!victoryMusic){
        printf("Cannot load music.mp3: %s\n", Mix_GetError());
    }

    Mix_Chunk* hoverSound = Mix_LoadWAV("hover.wav");
    if(!hoverSound){
        printf("Cannot load hover.wav: %s\n", Mix_GetError());
    }

    SDL_StartTextInput();
    SDL_Event e;
    int running = 1;
    int screenInput = 1;

    char name[20]="";
    Player allPlayers[100];
    int playerCount = 0;

    char searchResult[100] = "";
    int playerFound = 0;
    int playerRank = -1;
    int playerScore = 0;

    SDL_Rect valider    = {350, 300, 100, 50};
    SDL_Rect retourBtn  = {200, 450, 150, 50};
    SDL_Rect quitterBtn = {450, 450, 150, 50};
    SDL_Rect homeBtn    = {300, 400, 250, 50};

    int retourHovered  = 0;
    int quitterHovered = 0;
    int validerHovered = 0;
    int homeHovered    = 0;

    SDL_Texture* nameTex         = NULL;
    SDL_Texture* searchResultTex = NULL;
    SDL_Texture* lbTex[3]        = {NULL, NULL, NULL};
    SDL_Texture* lbScoreTex[3]   = {NULL, NULL, NULL};
    SDL_Texture* promptTex       = NULL;

    SDL_Surface* s = TTF_RenderText_Solid(font, "Saisir nom du joueur:", (SDL_Color){255,255,255,255});
    promptTex = SDL_CreateTextureFromSurface(renderer,s);
    SDL_FreeSurface(s);

    while(running){
        int changed = 0;

        while(SDL_PollEvent(&e)){
            if(e.type==SDL_QUIT) running=0;

            else if(e.type==SDL_MOUSEMOTION){
                int mx = e.motion.x, my = e.motion.y;

                if(screenInput){
                    int wasHoveredValider = validerHovered;
                    validerHovered = (mx>=valider.x && mx<=valider.x+valider.w &&
                                      my>=valider.y && my<=valider.y+valider.h);
                    if(hoverSound && validerHovered && !wasHoveredValider)
                        Mix_PlayChannel(-1, hoverSound, 0);

                    int wasHoveredHome = homeHovered;
                    homeHovered = (mx>=homeBtn.x && mx<=homeBtn.x+homeBtn.w &&
                                   my>=homeBtn.y && my<=homeBtn.y+homeBtn.h);
                    if(hoverSound && homeHovered && !wasHoveredHome)
                        Mix_PlayChannel(-1, hoverSound, 0);

                } else {
                    int wasHoveredRetour  = retourHovered;
                    int wasHoveredQuitter = quitterHovered;

                    retourHovered = (mx>=retourBtn.x && mx<=retourBtn.x+retourBtn.w &&
                                     my>=retourBtn.y && my<=retourBtn.y+retourBtn.h);
                    quitterHovered = (mx>=quitterBtn.x && mx<=quitterBtn.x+quitterBtn.w &&
                                      my>=quitterBtn.y && my<=quitterBtn.y+quitterBtn.h);

                    if(hoverSound && retourHovered && !wasHoveredRetour)
                        Mix_PlayChannel(-1, hoverSound, 0);
                    if(hoverSound && quitterHovered && !wasHoveredQuitter)
                        Mix_PlayChannel(-1, hoverSound, 0);
                }
            }

            else if(e.type==SDL_KEYDOWN){

                if(e.key.keysym.sym==SDLK_ESCAPE) running=0;

                else if(e.key.keysym.sym==SDLK_BACKSPACE && screenInput){
                    int l = strlen(name);
                    if(l>0){ name[l-1]='\0'; changed=1;
                        searchResult[0]='\0'; playerFound=0;
                        if(searchResultTex){ SDL_DestroyTexture(searchResultTex); searchResultTex=NULL; }
                    }
                }

                else if(e.key.keysym.sym==SDLK_RETURN && screenInput && strlen(name)>0){

                    FILE* file = fopen("test.txt", "a");
                    if(file){
                        fprintf(file, "%s 0\n", name);
                        fclose(file);
                    }

                    playerCount = loadPlayers(allPlayers);
                    sortPlayers(allPlayers, playerCount);

                    playerRank = findPlayer(allPlayers, playerCount, name, &playerScore);
                    if(playerRank >= 0){
                        if(playerRank < 3)
                            sprintf(searchResult, "%s - Rank #%d - In Top 3!", name, playerRank+1);
                        else
                            sprintf(searchResult, "%s - Rank #%d", name, playerRank+1);
                        playerFound = 1;
                    } else {
                        sprintf(searchResult, "%s saved!", name);
                        playerFound = 1;
                    }

                    if(searchResultTex) SDL_DestroyTexture(searchResultTex);
                    SDL_Color color = {0,255,0,255};
                    s = TTF_RenderText_Solid(font, searchResult, color);
                    searchResultTex = SDL_CreateTextureFromSurface(renderer,s);
                    SDL_FreeSurface(s);

                    screenInput = 0;

                    Mix_HaltMusic();
                    if(victoryMusic) Mix_PlayMusic(victoryMusic, -1);

                    for(int i=0;i<3 && i<playerCount;i++){
                        if(lbTex[i]) SDL_DestroyTexture(lbTex[i]);
                        if(lbScoreTex[i]) SDL_DestroyTexture(lbScoreTex[i]);

                        s = TTF_RenderText_Solid(font, allPlayers[i].name, (SDL_Color){0,0,50,255});
                        lbTex[i] = SDL_CreateTextureFromSurface(renderer,s);
                        SDL_FreeSurface(s);

                        char scoreText[20];
                        sprintf(scoreText, "%d", allPlayers[i].score);
                        s = TTF_RenderText_Solid(smallFont, scoreText, (SDL_Color){20,20,60,255});
                        lbScoreTex[i] = SDL_CreateTextureFromSurface(renderer,s);
                        SDL_FreeSurface(s);
                    }
                }

                else if(e.key.keysym.sym==SDLK_r && !screenInput){
                    screenInput = 1;
                    name[0]='\0'; searchResult[0]='\0'; playerFound=0;

                    Mix_HaltMusic();
                    if(nameTex){ SDL_DestroyTexture(nameTex); nameTex=NULL; }
                    if(searchResultTex){ SDL_DestroyTexture(searchResultTex); searchResultTex=NULL; }
                    for(int i=0;i<3;i++){
                        if(lbTex[i]){ SDL_DestroyTexture(lbTex[i]); lbTex[i]=NULL; }
                        if(lbScoreTex[i]){ SDL_DestroyTexture(lbScoreTex[i]); lbScoreTex[i]=NULL; }
                    }
                }

                else if(e.key.keysym.sym==SDLK_e){
                    printf("Switching to sous menu enigme...\n");
                }
            }

            else if(e.type==SDL_TEXTINPUT && screenInput){
                if(strlen(name)<19){
                    strcat(name, e.text.text); changed=1;
                    searchResult[0]='\0'; playerFound=0;
                    if(searchResultTex){ SDL_DestroyTexture(searchResultTex); searchResultTex=NULL; }
                }
            }

            else if(e.type==SDL_MOUSEBUTTONDOWN && e.button.button==SDL_BUTTON_LEFT){
                int mx=e.button.x, my=e.button.y;

                if(screenInput && strlen(name)>0 &&
                   mx>=valider.x && mx<=valider.x+valider.w &&
                   my>=valider.y && my<=valider.y+valider.h){

                    FILE* file = fopen("test.txt", "a");
                    if(file){
                        fprintf(file, "%s 0\n", name);
                        fclose(file);
                    }

                    playerCount = loadPlayers(allPlayers);
                    sortPlayers(allPlayers, playerCount);

                    playerRank = findPlayer(allPlayers, playerCount, name, &playerScore);
                    if(playerRank >= 0){
                        if(playerRank < 3)
                            sprintf(searchResult, "%s - Rank #%d - In Top 3!", name, playerRank+1);
                        else
                            sprintf(searchResult, "%s - Rank #%d", name, playerRank+1);
                        playerFound = 1;
                    } else {
                        sprintf(searchResult, "%s saved!", name);
                        playerFound = 1;
                    }

                    if(searchResultTex) SDL_DestroyTexture(searchResultTex);
                    SDL_Color color = {0,255,0,255};
                    s = TTF_RenderText_Solid(font, searchResult, color);
                    searchResultTex = SDL_CreateTextureFromSurface(renderer,s);
                    SDL_FreeSurface(s);

                    screenInput = 0;

                    Mix_HaltMusic();
                    if(victoryMusic) Mix_PlayMusic(victoryMusic, -1);

                    for(int i=0;i<3 && i<playerCount;i++){
                        if(lbTex[i]) SDL_DestroyTexture(lbTex[i]);
                        if(lbScoreTex[i]) SDL_DestroyTexture(lbScoreTex[i]);

                        s = TTF_RenderText_Solid(font, allPlayers[i].name, (SDL_Color){0,0,50,255});
                        lbTex[i] = SDL_CreateTextureFromSurface(renderer,s);
                        SDL_FreeSurface(s);

                        char scoreText[20];
                        sprintf(scoreText, "%d", allPlayers[i].score);
                        s = TTF_RenderText_Solid(smallFont, scoreText, (SDL_Color){20,20,60,255});
                        lbScoreTex[i] = SDL_CreateTextureFromSurface(renderer,s);
                        SDL_FreeSurface(s);
                    }
                }

                if(screenInput &&
                   mx>=homeBtn.x && mx<=homeBtn.x+homeBtn.w &&
                   my>=homeBtn.y && my<=homeBtn.y+homeBtn.h){
                    running = 0;
                }

                if(!screenInput &&
                   mx>=retourBtn.x && mx<=retourBtn.x+retourBtn.w &&
                   my>=retourBtn.y && my<=retourBtn.y+retourBtn.h){
                    screenInput=1;
                    name[0]='\0'; searchResult[0]='\0'; playerFound=0;
                    Mix_HaltMusic();
                    if(nameTex){ SDL_DestroyTexture(nameTex); nameTex=NULL; }
                    if(searchResultTex){ SDL_DestroyTexture(searchResultTex); searchResultTex=NULL; }
                    for(int i=0;i<3;i++){
                        if(lbTex[i]){ SDL_DestroyTexture(lbTex[i]); lbTex[i]=NULL; }
                        if(lbScoreTex[i]){ SDL_DestroyTexture(lbScoreTex[i]); lbScoreTex[i]=NULL; }
                    }
                }

                if(!screenInput &&
                   mx>=quitterBtn.x && mx<=quitterBtn.x+quitterBtn.w &&
                   my>=quitterBtn.y && my<=quitterBtn.y+quitterBtn.h){
                    running=0;
                }
            }
        }

        if(changed && screenInput){
            if(nameTex) SDL_DestroyTexture(nameTex);
            if(strlen(name)>0){
                s = TTF_RenderText_Solid(font, name, (SDL_Color){0,0,0,255});
                nameTex = SDL_CreateTextureFromSurface(renderer,s);
                SDL_FreeSurface(s);
            } else {
                nameTex=NULL;
            }
        }

        SDL_SetRenderDrawColor(renderer,0,0,0,255);
        SDL_RenderClear(renderer);

        if(screenInput){
            SDL_RenderCopy(renderer,bgTex,NULL,NULL);
        } else {
            SDL_RenderCopy(renderer,lbBgTex,NULL,NULL);
        }

        if(screenInput){

            if(promptTex){
                SDL_Rect pr={250,150,0,0};
                SDL_QueryTexture(promptTex,NULL,NULL,&pr.w,&pr.h);
                SDL_RenderCopy(renderer,promptTex,NULL,&pr);
            }

            SDL_Rect box={250,200,300,50};
            SDL_SetRenderDrawColor(renderer,255,255,255,255);
            SDL_RenderFillRect(renderer,&box);
            SDL_SetRenderDrawColor(renderer,0,0,0,255);
            SDL_RenderDrawRect(renderer,&box);

            if(nameTex){
                SDL_Rect tr={box.x+5,box.y+10,0,0};
                SDL_QueryTexture(nameTex,NULL,NULL,&tr.w,&tr.h);
                SDL_RenderCopy(renderer,nameTex,NULL,&tr);
            }

            if(validerHovered)
                SDL_SetRenderDrawColor(renderer,0,180,255,255);
            else
                SDL_SetRenderDrawColor(renderer,0,128,255,255);
            SDL_RenderFillRect(renderer,&valider);

            s = TTF_RenderText_Solid(font, "Valider", (SDL_Color){255,255,255,255});
            SDL_Texture* btnTex = SDL_CreateTextureFromSurface(renderer,s);
            SDL_FreeSurface(s);
            SDL_Rect btnTextRect={valider.x+10,valider.y+10,0,0};
            SDL_QueryTexture(btnTex,NULL,NULL,&btnTextRect.w,&btnTextRect.h);
            SDL_RenderCopy(renderer,btnTex,NULL,&btnTextRect);
            SDL_DestroyTexture(btnTex);

            if(homeHovered)
                SDL_SetRenderDrawColor(renderer,180,100,0,255);
            else
                SDL_SetRenderDrawColor(renderer,150,75,0,255);
            SDL_RenderFillRect(renderer,&homeBtn);

            s = TTF_RenderText_Solid(font,"Retour Menu Principal",(SDL_Color){255,255,255,255});
            SDL_Texture* homeTex = SDL_CreateTextureFromSurface(renderer,s);
            SDL_FreeSurface(s);
            SDL_Rect homeTextRect={homeBtn.x+10,homeBtn.y+10,0,0};
            SDL_QueryTexture(homeTex,NULL,NULL,&homeTextRect.w,&homeTextRect.h);
            SDL_RenderCopy(renderer,homeTex,NULL,&homeTextRect);
            SDL_DestroyTexture(homeTex);

        } else {

            if(searchResultTex){
                SDL_Rect sr={150,50,0,0};
                SDL_QueryTexture(searchResultTex,NULL,NULL,&sr.w,&sr.h);
                SDL_RenderCopy(renderer,searchResultTex,NULL,&sr);
            }

            if(lbTex[0] && playerCount>0){
                SDL_Rect nameRect={0,240,0,0};
                SDL_QueryTexture(lbTex[0],NULL,NULL,&nameRect.w,&nameRect.h);
                nameRect.x = 400 - nameRect.w/2;
                SDL_RenderCopy(renderer,lbTex[0],NULL,&nameRect);

                if(lbScoreTex[0]){
                    SDL_Rect scoreRect={0,275,0,0};
                    SDL_QueryTexture(lbScoreTex[0],NULL,NULL,&scoreRect.w,&scoreRect.h);
                    scoreRect.x = 400 - scoreRect.w/2;
                    SDL_RenderCopy(renderer,lbScoreTex[0],NULL,&scoreRect);
                }
            }

            if(lbTex[1] && playerCount>1){
                SDL_Rect nameRect={0,295,0,0};
                SDL_QueryTexture(lbTex[1],NULL,NULL,&nameRect.w,&nameRect.h);
                nameRect.x = 270 - nameRect.w/2;
                SDL_RenderCopy(renderer,lbTex[1],NULL,&nameRect);

                if(lbScoreTex[1]){
                    SDL_Rect scoreRect={0,330,0,0};
                    SDL_QueryTexture(lbScoreTex[1],NULL,NULL,&scoreRect.w,&scoreRect.h);
                    scoreRect.x = 270 - scoreRect.w/2;
                    SDL_RenderCopy(renderer,lbScoreTex[1],NULL,&scoreRect);
                }
            }

            if(lbTex[2] && playerCount>2){
                SDL_Rect nameRect={0,325,0,0};
                SDL_QueryTexture(lbTex[2],NULL,NULL,&nameRect.w,&nameRect.h);
                nameRect.x = 510 - nameRect.w/2;
                SDL_RenderCopy(renderer,lbTex[2],NULL,&nameRect);

                if(lbScoreTex[2]){
                    SDL_Rect scoreRect={0,360,0,0};
                    SDL_QueryTexture(lbScoreTex[2],NULL,NULL,&scoreRect.w,&scoreRect.h);
                    scoreRect.x = 510 - scoreRect.w/2;
                    SDL_RenderCopy(renderer,lbScoreTex[2],NULL,&scoreRect);
                }
            }

            if(retourHovered)
                SDL_SetRenderDrawColor(renderer,0,255,0,255);
            else
                SDL_SetRenderDrawColor(renderer,0,200,0,255);
            SDL_RenderFillRect(renderer,&retourBtn);

            s = TTF_RenderText_Solid(font,"Retour",(SDL_Color){255,255,255,255});
            SDL_Texture* retourTex = SDL_CreateTextureFromSurface(renderer,s);
            SDL_FreeSurface(s);
            SDL_Rect retourTextRect={retourBtn.x+30,retourBtn.y+10,0,0};
            SDL_QueryTexture(retourTex,NULL,NULL,&retourTextRect.w,&retourTextRect.h);
            SDL_RenderCopy(renderer,retourTex,NULL,&retourTextRect);
            SDL_DestroyTexture(retourTex);

            if(quitterHovered)
                SDL_SetRenderDrawColor(renderer,255,0,0,255);
            else
                SDL_SetRenderDrawColor(renderer,200,0,0,255);
            SDL_RenderFillRect(renderer,&quitterBtn);

            s = TTF_RenderText_Solid(font,"Quitter",(SDL_Color){255,255,255,255});
            SDL_Texture* quitterTex = SDL_CreateTextureFromSurface(renderer,s);
            SDL_FreeSurface(s);
            SDL_Rect quitterTextRect={quitterBtn.x+25,quitterBtn.y+10,0,0};
            SDL_QueryTexture(quitterTex,NULL,NULL,&quitterTextRect.w,&quitterTextRect.h);
            SDL_RenderCopy(renderer,quitterTex,NULL,&quitterTextRect);
            SDL_DestroyTexture(quitterTex);
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    if(nameTex) SDL_DestroyTexture(nameTex);
    if(searchResultTex) SDL_DestroyTexture(searchResultTex);
    if(promptTex) SDL_DestroyTexture(promptTex);
    for(int i=0;i<3;i++){
        if(lbTex[i]) SDL_DestroyTexture(lbTex[i]);
        if(lbScoreTex[i]) SDL_DestroyTexture(lbScoreTex[i]);
    }
    if(hoverSound) Mix_FreeChunk(hoverSound);
    if(victoryMusic) Mix_FreeMusic(victoryMusic);
    Mix_CloseAudio();
    TTF_CloseFont(font);
    TTF_CloseFont(smallFont);
    SDL_DestroyTexture(bgTex);
    SDL_DestroyTexture(lbBgTex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    return 0;
}
