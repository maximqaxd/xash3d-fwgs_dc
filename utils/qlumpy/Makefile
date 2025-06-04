# Makefile for qlumpy under POSIX using GCC

CC = gcc
CFLAGS = -Wall -Wextra -O2 -I../pvrstudiomdl/utils/common
LDFLAGS =

SRC = \
	cmdlib.c \
	lbmlib.c \
	qlumpy.c \
	quakegrb.c \
	scriplib.c \
	wadlib.c

OBJ = $(SRC:.c=.o)

OUTDIR = ./
BINDIR = $(OUTDIR)

all: $(BINDIR)/qlumpy

$(BINDIR):
	@mkdir -p $(BINDIR)

$(BINDIR)/qlumpy: $(OBJ) | $(BINDIR)
	$(CC) -o $@ $^ -lm

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(BINDIR)/qlumpy

.PHONY: all clean 