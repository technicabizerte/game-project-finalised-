CC = gcc
CFLAGS = -Wall -Wextra -g -O2 -std=c99 -D_DEFAULT_SOURCE
LDFLAGS = -lSDL2 -lSDL2_image -lSDL2_ttf -lm
TARGET = enigme2
SRCS = main.c enigme2.c enigme2_systems.c rotozoom.c
OBJS = $(SRCS:.c=.o)
HDR = enigme2.h rotozoom.h

# Default target
all: $(TARGET)

# Link executable from object files
$(TARGET): $(OBJS)
	@echo "[LINK] Linking $(TARGET)..."
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)
	@echo "======================================"
	@echo "  Build Complete: ./$(TARGET)"
	@echo "======================================"

# Compile source files to object files
%.o: %.c $(HDR)
	@echo "[CC] Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build files
clean:
	@echo "[CLEAN] Removing object files and executable..."
	rm -f $(OBJS) $(TARGET)
	@echo "Clean complete."

# Run the game
run: $(TARGET)
	@echo "[RUN] Starting $(TARGET)..."
	./$(TARGET)

# Rebuild everything from scratch
rebuild: clean all

# Check for required dependencies
check:
	@echo "Checking SDL2 dependencies..."
	@pkg-config --exists sdl2 && echo "  [OK] SDL2" || echo "  [MISSING] SDL2 - Install: sudo apt-get install libsdl2-dev"
	@pkg-config --exists SDL2_image && echo "  [OK] SDL2_image" || echo "  [MISSING] SDL2_image - Install: sudo apt-get install libsdl2-image-dev"
	@pkg-config --exists SDL2_ttf && echo "  [OK] SDL2_ttf" || echo "  [MISSING] SDL2_ttf - Install: sudo apt-get install libsdl2-ttf-dev"
	@echo ""
	@echo "To install all dependencies:"
	@echo "  sudo apt-get install libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev"

# Debug build with extra symbols
debug: CFLAGS += -DDEBUG -O0
debug: clean $(TARGET)

# Show help
help:
	@echo "Snowpiercer: Revolt — Build System"
	@echo ""
	@echo "Targets:"
	@echo "  make          - Build the game (default)"
	@echo "  make run      - Build and run the game"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make rebuild  - Clean and rebuild"
	@echo "  make check    - Verify SDL2 dependencies"
	@echo "  make debug    - Build with debug symbols"
	@echo "  make help     - Show this help"
	@echo ""
	@echo "Asset Requirements:"
	@echo "  assets/puzzles/puzzle_{1-5}_board.png"
	@echo "  assets/puzzles/puzzle_{1-5}_prop_{A,B,C}.png"
	@echo "  assets/ui/background.png (optional)"
	@echo "  assets/fonts/arial.ttf or system fallback"

.PHONY: all clean run rebuild check debug help
