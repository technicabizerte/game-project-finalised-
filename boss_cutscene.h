#ifndef BOSS_CUTSCENE_H
#define BOSS_CUTSCENE_H

#include <SDL2/SDL.h>
#include <stdbool.h>

#include "background.h"
#include "gameplay.h"
#include "ui.h"
#include "audio.h"
#include "dialogue_cinematic.h"

/* boss_cutscene — single-shot reveal that plays the first time the player
 * crosses into wagon 5, BEFORE boss_intro takes over.  Renders a black
 * screen with a 7x7 sprite-sheet animation (49 frames at 10 fps), drops
 * the existing vignette on top, fires a screen shake on frame 0, and at
 * frame 28 reuses dialogue_cinematic for the boss's one-liner.  Hands off
 * to boss_intro_start() once frame 48 is reached and the dialogue has
 * been dismissed (or the player hits Space/Enter to skip).
 *
 * One-shot: a static `played` flag persists across deaths; only a fresh
 * process re-arms the cutscene.  Same lifetime as boss_intro's flag. */

void boss_cutscene_init(SDL_Renderer        *renderer,
                        UIFontPack          *fonts,
                        UITextCache         *cache,
                        Background          *bg,
                        GameplayState       *gp,
                        AudioSystem         *audio,
                        DialogueCinematic   *cin);

bool boss_cutscene_should_start(void);
void boss_cutscene_start(void);

bool boss_cutscene_active(void);
bool boss_cutscene_freezes_world(void);

void boss_cutscene_update(float dt);
void boss_cutscene_render(void);

/* Returns true iff the key was consumed (Space/Enter while active). */
bool boss_cutscene_handle_skip_key(SDL_Keycode k);

#endif
