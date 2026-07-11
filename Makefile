CC ?= gcc
BUILD_DIR ?= build
LOADER_TARGET ?= $(BUILD_DIR)/codboz_s3e_loader
EXTRACT_TARGET ?= $(BUILD_DIR)/codboz_apk_extract
PACKAGE_DIR ?= $(BUILD_DIR)/package
RELEASE_DIR ?= $(BUILD_DIR)/release
PORT_PACKAGE_DIR := $(PACKAGE_DIR)/ports/codboz
PORT_SOURCE_DIR := packaging/ports/codboz
PORT_PAYLOAD_DIR := $(PORT_PACKAGE_DIR)/codboz
ZIP := $(BUILD_DIR)/codboz.zip
CFLAGS ?= -O2 -g
PROJECT_CPPFLAGS := -D_GNU_SOURCE -Iinclude -Ithird_party/lzma
PROJECT_CFLAGS := -std=c11 -Wall -Wextra -Werror
TARGET_CFLAGS ?=
LDFLAGS ?=
LOADER_LDLIBS += -ldl -pthread

LOADER_SRC := \
  src/codboz_assets.c \
  src/main.c \
  src/s3e_config.c \
  src/s3e_audio.c \
  src/s3e_egl.c \
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
	$(CC) $(PROJECT_CPPFLAGS) $(CFLAGS) $(PROJECT_CFLAGS) $(TARGET_CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)

package: all
	rm -rf $(PACKAGE_DIR)
	mkdir -p $(PORT_PACKAGE_DIR)
	cp -R $(PORT_SOURCE_DIR)/. $(PORT_PACKAGE_DIR)/
	cp $(PORT_PACKAGE_DIR)/cover.png $(PORT_PAYLOAD_DIR)/
	cp $(LOADER_TARGET) $(PORT_PAYLOAD_DIR)/
	cp $(EXTRACT_TARGET) $(PORT_PAYLOAD_DIR)/
	chmod 755 $(PORT_PACKAGE_DIR)/CODBOZ.sh
	chmod 755 $(PORT_PAYLOAD_DIR)/codboz_setup
	chmod 755 $(PORT_PAYLOAD_DIR)/codboz_s3e_loader
	chmod 755 $(PORT_PAYLOAD_DIR)/codboz_apk_extract
	find $(PACKAGE_DIR) -name .DS_Store -delete

zip: package
	rm -rf $(RELEASE_DIR)
	rm -f $(ZIP)
	mkdir -p $(RELEASE_DIR)/codboz
	cp $(PORT_PACKAGE_DIR)/CODBOZ.sh $(RELEASE_DIR)/
	cp -R $(PORT_PAYLOAD_DIR)/. $(RELEASE_DIR)/codboz/
	cp $(PORT_PACKAGE_DIR)/port.json $(RELEASE_DIR)/codboz/
	cp $(PORT_PACKAGE_DIR)/gameinfo.xml $(RELEASE_DIR)/codboz/
	cp $(PORT_PACKAGE_DIR)/README.md $(RELEASE_DIR)/codboz/codboz.md
	cp $(PORT_PACKAGE_DIR)/cover.png $(RELEASE_DIR)/codboz/
	cp $(PORT_PACKAGE_DIR)/screenshot.png $(RELEASE_DIR)/codboz/
	find $(RELEASE_DIR) -name .DS_Store -delete
	cd $(RELEASE_DIR) && zip -qr ../codboz.zip .

-include $(LOADER_OBJ:.o=.d)
-include $(EXTRACT_OBJ:.o=.d)
.PHONY: all clean package zip
