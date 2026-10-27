#!/usr/bin/env bash
# ----------------------------------------------------------------------
# Web (WebAssembly) build for Snowpiercer via Emscripten.
#
# The native build is untouched — keep using `make` for that.  This script
# compiles the SAME source tree with emcc, using the SDL2 / SDL2_image /
# SDL2_ttf / SDL2_mixer ports (PNG + OGG decoders), and emits a runnable
# page at build/web/index.html.
#
# Requires the Emscripten SDK (emcc) on PATH.  If emsdk is installed under
# ~/emsdk this script sources it automatically.
#
# Run it, then serve with:   emrun --no_browser --port 8000 build/web/index.html
# ----------------------------------------------------------------------
set -euo pipefail
cd "$(dirname "$0")"

# --- locate emcc -------------------------------------------------------
if ! command -v emcc >/dev/null 2>&1; then
    if [ -f "$HOME/emsdk/emsdk_env.sh" ]; then
        # shellcheck disable=SC1091
        source "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1 || true
    fi
fi
command -v emcc >/dev/null 2>&1 || {
    echo "error: emcc not found. Install emsdk and run 'source ~/emsdk/emsdk_env.sh'." >&2
    exit 1
}

OUT_DIR=build/web
mkdir -p "$OUT_DIR"

# --- sources: mirror of the native Makefile SRCS_REGULAR + leaderboard --
SRCS=(
  main.c background.c combat.c ui.c audio.c quiz.c weaponfx.c
  gameplay.c interlude_manager.c wire_puzzle.c highscore_screen.c
  histoire.c
  arduino_input.c llm.c llm_queue.c npc_dialogue.c enemy_taunts.c
  barker.c boss_monologue.c kinetic.c effects.c cutscene.c
  fade.c outro.c damage_numbers.c dialogue_cinematic.c door_anim.c
  boss_intro.c boss_cutscene.c boss_anim.c boss_ai.c boss_hud.c
  src/char_anim.c src/weapon_select.c src/skin_select.c
  src/mode_select.c src/two_player.c src/versus.c
  puzzle/enigme2.c puzzle/enigme2_systems.c puzzle/rotozoom.c
  quizz/enigme/enigme.c
  "high score menu /leaderboard.c"
)

# --- asset staging: trim + compress to fit Vercel's 100MB/file limit ---
# The raw asset tree packs to ~212MB in a single index.data, which exceeds
# Vercel's hard 100MB-per-file cap.  We build a pruned, compressed copy in
# $STAGE and preload THAT (mapped back to the original virtual paths the game
# opens).  Source assets are never modified.
#
# Cuts (all safe for the web build):
#   - assets/video.mp4         story video isn't played on web
#   - "assets/assets phase 2"  + "assets/assets"  (zero code references)
#   - every *.mp3              the web mixer is ogg-only (can't decode mp3)
#   - game music.wav           duplicate of game music.ogg (loaded first)
#   - train sound background.wav  23MB ambience -> falls back to the 4.5MB one
# Then large PNGs (>400KB: backgrounds, wagons, parallax) are quantized to a
# 256-colour palette (~5-10x smaller); small sprite frames are left untouched
# so their soft-alpha edges keep full quality.
STAGE="build/webstage"
echo "staging + trimming assets into $STAGE ..."
rm -rf "$STAGE"; mkdir -p "$STAGE"

cp -r assets "$STAGE/assets"
[ -d "spritesheets assets" ] && cp -r "spritesheets assets" "$STAGE/spritesheets assets"
shopt -s nullglob
for f in *.png *.ttf *.ogg *.wav; do cp "$f" "$STAGE/"; done   # note: no *.mp3
shopt -u nullglob

# prune
rm -f  "$STAGE/assets/video.mp4"
rm -rf "$STAGE/assets/assets phase 2" "$STAGE/assets/assets"
find "$STAGE" -type f -iname '*.mp3' -delete
rm -f  "$STAGE/assets/sound/game music.wav"
rm -f  "$STAGE/assets/sound/train sound background.wav"

# Quantize large PNGs with pngquant.  Crucially this PRESERVES 8-bit alpha —
# ImageMagick's PNG8 reduces alpha to 1-bit, which turns anti-aliased sprite
# edges into a solid white halo/box (the enemy "white box" bug).  pngquant
# keeps smooth alpha and still cuts ~3-10x.  --skip-if-larger keeps the
# original when compression wouldn't help.
PNGQUANT=""
for cand in ./tools/pngquant "$(command -v pngquant 2>/dev/null)"; do
  [ -n "$cand" ] && [ -x "$cand" ] && { PNGQUANT="$cand"; break; }
done
if [ -n "$PNGQUANT" ]; then
  find "$STAGE" -type f -iname '*.png' -size +400k | while IFS= read -r f; do
    "$PNGQUANT" --quality=65-92 --strip --force --skip-if-larger \
                --output "$f" -- "$f" 2>/dev/null || true
  done
else
  echo "  WARNING: pngquant not found — assets NOT compressed; index.data may"
  echo "           exceed Vercel's 100MB limit. Put a pngquant binary at ./tools/pngquant."
fi
echo "  staged asset size: $(du -sh "$STAGE" | cut -f1)"

# preload from the stage, mapped to the virtual paths the game opens
PRELOAD=( --preload-file "$STAGE/assets@assets" )
[ -d "$STAGE/spritesheets assets" ] && \
  PRELOAD+=( --preload-file "$STAGE/spritesheets assets@spritesheets assets" )
shopt -s nullglob
for f in "$STAGE"/*.png "$STAGE"/*.ttf "$STAGE"/*.ogg "$STAGE"/*.wav; do
    PRELOAD+=( --preload-file "$f@$(basename "$f")" )
done
shopt -u nullglob

# --- emcc settings (names verified against Emscripten 5.0.x settings.js) -
#   USE_SDL=2 / USE_SDL_IMAGE=2 / USE_SDL_TTF=2 / USE_SDL_MIXER=2  -> ports
#   SDL2_IMAGE_FORMATS=png / SDL2_MIXER_FORMATS=ogg                -> codecs
#   ALLOW_MEMORY_GROWTH=1  -> heap can grow at runtime
#   FETCH=1                -> links emscripten_fetch (the web LLM client)
#   -lidbfs.js             -> IndexedDB filesystem for persistent saves
#   EXPORTED_RUNTIME_METHODS / --pre-js -> mount /persist before main()
emcc -O2 -std=gnu99 \
  "${SRCS[@]}" \
  -I. -Isrc -Ipuzzle -Iquizz/enigme \
  -sUSE_SDL=2 \
  -sUSE_SDL_IMAGE=2 -sSDL2_IMAGE_FORMATS=png \
  -sUSE_SDL_TTF=2 \
  -sUSE_SDL_MIXER=2 -sSDL2_MIXER_FORMATS=ogg \
  -sALLOW_MEMORY_GROWTH=1 \
  -sFETCH=1 \
  -sEXIT_RUNTIME=0 \
  -sASYNCIFY=0 \
  -sFORCE_FILESYSTEM=1 \
  -lidbfs.js \
  -sEXPORTED_RUNTIME_METHODS=FS,addRunDependency,removeRunDependency \
  --pre-js web/idbfs_preload.js \
  "${PRELOAD[@]}" \
  -o "$OUT_DIR/index.html"

echo ""
echo "Built $OUT_DIR/index.html (+ index.js / index.wasm / index.data)"
echo "Run it:  emrun --no_browser --port 8000 $OUT_DIR/index.html"
echo "    then open http://localhost:8000/index.html"
