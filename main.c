#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "background.h"
#include "ui.h"
#include "audio.h"
#include "quiz.h"
#include "weaponfx.h"
#include "gameplay.h"
#include "interlude_manager.h"

#include "high score menu /leaderboard.h"
#include "highscore_screen.h"
#include "histoire.h"
#include "arduino_input.h"

#include "llm.h"
#include "llm_queue.h"
#include "npc_dialogue.h"
#include "enemy_taunts.h"
#include "barker.h"
#include "boss_monologue.h"
#include "kinetic.h"
#include "effects.h"
#include "cutscene.h"
#include "outro.h"
#include "damage_numbers.h"
#include "dialogue_cinematic.h"
#include "door_anim.h"
#include "boss_intro.h"
#include "boss_cutscene.h"
#include "boss_anim.h"
#include "boss_ai.h"
#include "boss_hud.h"
#include "weapon_select.h"
#include "skin_select.h"
#include "mode_select.h"
#include "two_player.h"
#include "versus.h"

#include <unistd.h>

#define ETAT_JEU        0
#define ETAT_SCORES     1
#define ETAT_SAISIE     2
#define ETAT_GAMEOVER   3
#define ETAT_SAUVEGARDE 5
#define ETAT_MENU       6
#define ETAT_OPTIONS    7
#define ETAT_QUIZ       8
#define ETAT_ENDING     9
#define ETAT_PAUSE         10
#define ETAT_CUTSCENE      11
#define ETAT_OUTRO         12
#define ETAT_CHAR_DEBUG    13  /* F1 from menu: cycle every (skin, weapon, anim) */
#define ETAT_WEAPON_SELECT 14  /* CHOOSE YOUR WEAPON screen, sits between save/load and skin pick */
#define ETAT_SKIN_SELECT   15  /* CHOOSE YOUR CHARACTER screen, sits between weapon pick and cutscene */
#define ETAT_MODE_SELECT   16  /* MODE DE JEU — campaign / coop / versus, sits right after MENU      */
#define ETAT_TWOPLAYER_TODO 17 /* Placeholder; kept around for future modes (unused after Prompt 12) */
#define ETAT_VERSUS         18 /* PvP arena — local two-player face-off                              */
#define ETAT_CONTROLS       19 /* Read-only controls reference, reachable from the in-game PAUSE menu */
/* Slot 4 (formerly ETAT_SELECTION, the old mode+avatar chooser) is retired —
 * its responsibilities are now split across WEAPON_SELECT + SKIN_SELECT.   */

static int keysHeld[323] = {0};

/* One handle per Arduino box.  ctrl1 drives player 1, ctrl2 (optional)
 * drives player 2 in co-op / versus.  Either may be NULL if its port
 * failed to open; all arduino_input_* calls are NULL-safe.               */
static ArduinoController *ctrl1 = NULL;
static ArduinoController *ctrl2 = NULL;

static int keyShoot  = 0;
static int keyMelee  = 0;
static int keyShoot2 = 0;
static int keyMelee2 = 0;

/* Which player slot the active weapon/skin select screen is configuring.
 * 0 = campaign / P1; 1 = P2 during CO-OP or VERSUS setup.  Re-read on
 * every SKIN_SELECT confirm to decide whether to advance to the next
 * slot or start the run.                                                */
static int g_setup_target_slot = 0;

static void draw_bg_image(SDL_Renderer *r, SDL_Texture *tex)
{
    SDL_Rect full = {0, 0, W_ECRAN, H_ECRAN};
    if (tex) {
        SDL_RenderCopy(r, tex, NULL, &full);
    } else {
        SDL_SetRenderDrawColor(r, 10, 15, 30, 255);
        SDL_RenderFillRect(r, &full);
    }

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 110);
    SDL_RenderFillRect(r, &full);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

static int layout_main_menu(UIButton out[5])
{
    const char *labels[5] = {"PLAY", "OPTIONS", "HIGH SCORES", "STORY", "QUIT"};
    for (int i = 0; i < 5; i++) {
        out[i].rect    = (SDL_Rect){470, 160 + i * 60, 300, 50};
        out[i].label   = labels[i];
        out[i].id      = i;
        out[i].enabled = 1;
    }
    return 5;
}

static int layout_options(UIButton out[5])
{
    out[0] = (UIButton){{100, 215, 150, 40},                 "-",            0, 1};
    out[1] = (UIButton){{280, 215, 150, 40},                 "+",            1, 1};
    out[2] = (UIButton){{100, 330, 150, 40},                 "WINDOWED",     2, 1};
    out[3] = (UIButton){{280, 330, 150, 40},                 "FULLSCREEN",   3, 1};
    out[4] = (UIButton){{(W_ECRAN - 200) / 2, 420, 200, 45}, "BACK",         4, 1};
    return 5;
}

static int layout_scores_bottom(UIButton out[2])
{
    out[0] = (UIButton){{150, H_ECRAN - 90, 200, 45}, "BACK",  0, 1};
    out[1] = (UIButton){{450, H_ECRAN - 90, 200, 45}, "QUIT",  1, 1};
    return 2;
}

static int layout_save_screen(UIButton out[2], int screen)
{
    int totalW = 200 + 40 + 200;
    int startX = W_ECRAN / 2 - totalW / 2;
    int btnY   = 280;
    const char *labelsA[2] = {"YES",       "NO"};
    const char *labelsB[2] = {"LOAD GAME", "NEW GAME"};
    for (int i = 0; i < 2; i++) {
        out[i].rect    = (SDL_Rect){startX + i * (200 + 40), btnY, 200, 60};
        out[i].label   = (screen == 0) ? labelsA[i] : labelsB[i];
        out[i].id      = i;
        out[i].enabled = 1;
    }
    return 2;
}

/* In-game PAUSE hub.  Order matters: the index is reused by the click and
 * keyboard handlers (0 RESUME, 1 OPTIONS, 2 CONTROLS, 3 SAVE+QUIT, 4 QUIT). */
#define PAUSE_RESUME   0
#define PAUSE_OPTIONS  1
#define PAUSE_CONTROLS 2
#define PAUSE_SAVEQUIT 3
#define PAUSE_QUIT     4
static int layout_pause_screen(UIButton out[5])
{
    const int btnW = 360, btnH = 44, gap = 12, btnY = 150;
    const char *labels[5] = {
        "RESUME",
        "OPTIONS",
        "CONTROLS",
        "SAVE AND QUIT",
        "QUIT WITHOUT SAVING"
    };
    for (int i = 0; i < 5; i++) {
        out[i].rect    = (SDL_Rect){(W_ECRAN - btnW) / 2, btnY + i * (btnH + gap), btnW, btnH};
        out[i].label   = labels[i];
        out[i].id      = i;
        out[i].enabled = 1;
    }
    return 5;
}

static int layout_controls_screen(UIButton out[1])
{
    out[0] = (UIButton){{(W_ECRAN - 200) / 2, H_ECRAN - 64, 200, 44}, "BACK", 0, 1};
    return 1;
}

static void draw_scores_list(SDL_Renderer *r, UIFontPack *fonts,
                              UITextCache *cache, Score *scores, int nb)
{
    SDL_Color jaune = {255, 215, 0, 255};
    SDL_Color cream = {240, 232, 210, 255};
    SDL_Color pale  = {190, 190, 210, 255};

    ui_draw_text_centered(cache, r, fonts->bebasTitle,
        "HIGH SCORES", jaune, W_ECRAN/2, 40);

    ui_draw_text_centered(cache, r, fonts->novaLabel,
        "RANK   NAME             SCORE   TIME     LEVEL",
        cream, W_ECRAN/2, 115);

    if (nb == 0) {
        ui_draw_text_centered(cache, r, fonts->story,
            "No scores recorded", pale, W_ECRAN/2, 200);
        return;
    }

    int rows = nb < MAX_SCORES ? nb : MAX_SCORES;
    for (int i = 0; i < rows; i++) {
        char row[128];
        snprintf(row, sizeof(row),
                 "%2d.  %-16s  %5d   %02d:%02d     %d/5",
                 i + 1, scores[i].nom, scores[i].score,
                 scores[i].tempsSec / 60, scores[i].tempsSec % 60,
                 scores[i].wagonAtteint);
        SDL_Color col = (i == 0) ? (SDL_Color){255, 215, 100, 255}
                                 : (SDL_Color){225, 225, 235, 255};
        ui_draw_text_centered(cache, r, fonts->mono, row, col,
                              W_ECRAN/2, 145 + i * 26);
    }
}

/* Common new-run init shared by the campaign SKIN_SELECT confirm and the
 * CO-OP P2-SKIN confirm.  Centralised so both modes go through the same
 * boss/barker/cutscene/cleanup beats — only the player count and the
 * gameplay-mode flag change.                                              */
static void start_new_run(Background    *bg_,
                          GameplayState *gp_,
                          BossMonologue *bm_,
                          WagonBarker   *barker_,
                          CutsceneState *cut_,
                          SDL_Renderer  *renderer_,
                          int           *scoreOut,
                          int           *lastWagonSeenOut,
                          int            game_mode_flag)
{
    bg_->nbJoueursActifs = (game_mode_flag == MODE_MULTI) ? 2 : 1;
    bg_->joueurs[0].hp   = JOUEUR_MAX_HP;
    if (game_mode_flag == MODE_MULTI) bg_->joueurs[1].hp = JOUEUR_MAX_HP;
    enemies_respawn_default(bg_);
    joueur_init_interieur(bg_);
    *scoreOut             = 0;
    bg_->tempsDepart      = SDL_GetTicks();
    bg_->wagonActif       = 0;
    *lastWagonSeenOut     = 0;
    gameplay_reset_for_run(gp_, game_mode_flag);
    gp_->totalInterludeScore = 0;
    gp_->enemiesKilled       = 0;
    boss_monologue_reset_run(bm_);
    wagon_barker_init(barker_);
    cutscene_start(cut_, renderer_, bg_->wagonActif);
}

#ifndef __EMSCRIPTEN__
#include <execinfo.h>
#endif
#include <signal.h>

#ifndef __EMSCRIPTEN__
static void crash_signal_handler(int sig)
{
    fprintf(stderr, "\n=== CAUGHT SIGNAL %d ===\n", sig);
    void *frames[64];
    int n = backtrace(frames, 64);
    backtrace_symbols_fd(frames, n, 2);
    fprintf(stderr, "=== end stack trace ===\n");
    fflush(stderr);
    signal(sig, SIG_DFL);
    raise(sig);
}
#endif

/* ----------------------------------------------------------------------
 * Per-frame engine state.  These were locals in main(); promoting them to
 * file scope lets the body of the old while(continuer) loop live in
 * one_frame(), which the native build calls in a while() and the web build
 * drives via emscripten_set_main_loop().  Behaviour is identical to the
 * in-place loop: file-scope state persists across one_frame() calls exactly
 * as the locals persisted across loop iterations.
 * -------------------------------------------------------------------- */
static SDL_Window   *fenetre;
static SDL_Renderer *renderer;

static UIFontPack    fonts;
static UIAssets      assets;
static UITextCache   tcache;
static AudioSystem   audio;
static QuizState     quiz;
static GameplayState gp;

static LLMSystem *llm;
static LLMQueue  *llm_queue;

static NPCDialogue       dlg;
static DialogueCinematic cinematic;
static WagonBarker       barker;
static BossMonologue     boss_mono;

static UIButtonState stMenu, stOptions, stScores, stSaveA, stSaveB,
                     stQuiz, stPause, stControls;

static SDL_Color blanc   = {255, 255, 255, 255};
static SDL_Color jaune   = {255, 215,   0, 255};
static SDL_Color glacial = {140, 200, 255, 255};

static Background    bg;
static KineticTrain  kin;
static CutsceneState cut;
static OutroState    outro;
static EffectsSystem effects;

static Score scores[MAX_SCORES];
static int   nbScores;
static int   scoreJoueur;
static char  nomJoueur[64];

static int    etat      = ETAT_MENU;
static int    continuer = 1;
static Uint32 lastTick;
static int    finalScore = 0, finalTimeSec = 0, finalWagon = 1;

static int sauvegardeScreen   = 0;
static int sauvegardeSelected = 0;
static int pauseSelected      = 0;
static int menuSelected       = 0;
static int optionsSelected    = 0;
static int scoresSelected     = 0;
static int controlsSelected   = 0;
/* OPTIONS and CONTROLS are reachable from two places (main menu and the
 * in-game PAUSE menu); these remember where "Back" should return to.      */
static int optionsReturn      = ETAT_MENU;
static int controlsReturn     = ETAT_PAUSE;
static int volumeLevel        = MIX_MAX_VOLUME / 2;
static int fullscreenMode     = 0;
static int lastWagonSeen      = 0;
static AudioTrack lastTrackState = AUDIO_TRACK_NONE;
static int lastEtat = -1;

/* Player-death fade-to-black timer (set when boss_ai finishes VICTORY). */
static Uint32 s_player_death_fade_start = 0;

/* Versus arena state — lives across rematches. */
static VersusState vs_state;

/* Debug character-anim viewer (F1 from menu). */
static CharAnim dbgCa = {0};
static Skin     dbgSkin   = SKIN_ELLIE;
static Weapon   dbgWeapon = WEAPON_SWORD;
static AnimType dbgAnim   = ANIM_IDLE;
static Skin     dbgLoadedSkin   = SKIN_COUNT;
static Weapon   dbgLoadedWeapon = WEAPON_COUNT;
static int      dbgFlip = 0;

/* One iteration of the game loop; defined after main(). */
static void one_frame(void);

#ifdef __EMSCRIPTEN__
/* Browser autoplay policy blocks all audio until the first user gesture, so
 * on web we defer opening the mixer, loading the sounds and starting the
 * music until the first key/click (triggered from one_frame()).  The native
 * build opens audio at startup and never calls this.                       */
static int g_audio_ready = 0;
static void audio_bringup(void)
{
    if (g_audio_ready) return;
    g_audio_ready = 1;
    Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);
    audio_init(&audio);
    audio_set_music_volume(&audio, volumeLevel);
    audio_start_ambience(&audio);
    audio_play_track(&audio, AUDIO_TRACK_MENU);
    lastTrackState = AUDIO_TRACK_MENU;
}
#endif

/* Versus (PvP) window setup.  Native goes desktop-fullscreen so the arena
 * fills the screen.  In the browser, OS fullscreen needs a user gesture and
 * black-screens the software renderer, so we just size the canvas to the
 * versus dimensions — versus_render_world reads SDL_GetWindowSize and fills
 * it edge-to-edge either way.  The logical size is set so pre-match mouse
 * events map into the versus coordinate space.                              */
static void versus_enter_window(void)
{
#ifdef __EMSCRIPTEN__
    SDL_SetWindowSize(fenetre, VS_CANVAS_W, VS_CANVAS_H);
#else
    SDL_SetWindowFullscreen(fenetre, 0);
    SDL_SetWindowSize(fenetre, VS_CANVAS_W, VS_CANVAS_H);
    SDL_SetWindowPosition(fenetre, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_SetWindowFullscreen(fenetre, SDL_WINDOW_FULLSCREEN_DESKTOP);
#endif
    SDL_RenderSetLogicalSize(renderer, VS_CANVAS_W, VS_CANVAS_H);
}

/* Restore the campaign window + 800x520 logical canvas on leaving versus. */
static void versus_exit_window(void)
{
    SDL_RenderSetScale(renderer, 1.0f, 1.0f);
#ifndef __EMSCRIPTEN__
    SDL_SetWindowFullscreen(fenetre, 0);
    SDL_SetWindowSize(fenetre, W_ECRAN, H_ECRAN);
    SDL_SetWindowPosition(fenetre, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
#else
    SDL_SetWindowSize(fenetre, W_ECRAN, H_ECRAN);
#endif
    SDL_RenderSetLogicalSize(renderer, W_ECRAN, H_ECRAN);
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

#ifndef __EMSCRIPTEN__
    signal(SIGABRT, crash_signal_handler);
    signal(SIGSEGV, crash_signal_handler);
    signal(SIGBUS,  crash_signal_handler);
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init : %s\n", SDL_GetError()); return EXIT_FAILURE;
    }
    if (!(IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG) & IMG_INIT_PNG)) {
        fprintf(stderr, "IMG_Init : %s\n", IMG_GetError()); SDL_Quit(); return EXIT_FAILURE;
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init : %s\n", TTF_GetError()); IMG_Quit(); SDL_Quit(); return EXIT_FAILURE;
    }
#ifndef __EMSCRIPTEN__
    /* Native opens the mixer up front.  On web, browser autoplay policy
     * blocks audio until a user gesture, so the whole audio bring-up is
     * deferred to the first key/click in one_frame() (see audio_bringup). */
    Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);
#endif

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    /* The Mesa Gallium driver under VirtualBox corrupts its internal
     * allocator after sustained texture creation/destruction. Force the
     * software renderer to bypass that code path entirely. */
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");

    fenetre = SDL_CreateWindow("Snowpiercer",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        W_ECRAN, H_ECRAN,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_SetWindowTitle(fenetre, "Snowpiercer");
    renderer = SDL_CreateRenderer(fenetre, -1,
        SDL_RENDERER_SOFTWARE | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(renderer, W_ECRAN, H_ECRAN);
    SDL_RenderSetIntegerScale(renderer, SDL_FALSE);

    ui_fonts_load(&fonts);
    ui_assets_load(&assets, renderer);
    ui_text_cache_init(&tcache);
    damage_numbers_init(fonts.mono ? fonts.mono : fonts.monoSmall,
                        renderer);
#ifndef __EMSCRIPTEN__
    audio_init(&audio);   /* web: loaded later in audio_bringup() */
#endif
    quiz_init(&quiz);
    gameplay_init(&gp); gameplay_load_assets(&gp, renderer);

    llm = llm_init(
        "../llama.cpp/build/bin/llama-server",
        "../llama.cpp/models/gemma-3-1b-it-Q4_K_M.gguf",
        "127.0.0.1", 8080);
    if (!llm) fprintf(stderr, "[main] LLM unavailable; dialogue will show errors\n");

    llm_queue = llm ? llm_queue_create(llm) : NULL;
    if (llm && !llm_queue)
        fprintf(stderr, "[main] LLM queue unavailable; flavor text disabled\n");

    npc_dialogue_init(&dlg, "Namgoong",
                      "the security & door expert who keeps the wagons sealed");

    dialogue_cinematic_init(&cinematic);
    dialogue_cinematic_bind_global(&cinematic);

    enemy_taunts_init();

    wagon_barker_init(&barker);
    boss_monologue_init(&boss_mono);

    ui_button_state_reset(&stMenu);
    ui_button_state_reset(&stOptions);
    ui_button_state_reset(&stControls);
    ui_button_state_reset(&stScores);
    ui_button_state_reset(&stSaveA);
    ui_button_state_reset(&stSaveB);
    ui_button_state_reset(&stQuiz);
    ui_button_state_reset(&stPause);


    background_init(&bg, renderer, 1);
    weapon_select_init(renderer);
    skin_select_init(renderer);
    mode_select_init(renderer);
    two_player_init();
    background_chargerImages(&bg, renderer, "platefixe.png", NULL);

    bg.texte.police     = fonts.mono;
    bg.texte.couleur    = blanc;
    bg.texte.contenu[0] = '\0';

    SDL_Rect rectGuide = {W_ECRAN/2-200, H_ECRAN/2-130, 400, 260};
    fenetreGuide_init(&bg, renderer, "guide.png", rectGuide);

    vues_init(&bg, renderer);
    plateformes_init(&bg, renderer);
    destructibles_init(&bg, renderer);

    wagonsInterieurs_init(&bg, renderer);
    neige_init(&bg);
    joueur_init_interieur(&bg);

    kinetic_init(&kin, renderer);

    memset(&cut, 0, sizeof(cut));

    memset(&outro, 0, sizeof(outro));

    effects_init(&effects, renderer);
    effects_bind_global(&effects);
    effects_seed_default_torches(&effects);

    /* door_anim: load the 3 door assets once and bind the engine pointers it
     * needs at update time (bg for wagon swap + input gate, gp for screen
     * shake, fonts/cache for the Stage-3 title card).  The init is here
     * because every dependency is constructed by this point. */
    door_anim_init(renderer, &fonts, &tcache, &bg, &gp, &audio);

    /* boss_intro: 4-stage cinematic that plays once per process when the
     * player first crosses into the final wagon.  Reuses door_anim,
     * dialogue_cinematic (letterbox bars), the additive pan_ox, the screen
     * shake, and the screen flash entirely; nothing is reimplemented. */
    boss_intro_init(renderer, &fonts, &tcache, &bg, &gp, &cinematic);

    /* boss_cutscene: plays once before boss_intro on the first crossing into
     * wagon 5.  Black background + 7x7 sprite reveal + vignette + one-shot
     * line ("You shall not pass.") via dialogue_cinematic, then hands off
     * to boss_intro_start(). */
    boss_cutscene_init(renderer, &fonts, &tcache, &bg, &gp, &audio, &cinematic);

    /* boss_anim / boss_hud: load the new 7-row sprite sheet for the final
     * boss + initialize the dedicated boss HUD.  Max HP matches the boss
     * enforcer after scale_enemy_for_level scales ENFORCER_HP (120) by
     * 1.40 for wagon >= 2 -> 168.  boss_hud_init crops Row1/Frame1 from
     * the sheet to make the portrait (falls back to a solid rect on any
     * texture failure). */
    /* boss_anim now loads its own per-animation sheets from
     * "spritesheets assets/final boss/final boss-*.png" — see boss_anim.c.
     * boss_hud still uses the legacy enforcer sheet only for its portrait
     * crop below; the in-world boss sprite is drawn from the new sheets. */
    boss_anim_init(renderer);
    boss_ai_init(&bg, &gp, &audio);
    {
        int bossMaxHp = (int)((float)ENFORCER_HP * ENEMY_HP_BASE_MULT * BOSS_HP_MULT);
        SDL_Texture *bossSheet = NULL;
        bossSheet = ui_loadTexture(renderer, "assets/boss/enemy_enforcer.png");
        boss_hud_init(renderer, &fonts, &tcache, &gp, &effects,
                      bossSheet, bossMaxHp, "WILFORD'S ENFORCER");
        if (bossSheet) SDL_DestroyTexture(bossSheet);  /* portrait already sampled */
    }

    nbScores = scores_charger(FICHIER_SCORES, scores, MAX_SCORES);

    lastTick = SDL_GetTicks();

    /* Player-death fade-to-black timer.  Captures SDL_GetTicks() the
     * frame boss_ai finishes its VICTORY animation; the render block
     * ramps alpha 0..255 over 500 ms, then ETAT_GAMEOVER lands. */

    /* Versus arena state — lives across rematches.                     */

    /* Debug character-anim viewer (F1 from menu).  Cycles every
     * (skin, weapon, anim) combo so each new sheet can be eyeballed
     * for correct grid resolution and foot-anchor placement.        */

#ifndef __EMSCRIPTEN__
    audio_set_music_volume(&audio, volumeLevel);
    audio_start_ambience(&audio);
    audio_play_track(&audio, AUDIO_TRACK_MENU);
#endif

    /* Controller 1 -> player 1, controller 2 -> player 2.  Paths are
     * env-overridable (ARDUINO_PORT / ARDUINO2_PORT) since the two boards
     * usually enumerate differently: the CH340 clone as ttyUSB0, a genuine
     * / newer board as ttyACM0.  Either may be absent — opening just logs a
     * warning and that player falls back to the keyboard.                 */
    {
        const char *p1 = getenv("ARDUINO_PORT");
        const char *p2 = getenv("ARDUINO2_PORT");
        ctrl1 = arduino_input_open(p1 && *p1 ? p1 : "/dev/ttyUSB0", ARD_PLAYER1);
        ctrl2 = arduino_input_open(p2 && *p2 ? p2 : "/dev/ttyUSB1", ARD_PLAYER2);
    }

#ifdef __EMSCRIPTEN__
    /* Browser: hand the frame pump to the event loop; never returns. */
    emscripten_set_main_loop(one_frame, 0, 1);
#else
    while (continuer) one_frame();
#endif
    scores_sauvegarder(FICHIER_SCORES, scores, nbScores);

    arduino_input_close(ctrl1);
    arduino_input_close(ctrl2);

    if (llm_queue) llm_queue_destroy(llm_queue);
    if (llm) llm_shutdown(llm);

    audio_free(&audio);
    gameplay_free_assets(&gp);
    damage_numbers_shutdown();
    ui_text_cache_clear(&tcache);
    ui_assets_free(&assets);
    ui_fonts_free(&fonts);
    effects_bind_global(NULL);
    effects_free(&effects);
    door_anim_free();
    kinetic_free(&kin);
    cutscene_free(&cut);
    outro_free(&outro);
    background_liberer(&bg);
    weapon_select_free();
    skin_select_free();
    mode_select_free();
    Mix_CloseAudio();
    TTF_Quit(); IMG_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(fenetre);
    SDL_Quit();
    return EXIT_SUCCESS;
}

static void one_frame(void)
{

        Uint32 nowTick = SDL_GetTicks();
        float dt = (float)(nowTick - lastTick) / 1000.0f;
        if (dt > 0.05f) dt = 0.05f;
        lastTick = nowTick;

        keyShoot  = 0;
        keyMelee  = 0;
        keyShoot2 = 0;
        keyMelee2 = 0;

        AudioTrack wantTrack = AUDIO_TRACK_MENU;
        if (etat == ETAT_JEU || etat == ETAT_GAMEOVER || etat == ETAT_PAUSE) {
            wantTrack = (bg.wagonActif >= MAX_WAGONS - 1)
                        ? AUDIO_TRACK_BOSS
                        : AUDIO_TRACK_GAME;
        }
        else if (etat == ETAT_QUIZ)                    wantTrack = AUDIO_TRACK_INTERLUDE;
        if (wantTrack != lastTrackState) {
            audio_play_track(&audio, wantTrack);
            lastTrackState = wantTrack;
        }

        if (etat != lastEtat) {
            if (etat == ETAT_GAMEOVER) {
                arduino_input_send_event(ctrl1, "DEAD");
                arduino_input_send_event(ctrl2, "DEAD");
            } else if (etat == ETAT_ENDING) {
                arduino_input_send_event(ctrl1, "WIN");
                arduino_input_send_event(ctrl2, "WIN");
            }
            lastEtat = etat;
        }

        stMenu.activatedId = stOptions.activatedId = stScores.activatedId = -1;
        stSaveA.activatedId = stSaveB.activatedId = -1;
        stQuiz.activatedId  = -1;
        stPause.activatedId = -1;
        stControls.activatedId = -1;

        arduino_input_poll(ctrl1);
        arduino_input_poll(ctrl2);

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
#ifdef __EMSCRIPTEN__
            /* First user gesture unlocks browser audio: bring the mixer up
             * now so the menu music / cutscene audio start playing. */
            if (!g_audio_ready &&
                (ev.type == SDL_KEYDOWN || ev.type == SDL_MOUSEBUTTONDOWN))
                audio_bringup();
#endif
            if (ev.type == SDL_QUIT) { continuer = 0; continue; }

            if (npc_dialogue_is_active(&dlg)) {
                if (ev.type == SDL_TEXTINPUT) {
                    npc_dialogue_handle_textinput(&dlg, ev.text.text);
                    continue;
                }
                if (ev.type == SDL_KEYDOWN) {
                    /* Cinematic gets first refusal: it eats Enter to fast-
                     * forward the typewriter and eats Esc to slide out;
                     * everything else falls through to the chatbot. */
                    if (!dialogue_cinematic_handle_key(&cinematic,
                                                       ev.key.keysym.sym))
                        npc_dialogue_handle_keydown(&dlg, ev.key.keysym.sym, llm_queue);
                    continue;
                }
                if (ev.type == SDL_MOUSEMOTION ||
                    ev.type == SDL_MOUSEBUTTONDOWN) {
                    continue;
                }
            }

            if (ev.type == SDL_MOUSEMOTION) {
                int lx, ly;
                ui_mouse_logical(renderer, ev.motion.x, ev.motion.y, &lx, &ly);

                if (etat == ETAT_MENU) {
                    UIButton btns[5]; int n = layout_main_menu(btns);
                    ui_button_handle_motion(&stMenu, btns, n, lx, ly);
                    if (stMenu.hoveredId >= 0) menuSelected = stMenu.hoveredId;
                }
                else if (etat == ETAT_OPTIONS) {
                    UIButton btns[5]; int n = layout_options(btns);
                    ui_button_handle_motion(&stOptions, btns, n, lx, ly);
                    if (stOptions.hoveredId >= 0) optionsSelected = stOptions.hoveredId;
                }
                else if (etat == ETAT_SCORES) {
                    UIButton btns[2]; int n = layout_scores_bottom(btns);
                    ui_button_handle_motion(&stScores, btns, n, lx, ly);
                    if (stScores.hoveredId >= 0) scoresSelected = stScores.hoveredId;
                }
                else if (etat == ETAT_SAUVEGARDE) {
                    UIButton btns[2]; int n = layout_save_screen(btns, sauvegardeScreen);
                    UIButtonState *st = (sauvegardeScreen == 0) ? &stSaveA : &stSaveB;
                    ui_button_handle_motion(st, btns, n, lx, ly);
                    if (st->hoveredId >= 0) sauvegardeSelected = st->hoveredId;
                }
                else if (etat == ETAT_MODE_SELECT) {
                    mode_select_handle_motion(lx, ly);
                }
                else if (etat == ETAT_WEAPON_SELECT) {
                    weapon_select_handle_motion(lx, ly);
                }
                else if (etat == ETAT_SKIN_SELECT) {
                    skin_select_handle_motion(lx, ly);
                }
                else if (etat == ETAT_VERSUS) {
                    versus_handle_motion(&vs_state, lx, ly);
                }
                else if (etat == ETAT_QUIZ) {
                    quiz_handle_mouse_motion(&quiz, renderer, ev.motion.x, ev.motion.y);
                }
                else if (etat == ETAT_PAUSE) {
                    UIButton btns[5]; int n = layout_pause_screen(btns);
                    ui_button_handle_motion(&stPause, btns, n, lx, ly);
                    if (stPause.hoveredId >= 0) pauseSelected = stPause.hoveredId;
                }
                else if (etat == ETAT_CONTROLS) {
                    UIButton btns[1]; int n = layout_controls_screen(btns);
                    ui_button_handle_motion(&stControls, btns, n, lx, ly);
                }
                else if (etat == ETAT_OUTRO) {
                    OutroAction act = outro_handle_mouse_motion(&outro, lx, ly);
                    if (act == OUTRO_ACTION_CLICK)
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                }
            }
            else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                int lx, ly;
                ui_mouse_logical(renderer, ev.button.x, ev.button.y, &lx, &ly);

                if (etat == ETAT_MENU) {
                    UIButton btns[5]; int n = layout_main_menu(btns);
                    ui_button_handle_click(&stMenu, btns, n, lx, ly);
                    if (stMenu.activatedId >= 0) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        menuSelected = stMenu.activatedId;
                        switch (menuSelected) {
                            case 0:
                                /* "Nouvelle partie" now opens the game-mode picker.
                                 * CAMPAIGN routes back to SAUVEGARDE -> existing flow;
                                 * CO-OP / VERSUS hit the two-player placeholder.   */
                                mode_select_enter();
                                etat = ETAT_MODE_SELECT;
                                break;
                            case 1: optionsReturn = ETAT_MENU; etat = ETAT_OPTIONS; break;
                            case 2: {
                                int quit = highscore_screen_run(fenetre, renderer);
                                lastTrackState = AUDIO_TRACK_NONE;
                                lastTick = SDL_GetTicks();
                                if (quit) continuer = 0;
                                break;
                            }
                            case 3: {
                                int quit = histoire_screen_run(fenetre, renderer, "assets/video.mp4");
                                lastTrackState = AUDIO_TRACK_NONE;
                                lastTick = SDL_GetTicks();
                                if (quit) continuer = 0;
                                break;
                            }
                            case 4: continuer = 0;          break;
                        }
                    }
                }
                else if (etat == ETAT_OPTIONS) {
                    UIButton btns[5]; int n = layout_options(btns);
                    ui_button_handle_click(&stOptions, btns, n, lx, ly);
                    if (stOptions.activatedId >= 0) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        optionsSelected = stOptions.activatedId;
                        switch (optionsSelected) {
                            case 0:
                                volumeLevel -= 16;
                                if (volumeLevel < 0) volumeLevel = 0;
                                audio_set_music_volume(&audio, volumeLevel);
                                audio_set_sfx_volume(&audio, volumeLevel);
                                break;
                            case 1:
                                volumeLevel += 16;
                                if (volumeLevel > MIX_MAX_VOLUME) volumeLevel = MIX_MAX_VOLUME;
                                audio_set_music_volume(&audio, volumeLevel);
                                audio_set_sfx_volume(&audio, volumeLevel);
                                break;
                            case 2:
                                fullscreenMode = 0;
                                SDL_SetWindowFullscreen(fenetre, 0);
                                SDL_RenderSetLogicalSize(renderer, W_ECRAN, H_ECRAN);
                                break;
                            case 3:
                                fullscreenMode = 1;
#ifndef __EMSCRIPTEN__   /* OS fullscreen black-screens the web canvas */
                                SDL_SetWindowFullscreen(fenetre, SDL_WINDOW_FULLSCREEN_DESKTOP);
#endif
                                SDL_RenderSetLogicalSize(renderer, W_ECRAN, H_ECRAN);
                                break;
                            case 4: etat = optionsReturn; break; /* BACK -> menu or pause */
                        }
                    }
                }
                else if (etat == ETAT_SCORES) {
                    UIButton btns[2]; int n = layout_scores_bottom(btns);
                    ui_button_handle_click(&stScores, btns, n, lx, ly);
                    if (stScores.activatedId >= 0) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        scoresSelected = stScores.activatedId;
                        if (scoresSelected == 0) etat = ETAT_MENU;
                        else continuer = 0;
                    }
                }
                else if (etat == ETAT_SAUVEGARDE) {
                    UIButton btns[2]; int n = layout_save_screen(btns, sauvegardeScreen);
                    UIButtonState *st = (sauvegardeScreen == 0) ? &stSaveA : &stSaveB;
                    ui_button_handle_click(st, btns, n, lx, ly);
                    if (st->activatedId >= 0) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        sauvegardeSelected = st->activatedId;
                        if (sauvegardeScreen == 0) {
                            if (sauvegardeSelected == 0) {
                                sauvegardeScreen   = 1;
                                sauvegardeSelected = 0;
                            } else {
                                g_setup_target_slot = 0;
                                weapon_select_enter(0);
                                etat = ETAT_WEAPON_SELECT;
                            }
                        } else {
                            if (sauvegardeSelected == 0) {
                                if (charger_partie(&bg, &gp, &scoreJoueur, &bg.tempsDepart)) {
                                    lastWagonSeen = bg.wagonActif;
                                    boss_monologue_reset_run(&boss_mono);
                                    wagon_barker_init(&barker);
                                    etat = ETAT_JEU;
                                } else {
                                    g_setup_target_slot = 0;
                                weapon_select_enter(0);
                                etat = ETAT_WEAPON_SELECT;
                                }
                            } else {
                                g_setup_target_slot = 0;
                                weapon_select_enter(0);
                                etat = ETAT_WEAPON_SELECT;
                            }
                            sauvegardeScreen = 0;
                        }
                    }
                }
                else if (etat == ETAT_MODE_SELECT) {
                    ModeSelectAction act = mode_select_handle_click(lx, ly);
                    if (act == MS_ACTION_CONFIRM) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        if (g_game_mode == MODE_CAMPAIGN) {
                            /* Campaign keeps its existing save/load gate. */
                            sauvegardeScreen   = 1;
                            sauvegardeSelected = 0;
                            ui_button_state_reset(&stSaveB);
                            etat = ETAT_SAUVEGARDE;
                        } else {
                            /* CO-OP and VERSUS share the same 4-screen
                             * setup: P1 weapon → P1 skin → P2 weapon →
                             * P2 skin.  Start at P1's weapon.            */
                            g_setup_target_slot = 0;
                            weapon_select_enter(0);
                            etat = ETAT_WEAPON_SELECT;
                        }
                    } else if (act == MS_ACTION_CANCEL) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        etat = ETAT_MENU;
                    }
                }
                else if (etat == ETAT_TWOPLAYER_TODO) {
                    /* Click anywhere on the placeholder returns to MENU. */
                    audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                    etat = ETAT_MENU;
                }
                else if (etat == ETAT_WEAPON_SELECT) {
                    WeaponSelectAction act = weapon_select_handle_click(lx, ly);
                    if (act == WS_ACTION_CONFIRM) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        skin_select_enter(renderer, g_setup_target_slot);
                        etat = ETAT_SKIN_SELECT;
                    } else if (act == WS_ACTION_CANCEL) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        mode_select_enter();
                        etat = ETAT_MODE_SELECT;
                    }
                }
                else if (etat == ETAT_SKIN_SELECT) {
                    SkinSelectAction act = skin_select_handle_click(lx, ly);
                    if (act == SS_ACTION_CONFIRM) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        if (g_game_mode == MODE_CAMPAIGN) {
                            start_new_run(&bg, &gp, &boss_mono, &barker,
                                          &cut, renderer,
                                          &scoreJoueur, &lastWagonSeen,
                                          MODE_MONO);
                            etat = ETAT_CUTSCENE;
                        } else if (g_setup_target_slot == 0) {
                            /* P1 done — kick P2 through the same screens. */
                            g_setup_target_slot = 1;
                            weapon_select_enter(1);
                            etat = ETAT_WEAPON_SELECT;
                        } else {
                            /* P2 done.  CO-OP starts the multi-player
                             * campaign; VERSUS jumps into the PvP arena. */
                            if (g_game_mode == MODE_COOP) {
                                start_new_run(&bg, &gp, &boss_mono, &barker,
                                              &cut, renderer,
                                              &scoreJoueur, &lastWagonSeen,
                                              MODE_MULTI);
                                etat = ETAT_CUTSCENE;
                            } else if (g_game_mode == MODE_VERSUS) {
                                /* Versus fills the desktop end-to-end.
                                 * Go desktop-fullscreen and disable the
                                 * campaign's 800×520 logical letterbox by
                                 * calling SDL_RenderSetLogicalSize(0, 0)
                                 * — without this disable the campaign's
                                 * letterboxed canvas would persist into
                                 * PvP and the bars come back.  No render
                                 * scale either: versus_render_world reads
                                 * the real window size via SDL_GetWindowSize
                                 * and renders the bg edge-to-edge.        */
                                versus_enter_window();
                                versus_begin_match(&vs_state, &bg, &gp);
                                etat = ETAT_VERSUS;
                            } else {
                                etat = ETAT_TWOPLAYER_TODO;
                            }
                        }
                    } else if (act == SS_ACTION_CANCEL) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        weapon_select_enter(g_setup_target_slot);
                        etat = ETAT_WEAPON_SELECT;
                    }
                }
                else if (etat == ETAT_VERSUS) {
                    versus_handle_click(&vs_state, lx, ly);
                }
                else if (etat == ETAT_QUIZ) {
                    quiz_handle_mouse_click(&quiz, renderer, ev.button.x, ev.button.y, &audio);
                }
                else if (etat == ETAT_PAUSE) {
                    UIButton btns[5]; int n = layout_pause_screen(btns);
                    ui_button_handle_click(&stPause, btns, n, lx, ly);
                    if (stPause.activatedId >= 0) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        pauseSelected = stPause.activatedId;
                        switch (pauseSelected) {
                            case PAUSE_RESUME:
                                etat = ETAT_JEU;
                                break;
                            case PAUSE_OPTIONS:
                                optionsReturn = ETAT_PAUSE;
                                ui_button_state_reset(&stOptions);
                                etat = ETAT_OPTIONS;
                                break;
                            case PAUSE_CONTROLS:
                                controlsReturn   = ETAT_PAUSE;
                                controlsSelected = 0;
                                ui_button_state_reset(&stControls);
                                etat = ETAT_CONTROLS;
                                break;
                            case PAUSE_SAVEQUIT:
                                if (sauvegarder_partie(&bg, &gp, scoreJoueur, bg.tempsDepart))
                                    fprintf(stderr, "[SAVE] Partie sauvegardee\n");
                                etat = ETAT_MENU;
                                break;
                            case PAUSE_QUIT:
                                etat = ETAT_MENU;
                                break;
                        }
                        pauseSelected = 0;
                        ui_button_state_reset(&stPause);
                    }
                }
                else if (etat == ETAT_CONTROLS) {
                    UIButton btns[1]; int n = layout_controls_screen(btns);
                    ui_button_handle_click(&stControls, btns, n, lx, ly);
                    if (stControls.activatedId >= 0) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        ui_button_state_reset(&stControls);
                        etat = controlsReturn;
                    }
                }
                else if (etat == ETAT_OUTRO) {
                    OutroAction act = outro_handle_mouse_click(&outro, lx, ly);
                    if (act == OUTRO_ACTION_REPLAY) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        outro_free(&outro);
                        bg.joueurs[0].hp = JOUEUR_MAX_HP;
                        if (bg.nbJoueursActifs == 2)
                            bg.joueurs[1].hp = JOUEUR_MAX_HP;
                        enemies_respawn_default(&bg);
                        joueur_init_interieur(&bg);
                        scoreJoueur    = 0;
                        bg.tempsDepart = SDL_GetTicks();
                        bg.wagonActif  = 0;
                        lastWagonSeen  = 0;
                        gameplay_reset_for_run(&gp, gp.gameMode);
                        gp.totalInterludeScore = 0;
                        gp.enemiesKilled = 0;
                        boss_monologue_reset_run(&boss_mono);
                        wagon_barker_init(&barker);
                        cutscene_start(&cut, renderer, bg.wagonActif);
                        etat = ETAT_CUTSCENE;
                    } else if (act == OUTRO_ACTION_MENU) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        outro_free(&outro);
                        etat = ETAT_MENU;
                    }
                }
            }

            else if (ev.type == SDL_KEYDOWN) {
                keysHeld[ev.key.keysym.sym & 0x1FF] = 1;

                if (etat == ETAT_CUTSCENE) {
                    SDL_Keycode k = ev.key.keysym.sym;
                    if (k == SDLK_SPACE || k == SDLK_RETURN)
                        cutscene_skip(&cut);
                    continue;
                }

                if (etat == ETAT_OUTRO) {
                    OutroAction act = outro_handle_key(&outro, ev.key.keysym.sym);
                    if (act == OUTRO_ACTION_CLICK) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                    } else if (act == OUTRO_ACTION_REPLAY) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        outro_free(&outro);
                        /* Restart from the beginning, keeping the
                         * current mode + avatar selection.  Mirrors
                         * the VALIDER reset block.                  */
                        bg.joueurs[0].hp = JOUEUR_MAX_HP;
                        if (bg.nbJoueursActifs == 2)
                            bg.joueurs[1].hp = JOUEUR_MAX_HP;
                        enemies_respawn_default(&bg);
                        joueur_init_interieur(&bg);
                        scoreJoueur    = 0;
                        bg.tempsDepart = SDL_GetTicks();
                        bg.wagonActif  = 0;
                        lastWagonSeen  = 0;
                        gameplay_reset_for_run(&gp, gp.gameMode);
                        gp.totalInterludeScore = 0;
                        gp.enemiesKilled = 0;
                        boss_monologue_reset_run(&boss_mono);
                        wagon_barker_init(&barker);
                        cutscene_start(&cut, renderer, bg.wagonActif);
                        etat = ETAT_CUTSCENE;
                    } else if (act == OUTRO_ACTION_MENU) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        outro_free(&outro);
                        etat = ETAT_MENU;
                    }
                    continue;
                }

                if (etat == ETAT_PAUSE) {
                    SDL_Keycode k = ev.key.keysym.sym;
                    if (k == SDLK_UP   || k == SDLK_w) pauseSelected = (pauseSelected + 4) % 5;
                    if (k == SDLK_DOWN || k == SDLK_s) pauseSelected = (pauseSelected + 1) % 5;
                    if (k == SDLK_RETURN) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        switch (pauseSelected) {
                            case PAUSE_RESUME:
                                etat = ETAT_JEU;
                                break;
                            case PAUSE_OPTIONS:
                                optionsReturn = ETAT_PAUSE;
                                ui_button_state_reset(&stOptions);
                                etat = ETAT_OPTIONS;
                                break;
                            case PAUSE_CONTROLS:
                                controlsReturn   = ETAT_PAUSE;
                                controlsSelected = 0;
                                ui_button_state_reset(&stControls);
                                etat = ETAT_CONTROLS;
                                break;
                            case PAUSE_SAVEQUIT:
                                if (sauvegarder_partie(&bg, &gp, scoreJoueur, bg.tempsDepart))
                                    fprintf(stderr, "[SAVE] Partie sauvegardee\n");
                                etat = ETAT_MENU;
                                break;
                            case PAUSE_QUIT:
                                etat = ETAT_MENU;
                                break;
                        }
                        pauseSelected = 0;
                        ui_button_state_reset(&stPause);
                    }
                    if (k == SDLK_ESCAPE || k == SDLK_BACKSPACE) { etat = ETAT_JEU; pauseSelected = 0; }
                    continue;
                }

                if (etat == ETAT_CONTROLS) {
                    SDL_Keycode k = ev.key.keysym.sym;
                    if (k == SDLK_ESCAPE || k == SDLK_BACKSPACE || k == SDLK_RETURN) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        ui_button_state_reset(&stControls);
                        etat = controlsReturn;
                    }
                    continue;
                }

                if (etat == ETAT_JEU) {
                    /* Boss-cutscene skip — eat Space/Enter while the
                     * reveal is playing, then fall through to the rest
                     * of this frame's input handling. */
                    if (boss_cutscene_handle_skip_key(ev.key.keysym.sym))
                        continue;

                    if (ev.key.keysym.sym == SDLK_ESCAPE) {
                        etat = ETAT_PAUSE;
                        pauseSelected = 0;
                        ui_button_state_reset(&stPause);
                        continue;
                    }
                    if (ev.key.keysym.sym == SDLK_g)
                        bg.fenetreguide.visible = !bg.fenetreguide.visible;
                    if (ev.key.keysym.sym == SDLK_F1) gp.debugHitbox = !gp.debugHitbox;
                    if (ev.key.keysym.sym == SDLK_n)  etat = ETAT_SAISIE;
                    if (ev.key.keysym.sym == SDLK_f)  {
                        if (gameplay_handle_player_shoot(&gp, 0, &audio)) {
                            keyShoot = 1; audio_play_sfx(&audio, SFX_ATTACK);
                            arduino_input_send_event(ctrl1, "SHOOT");
                        }
                    }
                    if (ev.key.keysym.sym == SDLK_e)  {
                        keyMelee = 1; audio_play_sfx(&audio, SFX_ATTACK);
                        arduino_input_send_event(ctrl1, "MELEE");
                        if (gp.gameMode == MODE_MONO) gameplay_handle_door_interact(&gp, &bg, &audio);
                        if (gp.gameMode == MODE_MULTI) gameplay_handle_revive_request(&gp, &bg, &audio, 0);
                    }
                    if (ev.key.keysym.sym == SDLK_k)  {
                        if (gameplay_handle_player_shoot(&gp, 1, &audio)) {
                            keyShoot2 = 1; audio_play_sfx(&audio, SFX_ATTACK);
                            arduino_input_send_event(ctrl2, "SHOOT");
                        }
                    }
                    if (ev.key.keysym.sym == SDLK_l)  {
                        keyMelee2 = 1; audio_play_sfx(&audio, SFX_ATTACK);
                        arduino_input_send_event(ctrl2, "MELEE");
                        if (gp.gameMode == MODE_MULTI) gameplay_handle_revive_request(&gp, &bg, &audio, 1);
                    }
                    if (ev.key.keysym.sym == SDLK_y) {
                        /* toggle between the cinematic "outside" view and the
                         * classic full-screen "inside" view */
                        kin.enabled = !kin.enabled;
                    }
                    if (ev.key.keysym.sym == SDLK_LSHIFT)
                        gameplay_handle_dash_input(&gp, &bg, &audio, 0);
                    if (ev.key.keysym.sym == SDLK_RSHIFT && bg.nbJoueursActifs >= 2)
                        gameplay_handle_dash_input(&gp, &bg, &audio, 1);
                    if ((ev.key.keysym.sym == SDLK_SPACE || ev.key.keysym.sym == SDLK_z) &&
                        bg.joueurs[0].auSol) {
                        audio_play_sfx(&audio, SFX_JUMP);
                    }
                    if (ev.key.keysym.sym == SDLK_t &&
                        gp.gameMode == MODE_MONO &&
                        gp.namgoong.activeSolo &&
                        gp.namgoong.hp > 0 &&
                        bg.wagonActif == gp.namgoong.wagon) {
                        float dx = bg.joueurs[0].x + bg.joueurs[0].w/2.0f
                                 - (gp.namgoong.x + 32.0f);
                        float dy = bg.joueurs[0].y + bg.joueurs[0].h/2.0f
                                 - (gp.namgoong.y + 32.0f);
                        float dist = sqrtf(dx*dx + dy*dy);
                        if (dist < 120.0f) {
                            char quest[96];
                            if (gp.door.state == DOOR_OPEN)
                                snprintf(quest, sizeof(quest),
                                    "moving to the next wagon");
                            else if (gp.kronoleCount < KRONOLE_DOOR_COST)
                                snprintf(quest, sizeof(quest),
                                    "find Kronole (%d/%d) to unlock the door",
                                    gp.kronoleCount, KRONOLE_DOOR_COST);
                            else
                                snprintf(quest, sizeof(quest),
                                    "open the locked door (you have enough Kronole)");
                            npc_dialogue_set_context(&dlg, "the rebel",
                                                     bg.wagonActif, quest);
                            dialogue_cinematic_start(&cinematic, &dlg,
                                gp.namgoong.x + 32.0f, gp.namgoong.y + 32.0f,
                                bg.joueurs[0].x + bg.joueurs[0].w/2.0f,
                                bg.joueurs[0].y + bg.joueurs[0].h/2.0f);
                            audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        }
                    }
                }
                if (etat == ETAT_GAMEOVER) {
                    if (ev.key.keysym.sym == SDLK_RETURN) { etat = ETAT_SAISIE; }
                    if (ev.key.keysym.sym == SDLK_ESCAPE) etat = ETAT_SCORES;
                }
                if (etat == ETAT_ENDING) {
                    if (ev.key.keysym.sym == SDLK_RETURN ||
                        ev.key.keysym.sym == SDLK_ESCAPE) {
                        etat = ETAT_MENU;
                    }
                }
                else if (etat == ETAT_SAUVEGARDE) {
                    if (ev.key.keysym.sym == SDLK_LEFT  || ev.key.keysym.sym == SDLK_a)
                        sauvegardeSelected = 0;
                    if (ev.key.keysym.sym == SDLK_RIGHT || ev.key.keysym.sym == SDLK_d)
                        sauvegardeSelected = 1;
                    if (ev.key.keysym.sym == SDLK_RETURN) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        if (sauvegardeScreen == 0) {
                            if (sauvegardeSelected == 0) {
                                sauvegardeScreen   = 1;
                                sauvegardeSelected = 0;
                            } else {
                                g_setup_target_slot = 0;
                                weapon_select_enter(0);
                                etat = ETAT_WEAPON_SELECT;
                            }
                        } else {
                            if (sauvegardeSelected == 0) {
                                if (charger_partie(&bg, &gp, &scoreJoueur, &bg.tempsDepart)) {
                                    lastWagonSeen = bg.wagonActif;
                                    boss_monologue_reset_run(&boss_mono);
                                    wagon_barker_init(&barker);
                                    etat = ETAT_JEU;
                                } else {
                                    g_setup_target_slot = 0;
                                weapon_select_enter(0);
                                etat = ETAT_WEAPON_SELECT;
                                }
                            } else {
                                g_setup_target_slot = 0;
                                weapon_select_enter(0);
                                etat = ETAT_WEAPON_SELECT;
                            }
                            sauvegardeScreen = 0;
                        }
                    }
                    if (ev.key.keysym.sym == SDLK_ESCAPE ||
                        ev.key.keysym.sym == SDLK_BACKSPACE) { /* controller BACK */
                        etat = ETAT_MENU;
                    }
                }
                else if (etat == ETAT_MODE_SELECT) {
                    ModeSelectAction act =
                        mode_select_handle_key(ev.key.keysym.sym);
                    if (ev.key.keysym.sym == SDLK_BACKSPACE) /* controller BACK = cancel */
                        act = MS_ACTION_CANCEL;
                    if (act == MS_ACTION_CONFIRM) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        if (g_game_mode == MODE_CAMPAIGN) {
                            sauvegardeScreen   = 1;
                            sauvegardeSelected = 0;
                            ui_button_state_reset(&stSaveB);
                            etat = ETAT_SAUVEGARDE;
                        } else {
                            g_setup_target_slot = 0;
                            weapon_select_enter(0);
                            etat = ETAT_WEAPON_SELECT;
                        }
                    } else if (act == MS_ACTION_CANCEL) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        etat = ETAT_MENU;
                    }
                }
                else if (etat == ETAT_TWOPLAYER_TODO) {
                    SDL_Keycode k = ev.key.keysym.sym;
                    if (k == SDLK_RETURN || k == SDLK_SPACE || k == SDLK_ESCAPE ||
                        k == SDLK_BACKSPACE) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        etat = ETAT_MENU;
                    }
                }
                else if (etat == ETAT_WEAPON_SELECT) {
                    WeaponSelectAction act =
                        weapon_select_handle_key(ev.key.keysym.sym);
                    if (ev.key.keysym.sym == SDLK_BACKSPACE) /* controller BACK = cancel */
                        act = WS_ACTION_CANCEL;
                    if (act == WS_ACTION_CONFIRM) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        skin_select_enter(renderer, g_setup_target_slot);
                        etat = ETAT_SKIN_SELECT;
                    } else if (act == WS_ACTION_CANCEL) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        mode_select_enter();
                        etat = ETAT_MODE_SELECT;
                    }
                }
                else if (etat == ETAT_SKIN_SELECT) {
                    SkinSelectAction act =
                        skin_select_handle_key(ev.key.keysym.sym);
                    if (ev.key.keysym.sym == SDLK_BACKSPACE) /* controller BACK = cancel */
                        act = SS_ACTION_CANCEL;
                    if (act == SS_ACTION_CONFIRM) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        if (g_game_mode == MODE_CAMPAIGN) {
                            start_new_run(&bg, &gp, &boss_mono, &barker,
                                          &cut, renderer,
                                          &scoreJoueur, &lastWagonSeen,
                                          MODE_MONO);
                            etat = ETAT_CUTSCENE;
                        } else if (g_setup_target_slot == 0) {
                            g_setup_target_slot = 1;
                            weapon_select_enter(1);
                            etat = ETAT_WEAPON_SELECT;
                        } else {
                            if (g_game_mode == MODE_COOP) {
                                start_new_run(&bg, &gp, &boss_mono, &barker,
                                              &cut, renderer,
                                              &scoreJoueur, &lastWagonSeen,
                                              MODE_MULTI);
                                etat = ETAT_CUTSCENE;
                            } else if (g_game_mode == MODE_VERSUS) {
                                /* Versus fills the desktop end-to-end.
                                 * Go desktop-fullscreen and disable the
                                 * campaign's 800×520 logical letterbox by
                                 * calling SDL_RenderSetLogicalSize(0, 0)
                                 * — without this disable the campaign's
                                 * letterboxed canvas would persist into
                                 * PvP and the bars come back.  No render
                                 * scale either: versus_render_world reads
                                 * the real window size via SDL_GetWindowSize
                                 * and renders the bg edge-to-edge.        */
                                versus_enter_window();
                                versus_begin_match(&vs_state, &bg, &gp);
                                etat = ETAT_VERSUS;
                            } else {
                                etat = ETAT_TWOPLAYER_TODO;
                            }
                        }
                    } else if (act == SS_ACTION_CANCEL) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        weapon_select_enter(g_setup_target_slot);
                        etat = ETAT_WEAPON_SELECT;
                    }
                }
                else if (etat == ETAT_VERSUS) {
                    SDL_Keycode k = ev.key.keysym.sym;
                    /* Per-player attack press-edges.  keyShoot* / keyMelee*
                     * are zeroed at the top of the main loop every frame,
                     * so two_player_poll() picks each press up exactly once
                     * via player_input[].shoot / .melee.  Only honour the
                     * keys during live combat — match-end + intro freeze.   */
                    if (vs_state.phase == VS_PHASE_FIGHT) {
                        if (k == SDLK_f) { keyShoot  = 1; audio_play_sfx(&audio, SFX_ATTACK); }
                        if (k == SDLK_e) { keyMelee  = 1; audio_play_sfx(&audio, SFX_ATTACK); }
                        if (k == SDLK_k) { keyShoot2 = 1; audio_play_sfx(&audio, SFX_ATTACK); }
                        if (k == SDLK_l) { keyMelee2 = 1; audio_play_sfx(&audio, SFX_ATTACK); }
                        /* Dash: same buttons as campaign — LShift for P1, RShift for P2. */
                        if (k == SDLK_LSHIFT) {
                            versus_handle_dash_input(&gp, &bg, 0);
                            audio_play_sfx(&audio, SFX_JUMP);
                        }
                        if (k == SDLK_RSHIFT) {
                            versus_handle_dash_input(&gp, &bg, 1);
                            audio_play_sfx(&audio, SFX_JUMP);
                        }
                    }
                    versus_handle_key(&vs_state, k);
                }
                else if (etat == ETAT_MENU) {
                    SDL_Keycode k = ev.key.keysym.sym;
                    if (k == SDLK_UP || k == SDLK_w)
                        menuSelected = (menuSelected + 4) % 5;
                    if (k == SDLK_DOWN || k == SDLK_s)
                        menuSelected = (menuSelected + 1) % 5;
                    if (k == SDLK_RETURN) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        switch (menuSelected) {
                            case 0:
                                mode_select_enter();
                                etat = ETAT_MODE_SELECT;
                                break;
                            case 1: optionsReturn = ETAT_MENU; etat = ETAT_OPTIONS; break;
                            case 2: {
                                int quit = highscore_screen_run(fenetre, renderer);
                                lastTrackState = AUDIO_TRACK_NONE;
                                lastTick = SDL_GetTicks();
                                if (quit) continuer = 0;
                                break;
                            }
                            case 3: {
                                int quit = histoire_screen_run(fenetre, renderer, "assets/video.mp4");
                                lastTrackState = AUDIO_TRACK_NONE;
                                lastTick = SDL_GetTicks();
                                if (quit) continuer = 0;
                                break;
                            }
                            case 4: continuer = 0; break;
                        }
                    }
                    if (k == SDLK_j) {
                        menuSelected = 0;
                        mode_select_enter();
                        etat = ETAT_MODE_SELECT;
                    }
                    if (k == SDLK_o) { menuSelected = 1; optionsReturn = ETAT_MENU; etat = ETAT_OPTIONS; }
                    if (k == SDLK_m) {
                        menuSelected = 2;
                        int quit = highscore_screen_run(fenetre, renderer);
                        lastTrackState = AUDIO_TRACK_NONE;
                        lastTick = SDL_GetTicks();
                        if (quit) continuer = 0;
                    }
                    if (k == SDLK_h) {
                        menuSelected = 3;
                        int quit = histoire_screen_run(fenetre, renderer, "assets/video.mp4");
                        lastTrackState = AUDIO_TRACK_NONE;
                        lastTick = SDL_GetTicks();
                        if (quit) continuer = 0;
                    }
                    if (k == SDLK_ESCAPE) continuer = 0;
                    if (k == SDLK_F1) {
                        /* Open the standalone char-anim debug viewer. */
                        etat = ETAT_CHAR_DEBUG;
                        dbgLoadedSkin   = SKIN_COUNT;   /* force reload   */
                        dbgLoadedWeapon = WEAPON_COUNT;
                    }
                }
                else if (etat == ETAT_CHAR_DEBUG) {
                    SDL_Keycode k = ev.key.keysym.sym;
                    if (k == SDLK_ESCAPE) {
                        etat = ETAT_MENU;
                    }
                    else if (k == SDLK_LEFT  || k == SDLK_a)
                        dbgSkin = (Skin)((dbgSkin + SKIN_COUNT - 1) % SKIN_COUNT);
                    else if (k == SDLK_RIGHT || k == SDLK_d)
                        dbgSkin = (Skin)((dbgSkin + 1) % SKIN_COUNT);
                    else if (k == SDLK_UP    || k == SDLK_w)
                        dbgWeapon = (Weapon)((dbgWeapon + WEAPON_COUNT - 1) % WEAPON_COUNT);
                    else if (k == SDLK_DOWN  || k == SDLK_s)
                        dbgWeapon = (Weapon)((dbgWeapon + 1) % WEAPON_COUNT);
                    else if (k == SDLK_PAGEUP || k == SDLK_LEFTBRACKET) {
                        dbgAnim = (AnimType)((dbgAnim + ANIM_COUNT - 1) % ANIM_COUNT);
                        dbgCa.is_locked = 0;
                        char_anim_set(&dbgCa, dbgAnim);
                    } else if (k == SDLK_PAGEDOWN || k == SDLK_RIGHTBRACKET) {
                        dbgAnim = (AnimType)((dbgAnim + 1) % ANIM_COUNT);
                        dbgCa.is_locked = 0;
                        char_anim_set(&dbgCa, dbgAnim);
                    } else if (k == SDLK_f) {
                        dbgFlip = !dbgFlip;
                    } else if (k == SDLK_r) {
                        /* replay the current one-shot */
                        dbgCa.is_locked = 0;
                        char_anim_set(&dbgCa, ANIM_IDLE);
                        char_anim_set(&dbgCa, dbgAnim);
                    }
                }
                else if (etat == ETAT_OPTIONS) {
                    SDL_Keycode k = ev.key.keysym.sym;
                    if (k == SDLK_BACKSPACE) etat = optionsReturn; /* controller BACK = on-screen BACK */
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
                    if (k == SDLK_RETURN) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        switch (optionsSelected) {
                            case 0:
                                volumeLevel -= 16;
                                if (volumeLevel < 0) volumeLevel = 0;
                                audio_set_music_volume(&audio, volumeLevel);
                                audio_set_sfx_volume(&audio, volumeLevel);
                                break;
                            case 1:
                                volumeLevel += 16;
                                if (volumeLevel > MIX_MAX_VOLUME) volumeLevel = MIX_MAX_VOLUME;
                                audio_set_music_volume(&audio, volumeLevel);
                                audio_set_sfx_volume(&audio, volumeLevel);
                                break;
                            case 2:
                                fullscreenMode = 0;
                                SDL_SetWindowFullscreen(fenetre, 0);
                                SDL_RenderSetLogicalSize(renderer, W_ECRAN, H_ECRAN);
                                break;
                            case 3:
                                fullscreenMode = 1;
#ifndef __EMSCRIPTEN__   /* OS fullscreen black-screens the web canvas */
                                SDL_SetWindowFullscreen(fenetre, SDL_WINDOW_FULLSCREEN_DESKTOP);
#endif
                                SDL_RenderSetLogicalSize(renderer, W_ECRAN, H_ECRAN);
                                break;
                            case 4: etat = optionsReturn; break; /* BACK -> menu or pause */
                        }
                    }
                    if (k == SDLK_PLUS || k == SDLK_EQUALS || k == SDLK_KP_PLUS) {
                        volumeLevel += 16;
                        if (volumeLevel > MIX_MAX_VOLUME) volumeLevel = MIX_MAX_VOLUME;
                        audio_set_music_volume(&audio, volumeLevel);
                        audio_set_sfx_volume(&audio, volumeLevel);
                        optionsSelected = 1;
                    }
                    if (k == SDLK_MINUS || k == SDLK_KP_MINUS) {
                        volumeLevel -= 16;
                        if (volumeLevel < 0) volumeLevel = 0;
                        audio_set_music_volume(&audio, volumeLevel);
                        audio_set_sfx_volume(&audio, volumeLevel);
                        optionsSelected = 0;
                    }
                    if (k == SDLK_f) {
                        fullscreenMode = 1;
#ifndef __EMSCRIPTEN__   /* OS fullscreen black-screens the web canvas */
                        SDL_SetWindowFullscreen(fenetre, SDL_WINDOW_FULLSCREEN_DESKTOP);
#endif
                        SDL_RenderSetLogicalSize(renderer, W_ECRAN, H_ECRAN);
                        optionsSelected = 3;
                    }
                    if (k == SDLK_n) {
                        fullscreenMode = 0;
                        SDL_SetWindowFullscreen(fenetre, 0);
                        SDL_RenderSetLogicalSize(renderer, W_ECRAN, H_ECRAN);
                        optionsSelected = 2;
                    }
                    if (k == SDLK_ESCAPE) etat = optionsReturn;
                }
                else if (etat == ETAT_SCORES) {
                    SDL_Keycode k = ev.key.keysym.sym;
                    if (k == SDLK_BACKSPACE) etat = ETAT_MENU; /* controller BACK -> main menu */
                    if (k == SDLK_LEFT  || k == SDLK_a) scoresSelected = 0;
                    if (k == SDLK_RIGHT || k == SDLK_d) scoresSelected = 1;
                    if (k == SDLK_RETURN) {
                        audio_play_sfx(&audio, SFX_BUTTON_CLICK);
                        if (scoresSelected == 0) etat = ETAT_MENU;
                        else continuer = 0;
                    }
                    if (k == SDLK_ESCAPE) continuer = 0;
                    if (k == SDLK_v) {
                        char saved[1024]; if (!getcwd(saved, sizeof(saved))) saved[0]=0;
                        Player ps[100];
                        int npl = 0;
                        if (chdir("high score menu ") == 0) {
                            npl = loadPlayers(ps);
                            sortPlayers(ps, npl);
                            if (saved[0]) chdir(saved);
                        }
                        Uint32 untilTick = SDL_GetTicks() + 6000;
                        while (SDL_GetTicks() < untilTick) {
                            SDL_Event lev;
                            int leave = 0;
                            while (SDL_PollEvent(&lev)) {
                                if (lev.type == SDL_QUIT) { continuer = 0; leave = 1; break; }
                                if (lev.type == SDL_KEYDOWN || lev.type == SDL_MOUSEBUTTONDOWN) {
                                    leave = 1; break;
                                }
                            }
                            if (leave) break;
                            SDL_SetRenderDrawColor(renderer, 8, 12, 22, 255);
                            SDL_RenderClear(renderer);
                            SDL_Color amber = {235,180,60,255};
                            SDL_Color white = {235,235,235,255};
                            ui_draw_text_centered(&tcache, renderer, fonts.bebasTitle,
                                "LEADERBOARD", amber, W_ECRAN/2, 40);
                            ui_draw_text_centered(&tcache, renderer, fonts.story,
                                "high score menu / test.txt",
                                (SDL_Color){180,200,220,255}, W_ECRAN/2, 80);
                            int rows = npl < 10 ? npl : 10;
                            for (int i = 0; i < rows; i++) {
                                char rb[128];
                                snprintf(rb, sizeof(rb), "%2d. %-20s %d",
                                    i+1, ps[i].name, ps[i].score);
                                SDL_Color c = (i == 0) ? amber : white;
                                ui_draw_text_centered(&tcache, renderer, fonts.mono,
                                    rb, c, W_ECRAN/2, 130 + i * 26);
                            }
                            ui_draw_text_centered(&tcache, renderer, fonts.mono,
                                "[Touche] Retour",
                                (SDL_Color){180,190,210,255}, W_ECRAN/2, H_ECRAN - 30);
                            SDL_RenderPresent(renderer);
                            SDL_Delay(16);
                        }
                    }
                }
                else if (etat == ETAT_QUIZ) {
                    quiz_handle_key(&quiz, ev.key.keysym.sym, &audio);
                }
            }
            else if (ev.type == SDL_KEYUP) {
                keysHeld[ev.key.keysym.sym & 0x1FF] = 0;
            }
        }

        /* Hitstop must tick every frame, even when the world is frozen
         * (cinematic / boss intro own their own freezes), so the boss-
         * segment hitstop doesn't get extended by overlapping pauses.
         * Decrement here OUTSIDE all gates with the raw frame dt. */
        hitstop_tick(dt);
        /* Screen shake is visual feedback only — keep it ticking during a
         * hitstop freeze so the segment-break shake is visible.  The
         * existing in-gate gameplay_update_shake (called from gameplay_update)
         * still runs during normal play, so this only fires while the
         * world-sim gate is closed by hitstop.  Other freezes (dialogue
         * cinematic / boss intro) keep their original behaviour. */
        if (etat == ETAT_JEU && hitstop_active())
            gameplay_update_shake(&gp, dt);

        /* World simulation: gated off entirely while the dialogue cinematic
         * has the world frozen (ENTER + ACTIVE stages), the boss intro is
         * mid-stage, or a hitstop is in progress.  Rendering still runs
         * below so the freeze is visible. */
        if (etat == ETAT_JEU && !dialogue_cinematic_freezes_world(&cinematic)
                              && !boss_intro_freezes_world()
                              && !boss_cutscene_freezes_world()
                              && !boss_ai_world_frozen()
                              && !hitstop_active()) {
            /* Centralised input snapshot — both co-op and the upcoming
             * versus arena read from player_input[] instead of poking
             * keysHeld directly.                                       */
            two_player_poll(keysHeld, keyShoot, keyMelee,
                                       keyShoot2, keyMelee2);

            int prevHp1 = bg.joueurs[0].hp;
            int prevHp2 = bg.joueurs[1].hp;

            bg.blockTransitionRight =
                (gp.gameMode == MODE_MONO && gp.door.state != DOOR_OPEN);

            plateformes_update(&bg);

            int prevBalles0 = bg.nbBalles[0];
            int prevBalles1 = bg.nbBalles[1];

            int dashOverride0 = gp.dash[0].active;
            int dashOverride1 = gp.dash[1].active;

            joueur_update_interieur_idx(&bg, 0,
                dashOverride0 ? 0 : player_input[0].gauche,
                dashOverride0 ? 0 : player_input[0].droite,
                player_input[0].sauter,
                player_input[0].shoot,
                player_input[0].melee, dt);

            if (bg.nbJoueursActifs >= 2)
                joueur_update_interieur_idx(&bg, 1,
                    dashOverride1 ? 0 : player_input[1].gauche,
                    dashOverride1 ? 0 : player_input[1].droite,
                    player_input[1].sauter,
                    player_input[1].shoot,
                    player_input[1].melee, dt);

            if (bg.nbBalles[0] > prevBalles0 && gp.ammo[0] > 0) gp.ammo[0]--;
            if (bg.nbJoueursActifs >= 2 && bg.nbBalles[1] > prevBalles1 && gp.ammo[1] > 0)
                gp.ammo[1]--;

            ennemis_update(bg.ennemis, &bg.nbEnnemis, bg.wagonActif, dt,
                           bg.joueurs, bg.nbJoueursActifs,
                           WAGON_SOL_Y, WAGON_PLAF_Y);

            int livingBefore = 0, livingAfter = 0;
            for (int i = 0; i < bg.nbEnnemis; i++)
                if (bg.ennemis[i].actif && bg.ennemis[i].etat != ES_DEAD) livingBefore++;

            for (int p = 0; p < bg.nbJoueursActifs; p++)
                balles_update_fx(bg.balles[p], &bg.nbBalles[p],
                                 bg.ennemis, &bg.nbEnnemis,
                                 &bg.particles, bg.hitFx, &bg.nbHitFx,
                                 bg.hitFxSheet);
            hitfx_update(bg.hitFx, &bg.nbHitFx, dt);

            for (int i = 0; i < bg.nbEnnemis; i++)
                if (bg.ennemis[i].actif && bg.ennemis[i].etat != ES_DEAD) livingAfter++;
            if (livingAfter < livingBefore)
                audio_play_sfx(&audio, SFX_ENEMY_DEATH);

            if (bg.joueurs[0].hp < prevHp1 || bg.joueurs[1].hp < prevHp2) {
                audio_play_sfx(&audio, SFX_PLAYER_HURT);
                if (bg.joueurs[0].hp < prevHp1) arduino_input_send_event(ctrl1, "HIT");
                if (bg.joueurs[1].hp < prevHp2) arduino_input_send_event(ctrl2, "HIT");
            }

            for (int p = 0; p < bg.nbJoueursActifs; p++)
                weapon_swing_update(&bg.swings[p], dt);
            particles_update(&bg.particles, dt);
            effects_update(&effects, dt);

            transitionSlide_update(&bg);
            /* door_anim must run AFTER transitionSlide_update so it can clamp
             * the slide offset back to 0 before kinetic_present_world reads
             * it for the camera pan — see door_anim.c. */
            door_anim_update(dt);
            flash_update(&bg);
            neige_update(&bg);
            kinetic_update(&kin, dt);

            scoreJoueur = (int)((SDL_GetTicks() - bg.tempsDepart) / 100);

            gameplay_update(&gp, &bg, &audio, dt);
            damage_numbers_update(dt);

            /* --- boss_anim + boss_hud bookkeeping ----------------------
             * The final boss is the lone ET_ENFORCER in MAX_WAGONS-1.  We
             * find it each frame, drive the animation state machine, watch
             * the 50% / 15% / 0% HP thresholds, and update the HUD.
             *
             * The two sync points (rage @ 50%, stagger @ 15%) fire both
             * the boss_anim trigger AND the matching boss_hud trigger on
             * the same frame; that's the contract the two modules share
             * through (and only through) the boss HP value.
             *
             * Reset thresholds + module state on every fresh entry into
             * the boss wagon so death+revive / save+load loops work.   */
            {
                static int s_in_boss_wagon    = 0;
                static int s_hurt80_fired     = 0;
                static int s_hurt60_fired     = 0;
                static int s_hurt40_fired     = 0;
                static int s_rage50_fired     = 0;
                static int s_slam20_fired     = 0;
                static int s_boss_death_fired = 0;
                static int s_taunt_armed      = 0;
                static int s_intro_was_active = 0;

                int in_boss_wagon = (bg.wagonActif == MAX_WAGONS - 1);

                if (in_boss_wagon && !s_in_boss_wagon) {
                    /* Fresh entry into the boss wagon — re-arm everything. */
                    int bossMaxHp = (int)((float)ENFORCER_HP *
                                          ENEMY_HP_BASE_MULT * BOSS_HP_MULT);
                    boss_anim_reset();
                    boss_hud_reset(bossMaxHp);
                    boss_ai_reset();
                    s_hurt80_fired = s_hurt60_fired = s_hurt40_fired = 0;
                    s_rage50_fired = s_slam20_fired = 0;
                    s_boss_death_fired = 0;
                    s_taunt_armed      = 0;
                    s_intro_was_active = boss_intro_active();
                }
                s_in_boss_wagon = in_boss_wagon;

                if (in_boss_wagon) {
                    int boss_idx = -1;
                    for (int i = 0; i < bg.nbEnnemis; i++) {
                        Ennemi *e = &bg.ennemis[i];
                        if (!e->actif) continue;
                        if (e->wagon != MAX_WAGONS - 1) continue;
                        if (e->type == ET_ENFORCER) { boss_idx = i; break; }
                    }
                    int boss_hp     = (boss_idx >= 0) ? bg.ennemis[boss_idx].hp : 0;
                    int bossMaxHp   = (int)((float)ENFORCER_HP *
                                            ENEMY_HP_BASE_MULT * 1.40f);

                    int intro_now = boss_intro_active();
                    int intro_ever_played = !boss_intro_should_start();
                    if (!boss_hud_is_visible() && !s_boss_death_fired) {
                        if ((s_intro_was_active && !intro_now) ||
                            (!s_intro_was_active && intro_ever_played))
                            boss_hud_show();
                    }
                    /* Arm the BOSS_POINT pre-fight taunt: every time control
                     * returns to the player after the intro (including auto-
                     * skipped intros on retries), the AI plays its taunt and
                     * blocks AI until it finishes. */
                    if (!s_taunt_armed) {
                        int intro_done_now    = s_intro_was_active && !intro_now;
                        int intro_skipped_now = (!s_intro_was_active && intro_ever_played);
                        if (intro_done_now || intro_skipped_now) {
                            boss_ai_arm_taunt();
                            s_taunt_armed = 1;
                        }
                    }
                    s_intro_was_active = intro_now;

                    /* HP-threshold events (fire once each, in HP order). */
                    if (boss_idx >= 0 && boss_hp > 0) {
                        int t80 = (bossMaxHp * 80) / 100;
                        int t60 = (bossMaxHp * 60) / 100;
                        int t50 = (bossMaxHp * 50) / 100;
                        int t40 = (bossMaxHp * 40) / 100;
                        int t20 = (bossMaxHp * 20) / 100;
                        if (!s_hurt80_fired && boss_hp <= t80) {
                            boss_ai_event_hurt_segment(0); s_hurt80_fired = 1; }
                        if (!s_hurt60_fired && boss_hp <= t60) {
                            boss_ai_event_hurt_segment(1); s_hurt60_fired = 1; }
                        if (!s_rage50_fired && boss_hp <= t50) {
                            boss_ai_event_rage_50();
                            boss_hud_pulse_name();
                            s_rage50_fired = 1; }
                        if (!s_hurt40_fired && boss_hp <= t40) {
                            boss_ai_event_hurt_segment(2); s_hurt40_fired = 1; }
                        if (!s_slam20_fired && boss_hp <= t20) {
                            boss_ai_event_slam_20();
                            boss_hud_flash_border();
                            s_slam20_fired = 1; }
                    }

                    /* Boss dead → request the death animation; HUD slides
                     * off; the outro fires once boss_ai_should_outro flips. */
                    if (!s_boss_death_fired &&
                        (boss_idx < 0 || boss_hp <= 0)) {
                        boss_ai_request_death();
                        boss_hud_hide();
                        s_boss_death_fired = 1;
                    }

                    /* boss_anim + boss_ai are ticked outside the gate
                     * (see above) so they keep advancing during taunt /
                     * segment freezes.  Only HUD bookkeeping (HP lerp +
                     * segment-break VFX) lives in the world-sim block. */
                    boss_hud_update(dt, boss_hp);
                }
            }

            if (bg.wagonActif > lastWagonSeen) {
                int from_wagon = lastWagonSeen + 1;
                lastWagonSeen = bg.wagonActif;
                int bonus = 0;
                InterludeType t = get_interlude_type(from_wagon);
                if (t != INTERLUDE_NONE) {
#ifdef __EMSCRIPTEN__
                    /* The between-wagon interludes (wire puzzle / quiz / jigsaw)
                     * are self-contained blocking minigames — each runs its own
                     * while(SDL_PollEvent)+SDL_Delay loop.  That never returns to
                     * the browser event loop, so it hangs the tab (the "crash"
                     * crossing into the next wagon).  The wagon has already
                     * advanced, so the interlude is a pure bonus interstitial:
                     * skip it on web until it's reworked as a frame-driven state.
                     * TODO(web): port interludes to one_frame() states.        */
                    (void)bonus;
#else
                    interlude_run(t, renderer, fenetre, &bonus, from_wagon,
                                  &fonts, &tcache, &assets, &audio, &gp);
                    scoreJoueur += bonus;
                    lastTrackState = AUDIO_TRACK_NONE;
                    lastTick = SDL_GetTicks();
                    SDL_Event pending;
                    while (SDL_PollEvent(&pending))
                        if (pending.type == SDL_QUIT) { continuer = 0; break; }
#endif
                }
            }

            /* Boss-death trigger.  In wagon 5 we now wait for the boss_ai
             * BOSS_DEATH animation to finish (boss_ai_should_outro) before
             * firing the outro.  Outside wagon 5 the path is unchanged. */
            if (bg.wagonActif == MAX_WAGONS - 1 && etat == ETAT_JEU) {
                int finalEnemiesAlive = 0;
                for (int e = 0; e < bg.nbEnnemis; e++) {
                    Ennemi *en = &bg.ennemis[e];
                    if (en->actif && en->wagon == bg.wagonActif &&
                        en->etat != ES_DEAD) finalEnemiesAlive++;
                }
                if (finalEnemiesAlive == 0 && boss_ai_should_outro()) {
                    finalScore   = scoreJoueur + gp.totalInterludeScore;
                    finalTimeSec = (int)((SDL_GetTicks() - bg.tempsDepart) / 1000);
                    finalWagon   = bg.wagonActif + 1;
                    outro_start(&outro, renderer, finalScore, finalTimeSec,
                                gp.enemiesKilled);
                    etat = ETAT_OUTRO;
                }
            }

            if (gp.gameMode == MODE_MONO && gp.namgoong.activeSolo &&
                gp.namgoong.hp <= 0) {
                etat         = ETAT_GAMEOVER;
                finalScore   = scoreJoueur + gp.totalInterludeScore;
                finalTimeSec = (int)((SDL_GetTicks() - bg.tempsDepart) / 1000);
                finalWagon   = bg.wagonActif + 1;
            }

            int tousMorts = !gameplay_anyone_alive(&bg, &gp);
            if (tousMorts) {
                if (bg.wagonActif == MAX_WAGONS - 1) {
                    /* Player died in the boss wagon — play BOSS_VICTORY
                     * before the loss screen.  The 0.5 s fade-to-black
                     * is rendered below; ETAT_GAMEOVER only after both
                     * the victory animation and fade finish. */
                    boss_ai_request_victory();
                    if (boss_ai_should_gameover()) {
                        if (s_player_death_fade_start == 0)
                            s_player_death_fade_start = SDL_GetTicks();
                        Uint32 fade_ms = SDL_GetTicks() - s_player_death_fade_start;
                        if (fade_ms >= 500u) {
                            etat         = ETAT_GAMEOVER;
                            finalScore   = scoreJoueur + gp.totalInterludeScore;
                            finalTimeSec = (int)((SDL_GetTicks() - bg.tempsDepart) / 1000);
                            finalWagon   = bg.wagonActif + 1;
                            s_player_death_fade_start = 0;
                        }
                    }
                } else {
                    etat         = ETAT_GAMEOVER;
                    finalScore   = scoreJoueur + gp.totalInterludeScore;
                    finalTimeSec = (int)((SDL_GetTicks() - bg.tempsDepart) / 1000);
                    finalWagon   = bg.wagonActif + 1;
                }
            }

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

        if (etat == ETAT_VERSUS) {
            /* Versus owns its own minimal tick — campaign systems
             * (doors, hazards, lives) don't apply.  hitstop_active()
             * gates the tick so PvP gets the same contact freeze as
             * campaign player attacks.                              */
            if (!hitstop_active()) {
                two_player_poll(keysHeld, keyShoot, keyMelee,
                                           keyShoot2, keyMelee2);
                versus_update(&vs_state, &bg, &gp, dt);
            }

            if (vs_state.request_rematch) {
                vs_state.request_rematch = 0;
                versus_begin_match(&vs_state, &bg, &gp);
            }
            if (vs_state.request_exit_to_menu) {
                vs_state.request_exit_to_menu = 0;
                /* Drop fullscreen + the versus stretch scale and restore
                 * the campaign window + logical canvas so the menu draws
                 * at its native 800×520. */
                versus_exit_window();
                etat = ETAT_MENU;
            }
        }

        if (etat == ETAT_QUIZ) {
            quiz_update(&quiz, &audio);
            if (quiz_is_finished(&quiz)) {
                scoreJoueur += quiz.bonusScore;
                etat = ETAT_JEU;
            }
        }

        if (etat == ETAT_CUTSCENE) {
            if (cutscene_update(&cut, dt)) {
                cutscene_free(&cut);
                /* Don't penalize the score timer for cutscene time. */
                bg.tempsDepart = SDL_GetTicks();
                etat = ETAT_JEU;
            }
        }

        if (etat == ETAT_OUTRO) {
            (void)outro_update(&outro, dt);
        }

        if (etat == ETAT_SAISIE) {
            scores_saisirNom(nomJoueur, sizeof(nomJoueur), renderer,
                              fonts.novaLabel ? fonts.novaLabel : fonts.fallback, jaune);
            scores_ajouterEtTrier(scores, &nbScores, nomJoueur, finalScore,
                                  finalTimeSec, finalWagon);
            scores_sauvegarder(FICHIER_SCORES, scores, nbScores);
            etat = ETAT_SCORES;
        }

        if (llm) llm_pump(llm);
        if (llm_queue) llm_queue_pump(llm_queue);
        npc_dialogue_update(&dlg, SDL_GetTicks());
        dialogue_cinematic_update(&cinematic, dt);
        /* boss_intro must tick even while the world is frozen (it owns the
         * freeze) — same placement logic as dialogue_cinematic_update. */
        boss_intro_update(dt);
        boss_cutscene_update(dt);
        /* boss_anim ticks outside the world-sim gate so it keeps advancing
         * during boss_ai's own segment-event freezes (HURT/RAGE/SLAM).   */
        if (etat == ETAT_JEU) boss_anim_update(dt);

        /* boss_ai itself must ALSO tick during its own freezes (TAUNT /
         * SEG_HURT / SEG_RAGE / SEG_SLAM / DYING / VICTORY) so those
         * locked animations can detect completion and transition out.
         * Gate only on external freezes + hitstop; deliberately omit
         * !boss_ai_world_frozen() (otherwise TAUNT and segment events
         * would self-deadlock — bug observed at end of boss intro:
         * boss points, screen stays frozen, no progression). */
        if (etat == ETAT_JEU && bg.wagonActif == MAX_WAGONS - 1
                             && !dialogue_cinematic_freezes_world(&cinematic)
                             && !boss_intro_freezes_world()
                             && !boss_cutscene_freezes_world()
                             && !hitstop_active()) {
            boss_ai_update(dt);
        }

        if (etat == ETAT_JEU) {
            int aliveInWagon = 0;
            for (int e = 0; e < bg.nbEnnemis; e++) {
                Ennemi *en = &bg.ennemis[e];
                if (en->actif && en->wagon == bg.wagonActif &&
                    en->etat != ES_DEAD) aliveInWagon++;
            }
            npc_dialogue_set_state(&dlg,
                aliveInWagon,
                gp.kronoleCount,
                KRONOLE_DOOR_COST,
                gp.door.state == DOOR_OPEN ? 1 : 0,
                gp.door.state == DOOR_UNLOCKING ? 1 : 0);

            /* Hush the background flavour text while the player is talking to
             * the NPC, so their (HIGH-priority) line isn't stuck behind a
             * freshly-queued barker/boss/taunt request on the single worker. */
            if (!npc_dialogue_is_active(&dlg)) {
                enemy_taunts_update(&bg, SDL_GetTicks(), llm_queue);
                wagon_barker_update(&barker, bg.wagonActif, SDL_GetTicks(), llm_queue);
                boss_monologue_update(&boss_mono, &bg, &gp,
                                       bg.tempsDepart, SDL_GetTicks(), llm_queue);
            }
        }

        /* Re-assert the logical-size letterbox every frame so a fullscreen
         * toggle (or any other state churn during this frame) can't leave the
         * presentation skewed / pillarboxed on only one side. */
        SDL_RenderSetLogicalSize(renderer, W_ECRAN, H_ECRAN);

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        if (etat == ETAT_JEU || etat == ETAT_PAUSE ||
            (etat == ETAT_OUTRO && outro_should_render_world(&outro))) {
            /* While the boss cutscene owns the screen, replace the wagon
             * world + HUD entirely with the cutscene's black-background
             * sprite reveal.  Dialogue chrome still paints on top via the
             * fallthrough else-branch below. */
            if (boss_cutscene_active()) {
                boss_cutscene_render();
                if (dialogue_cinematic_is_active(&cinematic))
                    dialogue_cinematic_render_chrome(&cinematic, renderer,
                                                      &fonts, &tcache);
            } else {
            int shx = gp.shakeOffsetX;
            int shy = gp.shakeOffsetY;

            /* (The cinematic used to push SDL_RenderSetScale(zoom, zoom) here
             * to get a 1.15x zoom-in, but SDL_RenderSetScale overrides the
             * scale that SDL_RenderSetLogicalSize installed for letterboxing,
             * so in fullscreen the letterbox bars + dialogue text ended up
             * mapped to physical pixels (0..800, 0..520) -- the top-left
             * corner of the screen.  Pan-via-ox still works and gives the
             * "camera moves toward speaker" half of the effect; the literal
             * magnification is dropped.) */

            /* Build the torch light-map for the current wagon BEFORE any
             * world rendering, so kinetic_begin_world's render-target swap
             * can't interfere.  The compositing happens later in the world
             * pass (inside wagon_renderFullAt / wagonsInterieurs_afficher_camera). */
            effects_prepare_lightmap(&effects, renderer, bg.wagonActif);

            /* render the wagon world.  In the cinematic ("outside") view, with
             * render-targets available and a single shared screen, we draw into
             * the off-screen worldRT (three wagon-slots wide -- prev/current/
             * next); otherwise we draw straight to the screen as before. */
            int useRT = (bg.splitMode == 0) ? kinetic_begin_world(&kin, renderer) : 0;
            /* SDL_RenderSetViewport with a non-NULL rect overrides the centered
             * letterbox SDL_RenderSetLogicalSize installed; we re-apply
             * SetLogicalSize when done so everything after this block keeps the
             * fullscreen pillarbox instead of rendering flush-left. */
            int needShake = (shx != 0 || shy != 0);
            /* Look-at pan: shift world-entity draws toward the speaker
             * midpoint.  The wagon background isn't pan-aware so it stays
             * fixed; the letterbox bars hide the resulting edge mismatch. */
            int cin_pan = dialogue_cinematic_world_pan_ox(&cinematic)
                        + boss_intro_world_pan_ox();
            if (useRT) {
                /* the current wagon lives in slot 1 -> ox = W_ECRAN */
                wagonsKinetic_renderRT(&bg, renderer);
                gameplay_render_world(&gp, renderer, &bg, W_ECRAN + cin_pan);
                enemy_taunts_render(&bg, renderer, W_ECRAN + cin_pan, &fonts, &tcache);
                damage_numbers_render(renderer, W_ECRAN + cin_pan);
                /* hit-flash is a screen effect; drawn after the composite below */
            } else if (bg.splitMode == 0) {
                if (needShake) {
                    SDL_Rect vpShake = {shx, shy, W_ECRAN, H_ECRAN};
                    SDL_RenderSetViewport(renderer, &vpShake);
                }
                wagonsInterieurs_afficher(&bg, renderer);
                gameplay_render_world(&gp, renderer, &bg, cin_pan);
                enemy_taunts_render(&bg, renderer, cin_pan, &fonts, &tcache);
                damage_numbers_render(renderer, cin_pan);
                if (needShake) SDL_RenderSetLogicalSize(renderer, W_ECRAN, H_ECRAN);
                flash_afficher(&bg, renderer);
            } else {
                SDL_Rect vpL = {shx, shy, 399, H_ECRAN};
                SDL_RenderSetViewport(renderer, &vpL);
                int ox_left = 399/2 - (int)(bg.joueurs[0].x + JOUEUR_W/2);
                if (ox_left > -WAGON_MUR_G_X)      ox_left = -WAGON_MUR_G_X;
                if (ox_left < 399 - WAGON_MUR_D_X) ox_left = 399 - WAGON_MUR_D_X;
                wagonsInterieurs_afficher_camera(&bg, renderer, ox_left);
                gameplay_render_world(&gp, renderer, &bg, ox_left);
                enemy_taunts_render(&bg, renderer, ox_left, &fonts, &tcache);
                damage_numbers_render(renderer, ox_left);

                SDL_RenderSetLogicalSize(renderer, W_ECRAN, H_ECRAN);
                SDL_SetRenderDrawColor(renderer, 230, 140, 50, 255);
                SDL_Rect sep = {399, 0, 2, H_ECRAN};
                SDL_RenderFillRect(renderer, &sep);

                SDL_Rect vpR = {401 + shx, shy, 399, H_ECRAN};
                SDL_RenderSetViewport(renderer, &vpR);
                int ox_right = 399/2 - (int)(bg.joueurs[1].x + JOUEUR_W/2);
                if (ox_right > -WAGON_MUR_G_X)      ox_right = -WAGON_MUR_G_X;
                if (ox_right < 399 - WAGON_MUR_D_X) ox_right = 399 - WAGON_MUR_D_X;
                wagonsInterieurs_afficher_camera(&bg, renderer, ox_right);
                gameplay_render_world(&gp, renderer, &bg, ox_right);
                enemy_taunts_render(&bg, renderer, ox_right, &fonts, &tcache);
                damage_numbers_render(renderer, ox_right);

                SDL_RenderSetLogicalSize(renderer, W_ECRAN, H_ECRAN);
                flash_afficher(&bg, renderer);
            }
            kinetic_end_world(&kin, renderer);

            /* composite the kinetic train scene over the screen */
            kinetic_render_backdrop(&kin, renderer);     /* rushing frozen wasteland   */
            kinetic_render_wind_back(&kin, renderer);    /* wind streaks behind the car */
            kinetic_present_world(&kin, renderer,        /* the wagon(s), sliding      */
                                  bg.wagonActif, bg.transitionSlide.wagonCible,
                                  bg.transitionSlide.actif,
                                  bg.transitionSlide.direction,
                                  (int)bg.transitionSlide.offset);
            kinetic_render_hull(&kin, renderer);         /* hull copies, panned to match */
            if (useRT) flash_afficher(&bg, renderer);    /* hit-flash over the scene    */
            effects_render_flash(&effects, renderer);    /* warm death-flash over scene */

            /* Vignette darkens the still-visible world before any HUD/bars
             * land on top of it. */
            dialogue_cinematic_render_vignette(&cinematic, renderer);

            background_afficherTemps(&bg, renderer, 15, 8);
            minimap_afficher(&bg, renderer);
            {
                SDL_Color labelAmber = {180, 130, 40, 255};
                ui_draw_text_centered(&tcache, renderer,
                    fonts.monoSmall ? fonts.monoSmall : fonts.mono,
                    "CARTE", labelAmber, W_ECRAN - 90, 4);
            }
            fenetreGuide_afficher(&bg, renderer);

            /* Door transition overlay — drawn after the world / kinetic
             * composite, before any HUD chrome so it covers gameplay but
             * the HUD stays readable.  No-ops while idle. */
            door_anim_render();

            /* Boss HUD — painted after the world, before the existing
             * player HUD chrome.  No-op when invisible (boss intro hasn't
             * finished or boss is dead and slide-out complete). */
            boss_hud_render();

            int hudW = 600;
            int hudX = (W_ECRAN - hudW) / 2;
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 5, 8, 18, 210);
            SDL_Rect hud = {hudX, H_ECRAN-45, hudW, 45};
            SDL_RenderFillRect(renderer, &hud);
            SDL_SetRenderDrawColor(renderer, 100, 180, 255, 200);
            SDL_RenderDrawLine(renderer, hudX, H_ECRAN-46, hudX + hudW, H_ECRAN-46);

            char buf[64];
            if (bg.nbJoueursActifs >= 2)
                snprintf(buf, sizeof(buf), "Score : %d   PV1 : %d   PV2 : %d",
                         scoreJoueur, bg.joueurs[0].hp, bg.joueurs[1].hp);
            else
                snprintf(buf, sizeof(buf), "Score : %d   PV : %d", scoreJoueur, bg.joueurs[0].hp);
            ui_draw_text(&tcache, renderer, fonts.mono, buf, jaune, hudX + 15, H_ECRAN-42);

            const char *noms[MAX_WAGONS] = {
                "Wagon Pauvres","Wagon Industrie","Wagon Aquarium","Wagon Jardin","Wagon Luxe"
            };
            char wbuf[64];
            snprintf(wbuf, sizeof(wbuf), "%d/5  %s", bg.wagonActif+1, noms[bg.wagonActif]);
            ui_draw_text(&tcache, renderer, fonts.mono, wbuf, glacial, hudX + hudW - 200, H_ECRAN-42);

            gameplay_render_hud(&gp, renderer, &bg, &fonts, &tcache, &assets, scoreJoueur);

            if (gp.gameMode == MODE_MONO && gp.namgoong.activeSolo &&
                gp.namgoong.hp > 0 && !npc_dialogue_is_active(&dlg) &&
                bg.wagonActif == gp.namgoong.wagon) {
                float dx = bg.joueurs[0].x + bg.joueurs[0].w/2.0f
                         - (gp.namgoong.x + 32.0f);
                float dy = bg.joueurs[0].y + bg.joueurs[0].h/2.0f
                         - (gp.namgoong.y + 32.0f);
                if (dx*dx + dy*dy < 120.0f * 120.0f) {
                    SDL_Color hint = {255, 220, 120, 255};
                    ui_draw_text_centered(&tcache, renderer,
                        fonts.mono ? fonts.mono : fonts.fallback,
                        "[T] parler a Namgoong", hint,
                        (int)(gp.namgoong.x + 32), (int)(gp.namgoong.y - 18));
                }
            }

            /* Farewell bubble above namgoong's head — fires once when the
             * wagon 4 → 5 door finishes unlocking; lives for 6 s.        */
            if (gp.gameMode == MODE_MONO && gp.namgoong.activeSolo &&
                gp.namgoong.hp > 0 &&
                bg.wagonActif == gp.namgoong.wagon &&
                gp.namgoong.farewellTick != 0) {
                Uint32 nowT = SDL_GetTicks();
                Uint32 elapsedT = nowT - gp.namgoong.farewellTick;
                if (elapsedT < 6000u) {
                    TTF_Font *fBubble = fonts.mono ? fonts.mono : fonts.fallback;
                    const char *line = "You go without me. I'll hold the rest.";
                    int tw = 0, th = 0;
                    if (fBubble) TTF_SizeUTF8(fBubble, line, &tw, &th);
                    int pad_x = 10, pad_y = 6;
                    int bx = (int)(gp.namgoong.x + 32) - tw / 2 - pad_x;
                    int by = (int)(gp.namgoong.y - 56) - pad_y;
                    SDL_Rect box = { bx, by, tw + 2 * pad_x, th + 2 * pad_y };
                    /* fade out across the last 1.5 s */
                    Uint8 a = 230;
                    if (elapsedT > 4500u)
                        a = (Uint8)(230 * (6000 - elapsedT) / 1500);
                    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                    SDL_SetRenderDrawColor(renderer, 18, 22, 30, a);
                    SDL_RenderFillRect(renderer, &box);
                    SDL_SetRenderDrawColor(renderer, 235, 210, 130, a);
                    SDL_RenderDrawRect(renderer, &box);
                    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                    /* Center the line both horizontally and vertically inside
                     * the bubble rect — ui_draw_text_centered treats y as the
                     * text TOP, so it left text drifting low; in_rect centers
                     * on both axes. */
                    SDL_Color farewell = {245, 230, 180, a};
                    ui_draw_text_in_rect(&tcache, renderer, fBubble,
                                         line, farewell, box);
                }
            }

            wagon_barker_render(&barker, renderer, &fonts, &tcache);
            boss_monologue_render(&boss_mono, renderer, &fonts, &tcache);

            /* When the cinematic owns the screen we draw its bars + typewriter
             * inside them; the original full-window chatbot UI is suppressed so
             * the text actually appears INSIDE the bottom letterbox bar as
             * specified instead of floating in mid-screen on top of it.       */
            if (dialogue_cinematic_is_active(&cinematic))
                dialogue_cinematic_render_chrome(&cinematic, renderer,
                                                  &fonts, &tcache);
            else
                npc_dialogue_render(&dlg, renderer, &fonts, &tcache);

            /* Boss-name + subtitle layered on top of the cinematic chrome's
             * bottom letterbox bar.  No-op outside the REVEAL/EXIT stages. */
            boss_intro_render();

            /* Player-death fade-to-black overlay (boss wagon only).
             * Ramps alpha 0 → 255 over 500 ms once boss_ai_should_gameover
             * trips; ETAT_GAMEOVER then lands and this block stops drawing. */
            if (s_player_death_fade_start != 0) {
                Uint32 ms = SDL_GetTicks() - s_player_death_fade_start;
                float t   = (float)ms / 500.0f;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                Uint8 a = (Uint8)(255.0f * t);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, a);
                SDL_Rect fullF = {0, 0, W_ECRAN, H_ECRAN};
                SDL_RenderFillRect(renderer, &fullF);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            }

            if (etat == ETAT_PAUSE) {
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 175);
                SDL_Rect full = {0, 0, W_ECRAN, H_ECRAN};
                SDL_RenderFillRect(renderer, &full);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

                SDL_Color jauneP = {255, 215, 0, 255};
                SDL_Color grisP  = {205, 210, 225, 255};
                ui_draw_text_centered(&tcache, renderer, fonts.bebasTitle,
                    "PAUSE", jauneP, W_ECRAN/2, 95);

                UIButton btnsP[5]; int nP = layout_pause_screen(btnsP);
                ui_button_draw_all(renderer, assets.buttonTex, btnsP, nP,
                                    &stPause, pauseSelected, &tcache, fonts.novaButton);

                ui_draw_text_centered(&tcache, renderer, fonts.mono,
                    "[^/v] Navigate  [ENTER] Confirm  [ESC] Resume",
                    grisP, W_ECRAN/2, H_ECRAN - 25);
            }

            kinetic_render_wind_front(&kin, renderer);   /* wind over the HUD */

            /* Phase 1 of the outro renders the frozen wagon (via this
             * block) and then stacks the white flash + fade on top.   */
            if (etat == ETAT_OUTRO) {
                outro_render(&outro, renderer, &fonts, &tcache, &assets);
            }
            }   /* end of !boss_cutscene_active() branch */

        } else if (etat == ETAT_QUIZ) {
            quiz_render(&quiz, renderer, &assets, &fonts, &tcache);

        } else if (etat == ETAT_ENDING) {
            SDL_SetRenderDrawColor(renderer, 8, 12, 22, 255);
            SDL_RenderClear(renderer);
            SDL_Color jauneE = {255, 215, 0, 255};
            SDL_Color blancE = {235, 235, 235, 255};
            SDL_Color cyanE  = {120, 200, 240, 255};

            ui_draw_text_centered(&tcache, renderer, fonts.bebasTitle,
                "FIN DU VOYAGE", jauneE, W_ECRAN/2, 60);

            int score = finalScore;
            int kills = gp.enemiesKilled;
            int interlude = gp.totalInterludeScore;
            int survivors = 0;
            for (int p = 0; p < bg.nbJoueursActifs; p++)
                if (!gp.permanentlyDead[p] && bg.joueurs[p].hp > 0) survivors++;

            char rank = 'C';
            int total = score + interlude;
            if (total >= 8000 && kills >= 8 && survivors == bg.nbJoueursActifs) rank = 'S';
            else if (total >= 5000 && survivors >= 1) rank = 'A';
            else if (total >= 3000) rank = 'B';

            char rb[64];
            snprintf(rb, sizeof(rb), "Temps total : %02d:%02d", finalTimeSec/60, finalTimeSec%60);
            ui_draw_text_centered(&tcache, renderer, fonts.story, rb, blancE, W_ECRAN/2, 150);
            snprintf(rb, sizeof(rb), "Ennemis vaincus : %d", kills);
            ui_draw_text_centered(&tcache, renderer, fonts.story, rb, blancE, W_ECRAN/2, 180);
            snprintf(rb, sizeof(rb), "Score interludes : %d", interlude);
            ui_draw_text_centered(&tcache, renderer, fonts.story, rb, blancE, W_ECRAN/2, 210);
            snprintf(rb, sizeof(rb), "Survivants : %d / %d", survivors, bg.nbJoueursActifs);
            ui_draw_text_centered(&tcache, renderer, fonts.story, rb, blancE, W_ECRAN/2, 240);
            snprintf(rb, sizeof(rb), "RANG  %c", rank);
            ui_draw_text_centered(&tcache, renderer, fonts.bebasTitle, rb, cyanE, W_ECRAN/2, 290);

            ui_draw_text_centered(&tcache, renderer, fonts.mono,
                "[ENTREE / ECHAP] Retour menu",
                (SDL_Color){180,190,210,255}, W_ECRAN/2, H_ECRAN - 40);

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
            ui_draw_text_centered(&tcache, renderer, fonts.bebasTitle,
                "GAME OVER", rouge, W_ECRAN/2, 120);
            snprintf(buf, sizeof(buf), "Score final : %d", finalScore);
            ui_draw_text_centered(&tcache, renderer, fonts.story, buf, blanc, W_ECRAN/2, 220);
            snprintf(buf, sizeof(buf), "Temps : %02d:%02d", finalTimeSec/60, finalTimeSec%60);
            ui_draw_text_centered(&tcache, renderer, fonts.story, buf, blanc, W_ECRAN/2, 250);
            snprintf(buf, sizeof(buf), "Wagon atteint : %d/5", finalWagon);
            ui_draw_text_centered(&tcache, renderer, fonts.story, buf, blanc, W_ECRAN/2, 280);
            ui_draw_text_centered(&tcache, renderer, fonts.mono,
                "[ENTREE] Enregistrer votre score   [ECHAP] Menu des scores",
                grisC, W_ECRAN/2, H_ECRAN - 60);

        } else if (etat == ETAT_SCORES) {

            for (int i = 0; i < H_ECRAN; i++) {
                float t = (float)i / (float)H_ECRAN;
                Uint8 r = (Uint8)(40 * (1.0f - t) + 10 * t);
                Uint8 g = (Uint8)(10 * (1.0f - t) +  5 * t);
                Uint8 b = (Uint8)(60 * (1.0f - t) + 20 * t);
                SDL_SetRenderDrawColor(renderer, r, g, b, 255);
                SDL_RenderDrawLine(renderer, 0, i, W_ECRAN - 1, i);
            }

            for (int i = 0; i < 60; i++) {
                int sx = (i * 97 + 13) % W_ECRAN;
                int sy = ((i * 211 + 41) % (H_ECRAN - 80)) + 30;
                Uint8 br = 100 + (i * 7) % 155;
                SDL_SetRenderDrawColor(renderer, br, br, br * 7 / 10, 255);
                SDL_Rect dot = {sx, sy, 2, 2};
                SDL_RenderFillRect(renderer, &dot);
            }

            draw_scores_list(renderer, &fonts, &tcache, scores, nbScores);

            UIButton btns[2]; int n = layout_scores_bottom(btns);
            ui_button_draw_all(renderer, assets.buttonTex, btns, n,
                                &stScores, scoresSelected, &tcache, fonts.novaButton);
            ui_draw_text_centered(&tcache, renderer, fonts.mono,
                "[</>] Naviguer  [ENTREE] Valider  [V] Leaderboard  [ECHAP] Quitter",
                (SDL_Color){180, 190, 210, 255}, W_ECRAN/2, H_ECRAN - 30);

        } else if (etat == ETAT_MODE_SELECT) {
            mode_select_render(renderer, &fonts, &tcache, assets.bgSave, assets.buttonTex);

        } else if (etat == ETAT_VERSUS) {
            versus_render_world(&vs_state, renderer, fenetre, &bg, &gp);
            versus_render_hud(&vs_state, renderer, fenetre, &fonts, &tcache, &bg);

        } else if (etat == ETAT_TWOPLAYER_TODO) {
            draw_bg_image(renderer, assets.bgSave);
            SDL_Color jauneP  = {255, 215,   0, 255};
            SDL_Color blancP  = {220, 222, 232, 255};
            SDL_Color hintP   = {180, 190, 210, 230};
            const char *titlePlace = (g_game_mode == MODE_VERSUS)
                ? "VERSUS — A VENIR"
                : "CO-OP — A VENIR";
            const char *desc2 = (g_game_mode == MODE_VERSUS)
                ? "L'arene de duel arrive dans la suite."
                : "La campagne cooperative arrive dans la suite.";
            ui_draw_text_centered(&tcache, renderer, fonts.bebasTitle,
                                  titlePlace, jauneP, W_ECRAN/2, 130);
            ui_draw_text_centered(&tcache, renderer, fonts.story,
                                  "Le setup deux joueurs sera ajoute au prochain pas.",
                                  blancP, W_ECRAN/2, 220);
            ui_draw_text_centered(&tcache, renderer, fonts.story,
                                  desc2, blancP, W_ECRAN/2, 252);
            ui_draw_text_centered(&tcache, renderer, fonts.mono,
                                  "[ENTREE/ESPACE/ECHAP] Retour au menu",
                                  hintP, W_ECRAN/2, H_ECRAN - 30);

        } else if (etat == ETAT_WEAPON_SELECT) {
            weapon_select_render(renderer, &fonts, &tcache, assets.bgSave, assets.buttonTex);

        } else if (etat == ETAT_SKIN_SELECT) {
            skin_select_update(dt);
            skin_select_render(renderer, &fonts, &tcache, assets.bgSave, assets.buttonTex);

        } else if (etat == ETAT_SAUVEGARDE) {
            draw_bg_image(renderer, assets.bgSave);

            SDL_Color blanc2 = {255, 255, 255, 255};
            SDL_Color gris2  = {180, 180, 180, 255};
            ui_draw_text_centered(&tcache, renderer, fonts.bebasTitle,
                (sauvegardeScreen == 0) ? "SAVE" : "LOAD GAME",
                blanc2, W_ECRAN/2, 60);

            ui_draw_text_centered(&tcache, renderer, fonts.story,
                (sauvegardeScreen == 0)
                    ? "Do you want to save your game ?"
                    : "Resume your saved game ?",
                gris2, W_ECRAN/2, 180);

            UIButton btns[2]; int n = layout_save_screen(btns, sauvegardeScreen);
            UIButtonState *st = (sauvegardeScreen == 0) ? &stSaveA : &stSaveB;
            ui_button_draw_all(renderer, assets.buttonTex, btns, n,
                                st, sauvegardeSelected, &tcache, fonts.novaButton);

            ui_draw_text_centered(&tcache, renderer, fonts.mono,
                "[<-/->] Navigate  [ENTER] Confirm  [ESC] Back",
                gris2, W_ECRAN/2, H_ECRAN - 30);

        } else if (etat == ETAT_MENU) {
            draw_bg_image(renderer, assets.bgMenu);

            if (assets.titreTex) {
                int tw, th;
                SDL_QueryTexture(assets.titreTex, NULL, NULL, &tw, &th);
                int dw = 320, dh = dw * th / tw;
                SDL_Rect tdst = {W_ECRAN - dw - 40, 38, dw, dh};
                SDL_RenderCopy(renderer, assets.titreTex, NULL, &tdst);
            } else {
                SDL_Color jauneMenu = {255, 215, 0, 255};
                ui_draw_text_centered(&tcache, renderer, fonts.bebasTitle,
                    "SNOWPIERCER", jauneMenu, 620, 70);
            }

            if (assets.logoTex) {
                SDL_Rect ldst = {8, 8, 80, 79};
                SDL_RenderCopy(renderer, assets.logoTex, NULL, &ldst);
            }

            UIButton btns[5]; int n = layout_main_menu(btns);
            ui_button_draw_all(renderer, assets.buttonTex, btns, n,
                                &stMenu, menuSelected, &tcache, fonts.novaButton);

            SDL_Color hintCol = {200, 210, 230, 255};
            ui_draw_text_centered(&tcache, renderer, fonts.mono,
                "[MOUSE] Click  [^/v] Navigate  [ENTER] Confirm  [ESC] Quit",
                hintCol, W_ECRAN/2, H_ECRAN - 25);

        } else if (etat == ETAT_OPTIONS) {
            draw_bg_image(renderer, assets.bgOptions);

            SDL_Color jauneOpt = {255, 215,   0, 255};
            SDL_Color blancOpt = {255, 255, 255, 255};
            SDL_Color hintOpt  = {180, 190, 210, 255};

            ui_draw_text_centered(&tcache, renderer, fonts.bebasTitle,
                "OPTIONS", jauneOpt, W_ECRAN/2, 50);

            ui_draw_text(&tcache, renderer, fonts.novaButton, "VOLUME", blancOpt, 100, 150);
            char vbuf[32];
            snprintf(vbuf, sizeof(vbuf), "%d %%", volumeLevel * 100 / 128);
            ui_draw_text(&tcache, renderer, fonts.novaButton, vbuf, blancOpt, 250, 150);

            SDL_SetRenderDrawColor(renderer, 40, 40, 60, 255);
            SDL_Rect volBg = {100, 185, 400, 10};
            SDL_RenderFillRect(renderer, &volBg);
            SDL_SetRenderDrawColor(renderer, 60, 180, 220, 255);
            SDL_Rect volFg = {100, 185, volumeLevel * 400 / 128, 10};
            SDL_RenderFillRect(renderer, &volFg);

            ui_draw_text(&tcache, renderer, fonts.novaButton,
                "DISPLAY MODE", blancOpt, 100, 290);
            ui_draw_text(&tcache, renderer, fonts.novaButton,
                fullscreenMode == 0 ? "Windowed" : "Fullscreen",
                blancOpt, 320, 290);

            UIButton btns[5]; int n = layout_options(btns);
            ui_button_draw_all(renderer, assets.buttonTex, btns, n,
                                &stOptions, optionsSelected, &tcache, fonts.novaButton);

            ui_draw_text_centered(&tcache, renderer, fonts.mono,
                "[^/v/</>] Navigate  [ENTER] Confirm  [+/-/F/N] Shortcuts  [ESC] Back",
                hintOpt, W_ECRAN/2, H_ECRAN - 25);
        } else if (etat == ETAT_CONTROLS) {
            draw_bg_image(renderer, assets.bgOptions);

            /* Dark panel behind the table so the text stays legible over
             * whatever the options background happens to be.              */
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 150);
            SDL_Rect cpanel = {60, 110, W_ECRAN - 120, H_ECRAN - 190};
            SDL_RenderFillRect(renderer, &cpanel);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

            SDL_Color jauneC = {255, 215,   0, 255};
            SDL_Color blancC = {235, 235, 235, 255};
            SDL_Color cyanC  = {120, 200, 240, 255};
            SDL_Color hintC  = {180, 190, 210, 255};

            ui_draw_text_centered(&tcache, renderer, fonts.bebasTitle,
                "CONTROLS", jauneC, W_ECRAN/2, 55);

            const int colA = 80, colK = 320, colP = 560;
            ui_draw_text(&tcache, renderer, fonts.story, "ACTION",     cyanC, colA, 128);
            ui_draw_text(&tcache, renderer, fonts.story, "KEYBOARD",   cyanC, colK, 128);
            ui_draw_text(&tcache, renderer, fonts.story, "CONTROLLER", cyanC, colP, 128);

            /* Keyboard column shows P1 / P2 where they differ. */
            static const char *ctrlRows[][3] = {
                {"Move",       "Q D  /  Arrows",  "Joystick"},
                {"Jump / Up",  "Z  /  Up",        "Joystick Up"},
                {"Shoot",      "F  /  K",         "SHOOT button"},
                {"Melee",      "E  /  L",         "MELEE button"},
                {"Dash",       "LShift / RShift", "DASH button"},
                {"Pause menu", "Esc",             "MENU button"},
                {"Confirm",    "Enter",           "SELECT button"},
                {"Back",       "Backspace",       "BACK button"},
            };
            int nCtrlRows = (int)(sizeof(ctrlRows) / sizeof(ctrlRows[0]));
            for (int i = 0; i < nCtrlRows; i++) {
                int ry = 168 + i * 33;
                ui_draw_text(&tcache, renderer, fonts.novaLabel, ctrlRows[i][0], jauneC, colA, ry);
                ui_draw_text(&tcache, renderer, fonts.novaLabel, ctrlRows[i][1], blancC, colK, ry);
                ui_draw_text(&tcache, renderer, fonts.novaLabel, ctrlRows[i][2], blancC, colP, ry);
            }

            UIButton btnsC[1]; int nC = layout_controls_screen(btnsC);
            ui_button_draw_all(renderer, assets.buttonTex, btnsC, nC,
                                &stControls, 0, &tcache, fonts.novaButton);

            ui_draw_text_centered(&tcache, renderer, fonts.mono,
                "[ESC] Back", hintC, W_ECRAN/2, H_ECRAN - 12);
        } else if (etat == ETAT_CUTSCENE) {
            /* Final fade-in panel shows the live wagon underneath the
             * lifting black overlay; earlier panels paint their own bg. */
            if (cut.panel == CUT_PANEL_FADEIN) {
                wagonsInterieurs_afficher(&bg, renderer);
            }
            cutscene_render(&cut, renderer, &fonts, &tcache);
            ui_draw_text_centered(&tcache, renderer, fonts.mono,
                "[ESPACE/ENTREE] Passer",
                (SDL_Color){170, 180, 200, 180}, W_ECRAN/2, H_ECRAN - 20);
        } else if (etat == ETAT_OUTRO) {
            /* Phase 1 is handled inside the world-render block above
             * (frozen wagon + flash + fade).  Phases 2-4 paint their
             * own backdrops via outro_render.                       */
            outro_render(&outro, renderer, &fonts, &tcache, &assets);
            if (outro.phase == OUTRO_PHASE_NARRATION ||
                outro.phase == OUTRO_PHASE_TRAIN) {
                ui_draw_text_centered(&tcache, renderer, fonts.mono,
                    "[ESPACE/ENTREE] Passer",
                    (SDL_Color){170, 180, 200, 180}, W_ECRAN/2, H_ECRAN - 20);
            }
        }
        else if (etat == ETAT_CHAR_DEBUG) {
            /* Reload character sheets when skin/weapon changes. */
            if (dbgLoadedSkin != dbgSkin || dbgLoadedWeapon != dbgWeapon) {
                char_anim_load(&dbgCa, renderer, dbgSkin, dbgWeapon);
                char_anim_set(&dbgCa, dbgAnim);
                dbgLoadedSkin   = dbgSkin;
                dbgLoadedWeapon = dbgWeapon;
            }
            char_anim_update(&dbgCa, dt);

            /* Dark scene with a visible floor line. */
            SDL_SetRenderDrawColor(renderer, 16, 18, 24, 255);
            SDL_RenderClear(renderer);

            int floorY = H_ECRAN - 90;
            SDL_SetRenderDrawColor(renderer, 60, 70, 90, 255);
            SDL_RenderDrawLine(renderer, 0, floorY, W_ECRAN, floorY);
            SDL_SetRenderDrawColor(renderer, 30, 38, 52, 255);
            for (int x = 0; x < W_ECRAN; x += 20)
                SDL_RenderDrawLine(renderer, x, floorY+1, x+10, floorY+1);

            /* Foot-anchored draw, twice scale so any grid bug is obvious. */
            char_anim_render(&dbgCa, renderer,
                             W_ECRAN/2, floorY,
                             CHAR_DISPLAY_H * 2,
                             dbgFlip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);

            /* HUD: which sheet are we looking at + grid info. */
            SDL_Color jaune = {255, 215, 0, 255};
            SDL_Color blanc = {235, 235, 240, 255};
            SDL_Color gris  = {170, 180, 200, 220};
            char buf[160];
            snprintf(buf, sizeof(buf), "CHAR ANIM DEBUG  -  %s / %s / %s",
                     char_skin_name(dbgSkin),
                     char_weapon_name(dbgWeapon),
                     char_anim_name(dbgAnim));
            ui_draw_text_centered(&tcache, renderer, fonts.bebasHeader,
                                  buf, jaune, W_ECRAN/2, 24);

            const CharAnimTrack *t = &dbgCa.tracks[dbgAnim];
            if (t->texture) {
                snprintf(buf, sizeof(buf),
                         "grid %dx%d   frame %d/%d   %dx%d px   %s   %s",
                         t->cols, t->rows,
                         t->current_frame + 1, t->frame_count,
                         t->frame_w, t->frame_h,
                         (t->mode == PLAY_LOOP)         ? "LOOP"          :
                         (t->mode == PLAY_LOCK_FOREVER) ? "LOCK_FOREVER"  :
                                                         "ONCE_HOLD",
                         dbgCa.is_locked ? "[locked]" : "");
            } else {
                snprintf(buf, sizeof(buf), "(no texture loaded for this anim)");
            }
            ui_draw_text_centered(&tcache, renderer, fonts.mono,
                                  buf, blanc, W_ECRAN/2, 52);

            ui_draw_text_centered(&tcache, renderer, fonts.mono,
                "[<-/->] skin   [^/v] weapon   [PGUP/PGDN] anim   "
                "[F] flip   [R] replay   [ESC] retour",
                gris, W_ECRAN/2, H_ECRAN - 24);
        }

        SDL_RenderPresent(renderer);

#ifdef __EMSCRIPTEN__
    /* continuer is cleared on QUIT / menu-quit; stop the RAF pump. */
    if (!continuer) emscripten_cancel_main_loop();
#endif
}
