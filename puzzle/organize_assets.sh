#!/bin/bash

# organize_assets.sh - Organizes puzzle assets into the correct structure
# Run this script from your project root directory

echo "======================================"
echo "  Asset Organization Script"
echo "======================================"
echo ""

# Create directory structure
echo "[1/4] Creating directory structure..."
mkdir -p assets/puzzles
mkdir -p assets/ui
mkdir -p assets/assets/set{1..5}
echo "      Directories created!"
echo ""

# Check for existing assets
echo "[2/4] Checking for existing assets..."

# Function to copy if exists
copy_if_exists() {
    if [ -f "$1" ]; then
        cp "$1" "$2"
        echo "      Copied: $1 -> $2"
        return 0
    fi
    return 1
}

# Look for puzzle images in various locations
for i in 1 2 3 4 5; do
    # Try different naming patterns
    copy_if_exists "assets/puzzles/puzzle_${i}_board.png" "assets/puzzles/puzzle_${i}_board.png" 2>/dev/null
    copy_if_exists "puzzle set ${i}/puzzle_${i}_board.png" "assets/puzzles/puzzle_${i}_board.png" 2>/dev/null
    copy_if_exists "puzzle_set_${i}/puzzle_${i}_board.png" "assets/puzzles/puzzle_${i}_board.png" 2>/dev/null
    
    # Propositions
    for prop in A B C; do
        lower_prop=$(echo $prop | tr '[:upper:]' '[:lower:]')
        copy_if_exists "assets/puzzles/puzzle_${i}_prop_${prop}.png" "assets/puzzles/puzzle_${i}_prop_${prop}.png" 2>/dev/null
        copy_if_exists "puzzle set ${i}/puzzle_${i}_prop_${prop}.png" "assets/puzzles/puzzle_${i}_prop_${prop}.png" 2>/dev/null
        copy_if_exists "puzzle_set_${i}/prop${prop}.png" "assets/puzzles/puzzle_${i}_prop_${prop}.png" 2>/dev/null
    done
done

echo ""
echo "[3/4] Checking for UI assets..."

# UI assets
copy_if_exists "assets/ui/background.png" "assets/ui/background.png" 2>/dev/null
copy_if_exists "ui/background.png" "assets/ui/background.png" 2>/dev/null
copy_if_exists "assets/assets/ui/train bg.png" "assets/ui/background.png" 2>/dev/null

copy_if_exists "assets/ui/success.png" "assets/ui/success.png" 2>/dev/null
copy_if_exists "ui/success.png" "assets/ui/success.png" 2>/dev/null
copy_if_exists "assets/assets/ui/win scren.png" "assets/ui/success.png" 2>/dev/null

copy_if_exists "assets/ui/fail.png" "assets/ui/fail.png" 2>/dev/null
copy_if_exists "ui/fail.png" "assets/ui/fail.png" 2>/dev/null
copy_if_exists "assets/assets/ui/loss screen.png" "assets/ui/fail.png" 2>/dev/null

copy_if_exists "assets/ui/timer_bar.png" "assets/ui/timer_bar.png" 2>/dev/null
copy_if_exists "ui/timer_bar.png" "assets/ui/timer_bar.png" 2>/dev/null
copy_if_exists "assets/assets/ui/timer.png" "assets/ui/timer_bar.png" 2>/dev/null

copy_if_exists "assets/ui/target_shadow.png" "assets/ui/target_shadow.png" 2>/dev/null
copy_if_exists "ui/target_shadow.png" "assets/ui/target_shadow.png" 2>/dev/null
copy_if_exists "assets/assets/ui/puzzle block.png" "assets/ui/target_shadow.png" 2>/dev/null

copy_if_exists "assets/ui/btn_next.png" "assets/ui/btn_next.png" 2>/dev/null
copy_if_exists "ui/btn_next.png" "assets/ui/btn_next.png" 2>/dev/null
copy_if_exists "assets/assets/ui/arrow.png" "assets/ui/btn_next.png" 2>/dev/null

echo ""
echo "[4/4] Verifying structure..."
echo ""
echo "Expected files in assets/puzzles/:"
ls -la assets/puzzles/ 2>/dev/null || echo "      (directory empty or not found)"
echo ""
echo "Expected files in assets/ui/:"
ls -la assets/ui/ 2>/dev/null || echo "      (directory empty or not found)"
echo ""

echo "======================================"
echo "  Organization Complete!"
echo "======================================"
echo ""
echo "Next steps:"
echo "  1. Ensure all puzzle images are in assets/puzzles/"
echo "  2. Ensure all UI images are in assets/ui/"
echo "  3. Run: make && ./enigme2"
echo ""
