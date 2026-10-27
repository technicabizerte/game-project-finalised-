# Enigme 2 - Puzzle Game

A professional SDL2-based puzzle game where players must identify and place the correct missing piece.

## Features

- **5 Unique Puzzles** - Random puzzle selection with no repeats until all are played
- **Drag & Drop Gameplay** - Select the correct proposition, then drag it to the target zone
- **Timer with Visual Feedback** - Color-changing timer bar (green → yellow → red)
- **Rotozoom Effects** - Animated success/failure messages
- **Lives System** - 3 lives, lose one for each wrong answer
- **Score Tracking** - Gain points for correct placements
- **Professional UI** - Smooth animations, hover effects, and polished visuals

## Controls

1. **Click** on the correct proposition from the 3 options
2. **Drag** the piece that appears to the target zone (pulsing square on the puzzle)
3. **Release** when near the target to snap it into place

## Building

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt-get install libsdl2-dev libsdl2-image-dev

# Or check if installed
make check
```

### Compile

```bash
# Build the game
make

# Or compile manually
gcc -Wall -o enigme2 main.c enigme2.c -lSDL2 -lSDL2_image -lm
```

### Run

```bash
# Run the game
make run

# Or directly
./enigme2
```

## File Structure

```
.
├── main.c              # Entry point
├── enigme2.c           # Main game logic
├── enigme2.h           # Header file
├── Makefile            # Build configuration
├── README.md           # This file
└── assets/
    ├── puzzles/        # Puzzle images (board + 3 propositions each)
    │   ├── puzzle_1_board.png
    │   ├── puzzle_1_prop_A.png
    │   ├── puzzle_1_prop_B.png
    │   ├── puzzle_1_prop_C.png
    │   └── ... (puzzles 2-5)
    └── ui/             # UI elements
        ├── background.png
        ├── success.png
        ├── fail.png
        ├── timer_bar.png
        ├── target_shadow.png
        └── btn_next.png
```

## Game States

| State | Description |
|-------|-------------|
| EN_JEU | Active gameplay - select and drag |
| GAGNE | Correct placement - click Next to continue |
| PERDU | Time's up or no lives - click Next to retry |

## Scoring

- **+10 points** for each correct placement
- **Game Over** when lives reach 0
- **Timer** - 30 seconds per puzzle

## Troubleshooting

### Game won't compile
```bash
# Install SDL2 dependencies
sudo apt-get update
sudo apt-get install libsdl2-dev libsdl2-image-dev
```

### Images not loading
- Ensure `assets/` folder is in the same directory as the executable
- Check that all puzzle images follow the naming convention:
  - `puzzle_X_board.png` (main puzzle)
  - `puzzle_X_prop_A.png` (proposition 1)
  - `puzzle_X_prop_B.png` (proposition 2)
  - `puzzle_X_prop_C.png` (proposition 3)

### Black screen
- Check that `assets/ui/background.png` exists
- Verify all image files are valid PNG files

## Credits

Developed for Projet 1A - Algorithmique & Programmation
