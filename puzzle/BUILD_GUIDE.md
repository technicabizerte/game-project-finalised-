# Snowpiercer: Revolt — Build Guide

## Quick Start

```bash
# 1. Check dependencies
make check

# 2. Install missing dependencies (if needed)
sudo apt-get install libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev

# 3. Build the game
make

# 4. Run the game
./enigme2
```

## File Structure

```
.
├── main.c              # Entry point
├── enigme2.c           # Main game logic & rendering
├── enigme2.h           # Header with all declarations
├── enigme2_systems.c   # Drag/drop, overlays, text wrapping
├── Makefile            # Build configuration
└── assets/
    ├── puzzles/        # Puzzle images
    │   ├── puzzle_1_board.png
    │   ├── puzzle_1_prop_A.png
    │   ├── puzzle_1_prop_B.png
    │   ├── puzzle_1_prop_C.png
    │   └── ... (puzzles 2-5)
    ├── ui/             # UI assets (optional)
    │   ├── background.png
    │   ├── success.png
    │   ├── fail.png
    │   ├── timer_bar.png
    │   └── target_shadow.png
    └── fonts/          # Font files (optional)
        └── arial.ttf
```

## Makefile Commands

| Command | Description |
|---------|-------------|
| `make` | Build the executable |
| `make run` | Build and run |
| `make clean` | Remove build files |
| `make rebuild` | Clean and rebuild |
| `make check` | Verify SDL2 installation |
| `make debug` | Build with debug symbols |
| `make help` | Show all commands |

## Resolving "Undefined Reference" Errors

### Error: `undefined reference to 'SDL_...'`
**Cause:** Missing SDL2 libraries  
**Fix:**
```bash
sudo apt-get install libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev
```

### Error: `undefined reference to 'TTF_...'`
**Cause:** SDL2_ttf not linked  
**Fix:** The Makefile already includes `-lSDL2_ttf` in LDFLAGS

### Error: `undefined reference to 'run_enigme2'`
**Cause:** Object files not linked together  
**Fix:** Ensure Makefile compiles all three .c files:
```makefile
SRCS = main.c enigme2.c enigme2_systems.c
```

## Resolving "IMG_Load failed" Errors

### Linux Case-Sensitivity
Linux filesystems are case-sensitive. The code tries multiple variants:

```c
g_bg = load_tex("assets/ui/background.png");
if (!g_bg) g_bg = load_tex("assets/ui/background.PNG");
```

### Asset Path Checklist

1. **Puzzle images** must be in `assets/puzzles/`:
   - `puzzle_1_board.png`
   - `puzzle_1_prop_A.png`
   - `puzzle_1_prop_B.png`
   - `puzzle_1_prop_C.png`
   - (repeat for 2-5)

2. **UI images** (optional) in `assets/ui/`:
   - `background.png`
   - `success.png`
   - `fail.png`

3. **Font file** (optional) in `assets/fonts/`:
   - `arial.ttf` or system fallback will be used

### Verify Asset Paths

```bash
# Check puzzle directory
ls -la assets/puzzles/

# Check UI directory
ls -la assets/ui/

# Check fonts directory
ls -la assets/fonts/
```

## Header Integration

The `enigme2.h` header includes:

```c
#ifndef ENIGME2_H
#define ENIGME2_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

/* Global SDL pointers - defined in enigme2.c */
extern SDL_Window   *g_win;
extern SDL_Renderer *g_ren;

/* Function prototypes */
int run_enigme2(void);
void lancer_chrono(t_chrono *c, int duree);
int temps_restant(t_chrono *c);
void init_joueur(t_joueur *j);

/* Systems functions */
void handle_mousedown(int mx, int my);
void handle_mousemotion(int mx, int my);
void handle_mouseup(int mx, int my);
void render_drag_ghost(SDL_Renderer *renderer);
void trigger_overlay(OverlayResult result);
void render_overlay(SDL_Renderer *renderer, ...);

#endif
```

## Troubleshooting Build Errors

### "redefinition of '...'"
**Cause:** Function defined in both .c files  
**Fix:** Use `static` for file-local functions, or move to header with `extern`

### "implicit declaration of function"
**Cause:** Missing function prototype in header  
**Fix:** Add prototype to `enigme2.h`

### "multiple definition of 'g_win'"
**Cause:** Global defined in multiple files  
**Fix:** Use `extern` in header, define in ONE .c file

## Debug Build

For debugging with GDB:

```bash
make debug
gdb ./enigme2
(gdb) run
```

## Expected Output

Successful build:
```
[CC] Compiling main.c...
[CC] Compiling enigme2.c...
[CC] Compiling enigme2_systems.c...
[LINK] Linking enigme2...
======================================
  Build Complete: ./enigme2
======================================
```

Successful run:
```
=================================
   SNOWPIERCER: REVOLT
   TACTICAL COMMAND INTERFACE
=================================
[INFO] Loading tactical assets...
[INFO] Assets loaded!
[INFO] New tactical round! Puzzle 1
[INFO] Tactical system online.
```
