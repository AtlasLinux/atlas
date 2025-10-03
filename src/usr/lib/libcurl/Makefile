# Compiler and flags
CC := gcc
CFLAGS := -fPIC -Wall -Wextra -O2
LDFLAGS := -shared

# Directories
SRC_DIR := src
BUILD_DIR := build

# Source and object files
SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

# Target shared library
TARGET := $(BUILD_DIR)/libcurl.so

# Default target
all: $(TARGET)

# Link shared library
$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

# Compile .c to .o
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Ensure build directory exists
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all clean
