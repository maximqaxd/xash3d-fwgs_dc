# Makefile for pvrstudiomdl

# Compiler and flags
CC = gcc
CFLAGS = -Wall -O2 -DNDEBUG -Dstricmp=strcasecmp -Dstrcmpi=strcasecmp -Dstrnicmp=strncasecmp
DEBUG_CFLAGS = -Wall -g -O0 -D_DEBUG -Dstricmp=strcasecmp -Dstrcmpi=strcasecmp -Dstrnicmp=strncasecmp
INCLUDES = -I../common

# Detect MinGW for .exe extension
SYS := $(shell $(CC) -dumpmachine)
ifeq ($(findstring mingw,$(SYS)),mingw)
    EXEEXT := .exe
else
    EXEEXT :=
endif

# Target executable
TARGET = pvrstudiomdl$(EXEEXT)
DEBUG_TARGET = pvrstudiomdl_debug$(EXEEXT)

# Source files from the Visual Studio project
COMMON_DIR = ../common
SOURCES = \
	bmpread.c \
	studiomdl.c \
	tristrip.c \
	write.c \
	$(COMMON_DIR)/cmdlib.c \
	$(COMMON_DIR)/lbmlib.c \
	$(COMMON_DIR)/mathlib.c \
	$(COMMON_DIR)/scriplib.c \
	$(COMMON_DIR)/trilib.c

# Object files
OBJECTS = $(SOURCES:.c=.o)
DEBUG_OBJECTS = $(SOURCES:.c=.debug.o)

# Default target
all: $(TARGET)

# Debug target
debug: CFLAGS = $(DEBUG_CFLAGS)
debug: $(DEBUG_TARGET)

# Build release version
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) -lm

# Build debug version
$(DEBUG_TARGET): $(DEBUG_OBJECTS)
	$(CC) $(DEBUG_OBJECTS) -o $(DEBUG_TARGET) -lm

# Compile source files (release)
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Compile source files (debug)
%.debug.o: %.c
	$(CC) $(DEBUG_CFLAGS) $(INCLUDES) -c $< -o $@

# Clean targets
clean:
	rm -f $(OBJECTS) $(DEBUG_OBJECTS) $(TARGET) $(DEBUG_TARGET)

# Install target
install: $(TARGET)
	mkdir -p ../../bin
	cp $(TARGET) ../../bin/

# Test if build works
test: $(TARGET)
	./$(TARGET) --help || echo "Built successfully (no help available)"

.PHONY: all debug clean install test