

#ifndef ROTOZOOM_H
#define ROTOZOOM_H

#include <SDL2/SDL.h>
#include <math.h>

typedef struct {
    float duration_ms;
    float start_scale;
    float end_scale;
    float start_angle;
    float end_angle;
    float overshoot_scale;
    float overshoot_at;
    float shake_amplitude;
    float shake_duration_ms;
} RotozoomAnimConfig;

typedef struct {

    RotozoomAnimConfig cfg;

    int         active;
    Uint32      start_time_ms;
    float       current_scale;
    float       current_angle;
    float       current_offset_x;
    float       current_offset_y;
    int         phase;
} RotozoomAnim;

static const RotozoomAnimConfig ROTOZOOM_PRESET_SUCCESS = {
    .duration_ms      = 900.0f,
    .start_scale      = 0.5f,
    .end_scale        = 1.2f,
    .start_angle      = -360.0f,
    .end_angle        = 0.0f,
    .overshoot_scale  = 1.15f,
    .overshoot_at     = 0.75f,
    .shake_amplitude  = 0.0f,
    .shake_duration_ms = 0.0f
};

static const RotozoomAnimConfig ROTOZOOM_PRESET_FAIL = {
    .duration_ms      = 800.0f,
    .start_scale      = 0.6f,
    .end_scale        = 1.2f,
    .start_angle      = 360.0f,
    .end_angle        = 0.0f,
    .overshoot_scale  = 0.0f,
    .overshoot_at     = 0.0f,
    .shake_amplitude  = 6.0f,
    .shake_duration_ms = 300.0f
};

static inline float rz_ease_cubic_out(float t)
{
    return 1.0f - powf(1.0f - t, 3.0f);
}

static inline float rz_ease_cubic_in_out(float t)
{
    return (t < 0.5f)
        ? 4.0f * t * t * t
        : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

static inline int rz_anim_init(RotozoomAnim *anim, const RotozoomAnimConfig *config)
{
    if (!anim || !config) return -1;

    anim->cfg             = *config;
    anim->active          = 1;
    anim->start_time_ms   = SDL_GetTicks();
    anim->current_scale   = config->start_scale;
    anim->current_angle   = config->start_angle;
    anim->current_offset_x = 0.0f;
    anim->current_offset_y = 0.0f;
    anim->phase           = 0;

    return 0;
}

static inline void rz_anim_trigger_success(RotozoomAnim *anim)
{
    rz_anim_init(anim, &ROTOZOOM_PRESET_SUCCESS);
}

static inline void rz_anim_trigger_fail(RotozoomAnim *anim)
{
    rz_anim_init(anim, &ROTOZOOM_PRESET_FAIL);
}

static inline int rz_anim_update(RotozoomAnim *anim, Uint32 now_ms)
{
    if (!anim || !anim->active) return 0;

    if (anim->phase == 0) {
        float elapsed = (float)(now_ms - anim->start_time_ms);
        float duration = anim->cfg.duration_ms;

        if (elapsed >= duration) {

            anim->current_scale = anim->cfg.end_scale;
            anim->current_angle = anim->cfg.end_angle;
            anim->current_offset_x = 0.0f;
            anim->current_offset_y = 0.0f;

            if (anim->cfg.shake_duration_ms > 0.0f && anim->cfg.shake_amplitude > 0.0f) {
                anim->phase = 1;
                anim->start_time_ms = now_ms;
                return 1;
            }

            anim->phase = 2;
            anim->active = 0;
            return 0;
        }

        float t = elapsed / duration;
        float eased_t = rz_ease_cubic_out(t);

        float base_scale;
        if (anim->cfg.overshoot_scale > 0.0f && anim->cfg.overshoot_at > 0.0f) {

            float overshoot_t = anim->cfg.overshoot_at;
            if (t <= overshoot_t) {

                float local_t = t / overshoot_t;
                local_t = rz_ease_cubic_out(local_t);
                base_scale = anim->cfg.start_scale
                    + (anim->cfg.overshoot_scale - anim->cfg.start_scale) * local_t;
            } else {

                float local_t = (t - overshoot_t) / (1.0f - overshoot_t);
                local_t = rz_ease_cubic_out(local_t);
                base_scale = anim->cfg.overshoot_scale
                    + (anim->cfg.end_scale - anim->cfg.overshoot_scale) * local_t;
            }
        } else {

            base_scale = anim->cfg.start_scale
                + (anim->cfg.end_scale - anim->cfg.start_scale) * eased_t;
        }

        float base_angle = anim->cfg.start_angle
            + (anim->cfg.end_angle - anim->cfg.start_angle) * eased_t;

        anim->current_scale = base_scale;
        anim->current_angle = base_angle;
        anim->current_offset_x = 0.0f;
        anim->current_offset_y = 0.0f;

        return 1;
    }

    if (anim->phase == 1) {
        float elapsed = (float)(now_ms - anim->start_time_ms);
        float shake_dur = anim->cfg.shake_duration_ms;
        float amplitude = anim->cfg.shake_amplitude;

        if (elapsed >= shake_dur) {
            anim->current_offset_x = 0.0f;
            anim->current_offset_y = 0.0f;
            anim->current_scale = anim->cfg.end_scale;
            anim->current_angle = anim->cfg.end_angle;
            anim->phase = 2;
            anim->active = 0;
            return 0;
        }

        float t = elapsed / shake_dur;

        float decay = 1.0f - t;
        float freq = 30.0f;
        float omega = 2.0f * M_PI * freq * (elapsed / 1000.0f);

        anim->current_offset_x = sinf(omega) * amplitude * decay;
        anim->current_offset_y = cosf(omega * 1.3f) * amplitude * 0.5f * decay;

        return 1;
    }

    anim->active = 0;
    return 0;
}

static inline void rz_anim_render(
    SDL_Renderer *renderer,
    SDL_Texture  *texture,
    float         center_x,
    float         center_y,
    int           orig_w,
    int           orig_h,
    RotozoomAnim *anim)
{
    if (!renderer || !texture || !anim) return;

    float scale = anim->current_scale;
    float angle = anim->current_angle;
    float ox = anim->current_offset_x;
    float oy = anim->current_offset_y;

    int scaled_w = (int)(orig_w * scale);
    int scaled_h = (int)(orig_h * scale);

    SDL_Rect dst = {
        .x = (int)(center_x - scaled_w / 2 + ox),
        .y = (int)(center_y - scaled_h / 2 + oy),
        .w = scaled_w,
        .h = scaled_h
    };

    SDL_Point center = { orig_w / 2, orig_h / 2 };

    SDL_RenderCopyEx(renderer, texture, NULL, &dst,
                     (double)angle, &center, SDL_FLIP_NONE);
}

static inline int rz_anim_is_active(RotozoomAnim *anim)
{
    return anim && anim->active;
}

static inline void rz_anim_reset(RotozoomAnim *anim)
{
    if (anim) {
        anim->active = 0;
        anim->phase = 2;
        anim->current_scale = 1.0f;
        anim->current_angle = 0.0f;
        anim->current_offset_x = 0.0f;
        anim->current_offset_y = 0.0f;
    }
}

#endif
