# Makefile for decompwad

CC = gcc
CFLAGS = -Wall -Wno-discarded-qualifiers -g -m32
LDFLAGS = -m32


# Detect MinGW for .exe extension
SYS := $(shell $(CC) -dumpmachine)
ifeq ($(findstring mingw,$(SYS)),mingw)
    EXEEXT := .exe
else
    EXEEXT :=
endif

CFLAGS += -funroll-loops -fexpensive-optimizations
	LDFLAGS += -lm -lgcc

SRC = \
	common.c \
	decompile.c \
	texture.c \
	wad.c \
	info.c

OBJ = $(SRC:.c=.o)

all: decompwad

# Compile the executable

decompwad: $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

# Compile object files

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up

clean:
	rm -f $(OBJ) decompwad

.PHONY: all clean 