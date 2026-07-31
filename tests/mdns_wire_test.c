#include "mdns_wire.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const struct mdns_wire_record *find_record(const struct mdns_wire_packet *packet,
                                                  uint16_t type) {
    for (size_t i = 0; i < packet->record_count; ++i) {
        if (packet->records[i].type == type) {
            return &packet->records[i];
        }
    }
    return NULL;
}

static void test_name_codec(void) {
    uint8_t packet[128] = {0};
    size_t offset = 0;
    assert(mdns_wire_encode_name(packet, sizeof(packet), &offset, "_PROJECT_KIWI._tcp.local.") ==
           MDNS_WIRE_OK);
    size_t encoded_length = offset;
    char decoded[MDNS_WIRE_MAX_NAME];
    offset = 0;
    assert(mdns_wire_decode_name(packet, encoded_length, &offset, decoded, sizeof(decoded)) ==
           MDNS_WIRE_OK);
    assert(strcmp(decoded, "_PROJECT_KIWI._tcp.local") == 0);
    assert(offset == encoded_length);

    offset = 0;
    assert(mdns_wire_encode_name(packet, sizeof(packet), &offset, ".") == MDNS_WIRE_OK);
    size_t decode_offset = 0;
    assert(mdns_wire_decode_name(packet, offset, &decode_offset, decoded, sizeof(decoded)) ==
           MDNS_WIRE_OK);
    assert(strcmp(decoded, ".") == 0);
}

static void test_ptr_query(void) {
    uint8_t data[512];
    size_t length;
    assert(mdns_wire_build_ptr_query(data, sizeof(data), "_PROJECT_KIWI._tcp", "local", false,
                                     &length) == MDNS_WIRE_OK);
    struct mdns_wire_packet packet;
    assert(mdns_wire_parse_packet(data, length, &packet) == MDNS_WIRE_OK);
    assert(packet.flags == 0);
    assert(packet.question_count == 1);
    assert(packet.record_count == 0);
    assert(strcmp(packet.questions[0].name, "_PROJECT_KIWI._tcp.local") == 0);
    assert(packet.questions[0].type == MDNS_WIRE_TYPE_PTR);
    assert(packet.questions[0].class_code == MDNS_WIRE_CLASS_IN);
    assert(!packet.questions[0].unicast_response);

    assert(mdns_wire_build_ptr_query(data, sizeof(data), "_PROJECT_KIWI._tcp", "local", true,
                                     &length) == MDNS_WIRE_OK);
    assert(mdns_wire_parse_packet(data, length, &packet) == MDNS_WIRE_OK);
    assert(packet.questions[0].unicast_response);
}

static void test_record_queries(void) {
    const struct {
        const char *name;
        uint16_t type;
    } cases[] = {
        {"Kino Lobby._PROJECT_KIWI._tcp.local", MDNS_WIRE_TYPE_SRV},
        {"Kino Lobby._PROJECT_KIWI._tcp.local", MDNS_WIRE_TYPE_TXT},
        {"codboz-peer.local", MDNS_WIRE_TYPE_A},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        uint8_t data[512];
        size_t length = 0;
        assert(mdns_wire_build_query(data, sizeof(data), cases[i].name, cases[i].type, false,
                                     &length) == MDNS_WIRE_OK);
        struct mdns_wire_packet packet;
        assert(mdns_wire_parse_packet(data, length, &packet) == MDNS_WIRE_OK);
        assert(packet.flags == 0 && packet.question_count == 1);
        assert(strcmp(packet.questions[0].name, cases[i].name) == 0);
        assert(packet.questions[0].type == cases[i].type);
        assert(packet.questions[0].class_code == MDNS_WIRE_CLASS_IN);
        assert(!packet.questions[0].unicast_response);
    }

    uint8_t data[32];
    size_t length = 123;
    assert(mdns_wire_build_query(data, sizeof(data), "bad..name", MDNS_WIRE_TYPE_A, false,
                                 &length) == MDNS_WIRE_INVALID_ARGUMENT);
    assert(length == 0);
    assert(mdns_wire_build_query(data, sizeof(data), "host.local", 0, false, &length) ==
           MDNS_WIRE_INVALID_ARGUMENT);
}

static void test_service_response(void) {
    static const uint8_t txt_version[] = "version=1";
    static const uint8_t txt_mode[] = {'m', 'o', 'd', 'e', '=', 'c', 'o', 'o', 'p'};
    const struct mdns_wire_txt_string txt[] = {
        {.data = txt_version, .length = sizeof(txt_version) - 1},
        {.data = txt_mode, .length = sizeof(txt_mode)},
        {.data = NULL, .length = 0},
    };
    const struct mdns_wire_service service = {
        .service_type = "_PROJECT_KIWI._tcp",
        .domain = "local",
        .instance = "Kino Lobby",
        .host = "codboz-rg35xx",
        .port = 32145,
        .ipv4 = {192, 168, 1, 44},
        .txt = txt,
        .txt_count = sizeof(txt) / sizeof(txt[0]),
    };
    const struct mdns_wire_response_options options = {
        .header_flags = MDNS_WIRE_RESPONSE_FLAGS,
        .ttl = 120,
        .cache_flush = true,
    };

    uint8_t data[1500];
    size_t length;
    assert(mdns_wire_build_service_response(data, sizeof(data), &service, &options, &length) ==
           MDNS_WIRE_OK);
    struct mdns_wire_packet packet;
    assert(mdns_wire_parse_packet(data, length, &packet) == MDNS_WIRE_OK);
    assert(packet.flags == MDNS_WIRE_RESPONSE_FLAGS);
    assert(packet.declared_answer_count == 1);
    assert(packet.declared_additional_count == 3);
    assert(packet.record_count == 4);

    const struct mdns_wire_record *ptr = find_record(&packet, MDNS_WIRE_TYPE_PTR);
    const struct mdns_wire_record *srv = find_record(&packet, MDNS_WIRE_TYPE_SRV);
    const struct mdns_wire_record *txt_record = find_record(&packet, MDNS_WIRE_TYPE_TXT);
    const struct mdns_wire_record *a = find_record(&packet, MDNS_WIRE_TYPE_A);
    assert(ptr && srv && txt_record && a);
    assert(strcmp(ptr->owner, "_PROJECT_KIWI._tcp.local") == 0);
    assert(strcmp(ptr->data.ptr.target, "Kino Lobby._PROJECT_KIWI._tcp.local") == 0);
    assert(ptr->ttl == 120 && !ptr->cache_flush);
    assert(srv->section == MDNS_WIRE_ADDITIONAL);
    assert(strcmp(srv->owner, ptr->data.ptr.target) == 0);
    assert(srv->data.srv.priority == 0 && srv->data.srv.weight == 0);
    assert(srv->data.srv.port == 32145);
    assert(strcmp(srv->data.srv.target, "codboz-rg35xx.local") == 0);
    assert(srv->cache_flush && srv->ttl == 120);
    assert(a->data.a.address[0] == 192 && a->data.a.address[1] == 168 &&
           a->data.a.address[2] == 1 && a->data.a.address[3] == 44);

    assert(txt_record->data.txt.string_count == 3);
    const uint8_t *string;
    size_t string_length;
    assert(mdns_wire_txt_at(txt_record, 0, &string, &string_length));
    assert(string_length == sizeof(txt_version) - 1);
    assert(memcmp(string, txt_version, string_length) == 0);
    assert(mdns_wire_txt_at(txt_record, 1, &string, &string_length));
    assert(string_length == sizeof(txt_mode));
    assert(memcmp(string, txt_mode, string_length) == 0);
    assert(mdns_wire_txt_at(txt_record, 2, &string, &string_length));
    assert(string_length == 0);
    assert(!mdns_wire_txt_at(txt_record, 3, &string, &string_length));
}

static void test_goodbye(void) {
    const struct mdns_wire_service service = {
        .service_type = "_PROJECT_KIWI._tcp",
        .domain = "local",
        .instance = "Leaving",
        .host = "codboz",
        .port = 1000,
        .ipv4 = {10, 0, 0, 2},
    };
    const struct mdns_wire_response_options options = {
        .header_flags = MDNS_WIRE_RESPONSE_FLAGS,
        .ttl = 4500,
        .cache_flush = false,
        .goodbye = true,
    };
    uint8_t data[1024];
    size_t length;
    assert(mdns_wire_build_service_response(data, sizeof(data), &service, &options, &length) ==
           MDNS_WIRE_OK);
    struct mdns_wire_packet packet;
    assert(mdns_wire_parse_packet(data, length, &packet) == MDNS_WIRE_OK);
    assert(packet.record_count == 4);
    for (size_t i = 0; i < packet.record_count; ++i) {
        assert(packet.records[i].ttl == 0);
        assert(!packet.records[i].cache_flush);
    }
}

static void test_compressed_name(void) {
    const uint8_t data[] = {
        3, 'f', 'o', 'o', 5, 'l', 'o', 'c', 'a', 'l', 0, 3, 'b', 'a', 'r', 0xc0, 0x00, 0xc0, 0x0b,
    };
    char name[MDNS_WIRE_MAX_NAME];
    size_t offset = 11;
    assert(mdns_wire_decode_name(data, sizeof(data), &offset, name, sizeof(name)) == MDNS_WIRE_OK);
    assert(strcmp(name, "bar.foo.local") == 0);
    assert(offset == 17);
    offset = 17;
    assert(mdns_wire_decode_name(data, sizeof(data), &offset, name, sizeof(name)) == MDNS_WIRE_OK);
    assert(strcmp(name, "bar.foo.local") == 0);
    assert(offset == 19);
}

static void test_malformed_names(void) {
    char name[MDNS_WIRE_MAX_NAME];

    const uint8_t bad_offset[] = {0xc0, 0x7f};
    size_t offset = 0;
    assert(mdns_wire_decode_name(bad_offset, sizeof(bad_offset), &offset, name, sizeof(name)) ==
           MDNS_WIRE_MALFORMED);

    const uint8_t self_loop[] = {0xc0, 0x00};
    offset = 0;
    assert(mdns_wire_decode_name(self_loop, sizeof(self_loop), &offset, name, sizeof(name)) ==
           MDNS_WIRE_MALFORMED);

    const uint8_t two_node_loop[] = {0xc0, 0x02, 0xc0, 0x00};
    offset = 0;
    assert(mdns_wire_decode_name(two_node_loop, sizeof(two_node_loop), &offset, name,
                                 sizeof(name)) == MDNS_WIRE_MALFORMED);

    const uint8_t truncated_label[] = {4, 'n', 'o'};
    offset = 0;
    assert(mdns_wire_decode_name(truncated_label, sizeof(truncated_label), &offset, name,
                                 sizeof(name)) == MDNS_WIRE_TRUNCATED);

    const uint8_t truncated_pointer[] = {0xc0};
    offset = 0;
    assert(mdns_wire_decode_name(truncated_pointer, sizeof(truncated_pointer), &offset, name,
                                 sizeof(name)) == MDNS_WIRE_TRUNCATED);

    const uint8_t reserved_label[] = {0x40, 0};
    offset = 0;
    assert(mdns_wire_decode_name(reserved_label, sizeof(reserved_label), &offset, name,
                                 sizeof(name)) == MDNS_WIRE_MALFORMED);
}

static void test_packet_failures(void) {
    uint8_t query[512];
    size_t length;
    assert(mdns_wire_build_ptr_query(query, sizeof(query), "_service._tcp", "local", false,
                                     &length) == MDNS_WIRE_OK);
    struct mdns_wire_packet packet;
    assert(mdns_wire_parse_packet(query, length - 1, &packet) == MDNS_WIRE_TRUNCATED);
    assert(mdns_wire_parse_packet(query, MDNS_WIRE_HEADER_SIZE - 1, &packet) ==
           MDNS_WIRE_TRUNCATED);

    uint8_t too_many[MDNS_WIRE_HEADER_SIZE] = {0};
    too_many[4] = 0;
    too_many[5] = MDNS_WIRE_MAX_QUESTIONS + 1;
    assert(mdns_wire_parse_packet(too_many, sizeof(too_many), &packet) == MDNS_WIRE_LIMIT_EXCEEDED);

    size_t tiny_length = 99;
    uint8_t tiny[8];
    assert(mdns_wire_build_ptr_query(tiny, sizeof(tiny), "_service._tcp", "local", false,
                                     &tiny_length) == MDNS_WIRE_BUFFER_TOO_SMALL);
    assert(tiny_length == 0);
}

int main(void) {
    test_name_codec();
    test_ptr_query();
    test_record_queries();
    test_service_response();
    test_goodbye();
    test_compressed_name();
    test_malformed_names();
    test_packet_failures();
    puts("mDNS wire tests passed");
    return 0;
}
