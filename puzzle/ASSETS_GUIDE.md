# Asset Guide - Enigme 2 Puzzle Game

This guide explains what images you need and where to place them.

## Folder Structure

```
assets/
├── puzzles/          # Puzzle images (board + 3 propositions each)
│   ├── puzzle_1_board.png
│   ├── puzzle_1_prop_A.png
│   ├── puzzle_1_prop_B.png
│   ├── puzzle_1_prop_C.png
│   ├── puzzle_2_board.png
│   ├── puzzle_2_prop_A.png
│   ├── puzzle_2_prop_B.png
│   ├── puzzle_2_prop_C.png
│   ├── puzzle_3_board.png
│   ├── puzzle_3_prop_A.png
│   ├── puzzle_3_prop_B.png
│   ├── puzzle_3_prop_C.png
│   ├── puzzle_4_board.png
│   ├── puzzle_4_prop_A.png
│   ├── puzzle_4_prop_B.png
│   ├── puzzle_4_prop_C.png
│   ├── puzzle_5_board.png
│   ├── puzzle_5_prop_A.png
│   ├── puzzle_5_prop_B.png
│   └── puzzle_5_prop_C.png
└── ui/               # UI elements
    ├── background.png
    ├── success.png
    ├── fail.png
    ├── timer_bar.png
    ├── target_shadow.png
    └── btn_next.png
```

## Puzzle Images (assets/puzzles/)

### Board Images (`puzzle_X_board.png`)
- **Quantity**: 5 images (puzzle_1 through puzzle_5)
- **Recommended Size**: 340x360 pixels (will be scaled to fit 360x380 panel)
- **Format**: PNG with transparency support
- **Content**: The main puzzle image showing a scene with a missing piece
- **Design Tips**:
  - Leave a visible gap/hole where the correct piece should fit
  - The gap should be roughly 85x85 pixels in the center area
  - Pixel art style works well with the game's aesthetic

### Proposition Images (`puzzle_X_prop_A/B/C.png`)
- **Quantity**: 15 images total (3 propositions × 5 puzzles)
- **Recommended Size**: 170x85 pixels each (will be scaled to fit 184x78 card)
- **Format**: PNG with transparency support
- **Content**: Individual pieces that could fit in the board's gap
- **Design Tips**:
  - Only ONE proposition per puzzle should correctly fit the gap
  - The other two should be clearly wrong (different shape/color/pattern)
  - Keep the piece visually distinct from the background
  - Match the art style of the corresponding board image

### Example Puzzle Set:
```
puzzle_1_board.png      - Train dining car with missing wall section
puzzle_1_prop_A.png     - Correct wall piece (fits perfectly)
puzzle_1_prop_B.png     - Wrong piece (different color/texture)
puzzle_1_prop_C.png     - Wrong piece (different shape)
```

## UI Images (assets/ui/)

### Background (`background.png`)
- **Size**: 960x540 pixels (full screen)
- **Content**: Background image displayed behind all UI panels
- **Suggestions**: Snowy landscape, train exterior, or atmospheric scene

### Success Overlay (`success.png`)
- **Size**: 200x200 pixels (will be scaled)
- **Content**: Visual feedback for correct answer
- **Suggestions**: Green checkmark, "ACCESS GRANTED" text, or positive icon

### Fail Overlay (`fail.png`)
- **Size**: 200x200 pixels (will be scaled)
- **Content**: Visual feedback for wrong answer
- **Suggestions**: Red X, "SYSTEM COMPROMISED" text, or warning icon

### Timer Bar (`timer_bar.png`)
- **Size**: 300x20 pixels
- **Content**: Horizontal bar showing time remaining
- **Note**: Currently rendered as colored rectangle, image is optional

### Target Shadow (`target_shadow.png`)
- **Size**: 90x90 pixels
- **Content**: Visual indicator showing where to drop the piece
- **Suggestions**: Glowing square outline, crosshair, or pulsing frame

### Next Button (`btn_next.png`)
- **Size**: 120x40 pixels
- **Content**: Button background for "NEXT" action
- **Note**: Currently rendered with text, image is optional

## Art Style Recommendations

1. **Consistency**: All puzzle boards and propositions should share the same art style
2. **Resolution**: Pixel art at consistent scale (e.g., 16x16 or 32x32 pixel tiles)
3. **Color Palette**: Limited palette for cohesive look (Snowpiercer uses cool blues, browns, greens)
4. **Contrast**: Ensure puzzle pieces stand out from their backgrounds
5. **Theme**: Match the Snowpiercer train aesthetic (industrial, dystopian, confined spaces)

## Creating New Puzzles

1. **Design the board**: Create your scene with a clear gap/missing section
2. **Create the correct piece**: Design a piece that perfectly fills the gap
3. **Create wrong pieces**: Design 2 pieces that clearly don't fit (wrong shape, color, or pattern)
4. **Name files correctly**: Follow the `puzzle_X_board.png` and `puzzle_X_prop_A/B/C.png` naming
5. **Place in folder**: Copy all 4 files to `assets/puzzles/`
6. **Update code** (optional): If adding more than 5 puzzles, update `NB_IMAGES_PUZZLE` in enigme2.h

## Testing Your Assets

After adding new images:
```bash
cd /path/to/Kimi_Agent_SDL2\ UI\ Refactor
make clean && make
./enigme2
```

Check the terminal for any image loading warnings.

## Current Asset Locations

Your assets are currently in:
- `/home/adem-dilou/attempt4/assets/puzzles/` - Puzzle images
- `/home/adem-dilou/attempt4/assets/ui/` - UI elements

The game loads from the `assets/` folder relative to the executable.
