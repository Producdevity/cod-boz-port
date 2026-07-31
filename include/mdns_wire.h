#ifndef CODBOZ_MDNS_WIRE_H
#define CODBOZ_MDNS_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    MDNS_WIRE_HEADER_SIZE = 12,
    MDNS_WIRE_MAX_NAME = 256,
    MDNS_WIRE_MAX_QUESTIONS = 8,
    MDNS_WIRE_MAX_RECORDS = 32,
    MDNS_WIRE_MAX_TXT_BYTES = 1024,
    MDNS_WIRE_FLAG_QR = 0x8000,
    MDNS_WIRE_RESPONSE_FLAGS = 0x8400,
    MDNS_WIRE_CLASS_IN = 1,
    MDNS_WIRE_TYPE_A = 1,
    MDNS_WIRE_TYPE_PTR = 12,
    MDNS_WIRE_TYPE_TXT = 16,
    MDNS_WIRE_TYPE_SRV = 33,
};

enum mdns_wire_status {
    MDNS_WIRE_OK = 0,
    MDNS_WIRE_INVALID_ARGUMENT,
    MDNS_WIRE_BUFFER_TOO_SMALL,
    MDNS_WIRE_TRUNCATED,
    MDNS_WIRE_MALFORMED,
    MDNS_WIRE_LIMIT_EXCEEDED,
};

enum mdns_wire_section {
    MDNS_WIRE_ANSWER = 0,
    MDNS_WIRE_AUTHORITY,
    MDNS_WIRE_ADDITIONAL,
};

enum mdns_wire_rdata_kind {
    MDNS_WIRE_RDATA_NONE = 0,
    MDNS_WIRE_RDATA_A,
    MDNS_WIRE_RDATA_PTR,
    MDNS_WIRE_RDATA_TXT,
    MDNS_WIRE_RDATA_SRV,
};

struct mdns_wire_question {
    char name[MDNS_WIRE_MAX_NAME];
    uint16_t type;
    uint16_t class_code;
    bool unicast_response;
};

struct mdns_wire_record {
    char owner[MDNS_WIRE_MAX_NAME];
    enum mdns_wire_section section;
    uint16_t type;
    uint16_t class_code;
    bool cache_flush;
    uint32_t ttl;
    uint16_t rdata_length;
    enum mdns_wire_rdata_kind data_kind;
    union {
        struct {
            uint8_t address[4];
        } a;
        struct {
            char target[MDNS_WIRE_MAX_NAME];
        } ptr;
        struct {
            uint8_t bytes[MDNS_WIRE_MAX_TXT_BYTES];
            uint16_t length;
            uint16_t string_count;
        } txt;
        struct {
            uint16_t priority;
            uint16_t weight;
            uint16_t port;
            char target[MDNS_WIRE_MAX_NAME];
        } srv;
    } data;
};

struct mdns_wire_packet {
    uint16_t id;
    uint16_t flags;
    uint16_t declared_question_count;
    uint16_t declared_answer_count;
    uint16_t declared_authority_count;
    uint16_t declared_additional_count;
    size_t question_count;
    size_t record_count;
    struct mdns_wire_question questions[MDNS_WIRE_MAX_QUESTIONS];
    struct mdns_wire_record records[MDNS_WIRE_MAX_RECORDS];
};

struct mdns_wire_txt_string {
    const uint8_t *data;
    size_t length;
};

struct mdns_wire_service {
    const char *service_type;
    const char *domain;
    /* Instance and host are single DNS labels, so dots are encoded literally. */
    const char *instance;
    const char *host;
    uint16_t port;
    uint8_t ipv4[4];
    const struct mdns_wire_txt_string *txt;
    size_t txt_count;
};

struct mdns_wire_response_options {
    uint16_t header_flags;
    uint32_t ttl;
    /* Cache-flush applies to the unique SRV, TXT, and A records; PTR stays shared. */
    bool cache_flush;
    bool goodbye;
};

enum mdns_wire_status mdns_wire_encode_name(uint8_t *buffer, size_t capacity, size_t *offset,
                                            const char *name);
enum mdns_wire_status mdns_wire_decode_name(const uint8_t *packet, size_t packet_size,
                                            size_t *offset, char *name, size_t name_capacity);

enum mdns_wire_status mdns_wire_build_ptr_query(uint8_t *buffer, size_t capacity,
                                                const char *service_type, const char *domain,
                                                bool unicast_response, size_t *packet_size);

enum mdns_wire_status mdns_wire_build_query(uint8_t *buffer, size_t capacity, const char *name,
                                            uint16_t type, bool unicast_response,
                                            size_t *packet_size);

enum mdns_wire_status mdns_wire_build_service_response(
    uint8_t *buffer, size_t capacity, const struct mdns_wire_service *service,
    const struct mdns_wire_response_options *options, size_t *packet_size);

enum mdns_wire_status mdns_wire_parse_packet(const uint8_t *data, size_t data_size,
                                             struct mdns_wire_packet *packet);

bool mdns_wire_txt_at(const struct mdns_wire_record *record, size_t index, const uint8_t **data,
                      size_t *length);

#endif
