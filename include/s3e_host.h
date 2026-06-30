#ifndef CODBOZ_S3E_HOST_H
#define CODBOZ_S3E_HOST_H

#include <stdbool.h>
#include <stdint.h>

bool s3e_host_init(const char *root);
void s3e_host_set_config(const uint8_t *data, uint32_t size);
void s3e_host_shutdown(void);
void *s3e_host_resolve(const char *symbol);

#endif
