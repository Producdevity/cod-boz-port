#include "s3e_host_internal.h"

#include "mdns_wire.h"
#include "zeroconf_platform.h"

enum {
    ZERO_CONF_RESULT_SUCCESS = 0,
    ZERO_CONF_RESULT_ERROR = 1,
    ZERO_CONF_MAX_SEARCHES = 4,
    ZERO_CONF_MAX_PUBLISHERS = 4,
    ZERO_CONF_MAX_DISCOVERED = 64,
    ZERO_CONF_MAX_ADDRESSES = 32,
    ZERO_CONF_MAX_TXT_RECORDS = 16,
    ZERO_CONF_TXT_RECORD_SIZE = 256,
    ZERO_CONF_PACKET_SIZE = 2048,
    ZERO_CONF_DEFAULT_TTL_SECONDS = 120,
    ZERO_CONF_QUERY_INTERVAL_MS = 2000,
    ZERO_CONF_FOLLOWUP_QUERY_INTERVAL_MS = 1000,
    ZERO_CONF_ANNOUNCE_INTERVAL_MS = 60000,
    ZERO_CONF_TXT_UPDATE_INTERVAL_MS = 1000,
};

struct zero_conf_service_id {
    uint32_t *guest_token;
    struct zero_conf_service_id *retired_next;
};

struct zero_conf_address {
    char host[MDNS_WIRE_MAX_NAME];
    uint8_t ipv4[4];
    uint64_t expires_at_ms;
    int in_use;
};

struct zero_conf_discovered {
    struct zero_conf_service_id *service_id;
    char instance_fqdn[MDNS_WIRE_MAX_NAME];
    char name[MDNS_WIRE_MAX_NAME];
    char host[MDNS_WIRE_MAX_NAME];
    uint8_t ipv4[4];
    uint16_t port;
    uint16_t txt_count;
    char txt[ZERO_CONF_MAX_TXT_RECORDS][ZERO_CONF_TXT_RECORD_SIZE];
    uint64_t ptr_expires_at_ms;
    uint64_t srv_expires_at_ms;
    uint64_t txt_expires_at_ms;
    uint64_t a_expires_at_ms;
    uint64_t next_srv_query_ms;
    uint64_t next_txt_query_ms;
    uint64_t next_a_query_ms;
    uint64_t change_sequence;
    int in_use;
    int has_ptr;
    int has_srv;
    int has_txt;
    int has_ipv4;
    int notified;
    int remove;
    int endpoint_changed;
    int txt_changed;
};

struct zero_conf_search {
    uint32_t generation;
    char service_type[MDNS_WIRE_MAX_NAME];
    char domain[MDNS_WIRE_MAX_NAME];
    char service_fqdn[MDNS_WIRE_MAX_NAME];
    s3e_zeroconf_callback_fn found_callback;
    s3e_zeroconf_callback_fn update_callback;
    s3e_zeroconf_callback_fn lost_callback;
    void *user_data;
    uint64_t next_query_ms;
    struct zero_conf_discovered discovered[ZERO_CONF_MAX_DISCOVERED];
    struct zero_conf_address addresses[ZERO_CONF_MAX_ADDRESSES];
    int in_use;
};

struct zero_conf_publisher {
    uint32_t generation;
    char name[65];
    char service_type[MDNS_WIRE_MAX_NAME];
    char domain[MDNS_WIRE_MAX_NAME];
    char service_fqdn[MDNS_WIRE_MAX_NAME];
    char instance_fqdn[MDNS_WIRE_MAX_NAME];
    char host[64];
    char host_fqdn[MDNS_WIRE_MAX_NAME];
    uint8_t ipv4[4];
    uint16_t port;
    uint16_t txt_count;
    char txt[ZERO_CONF_MAX_TXT_RECORDS][ZERO_CONF_TXT_RECORD_SIZE];
    uint16_t announced_txt_count;
    char announced_txt[ZERO_CONF_MAX_TXT_RECORDS][ZERO_CONF_TXT_RECORD_SIZE];
    uint64_t next_announce_ms;
    uint64_t last_announce_ms;
    uint64_t txt_update_due_ms;
    int txt_update_pending;
    int in_use;
};

static struct zero_conf_search g_searches[ZERO_CONF_MAX_SEARCHES];
static struct zero_conf_publisher g_publishers[ZERO_CONF_MAX_PUBLISHERS];
static int g_mdns_fd = -1;
static int g_zero_conf_pumping;
static uint32_t g_search_generation;
static uint32_t g_publisher_generation;
static uint32_t g_identity_generation;
static struct in_addr g_mdns_interface;
static int g_mdns_interface_valid;
static unsigned int g_callback_depth;
static struct zero_conf_service_id *g_retired_service_ids;

static void mark_discovered_changed(struct zero_conf_discovered *entry) {
    if (++entry->change_sequence == 0) {
        ++entry->change_sequence;
    }
}

static uint64_t zero_conf_now_ms(void) {
    return zeroconf_platform_now_ms();
}

static uint64_t expiry_from_ttl(uint64_t now, uint32_t ttl) {
    uint64_t milliseconds = (uint64_t)ttl * 1000u;
    if (UINT64_MAX - now < milliseconds) {
        return UINT64_MAX;
    }
    return now + milliseconds;
}

static struct zero_conf_service_id *allocate_service_id(void) {
    struct zero_conf_service_id *service_id = malloc(sizeof(*service_id));
    if (!service_id) {
        return NULL;
    }
    service_id->guest_token = malloc(sizeof(*service_id->guest_token));
    if (!service_id->guest_token) {
        free(service_id);
        return NULL;
    }
    *service_id->guest_token = ++g_identity_generation;
    if (!*service_id->guest_token) {
        *service_id->guest_token = ++g_identity_generation;
    }
    service_id->retired_next = NULL;
    return service_id;
}

static void free_service_id(struct zero_conf_service_id *service_id) {
    if (!service_id) {
        return;
    }
    free(service_id->guest_token);
    free(service_id);
}

static void flush_retired_service_ids(void) {
    if (g_callback_depth) {
        return;
    }
    while (g_retired_service_ids) {
        struct zero_conf_service_id *service_id = g_retired_service_ids;
        g_retired_service_ids = service_id->retired_next;
        free_service_id(service_id);
    }
}

static void retire_service_id(struct zero_conf_service_id **service_id) {
    if (!service_id || !*service_id) {
        return;
    }
    struct zero_conf_service_id *retired = *service_id;
    *service_id = NULL;
    if (!g_callback_depth) {
        free_service_id(retired);
        return;
    }
    retired->retired_next = g_retired_service_ids;
    g_retired_service_ids = retired;
}

static void callback_enter(void) {
    ++g_callback_depth;
}

static void callback_leave(void) {
    if (g_callback_depth) {
        --g_callback_depth;
    }
    flush_retired_service_ids();
}

static int copy_dns_component(char *output, size_t capacity, const char *input,
                              const char *fallback) {
    const char *value = input && input[0] ? input : fallback;
    if (!value) {
        return 0;
    }
    while (*value == '.') {
        ++value;
    }
    size_t length = strlen(value);
    while (length && value[length - 1] == '.') {
        --length;
    }
    if (!length || length >= capacity) {
        return 0;
    }
    memcpy(output, value, length);
    output[length] = 0;
    return 1;
}

static int join_dns_name(char *output, size_t capacity, const char *left, const char *right) {
    size_t left_length = strlen(left);
    size_t right_length = strlen(right);
    if (!left_length || !right_length || left_length + 1 + right_length >= capacity) {
        return 0;
    }
    memcpy(output, left, left_length);
    output[left_length] = '.';
    memcpy(output + left_length + 1, right, right_length + 1);
    return 1;
}

static int name_matches(const char *left, const char *right) {
    return strcasecmp(left, right) == 0;
}

static uint16_t effective_service_port(const struct zero_conf_search *search,
                                       uint16_t advertised_port) {
    /* The original BOZ host advertises its fixed port with the bytes swapped. */
    if (advertised_port == 2560 && name_matches(search->service_type, "_PROJECT_KIWI._tcp") &&
        name_matches(search->domain, "local")) {
        return 10;
    }
    return advertised_port;
}

static int instance_name_for_search(const struct zero_conf_search *search,
                                    const char *instance_fqdn, char *name, size_t capacity) {
    size_t instance_length = strlen(instance_fqdn);
    size_t service_length = strlen(search->service_fqdn);
    if (instance_length <= service_length + 1 ||
        instance_fqdn[instance_length - service_length - 1] != '.' ||
        strcasecmp(instance_fqdn + instance_length - service_length, search->service_fqdn) != 0) {
        return 0;
    }
    size_t name_length = instance_length - service_length - 1;
    if (!name_length || name_length >= capacity) {
        return 0;
    }
    memcpy(name, instance_fqdn, name_length);
    name[name_length] = 0;
    return 1;
}

static int ensure_mdns_socket(void) {
    if (g_mdns_fd >= 0) {
        return 1;
    }
    struct in_addr interface_address;
    int fd = zeroconf_platform_open_socket(&interface_address);
    if (fd < 0) {
        return 0;
    }
    g_mdns_fd = fd;
    g_mdns_interface = interface_address;
    g_mdns_interface_valid = 1;
    return 1;
}

static int find_local_ipv4(uint8_t address[4]) {
    if (!g_mdns_interface_valid) {
        if (!zeroconf_platform_select_ipv4(&g_mdns_interface)) {
            return 0;
        }
        g_mdns_interface_valid = 1;
    }
    memcpy(address, &g_mdns_interface.s_addr, 4);
    return 1;
}

static void make_host_label(char output[64]) {
    char system_name[128] = "codboz";
    if (gethostname(system_name, sizeof(system_name) - 1) != 0) {
        memcpy(system_name, "device", sizeof("device"));
    }
    system_name[sizeof(system_name) - 1] = 0;
    char sanitized[40];
    size_t length = 0;
    for (size_t i = 0; system_name[i] && length + 1 < sizeof(sanitized); ++i) {
        unsigned char character = (unsigned char)system_name[i];
        if (isalnum(character) || character == '-') {
            sanitized[length++] = (char)tolower(character);
        } else if (length && sanitized[length - 1] != '-') {
            sanitized[length++] = '-';
        }
    }
    while (length && sanitized[length - 1] == '-') {
        --length;
    }
    if (!length) {
        memcpy(sanitized, "device", sizeof("device"));
        length = sizeof("device") - 1;
    }
    sanitized[length] = 0;
    snprintf(output, 64, "codboz-%s-%lx", sanitized, (unsigned long)((uint32_t)getpid() & 0xffffu));
}

static int send_packet_to(const uint8_t *packet, size_t packet_size,
                          const struct sockaddr_in *destination) {
    return zeroconf_platform_send(g_mdns_fd, packet, packet_size, destination);
}

static int send_record_query(const char *name, uint16_t type) {
    uint8_t packet[512];
    size_t packet_size = 0;
    enum mdns_wire_status status =
        mdns_wire_build_query(packet, sizeof(packet), name, type, false, &packet_size);
    if (status != MDNS_WIRE_OK || !send_packet_to(packet, packet_size, NULL)) {
        return 0;
    }
    return 1;
}

static int send_query(struct zero_conf_search *search) {
    return send_record_query(search->service_fqdn, MDNS_WIRE_TYPE_PTR);
}

static int publisher_txt_views(const struct zero_conf_publisher *publisher,
                               struct mdns_wire_txt_string *views) {
    for (uint16_t i = 0; i < publisher->txt_count; ++i) {
        views[i].data = (const uint8_t *)publisher->txt[i];
        views[i].length = strlen(publisher->txt[i]);
    }
    return 1;
}

static void mark_publisher_txt_announced(struct zero_conf_publisher *publisher) {
    memcpy(publisher->announced_txt, publisher->txt, sizeof(publisher->announced_txt));
    publisher->announced_txt_count = publisher->txt_count;
    publisher->txt_update_pending = 0;
    publisher->txt_update_due_ms = 0;
}

static int send_publication(struct zero_conf_publisher *publisher, int goodbye,
                            const struct sockaddr_in *destination) {
    struct mdns_wire_txt_string txt[ZERO_CONF_MAX_TXT_RECORDS];
    publisher_txt_views(publisher, txt);
    struct mdns_wire_service service = {
        .service_type = publisher->service_type,
        .domain = publisher->domain,
        .instance = publisher->name,
        .host = publisher->host,
        .port = publisher->port,
        .txt = txt,
        .txt_count = publisher->txt_count,
    };
    memcpy(service.ipv4, publisher->ipv4, sizeof(service.ipv4));
    struct mdns_wire_response_options options = {
        .header_flags = MDNS_WIRE_RESPONSE_FLAGS,
        .ttl = ZERO_CONF_DEFAULT_TTL_SECONDS,
        .cache_flush = !goodbye,
        .goodbye = goodbye,
    };
    uint8_t packet[ZERO_CONF_PACKET_SIZE];
    size_t packet_size = 0;
    enum mdns_wire_status status =
        mdns_wire_build_service_response(packet, sizeof(packet), &service, &options, &packet_size);
    if (status != MDNS_WIRE_OK || !send_packet_to(packet, packet_size, destination)) {
        return 0;
    }
    return 1;
}

static int copy_txt_records(char target[ZERO_CONF_MAX_TXT_RECORDS][ZERO_CONF_TXT_RECORD_SIZE],
                            uint16_t *target_count, uint16_t count, const char **records) {
    if (count > ZERO_CONF_MAX_TXT_RECORDS || (count && !records)) {
        return 0;
    }
    char copied[ZERO_CONF_MAX_TXT_RECORDS][ZERO_CONF_TXT_RECORD_SIZE] = {{0}};
    for (uint16_t i = 0; i < count; ++i) {
        if (!records[i]) {
            return 0;
        }
        size_t length = strnlen(records[i], ZERO_CONF_TXT_RECORD_SIZE);
        if (length >= ZERO_CONF_TXT_RECORD_SIZE) {
            return 0;
        }
        memcpy(copied[i], records[i], length + 1);
    }
    memcpy(target, copied, sizeof(copied));
    *target_count = count;
    return 1;
}

static int txt_records_equal(const char left[ZERO_CONF_MAX_TXT_RECORDS][ZERO_CONF_TXT_RECORD_SIZE],
                             uint16_t left_count,
                             const char right[ZERO_CONF_MAX_TXT_RECORDS][ZERO_CONF_TXT_RECORD_SIZE],
                             uint16_t right_count) {
    return left_count == right_count &&
           memcmp(left, right,
                  sizeof(char[ZERO_CONF_MAX_TXT_RECORDS][ZERO_CONF_TXT_RECORD_SIZE])) == 0;
}

static void clear_discovered(struct zero_conf_discovered *entry) {
    retire_service_id(&entry->service_id);
    memset(entry, 0, sizeof(*entry));
}

static struct zero_conf_discovered *find_discovered(struct zero_conf_search *search,
                                                    const char *instance_fqdn) {
    for (size_t i = 0; i < ZERO_CONF_MAX_DISCOVERED; ++i) {
        struct zero_conf_discovered *entry = &search->discovered[i];
        if (entry->in_use && name_matches(entry->instance_fqdn, instance_fqdn)) {
            return entry;
        }
    }
    return NULL;
}

static struct zero_conf_discovered *allocate_discovered(struct zero_conf_search *search,
                                                        const char *instance_fqdn) {
    struct zero_conf_discovered *existing = find_discovered(search, instance_fqdn);
    if (existing) {
        return existing;
    }
    for (size_t i = 0; i < ZERO_CONF_MAX_DISCOVERED; ++i) {
        struct zero_conf_discovered *entry = &search->discovered[i];
        if (entry->in_use) {
            continue;
        }
        memset(entry, 0, sizeof(*entry));
        size_t length = strlen(instance_fqdn);
        if (length >= sizeof(entry->instance_fqdn) ||
            !instance_name_for_search(search, instance_fqdn, entry->name, sizeof(entry->name))) {
            return NULL;
        }
        entry->service_id = allocate_service_id();
        if (!entry->service_id) {
            return NULL;
        }
        memcpy(entry->instance_fqdn, instance_fqdn, length + 1);
        entry->in_use = 1;
        return entry;
    }
    return NULL;
}

static int discovered_complete(const struct zero_conf_discovered *entry) {
    return entry->has_ptr && entry->has_srv && entry->has_txt && entry->has_ipv4;
}

static void *discovered_service_id(struct zero_conf_discovered *entry) {
    return entry->service_id ? entry->service_id->guest_token : NULL;
}

static struct zero_conf_address *find_address(struct zero_conf_search *search, const char *host) {
    for (size_t i = 0; i < ZERO_CONF_MAX_ADDRESSES; ++i) {
        struct zero_conf_address *address = &search->addresses[i];
        if (address->in_use && name_matches(address->host, host)) {
            return address;
        }
    }
    return NULL;
}

static struct zero_conf_address *allocate_address(struct zero_conf_search *search,
                                                  const char *host) {
    struct zero_conf_address *address = find_address(search, host);
    if (address) {
        return address;
    }
    for (size_t i = 0; i < ZERO_CONF_MAX_ADDRESSES; ++i) {
        address = &search->addresses[i];
        if (address->in_use) {
            continue;
        }
        size_t length = strlen(host);
        if (!length || length >= sizeof(address->host)) {
            return NULL;
        }
        memset(address, 0, sizeof(*address));
        memcpy(address->host, host, length + 1);
        address->in_use = 1;
        return address;
    }
    return NULL;
}

static void apply_cached_address(struct zero_conf_search *search,
                                 struct zero_conf_discovered *entry, uint64_t now) {
    if (!entry->has_srv) {
        return;
    }
    struct zero_conf_address *address = find_address(search, entry->host);
    if (!address || !address->expires_at_ms || address->expires_at_ms <= now) {
        return;
    }
    mark_discovered_changed(entry);
    if (entry->has_ipv4 && memcmp(entry->ipv4, address->ipv4, sizeof(entry->ipv4)) != 0) {
        entry->endpoint_changed = 1;
    }
    memcpy(entry->ipv4, address->ipv4, sizeof(entry->ipv4));
    entry->a_expires_at_ms = address->expires_at_ms;
    entry->has_ipv4 = 1;
}

static void dispatch_found(struct zero_conf_search *search, struct zero_conf_discovered *entry) {
    if (!search->found_callback) {
        entry->notified = 1;
        return;
    }
    char name[sizeof(entry->name)];
    char service_type[sizeof(search->service_type)];
    char domain[sizeof(search->domain)];
    char host[sizeof(entry->host)];
    char txt[ZERO_CONF_MAX_TXT_RECORDS][ZERO_CONF_TXT_RECORD_SIZE];
    const char *txt_records[ZERO_CONF_MAX_TXT_RECORDS];
    memcpy(name, entry->name, sizeof(name));
    memcpy(service_type, search->service_type, sizeof(service_type));
    memcpy(domain, search->domain, sizeof(domain));
    memcpy(host, entry->host, sizeof(host));
    memcpy(txt, entry->txt, sizeof(txt));
    for (uint16_t i = 0; i < entry->txt_count; ++i) {
        txt_records[i] = txt[i];
    }
    struct s3e_zeroconf_found_data data = {
        .service_id = discovered_service_id(entry),
        .name = name,
        .service_type = service_type,
        .domain = domain,
        .host = host,
        /* BOZ copies this value directly into the network-order address port. */
        .port = htons(entry->port),
        .txt_count = entry->txt_count,
        .txt_records = txt_records,
    };
    memcpy(&data.ipv4_address, entry->ipv4, sizeof(data.ipv4_address));
    entry->notified = 1;
    callback_enter();
    search->found_callback(search, &data, search->user_data);
    callback_leave();
}

static void dispatch_update(struct zero_conf_search *search, struct zero_conf_discovered *entry) {
    if (!search->update_callback) {
        return;
    }
    char txt[ZERO_CONF_MAX_TXT_RECORDS][ZERO_CONF_TXT_RECORD_SIZE];
    const char *txt_records[ZERO_CONF_MAX_TXT_RECORDS];
    memcpy(txt, entry->txt, sizeof(txt));
    for (uint16_t i = 0; i < entry->txt_count; ++i) {
        txt_records[i] = txt[i];
    }
    struct s3e_zeroconf_txt_update_data data = {
        .service_id = discovered_service_id(entry),
        .txt_count = entry->txt_count,
        .txt_records = txt_records,
    };
    callback_enter();
    search->update_callback(search, &data, search->user_data);
    callback_leave();
}

static void dispatch_lost(struct zero_conf_search *search, struct zero_conf_discovered *entry,
                          void *service_id) {
    s3e_zeroconf_callback_fn callback = search->lost_callback;
    void *user_data = search->user_data;
    entry->notified = 0;
    if (callback) {
        callback_enter();
        callback(search, service_id, user_data);
        callback_leave();
    }
}

static int rotate_service_id_after_lost(struct zero_conf_search *search,
                                        struct zero_conf_discovered *entry,
                                        uint32_t search_generation) {
    struct zero_conf_service_id *replacement = allocate_service_id();
    if (!replacement) {
        return 0;
    }
    struct zero_conf_service_id *retired = entry->service_id;
    entry->service_id = replacement;
    dispatch_lost(search, entry, retired ? retired->guest_token : NULL);
    if (!search->in_use || search->generation != search_generation) {
        retire_service_id(&retired);
        return -1;
    }
    retire_service_id(&retired);
    return 1;
}

static void apply_ptr_records(struct zero_conf_search *search,
                              const struct mdns_wire_packet *packet, uint64_t now) {
    for (size_t i = 0; i < packet->record_count; ++i) {
        const struct mdns_wire_record *record = &packet->records[i];
        if (record->data_kind != MDNS_WIRE_RDATA_PTR || record->class_code != MDNS_WIRE_CLASS_IN ||
            !name_matches(record->owner, search->service_fqdn)) {
            continue;
        }
        if (!record->ttl) {
            struct zero_conf_discovered *entry = find_discovered(search, record->data.ptr.target);
            if (entry) {
                mark_discovered_changed(entry);
                entry->has_ptr = 0;
                entry->ptr_expires_at_ms = 0;
                entry->remove = 1;
            }
            continue;
        }
        struct zero_conf_discovered *entry = allocate_discovered(search, record->data.ptr.target);
        if (!entry) {
            continue;
        }
        mark_discovered_changed(entry);
        entry->has_ptr = 1;
        entry->ptr_expires_at_ms = expiry_from_ttl(now, record->ttl);
        entry->remove = 0;
    }
}

static void apply_service_records(struct zero_conf_search *search,
                                  const struct mdns_wire_packet *packet, uint64_t now) {
    for (size_t i = 0; i < packet->record_count; ++i) {
        const struct mdns_wire_record *record = &packet->records[i];
        if (record->class_code != MDNS_WIRE_CLASS_IN ||
            (record->data_kind != MDNS_WIRE_RDATA_SRV &&
             record->data_kind != MDNS_WIRE_RDATA_TXT)) {
            continue;
        }
        struct zero_conf_discovered *entry = find_discovered(search, record->owner);
        if (!entry && record->ttl) {
            char name[MDNS_WIRE_MAX_NAME];
            if (!instance_name_for_search(search, record->owner, name, sizeof(name))) {
                continue;
            }
            entry = allocate_discovered(search, record->owner);
        }
        if (!entry) {
            continue;
        }
        if (!record->ttl) {
            mark_discovered_changed(entry);
            if (record->data_kind == MDNS_WIRE_RDATA_SRV) {
                entry->has_srv = 0;
                entry->srv_expires_at_ms = 0;
                entry->next_srv_query_ms = 0;
            } else {
                entry->has_txt = 0;
                entry->txt_expires_at_ms = 0;
                entry->next_txt_query_ms = 0;
            }
            continue;
        }
        if (record->data_kind == MDNS_WIRE_RDATA_SRV) {
            mark_discovered_changed(entry);
            uint16_t port = effective_service_port(search, record->data.srv.port);
            int host_changed =
                entry->host[0] && !name_matches(entry->host, record->data.srv.target);
            if (entry->has_srv && (entry->port != port || host_changed)) {
                entry->endpoint_changed = 1;
            }
            if (host_changed) {
                entry->has_ipv4 = 0;
                entry->a_expires_at_ms = 0;
                entry->next_a_query_ms = 0;
            }
            entry->port = port;
            snprintf(entry->host, sizeof(entry->host), "%s", record->data.srv.target);
            entry->has_srv = 1;
            entry->srv_expires_at_ms = expiry_from_ttl(now, record->ttl);
            apply_cached_address(search, entry, now);
            continue;
        }

        uint16_t count = record->data.txt.string_count;
        if (count > ZERO_CONF_MAX_TXT_RECORDS) {
            continue;
        }
        char updated[ZERO_CONF_MAX_TXT_RECORDS][ZERO_CONF_TXT_RECORD_SIZE] = {{0}};
        int valid = 1;
        for (uint16_t item = 0; item < count; ++item) {
            const uint8_t *text;
            size_t length;
            if (!mdns_wire_txt_at(record, item, &text, &length) ||
                length >= ZERO_CONF_TXT_RECORD_SIZE || (length && memchr(text, 0, length))) {
                valid = 0;
                break;
            }
            memcpy(updated[item], text, length);
            updated[item][length] = 0;
        }
        if (!valid) {
            continue;
        }
        mark_discovered_changed(entry);
        if (entry->has_txt &&
            (entry->txt_count != count || memcmp(entry->txt, updated, sizeof(updated)) != 0)) {
            entry->txt_changed = 1;
        }
        memcpy(entry->txt, updated, sizeof(updated));
        entry->txt_count = count;
        entry->has_txt = 1;
        entry->txt_expires_at_ms = expiry_from_ttl(now, record->ttl);
    }
}

static void apply_address_records(struct zero_conf_search *search,
                                  const struct mdns_wire_packet *packet, uint64_t now) {
    for (size_t i = 0; i < packet->record_count; ++i) {
        const struct mdns_wire_record *record = &packet->records[i];
        if (record->data_kind != MDNS_WIRE_RDATA_A || record->class_code != MDNS_WIRE_CLASS_IN) {
            continue;
        }
        struct zero_conf_address *address = find_address(search, record->owner);
        if (!record->ttl) {
            if (address) {
                memset(address, 0, sizeof(*address));
            }
            for (size_t item = 0; item < ZERO_CONF_MAX_DISCOVERED; ++item) {
                struct zero_conf_discovered *entry = &search->discovered[item];
                if (entry->in_use && entry->has_srv && name_matches(entry->host, record->owner)) {
                    mark_discovered_changed(entry);
                    entry->has_ipv4 = 0;
                    entry->a_expires_at_ms = 0;
                    entry->next_a_query_ms = 0;
                }
            }
            continue;
        }
        address = allocate_address(search, record->owner);
        if (!address) {
            continue;
        }
        memcpy(address->ipv4, record->data.a.address, sizeof(address->ipv4));
        address->expires_at_ms = expiry_from_ttl(now, record->ttl);
        for (size_t item = 0; item < ZERO_CONF_MAX_DISCOVERED; ++item) {
            struct zero_conf_discovered *entry = &search->discovered[item];
            if (!entry->in_use || !entry->has_srv || !name_matches(entry->host, record->owner)) {
                continue;
            }
            mark_discovered_changed(entry);
            if (entry->has_ipv4 &&
                memcmp(entry->ipv4, record->data.a.address, sizeof(entry->ipv4)) != 0) {
                entry->endpoint_changed = 1;
            }
            memcpy(entry->ipv4, record->data.a.address, sizeof(entry->ipv4));
            entry->has_ipv4 = 1;
            entry->a_expires_at_ms = address->expires_at_ms;
        }
    }
}

static void send_followup_queries(struct zero_conf_search *search, uint64_t now) {
    for (size_t i = 0; i < ZERO_CONF_MAX_DISCOVERED; ++i) {
        struct zero_conf_discovered *entry = &search->discovered[i];
        if (!entry->in_use || entry->remove || !entry->has_ptr) {
            continue;
        }
        if (!entry->has_srv && now >= entry->next_srv_query_ms) {
            (void)send_record_query(entry->instance_fqdn, MDNS_WIRE_TYPE_SRV);
            entry->next_srv_query_ms = now + ZERO_CONF_FOLLOWUP_QUERY_INTERVAL_MS;
        }
        if (!entry->has_txt && now >= entry->next_txt_query_ms) {
            (void)send_record_query(entry->instance_fqdn, MDNS_WIRE_TYPE_TXT);
            entry->next_txt_query_ms = now + ZERO_CONF_FOLLOWUP_QUERY_INTERVAL_MS;
        }
        if (entry->has_srv && !entry->has_ipv4 && now >= entry->next_a_query_ms) {
            (void)send_record_query(entry->host, MDNS_WIRE_TYPE_A);
            entry->next_a_query_ms = now + ZERO_CONF_FOLLOWUP_QUERY_INTERVAL_MS;
        }
    }
}

static void dispatch_search_changes(struct zero_conf_search *search) {
    uint32_t generation = search->generation;
    for (size_t i = 0; i < ZERO_CONF_MAX_DISCOVERED; ++i) {
        struct zero_conf_discovered *entry = &search->discovered[i];
        if (!entry->in_use) {
            continue;
        }
        uint64_t change_sequence = entry->change_sequence;
        if (entry->remove) {
            if (entry->notified) {
                int rotation = rotate_service_id_after_lost(search, entry, generation);
                if (rotation < 0) {
                    return;
                }
                if (!rotation) {
                    continue;
                }
            }
            if (!entry->remove) {
                continue;
            }
            clear_discovered(entry);
            continue;
        }
        if (!discovered_complete(entry)) {
            if (entry->notified) {
                int rotation = rotate_service_id_after_lost(search, entry, generation);
                if (rotation < 0) {
                    return;
                }
                if (!rotation) {
                    continue;
                }
            }
            if (entry->change_sequence == change_sequence) {
                entry->endpoint_changed = 0;
                entry->txt_changed = 0;
            }
            continue;
        }
        if (!entry->notified) {
            dispatch_found(search, entry);
        } else if (entry->endpoint_changed) {
            int rotation = rotate_service_id_after_lost(search, entry, generation);
            if (rotation < 0) {
                return;
            }
            if (!rotation) {
                continue;
            }
            if (!entry->notified) {
                dispatch_found(search, entry);
            }
        } else if (entry->txt_changed) {
            dispatch_update(search, entry);
        }
        if (!search->in_use || search->generation != generation) {
            return;
        }
        if (entry->change_sequence == change_sequence) {
            entry->endpoint_changed = 0;
            entry->txt_changed = 0;
        }
    }
}

static int publisher_matches_query(const struct zero_conf_publisher *publisher,
                                   const struct mdns_wire_packet *packet, int *unicast_response) {
    int matched = 0;
    for (size_t i = 0; i < packet->question_count; ++i) {
        const struct mdns_wire_question *question = &packet->questions[i];
        if (question->class_code != MDNS_WIRE_CLASS_IN) {
            continue;
        }
        int type_matches =
            (question->type == MDNS_WIRE_TYPE_PTR &&
             name_matches(question->name, publisher->service_fqdn)) ||
            (question->type == MDNS_WIRE_TYPE_SRV &&
             name_matches(question->name, publisher->instance_fqdn)) ||
            (question->type == MDNS_WIRE_TYPE_TXT &&
             name_matches(question->name, publisher->instance_fqdn)) ||
            (question->type == MDNS_WIRE_TYPE_A &&
             name_matches(question->name, publisher->host_fqdn)) ||
            (question->type == 255 && (name_matches(question->name, publisher->service_fqdn) ||
                                       name_matches(question->name, publisher->instance_fqdn) ||
                                       name_matches(question->name, publisher->host_fqdn)));
        if (!type_matches) {
            continue;
        }
        matched = 1;
        if (question->unicast_response) {
            *unicast_response = 1;
        }
    }
    return matched;
}

void s3e_zero_conf_process_packet(const uint8_t *data, size_t data_size,
                                  const struct sockaddr_in *source) {
    if (!data || !data_size) {
        return;
    }
    struct mdns_wire_packet *packet = malloc(sizeof(*packet));
    if (!packet) {
        return;
    }
    enum mdns_wire_status status = mdns_wire_parse_packet(data, data_size, packet);
    if (status != MDNS_WIRE_OK) {
        free(packet);
        return;
    }
    if (packet->flags & MDNS_WIRE_FLAG_QR) {
        uint64_t now = zero_conf_now_ms();
        for (size_t i = 0; i < ZERO_CONF_MAX_SEARCHES; ++i) {
            struct zero_conf_search *search = &g_searches[i];
            if (!search->in_use) {
                continue;
            }
            apply_ptr_records(search, packet, now);
            apply_service_records(search, packet, now);
            apply_address_records(search, packet, now);
            send_followup_queries(search, now);
            dispatch_search_changes(search);
        }
        free(packet);
        return;
    }
    for (size_t i = 0; i < ZERO_CONF_MAX_PUBLISHERS; ++i) {
        struct zero_conf_publisher *publisher = &g_publishers[i];
        int unicast_response = 0;
        if (publisher->in_use && publisher_matches_query(publisher, packet, &unicast_response)) {
            send_publication(publisher, 0, unicast_response && source ? source : NULL);
        }
    }
    free(packet);
}

static void expire_discovered(uint64_t now) {
    for (size_t i = 0; i < ZERO_CONF_MAX_SEARCHES; ++i) {
        struct zero_conf_search *search = &g_searches[i];
        if (!search->in_use) {
            continue;
        }
        for (size_t item = 0; item < ZERO_CONF_MAX_ADDRESSES; ++item) {
            struct zero_conf_address *address = &search->addresses[item];
            if (address->in_use && address->expires_at_ms && address->expires_at_ms <= now) {
                memset(address, 0, sizeof(*address));
            }
        }
        for (size_t item = 0; item < ZERO_CONF_MAX_DISCOVERED; ++item) {
            struct zero_conf_discovered *entry = &search->discovered[item];
            if (!entry->in_use) {
                continue;
            }
            if (entry->has_ptr && entry->ptr_expires_at_ms && entry->ptr_expires_at_ms <= now) {
                mark_discovered_changed(entry);
                entry->has_ptr = 0;
                entry->ptr_expires_at_ms = 0;
                entry->remove = 1;
            }
            if (entry->has_srv && entry->srv_expires_at_ms && entry->srv_expires_at_ms <= now) {
                mark_discovered_changed(entry);
                entry->has_srv = 0;
                entry->srv_expires_at_ms = 0;
                entry->next_srv_query_ms = 0;
            }
            if (entry->has_txt && entry->txt_expires_at_ms && entry->txt_expires_at_ms <= now) {
                mark_discovered_changed(entry);
                entry->has_txt = 0;
                entry->txt_expires_at_ms = 0;
                entry->next_txt_query_ms = 0;
            }
            if (entry->has_ipv4 && entry->a_expires_at_ms && entry->a_expires_at_ms <= now) {
                mark_discovered_changed(entry);
                entry->has_ipv4 = 0;
                entry->a_expires_at_ms = 0;
                entry->next_a_query_ms = 0;
            }
            if (!entry->has_ptr && !entry->has_srv && !entry->has_txt) {
                mark_discovered_changed(entry);
                entry->remove = 1;
            }
        }
        send_followup_queries(search, now);
        dispatch_search_changes(search);
    }
}

static struct zero_conf_search *resolve_search(void *handle) {
    for (size_t i = 0; i < ZERO_CONF_MAX_SEARCHES; ++i) {
        if (&g_searches[i] == handle && g_searches[i].in_use) {
            return &g_searches[i];
        }
    }
    return NULL;
}

static struct zero_conf_publisher *resolve_publisher(void *handle) {
    for (size_t i = 0; i < ZERO_CONF_MAX_PUBLISHERS; ++i) {
        if (&g_publishers[i] == handle && g_publishers[i].in_use) {
            return &g_publishers[i];
        }
    }
    return NULL;
}

static void clear_search(struct zero_conf_search *search) {
    for (size_t i = 0; i < ZERO_CONF_MAX_DISCOVERED; ++i) {
        if (search->discovered[i].service_id) {
            retire_service_id(&search->discovered[i].service_id);
        }
    }
    memset(search, 0, sizeof(*search));
}

void *s3eZeroConfStartSearch(const char *service_type, const char *domain,
                             s3e_zeroconf_callback_fn found_callback,
                             s3e_zeroconf_callback_fn update_callback,
                             s3e_zeroconf_callback_fn lost_callback, void *user_data) {
    if (!service_type) {
        return NULL;
    }
    for (size_t i = 0; i < ZERO_CONF_MAX_SEARCHES; ++i) {
        struct zero_conf_search *search = &g_searches[i];
        if (search->in_use) {
            continue;
        }
        memset(search, 0, sizeof(*search));
        if (!copy_dns_component(search->service_type, sizeof(search->service_type), service_type,
                                NULL) ||
            !copy_dns_component(search->domain, sizeof(search->domain), domain, "local") ||
            !join_dns_name(search->service_fqdn, sizeof(search->service_fqdn), search->service_type,
                           search->domain)) {
            return NULL;
        }
        if (!ensure_mdns_socket()) {
            memset(search, 0, sizeof(*search));
            return NULL;
        }
        search->generation = ++g_search_generation;
        if (!search->generation) {
            search->generation = ++g_search_generation;
        }
        search->found_callback = found_callback;
        search->update_callback = update_callback;
        search->lost_callback = lost_callback;
        search->user_data = user_data;
        search->in_use = 1;
        uint64_t now = zero_conf_now_ms();
        (void)send_query(search);
        search->next_query_ms = now + ZERO_CONF_QUERY_INTERVAL_MS;
        return search;
    }
    return NULL;
}

void s3eZeroConfStopSearch(void *handle) {
    struct zero_conf_search *search = resolve_search(handle);
    if (!search) {
        return;
    }
    clear_search(search);
}

void *s3eZeroConfPublish(uint16_t port, const char *name, const char *service_type,
                         const char *domain, uint16_t txt_count, const char **txt_records) {
    if (!port || !name || !name[0] || !service_type || strlen(name) > 64) {
        return NULL;
    }
    for (size_t i = 0; i < ZERO_CONF_MAX_PUBLISHERS; ++i) {
        struct zero_conf_publisher *publisher = &g_publishers[i];
        if (publisher->in_use) {
            continue;
        }
        memset(publisher, 0, sizeof(*publisher));
        if (!copy_dns_component(publisher->name, sizeof(publisher->name), name, NULL) ||
            !copy_dns_component(publisher->service_type, sizeof(publisher->service_type),
                                service_type, NULL) ||
            !copy_dns_component(publisher->domain, sizeof(publisher->domain), domain, "local") ||
            !copy_txt_records(publisher->txt, &publisher->txt_count, txt_count, txt_records)) {
            memset(publisher, 0, sizeof(*publisher));
            return NULL;
        }
        /* BOZ passes the raw network-order s3eInetAddress port to this host-order API. */
        publisher->port = ntohs(port);
        publisher->generation = ++g_publisher_generation;
        if (!publisher->generation) {
            publisher->generation = ++g_publisher_generation;
        }
        publisher->in_use = 1;
        if (strlen(publisher->name) > 63 || !ensure_mdns_socket() ||
            !join_dns_name(publisher->service_fqdn, sizeof(publisher->service_fqdn),
                           publisher->service_type, publisher->domain) ||
            !join_dns_name(publisher->instance_fqdn, sizeof(publisher->instance_fqdn),
                           publisher->name, publisher->service_fqdn) ||
            !find_local_ipv4(publisher->ipv4)) {
            memset(publisher, 0, sizeof(*publisher));
            return NULL;
        }
        make_host_label(publisher->host);
        if (!join_dns_name(publisher->host_fqdn, sizeof(publisher->host_fqdn), publisher->host,
                           publisher->domain)) {
            memset(publisher, 0, sizeof(*publisher));
            return NULL;
        }
        uint64_t now = zero_conf_now_ms();
        (void)send_publication(publisher, 0, NULL);
        mark_publisher_txt_announced(publisher);
        publisher->last_announce_ms = now;
        publisher->next_announce_ms = now + ZERO_CONF_ANNOUNCE_INTERVAL_MS;
        return publisher;
    }
    return NULL;
}

int32_t s3eZeroConfUpdateTxtRecord(void *handle, uint16_t txt_count, const char **txt_records) {
    struct zero_conf_publisher *publisher = resolve_publisher(handle);
    char updated[ZERO_CONF_MAX_TXT_RECORDS][ZERO_CONF_TXT_RECORD_SIZE] = {{0}};
    uint16_t updated_count = 0;
    if (!publisher || !copy_txt_records(updated, &updated_count, txt_count, txt_records)) {
        return ZERO_CONF_RESULT_ERROR;
    }
    if (txt_records_equal(publisher->txt, publisher->txt_count, updated, updated_count)) {
        return ZERO_CONF_RESULT_SUCCESS;
    }
    memcpy(publisher->txt, updated, sizeof(updated));
    publisher->txt_count = updated_count;
    if (txt_records_equal(publisher->txt, publisher->txt_count, publisher->announced_txt,
                          publisher->announced_txt_count)) {
        publisher->txt_update_pending = 0;
        publisher->txt_update_due_ms = 0;
        return ZERO_CONF_RESULT_SUCCESS;
    }
    if (!publisher->txt_update_pending) {
        uint64_t now = zero_conf_now_ms();
        uint64_t earliest = publisher->last_announce_ms + ZERO_CONF_TXT_UPDATE_INTERVAL_MS;
        publisher->txt_update_due_ms = now > earliest ? now : earliest;
        publisher->txt_update_pending = 1;
    }
    return ZERO_CONF_RESULT_SUCCESS;
}

int32_t s3eZeroConfUnpublish(void *handle) {
    struct zero_conf_publisher *publisher = resolve_publisher(handle);
    if (!publisher) {
        return ZERO_CONF_RESULT_ERROR;
    }
    (void)send_publication(publisher, 1, NULL);
    memset(publisher, 0, sizeof(*publisher));
    return ZERO_CONF_RESULT_SUCCESS;
}

void s3e_zero_conf_pump(void) {
    if (g_zero_conf_pumping || g_callback_depth) {
        return;
    }
    g_zero_conf_pumping = 1;
    uint64_t now = zero_conf_now_ms();
    if (g_mdns_fd >= 0) {
        for (unsigned int count = 0; count < 32 && g_mdns_fd >= 0; ++count) {
            uint8_t packet[ZERO_CONF_PACKET_SIZE];
            struct sockaddr_in source;
            ssize_t received =
                zeroconf_platform_receive(g_mdns_fd, packet, sizeof(packet), &source);
            if (received <= 0) {
                break;
            }
            s3e_zero_conf_process_packet(packet, (size_t)received, &source);
        }
    }
    expire_discovered(now);
    for (size_t i = 0; i < ZERO_CONF_MAX_SEARCHES; ++i) {
        struct zero_conf_search *search = &g_searches[i];
        if (!search->in_use) {
            continue;
        }
        if (now >= search->next_query_ms) {
            (void)send_query(search);
            search->next_query_ms = now + ZERO_CONF_QUERY_INTERVAL_MS;
        }
    }
    for (size_t i = 0; i < ZERO_CONF_MAX_PUBLISHERS; ++i) {
        struct zero_conf_publisher *publisher = &g_publishers[i];
        if (!publisher->in_use) {
            continue;
        }
        if (publisher->txt_update_pending && now >= publisher->txt_update_due_ms) {
            if (send_publication(publisher, 0, NULL)) {
                mark_publisher_txt_announced(publisher);
                publisher->last_announce_ms = now;
                publisher->next_announce_ms = now + ZERO_CONF_ANNOUNCE_INTERVAL_MS;
            } else {
                publisher->txt_update_due_ms = now + ZERO_CONF_TXT_UPDATE_INTERVAL_MS;
            }
            continue;
        }
        if (now >= publisher->next_announce_ms) {
            uint8_t ipv4[4];
            if (find_local_ipv4(ipv4)) {
                memcpy(publisher->ipv4, ipv4, sizeof(publisher->ipv4));
            }
            if (send_publication(publisher, 0, NULL)) {
                mark_publisher_txt_announced(publisher);
                publisher->last_announce_ms = now;
            }
            publisher->next_announce_ms = now + ZERO_CONF_ANNOUNCE_INTERVAL_MS;
        }
    }
    g_zero_conf_pumping = 0;
}

void s3e_zero_conf_shutdown(void) {
    for (size_t i = 0; i < ZERO_CONF_MAX_PUBLISHERS; ++i) {
        if (g_publishers[i].in_use) {
            (void)send_publication(&g_publishers[i], 1, NULL);
        }
    }
    memset(g_publishers, 0, sizeof(g_publishers));
    for (size_t i = 0; i < ZERO_CONF_MAX_SEARCHES; ++i) {
        clear_search(&g_searches[i]);
    }
    flush_retired_service_ids();
    if (g_mdns_fd >= 0) {
        zeroconf_platform_close_socket(g_mdns_fd);
        g_mdns_fd = -1;
    }
    memset(&g_mdns_interface, 0, sizeof(g_mdns_interface));
    g_mdns_interface_valid = 0;
    g_zero_conf_pumping = 0;
}
