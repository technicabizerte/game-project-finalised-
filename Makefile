jeu: main.c background.c combat.c background.h combat.h
	gcc main.c background.c combat.c -o jeu $(shell sdl2-config --cflags --libs) -lSDL2_image -lSDL2_ttf -lSDL2_mixer -lm
