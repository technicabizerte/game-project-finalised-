#include "background.h"
#include "gameplay.h"
#include "effects.h"
#include "damage_numbers.h"
#include "door_anim.h"
#include "boss_intro.h"
#include "boss_cutscene.h"
#include "boss_anim.h"
#include "boss_ai.h"
#include "boss_hud.h"               /* hitstop_trigger on player attacks  */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
/* Flush the IDBFS-backed /persist mount to IndexedDB after a save write so it
 * survives a page reload.  No-op on native (saves go straight to disk).     */
#define PERSIST_SYNC() EM_ASM(FS.syncfs(false, function(e){}))
#else
#define PERSIST_SYNC() ((void)0)
#endif

int background_chargerImages(Background *bg, SDL_Renderer *renderer,
                              const char *cheminImg1, const char *cheminImg2)
{
    SDL_Surface *surf = IMG_Load(cheminImg1);
    if (!surf) {
        fprintf(stderr, "[BG] Erreur '%s' : %s\n", cheminImg1, IMG_GetError());
        return -1;
    }
    bg->imagebackground = SDL_CreateTextureFromSurface(renderer, surf);
    bg->imgW = surf->w;
    bg->imgH = surf->h;
    SDL_FreeSurface(surf);
    bg->imagebackground2 = NULL;
    if (cheminImg2) {
        surf = IMG_Load(cheminImg2);
        if (surf) { bg->imagebackground2 = SDL_CreateTextureFromSurface(renderer, surf); SDL_FreeSurface(surf); }
    }
    return 0;
}

int background_initialiserTexte(Background *bg, const char *contenu,
                                 TTF_Font *police, SDL_Color couleur, int x, int y)
{
    bg->texte.police  = police;
    bg->texte.couleur = couleur;
    strncpy(bg->texte.contenu, contenu, sizeof(bg->texte.contenu) - 1);
    bg->texte.x = x; bg->texte.y = y;
    return 0;
}

int background_initialiserSon(Background *bg, const char *cheminSon)
{
    bg->son = Mix_LoadMUS(cheminSon);
    if (!bg->son) { fprintf(stderr, "[BG] Erreur son : %s\n", Mix_GetError()); return -1; }
    return 0;
}

void background_initialiserPosition(Background *bg, int posx, int posy)
{ bg->posx = posx; bg->posy = posy; }

/* Cached renderer for late Ellie-animator loads inside joueur_init_interieur_idx
 * (which has no renderer parameter and is called from many sites). */
#include "skin_select.h"
#include "weapon_select.h"
#include "two_player.h"
#include "mode_select.h"
#include "versus.h"

static SDL_Renderer *s_char_anim_renderer = NULL;

/* Run the per-enemy melee-impact damage loop.  Extracted from the press
 * handler so it can fire on the sword-impact frame of ANIM_ATTACK instead
 * of immediately on key-down. */
static void apply_melee_impact(Background *bg, Joueur *j, int widx)
{
    /* Which player is swinging?  Convert the pointer back to an index so we
     * read THIS player's chosen weapon stats, not a shared global.  Clamped
     * defensively if a caller hands in a non-array Joueur.                  */
    int swinger_idx = (int)(j - bg->joueurs);
    if (swinger_idx < 0 || swinger_idx >= MAX_PLAYERS) swinger_idx = 0;

    /* SLASH VFX: spawn an additive arc on the active frame for every
     * swing — campaign, both PvP players, all weapons.  Vertical lift
     * tracks the sprite scale: campaign baseline is 55 px at scale 1.0;
     * versus multiplies by VERSUS_PLAYER_SCALE so the pivot lands on
     * the swinger's chest no matter how big the on-screen sprite is.
     * Without this, the old hard-coded 110 spawned the arc 50+ px
     * above the head once the versus scale dropped from 1.6 to ~1.0.   */
    {
        EffectsSystem *efx_s = effects_global();
        if (efx_s) {
            SlashKind sk = (player_cfg[swinger_idx].weapon == WEAPON_AXE)
                           ? SLASH_AXE : SLASH_SWORD;
            float lift = (g_game_mode == MODE_VERSUS)
                         ? 55.0f * VERSUS_PLAYER_SCALE
                         : 55.0f;
            float px   = j->x + (float)JOUEUR_W / 2.0f
                       + (float)j->meleeImpactDir * 18.0f;
            float py   = j->y + (float)JOUEUR_H - lift;
            effects_spawn_slash(efx_s, sk, px, py, j->meleeImpactDir);
        }
    }

    /* PvP shortcut: in VERSUS, melee targets the other player instead of
     * enemies.  Same visual feedback path (damage number, hit-fx, knockback,
     * stagger anim) — see versus_apply_melee().                            */
    if (g_game_mode == MODE_VERSUS) {
        versus_apply_melee(bg, swinger_idx);
        return;
    }

    const WeaponStats *ws =
        &CHAR_WEAPON_STATS[player_cfg[swinger_idx].weapon];

    for (int i = 0; i < bg->nbEnnemis; i++) {
        Ennemi *e = &bg->ennemis[i];
        if (!e->actif || e->wagon != widx || e->etat == ES_DEAD) continue;
        float ex = e->x + (j->meleeImpactDir == 1 ? 0 : (float)JOUEUR_W);
        float dist = ex - (j->x + JOUEUR_W / 2.0f);
        if (dist < 0) dist = -dist;
        if (dist >= (float)ws->melee_range) continue;

        /* Selected weapon's base damage with the long-standing 0.80
         * impact-frame falloff so the on-screen number lines up with
         * the previous feel.                                          */
        int _melee_dmg = (int)((float)ws->melee_damage * 0.80f);
        int _is_boss = (e->type == ET_ENFORCER && e->wagon == MAX_WAGONS - 1);
        e->hp -= _melee_dmg;
        e->hitFlashMs = 150;
        if (_is_boss) boss_ai_on_hit();
        {
            DamageNumberType t = (_melee_dmg >= DMG_NUM_CRIT_THRESHOLD)
                                 ? DMG_NUM_CRIT : DMG_NUM_NORMAL;
            spawn_damage_number(e->x + JOUEUR_W / 2.0f,
                                e->y + JOUEUR_H / 3.0f,
                                _melee_dmg, t);
        }
        {
            EffectsSystem *efx = effects_global();
            if (efx) {
                effects_on_hit(efx, EFX_ENT_ENEMY, i, j->meleeImpactDir);
                effects_spawn_impact(efx, e->x + JOUEUR_W / 2.0f,
                                          e->y + JOUEUR_H / 3.0f,
                                          j->meleeImpactDir, _melee_dmg);
            }
            /* Player melee → contact hitstop. */
            hitstop_trigger(_melee_dmg >= DMG_NUM_CRIT_THRESHOLD ? 0.08f : 0.05f);
        }
        hitfx_spawn(bg->hitFx, &bg->nbHitFx,
                    e->x + JOUEUR_W / 2.0f, e->y + JOUEUR_H / 3.0f,
                    bg->hitFxSheet);
        particles_burst(&bg->particles,
                        e->x + JOUEUR_W / 2.0f, e->y + JOUEUR_H / 3.0f);
        if (e->hp <= 0) {
            EffectsSystem *efx = effects_global();
            if (efx) effects_spawn_death_for_enemy(efx, e, j->meleeImpactDir);
            hitstop_trigger(0.12f);
            e->etat = ES_DEAD;
            int nf = (e->type == ET_ENFORCER) ? ENFORCER_NFRAMES[2] :
                     (e->type == ET_GUARD)    ? GUARD_NFRAMES[2] : WORKER_NFRAMES[2];
            animation_set_row(&e->anim, ANIM_E_DEATH, nf,
                              ENEMY_ANIM_FPS[ANIM_E_DEATH], 0);
        }
    }
}

int background_init(Background *bg, SDL_Renderer *renderer, int niveau)
{
    s_char_anim_renderer = renderer;
    bg->mode = MODE_MONO;
    bg->tempsDepart = SDL_GetTicks();
    bg->son = NULL;
    bg->imagebackground = bg->imagebackground2 = NULL;
    bg->fenetreguide.visible = 0; bg->fenetreguide.texture = NULL;
    bg->texte.police = NULL; bg->texte.contenu[0] = '\0';
    bg->posx = bg->posy = 0;
    bg->w = W_BACKGROUND; bg->h = H_BACKGROUND;
    bg->imgW = bg->imgH = 0;
    bg->vue = VUE_INTERIEURE;
    bg->wagonActif = 0;
    bg->nbWagons = bg->nbPortes = bg->nbPlateformes = bg->nbDestructibles = 0;
    bg->flash.actif = bg->flash.alpha = 0;
    bg->parallaxOffset = 0.0f;
    bg->transitionSlide.actif = 0;
    bg->transitionSlide.offset = 0.0f;
    bg->transitionSlide.direction = 1;
    bg->transitionSlide.wagonCible = 0;
    bg->camera1 = (SDL_Rect){0, 0, W_ECRAN, H_ECRAN};
    bg->camera2 = (SDL_Rect){0, 0, W_ECRAN/2, H_ECRAN};
    bg->posEcran1 = (SDL_Rect){0, 0, W_ECRAN/2, H_ECRAN};
    bg->posEcran2 = (SDL_Rect){W_ECRAN/2, 0, W_ECRAN/2, H_ECRAN};
    switch (niveau) {
        case 1:  bg->typePlateforme = PLATEFORME_FIXE;         break;
        case 2:  bg->typePlateforme = PLATEFORME_MOBILE;       break;
        default: bg->typePlateforme = PLATEFORME_DESTRUCTIBLE; break;
    }
    bg->niveau = niveau;

    for (int i = 0; i < MAX_WAGONS; i++) bg->wagonBgTex[i] = NULL;
    bg->outsideBgTex = NULL;
    for (int i = 0; i < 3; i++) bg->enemySheets[i] = NULL;
    bg->bulletTex    = NULL;
    bg->muzzleTex    = NULL;
    bg->hitFxSheet   = NULL;
    bg->nbBalles[0]     = 0;
    bg->nbBalles[1]     = 0;
    bg->nbJoueursActifs = 1;
    bg->nbEnnemis    = 0;
    bg->nbHitFx      = 0;
    memset(bg->balles,  0, sizeof(bg->balles));
    memset(bg->ennemis, 0, sizeof(bg->ennemis));
    memset(bg->hitFx,   0, sizeof(bg->hitFx));
    bg->splitMode     = 0;
    bg->splitCooldown = 0.0f;
    for (int i = 0; i < NB_JOUEURS_MAX; i++)
        weapon_swing_init(&bg->swings[i]);
    particles_init(&bg->particles);
    return 0;
}

void background_scrolling(Background *bg, int direction, int dx, int dy, int numBack)
{
    SDL_Rect *cam = (numBack == 2) ? &bg->camera2 : &bg->camera1;
    switch (direction) {
        case SCROLL_DROITE: if (cam->x + W_ECRAN + dx <= bg->w) cam->x += dx; break;
        case SCROLL_GAUCHE: if (cam->x - dx >= 0) cam->x -= dx;               break;
        case SCROLL_HAUT:   if (cam->y - dy >= 0) cam->y -= dy;               break;
        case SCROLL_BAS:    if (cam->y + cam->h + dy <= bg->h) cam->y += dy;  break;
        default: break;
    }
}

void background_afficher(Background *bg, SDL_Renderer *renderer, int mode, int deltax, int deltay)
{
    (void)deltax; (void)deltay; (void)mode;
    if (!bg->imagebackground) return;
    float scaleX = (float)bg->imgW / (float)bg->w;
    float scaleY = (float)bg->imgH / (float)bg->h;
    SDL_Rect src = {(int)(bg->camera1.x*scaleX),(int)(bg->camera1.y*scaleY),
                    (int)(W_ECRAN*scaleX),(int)(H_ECRAN*scaleY)};
    if (src.x < 0) src.x = 0;
    if (src.y < 0) src.y = 0;
    if (src.x + src.w > bg->imgW) src.x = bg->imgW - src.w;
    if (src.y + src.h > bg->imgH) src.y = bg->imgH - src.h;
    SDL_RenderCopy(renderer, bg->imagebackground, &src, NULL);
}

void background_afficherTemps(Background *bg, SDL_Renderer *renderer, int x, int y)
{
    if (!bg->texte.police) return;

    /* Cache the rendered time texture and only rebuild when the displayed
     * seconds value changes. Recreating an SDL_Texture every frame thrashes
     * the GPU driver's internal allocator (observed corrupting Mesa state
     * inside VirtualBox after a few thousand frames). */
    static SDL_Texture *s_tex   = NULL;
    static int          s_w     = 0;
    static int          s_h     = 0;
    static Uint32       s_secs  = 0xFFFFFFFFu;
    static SDL_Renderer *s_ren  = NULL;

    Uint32 e    = SDL_GetTicks() - bg->tempsDepart;
    Uint32 secs = e / 1000;

    if (s_ren != renderer && s_tex) {
        SDL_DestroyTexture(s_tex);
        s_tex = NULL;
        s_secs = 0xFFFFFFFFu;
    }
    s_ren = renderer;

    if (!s_tex || secs != s_secs) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Temps : %02u:%02u", secs / 60, secs % 60);
        SDL_Surface *surf = TTF_RenderUTF8_Blended(bg->texte.police, buf, bg->texte.couleur);
        if (!surf) return;
        if (s_tex) SDL_DestroyTexture(s_tex);
        s_tex = SDL_CreateTextureFromSurface(renderer, surf);
        s_w   = surf->w;
        s_h   = surf->h;
        s_secs = secs;
        SDL_FreeSurface(surf);
    }

    if (s_tex) {
        SDL_Rect d = {x, y, s_w, s_h};
        SDL_RenderCopy(renderer, s_tex, NULL, &d);
    }
}

int fenetreGuide_init(Background *bg, SDL_Renderer *renderer, const char *cheminImage, SDL_Rect rect)
{
    SDL_Surface *surf = IMG_Load(cheminImage);
    if (!surf) { fprintf(stderr, "[Guide] Erreur '%s' : %s\n", cheminImage, IMG_GetError()); return -1; }
    bg->fenetreguide.texture  = SDL_CreateTextureFromSurface(renderer, surf);
    bg->fenetreguide.position = rect;
    bg->fenetreguide.visible  = 0;
    SDL_FreeSurface(surf);
    return 0;
}

void fenetreGuide_afficher(Background *bg, SDL_Renderer *renderer)
{
    if (!bg->fenetreguide.visible || !bg->fenetreguide.texture) return;
    SDL_RenderCopy(renderer, bg->fenetreguide.texture, NULL, &bg->fenetreguide.position);
}

void scores_saisirNom(char *nom, int taille, SDL_Renderer *renderer, TTF_Font *police, SDL_Color couleur)
{
    SDL_StartTextInput();
    int idx=0, done=0; nom[0]='\0';
    SDL_Event ev; Uint32 lb=SDL_GetTicks(); int sc=1;
    while (!done) {
        while (SDL_PollEvent(&ev)) {
            if (ev.type==SDL_QUIT) { done=1; }
            else if (ev.type==SDL_KEYDOWN) {
                if (ev.key.keysym.sym==SDLK_RETURN||ev.key.keysym.sym==SDLK_KP_ENTER) { if(idx>0) done=1; }
                else if (ev.key.keysym.sym==SDLK_BACKSPACE && idx>0) nom[--idx]='\0';
            } else if (ev.type==SDL_TEXTINPUT && idx<taille-1) { nom[idx++]=ev.text.text[0]; nom[idx]='\0'; }
        }
        SDL_SetRenderDrawColor(renderer,20,20,40,255); SDL_RenderClear(renderer);
        if (police) {
            SDL_Color blanc={255,255,255,255};
            SDL_Surface *s=TTF_RenderUTF8_Blended(police,"Entrez votre nom :",blanc);
            if(s){SDL_Texture*t=SDL_CreateTextureFromSurface(renderer,s);SDL_Rect d={W_ECRAN/2-s->w/2,H_ECRAN/2-60,s->w,s->h};SDL_RenderCopy(renderer,t,NULL,&d);SDL_DestroyTexture(t);SDL_FreeSurface(s);}
            Uint32 now=SDL_GetTicks(); if(now-lb>500){sc=!sc;lb=now;}
            char aff[130]; snprintf(aff,sizeof(aff),"%s%s",nom,sc?"|":" ");
            s=TTF_RenderUTF8_Blended(police,aff,couleur);
            if(s){SDL_Texture*t=SDL_CreateTextureFromSurface(renderer,s);SDL_Rect d={W_ECRAN/2-s->w/2,H_ECRAN/2,s->w,s->h};SDL_RenderCopy(renderer,t,NULL,&d);SDL_DestroyTexture(t);SDL_FreeSurface(s);}
        }
        SDL_RenderPresent(renderer); SDL_Delay(16);
    }
    SDL_StopTextInput();
}

void scores_sauvegarder(const char *f, Score *scores, int nb)
{
    FILE *fp = fopen(f, "w"); if (!fp) return;
    for (int i = 0; i < nb; i++)
        fprintf(fp, "%s,%d,%d,%d\n", scores[i].nom, scores[i].score,
                scores[i].tempsSec, scores[i].wagonAtteint);
    fclose(fp);
    PERSIST_SYNC();
}

int scores_charger(const char *f, Score *scores, int max)
{
    FILE *fp = fopen(f, "r"); if (!fp) return 0;
    int nb = 0;
    char line[256];
    while (nb < max && fgets(line, sizeof(line), fp)) {
        char nom[64] = ""; int s = 0, t = 0, w = 0;
        if (sscanf(line, "%63[^,],%d,%d,%d", nom, &s, &t, &w) == 4) {
            strncpy(scores[nb].nom, nom, 63);
            scores[nb].nom[63]      = '\0';
            scores[nb].score        = s;
            scores[nb].tempsSec     = t;
            scores[nb].wagonAtteint = w;
            nb++;
        }
    }
    fclose(fp); return nb;
}

void scores_ajouterEtTrier(Score *scores, int *nb, const char *nom, int score,
                            int tempsSec, int wagonAtteint)
{
    if (*nb < MAX_SCORES) {
        strncpy(scores[*nb].nom, nom, 63);
        scores[*nb].nom[63]      = '\0';
        scores[*nb].score        = score;
        scores[*nb].tempsSec     = tempsSec;
        scores[*nb].wagonAtteint = wagonAtteint;
        (*nb)++;
    } else if (score > scores[MAX_SCORES-1].score) {
        strncpy(scores[MAX_SCORES-1].nom, nom, 63);
        scores[MAX_SCORES-1].nom[63]      = '\0';
        scores[MAX_SCORES-1].score        = score;
        scores[MAX_SCORES-1].tempsSec     = tempsSec;
        scores[MAX_SCORES-1].wagonAtteint = wagonAtteint;
    }
    for (int i = 0; i < *nb-1; i++)
        for (int j = 0; j < *nb-i-1; j++)
            if (scores[j].score < scores[j+1].score) {
                Score t = scores[j]; scores[j] = scores[j+1]; scores[j+1] = t;
            }
}

void scores_afficherMenu(Score *scores, int nb, SDL_Renderer *renderer,
                          TTF_Font *police, TTF_Font *policeGrande, SDL_Color couleur)
{
    if (!police) return;

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
        Uint8 brightness = 100 + (i * 7) % 155;
        SDL_SetRenderDrawColor(renderer, brightness, brightness,
                                brightness * 7 / 10, 255);
        SDL_Rect dot = {sx, sy, 2, 2};
        SDL_RenderFillRect(renderer, &dot);
    }

    TTF_Font *titleFont = policeGrande ? policeGrande : police;
    SDL_Color jaune = {255, 215, 0, 255};
    SDL_Surface *s = TTF_RenderUTF8_Blended(titleFont, "MEILLEURS SCORES", jaune);
    if (s) {
        SDL_Texture *t = SDL_CreateTextureFromSurface(renderer, s);
        SDL_Rect d = {W_ECRAN/2 - s->w/2, 40, s->w, s->h};
        SDL_RenderCopy(renderer, t, NULL, &d);
        SDL_DestroyTexture(t); SDL_FreeSurface(s);
    }

    SDL_Color blanc = {255, 255, 255, 255};
    s = TTF_RenderUTF8_Blended(police,
        "RANG   NOM              SCORE   TEMPS    WAGON", blanc);
    if (s) {
        SDL_Texture *t = SDL_CreateTextureFromSurface(renderer, s);
        SDL_Rect d = {W_ECRAN/2 - s->w/2, 110, s->w, s->h};
        SDL_RenderCopy(renderer, t, NULL, &d);
        SDL_DestroyTexture(t); SDL_FreeSurface(s);
    }

    if (nb == 0) {
        SDL_Color grisC = {180, 180, 180, 255};
        s = TTF_RenderUTF8_Blended(police, "Aucun score enregistre", grisC);
        if (s) {
            SDL_Texture *t = SDL_CreateTextureFromSurface(renderer, s);
            SDL_Rect d = {W_ECRAN/2 - s->w/2, 180, s->w, s->h};
            SDL_RenderCopy(renderer, t, NULL, &d);
            SDL_DestroyTexture(t); SDL_FreeSurface(s);
        }
    } else {
        int maxRows = nb < 10 ? nb : 10;
        for (int i = 0; i < maxRows; i++) {
            char row[128];
            snprintf(row, sizeof(row), "%2d.  %-16s  %5d   %02d:%02d     %d/5",
                     i+1, scores[i].nom, scores[i].score,
                     scores[i].tempsSec/60, scores[i].tempsSec%60,
                     scores[i].wagonAtteint);
            s = TTF_RenderUTF8_Blended(police, row, couleur);
            if (!s) continue;
            SDL_Texture *t = SDL_CreateTextureFromSurface(renderer, s);
            SDL_Rect d = {W_ECRAN/2 - s->w/2, 140 + i*28, s->w, s->h};
            SDL_RenderCopy(renderer, t, NULL, &d);
            SDL_DestroyTexture(t); SDL_FreeSurface(s);
        }
    }
}

void vues_init(Background *bg, SDL_Renderer *renderer)
{
    const char *f[MAX_WAGONS]={"wagon1.png","wagon2.png","wagon3.png","wagon4.png","wagon5.png"};
    const char *n[MAX_WAGONS]={"Wagon Pauvres","Wagon Industrie","Wagon Aquarium","Wagon Jardin","Wagon Luxe"};
    bg->nbWagons=MAX_WAGONS;
    for(int i=0;i<MAX_WAGONS;i++){
        SDL_Surface*s=IMG_Load(f[i]);
        bg->wagons[i].texture=s?SDL_CreateTextureFromSurface(renderer,s):NULL;
        if(s) SDL_FreeSurface(s);
        strncpy(bg->wagons[i].nom,n[i],31);
    }
}

void plateformes_init(Background *bg, SDL_Renderer *renderer) { (void)renderer; bg->nbPlateformes=0; }

void plateformes_update(Background *bg) {
    float t = (float)SDL_GetTicks() / 1000.0f;
    for (int w = 0; w < MAX_WAGONS; w++) {
        ObstaclesWagon *obs = &bg->stylesWagons[w].obstacles;
        for (int i = 0; i < obs->nb; i++) {
            int onGround = ((int)obs->hoverBaseY[i] + obs->rects[i].h >= WAGON_SOL_Y);
            if (!onGround)
                obs->rects[i].y = (int)(obs->hoverBaseY[i] + sinf(t * 1.4f + obs->hoverPhase[i]) * 6.0f);
        }
    }
}

int  plateforme_collision(Background *bg, SDL_Rect *j, int *v) { (void)bg;(void)j;(void)v; return 0; }
void destructibles_init(Background *bg, SDL_Renderer *renderer) { (void)renderer; bg->nbDestructibles=0; }
void destructibles_update(Background *bg) { (void)bg; }
int  destructibles_verifierCollision(Background *bg, SDL_Rect j) { (void)bg;(void)j; return 0; }
void destructibles_liberer(Background *bg) { (void)bg; }

void neige_init(Background *bg)
{
    Uint32 seed=SDL_GetTicks();
    for(int i=0;i<MAX_NEIGE;i++){
        ParticuleNeige*p=&bg->neige[i]; p->couche=i%3;
        seed=seed*1664525u+1013904223u; p->x=(float)((seed>>16)%W_ECRAN);
        seed=seed*1664525u+1013904223u; p->y=(float)((seed>>16)%H_ECRAN);
        seed=seed*1664525u+1013904223u; float r=(float)((seed>>16)%100)/100.0f;
        switch(p->couche){
            case 0:p->vitesse=0.3f+r*0.3f;p->alpha=55;p->taille=1;break;
            case 1:p->vitesse=0.8f+r*0.5f;p->alpha=110;p->taille=2;break;
            default:p->vitesse=1.6f+r*0.9f;p->alpha=180;p->taille=3;break;
        }
        seed=seed*1664525u+1013904223u;
        p->derive=((float)((seed>>16)%60)-30.0f)/100.0f;
    }
}

void neige_update(Background *bg)
{
    Uint32 t=SDL_GetTicks();
    for(int i=0;i<MAX_NEIGE;i++){
        ParticuleNeige*p=&bg->neige[i];
        float wave=(float)SDL_sin((double)(t*0.001+p->y*0.02))*0.3f;
        p->y+=p->vitesse; p->x+=p->derive+wave;
        if(p->y>H_ECRAN+4){ p->y=-4.0f; Uint32 s=(Uint32)(p->x*7919u)^t; s=s*1664525u+1013904223u; p->x=(float)((s>>16)%W_ECRAN); }
        if(p->x<-4) p->x=(float)W_ECRAN;
        if(p->x>W_ECRAN+4) p->x=0.0f;
    }
}

void flash_declencher(Background *bg) { bg->flash.actif=1; bg->flash.alpha=170; }

void flash_update(Background *bg)
{ if(!bg->flash.actif) return; bg->flash.alpha-=18; if(bg->flash.alpha<=0){bg->flash.alpha=0;bg->flash.actif=0;} }

void flash_afficher(Background *bg, SDL_Renderer *renderer)
{
    if(!bg->flash.actif||bg->flash.alpha<=0) return;
    SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer,190,225,255,(Uint8)bg->flash.alpha);
    SDL_Rect f={0,0,W_ECRAN,H_ECRAN}; SDL_RenderFillRect(renderer,&f);
}

void wagonsInterieurs_init(Background *bg, SDL_Renderer *renderer)
{

    { StyleWagon*s=&bg->stylesWagons[0];
      s->fond=(SDL_Color){36,28,22,255}; s->sol=(SDL_Color){52,40,30,255};
      s->plafond=(SDL_Color){42,33,26,255}; s->mur=(SDL_Color){48,37,29,255};
      s->accent=(SDL_Color){160,90,35,255}; s->accentClair=(SDL_Color){210,130,55,255};
      s->fenetres[0]=(SDL_Rect){110,WAGON_PLAF_Y+18,130,88};
      s->fenetres[1]=(SDL_Rect){560,WAGON_PLAF_Y+18,130,88}; s->nbFenetres=2;
      s->obstacles.nb=3;
      s->obstacles.rects[0]=(SDL_Rect){75,WAGON_SOL_Y-42,85,42};  s->obstacles.couleurs[0]=(SDL_Color){62,48,36,255};
      s->obstacles.rects[1]=(SDL_Rect){320,WAGON_SOL_Y-65,100,22};s->obstacles.couleurs[1]=(SDL_Color){58,44,33,255};
      s->obstacles.rects[2]=(SDL_Rect){600,WAGON_SOL_Y-42,85,42}; s->obstacles.couleurs[2]=(SDL_Color){62,48,36,255};
      for(int i=0;i<s->obstacles.nb;i++){
          s->obstacles.hoverBaseY[i]=(float)s->obstacles.rects[i].y;
          s->obstacles.hoverPhase[i]=(float)i*1.1f;
      }
    }

    { StyleWagon*s=&bg->stylesWagons[1];
      s->fond=(SDL_Color){30,34,42,255}; s->sol=(SDL_Color){48,54,66,255};
      s->plafond=(SDL_Color){36,40,50,255}; s->mur=(SDL_Color){42,48,60,255};
      s->accent=(SDL_Color){60,200,80,255}; s->accentClair=(SDL_Color){100,240,120,255};
      s->fenetres[0]=(SDL_Rect){290,WAGON_PLAF_Y+22,220,78}; s->nbFenetres=1;
      s->obstacles.nb=2;
      s->obstacles.rects[0]=(SDL_Rect){185,WAGON_SOL_Y-80, 130,22};s->obstacles.couleurs[0]=(SDL_Color){56,63,78,255};
      s->obstacles.rects[1]=(SDL_Rect){475,WAGON_SOL_Y-110,130,22};s->obstacles.couleurs[1]=(SDL_Color){56,63,78,255};
      for(int i=0;i<s->obstacles.nb;i++){
          s->obstacles.hoverBaseY[i]=(float)s->obstacles.rects[i].y;
          s->obstacles.hoverPhase[i]=(float)i*1.1f;
      }
    }

    /* wagon 2 – aquarium: 4 floating ice ledges at varied heights */
    { StyleWagon*s=&bg->stylesWagons[2];
      s->fond=(SDL_Color){6,18,52,255}; s->sol=(SDL_Color){10,36,64,255};
      s->plafond=(SDL_Color){6,14,44,255}; s->mur=(SDL_Color){8,24,58,255};
      s->accent=(SDL_Color){35,195,210,255}; s->accentClair=(SDL_Color){75,230,245,255};
      s->fenetres[0]=(SDL_Rect){72,WAGON_PLAF_Y+12,185,108};
      s->fenetres[1]=(SDL_Rect){543,WAGON_PLAF_Y+12,185,108}; s->nbFenetres=2;
      s->obstacles.nb=4;
      s->obstacles.rects[0]=(SDL_Rect){55, WAGON_SOL_Y-80, 140,18}; s->obstacles.couleurs[0]=(SDL_Color){12,50,85,255};
      s->obstacles.rects[1]=(SDL_Rect){285,WAGON_SOL_Y-45, 115,18}; s->obstacles.couleurs[1]=(SDL_Color){ 9,42,76,255};
      s->obstacles.rects[2]=(SDL_Rect){450,WAGON_SOL_Y-45, 115,18}; s->obstacles.couleurs[2]=(SDL_Color){ 9,42,76,255};
      s->obstacles.rects[3]=(SDL_Rect){610,WAGON_SOL_Y-100,140,18}; s->obstacles.couleurs[3]=(SDL_Color){12,50,85,255};
      for(int i=0;i<s->obstacles.nb;i++){
          s->obstacles.hoverBaseY[i]=(float)s->obstacles.rects[i].y;
          s->obstacles.hoverPhase[i]=(float)i*1.1f;
      }
    }

    { StyleWagon*s=&bg->stylesWagons[3];
      s->fond=(SDL_Color){16,30,16,255}; s->sol=(SDL_Color){36,25,14,255};
      s->plafond=(SDL_Color){12,25,12,255}; s->mur=(SDL_Color){20,36,20,255};
      s->accent=(SDL_Color){75,200,70,255}; s->accentClair=(SDL_Color){115,240,95,255};
      /* Coords sized to fully cover the solid-black window panes baked
       * into wagon3.png (Jardin); used by the procedural outside view. */
      s->fenetres[0]=(SDL_Rect){148, 186, 163, 136};
      s->fenetres[1]=(SDL_Rect){487, 183, 152, 143}; s->nbFenetres=2;
      s->obstacles.nb=4;
      s->obstacles.rects[0]=(SDL_Rect){85,WAGON_SOL_Y-36,72,36};  s->obstacles.couleurs[0]=(SDL_Color){38,58,22,255};
      s->obstacles.rects[1]=(SDL_Rect){248,WAGON_SOL_Y-54,95,18}; s->obstacles.couleurs[1]=(SDL_Color){32,50,18,255};
      s->obstacles.rects[2]=(SDL_Rect){457,WAGON_SOL_Y-54,95,18}; s->obstacles.couleurs[2]=(SDL_Color){32,50,18,255};
      s->obstacles.rects[3]=(SDL_Rect){643,WAGON_SOL_Y-36,72,36}; s->obstacles.couleurs[3]=(SDL_Color){38,58,22,255};
      for(int i=0;i<s->obstacles.nb;i++){
          s->obstacles.hoverBaseY[i]=(float)s->obstacles.rects[i].y;
          s->obstacles.hoverPhase[i]=(float)i*1.1f;
      }
    }

    { StyleWagon*s=&bg->stylesWagons[4];
      s->fond=(SDL_Color){36,14,14,255}; s->sol=(SDL_Color){55,28,14,255};
      s->plafond=(SDL_Color){40,10,10,255}; s->mur=(SDL_Color){46,18,16,255};
      s->accent=(SDL_Color){215,175,55,255}; s->accentClair=(SDL_Color){255,215,85,255};
      s->fenetres[0]=(SDL_Rect){145,WAGON_PLAF_Y+12,158,100};
      s->fenetres[1]=(SDL_Rect){497,WAGON_PLAF_Y+12,158,100}; s->nbFenetres=2;
      /* Boss arena — no obstacles, no platforms.  Clean floor. */
      s->obstacles.nb=0;
    }

    for (int i = 0; i < MAX_WAGONS; i++) {
        bg->stylesWagons[i].floorTex    = NULL;
        bg->stylesWagons[i].obstacleTex = NULL;
    }

    const char *floorFiles[MAX_WAGONS] = {
        "wagon0_floor.png","wagon1_floor.png","wagon2_floor.png",
        "wagon3_floor.png","wagon4_floor.png"
    };
    const char *obstacleFiles[MAX_WAGONS] = {
        "obstacle_poor.png","obstacle_industry.png","obstacle_aquarium.png",
        "obstacle_garden.png","obstacle_luxury.png"
    };
    for (int i = 0; i < MAX_WAGONS; i++) {
        SDL_Surface *surf = IMG_Load(floorFiles[i]);
        bg->stylesWagons[i].floorTex = surf ? SDL_CreateTextureFromSurface(renderer, surf) : NULL;
        if (surf) SDL_FreeSurface(surf);

        surf = IMG_Load(obstacleFiles[i]);
        bg->stylesWagons[i].obstacleTex = surf ? SDL_CreateTextureFromSurface(renderer, surf) : NULL;
        if (surf) SDL_FreeSurface(surf);
    }

    const char *wagonBgFiles[MAX_WAGONS] = {
        "wagon0.png","wagon1.png","wagon2.png","wagon3.png","wagon4.png"
    };
    for (int i = 0; i < MAX_WAGONS; i++) {
        SDL_Surface *surf = IMG_Load(wagonBgFiles[i]);
        bg->wagonBgTex[i] = surf ? SDL_CreateTextureFromSurface(renderer, surf) : NULL;
        if (surf) SDL_FreeSurface(surf);
    }

    /* outside_bg.png is no longer used (the in-window parallax was removed);
     * leave the texture NULL so background_liberer just skips it. */
    bg->outsideBgTex = NULL;

    bg->enemySheets[ET_GUARD]    = combat_loadTexture(renderer, "enemy_guard.png");
    bg->enemySheets[ET_WORKER]   = combat_loadTexture(renderer, "enemy_worker.png");
    bg->enemySheets[ET_ENFORCER] = combat_loadTexture(renderer, "enemy_enforcer.png");

    bg->bulletTex  = combat_loadTexture(renderer, "bullet.png");
    bg->muzzleTex  = combat_loadTexture(renderer, "muzzle.png");
    bg->hitFxSheet = combat_loadTexture(renderer, "hit_effect.png");

    enemies_respawn_default(bg);

    bg->wagonActif = 0;
    bg->parallaxOffset = 0.0f;
    bg->transitionSlide.actif = 0;
    bg->nbJoueursActifs = 2;
}

void enemies_respawn_default(Background *bg)
{
    memset(bg->ennemis, 0, sizeof(bg->ennemis));
    bg->nbEnnemis = 0;
    ennemis_spawn(bg->ennemis, &bg->nbEnnemis, ET_WORKER,   580, WAGON_SOL_Y-WORKER_HBH,   0, bg->enemySheets[ET_WORKER]);
    ennemis_spawn(bg->ennemis, &bg->nbEnnemis, ET_GUARD,    620, WAGON_SOL_Y-GUARD_HBH,    1, bg->enemySheets[ET_GUARD]);
    ennemis_spawn(bg->ennemis, &bg->nbEnnemis, ET_WORKER,   530, WAGON_SOL_Y-WORKER_HBH,   1, bg->enemySheets[ET_WORKER]);
    ennemis_spawn(bg->ennemis, &bg->nbEnnemis, ET_GUARD,    560, WAGON_SOL_Y-GUARD_HBH,    2, bg->enemySheets[ET_GUARD]);
    ennemis_spawn(bg->ennemis, &bg->nbEnnemis, ET_GUARD,    640, WAGON_SOL_Y-GUARD_HBH,    2, bg->enemySheets[ET_GUARD]);
    ennemis_spawn(bg->ennemis, &bg->nbEnnemis, ET_WORKER,   590, WAGON_SOL_Y-WORKER_HBH,   3, bg->enemySheets[ET_WORKER]);
    ennemis_spawn(bg->ennemis, &bg->nbEnnemis, ET_GUARD,    550, WAGON_SOL_Y-GUARD_HBH,    3, bg->enemySheets[ET_GUARD]);
    /* Wagon 5 is the boss arena — only the enforcer spawns here.  Spawn Y
     * uses BOSS_HBH so the larger boss hitbox sits flush with the floor
     * (see combat.h for the boss vs. regular-enforcer hitbox distinction). */
    ennemis_spawn(bg->ennemis, &bg->nbEnnemis, ET_ENFORCER, 580, WAGON_SOL_Y-BOSS_HBH, 4, bg->enemySheets[ET_ENFORCER]);
}

static void draw_window_outside_view(SDL_Renderer *renderer, SDL_Rect win)
{
    Uint32 t = SDL_GetTicks();
    /* Gradient sky: dark blue at top fading to a colder hue at the bottom. */
    for (int y = 0; y < win.h; y++) {
        float u = (float)y / (float)win.h;
        Uint8 r = (Uint8)(12 + 8  * u);
        Uint8 g = (Uint8)(20 + 12 * u);
        Uint8 b = (Uint8)(48 + 20 * (1.0f - u));
        SDL_SetRenderDrawColor(renderer, r, g, b, 255);
        SDL_RenderDrawLine(renderer, win.x, win.y + y, win.x + win.w - 1, win.y + y);
    }
    /* Stars: deterministic positions, twinkle with time. */
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < 24; i++) {
        unsigned h = (unsigned)i * 2654435761u + 0x9e3779b9u;
        int sx = win.x + (int)(h % (unsigned)win.w);
        int sy = win.y + (int)((h >> 8) % (unsigned)(win.h * 2 / 3));
        Uint8 a = (Uint8)(120 + (int)((SDL_sinf((float)t * 0.002f + (float)i) + 1.0f) * 60.0f));
        SDL_SetRenderDrawColor(renderer, 220, 230, 250, a);
        SDL_RenderDrawPoint(renderer, sx, sy);
    }
    /* Drifting snow: horizontal-ish streaks moving left across the window. */
    int scroll = (int)((t / 12) % 240);
    for (int i = 0; i < 14; i++) {
        unsigned h = (unsigned)i * 374761393u + 0x85ebca6bu;
        int baseX = (int)(h % 240u);
        int sx = win.x + ((baseX - scroll + 240) % 240) * win.w / 240;
        int sy = win.y + (int)((h >> 7) % (unsigned)win.h);
        Uint8 a = (Uint8)(100 + (int)((h >> 12) % 80u));
        SDL_SetRenderDrawColor(renderer, 230, 235, 245, a);
        int len = 2 + (int)((h >> 16) % 4u);
        SDL_RenderDrawLine(renderer, sx, sy, sx + len, sy + 1);
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static void wagon_dessiner(Background *bg, SDL_Renderer *renderer, int idx, int ox)
{
    if (idx < 0 || idx >= MAX_WAGONS) return;
    StyleWagon *s = &bg->stylesWagons[idx];

    SDL_Rect clip = {ox, 0, W_ECRAN, H_ECRAN};
    SDL_RenderSetClipRect(renderer, &clip);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    if (bg->wagonBgTex[idx]) {
        SDL_RenderCopy(renderer, bg->wagonBgTex[idx], NULL, &clip);

        /* Wagon 3 (Jardin) was authored with the window panes painted solid
         * black instead of an outside view. Paint a procedural snowy sky
         * over them so they read as windows looking outside. */
        if (idx == 3) {
            for (int i = 0; i < s->nbFenetres; i++) {
                SDL_Rect fw = {s->fenetres[i].x + ox, s->fenetres[i].y,
                               s->fenetres[i].w,       s->fenetres[i].h};
                draw_window_outside_view(renderer, fw);
            }
        }
    } else {
        SDL_SetRenderDrawColor(renderer, s->fond.r, s->fond.g, s->fond.b, 255);
        SDL_RenderFillRect(renderer, &clip);
    }

    /* Windows: the wagon PNGs paint their own outside view (the old scrolling
     * parallax-in-window pass clashed with that art and is gone).  We only
     * draw simple framed panes for the procedural fallback wagons. */
    if (!bg->wagonBgTex[idx]) {
        for (int i = 0; i < s->nbFenetres; i++) {
            SDL_Rect fw = {s->fenetres[i].x + ox, s->fenetres[i].y,
                           s->fenetres[i].w,       s->fenetres[i].h};
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(renderer, 9, 13, 24, 255);
            SDL_RenderFillRect(renderer, &fw);
            SDL_SetRenderDrawColor(renderer, 17, 23, 38, 255);
            SDL_Rect topGlow = {fw.x, fw.y, fw.w, fw.h / 3};
            SDL_RenderFillRect(renderer, &topGlow);
            SDL_SetRenderDrawColor(renderer,
                SDL_min(s->mur.r+28,255), SDL_min(s->mur.g+28,255), SDL_min(s->mur.b+28,255), 255);
            for (int t = 0; t < 3; t++) {
                SDL_Rect b = {fw.x-t, fw.y-t, fw.w+2*t, fw.h+2*t};
                SDL_RenderDrawRect(renderer, &b);
            }
        }
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, s->plafond.r, s->plafond.g, s->plafond.b, 255);
    SDL_Rect plaf = {ox, 0, W_ECRAN, WAGON_PLAF_Y};
    SDL_RenderFillRect(renderer, &plaf);
    SDL_SetRenderDrawColor(renderer, s->accent.r, s->accent.g, s->accent.b, 255);
    SDL_RenderDrawLine(renderer, ox, WAGON_PLAF_Y,   ox+W_ECRAN, WAGON_PLAF_Y);
    SDL_RenderDrawLine(renderer, ox, WAGON_PLAF_Y+1, ox+W_ECRAN, WAGON_PLAF_Y+1);

    SDL_SetRenderDrawColor(renderer, s->sol.r, s->sol.g, s->sol.b, 255);
    SDL_Rect sol = {ox, WAGON_SOL_Y, W_ECRAN, H_ECRAN - WAGON_SOL_Y};
    SDL_RenderFillRect(renderer, &sol);
    if (s->floorTex) {
        SDL_RenderCopy(renderer, s->floorTex, NULL, &sol);
    }
    SDL_SetRenderDrawColor(renderer, s->accent.r, s->accent.g, s->accent.b, 255);
    SDL_RenderDrawLine(renderer, ox, WAGON_SOL_Y-1, ox+W_ECRAN, WAGON_SOL_Y-1);
    SDL_RenderDrawLine(renderer, ox, WAGON_SOL_Y,   ox+W_ECRAN, WAGON_SOL_Y);

    SDL_SetRenderDrawColor(renderer, s->mur.r, s->mur.g, s->mur.b, 255);
    SDL_Rect murG = {ox,               WAGON_PLAF_Y, WAGON_MUR_G_X,              WAGON_SOL_Y-WAGON_PLAF_Y};
    SDL_Rect murD = {ox+WAGON_MUR_D_X, WAGON_PLAF_Y, W_ECRAN-WAGON_MUR_D_X,     WAGON_SOL_Y-WAGON_PLAF_Y};
    SDL_RenderFillRect(renderer, &murG);
    SDL_RenderFillRect(renderer, &murD);

    /* NOTE: the wagon doors used to be drawn here as plain SDL_RenderFillRect
     * panels (with a small lock-indicator rect).  That predates the
     * door_anim cinematic and the gp->tex_doorLocked asset rendered in
     * gameplay_render_world — the legacy rects were duplicating those, which
     * is the "old rect door overlapping the new asset" bug visible on the
     * right side of every wagon.  Removed.
     *
     * The door TRIGGER ZONE is unaffected: joueur_update_interieur uses
     * WAGON_MUR_D_X - WAGON_PORTE_W / WAGON_MUR_G_X + WAGON_PORTE_W as the
     * trigger thresholds (background.c:962-974) — pure constants, not the
     * rendered rects.  Same for the player clamps at lines 979-980 and the
     * post-transition reposition at 1094-1096. */

    for (int i = 0; i < s->obstacles.nb; i++) {
        SDL_Rect r = {s->obstacles.rects[i].x+ox, s->obstacles.rects[i].y,
                      s->obstacles.rects[i].w,     s->obstacles.rects[i].h};
        if (s->obstacleTex) {
            SDL_RenderCopy(renderer, s->obstacleTex, NULL, &r);
        } else {
            SDL_Color c = s->obstacles.couleurs[i];
            switch (idx) {
            case 0: {
                /* poor wagon – wooden crate: body, bright top, plank splits */
                SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
                SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, SDL_min(c.r+40,255), SDL_min(c.g+30,255), SDL_min(c.b+20,255), 255);
                SDL_Rect top = {r.x, r.y, r.w, 4};
                SDL_RenderFillRect(renderer, &top);
                SDL_SetRenderDrawColor(renderer, SDL_max(c.r-20,0), SDL_max(c.g-15,0), SDL_max(c.b-10,0), 255);
                SDL_Rect bot = {r.x, r.y+r.h-3, r.w, 3};
                SDL_RenderFillRect(renderer, &bot);
                SDL_SetRenderDrawColor(renderer, SDL_max(c.r-30,0), SDL_max(c.g-22,0), SDL_max(c.b-14,0), 255);
                for (int p = 1; p <= 2; p++) {
                    int px = r.x + r.w * p / 3;
                    SDL_RenderDrawLine(renderer, px, r.y+4, px, r.y+r.h-3);
                }
                SDL_SetRenderDrawColor(renderer, SDL_max(c.r-40,0), SDL_max(c.g-30,0), SDL_max(c.b-20,0), 255);
                SDL_RenderDrawRect(renderer, &r);
                break;
            }
            case 1: {
                /* industry wagon – metal grating: dark body, grating lines, rivet corners */
                SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
                SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, SDL_min(c.r+35,255), SDL_min(c.g+38,255), SDL_min(c.b+45,255), 255);
                for (int gx = r.x+6; gx < r.x+r.w-2; gx += 8)
                    SDL_RenderDrawLine(renderer, gx, r.y+1, gx, r.y+r.h-2);
                SDL_SetRenderDrawColor(renderer, SDL_min(c.r+50,255), SDL_min(c.g+55,255), SDL_min(c.b+65,255), 255);
                SDL_Rect tline = {r.x, r.y, r.w, 2};
                SDL_RenderFillRect(renderer, &tline);
                SDL_SetRenderDrawColor(renderer, s->accentClair.r, s->accentClair.g, s->accentClair.b, 255);
                SDL_Rect rv0 = {r.x+2,       r.y+2,       4, 4};
                SDL_Rect rv1 = {r.x+r.w-6,   r.y+2,       4, 4};
                SDL_Rect rv2 = {r.x+2,       r.y+r.h-6,   4, 4};
                SDL_Rect rv3 = {r.x+r.w-6,   r.y+r.h-6,   4, 4};
                SDL_RenderFillRect(renderer, &rv0); SDL_RenderFillRect(renderer, &rv1);
                SDL_RenderFillRect(renderer, &rv2); SDL_RenderFillRect(renderer, &rv3);
                SDL_SetRenderDrawColor(renderer, SDL_max(c.r-20,0), SDL_max(c.g-20,0), SDL_max(c.b-20,0), 255);
                SDL_RenderDrawRect(renderer, &r);
                break;
            }
            case 2: {
                /* aquarium wagon – ice ledge: icy body, white glint top, teal glow below */
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, SDL_min(c.r+30,255), SDL_min(c.g+70,255), SDL_min(c.b+90,255), 200);
                SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(renderer, 210, 245, 255, 255);
                SDL_Rect glint = {r.x+2, r.y, r.w-4, 3};
                SDL_RenderFillRect(renderer, &glint);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, s->accentClair.r, s->accentClair.g, s->accentClair.b, 45);
                SDL_Rect glow = {r.x-4, r.y+r.h, r.w+8, 10};
                SDL_RenderFillRect(renderer, &glow);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(renderer, s->accentClair.r, s->accentClair.g, s->accentClair.b, 255);
                SDL_RenderDrawRect(renderer, &r);
                break;
            }
            case 3: {
                /* garden wagon – earthen shelf: brown body, green grass top strip */
                SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
                SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, 55, 160, 40, 255);
                SDL_Rect grass = {r.x, r.y, r.w, 4};
                SDL_RenderFillRect(renderer, &grass);
                SDL_SetRenderDrawColor(renderer, 70, 185, 55, 255);
                SDL_RenderDrawLine(renderer, r.x+1, r.y+1, r.x+r.w-2, r.y+1);
                SDL_SetRenderDrawColor(renderer, SDL_max(c.r-18,0), SDL_max(c.g-12,0), SDL_max(c.b-8,0), 255);
                SDL_Rect shadow = {r.x, r.y+r.h-3, r.w, 3};
                SDL_RenderFillRect(renderer, &shadow);
                SDL_SetRenderDrawColor(renderer, SDL_max(c.r-30,0), SDL_max(c.g-20,0), SDL_max(c.b-10,0), 255);
                SDL_RenderDrawRect(renderer, &r);
                break;
            }
            case 4: {
                /* luxury wagon – gold-trimmed marble: cream body, gold top stripe, gold outline */
                SDL_SetRenderDrawColor(renderer, 215, 205, 188, 255);
                SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, s->accentClair.r, s->accentClair.g, s->accentClair.b, 255);
                SDL_Rect gold = {r.x, r.y, r.w, 4};
                SDL_RenderFillRect(renderer, &gold);
                SDL_SetRenderDrawColor(renderer, 180, 170, 155, 255);
                SDL_Rect shade = {r.x, r.y+r.h-3, r.w, 3};
                SDL_RenderFillRect(renderer, &shade);
                SDL_SetRenderDrawColor(renderer, s->accent.r, s->accent.g, s->accent.b, 255);
                for (int t = 0; t < 2; t++) {
                    SDL_Rect bord = {r.x-t, r.y-t, r.w+2*t, r.h+2*t};
                    SDL_RenderDrawRect(renderer, &bord);
                }
                break;
            }
            default: {
                SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
                SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, SDL_min(c.r+28,255), SDL_min(c.g+28,255), SDL_min(c.b+28,255), 255);
                SDL_RenderDrawLine(renderer, r.x, r.y, r.x+r.w, r.y);
                SDL_SetRenderDrawColor(renderer, s->accent.r/3, s->accent.g/3, s->accent.b/3, 255);
                SDL_RenderDrawRect(renderer, &r);
                break;
            }
            }
        }
    }

    int lx = ox + W_ECRAN/2;
    SDL_SetRenderDrawColor(renderer, s->accentClair.r, s->accentClair.g, s->accentClair.b, 255);
    SDL_Rect lamp = {lx-9, WAGON_PLAF_Y+2, 18, 10};
    SDL_RenderFillRect(renderer, &lamp);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, s->accentClair.r, s->accentClair.g, s->accentClair.b, 22);
    SDL_Rect halo = {lx-90, WAGON_PLAF_Y, 180, 90};
    SDL_RenderFillRect(renderer, &halo);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    SDL_RenderSetClipRect(renderer, NULL);
}

void joueur_init_interieur_idx(Background *bg, int idx)
{
    float spawnX = (float)(WAGON_MUR_G_X + 24 + idx * 40);
    Joueur *j = &bg->joueurs[idx];
    j->x    = spawnX;
    j->y    = (float)(WAGON_SOL_Y - JOUEUR_H);
    j->velX = j->velY = 0.0f;
    j->w    = JOUEUR_W;
    j->h    = JOUEUR_H;
    j->auSol         = 1;
    j->actif         = 1;
    j->direction     = 1;
    j->hp            = JOUEUR_MAX_HP;
    j->etat          = PS_IDLE;
    j->lastShootTick = 0;
    j->lastMeleeTick = 0;
    j->muzzleFrames  = 0;
    j->wasGrounded   = 1;
    j->pendingMeleeImpact = 0;
    j->meleeImpactDir     = 1;
    /* Legacy per-row AnimationSystem is still used as a state-machine
     * timer (j->anim.finished gates PS_HIT/PS_SHOOT/PS_MELEE transitions)
     * but no longer renders anything for the player; pass NULL texture. */
    animation_init(&j->anim, NULL,
                   PLAYER_COLS, PLAYER_ROWS,
                   ANIM_P_IDLE, PLAYER_NFRAMES[ANIM_P_IDLE],
                   PLAYER_ANIM_FPS[ANIM_P_IDLE], PLAYER_ANIM_LOOP[ANIM_P_IDLE]);
    /* Load this slot's chosen sheets — but only when they actually
     * changed.  char_anim_free()-on-reload happens inside char_anim_load()
     * and only touches THIS Joueur's ca, so loading P2 doesn't disturb
     * P1.  Skipping the redundant reload means save+load / wagon-respawn
     * within the same match don't re-decode the textures.              */
    if (s_char_anim_renderer &&
        (!j->ca.loaded ||
         j->ca.skin   != player_cfg[idx].skin ||
         j->ca.weapon != player_cfg[idx].weapon)) {
        char_anim_load(&j->ca, s_char_anim_renderer,
                       player_cfg[idx].skin, player_cfg[idx].weapon);
    }
    j->ca.is_locked = 0;
    char_anim_set(&j->ca, ANIM_IDLE);
}

void joueur_init_interieur(Background *bg)
{
    joueur_init_interieur_idx(bg, 0);
    if (bg->nbJoueursActifs >= 2) joueur_init_interieur_idx(bg, 1);
}

void joueur_update_interieur_idx(Background *bg, int idx,
                                  int gauche, int droite,
                                  int sauter, int tirer, int melee, float dt)
{
    Joueur *j = &bg->joueurs[idx];
    if (!j->actif) return;

    Uint32 now = SDL_GetTicks();

    animation_update(&j->anim, dt);
    char_anim_update(&j->ca, dt);

    /* Sword-impact gate: fire melee damage when the visible swing reaches
     * its connect frame, not on key-down.  Runs independent of etat
     * because the legacy ANIM_P_MELEE timer (~0.33s) expires before
     * ANIM_ATTACK's impact frame (~0.49s), so the press-handler's
     * PS_MELEE state is already gone by impact time. */
    if (j->pendingMeleeImpact && char_anim_attack_at_impact(&j->ca)) {
        apply_melee_impact(bg, j, bg->wagonActif);
        j->pendingMeleeImpact = 0;
    }
    /* Safety: if the player dies or is hit before impact, drop the pending
     * swing so we don't fire damage after the swing is no longer playing. */
    if (j->pendingMeleeImpact &&
        (j->etat == PS_DEAD || j->etat == PS_HIT ||
         j->ca.current == ANIM_DEATH || j->ca.current == ANIM_HIT_REACT)) {
        j->pendingMeleeImpact = 0;
    }

    /* Same idea for ranged fire: bullet is spawned when ANIM_GUN reaches
     * its visible muzzle-flash frame, not on key-down.                  */
    if (j->pendingGunFire && char_anim_gun_at_fire(&j->ca)) {
        if (bg->nbBalles[idx] < MAX_BALLES) {
            float mx = j->x + (j->gunFireDir == 1
                                 ? (float)JOUEUR_W + 4.0f
                                 : -4.0f);
            float my = j->y + (float)JOUEUR_H * 0.4f;
            Balle *b = &bg->balles[idx][bg->nbBalles[idx]++];
            b->actif = 1;
            b->x     = mx;
            b->y     = my;
            b->velX  = (float)(j->gunFireDir) * BULLET_SPEED;
            b->wagon = bg->wagonActif;
            /* Drive the muzzle-flash overlay (existing 3-frame burst).  */
            j->muzzleX      = mx;
            j->muzzleY      = j->y + (float)JOUEUR_H * 0.35f;
            j->muzzleFrames = 3;
        }
        j->pendingGunFire = 0;
    }
    if (j->pendingGunFire &&
        (j->etat == PS_DEAD || j->etat == PS_HIT ||
         j->ca.current == ANIM_DEATH || j->ca.current == ANIM_HIT_REACT)) {
        j->pendingGunFire = 0;
    }

    if (j->etat == PS_DEAD) {
        char_anim_set(&j->ca, ANIM_DEATH);
        return;
    }

    /* PS_HIT used to early-return here, locking the player out of all
     * movement input for the duration of ANIM_P_HIT (~0.67s).  That made
     * Ellie feel completely stunned every time an enemy connected — the
     * user wanted control back during hitstun.  We now just clear the
     * etat once the legacy timer expires and let the rest of the update
     * (movement, jump, attack) run.  The ANIM_HIT_REACT sprite has its own
     * lock so the visual stagger still plays through. */
    if (j->etat == PS_HIT && j->anim.finished) {
        j->etat = PS_IDLE;
        animation_set_row(&j->anim, ANIM_P_IDLE,
                          PLAYER_NFRAMES[ANIM_P_IDLE],
                          PLAYER_ANIM_FPS[ANIM_P_IDLE], 1);
    }

    if (j->etat == PS_SHOOT || j->etat == PS_MELEE) {
        if (j->anim.finished) {
            j->etat = PS_IDLE;
            animation_set_row(&j->anim, ANIM_P_IDLE,
                              PLAYER_NFRAMES[ANIM_P_IDLE],
                              PLAYER_ANIM_FPS[ANIM_P_IDLE], 1);
            char_anim_set(&j->ca, ANIM_IDLE);
        }
        if (j->muzzleFrames > 0) j->muzzleFrames--;

        j->velY += GRAVITE_JEU;
        if (j->velY > 18.0f) j->velY = 18.0f;
        j->y += j->velY;
        j->auSol = 0;
        int swing_floor = (g_game_mode == MODE_VERSUS)
                          ? VERSUS_FLOOR_Y : WAGON_SOL_Y;
        if (j->y + j->h >= (float)swing_floor) {
            j->y = (float)(swing_floor - j->h); j->velY = 0; j->auSol = 1;
        }
        return;
    }

    if (bg->transitionSlide.actif) return;

    int widx = bg->wagonActif;
    StyleWagon *s = &bg->stylesWagons[widx];

    /* Ranged fire (F by default; see keyShoot in main.c).  Guarded on:
     *   - cooldown expired
     *   - feet on the floor (no air-firing)
     *   - char_anim not locked (no firing through an attack/jump/hit/death) */
    if (tirer &&
        (int)(now - j->lastShootTick) >= SHOOT_COOLDOWN_MS &&
        j->auSol &&
        !char_anim_is_locked(&j->ca)) {
        j->lastShootTick = now;
        j->etat = PS_SHOOT;
        animation_set_row(&j->anim, ANIM_P_SHOOT,
                          PLAYER_NFRAMES[ANIM_P_SHOOT],
                          PLAYER_ANIM_FPS[ANIM_P_SHOOT], 0);
        char_anim_set(&j->ca, ANIM_GUN);
        /* The projectile spawn is deferred to CHAR_GUN_FIRE_FRAME — see
         * the pendingGunFire block at the top of this function.        */
        j->pendingGunFire = 1;
        j->gunFireDir     = j->direction;
        return;
    }

    if (melee && (int)(now - j->lastMeleeTick) >=
                  CHAR_WEAPON_STATS[player_cfg[idx].weapon].melee_cooldown_ms) {
        j->lastMeleeTick = now;
        j->etat = PS_MELEE;
        animation_set_row(&j->anim, ANIM_P_MELEE,
                          PLAYER_NFRAMES[ANIM_P_MELEE],
                          PLAYER_ANIM_FPS[ANIM_P_MELEE], 0);
        char_anim_set(&j->ca, ANIM_ATTACK);
        /* Defer damage until the sword reaches its impact frame; see
         * the PS_MELEE branch above where apply_melee_impact() is called. */
        j->pendingMeleeImpact = 1;
        j->meleeImpactDir     = j->direction;
        return;
    }

    if (gauche)      { j->velX = -VITESSE_JOUEUR; j->direction = -1; }
    else if (droite) { j->velX =  VITESSE_JOUEUR; j->direction =  1; }
    else               j->velX *= 0.72f;

    if (sauter && j->auSol) { j->velY = JUMP_FORCE; j->auSol = 0; }

    j->velY += GRAVITE_JEU;
    if (j->velY > 18.0f) j->velY = 18.0f;
    float xBeforeMove = j->x;
    j->x += j->velX;
    j->y += j->velY;

    if (j->y < (float)(WAGON_PLAF_Y + 2)) { j->y = (float)(WAGON_PLAF_Y+2); j->velY = 0.0f; }

    /* Versus uses a different floor line so feet rest on the grating
     * painted in the engine-room background art (Prompt 13).         */
    int floor_y = (g_game_mode == MODE_VERSUS)
                  ? VERSUS_FLOOR_Y : WAGON_SOL_Y;
    j->auSol = 0;
    if (j->y + j->h >= (float)floor_y) {
        j->y = (float)(floor_y - j->h); j->velY = 0.0f; j->auSol = 1;
    }

    for (int i = 0; i < s->obstacles.nb; i++) {
        SDL_Rect pr = s->obstacles.rects[i];
        if (j->x + (float)j->w <= (float)pr.x || j->x >= (float)(pr.x + pr.w)) continue;
        float feet    = j->y + (float)j->h;
        float platTop = (float)pr.y;
        if (j->velY >= 0.0f && feet >= platTop && (feet - j->velY) <= platTop + 10.0f) {
            /* landing or standing: fell onto platform from above */
            j->y     = platTop - (float)j->h;
            j->velY  = 0.0f;
            j->auSol = 1;
        } else if (feet < platTop && platTop - feet <= 10.0f && j->velY >= -1.0f) {
            /* riding: platform moved down this frame, snap player onto it */
            j->y     = platTop - (float)j->h;
            j->velY  = 0.0f;
            j->auSol = 1;
        }
    }

    if (!j->auSol) {
        EtatJoueur next = (j->velY < 0) ? PS_JUMP : PS_FALL;
        if (j->etat != next) {
            j->etat = next;
            int row = (next == PS_JUMP) ? ANIM_P_JUMP : ANIM_P_FALL;
            animation_set_row(&j->anim, row, PLAYER_NFRAMES[row], PLAYER_ANIM_FPS[row], 0);
        }
        /* ANIM_JUMP plays once and holds its last frame for the whole
         * airborne arc — no separate fall anim. */
        char_anim_set(&j->ca, ANIM_JUMP);
    } else if ((gauche || droite) && (j->etat != PS_WALK)) {
        j->etat = PS_WALK;
        animation_set_row(&j->anim, ANIM_P_WALK,
                          PLAYER_NFRAMES[ANIM_P_WALK], PLAYER_ANIM_FPS[ANIM_P_WALK], 1);
        char_anim_set(&j->ca, ANIM_RUN);
    } else if (!gauche && !droite && j->auSol && j->etat != PS_IDLE) {
        j->etat = PS_IDLE;
        animation_set_row(&j->anim, ANIM_P_IDLE,
                          PLAYER_NFRAMES[ANIM_P_IDLE], PLAYER_ANIM_FPS[ANIM_P_IDLE], 1);
        char_anim_set(&j->ca, ANIM_IDLE);
    } else if (j->auSol && !j->wasGrounded) {
        /* Just landed — leave the JUMP held frame and resync to
         * IDLE/RUN even if etat is unchanged. */
        char_anim_set(&j->ca,
            (gauche || droite) ? ANIM_RUN : ANIM_IDLE);
    }

    j->wasGrounded = j->auSol;

    /* In VERSUS the arena is a single-wagon shared space — suppress the
     * door/transition logic so neither player can leave the arena.      */
    if (g_game_mode != MODE_VERSUS) {
        if (j->x + j->w >= (float)(WAGON_MUR_D_X - WAGON_PORTE_W) && widx < MAX_WAGONS-1) {
            /* Don't trigger ANY transition (door anim or boss cinematic)
             * while the kronole door is still locked — main.c sets
             * blockTransitionRight from gp.door.state.  Without this
             * gate the boss-intro fired the moment the player touched
             * the right edge of wagon 3 even if they hadn't paid
             * namgoong yet.                                              */
            if (bg->blockTransitionRight) return;

            /* Forward wagon transition.  The legacy slide is replaced by the
             * door cinematic — close-and-lock when stepping into the final
             * (boss) wagon, normal door-open everywhere else.  door_anim itself
             * honors bg->blockTransitionRight, matching the old call.
             *
             * Single-shot boss-intro hook: the very first time we step into
             * the final wagon we go through boss_intro_start() (which itself
             * fires door_anim_close_and_lock + holds the input gate + drives
             * the 4-stage cinematic).  After that, the boss-intro one-shot is
             * burnt and we fall back to the plain door close-and-lock so a
             * post-death respawn into the boss wagon still slams the door. */
            if (widx + 1 == MAX_WAGONS - 1) {
                /* First crossing into wagon 5: play the boss cutscene
                 * BEFORE the existing boss intro.  The cutscene itself
                 * fires boss_intro_start() at its hand-off, so the
                 * door slam / pan / reveal still happens — just after
                 * the black-screen reveal animation.                  */
                if (boss_cutscene_should_start())      boss_cutscene_start();
                else if (boss_intro_should_start())    boss_intro_start();
                else                                    door_anim_close_and_lock();
            } else {
                door_anim_open();
            }
            return;
        }
        if (j->x <= (float)(WAGON_MUR_G_X + WAGON_PORTE_W) && widx > 0) {
            /* Backwards transition keeps the original slide; the door cinematic
             * is spec'd as a forward-only effect (Stage-3 "Wagon X / 5" reads
             * as a forward beat). */
            transitionSlide_declencher(bg, -1); return;
        }
    }

    /* Wall clamps.  VERSUS uses the wider 16:9 arena bounds (VS_WAGON_*)
     * and ignores door padding so the full arena width is playable;
     * campaign keeps the door-aware stops on the 800-wide wagon.        */
    int leftStop, rightStop;
    if (g_game_mode == MODE_VERSUS) {
        leftStop  = VS_WAGON_L;
        rightStop = VS_WAGON_R - j->w;
    } else {
        leftStop  = WAGON_MUR_G_X + (widx > 0 ? WAGON_PORTE_W : 0);
        rightStop = WAGON_MUR_D_X - (widx < MAX_WAGONS-1 ? WAGON_PORTE_W : 0) - j->w;
    }
    if (j->x < (float)leftStop)  { j->x = (float)leftStop;  j->velX = 0.0f; }
    if (j->x > (float)rightStop) { j->x = (float)rightStop; j->velX = 0.0f; }

    int other = idx ^ 1;
    if (bg->nbJoueursActifs >= 2 && bg->joueurs[other].actif &&
        bg->joueurs[other].hp > 0) {
        SDL_Rect me    = {(int)j->x,           (int)j->y, JOUEUR_W, JOUEUR_H};
        Joueur  *o     = &bg->joueurs[other];
        SDL_Rect oRect = {(int)o->x,            (int)o->y, JOUEUR_W, JOUEUR_H};
        if (SDL_HasIntersection(&me, &oRect))
            j->x = xBeforeMove;
    }
}

void joueur_update_interieur(Background *bg, int gauche, int droite,
                              int sauter, int tirer, int melee, float dt)
{
    joueur_update_interieur_idx(bg, 0, gauche, droite, sauter, tirer, melee, dt);
}

void joueur_afficher_interieur_idx(Background *bg, SDL_Renderer *renderer,
                                    int idx, int ox)
{
    Joueur *j = &bg->joueurs[idx];
    if (!j->actif) return;

    if (j->ca.loaded) {
        SDL_RendererFlip flip = (j->direction == -1) ? SDL_FLIP_HORIZONTAL
                                                     : SDL_FLIP_NONE;
        int foot_x = (int)j->x + ox + JOUEUR_W / 2;
        int foot_y = (int)j->y + JOUEUR_H;
        char_anim_render(&j->ca, renderer, foot_x, foot_y,
                         PLAYER_DRAW_H, flip);
    } else {

        SDL_Rect dest = {(int)j->x + ox, (int)j->y, j->w, j->h};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer, 44, 48, 56, 255);
        SDL_RenderFillRect(renderer, &dest);
        SDL_SetRenderDrawColor(renderer, 65, 105, 155, 255);
        SDL_Rect torso = {dest.x+2, dest.y+dest.h*3/10, dest.w-4, dest.h*4/10};
        SDL_RenderFillRect(renderer, &torso);
        SDL_SetRenderDrawColor(renderer, 205, 185, 165, 255);
        SDL_Rect tete = {dest.x+dest.w/4, dest.y-dest.h/5+2, dest.w/2, dest.h/5};
        SDL_RenderFillRect(renderer, &tete);
    }

    if (j->muzzleFrames > 0 && bg->muzzleTex) {
        int mx = (int)j->muzzleX + ox - MUZZLE_DRAW_W/2;
        if (j->direction == 1) mx = (int)j->x + ox + JOUEUR_W;
        else                   mx = (int)j->x + ox - MUZZLE_DRAW_W;
        int my = (int)j->muzzleY - MUZZLE_DRAW_H/2;
        SDL_Rect mdst = {mx, my, MUZZLE_DRAW_W, MUZZLE_DRAW_H};
        SDL_RendererFlip mflip = (j->direction == -1) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
        SDL_RenderCopyEx(renderer, bg->muzzleTex, NULL, &mdst, 0, NULL, mflip);
    }

    if (j->hp > 0) {
        int max_hp = (g_game_mode == MODE_VERSUS)
                     ? VERSUS_MAX_HP : JOUEUR_MAX_HP;
        int barW   = JOUEUR_W + 12;
        int filled = (j->hp * barW) / max_hp;
        if (filled > barW) filled = barW;
        int bx     = (int)j->x + ox + JOUEUR_W/2 - barW/2;
        int by     = (int)j->y - 10;
        SDL_Rect bgBar = {bx, by, barW, 4};
        SDL_Rect fgBar = {bx, by, filled, 4};
        SDL_SetRenderDrawColor(renderer, 20, 40, 20, 255);
        SDL_RenderFillRect(renderer, &bgBar);
        SDL_SetRenderDrawColor(renderer, 60, 220, 80, 255);
        SDL_RenderFillRect(renderer, &fgBar);
    }
}

void joueur_afficher_interieur(Background *bg, SDL_Renderer *renderer, int ox)
{
    joueur_afficher_interieur_idx(bg, renderer, 0, ox);
    if (bg->nbJoueursActifs >= 2)
        joueur_afficher_interieur_idx(bg, renderer, 1, ox);
}

void transitionSlide_declencher(Background *bg, int direction)
{
    if (bg->transitionSlide.actif) return;
    if (bg->blockTransitionRight && direction ==  1) return;
    if (bg->blockTransitionLeft  && direction == -1) return;
    bg->transitionSlide.actif      = 1;
    bg->transitionSlide.direction  = direction;
    bg->transitionSlide.offset     = 0.0f;
    bg->transitionSlide.wagonCible = bg->wagonActif + direction;
}

void transitionSlide_update(Background *bg)
{
    if (!bg->transitionSlide.actif) return;
    bg->transitionSlide.offset += 30.0f;
    if (bg->transitionSlide.offset >= (float)W_ECRAN) {
        bg->transitionSlide.offset = 0.0f;
        bg->transitionSlide.actif  = 0;
        bg->wagonActif = bg->transitionSlide.wagonCible;
        if (bg->transitionSlide.direction == 1)
            bg->blockTransitionLeft = 1;
        for (int p = 0; p < bg->nbJoueursActifs; p++) {
            if (!bg->joueurs[p].actif) continue;
            if (bg->transitionSlide.direction == 1)
                bg->joueurs[p].x = (float)(WAGON_MUR_G_X + WAGON_PORTE_W + 6 + p * 30);
            else
                bg->joueurs[p].x = (float)(WAGON_MUR_D_X - WAGON_PORTE_W - JOUEUR_W - 6 - p * 30);
            bg->joueurs[p].y    = (float)(WAGON_SOL_Y - JOUEUR_H);
            bg->joueurs[p].velX = bg->joueurs[p].velY = 0.0f;
            bg->joueurs[p].auSol = 1;
        }
    }
}

/* --- kinetic 3-hull compositor support -------------------------------------
 * Render the wagon scene into the kinetic off-screen target (three wagon-slots
 * wide -- 3*W_ECRAN):
 *   slot 0 [x 0 .. W_ECRAN)          = wagonActif-1, decor only (prev wagon)
 *   slot 1 [x W_ECRAN .. 2*W_ECRAN)  = wagonActif, full (decor + entities)
 *   slot 2 [x 2*W_ECRAN .. 3*W_ECRAN)= wagonActif+1, decor only (next wagon)
 * Out-of-range neighbour slots (at the ends of the train) are simply left
 * blank.  The kinetic compositor pans all three slots (and the matching hull
 * windows) so the camera moves hull-to-hull on a transition.  Players stay
 * frozen in the current wagon (slot 1, ox=W_ECRAN) during the slide --
 * transitionSlide_update repositions them once the swap completes. */
/* Locate the final-boss enforcer (lone ET_ENFORCER in MAX_WAGONS-1) so
 * the dedicated sprite-sheet renderer can draw it in place of the default
 * enemy renderer.  Matches the logic in boss_intro.c::find_boss_enemy_idx. */
static int wagon_find_boss_idx(const Background *bg)
{
    if (!bg) return -1;
    for (int i = 0; i < bg->nbEnnemis; i++) {
        const Ennemi *e = &bg->ennemis[i];
        if (!e->actif) continue;
        if (e->wagon != MAX_WAGONS - 1) continue;
        if (e->type == ET_ENFORCER) return i;
    }
    return -1;
}

static void wagon_render_boss_if_present(const Background *bg, int idx, int ox)
{
    if (idx != MAX_WAGONS - 1) return;
    int bi = wagon_find_boss_idx(bg);
    if (bi < 0) return;
    const Ennemi *boss = &bg->ennemis[bi];
    /* Always render via boss_anim — the BOSS_DEATH animation IS the
     * death visual now (replacing the old chunk-spark explosion path). */
    boss_anim_render((float)boss->x, (float)boss->y, ox,
                     boss->direction, BOSS_HBW, BOSS_HBH);
}

static void wagon_renderFullAt(Background *bg, SDL_Renderer *renderer, int idx, int ox)
{
    if (idx < 0 || idx >= MAX_WAGONS) return;
    EffectsSystem *efx = effects_global();

    wagon_dessiner(bg, renderer, idx, ox);
    /* shadows go on the wagon floor, under the entities */
    if (efx) effects_render_shadows(efx, bg, renderer, idx, ox);

    ennemis_afficher(bg->ennemis, bg->nbEnnemis, idx, renderer, ox);
    /* Boss is skipped inside ennemis_afficher; render it here with the
     * dedicated 7-row sheet so the new animation states show. */
    wagon_render_boss_if_present(bg, idx, ox);
    for (int p = 0; p < bg->nbJoueursActifs; p++)
        balles_afficher(bg->balles[p], bg->nbBalles[p], renderer, bg->bulletTex, ox);
    hitfx_afficher(bg->hitFx, bg->nbHitFx, renderer, ox);
    joueur_afficher_interieur(bg, renderer, ox);

    /* additive white blink + death chunks/sparks on top of the entities */
    if (efx) {
        effects_render_hit_blink(efx, bg, renderer, ox);
        effects_render_world(efx, renderer, ox);
    }
    particles_render(&bg->particles, renderer, ox);

    /* multiply-blend torch light over the wagon interior */
    if (efx) effects_composite_lightmap(efx, renderer, ox, 0, W_ECRAN, H_ECRAN);
}

void wagonsKinetic_renderRT(Background *bg, SDL_Renderer *renderer)
{
    int cur = bg->wagonActif;
    if (cur - 1 >= 0)            wagon_dessiner(bg, renderer, cur - 1, 0);            /* prev */
    wagon_renderFullAt(bg, renderer, cur, W_ECRAN);                                   /* current */
    if (cur + 1 < MAX_WAGONS)    wagon_dessiner(bg, renderer, cur + 1, 2 * W_ECRAN);  /* next */
}

void wagonsInterieurs_afficher_camera(Background *bg, SDL_Renderer *renderer, int ox)
{
    int idx = bg->wagonActif;
    int dir = bg->transitionSlide.direction;
    int off = (int)bg->transitionSlide.offset;
    int nxt = bg->transitionSlide.wagonCible;

    if (!bg->transitionSlide.actif) {
        EffectsSystem *efx = effects_global();
        wagon_dessiner(bg, renderer, idx, ox);
        if (efx) effects_render_shadows(efx, bg, renderer, idx, ox);
        ennemis_afficher(bg->ennemis, bg->nbEnnemis, idx, renderer, ox);
        wagon_render_boss_if_present(bg, idx, ox);
        for (int p = 0; p < bg->nbJoueursActifs; p++)
            balles_afficher(bg->balles[p], bg->nbBalles[p], renderer, bg->bulletTex, ox);
        hitfx_afficher(bg->hitFx, bg->nbHitFx, renderer, ox);
        joueur_afficher_interieur(bg, renderer, ox);
        if (efx) {
            effects_render_hit_blink(efx, bg, renderer, ox);
            effects_render_world(efx, renderer, ox);
        }
        particles_render(&bg->particles, renderer, ox);
        if (efx) effects_composite_lightmap(efx, renderer, ox, 0, W_ECRAN, H_ECRAN);
    } else {
        int ox_cur = (dir == 1) ? -off :  off;
        int ox_nxt = (dir == 1) ?  W_ECRAN - off : -W_ECRAN + off;
        wagon_dessiner(bg, renderer, nxt, ox + ox_nxt);
        wagon_dessiner(bg, renderer, idx, ox + ox_cur);
        joueur_afficher_interieur(bg, renderer, ox + ox_cur);
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < MAX_NEIGE; i++) {
        ParticuleNeige *p = &bg->neige[i];
        int cx = (int)p->x;
        int cy = (int)p->y;
        Uint8 a = (Uint8)p->alpha;
        SDL_SetRenderDrawColor(renderer, 200, 220, 255, a);
        SDL_RenderDrawPoint(renderer, cx, cy);
        if (p->taille >= 2) {
            SDL_RenderDrawPoint(renderer, cx + 1, cy);
            SDL_RenderDrawPoint(renderer, cx - 1, cy);
            SDL_RenderDrawPoint(renderer, cx, cy + 1);
            SDL_RenderDrawPoint(renderer, cx, cy - 1);
        }
        if (p->taille >= 3) {
            Uint8 ah = (Uint8)(a * 3 / 5);
            SDL_SetRenderDrawColor(renderer, 200, 220, 255, ah);
            SDL_RenderDrawPoint(renderer, cx + 1, cy + 1);
            SDL_RenderDrawPoint(renderer, cx - 1, cy + 1);
            SDL_RenderDrawPoint(renderer, cx + 1, cy - 1);
            SDL_RenderDrawPoint(renderer, cx - 1, cy - 1);
        }
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

void wagonsInterieurs_afficher(Background *bg, SDL_Renderer *renderer)
{
    wagonsInterieurs_afficher_camera(bg, renderer, 0);
}

void minimap_afficher(Background *bg, SDL_Renderer *renderer)
{
    const int MM_X = W_ECRAN - 170;
    const int MM_Y = 24;
    const int MM_W = 160;
    const int MM_H = 80;
    const int WX   = WAGON_MUR_D_X - WAGON_MUR_G_X;
    const int WY   = WAGON_SOL_Y   - WAGON_PLAF_Y;

    SDL_Rect box = {MM_X, MM_Y, MM_W, MM_H};

    SDL_Rect panel = {MM_X - 4, MM_Y - 4, MM_W + 8, MM_H + 8};
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 15, 15, 20, 200);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    SDL_RenderSetClipRect(renderer, &box);
    if (bg->wagonBgTex[bg->wagonActif]) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_RenderCopy(renderer, bg->wagonBgTex[bg->wagonActif], NULL, &box);
    } else {
        StyleWagon *s = &bg->stylesWagons[bg->wagonActif];
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer, s->fond.r, s->fond.g, s->fond.b, 255);
        SDL_RenderFillRect(renderer, &box);
    }
    SDL_RenderSetClipRect(renderer, NULL);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 110);
    SDL_RenderFillRect(renderer, &box);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    SDL_SetRenderDrawColor(renderer, 180, 130, 40, 255);
    SDL_RenderDrawRect(renderer, &panel);

    SDL_SetRenderDrawColor(renderer, 120, 100, 80, 220);
    SDL_Rect floorLine = {MM_X + 1, MM_Y + MM_H - 4, MM_W - 2, 3};
    SDL_RenderFillRect(renderer, &floorLine);

    SDL_SetRenderDrawColor(renderer, 220, 220, 220, 255);
    SDL_Rect ldoor = {MM_X + 2,        MM_Y + MM_H - 14, 5, 12};
    SDL_Rect rdoor = {MM_X + MM_W - 7, MM_Y + MM_H - 14, 5, 12};
    SDL_RenderFillRect(renderer, &ldoor);
    SDL_RenderFillRect(renderer, &rdoor);

    for (int i = 0; i < bg->nbEnnemis; i++) {
        Ennemi *e = &bg->ennemis[i];
        if (!e->actif || e->wagon != bg->wagonActif || e->etat == ES_DEAD) continue;
        int ehw = ennemi_hbw(e);
        int ehh = ennemi_hbh(e);
        int ex = MM_X + (int)((e->x + ehw / 2.0f - WAGON_MUR_G_X) * (float)MM_W / WX);
        int ey = MM_Y + (int)((e->y + ehh         - WAGON_PLAF_Y)  * (float)MM_H / WY);
        if (ex < MM_X + 6)        ex = MM_X + 6;
        if (ex > MM_X + MM_W - 7) ex = MM_X + MM_W - 7;
        if (ey < MM_Y + 6)        ey = MM_Y + 6;
        if (ey > MM_Y + MM_H - 7) ey = MM_Y + MM_H - 7;
        SDL_SetRenderDrawColor(renderer, 255, 50, 50, 255);
        SDL_Rect er = {ex - 5, ey - 5, 10, 10};
        SDL_RenderFillRect(renderer, &er);
        SDL_SetRenderDrawColor(renderer, 180, 20, 20, 255);
        SDL_RenderDrawRect(renderer, &er);
    }

    float pfeet_x = bg->joueurs[0].x + JOUEUR_W / 2.0f;
    float pfeet_y = bg->joueurs[0].y + JOUEUR_H;
    int px = MM_X + (int)((pfeet_x - WAGON_MUR_G_X) * (float)MM_W / WX);
    int py = MM_Y + (int)((pfeet_y - WAGON_PLAF_Y)  * (float)MM_H / WY);
    if (px < MM_X + 6)        px = MM_X + 6;
    if (px > MM_X + MM_W - 7) px = MM_X + MM_W - 7;
    if (py < MM_Y + 6)        py = MM_Y + 6;
    if (py > MM_Y + MM_H - 7) py = MM_Y + MM_H - 7;
    SDL_SetRenderDrawColor(renderer, 20, 30, 60, 255);
    SDL_Rect po = {px - 5, py - 5, 10, 10};
    SDL_RenderFillRect(renderer, &po);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_Rect pr = {px - 4, py - 4, 8, 8};
    SDL_RenderFillRect(renderer, &pr);

    if (bg->nbJoueursActifs >= 2 && bg->joueurs[1].hp > 0) {
        float p2fx = bg->joueurs[1].x + JOUEUR_W / 2.0f;
        float p2fy = bg->joueurs[1].y + JOUEUR_H;
        int p2x = MM_X + (int)((p2fx - WAGON_MUR_G_X) * (float)MM_W / WX);
        int p2y = MM_Y + (int)((p2fy - WAGON_PLAF_Y)  * (float)MM_H / WY);
        if (p2x < MM_X + 6)        p2x = MM_X + 6;
        if (p2x > MM_X + MM_W - 7) p2x = MM_X + MM_W - 7;
        if (p2y < MM_Y + 6)        p2y = MM_Y + 6;
        if (p2y > MM_Y + MM_H - 7) p2y = MM_Y + MM_H - 7;
        SDL_SetRenderDrawColor(renderer, 20, 30, 60, 255);
        SDL_Rect p2o = {p2x - 5, p2y - 5, 10, 10};
        SDL_RenderFillRect(renderer, &p2o);
        SDL_SetRenderDrawColor(renderer, 60, 200, 240, 255);
        SDL_Rect p2r = {p2x - 4, p2y - 4, 8, 8};
        SDL_RenderFillRect(renderer, &p2r);
    }
}

int sauvegarder_partie(const Background *bg, const struct GameplayState *gp,
                       int scoreJoueur, Uint32 tempsDepart)
{
    FILE *fp = fopen(FICHIER_SAUVEGARDE, "w");
    if (!fp) return 0;

    Uint32 elapsed = SDL_GetTicks() - tempsDepart;

    fprintf(fp, "wagon:%d\n",           bg->wagonActif);
    fprintf(fp, "nbJoueursActifs:%d\n", bg->nbJoueursActifs);
    fprintf(fp, "score:%d\n",           scoreJoueur);
    fprintf(fp, "tempsDepart:%u\n",     (unsigned)tempsDepart);
    fprintf(fp, "tempsEcoule:%u\n",     (unsigned)elapsed);

    if (gp) {
        fprintf(fp, "mode:%d\n",            gp->gameMode);
        fprintf(fp, "kronole:%d\n",         gp->kronoleCount);
        fprintf(fp, "enemiesKilled:%d\n",   gp->enemiesKilled);
        fprintf(fp, "interludeScore:%d\n",  gp->totalInterludeScore);
        for (int i = 0; i < NB_JOUEURS_MAX; i++)
            fprintf(fp, "ammo:%d:%d\n", i, gp->ammo[i]);
        fprintf(fp, "namgoong:%d:%f:%f:%d:%d:%d:%d\n",
                gp->namgoong.activeSolo,
                gp->namgoong.x, gp->namgoong.y,
                gp->namgoong.direction, gp->namgoong.wagon,
                gp->namgoong.hp, gp->namgoong.working);

        Uint32 doorElapsed = 0;
        if (gp->door.state == DOOR_UNLOCKING) {
            Uint32 now = SDL_GetTicks();
            doorElapsed = gp->door.paused
                ? (gp->door.pausedFor - gp->door.startTick)
                : (now - gp->door.startTick);
        }
        fprintf(fp, "door:%d:%d:%d:%d:%d:%d:%u\n",
                (int)gp->door.state, gp->door.actif, gp->door.paused,
                gp->door.targetWagon, gp->door.enemiesSpawned,
                gp->door.nbEnemiesAtUnlockStart, (unsigned)doorElapsed);
    }

    for (int i = 0; i < NB_JOUEURS_MAX; i++) {
        const Joueur *j = &bg->joueurs[i];
        fprintf(fp, "joueur:%d:%f:%f:%d:%d:%d\n",
                i, j->x, j->y, j->hp, j->direction, j->actif);
    }

    for (int i = 0; i < bg->nbEnnemis; i++) {
        const Ennemi *e = &bg->ennemis[i];
        fprintf(fp, "ennemi:%d:%f:%f:%d:%d:%d:%d\n",
                (int)e->type, e->x, e->y, e->hp,
                e->wagon, e->actif, (int)e->etat);
    }

    fclose(fp);
    PERSIST_SYNC();
    return 1;
}

int charger_partie(Background *bg, struct GameplayState *gp,
                   int *scoreJoueur, Uint32 *tempsDepart)
{
    FILE *fp = fopen(FICHIER_SAUVEGARDE, "r");
    if (!fp) return 0;

    memset(bg->ennemis, 0, sizeof(bg->ennemis));
    bg->nbEnnemis = 0;
    for (int i = 0; i < NB_JOUEURS_MAX; i++) {
        bg->nbBalles[i] = 0;
        memset(bg->balles[i], 0, sizeof(bg->balles[i]));
    }
    bg->nbHitFx = 0;
    memset(bg->hitFx, 0, sizeof(bg->hitFx));
    bg->transitionSlide.actif  = 0;
    bg->transitionSlide.offset = 0.0f;

    if (gp) {
        gameplay_reset_for_run(gp, gp->gameMode);
    }

    char    line[256];
    int     nbEnnemisLus = 0;
    Uint32  elapsed_ms   = 0;
    int     doorLineSeen = 0;

    while (fgets(line, sizeof(line), fp)) {
        int ival;
        unsigned uval;

        if (sscanf(line, "wagon:%d", &ival) == 1) {
            bg->wagonActif = ival;
            continue;
        }
        if (sscanf(line, "nbJoueursActifs:%d", &ival) == 1) {
            bg->nbJoueursActifs = ival;
            continue;
        }
        if (sscanf(line, "score:%d", &ival) == 1) {
            *scoreJoueur = ival;
            continue;
        }
        if (sscanf(line, "tempsEcoule:%u", &uval) == 1) {
            elapsed_ms = uval;
            continue;
        }
        if (sscanf(line, "mode:%d", &ival) == 1) {
            if (gp) gp->gameMode = ival;
            continue;
        }
        if (sscanf(line, "kronole:%d", &ival) == 1) {
            if (gp) gp->kronoleCount = ival;
            continue;
        }
        if (sscanf(line, "enemiesKilled:%d", &ival) == 1) {
            if (gp) gp->enemiesKilled = ival;
            continue;
        }
        if (sscanf(line, "interludeScore:%d", &ival) == 1) {
            if (gp) gp->totalInterludeScore = ival;
            continue;
        }
        {
            int aidx, aval;
            if (sscanf(line, "ammo:%d:%d", &aidx, &aval) == 2) {
                if (gp && aidx >= 0 && aidx < NB_JOUEURS_MAX)
                    gp->ammo[aidx] = aval;
                continue;
            }
        }
        {
            int nactive, ndir, nwagon, nhp, nworking;
            float nx, ny;
            if (sscanf(line, "namgoong:%d:%f:%f:%d:%d:%d:%d",
                       &nactive, &nx, &ny, &ndir, &nwagon, &nhp, &nworking) == 7) {
                if (gp) {
                    gp->namgoong.activeSolo = nactive;
                    gp->namgoong.x          = nx;
                    gp->namgoong.y          = ny;
                    gp->namgoong.direction  = ndir;
                    gp->namgoong.wagon      = nwagon;
                    gp->namgoong.hp         = nhp;
                    gp->namgoong.working    = nworking;
                    if (gp->namgoong.maxHp <= 0) gp->namgoong.maxHp = 100;
                }
                continue;
            }
        }
        {
            int dstate, dactif, dpaused, dtarget, despawn, dnbE;
            unsigned dElapsed;
            if (sscanf(line, "door:%d:%d:%d:%d:%d:%d:%u",
                       &dstate, &dactif, &dpaused, &dtarget,
                       &despawn, &dnbE, &dElapsed) == 7) {
                doorLineSeen = 1;
                if (gp) {
                    Uint32 now = SDL_GetTicks();
                    gp->door.state                  = (DoorState)dstate;
                    gp->door.actif                  = dactif;
                    gp->door.paused                 = dpaused;
                    gp->door.targetWagon            = dtarget;
                    gp->door.enemiesSpawned         = despawn;
                    gp->door.nbEnemiesAtUnlockStart = dnbE;
                    if (dstate == DOOR_UNLOCKING) {
                        gp->door.startTick = now - (Uint32)dElapsed;
                        gp->door.pausedFor = dpaused ? now : 0;
                    } else {
                        gp->door.startTick   = 0;
                        gp->door.pausedFor   = 0;
                        gp->namgoong.working = 0;
                    }
                    gp->door.message[0]       = '\0';
                    gp->door.messageStartTick = 0;
                }
                continue;
            }
        }
        {
            int   idx, jhp, jdir, jact;
            float jx, jy;
            if (sscanf(line, "joueur:%d:%f:%f:%d:%d:%d",
                       &idx, &jx, &jy, &jhp, &jdir, &jact) == 6) {
                if (idx >= 0 && idx < NB_JOUEURS_MAX) {
                    Joueur *j       = &bg->joueurs[idx];
                    j->x            = jx;
                    j->y            = jy;
                    j->hp           = jhp;
                    j->direction    = jdir;
                    j->actif        = jact;
                    j->velX         = 0.0f;
                    j->velY         = 0.0f;
                    j->auSol        = 1;
                    j->w            = JOUEUR_W;
                    j->h            = JOUEUR_H;
                    j->etat         = PS_IDLE;
                    j->lastShootTick = 0;
                    j->lastMeleeTick = 0;
                    j->muzzleFrames  = 0;
                    j->pendingMeleeImpact = 0;
                    j->meleeImpactDir     = jdir != 0 ? jdir : 1;
                    animation_init(&j->anim, NULL,
                                   PLAYER_COLS, PLAYER_ROWS,
                                   ANIM_P_IDLE, PLAYER_NFRAMES[ANIM_P_IDLE],
                                   PLAYER_ANIM_FPS[ANIM_P_IDLE], 1);
                    j->wasGrounded = 1;
                    if (s_char_anim_renderer &&
                        (!j->ca.loaded ||
                         j->ca.skin   != player_cfg[idx].skin ||
                         j->ca.weapon != player_cfg[idx].weapon)) {
                        char_anim_load(&j->ca, s_char_anim_renderer,
                                       player_cfg[idx].skin,
                                       player_cfg[idx].weapon);
                    }
                    if (j->ca.loaded) {
                        j->ca.is_locked = 0;
                        char_anim_set(&j->ca,
                            jhp <= 0 ? ANIM_DEATH : ANIM_IDLE);
                    }
                }
                continue;
            }

            int   etype, ehp, ewagon, eact, eetat;
            float ex, ey;
            if (sscanf(line, "ennemi:%d:%f:%f:%d:%d:%d:%d",
                       &etype, &ex, &ey, &ehp, &ewagon, &eact, &eetat) == 7) {
                if (nbEnnemisLus < MAX_ENNEMIS) {
                    Ennemi *e           = &bg->ennemis[nbEnnemisLus];
                    e->type             = (TypeEnnemi)etype;
                    e->x                = ex;
                    e->y                = ey;
                    e->hp               = ehp;
                    e->wagon            = ewagon;
                    e->actif            = eact;
                    e->etat             = (EtatEnnemi)eetat;
                    e->velY             = 0.0f;
                    e->auSol            = 1;
                    e->direction        = -1;
                    e->hitFlashMs       = 0;
                    e->lastAttackTick   = 0;
                    e->attackCooldownMs = (e->type == ET_ENFORCER) ? 2000u :
                                         (e->type == ET_GUARD)    ? 1400u : 1100u;

                    int arow, anf;
                    if (e->etat == ES_DEAD) {
                        arow = ANIM_E_DEATH;
                        anf  = (e->type == ET_ENFORCER) ? ENFORCER_NFRAMES[2] :
                               (e->type == ET_GUARD)    ? GUARD_NFRAMES[2]    :
                                                          WORKER_NFRAMES[2];
                    } else {
                        arow = ANIM_E_WALK;
                        anf  = (e->type == ET_ENFORCER) ? ENFORCER_NFRAMES[0] :
                               (e->type == ET_GUARD)    ? GUARD_NFRAMES[0]    :
                                                          WORKER_NFRAMES[0];
                    }
                    animation_init(&e->anim, bg->enemySheets[e->type],
                                   6, 3, arow, anf,
                                   ENEMY_ANIM_FPS[arow],
                                   (e->etat != ES_DEAD) ? 1 : 0);
                    nbEnnemisLus++;
                }
            }
        }
    }

    fclose(fp);

    bg->nbEnnemis = nbEnnemisLus;
    *tempsDepart  = SDL_GetTicks() - elapsed_ms;

    if (gp) {
        /* leave wagonInitialized[wagonActif] = 0 so gameplay_init_wagon()
           re-spawns this wagon's hazards, item pickups and ranged enemies
           on the next frame (the save file only stores enemies, not items) */
        gp->levelLoaded = 1;

        if (!doorLineSeen && gp->namgoong.working) {
            gp->door.state            = DOOR_OPEN;
            gp->door.actif            = 0;
            gp->door.paused           = 0;
            gp->door.startTick        = 0;
            gp->door.pausedFor        = 0;
            gp->door.targetWagon      = bg->wagonActif + 1;
            gp->door.message[0]       = '\0';
            gp->door.messageStartTick = 0;
            gp->namgoong.working      = 0;
        }
    }
    return 1;
}

void wagons_liberer(Background *bg)
{ for(int i=0;i<bg->nbWagons;i++) if(bg->wagons[i].texture) SDL_DestroyTexture(bg->wagons[i].texture); }

void background_liberer(Background *bg)
{
    if(bg->imagebackground)      SDL_DestroyTexture(bg->imagebackground);
    if(bg->imagebackground2)     SDL_DestroyTexture(bg->imagebackground2);
    if(bg->son)                  Mix_FreeMusic(bg->son);
    if(bg->fenetreguide.texture) SDL_DestroyTexture(bg->fenetreguide.texture);
    bg->imagebackground=bg->imagebackground2=NULL;
    bg->son=NULL; bg->fenetreguide.texture=NULL;
    for (int i = 0; i < MAX_WAGONS; i++) {
        if (bg->stylesWagons[i].floorTex)    SDL_DestroyTexture(bg->stylesWagons[i].floorTex);
        if (bg->stylesWagons[i].obstacleTex) SDL_DestroyTexture(bg->stylesWagons[i].obstacleTex);
        bg->stylesWagons[i].floorTex = bg->stylesWagons[i].obstacleTex = NULL;
        if (bg->wagonBgTex[i]) SDL_DestroyTexture(bg->wagonBgTex[i]);
        bg->wagonBgTex[i] = NULL;
    }
    if (bg->outsideBgTex) SDL_DestroyTexture(bg->outsideBgTex);

    for (int i = 0; i < 3; i++) {
        if (bg->enemySheets[i]) SDL_DestroyTexture(bg->enemySheets[i]);
    }
    if (bg->bulletTex)  SDL_DestroyTexture(bg->bulletTex);
    if (bg->muzzleTex)  SDL_DestroyTexture(bg->muzzleTex);
    if (bg->hitFxSheet) SDL_DestroyTexture(bg->hitFxSheet);
    wagons_liberer(bg);
    destructibles_liberer(bg);
}
