#ifndef BACKGROUND_H
#define BACKGROUND_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#include "combat.h"
#include "weaponfx.h"
#include "char_anim.h"

#define W_ECRAN        800
#define H_ECRAN        520
#define W_BACKGROUND   1600
#define H_BACKGROUND   900

#define MODE_MONO   0
#define MODE_MULTI  1

#define VUE_EXTERIEURE  0
#define VUE_INTERIEURE  1

#define MAX_SCORES      10
/* On web, saves live under the IDBFS mount at /persist so they survive a
 * page reload (see build_web.sh --pre-js and the FS.syncfs calls after each
 * write in background.c).  Native keeps them in the working directory.     */
#ifdef __EMSCRIPTEN__
#define FICHIER_SCORES      "/persist/scores.txt"
#define FICHIER_SAUVEGARDE  "/persist/savegame.txt"
#else
#define FICHIER_SCORES      "scores.txt"
#define FICHIER_SAUVEGARDE  "savegame.txt"
#endif

#define MAX_WAGONS          5
#define MAX_PORTES          5
#define MAX_OBSTACLES_WAGON 5

#define PLATEFORME_FIXE          0
#define PLATEFORME_MOBILE        1
#define PLATEFORME_DESTRUCTIBLE  2
#define MAX_PLATEFORMES          10
#define SCROLL_DROITE  0
#define SCROLL_GAUCHE  1
#define SCROLL_HAUT    2
#define SCROLL_BAS     3

#define MAX_DESTRUCTIBLES 10

#define WAGON_SOL_Y     (H_ECRAN - 80)
#define WAGON_PLAF_Y    42
#define WAGON_MUR_G_X   22
#define WAGON_MUR_D_X   (W_ECRAN - 22)
#define WAGON_PORTE_W   46
#define WAGON_PORTE_H   112

/* Versus-only canvas + wall bounds.  PvP uses a 16:9 logical canvas so
 * the engine-room art fills a 1024×576 fullscreen window without the
 * pillarbox the 800×520 campaign canvas produces.  Campaign code keeps
 * W_ECRAN/H_ECRAN; only versus paths read these.                       */
#define VS_CANVAS_W     1024
#define VS_CANVAS_H      576
#define VS_WAGON_L        22
#define VS_WAGON_R      (VS_CANVAS_W - 22)

#define NB_JOUEURS_MAX   2
#define MAX_NEIGE        300
#define GRAVITE_JEU      0.40f
#define JUMP_FORCE       -9.5f
/* Player hitbox — scaled by 1.15x vs. baseline (33 x 55).
 * Matches the 1.15x bump applied to PLAYER_DRAW_* in combat.h so the
 * draw rect stays anchored to the hitbox bottom-center and the player's
 * feet remain on the floor (see joueur_afficher_interieur_idx).         */
#define JOUEUR_W         38   /* baseline 33 * 1.15 = 38 */
#define JOUEUR_H         63   /* baseline 55 * 1.15 = 63 */
#define VITESSE_JOUEUR   4.0f
#define JOUEUR_MAX_HP    100
#define SHOOT_COOLDOWN_MS 350
#define MELEE_COOLDOWN_MS 500
#define MELEE_RANGE       55

typedef struct {
    TTF_Font  *police;
    SDL_Color  couleur;
    char       contenu[256];
    int        x, y;
} Texte;

typedef struct {
    SDL_Texture *texture;
    SDL_Rect     position;
    int          visible;
} FenetreGuide;

typedef struct {
    char nom[64];
    int  score;
    int  tempsSec;
    int  wagonAtteint;
} Score;

typedef struct {
    SDL_Texture *texture;
    SDL_Rect     rect;
    char         nom[32];
} Wagon;

typedef struct {
    SDL_Rect zoneEntree;
    SDL_Rect zoneSortie;
    int      indexWagon;
} PorteWagon;

typedef struct {
    SDL_Rect     rect;
    SDL_Texture *texture;
    int mobile;
    int vitesse;
    int direction;
    int minX, maxX;
} PlateformeFixe;

typedef enum {
    FLOCON_INTACT = 0,
    FLOCON_FISSURE,
    FLOCON_FONDU,
    FLOCON_DETRUIT
} EtatFlocon;

typedef struct {
    int          x, y, w, h;
    EtatFlocon   etat;
    SDL_Texture *sprites[3];
    Uint32       timerEtat;
    int          delaiMs;
} ElementDestructible;

typedef struct {
    float x, y;
    float vitesse;
    float derive;
    int   alpha;
    int   taille;
    int   couche;
} ParticuleNeige;

typedef struct {
    float      x, y;
    float      velX, velY;
    int        w, h;
    int        auSol;
    int        actif;
    int        direction;
    int             hp;
    EtatJoueur      etat;
    AnimationSystem anim;
    CharAnim        ca;
    int             wasGrounded;
    Uint32          lastShootTick;
    Uint32     lastMeleeTick;
    int        muzzleFrames;
    float      muzzleX, muzzleY;
    int        pendingMeleeImpact; /* 1 while waiting for sword to land at CHAR_ATTACK_IMPACT_FRAME */
    int        meleeImpactDir;     /* j->direction snapshotted at swing start */
    int        pendingGunFire;     /* 1 while waiting for ANIM_GUN to reach CHAR_GUN_FIRE_FRAME    */
    int        gunFireDir;         /* j->direction snapshotted at fire press                       */
} Joueur;

typedef struct {
    int actif;
    int alpha;
} FlashEcran;

typedef struct {
    SDL_Rect  rects[MAX_OBSTACLES_WAGON];
    SDL_Color couleurs[MAX_OBSTACLES_WAGON];
    int       nb;
    float     hoverBaseY[MAX_OBSTACLES_WAGON];
    float     hoverPhase[MAX_OBSTACLES_WAGON];
} ObstaclesWagon;

typedef struct {
    SDL_Color    fond;
    SDL_Color    sol;
    SDL_Color    plafond;
    SDL_Color    mur;
    SDL_Color    accent;
    SDL_Color    accentClair;
    SDL_Rect     fenetres[3];
    int          nbFenetres;
    ObstaclesWagon obstacles;
    SDL_Texture *floorTex;
    SDL_Texture *obstacleTex;
} StyleWagon;

typedef struct {
    int   actif;
    int   direction;
    float offset;
    int   wagonCible;
} TransitionSlide;

typedef struct {
    SDL_Texture *imagebackground;
    SDL_Texture *imagebackground2;
    int w, h;
    int imgW, imgH;
    int posx, posy;
    SDL_Rect camera1;
    SDL_Rect camera2;
    SDL_Rect posEcran1;
    SDL_Rect posEcran2;
    int typePlateforme;
    int niveau;
    int mode;
    Mix_Music   *son;
    Texte        texte;
    Uint32       tempsDepart;
    FenetreGuide fenetreguide;

    int        vue;
    int        wagonActif;

    Wagon      wagons[MAX_WAGONS];
    int        nbWagons;

    PorteWagon portes[MAX_PORTES];
    int        nbPortes;

    PlateformeFixe plateformes[MAX_PLATEFORMES];
    int            nbPlateformes;

    ElementDestructible destructibles[MAX_DESTRUCTIBLES];
    int                 nbDestructibles;

    ParticuleNeige neige[MAX_NEIGE];
    Joueur         joueurs[NB_JOUEURS_MAX];
    int            nbJoueursActifs;
    FlashEcran     flash;

    StyleWagon     stylesWagons[MAX_WAGONS];
    TransitionSlide transitionSlide;
    float          parallaxOffset;

    SDL_Texture *wagonBgTex[MAX_WAGONS];
    SDL_Texture *outsideBgTex;
    SDL_Texture *enemySheets[3];
    SDL_Texture *bulletTex;
    SDL_Texture *muzzleTex;
    SDL_Texture *hitFxSheet;

    Balle      balles[NB_JOUEURS_MAX][MAX_BALLES];
    int        nbBalles[NB_JOUEURS_MAX];

    Ennemi     ennemis[MAX_ENNEMIS];
    int        nbEnnemis;

    HitEffect  hitFx[MAX_HIT_FX];
    int        nbHitFx;

    int   splitMode;
    float splitCooldown;

    WeaponSwing    swings[NB_JOUEURS_MAX];
    ParticleField  particles;

    int blockTransitionRight;
    int blockTransitionLeft;
} Background;

int  background_chargerImages(Background *bg, SDL_Renderer *renderer,
                               const char *cheminImg1, const char *cheminImg2);
int  background_initialiserTexte(Background *bg, const char *contenu,
                                  TTF_Font *police, SDL_Color couleur, int x, int y);
int  background_initialiserSon(Background *bg, const char *cheminSon);
void background_initialiserPosition(Background *bg, int posx, int posy);
int  background_init(Background *bg, SDL_Renderer *renderer, int niveau);

void background_scrolling(Background *bg, int direction, int dx, int dy, int numBack);
void background_afficher(Background *bg, SDL_Renderer *renderer,
                          int mode, int deltax, int deltay);
void background_afficherTemps(Background *bg, SDL_Renderer *renderer, int x, int y);

int  fenetreGuide_init(Background *bg, SDL_Renderer *renderer,
                        const char *cheminImage, SDL_Rect rect);
void fenetreGuide_afficher(Background *bg, SDL_Renderer *renderer);

void scores_saisirNom(char *nom, int taille, SDL_Renderer *renderer,
                       TTF_Font *police, SDL_Color couleur);
void scores_sauvegarder(const char *fichier, Score *scores, int nbScores);
int  scores_charger(const char *fichier, Score *scores, int maxScores);
void scores_ajouterEtTrier(Score *scores, int *nbScores, const char *nom, int score,
                            int tempsSec, int wagonAtteint);
void scores_afficherMenu(Score *scores, int nbScores, SDL_Renderer *renderer,
                          TTF_Font *police, TTF_Font *policeGrande, SDL_Color couleur);

void vues_init(Background *bg, SDL_Renderer *renderer);

void plateformes_init(Background *bg, SDL_Renderer *renderer);
void plateformes_update(Background *bg);
int  plateforme_collision(Background *bg, SDL_Rect *joueur, int *velY);

void destructibles_init(Background *bg, SDL_Renderer *renderer);
void destructibles_update(Background *bg);
int  destructibles_verifierCollision(Background *bg, SDL_Rect joueur);
void destructibles_liberer(Background *bg);

void wagons_liberer(Background *bg);
void background_liberer(Background *bg);

void wagonsInterieurs_init(Background *bg, SDL_Renderer *renderer);
void enemies_respawn_default(Background *bg);
void wagonsInterieurs_afficher(Background *bg, SDL_Renderer *renderer);
void wagonsInterieurs_afficher_camera(Background *bg, SDL_Renderer *renderer, int ox);
void wagonsKinetic_renderRT(Background *bg, SDL_Renderer *renderer);
void minimap_afficher(Background *bg, SDL_Renderer *renderer);
void transitionSlide_declencher(Background *bg, int direction);
void transitionSlide_update(Background *bg);

void joueur_init_interieur_idx(Background *bg, int idx);
void joueur_init_interieur(Background *bg);
void joueur_update_interieur_idx(Background *bg, int idx,
                                  int gauche, int droite,
                                  int sauter, int tirer, int melee, float dt);
void joueur_update_interieur(Background *bg, int gauche, int droite,
                              int sauter, int tirer, int melee, float dt);
void joueur_afficher_interieur_idx(Background *bg, SDL_Renderer *renderer,
                                    int idx, int ox);
void joueur_afficher_interieur(Background *bg, SDL_Renderer *renderer, int ox);

void neige_init(Background *bg);
void neige_update(Background *bg);
void flash_declencher(Background *bg);
void flash_update(Background *bg);
void flash_afficher(Background *bg, SDL_Renderer *renderer);

void ennemis_update(Ennemi *ennemis, int *nb, int wagonActif, float dt,
                    Joueur *joueurs, int nbJoueurs,
                    int wagonSolY, int wagonPlafY);

struct GameplayState;
int sauvegarder_partie(const Background *bg, const struct GameplayState *gp,
                       int scoreJoueur, Uint32 tempsDepart);
int charger_partie(Background *bg, struct GameplayState *gp,
                   int *scoreJoueur, Uint32 *tempsDepart);

#endif
