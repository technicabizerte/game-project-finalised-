#include "enigme2.h"

SDL_Window   *g_win = NULL;
SDL_Renderer *g_ren = NULL;

static SDL_Texture *g_bg = NULL;
static SDL_Texture *g_success = NULL;
static SDL_Texture *g_fail = NULL;
static SDL_Texture *g_timer_icon = NULL;
static SDL_Texture *g_target_shadow = NULL;
static SDL_Texture *g_btn_next = NULL;

static int g_fullscreen = 0;

static SDL_Texture *g_puzzle[NB_IMAGES_PUZZLE] = {NULL};
static SDL_Texture *g_prop[NB_IMAGES_PUZZLE][NB_PROPOSITIONS] = {{NULL}};
static SDL_Texture *g_piece_tex = NULL;

SDL_Texture *g_card_textures[3] = {NULL};

static int g_running = 1;
static int g_current = 0;
static int g_selected = -1;

static int g_dragging = 0;
static int g_drag_off_x = 0;
static int g_drag_off_y = 0;
static int g_hover_prop = -1;
static int g_hover_next_btn = 0;

static Uint32 g_feedback_until = 0;
static int g_feedback_ok = 0;
static float g_rotozoom_angle = 0;
static float g_rotozoom_scale = 1.0f;
static int g_showing_message = 0;
static Uint32 g_message_start = 0;
static float g_pulse_phase = 0.0f;

static Uint32 g_state_end_time_ms = 0;
static int g_state_pending = 0;

static SDL_Texture *g_preview_tex = NULL;
static int g_preview_card_idx = -1;

static int g_popup_active = 0;
static Uint32 g_popup_until = 0;
static char g_popup_message[128] = "";

static float g_piece_visual_x = 0;
static float g_piece_visual_y = 0;

static TTF_Font *g_font_body = NULL;
static TTF_Font *g_font_title = NULL;
static TTF_Font *g_font_hud = NULL;
int g_drag_enabled = 1;
int g_dragged_prop_index = -1;

RotozoomAnim g_rotozoom_success = {0};
RotozoomAnim g_rotozoom_fail   = {0};

static int inside(int x, int y, SDL_Rect r)
{
    return (x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h);
}

static SDL_Texture *load_tex(const char *path)
{
    SDL_Surface *s = IMG_Load(path);
    SDL_Texture *t;
    if (!s)
    {
        printf("[WARN] IMG_Load failed: %s (%s)\n", path, IMG_GetError());
        return NULL;
    }
    t = SDL_CreateTextureFromSurface(g_ren, s);
    SDL_FreeSurface(s);
    if (!t) printf("[WARN] CreateTexture failed: %s\n", SDL_GetError());
    return t;
}

static TTF_Font *load_font(const char *path, int size)
{
    TTF_Font *font = TTF_OpenFont(path, size);
    if (!font)
    {
        printf("[WARN] TTF_OpenFont failed: %s (%s)\n", path, TTF_GetError());
    }
    return font;
}

static void destroy_all_textures(void)
{
    int i, j;
    for (i = 0; i < NB_IMAGES_PUZZLE; i++)
    {
        if (g_puzzle[i]) SDL_DestroyTexture(g_puzzle[i]);
        g_puzzle[i] = NULL;
        for (j = 0; j < NB_PROPOSITIONS; j++)
        {
            if (g_prop[i][j]) SDL_DestroyTexture(g_prop[i][j]);
            g_prop[i][j] = NULL;
        }
    }

    if (g_bg) SDL_DestroyTexture(g_bg);
    if (g_success) SDL_DestroyTexture(g_success);
    if (g_fail) SDL_DestroyTexture(g_fail);
    if (g_timer_icon) SDL_DestroyTexture(g_timer_icon);
    if (g_target_shadow) SDL_DestroyTexture(g_target_shadow);
    if (g_btn_next) SDL_DestroyTexture(g_btn_next);
    g_bg = NULL; g_success = NULL; g_fail = NULL;
    g_timer_icon = NULL; g_target_shadow = NULL; g_btn_next = NULL;
}

static void destroy_fonts(void)
{
    if (g_font_body) TTF_CloseFont(g_font_body);
    if (g_font_title) TTF_CloseFont(g_font_title);
    if (g_font_hud) TTF_CloseFont(g_font_hud);
    g_font_body = NULL;
    g_font_title = NULL;
    g_font_hud = NULL;
}

static int init_sdl(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        printf("[ERROR] SDL_Init failed: %s\n", SDL_GetError());
        return 0;
    }

    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG))
    {
        printf("[ERROR] IMG_Init failed: %s\n", IMG_GetError());
        SDL_Quit();
        return 0;
    }

    if (TTF_Init() != 0)
    {
        printf("[ERROR] TTF_Init failed: %s\n", TTF_GetError());
        IMG_Quit();
        SDL_Quit();
        return 0;
    }

    g_win = SDL_CreateWindow("Snowpiercer: Revolt - Tactical Command",
                             SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    if (!g_win)
    {
        printf("[ERROR] SDL_CreateWindow failed: %s\n", SDL_GetError());
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
        return 0;
    }

    g_fullscreen = 0;

    g_ren = SDL_CreateRenderer(g_win, -1,
                               SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_ren)
    {
        printf("[ERROR] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_win);
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
        return 0;
    }

    SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);
    return 1;
}

static void quit_sdl(void)
{
    destroy_all_textures();
    destroy_fonts();

    if (g_ren) {  }
    if (g_win) {  }
    g_ren = NULL; g_win = NULL;
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
}

static void draw_rivet(int x, int y, int size)
{
    SDL_Rect rivet_outer = {x - size, y - size, size * 2, size * 2};
    SDL_Rect rivet_inner = {x - size/2, y - size/2, size, size};

    SDL_SetRenderDrawColor(g_ren, COLOR_RIVET_DARK);
    SDL_RenderFillRect(g_ren, &rivet_outer);

    SDL_SetRenderDrawColor(g_ren, COLOR_RIVET);
    SDL_RenderFillRect(g_ren, &rivet_inner);
}

static void draw_tactical_frame(SDL_Renderer *ren, SDL_Rect r)
{
    (void)ren;

    SDL_SetRenderDrawColor(g_ren, COLOR_PANEL_BG);
    SDL_RenderFillRect(g_ren, &r);

    SDL_SetRenderDrawColor(g_ren, COLOR_FRAME_BORDER);
    SDL_RenderDrawRect(g_ren, &r);

    SDL_Rect inner_border = {r.x + 1, r.y + 1, r.w - 2, r.h - 2};
    SDL_SetRenderDrawColor(g_ren, COLOR_FRAME_DIM);
    SDL_RenderDrawRect(g_ren, &inner_border);

    int rivet_size = 4;
    int offset = 6;

    draw_rivet(r.x + offset, r.y + offset, rivet_size);
    draw_rivet(r.x + r.w - offset, r.y + offset, rivet_size);
    draw_rivet(r.x + offset, r.y + r.h - offset, rivet_size);
    draw_rivet(r.x + r.w - offset, r.y + r.h - offset, rivet_size);
}

static void draw_pulsing_glow(SDL_Rect r, Uint8 r_col, Uint8 g_col, Uint8 b_col)
{
    float pulse = 0.5f + 0.5f * sinf(g_pulse_phase);
    Uint8 alpha = (Uint8)(60 + pulse * 60);

    SDL_SetRenderDrawColor(g_ren, r_col, g_col, b_col, alpha);

    for (int i = 1; i <= 3; i++)
    {
        SDL_Rect glow = {
            r.x - i * 4,
            r.y - i * 4,
            r.w + i * 8,
            r.h + i * 8
        };
        SDL_SetRenderDrawColor(g_ren, r_col, g_col, b_col, alpha / (i + 1));
        SDL_RenderDrawRect(g_ren, &glow);
    }
}

void init_joueur(t_joueur *j)
{
    j->score = 0;
    j->vies = VIES_DEPART;
    j->niveau = 1;
}

int charger_images_puzzle(t_enigme2 *e, char *dossier)
{
    int i;
    char base[TAILLE_CHEMIN];

    e->nb_images = NB_IMAGES_PUZZLE;
    snprintf(base, sizeof(base), "%s/puzzles", dossier);

    for (i = 0; i < NB_IMAGES_PUZZLE; i++)
    {
        e->tab_images[i].id = i + 1;
        e->tab_images[i].deja_utilisee = 0;

        snprintf(e->tab_images[i].chemin_image, TAILLE_CHEMIN,
                 "%s/puzzle_%d_board.png", base, i + 1);
        snprintf(e->tab_images[i].chemin_propositions[0], TAILLE_CHEMIN,
                 "%s/puzzle_%d_prop_A.png", base, i + 1);
        snprintf(e->tab_images[i].chemin_propositions[1], TAILLE_CHEMIN,
                 "%s/puzzle_%d_prop_B.png", base, i + 1);
        snprintf(e->tab_images[i].chemin_propositions[2], TAILLE_CHEMIN,
                 "%s/puzzle_%d_prop_C.png", base, i + 1);
    }

    e->tab_images[0].bonne_proposition = 0;
    e->tab_images[1].bonne_proposition = 1;
    e->tab_images[2].bonne_proposition = 2;
    e->tab_images[3].bonne_proposition = 0;
    e->tab_images[4].bonne_proposition = 1;

    return 1;
}

void init_enigme2(t_enigme2 *e, char *dossier)
{
    memset(e, 0, sizeof(*e));
    init_joueur(&e->joueur);
    charger_images_puzzle(e, dossier);

    e->puzzles_completed = 0;
    e->puzzles_correct = 0;
    e->puzzles_wrong = 0;
    e->popup_active = 0;

    e->piece.x = PANEL_STATUS_X + PANEL_STATUS_W / 2 - 45;
    e->piece.y = PANEL_STATUS_Y + PANEL_STATUS_H / 2 - 45;
    e->piece.largeur = 90;
    e->piece.hauteur = 90;
    e->piece.bien_placee = 0;
    e->piece.snap_x = TARGET_X;
    e->piece.snap_y = TARGET_Y;

    g_piece_visual_x = e->piece.x;
    g_piece_visual_y = e->piece.y;

    e->en_drag = 0;
    e->etat = MENU;
}

int toutes_images_utilisees(t_enigme2 *e)
{
    int i;
    for (i = 0; i < e->nb_images; i++)
        if (!e->tab_images[i].deja_utilisee) return 0;
    return 1;
}

void reinit_images_utilisees(t_enigme2 *e)
{
    int i;
    for (i = 0; i < e->nb_images; i++) e->tab_images[i].deja_utilisee = 0;
}

int image_aleatoire(t_enigme2 *e)
{
    int idx;
    if (toutes_images_utilisees(e)) reinit_images_utilisees(e);

    do { idx = rand() % e->nb_images; }
    while (e->tab_images[idx].deja_utilisee);

    e->tab_images[idx].deja_utilisee = 1;
    return idx;
}

void afficher_image_puzzle(t_image_puzzle *img)
{
    printf("[INFO] Puzzle: %s\n", img->chemin_image);
}

void afficher_propositions(t_image_puzzle *img)
{
    printf("[INFO] Propositions:\n  %s\n  %s\n  %s\n",
           img->chemin_propositions[0],
           img->chemin_propositions[1],
           img->chemin_propositions[2]);
}

void gerer_clic_souris(t_enigme2 *e, int x, int y)
{
    e->souris_x = x;
    e->souris_y = y;

    if (!g_drag_enabled) return;

    SDL_Rect piece_rect = {e->piece.x, e->piece.y, e->piece.largeur, e->piece.hauteur};
    if (inside(x, y, piece_rect))
    {
        e->en_drag = 1;
        g_dragging = 1;
        g_drag_off_x = x - e->piece.x;
        g_drag_off_y = y - e->piece.y;
    }
}

void deplacer_piece(t_enigme2 *e, int x, int y)
{
    if (g_dragging && e->etat == EN_JEU)
    {
        e->piece.x = x - g_drag_off_x;
        e->piece.y = y - g_drag_off_y;
    }
}

int verifier_position_piece(t_piece *p, int cible_x, int cible_y)
{
    int dx = abs(p->x - cible_x);
    int dy = abs(p->y - cible_y);
    return (dx <= 25 && dy <= 25);
}

void relacher_piece(t_enigme2 *e, int x, int y)
{
    (void)x; (void)y;
    g_dragging = 0;
    e->en_drag = 0;

    if (e->etat != EN_JEU || !g_drag_enabled) return;

    if (verifier_position_piece(&e->piece, TARGET_X, TARGET_Y))
    {
        e->piece.x = TARGET_X;
        e->piece.y = TARGET_Y;
        e->piece.bien_placee = 1;
        e->joueur.score += 10;
        e->puzzles_completed++;
        e->puzzles_correct++;
        e->etat = GAGNE;
        g_showing_message = 1;
        g_message_start = SDL_GetTicks();
        g_feedback_ok = 1;
        g_feedback_until = SDL_GetTicks() + 500;
        trigger_overlay(OVERLAY_SUCCESS);
        printf("[INFO] Puzzle solved! Completed: %d/%d\n", e->puzzles_completed, MAX_PUZZLES_PER_SESSION);
    }
}
void lancer_chrono(t_chrono *c, int duree)
{
    c->debut = (int)SDL_GetTicks();
    c->duree = duree;
    c->reste = duree;
}

int temps_restant(t_chrono *c)
{
    int ecoule = ((int)SDL_GetTicks() - c->debut) / 1000;
    int r = c->duree - ecoule;
    if (r < 0) r = 0;
    c->reste = r;
    return r;
}

int temps_ecoule(t_chrono *c) { return temps_restant(c) <= 0; }
void animation_chrono(t_chrono *c) { (void)c; }

void effet_rotozoom(char *msg) { printf("[INFO] %s\n", msg); }
void message_succes(void) { effet_rotozoom("SUCCESS!"); }
void message_echec(void) { effet_rotozoom("FAILED!"); }

static void load_assets(t_enigme2 *e)
{
    int i, j;

    printf("[INFO] Loading tactical assets...\n");

    g_bg = load_tex("assets/ui/background.png");
    if (!g_bg) g_bg = load_tex("assets/ui/background.PNG");

    g_success = load_tex("assets/ui/success.png");
    if (!g_success) g_success = load_tex("assets/ui/success.PNG");

    g_fail = load_tex("assets/ui/fail.png");
    if (!g_fail) g_fail = load_tex("assets/ui/fail.PNG");

    g_timer_icon = load_tex("assets/ui/timer_bar.png");
    if (!g_timer_icon) g_timer_icon = load_tex("assets/ui/timer_bar.PNG");

    g_target_shadow = load_tex("assets/ui/target_shadow.png");
    if (!g_target_shadow) g_target_shadow = load_tex("assets/ui/target_shadow.PNG");

    g_btn_next = load_tex("assets/ui/btn_next.png");
    if (!g_btn_next) g_btn_next = load_tex("assets/ui/btn_next.PNG");

    for (i = 0; i < e->nb_images; i++)
    {
        g_puzzle[i] = load_tex(e->tab_images[i].chemin_image);
        for (j = 0; j < NB_PROPOSITIONS; j++)
        {
            g_prop[i][j] = load_tex(e->tab_images[i].chemin_propositions[j]);
        }
    }

    for (j = 0; j < 3; j++)
    {
        g_card_textures[j] = g_prop[g_current][j];
    }

    g_font_body = load_font("assets/fonts/arial.ttf", 11);
    if (!g_font_body) g_font_body = load_font("assets/fonts/Arial.ttf", 11);
    if (!g_font_body) g_font_body = load_font("assets/fonts/mono.ttf", 11);
    if (!g_font_body) g_font_body = load_font("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 11);

    g_font_title = load_font("assets/fonts/arial.ttf", 12);
    if (!g_font_title) g_font_title = load_font("assets/fonts/Arial.ttf", 12);
    if (!g_font_title) g_font_title = load_font("assets/fonts/mono.ttf", 12);
    if (!g_font_title) g_font_title = load_font("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 12);

    g_font_hud = load_font("assets/fonts/arial.ttf", 28);
    if (!g_font_hud) g_font_hud = load_font("assets/fonts/Arial.ttf", 28);
    if (!g_font_hud) g_font_hud = load_font("assets/fonts/mono.ttf", 28);
    if (!g_font_hud) g_font_hud = load_font("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 28);

    if (g_font_body && g_font_title && g_font_hud)
        printf("[INFO] Fonts loaded successfully!\n");
    else
        printf("[WARN] Some fonts failed to load - text rendering may be limited\n");

    printf("[INFO] Assets loaded!\n");
}

static void show_popup(const char *msg)
{
    strncpy(g_popup_message, msg, sizeof(g_popup_message) - 1);
    g_popup_message[sizeof(g_popup_message) - 1] = '\0';
    g_popup_active = 1;
    g_popup_until = SDL_GetTicks() + 2000;
    printf("[POPUP] %s\n", msg);
}

static void new_round(t_enigme2 *e)
{

    if (e->puzzles_completed >= MAX_PUZZLES_PER_SESSION)
    {
        e->etat = FIN_JEU;
        printf("[INFO] Game Over! Final Score: %d\n", e->joueur.score);
        return;
    }

    g_current = e->puzzles_completed;
    g_selected = -1;
    g_piece_tex = NULL;
    g_drag_enabled = 1;
    g_showing_message = 0;
    g_rotozoom_angle = 0;
    g_rotozoom_scale = 1.0f;
    g_state_pending = 0;
    g_state_end_time_ms = 0;
    g_dragged_prop_index = -1;
    g_preview_tex = NULL;
    g_preview_card_idx = -1;

    rz_anim_reset(&g_rotozoom_success);
    rz_anim_reset(&g_rotozoom_fail);

    reset_drag_state();

    e->piece.x = PANEL_STATUS_X + PANEL_STATUS_W / 2 - 45;
    e->piece.y = PANEL_STATUS_Y + PANEL_STATUS_H / 2 - 45;
    g_piece_visual_x = e->piece.x;
    g_piece_visual_y = e->piece.y;

    e->piece.largeur = 90;
    e->piece.hauteur = 90;
    e->piece.bien_placee = 0;
    e->etat = EN_JEU;

    for (int j = 0; j < 3; j++)
    {
        g_card_textures[j] = g_prop[g_current][j];
    }

    lancer_chrono(&e->chrono, TEMPS_ENIGME);

    printf("[INFO] New tactical round! Puzzle %d (Completed: %d/%d)\n",
           g_current + 1, e->puzzles_completed, MAX_PUZZLES_PER_SESSION);
}

static void process_events(t_enigme2 *e)
{
    SDL_Event ev;
    SDL_Rect card_rects[NB_PROPOSITIONS] = {
        {CARD_X, CARD_1_Y, CARD_W, CARD_H},
        {CARD_X, CARD_2_Y, CARD_W, CARD_H},
        {CARD_X, CARD_3_Y, CARD_W, CARD_H}
    };
    int i;

    while (SDL_PollEvent(&ev))
    {
        if (ev.type == SDL_QUIT) { SDL_PushEvent(&ev); g_running = 0; }

        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
        {
            g_fullscreen = !g_fullscreen;
            if (g_fullscreen)
            {
                SDL_SetWindowFullscreen(g_win, SDL_WINDOW_FULLSCREEN_DESKTOP);
            }
            else
            {
                SDL_SetWindowFullscreen(g_win, 0);
                SDL_SetWindowSize(g_win, WIN_W, WIN_H);
                SDL_SetWindowPosition(g_win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            }
            printf("[INFO] Fullscreen: %s\n", g_fullscreen ? "ON" : "OFF");
        }

        if (g_state_pending) continue;

        if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT)
        {
            int mx = ev.button.x, my = ev.button.y;

            if (e->etat == EN_JEU) {
                int card_idx = -1;
                for (int ci = 0; ci < NB_PROPOSITIONS; ci++) {
                    SDL_Rect cr = {CARD_X, CARD_1_Y + ci * (CARD_H + CARD_SPACING),
                                   CARD_W, CARD_H};
                    if (inside(mx, my, cr)) {
                        card_idx = ci;
                        break;
                    }
                }
                if (card_idx >= 0) {
                    g_preview_card_idx = card_idx;
                    g_preview_tex = g_card_textures[card_idx];
                }
            }

            handle_mousedown(mx, my);

            SDL_Rect next_btn = {BTN_NEXT_X, BTN_NEXT_Y, BTN_NEXT_W, BTN_NEXT_H};

            if (inside(mx, my, next_btn))
            {
                if (e->etat == GAGNE || e->etat == PERDU)
                {
                    new_round(e);
                    continue;
                }
                else if (e->etat == EN_JEU)
                {
                    show_popup("Drag a proposition to the target zone!");
                }
            }

            if (e->etat == GAGNE || e->etat == PERDU)
            {
                new_round(e);
                continue;
            }
        }

        if (ev.type == SDL_MOUSEMOTION)
        {
            int mx = ev.motion.x, my = ev.motion.y;

            handle_mousemotion(mx, my);

            g_hover_prop = -1;
            for (i = 0; i < NB_PROPOSITIONS; i++)
            {
                if (inside(mx, my, card_rects[i])) g_hover_prop = i;
            }

            SDL_Rect next_btn = {BTN_NEXT_X, BTN_NEXT_Y, BTN_NEXT_W, BTN_NEXT_H};
            g_hover_next_btn = inside(mx, my, next_btn) ? 1 : 0;

            deplacer_piece(e, mx, my);
        }

        if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT)
        {
            int mx = ev.button.x, my = ev.button.y;
            DragResult result = handle_mouseup(mx, my);

            if (result != DRAG_NONE && result != DRAG_CANCEL && e->etat == EN_JEU)
            {

                int bonne = e->tab_images[g_current].bonne_proposition;

                if (g_dragged_prop_index == bonne)
                {

                    e->joueur.score += 10;
                    e->puzzles_completed++;
                    e->puzzles_correct++;
                    e->etat = GAGNE;
                    g_showing_message = 1;
                    g_message_start = SDL_GetTicks();
                    g_feedback_ok = 1;
                    g_feedback_until = SDL_GetTicks() + 500;
                    rz_anim_trigger_success(&g_rotozoom_success);

                    g_state_end_time_ms = SDL_GetTicks() + 1000;
                    g_state_pending = 1;
                    printf("[INFO] Puzzle solved! Auto-advance in 1.0s\n");
                }
                else
                {

                    e->joueur.vies--;
                    e->puzzles_wrong++;
                    g_feedback_ok = 0;
                    g_feedback_until = SDL_GetTicks() + 500;
                    rz_anim_trigger_fail(&g_rotozoom_fail);
                    printf("[INFO] Wrong proposition! Lives: %d\n", e->joueur.vies);

                    if (e->joueur.vies <= 0)
                    {
                        e->etat = FIN_JEU;
                        g_showing_message = 1;
                        g_message_start = SDL_GetTicks();
                        g_state_pending = 0;
                    }
                    else
                    {
                        e->etat = PERDU;

                        g_state_end_time_ms = SDL_GetTicks() + 1200;
                        g_state_pending = 1;
                        printf("[INFO] Auto-advance in 1.2s\n");
                    }
                }
            }
        }
    }
}

static void update_game(t_enigme2 *e)
{
    Uint32 now = SDL_GetTicks();

    g_pulse_phase += 0.1f;

    if (g_state_pending && now >= g_state_end_time_ms)
    {
        g_state_pending = 0;
        g_state_end_time_ms = 0;

        if (e->etat == GAGNE || e->etat == PERDU)
        {
            printf("[INFO] Auto-advancing to next puzzle...\n");
            new_round(e);
            return;
        }
    }

    if (rz_anim_is_active(&g_rotozoom_success)) {
        rz_anim_update(&g_rotozoom_success, now);
    }
    if (rz_anim_is_active(&g_rotozoom_fail)) {
        rz_anim_update(&g_rotozoom_fail, now);
    }

    g_piece_visual_x += (e->piece.x - g_piece_visual_x) * 0.25f;
    g_piece_visual_y += (e->piece.y - g_piece_visual_y) * 0.25f;

    if (e->etat == EN_JEU && temps_ecoule(&e->chrono) && !g_state_pending)
    {

        e->joueur.vies--;
        e->puzzles_wrong++;
        e->puzzles_completed++;
        rz_anim_trigger_fail(&g_rotozoom_fail);
        e->etat = PERDU;
        printf("[INFO] TIME CRITICAL - %d lives remaining\n", e->joueur.vies);

        if (e->joueur.vies <= 0)
        {
            e->etat = FIN_JEU;
            g_showing_message = 1;
            g_message_start = now;
            g_state_pending = 0;
        }
        else
        {

            g_state_end_time_ms = now + 1200;
            g_state_pending = 1;
            printf("[INFO] Auto-advance in 1.2s\n");
        }
    }
}

static void draw_hp_panel(int lives)
{
    SDL_Rect panel = {PANEL_HP_X, PANEL_HP_Y, PANEL_HP_W, PANEL_HP_H};
    draw_tactical_frame(g_ren, panel);

    SDL_Rect title_bg = {panel.x + 8, panel.y + 8, panel.w - 16, 18};
    SDL_SetRenderDrawColor(g_ren, 40, 50, 60, 255);
    SDL_RenderFillRect(g_ren, &title_bg);

    if (g_font_hud)
    {
        SDL_Color text_color = {255, 60, 60, 255};
        SDL_Surface *surf = TTF_RenderText_Blended(g_font_body, "hp bar", text_color);
        if (surf)
        {
            SDL_Texture *tex = SDL_CreateTextureFromSurface(g_ren, surf);
            if (tex)
            {
                SDL_Rect dst = {
                    title_bg.x + (title_bg.w - surf->w) / 2,
                    title_bg.y + (title_bg.h - surf->h) / 2,
                    surf->w, surf->h
                };
                SDL_RenderCopy(g_ren, tex, NULL, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surf);
        }
    }

    int bar_w = (panel.w - 30) / VIES_DEPART;
    int bar_h = 12;
    int bar_y = panel.y + 28;

    for (int i = 0; i < VIES_DEPART; i++)
    {
        SDL_Rect bar_bg = {
            panel.x + 12 + i * (bar_w + 5),
            bar_y,
            bar_w,
            bar_h
        };

        if (i < lives)
        {
            if (lives == 1)
                SDL_SetRenderDrawColor(g_ren, COLOR_HP_CRITICAL);
            else if (lives == 2)
                SDL_SetRenderDrawColor(g_ren, COLOR_HP_LOW);
            else
                SDL_SetRenderDrawColor(g_ren, COLOR_HP_FULL);
            SDL_RenderFillRect(g_ren, &bar_bg);
        }
        else
        {
            SDL_SetRenderDrawColor(g_ren, 40, 45, 50, 255);
            SDL_RenderFillRect(g_ren, &bar_bg);
        }

        SDL_SetRenderDrawColor(g_ren, 80, 90, 100, 255);
        SDL_RenderDrawRect(g_ren, &bar_bg);
    }
}

static void draw_timer_bar(int remaining)
{
    SDL_Rect timer_bg = {TIMER_X, TIMER_Y, TIMER_W, TIMER_H};
    SDL_Rect timer_fill = timer_bg;
    float ratio = (float)remaining / TEMPS_ENIGME;
    timer_fill.w = (int)(TIMER_W * ratio);

    draw_tactical_frame(g_ren, timer_bg);

    if (ratio > 0.5f)
        SDL_SetRenderDrawColor(g_ren, COLOR_TIMER_GOOD);
    else if (ratio > 0.25f)
        SDL_SetRenderDrawColor(g_ren, COLOR_TIMER_WARN);
    else
        SDL_SetRenderDrawColor(g_ren, COLOR_TIMER_DANGER);

    SDL_Rect fill_inset = {
        timer_bg.x + 4,
        timer_bg.y + 4,
        timer_fill.w - 8,
        timer_bg.h - 8
    };
    if (fill_inset.w > 0)
        SDL_RenderFillRect(g_ren, &fill_inset);
}

static void draw_puzzle_panel(t_enigme2 *e)
{
    (void)e;
    SDL_Rect panel = {PANEL_PUZZLE_X, PANEL_PUZZLE_Y,
                      PANEL_PUZZLE_W, PANEL_PUZZLE_H};
    draw_tactical_frame(g_ren, panel);

    SDL_Rect puzzle_display = {
        panel.x + 10,
        panel.y + 10,
        panel.w - 20,
        panel.h - 20
    };

    SDL_SetRenderDrawColor(g_ren, 15, 18, 22, 255);
    SDL_RenderFillRect(g_ren, &puzzle_display);

    if (g_puzzle[g_current])
    {
        SDL_RenderCopy(g_ren, g_puzzle[g_current], NULL, &puzzle_display);
    }

    SDL_Rect target = {TARGET_X, TARGET_Y, TARGET_SIZE, TARGET_SIZE};

    float pulse = 1.0f + 0.08f * sinf(g_pulse_phase * 0.5f);
    int offset = (int)((TARGET_SIZE * (pulse - 1.0f)) / 2);

    SDL_Rect pulse_rect = {
        TARGET_X - offset,
        TARGET_Y - offset,
        TARGET_SIZE + offset * 2,
        TARGET_SIZE + offset * 2
    };

    if (g_target_shadow)
    {
        SDL_RenderCopy(g_ren, g_target_shadow, NULL, &pulse_rect);
    }
    else
    {
        SDL_SetRenderDrawColor(g_ren, COLOR_TARGET);
        SDL_RenderFillRect(g_ren, &pulse_rect);
    }

    SDL_SetRenderDrawColor(g_ren, COLOR_FRAME_BORDER);
    SDL_RenderDrawRect(g_ren, &target);

    SDL_SetRenderDrawColor(g_ren, COLOR_FRAME_DIM);
    SDL_RenderDrawLine(g_ren, TARGET_X + TARGET_SIZE/2, TARGET_Y + 10,
                       TARGET_X + TARGET_SIZE/2, TARGET_Y + TARGET_SIZE - 10);
    SDL_RenderDrawLine(g_ren, TARGET_X + 10, TARGET_Y + TARGET_SIZE/2,
                       TARGET_X + TARGET_SIZE - 10, TARGET_Y + TARGET_SIZE/2);

    int bracket_len = 15;
    SDL_SetRenderDrawColor(g_ren, COLOR_FRAME_BORDER);
    SDL_RenderDrawLine(g_ren, target.x, target.y, target.x + bracket_len, target.y);
    SDL_RenderDrawLine(g_ren, target.x, target.y, target.x, target.y + bracket_len);
    SDL_RenderDrawLine(g_ren, target.x + target.w, target.y,
                       target.x + target.w - bracket_len, target.y);
    SDL_RenderDrawLine(g_ren, target.x + target.w, target.y,
                       target.x + target.w, target.y + bracket_len);
    SDL_RenderDrawLine(g_ren, target.x, target.y + target.h,
                       target.x + bracket_len, target.y + target.h);
    SDL_RenderDrawLine(g_ren, target.x, target.y + target.h,
                       target.x, target.y + target.h - bracket_len);
    SDL_RenderDrawLine(g_ren, target.x + target.w, target.y + target.h,
                       target.x + target.w - bracket_len, target.y + target.h);
    SDL_RenderDrawLine(g_ren, target.x + target.w, target.y + target.h,
                       target.x + target.w, target.y + target.h - bracket_len);
}

static void draw_inventory_cards(t_enigme2 *e)
{
    SDL_Rect cards[NB_PROPOSITIONS] = {
        {CARD_X, CARD_1_Y, CARD_W, CARD_H},
        {CARD_X, CARD_2_Y, CARD_W, CARD_H},
        {CARD_X, CARD_3_Y, CARD_W, CARD_H}
    };

    for (int i = 0; i < NB_PROPOSITIONS; i++)
    {
        draw_tactical_frame(g_ren, cards[i]);

        if (i == g_selected)
        {
            if (g_feedback_ok)
                draw_pulsing_glow(cards[i], 100, 240, 180);
            else
                draw_pulsing_glow(cards[i], 255, 80, 80);
        }
        else if (i == g_hover_prop && e->etat == EN_JEU)
        {
            draw_pulsing_glow(cards[i], 80, 220, 255);
        }

        SDL_Rect card_inner = {
            cards[i].x + 8,
            cards[i].y + 8,
            cards[i].w - 16,
            cards[i].h - 16
        };

        SDL_SetRenderDrawColor(g_ren, 20, 24, 30, 255);
        SDL_RenderFillRect(g_ren, &card_inner);

        if (g_prop[g_current][i])
        {
            SDL_RenderCopy(g_ren, g_prop[g_current][i], NULL, &card_inner);
        }

        if (i == g_selected)
        {
            SDL_SetRenderDrawColor(g_ren, COLOR_FRAME_BORDER);
            SDL_Rect sel = {
                cards[i].x + 4,
                cards[i].y + 4,
                cards[i].w - 8,
                cards[i].h - 8
            };
            SDL_RenderDrawRect(g_ren, &sel);
        }
    }

    if (g_piece_tex && g_drag_enabled)
    {
        SDL_Rect piece_rect = {
            (int)g_piece_visual_x,
            (int)g_piece_visual_y,
            e->piece.largeur,
            e->piece.hauteur
        };

        if (is_dragging())
        {
            piece_rect.w = (int)(e->piece.largeur * 1.15f);
            piece_rect.h = (int)(e->piece.hauteur * 1.15f);
            piece_rect.x -= (piece_rect.w - e->piece.largeur) / 2;
            piece_rect.y -= (piece_rect.h - e->piece.hauteur) / 2;

            draw_pulsing_glow(piece_rect, 100, 200, 255);
        }

        SDL_RenderCopy(g_ren, g_piece_tex, NULL, &piece_rect);
    }

    SDL_Rect next_btn = {BTN_NEXT_X, BTN_NEXT_Y, BTN_NEXT_W, BTN_NEXT_H};
    draw_tactical_frame(g_ren, next_btn);

    if (g_hover_next_btn && e->etat == GAGNE)
    {
        draw_pulsing_glow(next_btn, 100, 240, 180);
    }

    SDL_Rect btn_inner = {
        next_btn.x + 6,
        next_btn.y + 6,
        next_btn.w - 12,
        next_btn.h - 12
    };
    SDL_SetRenderDrawColor(g_ren, 30, 35, 42, 255);
    SDL_RenderFillRect(g_ren, &btn_inner);

    if (g_font_title)
    {
        SDL_Color btn_color = g_hover_next_btn && e->etat == GAGNE ?
            (SDL_Color){100, 255, 180, 255} : (SDL_Color){220, 230, 240, 255};

        SDL_Surface *text_surf = TTF_RenderText_Blended(g_font_title, "NEXT", btn_color);
        if (text_surf)
        {
            SDL_Texture *text_tex = SDL_CreateTextureFromSurface(g_ren, text_surf);
            if (text_tex)
            {
                SDL_Rect text_rect;
                SDL_QueryTexture(text_tex, NULL, NULL, &text_rect.w, &text_rect.h);
                text_rect.x = next_btn.x + (next_btn.w - text_rect.w) / 2;
                text_rect.y = next_btn.y + (next_btn.h - text_rect.h) / 2;
                SDL_RenderCopy(g_ren, text_tex, NULL, &text_rect);
                SDL_DestroyTexture(text_tex);
            }
            SDL_FreeSurface(text_surf);
        }
    }
}

static void render_end_game_screen(t_enigme2 *e)
{
    SDL_SetRenderDrawColor(g_ren, COLOR_BG_DARK);
    SDL_RenderClear(g_ren);

    if (g_bg)
    {
        SDL_RenderCopy(g_ren, g_bg, NULL, NULL);
    }

    int panel_w = 500;
    int panel_h = 350;
    int panel_x = (WIN_W - panel_w) / 2;
    int panel_y = (WIN_H - panel_h) / 2;

    SDL_Rect panel = {panel_x, panel_y, panel_w, panel_h};
    draw_tactical_frame(g_ren, panel);

    if (g_font_title)
    {
        int is_game_over = (e->joueur.vies <= 0);
        const char *title_text = is_game_over ? "GAME OVER" : "MISSION COMPLETE";
        SDL_Color title_color = is_game_over ? (SDL_Color){255, 60, 60, 255} : (SDL_Color){255, 200, 80, 255};
        SDL_Surface *title_surf = TTF_RenderText_Blended(g_font_title, title_text, title_color);
        if (title_surf)
        {
            SDL_Texture *title_tex = SDL_CreateTextureFromSurface(g_ren, title_surf);
            if (title_tex)
            {
                SDL_Rect title_rect;
                SDL_QueryTexture(title_tex, NULL, NULL, &title_rect.w, &title_rect.h);
                title_rect.x = panel_x + (panel_w - title_rect.w) / 2;
                title_rect.y = panel_y + 30;
                SDL_RenderCopy(g_ren, title_tex, NULL, &title_rect);
                SDL_DestroyTexture(title_tex);
            }
            SDL_FreeSurface(title_surf);
        }
    }

    if (g_font_body)
    {
        char stats[256];
        snprintf(stats, sizeof(stats),
            "Puzzles Solved: %d / %d\n\nCorrect: %d\n\nWrong: %d\n\nFinal Score: %d",
            e->puzzles_correct,
            MAX_PUZZLES_PER_SESSION,
            e->puzzles_correct,
            e->puzzles_wrong,
            e->joueur.score);

        SDL_Color stats_color = {200, 230, 220, 255};
        SDL_Surface *stats_surf = TTF_RenderText_Blended(g_font_body, stats, stats_color);
        if (stats_surf)
        {
            SDL_Texture *stats_tex = SDL_CreateTextureFromSurface(g_ren, stats_surf);
            if (stats_tex)
            {
                SDL_Rect stats_rect;
                SDL_QueryTexture(stats_tex, NULL, NULL, &stats_rect.w, &stats_rect.h);
                stats_rect.x = panel_x + (panel_w - stats_rect.w) / 2;
                stats_rect.y = panel_y + 90;
                SDL_RenderCopy(g_ren, stats_tex, NULL, &stats_rect);
                SDL_DestroyTexture(stats_tex);
            }
            SDL_FreeSurface(stats_surf);
        }
    }

    if (g_font_body)
    {
        SDL_Color hint_color = {150, 160, 170, 255};
        SDL_Surface *hint_surf = TTF_RenderText_Blended(g_font_body,
            "Click or Press Any Key to Play Again", hint_color);
        if (hint_surf)
        {
            SDL_Texture *hint_tex = SDL_CreateTextureFromSurface(g_ren, hint_surf);
            if (hint_tex)
            {
                SDL_Rect hint_rect;
                SDL_QueryTexture(hint_tex, NULL, NULL, &hint_rect.w, &hint_rect.h);
                hint_rect.x = panel_x + (panel_w - hint_rect.w) / 2;
                hint_rect.y = panel_y + panel_h - 50;
                SDL_RenderCopy(g_ren, hint_tex, NULL, &hint_rect);
                SDL_DestroyTexture(hint_tex);
            }
            SDL_FreeSurface(hint_surf);
        }
    }

    SDL_RenderPresent(g_ren);
}

static void render_popup(void)
{
    if (!g_popup_active) return;

    if (SDL_GetTicks() > g_popup_until)
    {
        g_popup_active = 0;
        return;
    }

    int popup_w = 320;
    int popup_h = 60;
    int popup_x = (WIN_W - popup_w) / 2;
    int popup_y = 80;

    SDL_Rect popup_bg = {popup_x, popup_y, popup_w, popup_h};

    SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_ren, 40, 20, 20, 240);
    SDL_RenderFillRect(g_ren, &popup_bg);

    SDL_SetRenderDrawColor(g_ren, 255, 80, 80, 255);
    SDL_RenderDrawRect(g_ren, &popup_bg);

    if (g_font_body)
    {
        SDL_Color text_color = {255, 200, 200, 255};
        SDL_Surface *text_surf = TTF_RenderText_Blended(g_font_body,
            g_popup_message, text_color);
        if (text_surf)
        {
            SDL_Texture *text_tex = SDL_CreateTextureFromSurface(g_ren, text_surf);
            if (text_tex)
            {
                SDL_Rect text_rect;
                SDL_QueryTexture(text_tex, NULL, NULL, &text_rect.w, &text_rect.h);
                text_rect.x = popup_x + (popup_w - text_rect.w) / 2;
                text_rect.y = popup_y + (popup_h - text_rect.h) / 2;
                SDL_RenderCopy(g_ren, text_tex, NULL, &text_rect);
                SDL_DestroyTexture(text_tex);
            }
            SDL_FreeSurface(text_surf);
        }
    }
}

static void render_fit_texture(SDL_Renderer *ren, SDL_Texture *tex, SDL_Rect *bounds)
{
    if (!tex) return;

    int tw, th;
    SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
    if (tw <= 0 || th <= 0) return;

    float scale_w = (float)bounds->w / tw;
    float scale_h = (float)bounds->h / th;
    float scale = (scale_w < scale_h) ? scale_w : scale_h;

    int dw = (int)(tw * scale);
    int dh = (int)(th * scale);
    SDL_Rect dst = {
        bounds->x + (bounds->w - dw) / 2,
        bounds->y + (bounds->h - dh) / 2,
        dw, dh
    };
    SDL_RenderCopy(ren, tex, NULL, &dst);
}

static void draw_preview_stats_panel(t_enigme2 *e)
{
    SDL_Rect panel = {PANEL_STATUS_X, PANEL_STATUS_Y,
                      PANEL_STATUS_W, PANEL_STATUS_H};
    int rem = temps_restant(&e->chrono);

    draw_tactical_frame(g_ren, panel);

    int preview_margin = 8;
    SDL_Rect preview_frame = {
        panel.x + preview_margin,
        panel.y + preview_margin,
        panel.w - preview_margin * 2,
        80
    };

    SDL_SetRenderDrawColor(g_ren, COLOR_FRAME_BORDER);
    SDL_RenderDrawRect(g_ren, &preview_frame);

    SDL_Rect preview_inner = {
        preview_frame.x + 2,
        preview_frame.y + 2,
        preview_frame.w - 4,
        preview_frame.h - 4
    };
    SDL_SetRenderDrawColor(g_ren, 15, 18, 22, 255);
    SDL_RenderFillRect(g_ren, &preview_inner);

    if (g_preview_tex)
    {

        render_fit_texture(g_ren, g_preview_tex, &preview_inner);

        if (is_dragging())
        {
            draw_pulsing_glow(preview_frame, 80, 220, 255);
        }
    }
    else
    {

        if (g_font_body)
        {
            SDL_Color placeholder_color = {100, 110, 120, 255};
            SDL_Surface *surf = TTF_RenderText_Blended(g_font_body, "NO ITEM", placeholder_color);
            if (surf)
            {
                SDL_Texture *tex = SDL_CreateTextureFromSurface(g_ren, surf);
                if (tex)
                {
                    SDL_Rect dst = {
                        preview_inner.x + (preview_inner.w - surf->w) / 2,
                        preview_inner.y + (preview_inner.h - surf->h) / 2,
                        surf->w, surf->h
                    };
                    SDL_RenderCopy(g_ren, tex, NULL, &dst);
                    SDL_DestroyTexture(tex);
                }
                SDL_FreeSurface(surf);
            }
        }
    }

    int stats_y = preview_frame.y + preview_frame.h + 8;
    int line_h = 14;
    int stats_x = panel.x + 10;

    if (g_font_body)
    {

        char buf[64];
        SDL_Color stats_color = {180, 210, 200, 255};

        snprintf(buf, sizeof(buf), "Puzzle %d / %d", e->puzzles_completed + 1, MAX_PUZZLES_PER_SESSION);
        SDL_Surface *s1 = TTF_RenderText_Blended(g_font_body, buf, stats_color);
        if (s1) {
            SDL_Texture *t1 = SDL_CreateTextureFromSurface(g_ren, s1);
            if (t1) {
                SDL_Rect r1 = {stats_x, stats_y, s1->w, s1->h};
                SDL_RenderCopy(g_ren, t1, NULL, &r1);
                SDL_DestroyTexture(t1);
            }
            SDL_FreeSurface(s1);
        }

        snprintf(buf, sizeof(buf), "Lives: %d", e->joueur.vies);
        SDL_Color lives_color = (e->joueur.vies <= 1)
            ? (SDL_Color){255, 60, 60, 255}
            : (SDL_Color){60, 200, 100, 255};
        SDL_Surface *s2 = TTF_RenderText_Blended(g_font_body, buf, lives_color);
        if (s2) {
            SDL_Texture *t2 = SDL_CreateTextureFromSurface(g_ren, s2);
            if (t2) {
                SDL_Rect r2 = {stats_x, stats_y + line_h, s2->w, s2->h};
                SDL_RenderCopy(g_ren, t2, NULL, &r2);
                SDL_DestroyTexture(t2);
            }
            SDL_FreeSurface(s2);
        }

        SDL_Color score_color = {220, 230, 240, 255};
        snprintf(buf, sizeof(buf), "Score: %d", e->joueur.score);
        SDL_Surface *s3 = TTF_RenderText_Blended(g_font_body, buf, score_color);
        if (s3) {
            SDL_Texture *t3 = SDL_CreateTextureFromSurface(g_ren, s3);
            if (t3) {
                SDL_Rect r3 = {stats_x, stats_y + line_h * 2, s3->w, s3->h};
                SDL_RenderCopy(g_ren, t3, NULL, &r3);
                SDL_DestroyTexture(t3);
            }
            SDL_FreeSurface(s3);
        }

        SDL_Color timer_color = (rem <= 5)
            ? (SDL_Color){255, 60, 60, 255}
            : (rem <= 15)
                ? (SDL_Color){255, 190, 50, 255}
                : (SDL_Color){80, 200, 100, 255};
        snprintf(buf, sizeof(buf), "Time: %ds", rem);
        SDL_Surface *s4 = TTF_RenderText_Blended(g_font_body, buf, timer_color);
        if (s4) {
            SDL_Texture *t4 = SDL_CreateTextureFromSurface(g_ren, s4);
            if (t4) {
                SDL_Rect r4 = {stats_x, stats_y + line_h * 3, s4->w, s4->h};
                SDL_RenderCopy(g_ren, t4, NULL, &r4);
                SDL_DestroyTexture(t4);
            }
            SDL_FreeSurface(s4);
        }
    }
}

static void render_game(t_enigme2 *e)
{
    int rem = temps_restant(&e->chrono);

    SDL_SetRenderDrawColor(g_ren, COLOR_BG_DARK);
    SDL_RenderClear(g_ren);

    if (g_bg)
    {
        SDL_RenderCopy(g_ren, g_bg, NULL, NULL);
    }

    draw_hp_panel(e->joueur.vies);

    SDL_Rect lore_panel = {PANEL_OBJECTIVE_X, PANEL_OBJECTIVE_Y,
                           PANEL_OBJECTIVE_W, PANEL_OBJECTIVE_H};
    draw_tactical_frame(g_ren, lore_panel);

    if (g_font_body && g_font_title && g_current < 5)
    {
        SDL_Color body_color = {160, 220, 200, 255};
        SDL_Color title_color = {0, 255, 200, 255};
        render_lore_panel(g_ren, g_font_body, g_font_title,
                         g_missions[g_current].title,
                         g_missions[g_current].briefing,
                         &lore_panel, body_color, title_color);
    }

    draw_preview_stats_panel(e);

    draw_timer_bar(rem);
    draw_puzzle_panel(e);

    draw_inventory_cards(e);

    render_drag_ghost(g_ren);

    int win_w, win_h;
    SDL_RenderGetLogicalSize(g_ren, &win_w, &win_h);
    if (win_w == 0 || win_h == 0) SDL_GetWindowSize(g_win, &win_w, &win_h);

    SDL_Rect center_rect = {0, 0, win_w, win_h};
    render_overlay(g_ren, &center_rect, g_font_hud, g_font_body);

    render_rotozoom_overlay(g_ren, g_success, g_fail,
                            &g_rotozoom_success, &g_rotozoom_fail,
                            g_font_hud, g_font_body);

    if (SDL_GetTicks() < g_feedback_until)
    {
        if (g_feedback_ok)
            SDL_SetRenderDrawColor(g_ren, 0, 255, 120, 50);
        else
            SDL_SetRenderDrawColor(g_ren, 255, 40, 40, 50);
        SDL_Rect full = {0, 0, win_w, win_h};
        SDL_RenderFillRect(g_ren, &full);
    }

    render_popup();

    SDL_RenderPresent(g_ren);
}

int run_enigme2(void)
{
    t_enigme2 e;
    int restart = 0;

    printf("=================================\n");
    printf("   SNOWPIERCER: REVOLT\n");
    printf("   TACTICAL COMMAND INTERFACE\n");
    printf("=================================\n");

    srand((unsigned int)time(NULL));

    if (!init_sdl())
    {
        printf("[ERROR] SDL initialization failed!\n");
        return 1;
    }

    init_enigme2(&e, "assets");
    load_assets(&e);
    new_round(&e);

    printf("[INFO] Tactical system online.\n");
    printf("[INFO] Select inventory card -> Drag to target zone.\n");

    while (g_running)
    {

        if (e.etat == FIN_JEU)
        {
            render_end_game_screen(&e);

            SDL_Event ev;
            while (SDL_WaitEvent(&ev))
            {
                if (ev.type == SDL_QUIT)
                {
                    g_running = 0;
                    break;
                }
                if (ev.type == SDL_KEYDOWN ||
                    (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT))
                {

                    e.puzzles_completed = 0;
                    e.puzzles_correct = 0;
                    e.puzzles_wrong = 0;
                    e.joueur.score = 0;
                    e.joueur.vies = VIES_DEPART;
                    new_round(&e);
                    restart = 1;
                    break;
                }
            }

            if (!g_running) break;
            if (restart)
            {
                restart = 0;
                continue;
            }
            SDL_Delay(16);
            continue;
        }

        process_events(&e);
        update_game(&e);
        render_game(&e);
        SDL_Delay(16);
    }

    printf("[INFO] System shutdown. Final score: %d\n", e.joueur.score);

    quit_sdl();
    return 0;
}

int run_enigme2_with_renderer(SDL_Renderer *renderer, SDL_Window *window) {
    t_enigme2 e;
    int restart = 0;
    int running = 1;

    printf("=================================\n");
    printf("   SNOWPIERCER: REVOLT\n");
    printf("   TACTICAL COMMAND INTERFACE\n");
    printf("=================================\n");

    srand((unsigned int)time(NULL));

    g_win = window;
    g_ren = renderer;
    g_running = 1;
    SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);

    g_font_body = load_font("assets/fonts/arial.ttf", 14);
    if (!g_font_body) g_font_body = load_font("assets/fonts/mono.ttf", 14);
    if (!g_font_body) g_font_body = load_font("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 14);

    g_font_title = load_font("assets/fonts/arial.ttf", 20);
    if (!g_font_title) g_font_title = load_font("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 20);

    g_font_hud = load_font("assets/fonts/arial.ttf", 24);
    if (!g_font_hud) g_font_hud = load_font("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 24);

    if (!g_font_body || !g_font_title || !g_font_hud) {
        printf("[ERROR] Font loading failed!\n");
        return 0;
    }

    init_enigme2(&e, "assets");
    load_assets(&e);
    new_round(&e);

    printf("[INFO] Tactical system online.\n");
    printf("[INFO] Select inventory card -> Drag to target zone.\n");

    rz_anim_init(&g_rotozoom_success, &ROTOZOOM_PRESET_SUCCESS);
    rz_anim_init(&g_rotozoom_fail, &ROTOZOOM_PRESET_FAIL);

    while (running) {

        if (e.etat == FIN_JEU) {
            render_end_game_screen(&e);

            SDL_Event ev;
            while (SDL_WaitEvent(&ev)) {
                if (ev.type == SDL_QUIT) {
                    SDL_PushEvent(&ev);
                    running = 0;
                    break;
                }
                if (ev.type == SDL_KEYDOWN ||
                    (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT)) {
                    running = 0;
                    break;
                }
            }

            if (!running) break;
            SDL_Delay(16);
            continue;
        }

        process_events(&e);
        if (!g_running) running = 0;
        update_game(&e);
        render_game(&e);
        SDL_Delay(16);
    }

    printf("[INFO] System shutdown. Final score: %d\n", e.joueur.score);

    destroy_all_textures();
    destroy_fonts();
    g_ren = NULL;
    g_win = NULL;

    return 1;
}

