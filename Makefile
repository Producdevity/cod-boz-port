CC ?= gcc
BUILD_DIR ?= build
LOADER_TARGET ?= $(BUILD_DIR)/codboz_s3e_loader
EXTRACT_TARGET ?= $(BUILD_DIR)/codboz_apk_extract
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -Iinclude -Ithird_party/lzma
LDFLAGS ?=
LOADER_LDLIBS += -ldl -pthread

LOADER_SRC := \
  src/main.c \
  src/s3e_config.c \
  src/s3e_file.c \
  src/s3e_gl.c \
  src/s3e_host.c \
  src/s3e_image.c \
  src/s3e_input.c \
  src/s3e_runtime.c
LOADER_OBJ := $(LOADER_SRC:%.c=$(BUILD_DIR)/%.o)

EXTRACT_SRC := \
  tools/apk_extract/codboz_apk_extract.c \
  third_party/lzma/LzmaDec.c
EXTRACT_OBJ := $(EXTRACT_SRC:%.c=$(BUILD_DIR)/%.o)

all: $(LOADER_TARGET) $(EXTRACT_TARGET)

$(LOADER_TARGET): $(LOADER_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LOADER_LDLIBS)

$(EXTRACT_TARGET): $(EXTRACT_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)

-include $(LOADER_OBJ:.o=.d)
-include $(EXTRACT_OBJ:.o=.d)
.PHONY: all clean
