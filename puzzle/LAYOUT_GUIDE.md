# Tactical Command Interface - Layout Guide

## Overview

The interface has been refactored from a scattered layout to a professional **3-Column Tactical Command** grid at **960×540** resolution.

## Layout Structure

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  TACTICAL COMMAND INTERFACE                                    [TIMER BAR]  │
├────────────────────┬────────────────────────────┬───────────────────────────┤
│                    │                            │                           │
│  [HP PANEL]        │                            │   [INVENTORY CARD 1]      │
│  ███░░░            │                            │   ┌───────────────────┐   │
│                    │                            │   │   Proposition A   │   │
├────────────────────┤                            │   └───────────────────┘   │
│                    │                            │                           │
│  [OBJECTIVE]       │      MAIN PUZZLE           │   [INVENTORY CARD 2]      │
│  ┌────────────┐    │      ┌──────────────┐      │   ┌───────────────────┐   │
│  │  Objective │    │      │              │      │   │   Proposition B   │   │
│  │  Text...   │    │      │   ┌──────┐   │      │   └───────────────────┘   │
│  │            │    │      │   │TARGET│   │      │                           │
│  └────────────┘    │      │   │ ZONE │   │      │   [INVENTORY CARD 3]      │
│                    │      │   └──────┘   │      │   ┌───────────────────┐   │
├────────────────────┤      │              │      │   │   Proposition C   │   │
│                    │      └──────────────┘      │   └───────────────────┘   │
│  [SYSTEM STATUS]   │                            │                           │
│  ┌────────────┐    │                            │   [DRAGGABLE PIECE]       │
│  │  SCORE:    │    │                            │         ┌─────┐           │
│  │  LEVEL:    │    │                            │         │     │           │
│  │  ◉◉◉◉◉    │    │                            │         └─────┘           │
│  └────────────┘    │                            │                           │
│                    │                            │                           │
└────────────────────┴────────────────────────────┴───────────────────────────┘
         LEFT              CENTER (420px)              RIGHT (280px)
        (200px)                                    
```

## Coordinate Reference

### Column Boundaries

| Column | X Position | Width | Height |
|--------|-----------|-------|--------|
| Left | 15 | 200 | 475 (from y=50) |
| Center | 230 | 420 | 475 (from y=50) |
| Right | 665 | 280 | 475 (from y=50) |

### Left Column Panels

```c
/* HP Panel */
PANEL_HP_X = 15,      PANEL_HP_Y = 50
PANEL_HP_W = 200,     PANEL_HP_H = 80

/* Objective Panel */
PANEL_OBJECTIVE_X = 15,      PANEL_OBJECTIVE_Y = 145  (50 + 80 + 15)
PANEL_OBJECTIVE_W = 200,     PANEL_OBJECTIVE_H = 180

/* Status Panel */
PANEL_STATUS_X = 15,         PANEL_STATUS_Y = 340  (145 + 180 + 15)
PANEL_STATUS_W = 200,        PANEL_STATUS_H = 190
```

### Center Column

```c
/* Timer Bar (at top) */
TIMER_X = 230,       TIMER_Y = 15
TIMER_W = 300,       TIMER_H = 22

/* Main Puzzle Panel */
PANEL_PUZZLE_X = 230,       PANEL_PUZZLE_Y = 50
PANEL_PUZZLE_W = 420,       PANEL_PUZZLE_H = 475

/* Target Zone (within puzzle) */
TARGET_X = 390,      /* 230 + 160 */
TARGET_Y = 237,      /* 50 + 187 */
TARGET_SIZE = 100
```

### Right Column - Inventory Cards

```c
/* Card dimensions */
CARD_X = 675,        /* 665 + 10 margin */
CARD_W = 260,        /* 280 - 20 margin */
CARD_H = 135

/* Card positions */
CARD_1_Y = 50
CARD_2_Y = 200       /* 50 + 135 + 15 */
CARD_3_Y = 350       /* 200 + 135 + 15 */

/* Draggable piece start position */
Piece X = 785        /* 675 + 130 - 45 */
Piece Y = 515        /* 350 + 135 + 30 */
```

## Visual Elements

### Tactical Frame (`draw_tactical_frame`)

Each panel uses the tactical frame renderer:

```c
void draw_tactical_frame(SDL_Renderer *ren, SDL_Rect r)
```

**Features:**
- Dark panel background (22, 26, 32, 245)
- 2-pixel cyan border (80, 220, 255)
- Secondary dim border (60, 180, 220)
- Rivet details at all 4 corners

### Glow Effects

**Hover Glow:** Pulsing cyan around inventory cards
**Selected Glow:** 
- Green pulse for correct answer
- Red pulse for wrong answer

```c
draw_pulsing_glow(rect, r, g, b);  // Uses SDL_BLENDMODE_BLEND
```

### HP Display

Visual bar with 3 segments:
- **Green** (3 lives): Full health
- **Yellow** (2 lives): Warning
- **Red** (1 life): Critical

## Interaction Flow

1. **Select Inventory Card** (Right Column)
   - Click on one of 3 proposition cards
   - Glow effect indicates selection

2. **Correct Answer** → Piece Appears
   - Draggable piece spawns below cards
   - Can now drag to target zone

3. **Drag to Target** (Center Column)
   - Target zone pulses to indicate drop location
   - Piece scales up while dragging
   - Snap-to-target when within 25px

4. **Success/Failure** (Rotozoom)
   - Message appears in center of puzzle panel
   - Spins and pulses with animation
   - Click anywhere to continue

## Asset Paths

```
assets/
├── puzzles/
│   ├── puzzle_1_board.png
│   ├── puzzle_1_prop_A.png
│   ├── puzzle_1_prop_B.png
│   ├── puzzle_1_prop_C.png
│   └── ... (puzzles 2-5)
└── ui/
    ├── background.png
    ├── success.png
    ├── fail.png
    ├── timer_bar.png (optional)
    └── target_shadow.png (optional)
```

## Key Changes from Original

| Aspect | Before | After |
|--------|--------|-------|
| Layout | Scattered elements | 3-column tactical grid |
| Window | 960×540 (same) | Same, but organized |
| Puzzle | 520×310 at (40,90) | 420×475 at (230,50) |
| Props | 300×80 horizontal | 260×135 vertical cards |
| Target | Simple rectangle | Pulsing with brackets |
| Frames | Basic rectangles | Tactical frames with rivets |
| Glow | None | Pulsing blend-mode glows |

## Building

```bash
# Check dependencies
make check

# Build
make

# Run
./enigme2
```

## Preserved Logic

All original game logic is preserved:
- `g_drag_enabled` - Still controls drag state
- `g_dragging` - Still tracks drag operation
- `verifier_position_piece()` - Same snap logic (25px threshold)
- `g_selected` - Still tracks selected proposition
- Timer, lives, scoring - Unchanged
