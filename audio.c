#include "audio.h"
#include <stdio.h>
#include <string.h>

#define AUDIO_AMBIENCE_CHANNEL 6

#define SND_DIR "assets/sound"

static Mix_Chunk *try_load_wav(const char *path)
{
    Mix_Chunk *c = Mix_LoadWAV(path);
    if (!c) fprintf(stderr, "[audio] Mix_LoadWAV '%s': %s\n", path, Mix_GetError());
    return c;
}

static Mix_Music *try_load_mus(const char *path)
{
    Mix_Music *m = Mix_LoadMUS(path);
    if (!m) fprintf(stderr, "[audio] Mix_LoadMUS '%s': %s\n", path, Mix_GetError());
    return m;
}

int audio_init(AudioSystem *a)
{
    memset(a, 0, sizeof(*a));
    a->ambienceChannel = -1;
    a->masterVolume    = MIX_MAX_VOLUME;
    a->musicVolume     = MIX_MAX_VOLUME / 2;
    a->sfxVolume       = MIX_MAX_VOLUME;
    a->current         = AUDIO_TRACK_NONE;

    a->gameMusic = try_load_mus(SND_DIR "/game music.ogg");
    if (!a->gameMusic) a->gameMusic = try_load_mus(SND_DIR "/game music.wav");
    a->bossMusic = try_load_mus(SND_DIR "/boss music.ogg");
    a->menuMusic = try_load_mus(SND_DIR "/Snowpiercer Main Theme.ogg");
    if (!a->menuMusic) a->menuMusic = try_load_mus(SND_DIR "/game music.wav");

    a->sfx[SFX_BUTTON_CLICK]  = try_load_wav(SND_DIR "/button click.wav");
    a->sfx[SFX_HIT_IMPACT]    = try_load_wav(SND_DIR "/enemy hit.wav");
    a->sfx[SFX_ENEMY_DEATH]   = try_load_wav(SND_DIR "/enemy death.wav");

    a->sfx[SFX_ATTACK]        = a->sfx[SFX_HIT_IMPACT];
    a->sfx[SFX_PLAYER_HURT]   = a->sfx[SFX_HIT_IMPACT];
    a->sfx[SFX_JUMP]          = a->sfx[SFX_BUTTON_CLICK];
    a->sfx[SFX_QUIZ_CORRECT]  = a->sfx[SFX_BUTTON_CLICK];
    a->sfx[SFX_QUIZ_WRONG]    = a->sfx[SFX_ENEMY_DEATH];

    a->ambience = try_load_wav(SND_DIR "/train sound background.wav");
    if (!a->ambience)
        a->ambience = try_load_wav(SND_DIR "/background ambient music.wav");

    Mix_AllocateChannels(16);
    audio_set_master_volume(a, a->masterVolume);
    return 0;
}

void audio_free(AudioSystem *a)
{
    audio_stop_ambience(a);
    audio_stop_music(a);

    if (a->menuMusic) Mix_FreeMusic(a->menuMusic);
    if (a->gameMusic) Mix_FreeMusic(a->gameMusic);
    if (a->bossMusic) Mix_FreeMusic(a->bossMusic);

    Mix_Chunk *seen[SFX_COUNT + 1]; int nseen = 0;
    for (int i = 0; i < SFX_COUNT; i++) {
        Mix_Chunk *c = a->sfx[i];
        if (!c) continue;
        int dup = 0;
        for (int j = 0; j < nseen; j++) if (seen[j] == c) { dup = 1; break; }
        if (!dup) { seen[nseen++] = c; Mix_FreeChunk(c); }
    }
    if (a->ambience) {
        int dup = 0;
        for (int j = 0; j < nseen; j++) if (seen[j] == a->ambience) { dup = 1; break; }
        if (!dup) Mix_FreeChunk(a->ambience);
    }

    memset(a, 0, sizeof(*a));
}

static int scaled(int local, int master)
{
    int v = (local * master) / MIX_MAX_VOLUME;
    if (v < 0) v = 0;
    if (v > MIX_MAX_VOLUME) v = MIX_MAX_VOLUME;
    return v;
}

void audio_set_master_volume(AudioSystem *a, int v)
{
    if (v < 0) v = 0;
    if (v > MIX_MAX_VOLUME) v = MIX_MAX_VOLUME;
    a->masterVolume = v;
    Mix_VolumeMusic(scaled(a->musicVolume, a->masterVolume));
    Mix_Volume(-1, scaled(a->sfxVolume, a->masterVolume));
}

void audio_set_music_volume(AudioSystem *a, int v)
{
    if (v < 0) v = 0;
    if (v > MIX_MAX_VOLUME) v = MIX_MAX_VOLUME;
    a->musicVolume = v;
    Mix_VolumeMusic(scaled(a->musicVolume, a->masterVolume));
}

void audio_set_sfx_volume(AudioSystem *a, int v)
{
    if (v < 0) v = 0;
    if (v > MIX_MAX_VOLUME) v = MIX_MAX_VOLUME;
    a->sfxVolume = v;
    Mix_Volume(-1, scaled(a->sfxVolume, a->masterVolume));
}

void audio_play_track(AudioSystem *a, AudioTrack t)
{
    if (t == a->current && Mix_PlayingMusic()) return;

    Mix_Music *m = NULL;
    switch (t) {
        case AUDIO_TRACK_MENU:      m = a->menuMusic; break;
        case AUDIO_TRACK_INTERLUDE: m = a->menuMusic; break;
        case AUDIO_TRACK_GAME:      m = a->gameMusic; break;
        case AUDIO_TRACK_BOSS:      m = a->bossMusic ? a->bossMusic : a->gameMusic; break;
        case AUDIO_TRACK_NONE:
        default:                    m = NULL;         break;
    }
    if (!m) {
        Mix_HaltMusic();
        a->current = AUDIO_TRACK_NONE;
        return;
    }
    Mix_PlayMusic(m, -1);
    a->current = t;
}

void audio_stop_music(AudioSystem *a)
{
    Mix_HaltMusic();
    a->current = AUDIO_TRACK_NONE;
}

void audio_play_sfx(AudioSystem *a, SfxId id)
{
    if (id < 0 || id >= SFX_COUNT) return;
    Mix_Chunk *c = a->sfx[id];
    if (!c) return;
    Mix_PlayChannel(-1, c, 0);
}

void audio_start_ambience(AudioSystem *a)
{
    if (!a->ambience || a->ambienceChannel >= 0) return;

    Mix_Volume(AUDIO_AMBIENCE_CHANNEL,
               scaled(a->sfxVolume / 2, a->masterVolume));
    a->ambienceChannel = Mix_PlayChannel(AUDIO_AMBIENCE_CHANNEL, a->ambience, -1);
}

void audio_stop_ambience(AudioSystem *a)
{
    if (a->ambienceChannel >= 0) {
        Mix_HaltChannel(a->ambienceChannel);
        a->ambienceChannel = -1;
    }
}
