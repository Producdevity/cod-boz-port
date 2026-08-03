CC ?= gcc
HOST_CC ?= cc
CLANG_FORMAT ?= clang-format
SHELLCHECK ?= shellcheck
HOST_TEST_CFLAGS ?= -O0 -g
HOST_TEST_LDFLAGS ?=
BUILD_DIR ?= build
LOADER_TARGET ?= $(BUILD_DIR)/codboz_s3e_loader
EXTRACT_TARGET ?= $(BUILD_DIR)/codboz_apk_extract
PACKAGE_DIR ?= $(BUILD_DIR)/package
RELEASE_DIR ?= $(BUILD_DIR)/release
PORT_PACKAGE_DIR := $(PACKAGE_DIR)/ports/codboz
PORT_SOURCE_DIR := packaging/ports/codboz
PORT_PAYLOAD_DIR := $(PORT_PACKAGE_DIR)/codboz
RELEASE_ARCHIVE ?= $(abspath $(BUILD_DIR)/codboz.zip)
ARCHIVE_TIMESTAMP ?= 200001010000
HOST_TESTS := \
  $(BUILD_DIR)/tests/s3e_socket_test \
  $(BUILD_DIR)/tests/s3e_config_test \
  $(BUILD_DIR)/tests/mdns_wire_test \
  $(BUILD_DIR)/tests/s3e_zeroconf_test \
  $(BUILD_DIR)/tests/s3e_timer_test \
  $(BUILD_DIR)/tests/s3e_memory_test \
  $(BUILD_DIR)/tests/device_id_test \
  $(BUILD_DIR)/tests/s3e_audio_test \
  $(BUILD_DIR)/tests/s3e_audio_unit_test
CFLAGS ?= -O2 -g
PROJECT_CPPFLAGS := -D_GNU_SOURCE -Iinclude -Ithird_party/lzma
PROJECT_CFLAGS := -std=c11 -Wall -Wextra -Werror
TARGET_CFLAGS ?=
LDFLAGS ?=
LOADER_LDLIBS += -ldl -pthread
FORMAT_FILES := $(shell find include src tests tools -type f \( -name '*.c' -o -name '*.h' \) | sort)
SHELL_FILES := $(shell find scripts packaging tests -type f -name '*.sh' | sort) \
  packaging/ports/codboz/codboz/codboz_setup

LOADER_SRC := \
  src/codboz_assets.c \
  src/main.c \
  src/s3e_config.c \
  src/s3e_audio.c \
  src/s3e_audio_unit.c \
  src/s3e_egl.c \
  src/s3e_file.c \
  src/s3e_gl.c \
  src/s3e_host.c \
  src/s3e_image.c \
  src/s3e_input.c \
  src/s3e_memory.c \
  src/device_id.c \
  src/s3e_runtime.c \
  src/s3e_timer.c \
  src/s3e_socket.c \
  src/mdns_wire.c \
  src/zeroconf_platform_posix.c \
  src/s3e_zeroconf.c
LOADER_OBJ := $(LOADER_SRC:%.c=$(BUILD_DIR)/%.o)

EXTRACT_SRC := \
  tools/apk_extract/codboz_apk_extract.c \
  third_party/lzma/LzmaDec.c
EXTRACT_OBJ := $(EXTRACT_SRC:%.c=$(BUILD_DIR)/%.o)

all: $(LOADER_TARGET) $(EXTRACT_TARGET)

check: format-check shellcheck test-host

format:
	$(CLANG_FORMAT) -i $(FORMAT_FILES)

format-check:
	$(CLANG_FORMAT) --dry-run --Werror $(FORMAT_FILES)

shellcheck:
	$(SHELLCHECK) $(SHELL_FILES)

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
	rm -f $(RELEASE_ARCHIVE)
	mkdir -p $(RELEASE_DIR)/codboz
	cp $(PORT_PACKAGE_DIR)/CODBOZ.sh $(RELEASE_DIR)/
	cp -R $(PORT_PAYLOAD_DIR)/. $(RELEASE_DIR)/codboz/
	cp $(PORT_PACKAGE_DIR)/port.json $(RELEASE_DIR)/codboz/
	cp $(PORT_PACKAGE_DIR)/gameinfo.xml $(RELEASE_DIR)/codboz/
	cp $(PORT_PACKAGE_DIR)/README.md $(RELEASE_DIR)/codboz/codboz.md
	cp $(PORT_PACKAGE_DIR)/cover.png $(RELEASE_DIR)/codboz/
	cp $(PORT_PACKAGE_DIR)/screenshot.png $(RELEASE_DIR)/codboz/
	find $(RELEASE_DIR) -name .DS_Store -delete
	find $(RELEASE_DIR) -exec touch -t $(ARCHIVE_TIMESTAMP) {} +
	cd $(RELEASE_DIR) && LC_ALL=C find . -mindepth 1 -print | LC_ALL=C sort | \
	  zip -Xq "$(RELEASE_ARCHIVE)" -@

$(BUILD_DIR)/tests/s3e_socket_test: tests/s3e_socket_test.c src/s3e_socket.c src/s3e_config.c \
                     include/s3e_host_internal.h | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(HOST_CC) $(PROJECT_CPPFLAGS) $(HOST_TEST_CFLAGS) \
	  $(PROJECT_CFLAGS) -pthread \
	  -o $@ tests/s3e_socket_test.c src/s3e_socket.c src/s3e_config.c \
	  $(HOST_TEST_LDFLAGS)

$(BUILD_DIR)/tests/s3e_config_test: tests/s3e_config_test.c src/s3e_config.c src/codboz_assets.c \
                     include/s3e_host_internal.h include/s3e_image.h | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(HOST_CC) $(PROJECT_CPPFLAGS) $(HOST_TEST_CFLAGS) $(PROJECT_CFLAGS) \
	  -o $@ tests/s3e_config_test.c src/s3e_config.c src/codboz_assets.c \
	  $(HOST_TEST_LDFLAGS)

$(BUILD_DIR)/tests/mdns_wire_test: tests/mdns_wire_test.c src/mdns_wire.c \
                                   include/mdns_wire.h | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(HOST_CC) $(PROJECT_CPPFLAGS) $(HOST_TEST_CFLAGS) $(PROJECT_CFLAGS) -o $@ \
	  tests/mdns_wire_test.c src/mdns_wire.c $(HOST_TEST_LDFLAGS)

$(BUILD_DIR)/tests/s3e_zeroconf_test: tests/s3e_zeroconf_test.c \
                                       tests/zeroconf_platform_fake.c \
                                       src/s3e_zeroconf.c src/mdns_wire.c \
                                       include/s3e_host_internal.h include/mdns_wire.h \
                                       include/zeroconf_platform.h | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(HOST_CC) $(PROJECT_CPPFLAGS) -Itests $(HOST_TEST_CFLAGS) \
	  $(PROJECT_CFLAGS) -o $@ tests/s3e_zeroconf_test.c \
	  tests/zeroconf_platform_fake.c src/s3e_zeroconf.c src/mdns_wire.c \
	  $(HOST_TEST_LDFLAGS)

$(BUILD_DIR)/tests/s3e_timer_test: tests/s3e_timer_test.c src/s3e_timer.c \
                                    include/s3e_host_internal.h | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(HOST_CC) $(PROJECT_CPPFLAGS) $(HOST_TEST_CFLAGS) $(PROJECT_CFLAGS) -pthread \
	  -o $@ tests/s3e_timer_test.c src/s3e_timer.c $(HOST_TEST_LDFLAGS)

$(BUILD_DIR)/tests/s3e_memory_test: tests/s3e_memory_test.c src/s3e_memory.c \
                                     include/s3e_host_internal.h | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(HOST_CC) $(PROJECT_CPPFLAGS) $(HOST_TEST_CFLAGS) $(PROJECT_CFLAGS) -pthread \
	  -o $@ tests/s3e_memory_test.c src/s3e_memory.c $(HOST_TEST_LDFLAGS)

$(BUILD_DIR)/tests/device_id_test: tests/device_id_test.c src/device_id.c \
                                  include/s3e_host_internal.h | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(HOST_CC) $(PROJECT_CPPFLAGS) $(HOST_TEST_CFLAGS) $(PROJECT_CFLAGS) -pthread \
	  -o $@ tests/device_id_test.c src/device_id.c $(HOST_TEST_LDFLAGS)

$(BUILD_DIR)/tests/s3e_audio_test: tests/s3e_audio_test.c src/s3e_audio.c \
                                    include/s3e_host_internal.h | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(HOST_CC) $(PROJECT_CPPFLAGS) -Ddlsym=s3e_audio_test_dlsym \
	  -Ddlclose=s3e_audio_test_dlclose $(HOST_TEST_CFLAGS) $(PROJECT_CFLAGS) \
	  -o $@ tests/s3e_audio_test.c src/s3e_audio.c $(HOST_TEST_LDFLAGS)

$(BUILD_DIR)/tests/s3e_audio_unit_test: tests/s3e_audio_unit_test.c src/s3e_audio_unit.c \
                                         include/s3e_host_internal.h | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(HOST_CC) $(PROJECT_CPPFLAGS) $(HOST_TEST_CFLAGS) $(PROJECT_CFLAGS) \
	  -o $@ tests/s3e_audio_unit_test.c src/s3e_audio_unit.c $(HOST_TEST_LDFLAGS)

test-host: $(HOST_TESTS)
	@set -e; for test in $(HOST_TESTS); do "$$test"; done
	tests/portmaster_launcher_test.sh

test-host-sanitize:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/sanitize \
	  HOST_TEST_CFLAGS="-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined" \
	  HOST_TEST_LDFLAGS="-fsanitize=address,undefined" test-host

-include $(LOADER_OBJ:.o=.d)
-include $(EXTRACT_OBJ:.o=.d)
.PHONY: all check clean format format-check package shellcheck test-host test-host-sanitize zip
