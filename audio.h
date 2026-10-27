#ifndef AUDIO_H
#define AUDIO_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

typedef enum {
    AUDIO_TRACK_NONE = 0,
    AUDIO_TRACK_MENU,
    AUDIO_TRACK_GAME,
    AUDIO_TRACK_INTERLUDE,
    AUDIO_TRACK_BOSS
} AudioTrack;

typedef enum {
    SFX_BUTTON_CLICK = 0,
    SFX_JUMP,
    SFX_ATTACK,
    SFX_HIT_IMPACT,
    SFX_ENEMY_DEATH,
    SFX_PLAYER_HURT,
    SFX_QUIZ_CORRECT,
    SFX_QUIZ_WRONG,
    SFX_COUNT
} SfxId;

typedef struct {
    Mix_Music   *menuMusic;
    Mix_Music   *gameMusic;
    Mix_Music   *bossMusic;
    Mix_Chunk   *sfx[SFX_COUNT];
    Mix_Chunk   *ambience;
    int          ambienceChannel;

    int          masterVolume;
    int          musicVolume;
    int          sfxVolume;

    AudioTrack   current;
} AudioSystem;

int  audio_init(AudioSystem *a);
void audio_free(AudioSystem *a);

void audio_set_master_volume(AudioSystem *a, int v);
void audio_set_music_volume(AudioSystem *a,  int v);
void audio_set_sfx_volume(AudioSystem *a,    int v);

void audio_play_track(AudioSystem *a, AudioTrack t);
void audio_stop_music(AudioSystem *a);

void audio_play_sfx(AudioSystem *a, SfxId id);

void audio_start_ambience(AudioSystem *a);
void audio_stop_ambience(AudioSystem *a);

#endif
