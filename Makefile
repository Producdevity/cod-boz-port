CC ?= gcc
BUILD_DIR ?= build
TARGET ?= $(BUILD_DIR)/codboz_s3e_loader
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -Iinclude
LDFLAGS ?=
LDLIBS += -ldl -pthread

SRC := \
  src/main.c \
  src/s3e_config.c \
  src/s3e_file.c \
  src/s3e_gl.c \
  src/s3e_host.c \
  src/s3e_image.c \
  src/s3e_input.c \
  src/s3e_runtime.c
OBJ := $(SRC:src/%.c=$(BUILD_DIR)/%.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)

-include $(OBJ:.o=.d)
.PHONY: all clean
