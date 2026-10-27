SRCS_REGULAR = main.c background.c combat.c ui.c audio.c quiz.c weaponfx.c \
       gameplay.c interlude_manager.c wire_puzzle.c highscore_screen.c \
       histoire.c \
       arduino_input.c llm.c llm_queue.c npc_dialogue.c enemy_taunts.c \
       barker.c boss_monologue.c kinetic.c effects.c cutscene.c \
       fade.c outro.c damage_numbers.c dialogue_cinematic.c door_anim.c \
       boss_intro.c boss_cutscene.c boss_anim.c boss_ai.c boss_hud.c \
       src/char_anim.c \
       src/weapon_select.c \
       src/skin_select.c \
       src/mode_select.c \
       src/two_player.c \
       src/versus.c \
       puzzle/enigme2.c puzzle/enigme2_systems.c puzzle/rotozoom.c \
       quizz/enigme/enigme.c

HSC_SRC := high\ score\ menu\ /leaderboard.c

HDRS = background.h combat.h ui.h audio.h quiz.h weaponfx.h \
       gameplay.h interlude_manager.h wire_puzzle.h highscore_screen.h \
       histoire.h \
       arduino_input.h llm.h llm_queue.h npc_dialogue.h enemy_taunts.h \
       barker.h boss_monologue.h kinetic.h effects.h cutscene.h \
       fade.h outro.h damage_numbers.h dialogue_cinematic.h door_anim.h \
       boss_intro.h boss_cutscene.h boss_anim.h boss_ai.h boss_hud.h \
       src/char_anim.h \
       src/weapon_select.h \
       src/skin_select.h \
       src/mode_select.h \
       src/two_player.h \
       src/versus.h

CURL_CFLAGS := $(shell pkg-config --cflags libcurl)
CURL_LIBS   := $(shell pkg-config --libs libcurl)

jeu: $(SRCS_REGULAR) $(HDRS)
	gcc -std=gnu99 -g -rdynamic -Wall -Wextra $(SRCS_REGULAR) "high score menu /leaderboard.c" -o jeu \
	    $(shell sdl2-config --cflags --libs) \
	    -lSDL2_image -lSDL2_ttf -lSDL2_mixer -lm \
	    $(CURL_CFLAGS) $(CURL_LIBS) -lpthread \
	    -I. -Isrc -Ipuzzle -Iquizz/enigme

# WebAssembly build via Emscripten (see build_web.sh).  Does not touch the
# native `jeu` target above.  Requires emcc on PATH.
.PHONY: web
web:
	./build_web.sh
