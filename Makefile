# medplay - MinGW-w64 / MSYS2 UCRT64 build
#
# Phase 0: SDL2 + Nuked-OPL3 tone test.
# Nuked-OPL3 (src/opl3.c, src/opl3.h) is vendored under src/ and compiled in.
#
#   make            build medplay.exe
#   make run        build and run the tone test
#   make clean      remove build artefacts
#
# Override the compiler if needed, e.g.:  make CC=x86_64-w64-mingw32-gcc

# Note: plain `?=` won't override make's builtin CC=cc, so use `=`.
# Override on the command line if needed: make CC=x86_64-w64-mingw32-gcc
CC      = gcc
CSTD     = -std=c11
WARN     = -Wall -Wextra
OPT      = -O2

# Prefer sdl2-config, fall back to pkg-config.
SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null || pkg-config --cflags sdl2)
SDL_LIBS   := $(shell sdl2-config --libs   2>/dev/null || pkg-config --libs   sdl2)

CFLAGS  = $(OPT) $(CSTD) $(WARN) $(SDL_CFLAGS)
LDLIBS  = $(SDL_LIBS)

BIN  = medplay.exe
SRC  = src/main.c src/opl3.c
OBJ  = $(SRC:.c=.o)

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all run clean
