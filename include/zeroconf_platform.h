#ifndef CODBOZ_ZEROCONF_PLATFORM_H
#define CODBOZ_ZEROCONF_PLATFORM_H

#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

uint64_t zeroconf_platform_now_ms(void);
int zeroconf_platform_select_ipv4(struct in_addr *selected);
int zeroconf_platform_open_socket(struct in_addr *selected);
int zeroconf_platform_send(int socket_fd, const uint8_t *packet, size_t packet_size,
                           const struct sockaddr_in *destination);
ssize_t zeroconf_platform_receive(int socket_fd, uint8_t *packet, size_t capacity,
                                  struct sockaddr_in *source);
void zeroconf_platform_close_socket(int socket_fd);
void s3e_zero_conf_process_packet(const uint8_t *packet, size_t packet_size,
                                  const struct sockaddr_in *source);

#endif
