#include "mdns_wire.h"
#include "s3e_host_internal.h"
#include "zeroconf_platform.h"
#include "zeroconf_platform_fake.h"

#include <assert.h>

enum {
    TEST_PACKET_SIZE = 2048,
    TEST_RESULT_SUCCESS = 0,
    TEST_RESULT_ERROR = 1,
};

struct callback_state {
    void *search;
    void *service_id;
    uint32_t ipv4_address;
    uint16_t port;
    uint16_t txt_count;
    char name[MDNS_WIRE_MAX_NAME];
    char txt[256];
    int found_count;
    int update_count;
    int lost_count;
};

static int32_t found_callback(void *search, void *system_data, void *user_data) {
    struct callback_state *state = user_data;
    const struct s3e_zeroconf_found_data *found = system_data;
    assert(search == state->search);
    assert(found && found->service_id && found->name && found->host);
    state->service_id = found->service_id;
    state->ipv4_address = found->ipv4_address;
    state->port = found->port;
    state->txt_count = found->txt_count;
    snprintf(state->name, sizeof(state->name), "%s", found->name);
    state->txt[0] = 0;
    if (found->txt_count) {
        assert(found->txt_records && found->txt_records[0]);
        snprintf(state->txt, sizeof(state->txt), "%s", found->txt_records[0]);
    }
    ++state->found_count;
    return 0;
}

static int32_t update_callback(void *search, void *system_data, void *user_data) {
    struct callback_state *state = user_data;
    const struct s3e_zeroconf_txt_update_data *update = system_data;
    assert(search == state->search);
    assert(update && update->service_id == state->service_id);
    state->txt_count = update->txt_count;
    state->txt[0] = 0;
    if (update->txt_count) {
        assert(update->txt_records && update->txt_records[0]);
        snprintf(state->txt, sizeof(state->txt), "%s", update->txt_records[0]);
    }
    ++state->update_count;
    return 0;
}

static int32_t lost_callback(void *search, void *system_data, void *user_data) {
    struct callback_state *state = user_data;
    assert(search == state->search);
    assert(system_data == state->service_id);
    ++state->lost_count;
    return 0;
}

static size_t build_service_packet(uint8_t *packet, size_t capacity, uint16_t port,
                                   const char *txt_value, uint32_t ttl, bool goodbye) {
    struct mdns_wire_txt_string txt = {
        .data = (const uint8_t *)txt_value,
        .length = strlen(txt_value),
    };
    struct mdns_wire_service service = {
        .service_type = "_PROJECT_KIWI._tcp",
        .domain = "local",
        .instance = "Kino Lobby",
        .host = "codboz-peer",
        .port = port,
        .ipv4 = {192, 168, 50, 23},
        .txt = &txt,
        .txt_count = 1,
    };
    struct mdns_wire_response_options options = {
        .header_flags = MDNS_WIRE_RESPONSE_FLAGS,
        .ttl = ttl,
        .cache_flush = !goodbye,
        .goodbye = goodbye,
    };
    size_t packet_size = 0;
    assert(mdns_wire_build_service_response(packet, capacity, &service, &options, &packet_size) ==
           MDNS_WIRE_OK);
    return packet_size;
}

static struct mdns_wire_packet parse_sent_packet(size_t index) {
    uint8_t packet[TEST_PACKET_SIZE];
    size_t packet_size = 0;
    assert(zeroconf_platform_fake_sent_packet(index, packet, sizeof(packet), &packet_size));
    struct mdns_wire_packet parsed;
    assert(mdns_wire_parse_packet(packet, packet_size, &parsed) == MDNS_WIRE_OK);
    return parsed;
}

static const struct mdns_wire_record *find_record(const struct mdns_wire_packet *packet,
                                                  enum mdns_wire_rdata_kind kind) {
    for (size_t i = 0; i < packet->record_count; ++i) {
        if (packet->records[i].data_kind == kind) {
            return &packet->records[i];
        }
    }
    return NULL;
}

static void assert_sent_txt(size_t index, const char *expected) {
    struct mdns_wire_packet packet = parse_sent_packet(index);
    const struct mdns_wire_record *record = find_record(&packet, MDNS_WIRE_RDATA_TXT);
    assert(record);
    const uint8_t *text = NULL;
    size_t length = 0;
    assert(mdns_wire_txt_at(record, 0, &text, &length));
    assert(length == strlen(expected));
    assert(memcmp(text, expected, length) == 0);
}

static void test_discovery_lifecycle(void) {
    zeroconf_platform_fake_reset();
    struct callback_state state = {0};
    state.search = s3eZeroConfStartSearch(
        "_PROJECT_KIWI._tcp", NULL, (void *)(uintptr_t)found_callback,
        (void *)(uintptr_t)update_callback, (void *)(uintptr_t)lost_callback, &state);
    assert(state.search);
    assert(zeroconf_platform_fake_sent_count() == 1);

    uint8_t packet[TEST_PACKET_SIZE];
    size_t packet_size =
        build_service_packet(packet, sizeof(packet), 32145, "version=1", 120, false);
    s3e_zero_conf_process_packet(packet, packet_size, NULL);
    assert(state.found_count == 1);
    assert(state.update_count == 0);
    assert(state.lost_count == 0);
    assert(strcmp(state.name, "Kino Lobby") == 0);
    assert(strcmp(state.txt, "version=1") == 0);
    assert(state.port == htons(32145));
    const uint8_t expected_ipv4[] = {192, 168, 50, 23};
    assert(memcmp(&state.ipv4_address, expected_ipv4, sizeof(expected_ipv4)) == 0);

    s3e_zero_conf_process_packet(packet, packet_size, NULL);
    assert(state.found_count == 1 && state.update_count == 0);

    packet_size = build_service_packet(packet, sizeof(packet), 32145, "version=2", 120, false);
    s3e_zero_conf_process_packet(packet, packet_size, NULL);
    assert(state.update_count == 1);
    assert(strcmp(state.txt, "version=2") == 0);

    void *first_service_id = state.service_id;
    packet_size = build_service_packet(packet, sizeof(packet), 32146, "version=2", 120, false);
    s3e_zero_conf_process_packet(packet, packet_size, NULL);
    assert(state.lost_count == 1);
    assert(state.found_count == 2);
    assert(state.service_id != first_service_id);
    assert(state.port == htons(32146));

    packet_size = build_service_packet(packet, sizeof(packet), 32146, "version=2", 0, true);
    s3e_zero_conf_process_packet(packet, packet_size, NULL);
    assert(state.lost_count == 2);

    s3eZeroConfStopSearch(state.search);
    s3e_zero_conf_shutdown();
}

static void test_publish_lifecycle(void) {
    zeroconf_platform_fake_reset();
    const char *txt[] = {"version=1"};
    void *publication =
        s3eZeroConfPublish(htons(10), "Kino Lobby", "_PROJECT_KIWI._tcp", NULL, 1, txt);
    assert(publication);
    assert(zeroconf_platform_fake_sent_count() == 1);

    struct mdns_wire_packet announcement = parse_sent_packet(0);
    const struct mdns_wire_record *srv = find_record(&announcement, MDNS_WIRE_RDATA_SRV);
    const struct mdns_wire_record *address = find_record(&announcement, MDNS_WIRE_RDATA_A);
    assert(srv && srv->data.srv.port == 10);
    const uint8_t expected_ipv4[] = {192, 0, 2, 40};
    assert(address && memcmp(address->data.a.address, expected_ipv4, sizeof(expected_ipv4)) == 0);
    assert_sent_txt(0, "version=1");

    assert(s3eZeroConfUpdateTxtRecord(publication, 1, txt) == TEST_RESULT_SUCCESS);
    s3e_zero_conf_pump();
    assert(zeroconf_platform_fake_sent_count() == 1);

    const char *updated[] = {"version=2"};
    assert(s3eZeroConfUpdateTxtRecord(publication, 1, updated) == TEST_RESULT_SUCCESS);
    zeroconf_platform_fake_advance(999);
    s3e_zero_conf_pump();
    assert(zeroconf_platform_fake_sent_count() == 1);
    zeroconf_platform_fake_advance(1);
    s3e_zero_conf_pump();
    assert(zeroconf_platform_fake_sent_count() == 2);
    assert_sent_txt(1, "version=2");

    assert(s3eZeroConfUnpublish(publication) == TEST_RESULT_SUCCESS);
    assert(zeroconf_platform_fake_sent_count() == 3);
    assert(s3eZeroConfUnpublish(publication) == TEST_RESULT_ERROR);
    assert(!s3eZeroConfPublish(0, "Bad", "_PROJECT_KIWI._tcp", NULL, 1, txt));
    s3e_zero_conf_shutdown();
}

int main(void) {
    test_discovery_lifecycle();
    test_publish_lifecycle();
    puts("s3e ZeroConf tests passed");
    return 0;
}
