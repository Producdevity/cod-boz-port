#include "zeroconf_platform.h"

#include "zeroconf_platform_fake.h"

#include <arpa/inet.h>
#include <string.h>

enum {
    TEST_MAX_PACKETS = 128,
    TEST_PACKET_SIZE = 2048,
};

struct sent_packet {
    uint8_t data[TEST_PACKET_SIZE];
    size_t size;
};

static struct sent_packet g_sent_packets[TEST_MAX_PACKETS];
static size_t g_sent_count;
static uint64_t g_now_ms = 1000;

uint64_t zeroconf_platform_now_ms(void) {
    return g_now_ms;
}

int zeroconf_platform_select_ipv4(struct in_addr *selected) {
    if (!selected) {
        return 0;
    }
    selected->s_addr = htonl(0xc0000228u);
    return 1;
}

int zeroconf_platform_open_socket(struct in_addr *selected) {
    return zeroconf_platform_select_ipv4(selected) ? 1 : -1;
}

int zeroconf_platform_send(int socket_fd, const uint8_t *packet, size_t packet_size,
                           const struct sockaddr_in *destination) {
    (void)socket_fd;
    (void)destination;
    if (!packet || !packet_size || packet_size > TEST_PACKET_SIZE ||
        g_sent_count >= TEST_MAX_PACKETS) {
        return 0;
    }
    struct sent_packet *sent = &g_sent_packets[g_sent_count++];
    memcpy(sent->data, packet, packet_size);
    sent->size = packet_size;
    return 1;
}

ssize_t zeroconf_platform_receive(int socket_fd, uint8_t *packet, size_t capacity,
                                  struct sockaddr_in *source) {
    (void)socket_fd;
    (void)packet;
    (void)capacity;
    (void)source;
    return -1;
}

void zeroconf_platform_close_socket(int socket_fd) {
    (void)socket_fd;
}

void zeroconf_platform_fake_reset(void) {
    memset(g_sent_packets, 0, sizeof(g_sent_packets));
    g_sent_count = 0;
    g_now_ms = 1000;
}

void zeroconf_platform_fake_advance(uint64_t milliseconds) {
    g_now_ms += milliseconds;
}

size_t zeroconf_platform_fake_sent_count(void) {
    return g_sent_count;
}

int zeroconf_platform_fake_sent_packet(size_t index, uint8_t *packet, size_t capacity,
                                       size_t *packet_size) {
    if (!packet || !packet_size || index >= g_sent_count || g_sent_packets[index].size > capacity) {
        return 0;
    }
    *packet_size = g_sent_packets[index].size;
    memcpy(packet, g_sent_packets[index].data, *packet_size);
    return 1;
}
