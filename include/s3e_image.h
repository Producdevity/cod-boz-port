#ifndef CODBOZ_S3E_IMAGE_H
#define CODBOZ_S3E_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct s3e_header {
    uint32_t ident;
    uint32_t version;
    uint16_t flags;
    uint16_t arch;
    uint32_t fixup_offset;
    uint32_t fixup_size;
    uint32_t code_offset;
    uint32_t code_file_size;
    uint32_t code_mem_size;
    uint32_t sig_offset;
    uint32_t sig_size;
    uint32_t entry_offset;
    uint32_t config_offset;
    uint32_t config_size;
    uint32_t base_addr_orig;
    uint32_t extra_offset;
    uint32_t extra_size;
    uint32_t ext_header_size;
    uint32_t data_offset;
    uint32_t is_juice;
};

struct s3e_symbol_table {
    char **items;
    size_t count;
};

struct s3e_image {
    uint8_t *file_data;
    size_t file_size;
    struct s3e_header header;
    struct s3e_symbol_table symbols;
};

struct s3e_loaded_image {
    uint8_t *base;
    size_t map_size;
    uint32_t entry_offset;
};

bool s3e_image_load(const char *path, struct s3e_image *image);
void s3e_image_free(struct s3e_image *image);
bool s3e_image_parse_symbols(struct s3e_image *image);
bool s3e_image_map_and_relocate(const struct s3e_image *image, void *(*resolve)(const char *symbol),
                                struct s3e_loaded_image *loaded);
bool codboz_override_player_name(struct s3e_loaded_image *loaded, uint32_t player_name_address);
void s3e_loaded_image_unmap(struct s3e_loaded_image *loaded);

#endif
