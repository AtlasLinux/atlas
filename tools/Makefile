# Compiler and flags
CC := gcc
CFLAGS := -O2 -Wall -Wextra -std=c17

# Find all main.c files under src/
MAINS := $(shell find src -type f -name main.c)

# Derive executable paths under build/, keeping directory layout
# Example: src/foo/bar/main.c -> build/foo/bar
TARGETS := $(patsubst src/%/main.c, build/%,$(MAINS))

.PHONY: all clean

all: $(TARGETS)

# Build rule: compile each main.c to an executable
build/%: src/%/main.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) src/$*/*.c -o $@

clean:
	rm -rf build
