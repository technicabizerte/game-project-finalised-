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

    static int p1SelectedChar = 0;
    static int p2SelectedChar = 0;
    static int p1Confirmed    = 0;
    static int p2Confirmed    = 0;
    static int sauvegardeScreen   = 0;   /* 0 = screen A, 1 = screen B */
    static int sauvegardeSelected = 0;   /* 0 = left btn, 1 = right btn */
    static int menuSelected       = 0;   /* 0..4 highlighted main-menu button */

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
                if (etat == ETAT_SCORES && ev.key.keysym.sym == SDLK_ESCAPE) {
                    etat = ETAT_MENU;
                }
                if (etat == ETAT_GAMEOVER) {
                    if (ev.key.keysym.sym == SDLK_RETURN) etat = ETAT_SAISIE;
                    if (ev.key.keysym.sym == SDLK_ESCAPE) etat = ETAT_SCORES;
                }
                else if (etat == ETAT_SELECTION) {
                    /* P1 navigation */
                    if (ev.key.keysym.sym == SDLK_a) {
                        do { p1SelectedChar = (p1SelectedChar + 2) % 3; }
                        while (!PERSONNAGES[p1SelectedChar].unlocked);
                    }
                    if (ev.key.keysym.sym == SDLK_d) {
                        do { p1SelectedChar = (p1SelectedChar + 1) % 3; }
                        while (!PERSONNAGES[p1SelectedChar].unlocked);
                    }
                    if (ev.key.keysym.sym == SDLK_f) p1Confirmed = 1;

                    /* P2 navigation */
                    if (ev.key.keysym.sym == SDLK_LEFT) {
                        do { p2SelectedChar = (p2SelectedChar + 2) % 3; }
                        while (!PERSONNAGES[p2SelectedChar].unlocked);
                    }
                    if (ev.key.keysym.sym == SDLK_RIGHT) {
                        do { p2SelectedChar = (p2SelectedChar + 1) % 3; }
                        while (!PERSONNAGES[p2SelectedChar].unlocked);
                    }
                    if (ev.key.keysym.sym == SDLK_k) p2Confirmed = 1;

                    /* Start game */
                    if (ev.key.keysym.sym == SDLK_RETURN &&
                        p1Confirmed && p2Confirmed) {
                        bg.joueurs[0].hp = JOUEUR_MAX_HP;
                        bg.joueurs[1].hp = JOUEUR_MAX_HP;
                        joueur_init_interieur(&bg);
                        scoreJoueur    = 0;
                        bg.tempsDepart = SDL_GetTicks();
                        bg.wagonActif  = 0;
                        etat = ETAT_JEU;
                    }

                    /* Escape from selection = back to main menu */
                    if (ev.key.keysym.sym == SDLK_ESCAPE) etat = ETAT_MENU;
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
                            case 1: fprintf(stderr, "[MENU] Options a venir\n"); break;
                            case 2: etat = ETAT_SCORES; break;
                            case 3: fprintf(stderr, "[MENU] Histoire a venir\n"); break;
                            case 4: continuer = 0; break;
                        }
                    }

                    /* Direct keyboard shortcuts */
                    if (k == SDLK_j) { menuSelected = 0; etat = ETAT_SAUVEGARDE; }
                    if (k == SDLK_o) { menuSelected = 1;
                        fprintf(stderr, "[MENU] Options a venir\n"); }
                    if (k == SDLK_m) { menuSelected = 2; etat = ETAT_SCORES; }
                    if (k == SDLK_h) { menuSelected = 3;
                        fprintf(stderr, "[MENU] Histoire a venir\n"); }
                    if (k == SDLK_ESCAPE) continuer = 0;
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
            background_afficher(&bg, renderer, MODE_MONO, 0, 0);
            scores_afficherMenu(scores, nbScores, renderer, police, blanc);

        } else if (etat == ETAT_SELECTION) {
            SDL_SetRenderDrawColor(renderer, 10, 15, 25, 255);
            SDL_RenderClear(renderer);

            TTF_Font *titleFont = policeGrande ? policeGrande : police;
            SDL_Color titreCol = {255, 255, 255, 255};
            afficherTexteCentre(renderer, titleFont,
                                "CHOISISSEZ VOTRE PERSONNAGE", titreCol, 25);

            const int COL_W = 400;

            for (int pl = 0; pl < 2; pl++) {
                int cx        = pl * 400;
                int selChar   = (pl == 0) ? p1SelectedChar : p2SelectedChar;
                int confirmed = (pl == 0) ? p1Confirmed    : p2Confirmed;

                /* Player label */
                SDL_Color labelCol = (pl == 0)
                    ? (SDL_Color){255, 255, 255, 255}
                    : (SDL_Color){ 60, 180, 220, 255};
                afficherTexteColonne(renderer, police,
                    (pl == 0) ? "JOUEUR 1" : "JOUEUR 2",
                    labelCol, cx, COL_W, 80);

                /* Character cards */
                for (int i = 0; i < 3; i++) {
                    int card_x = cx + 30;
                    int card_y = 120 + i * 100;
                    SDL_Rect card = {card_x, card_y, 340, 90};

                    /* Card background */
                    SDL_SetRenderDrawColor(renderer, 25, 30, 50, 255);
                    SDL_RenderFillRect(renderer, &card);

                    /* Selection border */
                    if (i == selChar) {
                        SDL_SetRenderDrawColor(renderer, 230, 140, 50, 255);
                        SDL_RenderDrawRect(renderer, &card);
                        SDL_Rect inner = {card_x+1, card_y+1, 338, 88};
                        SDL_RenderDrawRect(renderer, &inner);
                    } else {
                        SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);
                        SDL_RenderDrawRect(renderer, &card);
                    }

                    if (!PERSONNAGES[i].unlocked) {
                        /* Dark overlay + "A VENIR" label */
                        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 120);
                        SDL_RenderFillRect(renderer, &card);
                        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                        SDL_Color gris = {120, 120, 120, 255};
                        afficherTexteColonne(renderer, police, "A VENIR", gris,
                                             card_x, 340, card_y + 36);
                    } else {
                        /* Name */
                        SDL_Color w = {255, 255, 255, 255};
                        afficherTexte(renderer, police, PERSONNAGES[i].nom, w,
                                      card_x + 10, card_y + 6);

                        /* Stat bars */
                        SDL_Color lbl = {180, 180, 180, 255};

                        afficherTexte(renderer, police, "VITESSE", lbl,
                                      card_x+8, card_y+28);
                        SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
                        SDL_Rect vbg = {card_x+80, card_y+30, 100, 6};
                        SDL_RenderFillRect(renderer, &vbg);
                        SDL_SetRenderDrawColor(renderer, 230, 180, 40, 255);
                        SDL_Rect vfg = {card_x+80, card_y+30,
                                        PERSONNAGES[i].vitesse * 10, 6};
                        SDL_RenderFillRect(renderer, &vfg);

                        afficherTexte(renderer, police, "FORCE", lbl,
                                      card_x+8, card_y+43);
                        SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
                        SDL_Rect fbg = {card_x+80, card_y+45, 100, 6};
                        SDL_RenderFillRect(renderer, &fbg);
                        SDL_SetRenderDrawColor(renderer, 230, 180, 40, 255);
                        SDL_Rect ffg = {card_x+80, card_y+45,
                                        PERSONNAGES[i].force * 10, 6};
                        SDL_RenderFillRect(renderer, &ffg);

                        afficherTexte(renderer, police, "SANTE", lbl,
                                      card_x+8, card_y+58);
                        SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
                        SDL_Rect sbg = {card_x+80, card_y+60, 100, 6};
                        SDL_RenderFillRect(renderer, &sbg);
                        SDL_SetRenderDrawColor(renderer, 230, 180, 40, 255);
                        SDL_Rect sfg = {card_x+80, card_y+60,
                                        PERSONNAGES[i].sante * 10, 6};
                        SDL_RenderFillRect(renderer, &sfg);
                    }
                }

                /* Controls reminder */
                SDL_Color ctrlCol = {160, 160, 160, 255};
                afficherTexteColonne(renderer, police,
                    (pl == 0) ? "A/D=bouger  SPACE=sauter  F=tirer  E=melee"
                              : "LEFT/RIGHT=bouger  UP=sauter  K=tirer  L=melee",
                    ctrlCol, cx, COL_W, H_ECRAN - 90);

                /* Confirmation state */
                if (!confirmed) {
                    SDL_Color confCol = (pl == 0)
                        ? (SDL_Color){255, 255, 255, 255}
                        : (SDL_Color){ 60, 180, 220, 255};
                    afficherTexteColonne(renderer, police,
                        (pl == 0) ? "J1 : [F] pour confirmer"
                                  : "J2 : [K] pour confirmer",
                        confCol, cx, COL_W, H_ECRAN - 55);
                } else {
                    SDL_Color grn = {60, 220, 80, 255};
                    afficherTexteColonne(renderer, police,
                        (pl == 0) ? "J1 : CONFIRME" : "J2 : CONFIRME",
                        grn, cx, COL_W, H_ECRAN - 55);
                }
            }

            /* Bottom instruction */
            SDL_Color instrCol = {200, 200, 200, 255};
            afficherTexteCentre(renderer, police,
                (p1Confirmed && p2Confirmed)
                    ? "[ENTREE] Commencer la partie"
                    : "Les deux joueurs doivent confirmer",
                instrCol, H_ECRAN - 25);

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
