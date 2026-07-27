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
#
# Build note (mixed-toolchain environments): use the UCRT64 `mingw32-make`,
# not MSYS2's `/usr/bin/make`. The POSIX make spawns recipes under /bin/sh,
# which (a) resolves sdl2-config from whatever is first on PATH (e.g. Git's
# bundled toolchain) and (b) sets TMP=/tmp, which native gcc cannot use and
# then falls back to an unwritable C:\WINDOWS. `mingw32-make` runs recipes
# natively and avoids both problems.

# Note: plain `?=` won't override make's builtin CC=cc, so use `=`.
# Override on the command line if needed: make CC=x86_64-w64-mingw32-gcc
CC      = gcc
CSTD     = -std=c11
WARN     = -Wall -Wextra
OPT      = -O2

# Prefer sdl2-config, fall back to pkg-config.
#
# We provide our own main() (SDL_MAIN_HANDLED) and want a console subsystem app
# so SIGINT (Ctrl-C) reaches the process and stdout works. So strip SDL's
# -Dmain=SDL_main, drop -lSDL2main, and replace -mwindows with -mconsole.
SDL_CFLAGS := $(shell (sdl2-config --cflags 2>/dev/null || pkg-config --cflags sdl2) | sed 's/-Dmain=SDL_main//')
SDL_LIBS   := $(shell (sdl2-config --libs   2>/dev/null || pkg-config --libs   sdl2) | sed 's/-mwindows/-mconsole/g; s/-lSDL2main//g')

CFLAGS  = $(OPT) $(CSTD) $(WARN) $(SDL_CFLAGS)
LDLIBS  = $(SDL_LIBS)

BIN  = medplay.exe
SRC  = src/main.c src/opl3.c src/opl_dev.c src/osl1.c src/replay.c
OBJ  = $(SRC:.c=.o)

# All project headers. Every object depends on all of them: a coarse but safe
# rule so a struct-layout change (e.g. adding a field to opl_dev) forces every
# translation unit to recompile. Without this a stale object built against the
# old layout links against the new one and corrupts memory at run time.
HDR  = src/opl3.h src/opl_dev.h src/osl1.h src/replay.h

# Parser parity tool (no SDL needed): tools/osl1_dump.c + src/osl1.c
DUMP_BIN = osl1_dump.exe
DUMP_SRC = tools/osl1_dump.c src/osl1.c

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

src/%.o: src/%.c $(HDR)
	$(CC) $(CFLAGS) -c $< -o $@

# osl1_dump links only the parser; SDL flags are harmless but unused.
$(DUMP_BIN): $(DUMP_SRC)
	$(CC) $(OPT) $(CSTD) $(WARN) $^ -o $@

dump: $(DUMP_BIN)

# opl_scale: headless backend check (opl_dev + opl3, no SDL).
SCALE_BIN = opl_scale.exe
SCALE_SRC = tools/opl_scale.c src/opl_dev.c src/opl3.c
$(SCALE_BIN): $(SCALE_SRC)
	$(CC) $(OPT) $(CSTD) $(WARN) $^ -o $@

scale: $(SCALE_BIN)

# decode_dump: headless replay-decoder validation (osl1 + replay + opl_dev + opl3, no SDL).
DECODE_BIN = decode_dump.exe
DECODE_SRC = tools/decode_dump.c src/osl1.c src/replay.c src/opl_dev.c src/opl3.c
$(DECODE_BIN): $(DECODE_SRC)
	$(CC) $(OPT) $(CSTD) $(WARN) $^ -o $@

decode: $(DECODE_BIN)

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(OBJ) $(BIN) $(DUMP_BIN) $(SCALE_BIN) $(DECODE_BIN)

.PHONY: all run dump scale decode clean
