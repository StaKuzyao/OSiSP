# Makefile for NCurses File Manager

# Compiler settings
CC = gcc
CFLAGS = -W -Wall -Wextra -std=c11 -pedantic
LDFLAGS = -lncursesw

# Source files
SRC = main.c
EXEC = fm

# Build rules
all: $(EXEC)

$(EXEC): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Phony targets
.PHONY: clean debug release

clean:
	rm -f $(EXEC)

debug: CFLAGS += -g -DDEBUG
debug: $(EXEC)

release: CFLAGS += -O2
release: $(EXEC)