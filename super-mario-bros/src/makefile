# ============================================================
#  Makefile — Super Mario Bros : Enhanced Edition
#  Builds the game on Windows (MinGW), Linux, and macOS.
#
#  USAGE:
#    cd src
#    make            # build the game
#    make run        # build and run
#    make clean      # delete the compiled binary
# ============================================================

CC      := gcc
CFLAGS  := -O2 -Wall -std=c99
SRC     := super_mario.c

# ---- Detect the operating system and pick the right linker flags ----
ifeq ($(OS),Windows_NT)
    TARGET  := super_mario.exe
    LDFLAGS := -lraylib -lopengl32 -lgdi32 -lwinmm
    RM      := del /Q
else
    UNAME_S := $(shell uname -s)
    TARGET  := super_mario
    ifeq ($(UNAME_S),Linux)
        LDFLAGS := -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
    endif
    ifeq ($(UNAME_S),Darwin)
        LDFLAGS := -lraylib -framework OpenGL -framework Cocoa \
                   -framework IOKit -framework CoreVideo
    endif
    RM := rm -f
endif

# ---- Targets ----
all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(SRC) -o $(TARGET) $(CFLAGS) $(LDFLAGS)
	@echo ""
	@echo "  Build successful!  Run with:  ./$(TARGET)"
	@echo ""

run: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(TARGET)

.PHONY: all run clean
