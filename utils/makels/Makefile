# Makefile for makels under POSIX using GCC

CC = g++
CFLAGS = -Wall -Wextra -O2
LDFLAGS =

SRC = makels.cpp
OBJ = $(SRC:.cpp=.o)

OUTDIR = ./
BINDIR = $(OUTDIR)

all: $(BINDIR)/makels

$(BINDIR):
	@mkdir -p $(BINDIR)

$(BINDIR)/makels: $(OBJ) | $(BINDIR)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o $(BINDIR)/makels

.PHONY: all clean 