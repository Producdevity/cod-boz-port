#ifndef CODBOZ_ZEROCONF_PLATFORM_FAKE_H
#define CODBOZ_ZEROCONF_PLATFORM_FAKE_H

#include <stddef.h>
#include <stdint.h>

void zeroconf_platform_fake_reset(void);
void zeroconf_platform_fake_advance(uint64_t milliseconds);
size_t zeroconf_platform_fake_sent_count(void);
int zeroconf_platform_fake_sent_packet(size_t index, uint8_t *packet, size_t capacity,
                                       size_t *packet_size);

#endif
