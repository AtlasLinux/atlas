# Directories
SRC_DIR := src
BUILD_DIR := build

CC := gcc
CFLAGS := -I../../include -L../../lib -lwayland-client -lwayland-server

# Find all .c source files in SRC_DIR
SRCS := $(wildcard $(SRC_DIR)/*.c)

# Generate list of binaries in BUILD_DIR by replacing src/ with bin/ and .c with nothing
BINS := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%, $(SRCS))

# Default target
all: $(BINS)

# Rule to build each binary
#$(BUILD_DIR)/%: $(SRC_DIR)/%.c | $(BUILD_DIR)
#	$(CC) $(CFLAGS) -o $@ $<

# Create bin directory if it doesn't exist
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# Clean target
clean:
	@rm -rf $(BUILD_DIR)

.PHONY: all clean
