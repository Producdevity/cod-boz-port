#include "s3e_host_internal.h"
#include "s3e_image.h"

enum {
    PLAYER_NAME_REFERENCE_OFFSET = 0x18f74c,
    PLAYER_NAME_REFERENCE_PC_OFFSET = 0x18f60a,
    PLAYER_NAME_FORMAT_OFFSET = 0x3af131,
};

/* BOZ 1.0.11 builds its generic account name from this Player-%d reference. */
bool codboz_override_player_name(struct s3e_loaded_image *loaded, uint32_t player_name_address) {
    static const char format[] = "Player-%d";
    if (!loaded || !loaded->base || loaded->map_size < PLAYER_NAME_FORMAT_OFFSET + sizeof(format)) {
        return false;
    }

    uint8_t *reference = loaded->base + PLAYER_NAME_REFERENCE_OFFSET;
    uint32_t relative;
    memcpy(&relative, reference, sizeof(relative));
    if (relative != PLAYER_NAME_FORMAT_OFFSET - PLAYER_NAME_REFERENCE_PC_OFFSET ||
        memcmp(loaded->base + PLAYER_NAME_FORMAT_OFFSET, format, sizeof(format)) != 0) {
        return false;
    }

    uint32_t pc = (uint32_t)(uintptr_t)loaded->base + PLAYER_NAME_REFERENCE_PC_OFFSET;
    relative = player_name_address - pc;
    memcpy(reference, &relative, sizeof(relative));
    return true;
}

enum {
    HUD_ATLAS_WIDTH = 1024,
    HUD_ATLAS_HEIGHT = 512,
    HUD_ATLAS_BYTES_PER_PIXEL = 2,
};

struct hud_atlas_layout {
    size_t group_size;
    size_t pixel_offset;
};

struct hud_rect {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
};

void codboz_hide_virtual_stick_artwork(const char *name, uint8_t *data, size_t size) {
    static const struct hud_atlas_layout layouts[] = {
        {17446625u, 0x5d898cu},
        {12372273u, 0x41b8e4u},
    };
    static const uint8_t atlas_header[] = {
        0x00, 0x20, 0x00, 0x10, 0x00, 0xbe, 0x32, 0x9b, 0x61, 0x0c, 0x43, 0x24, 0x00, 0x00,
        0x06, 0x00, 0x10, 0x00, 0x10, 0x05, 0x00, 0x10, 0x00, 0x04, 0x00, 0x02, 0x00, 0x08,
    };
    static const struct hud_rect stick_rects[] = {
        {0, 0, 257, 236},
        {257, 0, 92, 72},
        {625, 431, 80, 80},
    };

    const char *slash = strrchr(name ? name : "", '/');
    const char *base = slash ? slash + 1 : (name ? name : "");
    if (strcasecmp(base, "ingame.group.bin") != 0) {
        return;
    }

    size_t atlas_size = HUD_ATLAS_WIDTH * HUD_ATLAS_HEIGHT * HUD_ATLAS_BYTES_PER_PIXEL;
    uint8_t *pixels = NULL;
    for (size_t i = 0; i < sizeof(layouts) / sizeof(layouts[0]); ++i) {
        size_t offset = layouts[i].pixel_offset;
        if (size == layouts[i].group_size && offset >= sizeof(atlas_header) &&
            offset + atlas_size <= size &&
            memcmp(data + offset - sizeof(atlas_header), atlas_header, sizeof(atlas_header)) == 0) {
            pixels = data + offset;
            break;
        }
    }
    if (!pixels) {
        return;
    }

    /* The atlas is RGBA4444; alpha is the low nibble of the first byte. */
    for (size_t i = 0; i < sizeof(stick_rects) / sizeof(stick_rects[0]); ++i) {
        const struct hud_rect *rect = &stick_rects[i];
        for (size_t y = rect->y; y < (size_t)rect->y + rect->height; ++y) {
            uint8_t *row = pixels + (y * HUD_ATLAS_WIDTH + rect->x) * HUD_ATLAS_BYTES_PER_PIXEL;
            for (size_t x = 0; x < rect->width; ++x) {
                row[x * HUD_ATLAS_BYTES_PER_PIXEL] &= 0xf0u;
            }
        }
    }
}
