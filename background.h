#ifndef BACKGROUND_H
#define BACKGROUND_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#include "combat.h"

/* ══ Dimensions écran ══ */
#define W_ECRAN        800
#define H_ECRAN        520
#define W_BACKGROUND   1600
#define H_BACKGROUND   900

/* ══ Modes affichage ══ */
#define MODE_MONO   0
#define MODE_MULTI  1

/* ══ Vues ══ */
#define VUE_EXTERIEURE  0
#define VUE_INTERIEURE  1

/* ══ Scores ══ */
#define MAX_SCORES      10
#define FICHIER_SCORES  "scores.txt"

/* ══ Wagons ══ */
#define MAX_WAGONS          5
#define MAX_PORTES          5
#define MAX_OBSTACLES_WAGON 5

/* ══ Plateformes (legacy) ══ */
#define PLATEFORME_FIXE          0
#define PLATEFORME_MOBILE        1
#define PLATEFORME_DESTRUCTIBLE  2
#define MAX_PLATEFORMES          10
#define SCROLL_DROITE  0
#define SCROLL_GAUCHE  1
#define SCROLL_HAUT    2
#define SCROLL_BAS     3

/* ══ Destructibles ══ */
#define MAX_DESTRUCTIBLES 10

/* ══ Intérieur wagon — layout ══ */
#define WAGON_SOL_Y     (H_ECRAN - 80)
#define WAGON_PLAF_Y    42
#define WAGON_MUR_G_X   22
#define WAGON_MUR_D_X   (W_ECRAN - 22)
#define WAGON_PORTE_W   46
#define WAGON_PORTE_H   112

/* ══ Joueur ══ */
#define NB_JOUEURS_MAX   2
#define MAX_NEIGE        300
#define GRAVITE_JEU      0.40f
#define JUMP_FORCE       -9.5f
#define JOUEUR_W         28
#define JOUEUR_H         48
#define VITESSE_JOUEUR   4.0f
#define JOUEUR_MAX_HP    100
#define SHOOT_COOLDOWN_MS 350
#define MELEE_COOLDOWN_MS 500
#define MELEE_RANGE       55

/* ─────────────────────────────────────
   STRUCTS
   ───────────────────────────────────── */

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

/* ══ Joueur ══ */
typedef struct {
    float      x, y;
    float      velX, velY;
    int        w, h;
    int        auSol;
    int        actif;
    int        direction;        /* 1 = droite, -1 = gauche */
    int             hp;
    EtatJoueur      etat;
    AnimationSystem anim;
    Uint32          lastShootTick;
    Uint32     lastMeleeTick;
    int        muzzleFrames;     /* countdown frames to show muzzle flash */
    float      muzzleX, muzzleY;
} Joueur;

typedef struct {
    int actif;
    int alpha;
} FlashEcran;

typedef struct {
    SDL_Rect  rects[MAX_OBSTACLES_WAGON];
    SDL_Color couleurs[MAX_OBSTACLES_WAGON];
    int       nb;
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

/* ══ Background ══ */
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

    /* ══ Phase 2 assets ══ */
    SDL_Texture *wagonBgTex[MAX_WAGONS];  /* wagon0-4 interior backgrounds */
    SDL_Texture *outsideBgTex;            /* window parallax */
    SDL_Texture *playerSheet;
    SDL_Texture *enemySheets[3];          /* guard, worker, enforcer */
    SDL_Texture *bulletTex;
    SDL_Texture *muzzleTex;
    SDL_Texture *hitFxSheet;

    /* ══ Combat state ══ */
    Balle      balles[NB_JOUEURS_MAX][MAX_BALLES];
    int        nbBalles[NB_JOUEURS_MAX];

    Ennemi     ennemis[MAX_ENNEMIS];
    int        nbEnnemis;

    HitEffect  hitFx[MAX_HIT_FX];
    int        nbHitFx;

    /* ══ Split-screen state ══ */
    int   splitMode;       /* 0 = MONO, 1 = SPLIT */
    float splitCooldown;   /* seconds, prevents thrashing */

} Background;

/* ─────────────────────────────────────
   PROTOTYPES
   ───────────────────────────────────── */

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
                          TTF_Font *police, SDL_Color couleur);

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
void wagonsInterieurs_afficher(Background *bg, SDL_Renderer *renderer);
void wagonsInterieurs_afficher_camera(Background *bg, SDL_Renderer *renderer, int ox);
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

/* Declared here (not combat.h) because it needs Joueur */
void ennemis_update(Ennemi *ennemis, int *nb, int wagonActif, float dt,
                    Joueur *joueurs, int nbJoueurs,
                    int wagonSolY, int wagonPlafY);

int sauvegarder_partie(const Background *bg, int scoreJoueur, Uint32 tempsDepart);
int charger_partie(Background *bg, int *scoreJoueur, Uint32 *tempsDepart);

extern int g_avatarTint[NB_JOUEURS_MAX];  /* 0 = none, 1 = bluish */

#endif
