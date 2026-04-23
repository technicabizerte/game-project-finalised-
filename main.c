#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "background.h"

#define ETAT_JEU    0
#define ETAT_SCORES 1
#define ETAT_SAISIE 2

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

    int etat      = ETAT_JEU;
    int continuer = 1;
    Uint32 lastTick = SDL_GetTicks();

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
                }
                if (etat == ETAT_SCORES && ev.key.keysym.sym == SDLK_ESCAPE)
                    etat = ETAT_JEU;
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
        }

        if (etat == ETAT_SAISIE) {
            scores_saisirNom(nomJoueur, sizeof(nomJoueur), renderer, police, jaune);
            scores_ajouterEtTrier(scores, &nbScores, nomJoueur, scoreJoueur);
            scores_sauvegarder(FICHIER_SCORES, scores, nbScores);
            etat = ETAT_SCORES;
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        if (etat == ETAT_JEU) {
            wagonsInterieurs_afficher(&bg, renderer);
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
                "A/D:Marcher  ESPACE:Sauter  F:Tirer  E:Melee  G:Guide  F1:Scores",
                blanc, 15, H_ECRAN-22);

        } else if (etat == ETAT_SCORES) {
            background_afficher(&bg, renderer, MODE_MONO, 0, 0);
            scores_afficherMenu(scores, nbScores, renderer, police, blanc);
        }

        SDL_RenderPresent(renderer);
    }

    scores_sauvegarder(FICHIER_SCORES, scores, nbScores);
    background_liberer(&bg);
    if (police) TTF_CloseFont(police);
    Mix_CloseAudio();
    TTF_Quit(); IMG_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(fenetre);
    SDL_Quit();
    return EXIT_SUCCESS;
}
