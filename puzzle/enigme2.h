#ifndef ENIGME2_H
#define ENIGME2_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <time.h>
#include "rotozoom.h"

#define NB_IMAGES_PUZZLE    5
#define NB_PROPOSITIONS     3
#define TAILLE_CHEMIN       512
#define VIES_DEPART         3
#define TEMPS_ENIGME        30

#define MENU        0
#define EN_JEU      1
#define GAGNE       2
#define PERDU       3
#define FIN_JEU     4

#define WIN_W   960
#define WIN_H   480

#define MAX_PUZZLES_PER_SESSION 5

#define COLOR_BG_DARK       12, 14, 18, 255
#define COLOR_PANEL_BG      22, 26, 32, 245
#define COLOR_FRAME_BORDER  80, 220, 255, 255
#define COLOR_FRAME_DIM     60, 180, 220, 200
#define COLOR_RIVET         150, 160, 170, 255
#define COLOR_RIVET_DARK    60, 70, 80, 255
#define COLOR_TEXT_PRIMARY  220, 230, 240, 255
#define COLOR_TEXT_DIM      140, 150, 160, 255
#define COLOR_SUCCESS       60, 220, 120, 255
#define COLOR_ERROR         255, 70, 70, 255
#define COLOR_TIMER_GOOD    80, 200, 100, 255
#define COLOR_TIMER_WARN    255, 190, 50, 255
#define COLOR_TIMER_DANGER  255, 60, 60, 255
#define COLOR_GLOW_HOVER    80, 220, 255, 80
#define COLOR_GLOW_ACTIVE   100, 240, 180, 100
#define COLOR_GLOW_ERROR    255, 80, 80, 80
#define COLOR_HP_FULL       60, 200, 100, 255
#define COLOR_HP_LOW        255, 180, 60, 255
#define COLOR_HP_CRITICAL   255, 60, 60, 255
#define COLOR_TARGET 80, 220, 255, 255

#define GAP_SIZE            10

#define COL_LEFT_W          200
#define COL_LEFT_X          50

#define PANEL_HP_W          COL_LEFT_W
#define PANEL_HP_H          50
#define PANEL_HP_X          COL_LEFT_X
#define PANEL_HP_Y          65

#define PANEL_OBJECTIVE_W   COL_LEFT_W
#define PANEL_OBJECTIVE_H   200
#define PANEL_OBJECTIVE_X   COL_LEFT_X
#define PANEL_OBJECTIVE_Y   (PANEL_HP_Y + PANEL_HP_H + GAP_SIZE)

#define PANEL_STATUS_W      COL_LEFT_W
#define PANEL_STATUS_H      170
#define PANEL_STATUS_X      COL_LEFT_X
#define PANEL_STATUS_Y      (PANEL_OBJECTIVE_Y + PANEL_OBJECTIVE_H + GAP_SIZE)

#define PANEL_PUZZLE_W      360
#define PANEL_PUZZLE_H      360
#define PANEL_PUZZLE_X      290
#define PANEL_PUZZLE_Y      65

#define TARGET_SIZE         80
#define TARGET_X            (PANEL_PUZZLE_X + PANEL_PUZZLE_W / 2 - TARGET_SIZE / 2)
#define TARGET_Y            (PANEL_PUZZLE_Y + PANEL_PUZZLE_H / 2 - TARGET_SIZE / 2)

#define COL_RIGHT_W         180
#define COL_RIGHT_X         690
#define CARD_W              (COL_RIGHT_W - 14)
#define CARD_H              95
#define CARD_X              (COL_RIGHT_X + 7)
#define CARD_START_Y        65
#define CARD_SPACING        GAP_SIZE

#define CARD_1_Y            CARD_START_Y
#define CARD_2_Y            (CARD_1_Y + CARD_H + CARD_SPACING)
#define CARD_3_Y            (CARD_2_Y + CARD_H + CARD_SPACING)

#define BTN_NEXT_W          110
#define BTN_NEXT_H          40
#define BTN_NEXT_X          (COL_RIGHT_X + (COL_RIGHT_W - BTN_NEXT_W) / 2)
#define BTN_NEXT_Y          (CARD_3_Y + CARD_H + GAP_SIZE + 5)

#define TIMER_X             (PANEL_PUZZLE_X + 10)
#define TIMER_Y             25
#define TIMER_W             340
#define TIMER_H             18

#define HEADER_H            40
#define M_PI 3.14159265358979323846
extern int g_drag_enabled;
extern int g_dragged_prop_index;

typedef struct
{
    int x, y, largeur, hauteur;
    int bien_placee;
    int snap_x, snap_y;
    char chemin_image[TAILLE_CHEMIN];
} t_piece;

typedef struct
{
    int id;
    char chemin_image[TAILLE_CHEMIN];
    char chemin_propositions[NB_PROPOSITIONS][TAILLE_CHEMIN];
    int bonne_proposition;
    int deja_utilisee;
} t_image_puzzle;

typedef struct
{
    int score, vies, niveau;
} t_joueur;

typedef struct
{
    int debut, duree, reste;
} t_chrono;

typedef struct
{
    t_image_puzzle tab_images[NB_IMAGES_PUZZLE];
    int nb_images;
    t_joueur joueur;
    t_chrono chrono;
    t_piece piece;
    int souris_x, souris_y;
    int en_drag;
    int etat;

    int puzzles_completed;
    int puzzles_correct;
    int puzzles_wrong;

    int popup_active;
    Uint32 popup_until;
    char popup_message[128];
} t_enigme2;

extern SDL_Window   *g_win;
extern SDL_Renderer *g_ren;

int  charger_images_puzzle(t_enigme2 *e, char *dossier);
void init_enigme2(t_enigme2 *e, char *dossier);
void init_joueur(t_joueur *j);

int  image_aleatoire(t_enigme2 *e);
void reinit_images_utilisees(t_enigme2 *e);
int  toutes_images_utilisees(t_enigme2 *e);

void afficher_image_puzzle(t_image_puzzle *img);
void afficher_propositions(t_image_puzzle *img);

void gerer_clic_souris(t_enigme2 *e, int x, int y);
void deplacer_piece(t_enigme2 *e, int x, int y);
void relacher_piece(t_enigme2 *e, int x, int y);
int  verifier_position_piece(t_piece *p, int cible_x, int cible_y);

void lancer_chrono(t_chrono *c, int duree);
int  temps_restant(t_chrono *c);
int  temps_ecoule(t_chrono *c);
void animation_chrono(t_chrono *c);

void message_succes(void);
void message_echec(void);
void effet_rotozoom(char *msg);

int run_enigme2(void);

int run_enigme2_with_renderer(SDL_Renderer *renderer, SDL_Window *window);

typedef enum {
    DRAG_NONE = 0,
    DRAG_SUCCESS,
    DRAG_FAIL,
    DRAG_CANCEL
} DragResult;

DragResult handle_mousedown(int mx, int my);
void handle_mousemotion(int mx, int my);
DragResult handle_mouseup(int mx, int my);
void render_drag_ghost(SDL_Renderer *renderer);
void reset_drag_state(void);
int  is_dragging(void);

typedef enum { OVERLAY_NONE = 0, OVERLAY_SUCCESS, OVERLAY_FAIL } OverlayResult;
void trigger_overlay(OverlayResult result);
void render_overlay(SDL_Renderer *renderer,
                    const SDL_Rect *center_rect,
                    TTF_Font *font_large,
                    TTF_Font *font_small);

#define LORE_MAX_LINES  128
#define LORE_MAX_CHARS  128

typedef struct {
    char  lines[LORE_MAX_LINES][LORE_MAX_CHARS];
    int   line_count;
} WrappedText;

int wrap_text(TTF_Font *font, const char *text, int max_width, WrappedText *out);
void render_lore_panel(SDL_Renderer   *renderer,
                       TTF_Font       *font,
                       TTF_Font       *title_font,
                       const char     *title,
                       const char     *body,
                       const SDL_Rect *panel,
                       SDL_Color       text_color,
                       SDL_Color       title_color);

typedef struct {
    const char *title;
    const char *briefing;
} MissionLore;

extern const MissionLore g_missions[5];

extern RotozoomAnim g_rotozoom_success;
extern RotozoomAnim g_rotozoom_fail;

void render_rotozoom_overlay(SDL_Renderer *renderer,
                              SDL_Texture *success_tex,
                              SDL_Texture *fail_tex,
                              RotozoomAnim *success_anim,
                              RotozoomAnim *fail_anim,
                              TTF_Font *font_large,
                              TTF_Font *font_small);

#endif
