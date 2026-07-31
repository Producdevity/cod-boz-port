#include "mdns_wire.h"

#include <string.h>

enum {
    DNS_POINTER_MASK = 0xc0,
    DNS_POINTER_VALUE_MASK = 0x3fff,
    DNS_CACHE_FLUSH = 0x8000,
    DNS_MAX_POINTER_JUMPS = 32,
};

struct wire_writer {
    uint8_t *data;
    size_t capacity;
    size_t offset;
    bool failed;
};

static void writer_bytes(struct wire_writer *writer, const void *data, size_t length) {
    if (writer->failed || length > writer->capacity - writer->offset) {
        writer->failed = true;
        return;
    }
    if (length != 0) {
        memcpy(writer->data + writer->offset, data, length);
    }
    writer->offset += length;
}

static void writer_u8(struct wire_writer *writer, uint8_t value) {
    writer_bytes(writer, &value, sizeof(value));
}

static void writer_u16(struct wire_writer *writer, uint16_t value) {
    uint8_t encoded[2] = {(uint8_t)(value >> 8), (uint8_t)value};
    writer_bytes(writer, encoded, sizeof(encoded));
}

static void writer_u32(struct wire_writer *writer, uint32_t value) {
    uint8_t encoded[4] = {
        (uint8_t)(value >> 24),
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 8),
        (uint8_t)value,
    };
    writer_bytes(writer, encoded, sizeof(encoded));
}

static void writer_patch_u16(struct wire_writer *writer, size_t offset, uint16_t value) {
    if (writer->failed || offset > writer->capacity || writer->capacity - offset < 2) {
        writer->failed = true;
        return;
    }
    writer->data[offset] = (uint8_t)(value >> 8);
    writer->data[offset + 1] = (uint8_t)value;
}

static bool dotted_name_valid(const char *name, size_t *wire_length) {
    if (!name || name[0] == '\0') {
        return false;
    }
    if (strcmp(name, ".") == 0) {
        if (wire_length) {
            *wire_length = 1;
        }
        return true;
    }

    size_t total = 1;
    const char *label = name;
    for (const char *cursor = name;; ++cursor) {
        if (*cursor != '.' && *cursor != '\0') {
            continue;
        }
        size_t length = (size_t)(cursor - label);
        if (length == 0) {
            if (*cursor == '\0' && cursor != name && cursor[-1] == '.') {
                break;
            }
            return false;
        }
        if (length > 63 || total > 255 - (length + 1)) {
            return false;
        }
        total += length + 1;
        if (*cursor == '\0') {
            break;
        }
        label = cursor + 1;
    }
    if (wire_length) {
        *wire_length = total;
    }
    return true;
}

static void writer_name(struct wire_writer *writer, const char *name) {
    if (!dotted_name_valid(name, NULL)) {
        writer->failed = true;
        return;
    }
    if (strcmp(name, ".") == 0) {
        writer_u8(writer, 0);
        return;
    }

    const char *label = name;
    for (const char *cursor = name;; ++cursor) {
        if (*cursor != '.' && *cursor != '\0') {
            continue;
        }
        size_t length = (size_t)(cursor - label);
        if (length == 0 && *cursor == '\0') {
            break;
        }
        writer_u8(writer, (uint8_t)length);
        writer_bytes(writer, label, length);
        if (*cursor == '\0') {
            break;
        }
        label = cursor + 1;
        if (*label == '\0') {
            break;
        }
    }
    writer_u8(writer, 0);
}

static bool raw_label_valid(const char *label) {
    if (!label) {
        return false;
    }
    size_t length = strlen(label);
    return length != 0 && length <= 63;
}

static void writer_label(struct wire_writer *writer, const char *label) {
    size_t length = strlen(label);
    writer_u8(writer, (uint8_t)length);
    writer_bytes(writer, label, length);
}

static void writer_pointer(struct wire_writer *writer, size_t offset) {
    if (offset > DNS_POINTER_VALUE_MASK) {
        writer->failed = true;
        return;
    }
    writer_u16(writer, (uint16_t)(DNS_POINTER_MASK << 8) | (uint16_t)offset);
}

static bool join_name(char output[MDNS_WIRE_MAX_NAME], const char *prefix, const char *suffix) {
    if (!prefix || !suffix) {
        return false;
    }
    size_t prefix_length = strlen(prefix);
    while (prefix_length != 0 && prefix[prefix_length - 1] == '.') {
        --prefix_length;
    }
    size_t suffix_start = 0;
    while (suffix[suffix_start] == '.') {
        ++suffix_start;
    }
    size_t suffix_length = strlen(suffix + suffix_start);
    while (suffix_length != 0 && suffix[suffix_start + suffix_length - 1] == '.') {
        --suffix_length;
    }
    if (prefix_length == 0 || suffix_length == 0 ||
        prefix_length + 1 + suffix_length >= MDNS_WIRE_MAX_NAME) {
        return false;
    }
    memcpy(output, prefix, prefix_length);
    output[prefix_length] = '.';
    memcpy(output + prefix_length + 1, suffix + suffix_start, suffix_length);
    output[prefix_length + 1 + suffix_length] = '\0';
    return dotted_name_valid(output, NULL);
}

static bool dotted_prefix_wire_length(const char *name, size_t *length) {
    if (!dotted_name_valid(name, NULL) || strcmp(name, ".") == 0) {
        return false;
    }
    size_t total = 0;
    const char *label = name;
    for (const char *cursor = name;; ++cursor) {
        if (*cursor != '.' && *cursor != '\0') {
            continue;
        }
        size_t label_length = (size_t)(cursor - label);
        if (label_length != 0) {
            total += 1 + label_length;
        }
        if (*cursor == '\0') {
            break;
        }
        label = cursor + 1;
        if (*label == '\0') {
            break;
        }
    }
    *length = total;
    return true;
}

enum mdns_wire_status mdns_wire_encode_name(uint8_t *buffer, size_t capacity, size_t *offset,
                                            const char *name) {
    if (!buffer || !offset || *offset > capacity || !dotted_name_valid(name, NULL)) {
        return MDNS_WIRE_INVALID_ARGUMENT;
    }
    struct wire_writer writer = {
        .data = buffer,
        .capacity = capacity,
        .offset = *offset,
    };
    writer_name(&writer, name);
    if (writer.failed) {
        return MDNS_WIRE_BUFFER_TOO_SMALL;
    }
    *offset = writer.offset;
    return MDNS_WIRE_OK;
}

enum mdns_wire_status mdns_wire_decode_name(const uint8_t *packet, size_t packet_size,
                                            size_t *offset, char *name, size_t name_capacity) {
    if (!packet || !offset || !name || name_capacity == 0 || *offset > packet_size) {
        return MDNS_WIRE_INVALID_ARGUMENT;
    }
    if (*offset == packet_size) {
        return MDNS_WIRE_TRUNCATED;
    }

    size_t cursor = *offset;
    size_t next_offset = cursor;
    size_t output_length = 0;
    size_t expanded_wire_length = 1;
    size_t visited[DNS_MAX_POINTER_JUMPS];
    size_t visited_count = 0;
    bool jumped = false;

    for (;;) {
        if (cursor >= packet_size) {
            return MDNS_WIRE_TRUNCATED;
        }
        uint8_t label_length = packet[cursor];
        if ((label_length & DNS_POINTER_MASK) == DNS_POINTER_MASK) {
            if (packet_size - cursor < 2) {
                return MDNS_WIRE_TRUNCATED;
            }
            size_t target = ((size_t)(label_length & 0x3f) << 8) | packet[cursor + 1];
            if (target >= packet_size || visited_count == DNS_MAX_POINTER_JUMPS) {
                return MDNS_WIRE_MALFORMED;
            }
            for (size_t i = 0; i < visited_count; ++i) {
                if (visited[i] == target) {
                    return MDNS_WIRE_MALFORMED;
                }
            }
            visited[visited_count++] = target;
            if (!jumped) {
                next_offset = cursor + 2;
            }
            cursor = target;
            jumped = true;
            continue;
        }
        if ((label_length & DNS_POINTER_MASK) != 0) {
            return MDNS_WIRE_MALFORMED;
        }
        ++cursor;
        if (label_length == 0) {
            if (!jumped) {
                next_offset = cursor;
            }
            if (output_length == 0) {
                if (name_capacity < 2) {
                    return MDNS_WIRE_LIMIT_EXCEEDED;
                }
                name[output_length++] = '.';
            }
            name[output_length] = '\0';
            *offset = next_offset;
            return MDNS_WIRE_OK;
        }
        if (label_length > packet_size - cursor) {
            return MDNS_WIRE_TRUNCATED;
        }
        if (expanded_wire_length > 255 - ((size_t)label_length + 1)) {
            return MDNS_WIRE_MALFORMED;
        }
        expanded_wire_length += (size_t)label_length + 1;
        size_t separator = output_length == 0 ? 0 : 1;
        if (separator + (size_t)label_length > name_capacity - 1 - output_length) {
            return MDNS_WIRE_LIMIT_EXCEEDED;
        }
        if (separator != 0) {
            name[output_length++] = '.';
        }
        memcpy(name + output_length, packet + cursor, label_length);
        output_length += label_length;
        cursor += label_length;
        if (!jumped) {
            next_offset = cursor;
        }
    }
}

static enum mdns_wire_status read_u16(const uint8_t *data, size_t size, size_t *offset,
                                      uint16_t *value) {
    if (size - *offset < 2) {
        return MDNS_WIRE_TRUNCATED;
    }
    *value = (uint16_t)((uint16_t)data[*offset] << 8) | data[*offset + 1];
    *offset += 2;
    return MDNS_WIRE_OK;
}

static enum mdns_wire_status read_u32(const uint8_t *data, size_t size, size_t *offset,
                                      uint32_t *value) {
    if (size - *offset < 4) {
        return MDNS_WIRE_TRUNCATED;
    }
    *value = ((uint32_t)data[*offset] << 24) | ((uint32_t)data[*offset + 1] << 16) |
             ((uint32_t)data[*offset + 2] << 8) | data[*offset + 3];
    *offset += 4;
    return MDNS_WIRE_OK;
}

static enum mdns_wire_status parse_question(const uint8_t *data, size_t size, size_t *offset,
                                            struct mdns_wire_question *question) {
    enum mdns_wire_status status =
        mdns_wire_decode_name(data, size, offset, question->name, sizeof(question->name));
    if (status != MDNS_WIRE_OK) {
        return status;
    }
    uint16_t raw_class;
    status = read_u16(data, size, offset, &question->type);
    if (status == MDNS_WIRE_OK) {
        status = read_u16(data, size, offset, &raw_class);
    }
    if (status != MDNS_WIRE_OK) {
        return status;
    }
    question->unicast_response = (raw_class & DNS_CACHE_FLUSH) != 0;
    question->class_code = raw_class & ~DNS_CACHE_FLUSH;
    return MDNS_WIRE_OK;
}

static enum mdns_wire_status parse_txt(const uint8_t *data, size_t start, size_t end,
                                       struct mdns_wire_record *record) {
    size_t length = end - start;
    if (length > sizeof(record->data.txt.bytes)) {
        return MDNS_WIRE_LIMIT_EXCEEDED;
    }
    size_t cursor = start;
    size_t count = 0;
    while (cursor < end) {
        size_t string_length = data[cursor++];
        if (string_length > end - cursor) {
            return MDNS_WIRE_MALFORMED;
        }
        cursor += string_length;
        ++count;
    }
    if (count > UINT16_MAX) {
        return MDNS_WIRE_LIMIT_EXCEEDED;
    }
    memcpy(record->data.txt.bytes, data + start, length);
    record->data.txt.length = (uint16_t)length;
    record->data.txt.string_count = (uint16_t)count;
    record->data_kind = MDNS_WIRE_RDATA_TXT;
    return MDNS_WIRE_OK;
}

static enum mdns_wire_status parse_record(const uint8_t *data, size_t size, size_t *offset,
                                          enum mdns_wire_section section,
                                          struct mdns_wire_record *record) {
    enum mdns_wire_status status =
        mdns_wire_decode_name(data, size, offset, record->owner, sizeof(record->owner));
    if (status != MDNS_WIRE_OK) {
        return status;
    }
    uint16_t raw_class;
    status = read_u16(data, size, offset, &record->type);
    if (status == MDNS_WIRE_OK) {
        status = read_u16(data, size, offset, &raw_class);
    }
    if (status == MDNS_WIRE_OK) {
        status = read_u32(data, size, offset, &record->ttl);
    }
    if (status == MDNS_WIRE_OK) {
        status = read_u16(data, size, offset, &record->rdata_length);
    }
    if (status != MDNS_WIRE_OK) {
        return status;
    }
    record->section = section;
    record->cache_flush = (raw_class & DNS_CACHE_FLUSH) != 0;
    record->class_code = raw_class & ~DNS_CACHE_FLUSH;
    record->data_kind = MDNS_WIRE_RDATA_NONE;

    size_t rdata_start = *offset;
    if ((size_t)record->rdata_length > size - rdata_start) {
        return MDNS_WIRE_TRUNCATED;
    }
    size_t rdata_end = rdata_start + record->rdata_length;

    switch (record->type) {
    case MDNS_WIRE_TYPE_A:
        if (record->rdata_length != 4) {
            return MDNS_WIRE_MALFORMED;
        }
        memcpy(record->data.a.address, data + rdata_start, 4);
        record->data_kind = MDNS_WIRE_RDATA_A;
        break;
    case MDNS_WIRE_TYPE_PTR: {
        size_t name_offset = rdata_start;
        status = mdns_wire_decode_name(data, size, &name_offset, record->data.ptr.target,
                                       sizeof(record->data.ptr.target));
        if (status != MDNS_WIRE_OK) {
            return status;
        }
        if (name_offset != rdata_end) {
            return MDNS_WIRE_MALFORMED;
        }
        record->data_kind = MDNS_WIRE_RDATA_PTR;
        break;
    }
    case MDNS_WIRE_TYPE_TXT:
        status = parse_txt(data, rdata_start, rdata_end, record);
        if (status != MDNS_WIRE_OK) {
            return status;
        }
        break;
    case MDNS_WIRE_TYPE_SRV: {
        if (record->rdata_length < 7) {
            return MDNS_WIRE_MALFORMED;
        }
        size_t field_offset = rdata_start;
        status = read_u16(data, rdata_end, &field_offset, &record->data.srv.priority);
        if (status == MDNS_WIRE_OK) {
            status = read_u16(data, rdata_end, &field_offset, &record->data.srv.weight);
        }
        if (status == MDNS_WIRE_OK) {
            status = read_u16(data, rdata_end, &field_offset, &record->data.srv.port);
        }
        if (status == MDNS_WIRE_OK) {
            status = mdns_wire_decode_name(data, size, &field_offset, record->data.srv.target,
                                           sizeof(record->data.srv.target));
        }
        if (status != MDNS_WIRE_OK) {
            return status;
        }
        if (field_offset != rdata_end) {
            return MDNS_WIRE_MALFORMED;
        }
        record->data_kind = MDNS_WIRE_RDATA_SRV;
        break;
    }
    default:
        break;
    }
    *offset = rdata_end;
    return MDNS_WIRE_OK;
}

enum mdns_wire_status mdns_wire_parse_packet(const uint8_t *data, size_t data_size,
                                             struct mdns_wire_packet *packet) {
    if (!data || !packet) {
        return MDNS_WIRE_INVALID_ARGUMENT;
    }
    if (data_size < MDNS_WIRE_HEADER_SIZE) {
        return MDNS_WIRE_TRUNCATED;
    }
    memset(packet, 0, sizeof(*packet));
    size_t offset = 0;
    enum mdns_wire_status status = read_u16(data, data_size, &offset, &packet->id);
    if (status == MDNS_WIRE_OK) {
        status = read_u16(data, data_size, &offset, &packet->flags);
    }
    if (status == MDNS_WIRE_OK) {
        status = read_u16(data, data_size, &offset, &packet->declared_question_count);
    }
    if (status == MDNS_WIRE_OK) {
        status = read_u16(data, data_size, &offset, &packet->declared_answer_count);
    }
    if (status == MDNS_WIRE_OK) {
        status = read_u16(data, data_size, &offset, &packet->declared_authority_count);
    }
    if (status == MDNS_WIRE_OK) {
        status = read_u16(data, data_size, &offset, &packet->declared_additional_count);
    }
    if (status != MDNS_WIRE_OK) {
        return status;
    }

    size_t record_count = (size_t)packet->declared_answer_count + packet->declared_authority_count +
                          packet->declared_additional_count;
    if (packet->declared_question_count > MDNS_WIRE_MAX_QUESTIONS ||
        record_count > MDNS_WIRE_MAX_RECORDS) {
        return MDNS_WIRE_LIMIT_EXCEEDED;
    }

    for (size_t i = 0; i < packet->declared_question_count; ++i) {
        status = parse_question(data, data_size, &offset, &packet->questions[i]);
        if (status != MDNS_WIRE_OK) {
            return status;
        }
        ++packet->question_count;
    }

    const uint16_t section_counts[] = {
        packet->declared_answer_count,
        packet->declared_authority_count,
        packet->declared_additional_count,
    };
    for (size_t section = 0; section < sizeof(section_counts) / sizeof(section_counts[0]);
         ++section) {
        for (size_t i = 0; i < section_counts[section]; ++i) {
            status = parse_record(data, data_size, &offset, (enum mdns_wire_section)section,
                                  &packet->records[packet->record_count]);
            if (status != MDNS_WIRE_OK) {
                return status;
            }
            ++packet->record_count;
        }
    }
    return MDNS_WIRE_OK;
}

enum mdns_wire_status mdns_wire_build_query(uint8_t *buffer, size_t capacity, const char *name,
                                            uint16_t type, bool unicast_response,
                                            size_t *packet_size) {
    if (!buffer || !packet_size) {
        return MDNS_WIRE_INVALID_ARGUMENT;
    }
    *packet_size = 0;
    if (!dotted_name_valid(name, NULL) || type == 0) {
        return MDNS_WIRE_INVALID_ARGUMENT;
    }

    struct wire_writer writer = {.data = buffer, .capacity = capacity};
    writer_u16(&writer, 0);
    writer_u16(&writer, 0);
    writer_u16(&writer, 1);
    writer_u16(&writer, 0);
    writer_u16(&writer, 0);
    writer_u16(&writer, 0);
    writer_name(&writer, name);
    writer_u16(&writer, type);
    writer_u16(&writer, MDNS_WIRE_CLASS_IN | (unicast_response ? DNS_CACHE_FLUSH : 0));
    if (writer.failed) {
        return MDNS_WIRE_BUFFER_TOO_SMALL;
    }
    *packet_size = writer.offset;
    return MDNS_WIRE_OK;
}

enum mdns_wire_status mdns_wire_build_ptr_query(uint8_t *buffer, size_t capacity,
                                                const char *service_type, const char *domain,
                                                bool unicast_response, size_t *packet_size) {
    if (!buffer || !packet_size) {
        return MDNS_WIRE_INVALID_ARGUMENT;
    }
    *packet_size = 0;
    char service_name[MDNS_WIRE_MAX_NAME];
    if (!join_name(service_name, service_type, domain)) {
        return MDNS_WIRE_INVALID_ARGUMENT;
    }
    return mdns_wire_build_query(buffer, capacity, service_name, MDNS_WIRE_TYPE_PTR,
                                 unicast_response, packet_size);
}

static bool service_valid(const struct mdns_wire_service *service) {
    if (!service || !raw_label_valid(service->instance) || !raw_label_valid(service->host) ||
        service->txt_count > UINT16_MAX || (service->txt_count != 0 && !service->txt)) {
        return false;
    }
    char service_name[MDNS_WIRE_MAX_NAME];
    if (!join_name(service_name, service->service_type, service->domain)) {
        return false;
    }
    size_t txt_length = 0;
    for (size_t i = 0; i < service->txt_count; ++i) {
        if (service->txt[i].length > 255 ||
            (service->txt[i].length != 0 && !service->txt[i].data) ||
            txt_length > UINT16_MAX - (service->txt[i].length + 1)) {
            return false;
        }
        txt_length += service->txt[i].length + 1;
    }
    return true;
}

static void writer_record_header(struct wire_writer *writer, uint16_t type, uint16_t class_code,
                                 uint32_t ttl, size_t *length_offset, size_t *rdata_offset) {
    writer_u16(writer, type);
    writer_u16(writer, class_code);
    writer_u32(writer, ttl);
    *length_offset = writer->offset;
    writer_u16(writer, 0);
    *rdata_offset = writer->offset;
}

static void writer_finish_record(struct wire_writer *writer, size_t length_offset,
                                 size_t rdata_offset) {
    if (writer->failed || writer->offset < rdata_offset ||
        writer->offset - rdata_offset > UINT16_MAX) {
        writer->failed = true;
        return;
    }
    writer_patch_u16(writer, length_offset, (uint16_t)(writer->offset - rdata_offset));
}

enum mdns_wire_status mdns_wire_build_service_response(
    uint8_t *buffer, size_t capacity, const struct mdns_wire_service *service,
    const struct mdns_wire_response_options *options, size_t *packet_size) {
    if (!buffer || !options || !packet_size || !service_valid(service)) {
        return MDNS_WIRE_INVALID_ARGUMENT;
    }
    *packet_size = 0;

    char service_name[MDNS_WIRE_MAX_NAME];
    if (!join_name(service_name, service->service_type, service->domain)) {
        return MDNS_WIRE_INVALID_ARGUMENT;
    }
    size_t service_prefix_length;
    if (!dotted_prefix_wire_length(service->service_type, &service_prefix_length)) {
        return MDNS_WIRE_INVALID_ARGUMENT;
    }

    uint32_t ttl = options->goodbye ? 0 : options->ttl;
    uint16_t unique_class = MDNS_WIRE_CLASS_IN | (options->cache_flush ? DNS_CACHE_FLUSH : 0);
    struct wire_writer writer = {.data = buffer, .capacity = capacity};
    writer_u16(&writer, 0);
    writer_u16(&writer, options->header_flags);
    writer_u16(&writer, 0);
    writer_u16(&writer, 1);
    writer_u16(&writer, 0);
    writer_u16(&writer, 3);

    size_t service_offset = writer.offset;
    writer_name(&writer, service_name);
    size_t domain_offset = service_offset + service_prefix_length;

    size_t length_offset;
    size_t rdata_offset;
    writer_record_header(&writer, MDNS_WIRE_TYPE_PTR, MDNS_WIRE_CLASS_IN, ttl, &length_offset,
                         &rdata_offset);
    size_t instance_offset = writer.offset;
    writer_label(&writer, service->instance);
    writer_pointer(&writer, service_offset);
    writer_finish_record(&writer, length_offset, rdata_offset);

    writer_pointer(&writer, instance_offset);
    writer_record_header(&writer, MDNS_WIRE_TYPE_SRV, unique_class, ttl, &length_offset,
                         &rdata_offset);
    writer_u16(&writer, 0);
    writer_u16(&writer, 0);
    writer_u16(&writer, service->port);
    size_t host_offset = writer.offset;
    writer_label(&writer, service->host);
    writer_pointer(&writer, domain_offset);
    writer_finish_record(&writer, length_offset, rdata_offset);

    writer_pointer(&writer, instance_offset);
    writer_record_header(&writer, MDNS_WIRE_TYPE_TXT, unique_class, ttl, &length_offset,
                         &rdata_offset);
    for (size_t i = 0; i < service->txt_count; ++i) {
        writer_u8(&writer, (uint8_t)service->txt[i].length);
        writer_bytes(&writer, service->txt[i].data, service->txt[i].length);
    }
    writer_finish_record(&writer, length_offset, rdata_offset);

    writer_pointer(&writer, host_offset);
    writer_record_header(&writer, MDNS_WIRE_TYPE_A, unique_class, ttl, &length_offset,
                         &rdata_offset);
    writer_bytes(&writer, service->ipv4, sizeof(service->ipv4));
    writer_finish_record(&writer, length_offset, rdata_offset);

    if (writer.failed) {
        return MDNS_WIRE_BUFFER_TOO_SMALL;
    }
    *packet_size = writer.offset;
    return MDNS_WIRE_OK;
}

bool mdns_wire_txt_at(const struct mdns_wire_record *record, size_t index, const uint8_t **data,
                      size_t *length) {
    if (!record || !data || !length || record->data_kind != MDNS_WIRE_RDATA_TXT ||
        index >= record->data.txt.string_count) {
        return false;
    }
    size_t offset = 0;
    for (size_t current = 0; current <= index; ++current) {
        if (offset >= record->data.txt.length) {
            return false;
        }
        size_t string_length = record->data.txt.bytes[offset++];
        if (string_length > record->data.txt.length - offset) {
            return false;
        }
        if (current == index) {
            *data = record->data.txt.bytes + offset;
            *length = string_length;
            return true;
        }
        offset += string_length;
    }
    return false;
}
