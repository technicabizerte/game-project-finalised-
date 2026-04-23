#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "background.h"

#define ETAT_JEU        0
#define ETAT_SCORES     1
#define ETAT_SAISIE     2
#define ETAT_GAMEOVER   3
#define ETAT_SELECTION  4
#define ETAT_SAUVEGARDE 5
#define ETAT_MENU       6
#define ETAT_OPTIONS    7

typedef struct {
    const char *nom;
    int vitesse, force, sante;
    int unlocked;
} CharInfo;

static CharInfo PERSONNAGES[3] = {
    {"Survivant", 6, 5, 7, 1},
    {"A venir",   0, 0, 0, 0},
    {"A venir",   0, 0, 0, 0},
};

int g_avatarTint[NB_JOUEURS_MAX] = {0, 1};

static int keysHeld[323] = {0};
/* one-shot flags set on keydown, consumed each frame */
static int keyShoot  = 0;
static int keyMelee  = 0;
static int keyShoot2 = 0;
static int keyMelee2 = 0;

static void afficherTexte(SDL_Renderer *r, TTF_Font *p, const char *txt,
                           SDL_Color c, int x, int y)
{
    if (!p) return;
    SDL_Surface *s = TTF_RenderUTF8_Blended(p, txt, c);
    if (!s) return;
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
    SDL_Rect d = {x, y, s->w, s->h};
    SDL_RenderCopy(r, t, NULL, &d);
    SDL_DestroyTexture(t);
    SDL_FreeSurface(s);
}

static void afficherTexteCentre(SDL_Renderer *r, TTF_Font *p, const char *txt,
                                SDL_Color c, int y)
{
    if (!p) return;
    SDL_Surface *s = TTF_RenderUTF8_Blended(p, txt, c);
    if (!s) return;
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
    SDL_Rect d = {W_ECRAN/2 - s->w/2, y, s->w, s->h};
    SDL_RenderCopy(r, t, NULL, &d);
    SDL_DestroyTexture(t);
    SDL_FreeSurface(s);
}

static void afficherTexteCentreRect(SDL_Renderer *r, TTF_Font *p,
                                     const char *txt, SDL_Color c,
                                     SDL_Rect rect)
{
    if (!p) return;
    SDL_Surface *s = TTF_RenderUTF8_Blended(p, txt, c);
    if (!s) return;
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
    SDL_Rect dst = {rect.x + (rect.w - s->w)/2,
                    rect.y + (rect.h - s->h)/2,
                    s->w, s->h};
    SDL_RenderCopy(r, t, NULL, &dst);
    SDL_DestroyTexture(t);
    SDL_FreeSurface(s);
}

static void afficherTexteColonne(SDL_Renderer *r, TTF_Font *p, const char *txt,
                                  SDL_Color c, int col_x, int col_w, int y)
{
    if (!p) return;
    SDL_Surface *s = TTF_RenderUTF8_Blended(p, txt, c);
    if (!s) return;
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
    SDL_Rect d = {col_x + col_w/2 - s->w/2, y, s->w, s->h};
    SDL_RenderCopy(r, t, NULL, &d);
    SDL_DestroyTexture(t);
    SDL_FreeSurface(s);
}

/* Draw one menu button with three visual states:
   isNav=1 → orange+yellow (navigation cursor)
   isPicked=1 && !isNav → dark green (current selection)
   both → orange+yellow with a green dot marker */
static void drawMenuBtn(SDL_Renderer *r, TTF_Font *p, const char *label,
                         SDL_Rect btn, int isNav, int isPicked)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    if (isNav) {
        SDL_SetRenderDrawColor(r, 230, 140, 50, 240);
        SDL_RenderFillRect(r, &btn);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r, 255, 215, 0, 255);
        SDL_RenderDrawRect(r, &btn);
        SDL_Rect inner = {btn.x+1, btn.y+1, btn.w-2, btn.h-2};
        SDL_RenderDrawRect(r, &inner);
        if (isPicked) {
            SDL_SetRenderDrawColor(r, 60, 220, 80, 255);
            SDL_Rect dot = {btn.x + btn.w - 10, btn.y + 3, 7, 7};
            SDL_RenderFillRect(r, &dot);
        }
    } else if (isPicked) {
        SDL_SetRenderDrawColor(r, 30, 80, 40, 240);
        SDL_RenderFillRect(r, &btn);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r, 60, 160, 70, 255);
        SDL_RenderDrawRect(r, &btn);
        SDL_Rect inner2 = {btn.x+1, btn.y+1, btn.w-2, btn.h-2};
        SDL_RenderDrawRect(r, &inner2);
    } else {
        SDL_SetRenderDrawColor(r, 30, 40, 70, 220);
        SDL_RenderFillRect(r, &btn);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r, 100, 100, 130, 255);
        SDL_RenderDrawRect(r, &btn);
    }
    SDL_Color tc = isNav ? (SDL_Color){255, 255, 255, 255}
                         : (SDL_Color){200, 210, 230, 255};
    afficherTexteCentreRect(r, p, label, tc, btn);
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init : %s\n", SDL_GetError()); return EXIT_FAILURE;
    }
    if (!(IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG) & IMG_INIT_PNG)) {
        fprintf(stderr, "IMG_Init : %s\n", IMG_GetError()); SDL_Quit(); return EXIT_FAILURE;
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init : %s\n", TTF_GetError()); IMG_Quit(); SDL_Quit(); return EXIT_FAILURE;
    }
    Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);

    SDL_Window   *fenetre  = SDL_CreateWindow("Snowpiercer - Le Jeu",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W_ECRAN, H_ECRAN, 0);
    SDL_Renderer *renderer = SDL_CreateRenderer(fenetre, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(renderer, W_ECRAN, H_ECRAN);

    TTF_Font *police = NULL;
    const char *polices[] = {
        "arial.ttf",
        "/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf",
        "/usr/share/fonts/truetype/freefont/FreeMono.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
        NULL
    };
    for (int i = 0; polices[i] && !police; i++)
        police = TTF_OpenFont(polices[i], 18);

    TTF_Font *policeGrande = NULL;
    for (int i = 0; polices[i] && !policeGrande; i++)
        policeGrande = TTF_OpenFont(polices[i], 48);
    if (!policeGrande) policeGrande = police;

    SDL_Color blanc   = {255, 255, 255, 255};
    SDL_Color jaune   = {255, 215,   0, 255};
    SDL_Color glacial = {140, 200, 255, 255};

    Background bg;
    background_init(&bg, renderer, 1);
    background_chargerImages(&bg, renderer, "platefixe.png", NULL);

    bg.texte.police     = police;
    bg.texte.couleur    = blanc;
    bg.texte.contenu[0] = '\0';

    SDL_Rect rectGuide = {W_ECRAN/2-200, H_ECRAN/2-130, 400, 260};
    fenetreGuide_init(&bg, renderer, "guide.png", rectGuide);

    vues_init(&bg, renderer);
    plateformes_init(&bg, renderer);
    destructibles_init(&bg, renderer);
    background_initialiserSon(&bg, "son.mp3");
    Mix_PlayMusic(bg.son, -1);
    Mix_VolumeMusic(MIX_MAX_VOLUME / 2);

    wagonsInterieurs_init(&bg, renderer);
    neige_init(&bg);
    joueur_init_interieur(&bg);

    Score scores[MAX_SCORES];
    int   nbScores    = scores_charger(FICHIER_SCORES, scores, MAX_SCORES);
    int   scoreJoueur = 0;
    char  nomJoueur[64] = "";

    int etat      = ETAT_MENU;
    int continuer = 1;
    Uint32 lastTick = SDL_GetTicks();
    int finalScore = 0, finalTimeSec = 0, finalWagon = 1;

    static int joueurScreen   = 0;   /* 0 = Screen A (mode), 1 = Screen B (config) */
    static int joueurSelected = 0;   /* highlighted button index */
    static int joueurMode     = 1;   /* 1 = mono, 2 = multi */
    static int joueurAvatarP1 = 0;   /* 0 or 1 */
    static int joueurAvatarP2 = 1;
    static int joueurInputP1  = 0;   /* 0 = AZSD/F/E, 1 = arrows/K/L */
    static int joueurInputP2  = 1;
    static int sauvegardeScreen   = 0;   /* 0 = screen A, 1 = screen B */
    static int sauvegardeSelected = 0;   /* 0 = left btn, 1 = right btn */
    static int menuSelected       = 0;   /* 0..4 highlighted main-menu button */
    static int optionsSelected    = 0;   /* 0..4: 0=Vol-, 1=Vol+, 2=Normal, 3=Fullscreen, 4=Retour */
    static int scoresSelected     = 0;   /* 0 = Retour, 1 = Quitter */
    static int volumeLevel        = MIX_MAX_VOLUME / 2;   /* 0..128 */
    static int fullscreenMode     = 0;   /* 0 = windowed, 1 = fullscreen */

    while (continuer) {

        Uint32 nowTick = SDL_GetTicks();
        float dt = (float)(nowTick - lastTick) / 1000.0f;
        if (dt > 0.05f) dt = 0.05f;
        lastTick = nowTick;

        keyShoot  = 0;
        keyMelee  = 0;
        keyShoot2 = 0;
        keyMelee2 = 0;

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { continuer = 0; }
            else if (ev.type == SDL_KEYDOWN) {
                keysHeld[ev.key.keysym.sym & 0x1FF] = 1;
                if (etat == ETAT_JEU) {
                    if (ev.key.keysym.sym == SDLK_g)
                        bg.fenetreguide.visible = !bg.fenetreguide.visible;
                    if (ev.key.keysym.sym == SDLK_F1) etat = ETAT_SCORES;
                    if (ev.key.keysym.sym == SDLK_n)  etat = ETAT_SAISIE;
                    if (ev.key.keysym.sym == SDLK_f)  keyShoot  = 1;
                    if (ev.key.keysym.sym == SDLK_e)  keyMelee  = 1;
                    if (ev.key.keysym.sym == SDLK_k)  keyShoot2 = 1;
                    if (ev.key.keysym.sym == SDLK_l)  keyMelee2 = 1;
                    if (ev.key.keysym.sym == SDLK_s) {
                        if (sauvegarder_partie(&bg, scoreJoueur, bg.tempsDepart))
                            fprintf(stderr, "[SAVE] Partie sauvegardee\n");
                    }
                }
                if (etat == ETAT_GAMEOVER) {
                    if (ev.key.keysym.sym == SDLK_RETURN) { Mix_PauseMusic(); etat = ETAT_SAISIE; }
                    if (ev.key.keysym.sym == SDLK_ESCAPE) etat = ETAT_SCORES;
                }
                else if (etat == ETAT_SELECTION) {
                    SDL_Keycode k = ev.key.keysym.sym;
                    if (joueurScreen == 0) {
                        /* Screen A: Mono / Multi / Retour */
                        if (k == SDLK_UP || k == SDLK_w)
                            joueurSelected = (joueurSelected + 2) % 3;
                        if (k == SDLK_DOWN || k == SDLK_s)
                            joueurSelected = (joueurSelected + 1) % 3;
                        if (k == SDLK_RETURN) {
                            if (joueurSelected == 0) {
                                joueurMode = 1;
                                joueurScreen = 1;
                                joueurSelected = 0;
                            } else if (joueurSelected == 1) {
                                joueurMode = 2;
                                joueurScreen = 1;
                                joueurSelected = 0;
                            } else {
                                etat = ETAT_MENU;
                                joueurScreen = 0;
                                joueurSelected = 0;
                            }
                        }
                        if (k == SDLK_ESCAPE) {
                            etat = ETAT_MENU;
                            joueurScreen = 0;
                            joueurSelected = 0;
                        }
                    } else {
                        /* Screen B: full 2D navigation */
                        if (k == SDLK_LEFT || k == SDLK_a) {
                            if (joueurSelected == 1) joueurSelected = 0;
                            else if (joueurSelected == 3) joueurSelected = 2;
                            else if (joueurMode==2 && joueurSelected==5) joueurSelected = 4;
                            else if (joueurMode==2 && joueurSelected==7) joueurSelected = 6;
                            else if (joueurSelected == 9) joueurSelected = 8;
                        }
                        if (k == SDLK_RIGHT || k == SDLK_d) {
                            if (joueurSelected == 0) joueurSelected = 1;
                            else if (joueurSelected == 2) joueurSelected = 3;
                            else if (joueurMode==2 && joueurSelected==4) joueurSelected = 5;
                            else if (joueurMode==2 && joueurSelected==6) joueurSelected = 7;
                            else if (joueurSelected == 8) joueurSelected = 9;
                        }
                        if (k == SDLK_DOWN || k == SDLK_s) {
                            if (joueurSelected <= 1)
                                joueurSelected += 2;              /* Avatar P1 → Input P1 */
                            else if (joueurSelected <= 3) {
                                if (joueurMode == 2) joueurSelected += 2; /* Input P1 → Avatar P2 */
                                else joueurSelected = 8;           /* mono → VALIDER */
                            }
                            else if (joueurSelected==4 || joueurSelected==5)
                                joueurSelected += 2;              /* Avatar P2 → Input P2 */
                            else if (joueurSelected==6 || joueurSelected==7)
                                joueurSelected = 8;               /* Input P2 → VALIDER */
                            else if (joueurSelected >= 8)
                                joueurSelected -= 8;              /* bottom → wrap top */
                        }
                        if (k == SDLK_UP || k == SDLK_w) {
                            if (joueurSelected >= 8) {
                                joueurSelected = (joueurMode==2) ? 6 : 2; /* → last input row */
                            }
                            else if (joueurSelected==6 || joueurSelected==7)
                                joueurSelected -= 2;              /* Input P2 → Avatar P2 */
                            else if (joueurSelected==4 || joueurSelected==5)
                                joueurSelected -= 2;              /* Avatar P2 → Input P1 */
                            else if (joueurSelected==2 || joueurSelected==3)
                                joueurSelected -= 2;              /* Input P1 → Avatar P1 */
                            else if (joueurSelected==0 || joueurSelected==1) {
                                if (joueurMode==2) joueurSelected += 6; /* wrap → Input P2 */
                                else joueurSelected = 8;                 /* mono wrap → VALIDER */
                            }
                        }
                        if (k == SDLK_RETURN) {
                            switch (joueurSelected) {
                                case 0: joueurAvatarP1 = 0; break;
                                case 1: joueurAvatarP1 = 1; break;
                                case 2: joueurInputP1  = 0; break;
                                case 3: joueurInputP1  = 1; break;
                                case 4: joueurAvatarP2 = 0; break;
                                case 5: joueurAvatarP2 = 1; break;
                                case 6: joueurInputP2  = 0; break;
                                case 7: joueurInputP2  = 1; break;
                                case 8:
                                    g_avatarTint[0] = joueurAvatarP1;
                                    g_avatarTint[1] = joueurAvatarP2;
                                    bg.nbJoueursActifs = joueurMode;
                                    bg.joueurs[0].hp   = JOUEUR_MAX_HP;
                                    if (joueurMode==2) bg.joueurs[1].hp = JOUEUR_MAX_HP;
                                    joueur_init_interieur(&bg);
                                    scoreJoueur    = 0;
                                    bg.tempsDepart = SDL_GetTicks();
                                    bg.wagonActif  = 0;
                                    joueurScreen   = 0;
                                    joueurSelected = 0;
                                    etat = ETAT_JEU;
                                    break;
                                case 9:
                                    joueurScreen   = 0;
                                    joueurSelected = 0;
                                    break;
                            }
                        }
                        if (k == SDLK_ESCAPE) {
                            joueurScreen   = 0;
                            joueurSelected = 0;
                        }
                    }
                }
                else if (etat == ETAT_SAUVEGARDE) {
                    if (ev.key.keysym.sym == SDLK_LEFT ||
                        ev.key.keysym.sym == SDLK_a)
                        sauvegardeSelected = 0;
                    if (ev.key.keysym.sym == SDLK_RIGHT ||
                        ev.key.keysym.sym == SDLK_d)
                        sauvegardeSelected = 1;
                    if (ev.key.keysym.sym == SDLK_RETURN) {
                        if (sauvegardeScreen == 0) {
                            if (sauvegardeSelected == 0) {
                                /* OUI: go to screen B */
                                sauvegardeScreen   = 1;
                                sauvegardeSelected = 0;
                            } else {
                                /* NON: new game */
                                etat = ETAT_SELECTION;
                            }
                        } else {
                            if (sauvegardeSelected == 0) {
                                /* CHARGER */
                                if (charger_partie(&bg, &scoreJoueur, &bg.tempsDepart)) {
                                    etat = ETAT_JEU;
                                } else {
                                    etat = ETAT_SELECTION;
                                }
                            } else {
                                /* NOUVELLE PARTIE */
                                etat = ETAT_SELECTION;
                            }
                            sauvegardeScreen = 0;
                        }
                    }
                    if (ev.key.keysym.sym == SDLK_ESCAPE) {
                        if (sauvegardeScreen == 1)
                            sauvegardeScreen = 0;
                        else
                            etat = ETAT_MENU;
                    }
                }
                else if (etat == ETAT_MENU) {
                    SDL_Keycode k = ev.key.keysym.sym;

                    /* Arrow navigation with wrap-around */
                    if (k == SDLK_UP || k == SDLK_w)
                        menuSelected = (menuSelected + 4) % 5;
                    if (k == SDLK_DOWN || k == SDLK_s)
                        menuSelected = (menuSelected + 1) % 5;

                    /* Enter triggers currently-selected button */
                    if (k == SDLK_RETURN) {
                        switch (menuSelected) {
                            case 0: etat = ETAT_SAUVEGARDE; break;
                            case 1: etat = ETAT_OPTIONS; break;
                            case 2: etat = ETAT_SCORES; break;
                            case 3: fprintf(stderr, "[MENU] Histoire a venir\n"); break;
                            case 4: continuer = 0; break;
                        }
                    }

                    /* Direct keyboard shortcuts */
                    if (k == SDLK_j) { menuSelected = 0; etat = ETAT_SAUVEGARDE; }
                    if (k == SDLK_o) { menuSelected = 1; etat = ETAT_OPTIONS; }
                    if (k == SDLK_m) { menuSelected = 2; etat = ETAT_SCORES; }
                    if (k == SDLK_h) { menuSelected = 3;
                        fprintf(stderr, "[MENU] Histoire a venir\n"); }
                    if (k == SDLK_ESCAPE) continuer = 0;
                }
                else if (etat == ETAT_OPTIONS) {
                    SDL_Keycode k = ev.key.keysym.sym;

                    /* 2D navigation — Volume row (0,1), Mode row (2,3), Retour (4). */
                    if (k == SDLK_UP || k == SDLK_w) {
                        if (optionsSelected == 4) optionsSelected = 2;
                        else if (optionsSelected >= 2) optionsSelected -= 2;
                    }
                    if (k == SDLK_DOWN || k == SDLK_s) {
                        if (optionsSelected <= 1) optionsSelected += 2;
                        else if (optionsSelected <= 3) optionsSelected = 4;
                    }
                    if (k == SDLK_LEFT || k == SDLK_a) {
                        if (optionsSelected == 1) optionsSelected = 0;
                        else if (optionsSelected == 3) optionsSelected = 2;
                    }
                    if (k == SDLK_RIGHT || k == SDLK_d) {
                        if (optionsSelected == 0) optionsSelected = 1;
                        else if (optionsSelected == 2) optionsSelected = 3;
                    }

                    /* Enter triggers action on selected control */
                    if (k == SDLK_RETURN) {
                        switch (optionsSelected) {
                            case 0:
                                volumeLevel -= 16;
                                if (volumeLevel < 0) volumeLevel = 0;
                                Mix_VolumeMusic(volumeLevel);
                                break;
                            case 1:
                                volumeLevel += 16;
                                if (volumeLevel > MIX_MAX_VOLUME) volumeLevel = MIX_MAX_VOLUME;
                                Mix_VolumeMusic(volumeLevel);
                                break;
                            case 2:
                                fullscreenMode = 0;
                                SDL_SetWindowFullscreen(fenetre, 0);
                                break;
                            case 3:
                                fullscreenMode = 1;
                                SDL_SetWindowFullscreen(fenetre, SDL_WINDOW_FULLSCREEN_DESKTOP);
                                break;
                            case 4:
                                etat = ETAT_MENU;
                                break;
                        }
                    }

                    /* Direct shortcuts per cahier des charges */
                    if (k == SDLK_PLUS || k == SDLK_EQUALS || k == SDLK_KP_PLUS) {
                        volumeLevel += 16;
                        if (volumeLevel > MIX_MAX_VOLUME) volumeLevel = MIX_MAX_VOLUME;
                        Mix_VolumeMusic(volumeLevel);
                        optionsSelected = 1;
                    }
                    if (k == SDLK_MINUS || k == SDLK_KP_MINUS) {
                        volumeLevel -= 16;
                        if (volumeLevel < 0) volumeLevel = 0;
                        Mix_VolumeMusic(volumeLevel);
                        optionsSelected = 0;
                    }
                    if (k == SDLK_f) {
                        fullscreenMode = 1;
                        SDL_SetWindowFullscreen(fenetre, SDL_WINDOW_FULLSCREEN_DESKTOP);
                        optionsSelected = 3;
                    }
                    if (k == SDLK_n) {
                        fullscreenMode = 0;
                        SDL_SetWindowFullscreen(fenetre, 0);
                        optionsSelected = 2;
                    }
                    if (k == SDLK_ESCAPE) etat = ETAT_MENU;
                }
                else if (etat == ETAT_SCORES) {
                    SDL_Keycode k = ev.key.keysym.sym;
                    if (k == SDLK_LEFT  || k == SDLK_a) scoresSelected = 0;
                    if (k == SDLK_RIGHT || k == SDLK_d) scoresSelected = 1;
                    if (k == SDLK_RETURN) {
                        if (scoresSelected == 0) {
                            etat = ETAT_MENU;
                        } else {
                            continuer = 0;
                        }
                    }
                    if (k == SDLK_ESCAPE) {
                        /* Per cahier des charges: ECHAP on leaderboard quits */
                        continuer = 0;
                    }
                    if (k == SDLK_e) {
                        fprintf(stderr, "[SCORES] Enigme submenu a venir "
                                        "(integration avec equipe enigme)\n");
                    }
                }
            }
            else if (ev.type == SDL_KEYUP) {
                keysHeld[ev.key.keysym.sym & 0x1FF] = 0;
            }
        }

        if (etat == ETAT_JEU) {
            int gauche = keysHeld[SDLK_a     & 0x1FF];
            int droite = keysHeld[SDLK_d     & 0x1FF];
            int sauter = keysHeld[SDLK_SPACE & 0x1FF] || keysHeld[SDLK_w & 0x1FF];

            joueur_update_interieur(&bg, gauche, droite, sauter, keyShoot, keyMelee, dt);

            int g2 = keysHeld[SDLK_LEFT  & 0x1FF];
            int d2 = keysHeld[SDLK_RIGHT & 0x1FF];
            int s2 = keysHeld[SDLK_UP    & 0x1FF];
            if (bg.nbJoueursActifs >= 2)
                joueur_update_interieur_idx(&bg, 1, g2, d2, s2, keyShoot2, keyMelee2, dt);

            /* Update combat systems */
            ennemis_update(bg.ennemis, &bg.nbEnnemis, bg.wagonActif, dt,
                           bg.joueurs, bg.nbJoueursActifs,
                           WAGON_SOL_Y, WAGON_PLAF_Y);
            for (int p = 0; p < bg.nbJoueursActifs; p++)
                balles_update(bg.balles[p], &bg.nbBalles[p],
                              bg.ennemis, &bg.nbEnnemis,
                              &bg.joueurs[p].hp, &bg.joueurs[p].etat, &bg.joueurs[p].anim,
                              bg.joueurs[p].x, bg.joueurs[p].y, bg.joueurs[p].w, bg.joueurs[p].h);
            hitfx_update(bg.hitFx, &bg.nbHitFx, dt);

            transitionSlide_update(&bg);
            flash_update(&bg);
            neige_update(&bg);

            /* Parallax scroll — use outside bg width if available */
            int parW = 0;
            if (bg.outsideBgTex)
                SDL_QueryTexture(bg.outsideBgTex, NULL, NULL, &parW, NULL);
            else
                parW = bg.imgW;
            if (parW > 0) {
                bg.parallaxOffset += 0.9f;
                if (bg.parallaxOffset >= (float)parW)
                    bg.parallaxOffset -= (float)parW;
            }

            scoreJoueur = (int)((SDL_GetTicks() - bg.tempsDepart) / 100);

            int tousMorts = 1;
            for (int p = 0; p < bg.nbJoueursActifs; p++)
                if (bg.joueurs[p].hp > 0) { tousMorts = 0; break; }
            if (tousMorts) {
                etat         = ETAT_GAMEOVER;
                finalScore   = scoreJoueur;
                finalTimeSec = (int)((SDL_GetTicks() - bg.tempsDepart) / 1000);
                finalWagon   = bg.wagonActif + 1;
            }

            /* Split-mode hysteresis (2-player only) */
            if (bg.nbJoueursActifs >= 2) {
                float splitDx = fabsf(bg.joueurs[0].x - bg.joueurs[1].x);
                bg.splitCooldown -= dt;
                if (bg.splitCooldown <= 0.0f) {
                    if (bg.splitMode == 0 && splitDx > 400.0f) {
                        bg.splitMode = 1;
                        bg.splitCooldown = 0.3f;
                    } else if (bg.splitMode == 1 && splitDx < 300.0f) {
                        bg.splitMode = 0;
                        bg.splitCooldown = 0.3f;
                    }
                }
            }
        }

        if (etat == ETAT_SAISIE) {
            scores_saisirNom(nomJoueur, sizeof(nomJoueur), renderer, police, jaune);
            scores_ajouterEtTrier(scores, &nbScores, nomJoueur, finalScore,
                                  finalTimeSec, finalWagon);
            scores_sauvegarder(FICHIER_SCORES, scores, nbScores);
            etat = ETAT_SCORES;
            /* TODO: load victory.mp3 and Mix_PlayMusic for celebratory tone per spec */
            Mix_ResumeMusic();
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        if (etat == ETAT_JEU) {
            if (bg.splitMode == 0) {
                wagonsInterieurs_afficher(&bg, renderer);
            } else {
                /* LEFT half — P1 camera */
                SDL_Rect vpL = {0, 0, 399, H_ECRAN};
                SDL_RenderSetViewport(renderer, &vpL);
                int ox_left = 399/2 - (int)(bg.joueurs[0].x + JOUEUR_W/2);
                if (ox_left > -WAGON_MUR_G_X)      ox_left = -WAGON_MUR_G_X;
                if (ox_left < 399 - WAGON_MUR_D_X) ox_left = 399 - WAGON_MUR_D_X;
                wagonsInterieurs_afficher_camera(&bg, renderer, ox_left);

                /* Orange separator */
                SDL_RenderSetViewport(renderer, NULL);
                SDL_SetRenderDrawColor(renderer, 230, 140, 50, 255);
                SDL_Rect sep = {399, 0, 2, H_ECRAN};
                SDL_RenderFillRect(renderer, &sep);

                /* RIGHT half — P2 camera */
                SDL_Rect vpR = {401, 0, 399, H_ECRAN};
                SDL_RenderSetViewport(renderer, &vpR);
                int ox_right = 399/2 - (int)(bg.joueurs[1].x + JOUEUR_W/2);
                if (ox_right > -WAGON_MUR_G_X)      ox_right = -WAGON_MUR_G_X;
                if (ox_right < 399 - WAGON_MUR_D_X) ox_right = 399 - WAGON_MUR_D_X;
                wagonsInterieurs_afficher_camera(&bg, renderer, ox_right);

                /* Reset viewport — HUD, minimap, guide, timer render full-width */
                SDL_RenderSetViewport(renderer, NULL);
            }
            flash_afficher(&bg, renderer);
            background_afficherTemps(&bg, renderer, W_ECRAN-155, 8);
            minimap_afficher(&bg, renderer);
            fenetreGuide_afficher(&bg, renderer);

            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 5, 8, 18, 210);
            SDL_Rect hud = {0, H_ECRAN-45, W_ECRAN, 45};
            SDL_RenderFillRect(renderer, &hud);
            SDL_SetRenderDrawColor(renderer, 100, 180, 255, 200);
            SDL_RenderDrawLine(renderer, 0, H_ECRAN-46, W_ECRAN, H_ECRAN-46);

            char buf[64];
            if (bg.nbJoueursActifs >= 2)
                snprintf(buf, sizeof(buf), "Score : %d   PV1 : %d   PV2 : %d",
                         scoreJoueur, bg.joueurs[0].hp, bg.joueurs[1].hp);
            else
                snprintf(buf, sizeof(buf), "Score : %d   PV : %d", scoreJoueur, bg.joueurs[0].hp);
            afficherTexte(renderer, police, buf, jaune, 15, H_ECRAN-42);

            const char *noms[MAX_WAGONS] = {
                "Wagon Pauvres","Wagon Industrie","Wagon Aquarium","Wagon Jardin","Wagon Luxe"
            };
            char wbuf[64];
            snprintf(wbuf, sizeof(wbuf), "%d/5  %s", bg.wagonActif+1, noms[bg.wagonActif]);
            afficherTexte(renderer, police, wbuf, glacial, W_ECRAN/2-80, H_ECRAN-42);

            afficherTexte(renderer, police,
                "A/D:Marcher  ESPACE:Sauter  F:Tirer  E:Melee  G:Guide  F1:Scores  S:Sauver",
                blanc, 15, H_ECRAN-22);

        } else if (etat == ETAT_GAMEOVER) {
            wagonsInterieurs_afficher(&bg, renderer);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 60, 0, 0, 190);
            SDL_Rect overlay = {0, 0, W_ECRAN, H_ECRAN};
            SDL_RenderFillRect(renderer, &overlay);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            SDL_Color rouge = {255, 60, 60, 255};
            SDL_Color grisC = {180, 180, 180, 255};
            char buf[128];
            afficherTexteCentre(renderer, policeGrande, "GAME OVER", rouge, 120);
            snprintf(buf, sizeof(buf), "Score final : %d", finalScore);
            afficherTexteCentre(renderer, police, buf, blanc, 220);
            snprintf(buf, sizeof(buf), "Temps : %02d:%02d", finalTimeSec/60, finalTimeSec%60);
            afficherTexteCentre(renderer, police, buf, blanc, 250);
            snprintf(buf, sizeof(buf), "Wagon atteint : %d/5", finalWagon);
            afficherTexteCentre(renderer, police, buf, blanc, 280);
            afficherTexteCentre(renderer, police,
                "[ENTREE] Enregistrer votre score   [ECHAP] Menu des scores",
                grisC, H_ECRAN - 60);

        } else if (etat == ETAT_SCORES) {
            scores_afficherMenu(scores, nbScores, renderer, police, policeGrande, blanc);
            SDL_Rect btnRetour  = {150, H_ECRAN - 90, 200, 45};
            SDL_Rect btnQuitter = {450, H_ECRAN - 90, 200, 45};
            drawMenuBtn(renderer, police, "RETOUR",  btnRetour,  scoresSelected == 0, 0);
            drawMenuBtn(renderer, police, "QUITTER", btnQuitter, scoresSelected == 1, 0);
            afficherTexteCentre(renderer, police,
                "[</>] Naviguer  [ENTREE] Valider  [E] Enigme  [ECHAP] Quitter",
                (SDL_Color){180, 190, 210, 255}, H_ECRAN - 30);

        } else if (etat == ETAT_SELECTION) {
            /* Warm gradient: dark red top → deep blue bottom */
            for (int i = 0; i < H_ECRAN; i++) {
                float t = (float)i / (float)H_ECRAN;
                Uint8 r = (Uint8)(60 * (1.0f - t) +  8 * t);
                Uint8 g = (Uint8)(20 * (1.0f - t) + 12 * t);
                Uint8 b = (Uint8)(30 * (1.0f - t) + 35 * t);
                SDL_SetRenderDrawColor(renderer, r, g, b, 255);
                SDL_RenderDrawLine(renderer, 0, i, W_ECRAN - 1, i);
            }

            SDL_Color jauneS = {255, 215,   0, 255};
            SDL_Color blancS = {255, 255, 255, 255};
            SDL_Color hintS  = {180, 190, 210, 255};
            TTF_Font *tf = policeGrande ? policeGrande : police;

            if (joueurScreen == 0) {
                /* Screen A: CHOIX DU MODE */
                afficherTexteCentre(renderer, tf, "CHOIX DU MODE", jauneS, 40);

                SDL_Rect btnA[3] = {
                    {250, 160, 300, 55},
                    {250, 240, 300, 55},
                    {300, 340, 200, 45}
                };
                const char *lblA[3] = {"MONO JOUEUR", "MULTI JOUEURS", "RETOUR"};

                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                for (int i = 0; i < 3; i++) {
                    SDL_Rect btn = btnA[i];
                    int sel = (i == joueurSelected);
                    if (sel) {
                        SDL_SetRenderDrawColor(renderer, 230, 140, 50, 240);
                        SDL_RenderFillRect(renderer, &btn);
                        SDL_SetRenderDrawColor(renderer, 255, 215, 0, 255);
                        SDL_RenderDrawRect(renderer, &btn);
                        SDL_Rect inner = {btn.x+1, btn.y+1, btn.w-2, btn.h-2};
                        SDL_RenderDrawRect(renderer, &inner);
                    } else {
                        SDL_SetRenderDrawColor(renderer, 30, 40, 70, 220);
                        SDL_RenderFillRect(renderer, &btn);
                        SDL_SetRenderDrawColor(renderer, 100, 100, 130, 255);
                        SDL_RenderDrawRect(renderer, &btn);
                    }
                    SDL_Color tc = sel ? (SDL_Color){255, 255, 255, 255}
                                       : (SDL_Color){200, 210, 230, 255};
                    afficherTexteCentreRect(renderer, police, lblA[i], tc, btn);
                }
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

                afficherTexteCentre(renderer, police,
                    "[^/v] Naviguer  [ENTREE] Valider  [ECHAP] Retour",
                    hintS, H_ECRAN - 25);

            } else {
                /* Screen B — full avatar / input / validate config */
                afficherTexteCentre(renderer, tf, "CONFIGURATION JOUEUR", jauneS, 40);
                char modeBuf[32];
                snprintf(modeBuf, sizeof(modeBuf), "Mode : %s",
                         joueurMode == 1 ? "1 Joueur" : "2 Joueurs");
                afficherTexteCentre(renderer, police, modeBuf, blancS, 85);

                SDL_Color cyanS = { 60, 180, 220, 255};
                SDL_Color greyS = {160, 160, 160, 255};

                /* ── P1 column (always visible) ── */
                afficherTexte(renderer, police, "JOUEUR 1", blancS, 100, 115);

                SDL_Rect avP1[2] = {{50, 145, 140, 45}, {210, 145, 140, 45}};
                drawMenuBtn(renderer, police, "AVATAR 1", avP1[0],
                            joueurSelected == 0, joueurAvatarP1 == 0);
                drawMenuBtn(renderer, police, "AVATAR 2", avP1[1],
                            joueurSelected == 1, joueurAvatarP1 == 1);

                SDL_Rect inP1[2] = {{50, 210, 140, 45}, {210, 210, 140, 45}};
                drawMenuBtn(renderer, police, "INPUT 1", inP1[0],
                            joueurSelected == 2, joueurInputP1 == 0);
                drawMenuBtn(renderer, police, "INPUT 2", inP1[1],
                            joueurSelected == 3, joueurInputP1 == 1);

                char prevP1[64];
                snprintf(prevP1, sizeof(prevP1), "Avatar: %d  Input: %s",
                         joueurAvatarP1 + 1,
                         joueurInputP1 == 0 ? "A/D/SPACE/F/E" : "Fl./K/L");
                afficherTexte(renderer, police, prevP1, greyS, 50, 275);

                /* ── P2 column (multi only) ── */
                if (joueurMode == 2) {
                    SDL_SetRenderDrawColor(renderer, 80, 80, 100, 255);
                    SDL_RenderDrawLine(renderer, 400, 110, 400, 360);

                    afficherTexte(renderer, police, "JOUEUR 2", cyanS, 500, 115);

                    SDL_Rect avP2[2] = {{420, 145, 140, 45}, {580, 145, 140, 45}};
                    drawMenuBtn(renderer, police, "AVATAR 1", avP2[0],
                                joueurSelected == 4, joueurAvatarP2 == 0);
                    drawMenuBtn(renderer, police, "AVATAR 2", avP2[1],
                                joueurSelected == 5, joueurAvatarP2 == 1);

                    SDL_Rect inP2[2] = {{420, 210, 140, 45}, {580, 210, 140, 45}};
                    drawMenuBtn(renderer, police, "INPUT 1", inP2[0],
                                joueurSelected == 6, joueurInputP2 == 0);
                    drawMenuBtn(renderer, police, "INPUT 2", inP2[1],
                                joueurSelected == 7, joueurInputP2 == 1);

                    char prevP2[64];
                    snprintf(prevP2, sizeof(prevP2), "Avatar: %d  Input: %s",
                             joueurAvatarP2 + 1,
                             joueurInputP2 == 0 ? "A/D/SPACE/F/E" : "Fl./K/L");
                    afficherTexte(renderer, police, prevP2, greyS, 420, 275);
                }

                /* ── Bottom row ── */
                SDL_Rect btnBot[2] = {{170, 380, 220, 50}, {410, 380, 220, 50}};
                drawMenuBtn(renderer, police, "VALIDER", btnBot[0],
                            joueurSelected == 8, 0);
                drawMenuBtn(renderer, police, "RETOUR",  btnBot[1],
                            joueurSelected == 9, 0);

                afficherTexteCentre(renderer, police,
                    "[^/v/</>] Naviguer  [ENTREE] Valider  [ECHAP] Retour",
                    hintS, H_ECRAN - 25);
            }

        } else if (etat == ETAT_SAUVEGARDE) {
            SDL_SetRenderDrawColor(renderer, 10, 15, 25, 255);
            SDL_RenderClear(renderer);

            /* Title */
            SDL_Color blanc2   = {255, 255, 255, 255};
            SDL_Color jaune2   = {255, 215,   0, 255};
            SDL_Color gris2    = {180, 180, 180, 255};
            afficherTexteCentre(renderer, policeGrande,
                (sauvegardeScreen == 0) ? "SAUVEGARDE" : "CHARGER PARTIE",
                blanc2, 60);

            /* Message */
            afficherTexteCentre(renderer, police,
                (sauvegardeScreen == 0)
                    ? "Voulez-vous sauvegarder votre jeu ?"
                    : "Reprendre votre partie sauvegardee ?",
                gris2, 180);

            /* Buttons — two 200×60, 40px gap, centered */
            const char *labels[2][2] = {
                {"OUI",           "NON"},
                {"CHARGER LE JEU","NOUVELLE PARTIE"}
            };
            int totalW  = 200 + 40 + 200;
            int startX  = W_ECRAN / 2 - totalW / 2;
            int btnY    = 280;

            for (int b = 0; b < 2; b++) {
                int bx = startX + b * (200 + 40);
                SDL_Rect btn = {bx, btnY, 200, 60};

                /* Fill */
                SDL_SetRenderDrawColor(renderer, 40, 60, 100, 255);
                SDL_RenderFillRect(renderer, &btn);

                /* Border: yellow if selected, orange otherwise */
                if (b == sauvegardeSelected) {
                    SDL_SetRenderDrawColor(renderer, 255, 215, 0, 255);
                } else {
                    SDL_SetRenderDrawColor(renderer, 230, 140, 50, 255);
                }
                SDL_RenderDrawRect(renderer, &btn);
                SDL_Rect btnInner = {bx+1, btnY+1, 198, 58};
                SDL_RenderDrawRect(renderer, &btnInner);

                /* Label centered in button */
                SDL_Color labelCol = (b == sauvegardeSelected) ? jaune2 : blanc2;
                afficherTexteColonne(renderer, police,
                    labels[sauvegardeScreen][b],
                    labelCol, bx, 200, btnY + 20);
            }

            /* Bottom hint */
            afficherTexteCentre(renderer, police,
                "[<-/->] Naviguer  [ENTREE] Valider  [ECHAP] Retour",
                gris2, H_ECRAN - 30);

        } else if (etat == ETAT_MENU) {
            SDL_Rect fullScreen = {0, 0, W_ECRAN, H_ECRAN};

            /* Background: outside_bg.png stretched, or dark blue fallback */
            if (bg.outsideBgTex) {
                SDL_RenderCopy(renderer, bg.outsideBgTex, NULL, &fullScreen);
            } else {
                SDL_SetRenderDrawColor(renderer, 10, 15, 30, 255);
                SDL_RenderFillRect(renderer, &fullScreen);
            }

            /* Dark overlay for readability */
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 140);
            SDL_RenderFillRect(renderer, &fullScreen);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

            /* Title + subtitle */
            SDL_Color jauneMenu = {255, 215, 0, 255};
            SDL_Color blancMenu = {255, 255, 255, 255};
            TTF_Font *titleFont = policeGrande ? policeGrande : police;
            afficherTexteCentre(renderer, titleFont, "SNOWPIERCER", jauneMenu, 70);
            afficherTexteCentre(renderer, police,    "LE JEU",      blancMenu, 135);

            /* 5 buttons stacked, centered horizontally */
            const char *menuLabels[5] = {
                "JOUER", "OPTIONS", "MEILLEURS SCORES", "HISTOIRE", "QUITTER"
            };
            for (int i = 0; i < 5; i++) {
                SDL_Rect btnRect = {250, 190 + i * 65, 300, 50};
                int isSelected = (i == menuSelected);

                if (isSelected) {
                    /* Orange fill, yellow 2px border */
                    SDL_SetRenderDrawColor(renderer, 230, 140, 50, 240);
                    SDL_RenderFillRect(renderer, &btnRect);
                    SDL_SetRenderDrawColor(renderer, 255, 215, 0, 255);
                    SDL_RenderDrawRect(renderer, &btnRect);
                    SDL_Rect inner = {btnRect.x+1, btnRect.y+1,
                                      btnRect.w-2, btnRect.h-2};
                    SDL_RenderDrawRect(renderer, &inner);
                } else {
                    /* Dark blue fill, grey border */
                    SDL_SetRenderDrawColor(renderer, 30, 40, 70, 220);
                    SDL_RenderFillRect(renderer, &btnRect);
                    SDL_SetRenderDrawColor(renderer, 100, 100, 130, 255);
                    SDL_RenderDrawRect(renderer, &btnRect);
                }

                SDL_Color txtCol = isSelected
                    ? (SDL_Color){255, 255, 255, 255}
                    : (SDL_Color){200, 210, 230, 255};
                int textY = btnRect.y + (btnRect.h - 22) / 2;
                afficherTexteCentre(renderer, police, menuLabels[i], txtCol, textY);
            }

            /* Bottom instruction */
            SDL_Color hintCol = {180, 190, 210, 255};
            afficherTexteCentre(renderer, police,
                "[^/v] Naviguer  [ENTREE] Valider  [J/O/M/H] Raccourcis  [ECHAP] Quitter",
                hintCol, H_ECRAN - 25);

        } else if (etat == ETAT_OPTIONS) {
            /* Vertical gradient background: (10,45,90) top -> (5,10,25) bottom */
            for (int i = 0; i < H_ECRAN; i++) {
                float t = (float)i / (float)H_ECRAN;
                Uint8 r = (Uint8)(10 * (1.0f - t) +  5 * t);
                Uint8 g = (Uint8)(45 * (1.0f - t) + 10 * t);
                Uint8 b = (Uint8)(90 * (1.0f - t) + 25 * t);
                SDL_SetRenderDrawColor(renderer, r, g, b, 255);
                SDL_RenderDrawLine(renderer, 0, i, W_ECRAN - 1, i);
            }

            SDL_Color jauneOpt = {255, 215,   0, 255};
            SDL_Color blancOpt = {255, 255, 255, 255};
            SDL_Color hintOpt  = {180, 190, 210, 255};
            TTF_Font *titleFont = policeGrande ? policeGrande : police;

            /* Title */
            afficherTexteCentre(renderer, titleFont, "OPTIONS", jauneOpt, 50);

            /* Group 1 — VOLUME */
            afficherTexte(renderer, police, "VOLUME", blancOpt, 100, 150);
            char vbuf[32];
            snprintf(vbuf, sizeof(vbuf), "%d %%", volumeLevel * 100 / 128);
            afficherTexte(renderer, police, vbuf, blancOpt, 250, 150);

            /* Volume bar */
            SDL_SetRenderDrawColor(renderer, 40, 40, 60, 255);
            SDL_Rect volBg = {100, 185, 400, 10};
            SDL_RenderFillRect(renderer, &volBg);
            SDL_SetRenderDrawColor(renderer, 60, 180, 220, 255);
            SDL_Rect volFg = {100, 185, volumeLevel * 400 / 128, 10};
            SDL_RenderFillRect(renderer, &volFg);

            /* Group 2 — MODE D'AFFICHAGE */
            afficherTexte(renderer, police, "MODE D'AFFICHAGE", blancOpt, 100, 290);
            afficherTexte(renderer, police,
                fullscreenMode == 0 ? "Normal" : "Plein ecran",
                blancOpt, 320, 290);

            /* Five buttons */
            SDL_Rect buttons[5] = {
                {100, 215, 150, 40},                /* 0: DIMINUER */
                {280, 215, 150, 40},                /* 1: AUGMENTER */
                {100, 330, 150, 40},                /* 2: NORMAL */
                {280, 330, 150, 40},                /* 3: PLEIN ECRAN */
                {(W_ECRAN - 200) / 2, 420, 200, 45} /* 4: RETOUR */
            };
            const char *btnLabels[5] = {
                "-", "+", "NORMAL", "PLEIN ECRAN", "RETOUR"
            };

            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            for (int i = 0; i < 5; i++) {
                SDL_Rect btn = buttons[i];
                int isSelected = (i == optionsSelected);

                if (isSelected) {
                    SDL_SetRenderDrawColor(renderer, 230, 140, 50, 240);
                    SDL_RenderFillRect(renderer, &btn);
                    SDL_SetRenderDrawColor(renderer, 255, 215, 0, 255);
                    SDL_RenderDrawRect(renderer, &btn);
                    SDL_Rect inner = {btn.x+1, btn.y+1, btn.w-2, btn.h-2};
                    SDL_RenderDrawRect(renderer, &inner);
                } else {
                    SDL_SetRenderDrawColor(renderer, 30, 50, 80, 220);
                    SDL_RenderFillRect(renderer, &btn);
                    SDL_SetRenderDrawColor(renderer, 100, 110, 140, 255);
                    SDL_RenderDrawRect(renderer, &btn);
                }

                SDL_Color txtCol = isSelected
                    ? (SDL_Color){255, 255, 255, 255}
                    : (SDL_Color){200, 210, 230, 255};
                afficherTexteCentreRect(renderer, police, btnLabels[i], txtCol, btn);
            }
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

            /* Bottom instruction */
            afficherTexteCentre(renderer, police,
                "[^/v/</>] Naviguer  [ENTREE] Valider  [+/-/F/N] Raccourcis  [ECHAP] Retour",
                hintOpt, H_ECRAN - 25);
        }

        SDL_RenderPresent(renderer);
    }

    scores_sauvegarder(FICHIER_SCORES, scores, nbScores);
    background_liberer(&bg);
    if (policeGrande && policeGrande != police) TTF_CloseFont(policeGrande);
    if (police) TTF_CloseFont(police);
    Mix_CloseAudio();
    TTF_Quit(); IMG_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(fenetre);
    SDL_Quit();
    return EXIT_SUCCESS;
}
