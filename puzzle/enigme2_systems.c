

#include "enigme2.h"

typedef struct {
    int         active;
    SDL_Texture *piece_tex;
    int          drag_x;
    int          drag_y;
    int          src_card;
    int          piece_w;
    int          piece_h;
} DragState;

static DragState g_drag = {0};

static SDL_Rect g_card_rects[3] = {
    {CARD_X, CARD_1_Y, CARD_W, CARD_H},
    {CARD_X, CARD_2_Y, CARD_W, CARD_H},
    {CARD_X, CARD_3_Y, CARD_W, CARD_H}
};

DragResult handle_mousedown(int mx, int my)
{
    for (int i = 0; i < 3; i++) {
        SDL_Point pt = {mx, my};
        if (SDL_PointInRect(&pt, &g_card_rects[i])) {
            extern SDL_Texture *g_card_textures[3];

            g_drag.active     = 1;
            g_drag.piece_tex  = g_card_textures[i];
            g_drag.src_card   = i;
            g_drag.drag_x     = mx;
            g_drag.drag_y     = my;

            if (g_drag.piece_tex) {
                SDL_QueryTexture(g_drag.piece_tex, NULL, NULL,
                                 &g_drag.piece_w, &g_drag.piece_h);
            }
            return DRAG_NONE;
        }
    }
    return DRAG_NONE;
}

void handle_mousemotion(int mx, int my)
{
    if (!g_drag.active) return;
    g_drag.drag_x = mx;
    g_drag.drag_y = my;
}

extern int verifier_position_piece(t_piece *p, int cible_x, int cible_y);

DragResult handle_mouseup(int mx, int my)
{
    if (!g_drag.active) return DRAG_NONE;

    SDL_Point pt = {mx, my};
    SDL_Rect target_rect = {TARGET_X, TARGET_Y, TARGET_SIZE, TARGET_SIZE};

    extern int g_dragged_prop_index;
    g_dragged_prop_index = g_drag.src_card;

    DragResult result = DRAG_CANCEL;

    if (SDL_PointInRect(&pt, &target_rect)) {

        result = DRAG_FAIL;
    }

    g_drag.active    = 0;
    g_drag.piece_tex = NULL;
    g_drag.src_card  = -1;

    return result;
}

void render_drag_ghost(SDL_Renderer *renderer)
{
    if (!g_drag.active || !g_drag.piece_tex) return;

    SDL_Rect dst = {
        g_drag.drag_x - g_drag.piece_w / 2,
        g_drag.drag_y - g_drag.piece_h / 2,
        g_drag.piece_w,
        g_drag.piece_h
    };

    SDL_SetTextureAlphaMod(g_drag.piece_tex, 180);
    SDL_RenderCopy(renderer, g_drag.piece_tex, NULL, &dst);
    SDL_SetTextureAlphaMod(g_drag.piece_tex, 255);
}

void reset_drag_state(void)
{
    g_drag.active    = 0;
    g_drag.piece_tex = NULL;
    g_drag.src_card  = -1;
}

int is_dragging(void)
{
    return g_drag.active ? 1 : 0;
}

#define OVERLAY_DURATION_MS 2200

typedef struct {
    OverlayResult type;
    Uint32        show_at_ms;
    int           visible;
} OverlayState;

static OverlayState g_overlay = {0};

void trigger_overlay(OverlayResult result)
{
    g_overlay.type       = result;
    g_overlay.show_at_ms = SDL_GetTicks();
    g_overlay.visible    = 1;
}

void render_overlay(SDL_Renderer *renderer,
                    const SDL_Rect *center_rect,
                    TTF_Font *font_large,
                    TTF_Font *font_small)
{
    if (!g_overlay.visible) return;

    if ((SDL_GetTicks() - g_overlay.show_at_ms) > OVERLAY_DURATION_MS) {
        g_overlay.visible = 0;
        return;
    }

    int is_success = (g_overlay.type == OVERLAY_SUCCESS);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    if (is_success) {
        SDL_SetRenderDrawColor(renderer, 0, 40, 30, 210);
    } else {
        SDL_SetRenderDrawColor(renderer, 40, 0, 0, 210);
    }
    SDL_RenderFillRect(renderer, center_rect);

    if (is_success) {
        SDL_SetRenderDrawColor(renderer, 0, 255, 180, 255);
    } else {
        SDL_SetRenderDrawColor(renderer, 255, 60, 60, 255);
    }
    SDL_RenderDrawRect(renderer, center_rect);

    if (font_large) {
        const char *main_text = is_success ? "ACCESS GRANTED" : "SYSTEM COMPROMISED";
        SDL_Color text_color  = is_success
            ? (SDL_Color){0, 255, 180, 255}
            : (SDL_Color){255, 80, 80, 255};

        SDL_Surface *surf = TTF_RenderText_Blended(font_large, main_text, text_color);
        if (surf) {
            SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
            if (tex) {
                SDL_Rect dst = {
                    center_rect->x + (center_rect->w - surf->w) / 2,
                    center_rect->y + center_rect->h / 2 - surf->h - 10,
                    surf->w, surf->h
                };
                SDL_RenderCopy(renderer, tex, NULL, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surf);
        }
    }

    if (font_small) {
        const char *sub_text = is_success
            ? "> AUTHENTICATION SEQUENCE COMPLETE"
            : "> INTRUSION DETECTED -- LOCKDOWN INITIATED";
        SDL_Color sub_color = {180, 180, 180, 200};

        SDL_Surface *sub_surf = TTF_RenderText_Blended(font_small, sub_text, sub_color);
        if (sub_surf) {
            SDL_Texture *sub_tex = SDL_CreateTextureFromSurface(renderer, sub_surf);
            if (sub_tex) {
                SDL_Rect dst = {
                    center_rect->x + (center_rect->w - sub_surf->w) / 2,
                    center_rect->y + center_rect->h / 2 + 15,
                    sub_surf->w, sub_surf->h
                };
                SDL_RenderCopy(renderer, sub_tex, NULL, &dst);
                SDL_DestroyTexture(sub_tex);
            }
            SDL_FreeSurface(sub_surf);
        }
    }
}

int wrap_text(TTF_Font *font, const char *text, int max_width, WrappedText *out)
{
    if (!font || !text || !out) return -1;

    out->line_count = 0;
    memset(out->lines, 0, sizeof(out->lines));

    char buf[4096];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char current_line[LORE_MAX_CHARS] = {0};
    char test_line[LORE_MAX_CHARS]    = {0};
    int  line_w = 0;

    char *token = strtok(buf, " ");
    while (token != NULL) {
        char *nl = strchr(token, '\n');
        if (nl) {
            *nl = '\0';

            if (*token != '\0') {
                if (strlen(current_line) > 0) {
                    snprintf(test_line, sizeof(test_line), "%s %s", current_line, token);
                } else {
                    snprintf(test_line, sizeof(test_line), "%s", token);
                }
                TTF_SizeUTF8(font, test_line, &line_w, NULL);
                if (line_w > max_width && strlen(current_line) > 0) {
                    strncpy(out->lines[out->line_count++], current_line, LORE_MAX_CHARS - 1);
                    snprintf(current_line, sizeof(current_line), "%s", token);
                } else {
                    strncpy(current_line, test_line, LORE_MAX_CHARS - 1);
                }
            }

            strncpy(out->lines[out->line_count++], current_line, LORE_MAX_CHARS - 1);
            current_line[0] = '\0';

            token = nl + 1;
            if (*token == '\0') {
                token = strtok(NULL, " ");
                continue;
            }
        }

        if (strlen(current_line) > 0) {
            snprintf(test_line, sizeof(test_line), "%s %s", current_line, token);
        } else {
            snprintf(test_line, sizeof(test_line), "%s", token);
        }

        TTF_SizeUTF8(font, test_line, &line_w, NULL);

        if (line_w > max_width) {
            if (strlen(current_line) > 0) {
                strncpy(out->lines[out->line_count++], current_line, LORE_MAX_CHARS - 1);
            }
            strncpy(current_line, token, LORE_MAX_CHARS - 1);
        } else {
            strncpy(current_line, test_line, LORE_MAX_CHARS - 1);
        }

        if (out->line_count >= LORE_MAX_LINES - 1) break;
        token = strtok(NULL, " ");
    }

    if (strlen(current_line) > 0) {
        strncpy(out->lines[out->line_count++], current_line, LORE_MAX_CHARS - 1);
    }

    return out->line_count;
}

void render_lore_panel(SDL_Renderer   *renderer,
                       TTF_Font       *font,
                       TTF_Font       *title_font,
                       const char     *title,
                       const char     *body,
                       const SDL_Rect *panel,
                       SDL_Color       text_color,
                       SDL_Color       title_color)
{
    const int PADDING      = 12;
    const int LINE_SPACING = 3;

    if (!font || !title_font) return;

    int font_h  = TTF_FontLineSkip(font);
    int title_h = TTF_FontLineSkip(title_font);

    int text_x     = panel->x + PADDING;
    int text_width = panel->w - PADDING * 2;
    int cursor_y   = panel->y + PADDING;

    if (title && *title) {
        SDL_Surface *ts = TTF_RenderText_Blended(title_font, title, title_color);
        if (ts) {
            SDL_Texture *tt = SDL_CreateTextureFromSurface(renderer, ts);
            if (tt) {
                SDL_Rect dst = {text_x, cursor_y, ts->w, ts->h};
                SDL_RenderCopy(renderer, tt, NULL, &dst);
                SDL_DestroyTexture(tt);
            }
            SDL_FreeSurface(ts);
            cursor_y += title_h + 6;
        }
    }

    SDL_SetRenderDrawColor(renderer, text_color.r, text_color.g, text_color.b, 80);
    SDL_RenderDrawLine(renderer, text_x, cursor_y, text_x + text_width, cursor_y);
    cursor_y += 10;

    if (!body || !*body) return;

    WrappedText wt;
    int n = wrap_text(font, body, text_width, &wt);
    if (n < 0) return;

    for (int i = 0; i < n; i++) {
        if (cursor_y + font_h > panel->y + panel->h - PADDING) break;

        const char *line = wt.lines[i];
        if (!line || *line == '\0') {
            cursor_y += font_h / 2;
            continue;
        }

        SDL_Surface *ls = TTF_RenderText_Blended(font, line, text_color);
        if (ls) {
            SDL_Texture *lt = SDL_CreateTextureFromSurface(renderer, ls);
            if (lt) {
                SDL_Rect dst = {text_x, cursor_y, ls->w, ls->h};
                SDL_RenderCopy(renderer, lt, NULL, &dst);
                SDL_DestroyTexture(lt);
            }
            SDL_FreeSurface(ls);
        }
        cursor_y += font_h + LINE_SPACING;
    }
}

const MissionLore g_missions[5] = {
    {
        "MISSION: AUXILIARY POWER",
        "PRIORITY ALERT -- TAIL SECTION\n\n"
        "The auxiliary server nodes in Car 14 have gone dark. "
        "Without power restoration, the train's security grid "
        "will trigger a full-car lockout in 04:00 minutes.\n\n"
        "A replacement relay board has been located in the "
        "maintenance locker. Match the correct power module to "
        "the server rack input before the countdown expires.\n\n"
        "CAUTION: incorrect insertion will trip the breaker. "
        "You will have ONE attempt."
    },
    {
        "MISSION: GUARD RELAY",
        "INTERCEPT DIRECTIVE -- CAR 9\n\n"
        "A Wilford Security guard carries an active comms badge "
        "cycling through three authorization frequencies. "
        "Your window to clone the signal is 30 seconds per cycle.\n\n"
        "Identify the correct frequency badge from the captured "
        "specimens and insert it into the relay scanner before "
        "the guard completes his round.\n\n"
        "NOTE: Wilford's system logs badge reads. "
        "A wrong read triggers an immediate patrol response."
    },
    {
        "MISSION: CARGO INSPECTION",
        "RANDOM CHECK -- CAR 12\n\n"
        "An empty cargo wagon has been flagged for inspection. "
        "Wilford Security suspects unauthorized cargo transport.\n\n"
        "Three manifest seals are available. Only one matches "
        "the correct inspection protocol for this wagon type.\n\n"
        "Apply the correct seal before the inspection window closes. "
        "A failed inspection means contraband charges.\n\n"
        "Time remaining: Standard inspection protocol."
    },
    {
        "MISSION: AQUARIUM MAINTENANCE",
        "LIFE SUPPORT -- CAR 8\n\n"
        "The protein farm aquarium filtration system requires "
        "immediate recalibration. Water quality sensors detect "
        "a 2.3% pH imbalance.\n\n"
        "Three filter modules are available from maintenance. "
        "Only one has the correct flow rate specification.\n\n"
        "Install the correct module before the fish population "
        "enters critical stress. The tail section depends on "
        "this protein source.\n\n"
        "CAUTION: System lockdown in progress."
    },
    {
        "MISSION: COMMAND ACCESS",
        "ENDGAME -- FRONT CAR\n\n"
        "We are 200 meters from the engine room.\n\n"
        "Melanie Cavill's emergency override requires a "
        "three-part command key. Two segments are already "
        "in our possession. The third is secured behind "
        "the navigation terminal.\n\n"
        "Match the final key component to its terminal port. "
        "Once inserted, the resistance assumes full navigational "
        "control of Snowpiercer.\n\n"
        "1001 cars. 7 years. This is the moment.\n"
        "Do not hesitate."
    },
};

void render_rotozoom_overlay(SDL_Renderer *renderer,
                              SDL_Texture *success_tex,
                              SDL_Texture *fail_tex,
                              RotozoomAnim *success_anim,
                              RotozoomAnim *fail_anim,
                              TTF_Font *font_large,
                              TTF_Font *font_small)
{
    int win_w, win_h;
    SDL_RenderGetLogicalSize(renderer, &win_w, &win_h);
    if (win_w == 0 || win_h == 0) SDL_GetWindowSize(g_win, &win_w, &win_h);

    int show_success = rz_anim_is_active(success_anim);
    int show_fail    = rz_anim_is_active(fail_anim);

    if (!show_success && !show_fail) return;

    RotozoomAnim *active_anim = show_success ? success_anim : fail_anim;
    int is_success = show_success;
    SDL_Texture *icon_tex = is_success ? success_tex : fail_tex;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    if (is_success) {
        SDL_SetRenderDrawColor(renderer, 0, 40, 30, 180);
    } else {
        SDL_SetRenderDrawColor(renderer, 40, 0, 0, 180);
    }
    SDL_Rect bg_rect = {0, 0, win_w, win_h};
    SDL_RenderFillRect(renderer, &bg_rect);

    if (icon_tex) {
        int tex_w, tex_h;
        SDL_QueryTexture(icon_tex, NULL, NULL, &tex_w, &tex_h);

        float base_scale = 0.6f;
        int base_w = (int)(tex_w * base_scale);
        int base_h = (int)(tex_h * base_scale);

        float cx = win_w / 2.0f;
        float cy = win_h / 2.0f - 30;

        float scale = active_anim->current_scale;
        float angle = active_anim->current_angle;
        float ox = active_anim->current_offset_x;
        float oy = active_anim->current_offset_y;

        int final_w = (int)(base_w * scale);
        int final_h = (int)(base_h * scale);

        SDL_Rect dst = {
            .x = (int)(cx - final_w / 2 + ox),
            .y = (int)(cy - final_h / 2 + oy),
            .w = final_w,
            .h = final_h
        };

        SDL_Point center = { base_w / 2, base_h / 2 };

        SDL_RenderCopyEx(renderer, icon_tex, NULL, &dst,
                         (double)angle, &center, SDL_FLIP_NONE);
    }

    if (font_large) {
        const char *main_text = is_success
            ? "ACCESS GRANTED"
            : "SYSTEM COMPROMISED";
        SDL_Color text_color = is_success
            ? (SDL_Color){0, 255, 180, 255}
            : (SDL_Color){255, 80, 80, 255};

        SDL_Surface *surf = TTF_RenderText_Blended(font_large, main_text, text_color);
        if (surf) {
            SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
            if (tex) {
                SDL_Rect dst = {
                    win_w / 2 - surf->w / 2,
                    win_h / 2 + 40,
                    surf->w, surf->h
                };
                SDL_RenderCopy(renderer, tex, NULL, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surf);
        }
    }

    if (font_small) {
        const char *sub_text = is_success
            ? "> AUTHENTICATION SEQUENCE COMPLETE"
            : "> INTRUSION DETECTED -- LOCKDOWN INITIATED";
        SDL_Color sub_color = {180, 180, 180, 200};

        SDL_Surface *sub_surf = TTF_RenderText_Blended(font_small, sub_text, sub_color);
        if (sub_surf) {
            SDL_Texture *sub_tex = SDL_CreateTextureFromSurface(renderer, sub_surf);
            if (sub_tex) {
                SDL_Rect dst = {
                    win_w / 2 - sub_surf->w / 2,
                    win_h / 2 + 75,
                    sub_surf->w, sub_surf->h
                };
                SDL_RenderCopy(renderer, sub_tex, NULL, &dst);
                SDL_DestroyTexture(sub_tex);
            }
            SDL_FreeSurface(sub_surf);
        }
    }
}
