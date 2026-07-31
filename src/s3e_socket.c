#include "s3e_host_internal.h"

#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

enum {
    S3E_RESULT_SUCCESS = 0,
    S3E_RESULT_ERROR = 1,
    S3E_SOCKET_HANDLE_FIRST = 3000,
    S3E_SOCKET_SLOT_COUNT = 32,
    S3E_NETWORK_TYPE_WLAN = 3,
};

struct socket_slot {
    int fd;
    uint32_t generation;
    int32_t type;
    int32_t domain;
    void *connect_callback;
    void *connect_user_data;
    void *accept_callback;
    void *accept_user_data;
    void *readable_callback;
    void *readable_user_data;
    void *writable_callback;
    void *writable_user_data;
    int in_use;
    int connect_pending;
};

struct socket_poll_entry {
    struct pollfd pollfd;
    uint32_t slot;
    uint32_t generation;
};

struct dns_lookup {
    pthread_t thread;
    char *hostname;
    struct s3e_inet_address *address;
    void *callback;
    void *user_data;
    uint16_t port;
    struct sockaddr_storage result;
    socklen_t result_length;
    int gai_error;
    int done;
    int cancelled;
};

static struct socket_slot g_socket_slots[S3E_SOCKET_SLOT_COUNT];
static _Thread_local int32_t g_socket_error;
static pthread_mutex_t g_dns_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct dns_lookup *g_dns_lookup;

static void set_socket_error(int32_t error) {
    g_socket_error = error;
}

static int32_t map_socket_errno(int error) {
    switch (error) {
    case 0:
        return S3E_SOCKET_ERR_NONE;
    case EPERM:
        return S3E_SOCKET_ERR_NOTPERM;
    case EBADF:
    case EINVAL:
#ifdef EADDRNOTAVAIL
    case EADDRNOTAVAIL:
#endif
        return S3E_SOCKET_ERR_PARAM;
#if EAGAIN != EWOULDBLOCK
    case EAGAIN:
#endif
    case EWOULDBLOCK:
        return S3E_SOCKET_ERR_WOULDBLOCK;
    case EACCES:
        return S3E_SOCKET_ERR_UNAVAIL;
    case EPIPE:
#ifdef ESHUTDOWN
    case ESHUTDOWN:
#endif
        return S3E_SOCKET_ERR_SHUTDOWN;
    case ENOTSOCK:
        return S3E_SOCKET_ERR_NOTSOCK;
    case EMSGSIZE:
        return S3E_SOCKET_ERR_MSGSIZE;
#ifdef EPFNOSUPPORT
    case EPFNOSUPPORT:
#endif
    case EAFNOSUPPORT:
#ifdef EPROTONOSUPPORT
    case EPROTONOSUPPORT:
#endif
        return S3E_SOCKET_ERR_UNSUPPORTED;
    case EADDRINUSE:
        return S3E_SOCKET_ERR_ADDRINUSE;
    case ENETUNREACH:
#ifdef ENETDOWN
    case ENETDOWN:
#endif
    case EHOSTUNREACH:
        return S3E_SOCKET_ERR_NETDOWN;
    case ECONNABORTED:
    case ECONNRESET:
        return S3E_SOCKET_ERR_CONNRESET;
    case EISCONN:
        return S3E_SOCKET_ERR_ISCONN;
    case ENOTCONN:
        return S3E_SOCKET_ERR_NOTCONN;
    case ETIMEDOUT:
        return S3E_SOCKET_ERR_TIMEDOUT;
    case ECONNREFUSED:
        return S3E_SOCKET_ERR_CONNREFUSED;
    case EALREADY:
        return S3E_SOCKET_ERR_ALREADY;
    case EINPROGRESS:
        return S3E_SOCKET_ERR_INPROGRESS;
    case EMFILE:
    case ENFILE:
        return S3E_SOCKET_ERR_TOO_MANY;
    default:
        return S3E_SOCKET_ERR_UNAVAIL;
    }
}

static int32_t fail_with_errno(int error) {
    set_socket_error(map_socket_errno(error));
    return S3E_RESULT_ERROR;
}

static void *slot_handle(uint32_t slot) {
    return (void *)(uintptr_t)(S3E_SOCKET_HANDLE_FIRST + slot);
}

static int handle_slot(void *handle) {
    uintptr_t value = (uintptr_t)handle;
    if (value < S3E_SOCKET_HANDLE_FIRST ||
        value >= S3E_SOCKET_HANDLE_FIRST + S3E_SOCKET_SLOT_COUNT) {
        return -1;
    }
    uint32_t slot = (uint32_t)(value - S3E_SOCKET_HANDLE_FIRST);
    return g_socket_slots[slot].in_use ? (int)slot : -1;
}

static struct socket_slot *resolve_socket(void *handle) {
    int slot = handle_slot(handle);
    if (slot < 0) {
        set_socket_error(S3E_SOCKET_ERR_PARAM);
        return NULL;
    }
    return &g_socket_slots[slot];
}

static int allocate_socket_slot(int fd, int32_t type, int32_t domain) {
    for (uint32_t i = 0; i < S3E_SOCKET_SLOT_COUNT; ++i) {
        struct socket_slot *slot = &g_socket_slots[i];
        if (slot->in_use) {
            continue;
        }
        uint32_t generation = slot->generation + 1;
        if (!generation) {
            generation = 1;
        }
        memset(slot, 0, sizeof(*slot));
        slot->fd = fd;
        slot->generation = generation;
        slot->type = type;
        slot->domain = domain;
        slot->in_use = 1;
        return (int)i;
    }
    set_socket_error(S3E_SOCKET_ERR_TOO_MANY);
    return -1;
}

static void close_socket_slot(uint32_t index) {
    struct socket_slot *slot = &g_socket_slots[index];
    uint32_t generation = slot->generation + 1;
    if (slot->fd >= 0) {
        close(slot->fd);
    }
    memset(slot, 0, sizeof(*slot));
    slot->fd = -1;
    slot->generation = generation ? generation : 1;
}

static int set_nonblocking(int fd) {
    int enabled = 1;
    if (ioctl(fd, FIONBIO, &enabled) < 0) {
        return -1;
    }
    return ioctl(fd, FIOCLEX) < 0 ? -1 : 0;
}

static int socket_family(int32_t domain) {
    switch (domain) {
    case S3E_SOCKET_UNSPEC:
    case S3E_SOCKET_INET:
        return AF_INET;
    case S3E_SOCKET_INET6:
        return AF_INET6;
    default:
        return -1;
    }
}

static int socket_kind(uint32_t type) {
    switch (type) {
    case S3E_SOCKET_TCP:
        return SOCK_STREAM;
    case S3E_SOCKET_UDP:
        return SOCK_DGRAM;
    case S3E_SOCKET_RAW:
        return SOCK_RAW;
    default:
        return -1;
    }
}

static int address_to_sockaddr(const struct s3e_inet_address *address,
                               struct sockaddr_storage *storage, socklen_t *length) {
    if (!address || !storage || !length) {
        set_socket_error(S3E_SOCKET_ERR_PARAM);
        return -1;
    }
    memset(storage, 0, sizeof(*storage));
    if (address->type & S3E_SOCKET_ADDR_IPV6) {
        struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)storage;
        ipv6->sin6_family = AF_INET6;
        ipv6->sin6_port = address->port;
        memcpy(&ipv6->sin6_addr, address->ipv6_address, sizeof(ipv6->sin6_addr));
        *length = sizeof(*ipv6);
        return 0;
    }
    if (address->type & S3E_SOCKET_ADDR_LOCAL) {
        set_socket_error(S3E_SOCKET_ERR_UNSUPPORTED);
        return -1;
    }
    struct sockaddr_in *ipv4 = (struct sockaddr_in *)storage;
    ipv4->sin_family = AF_INET;
    ipv4->sin_port = address->port;
    ipv4->sin_addr.s_addr = address->ip_address;
    *length = sizeof(*ipv4);
    return 0;
}

static void sockaddr_to_address(const struct sockaddr *source, socklen_t length,
                                struct s3e_inet_address *address) {
    if (!source || !address) {
        return;
    }
    memset(address, 0, sizeof(*address));
    if (source->sa_family == AF_INET && length >= sizeof(struct sockaddr_in)) {
        const struct sockaddr_in *ipv4 = (const struct sockaddr_in *)source;
        address->type = S3E_SOCKET_ADDR_IPV4;
        address->ip_address = ipv4->sin_addr.s_addr;
        address->port = ipv4->sin_port;
        (void)inet_ntop(AF_INET, &ipv4->sin_addr, address->string, sizeof(address->string));
    } else if (source->sa_family == AF_INET6 && length >= sizeof(struct sockaddr_in6)) {
        const struct sockaddr_in6 *ipv6 = (const struct sockaddr_in6 *)source;
        address->type = S3E_SOCKET_ADDR_IPV6;
        memcpy(address->ipv6_address, &ipv6->sin6_addr, sizeof(address->ipv6_address));
        address->port = ipv6->sin6_port;
        (void)inet_ntop(AF_INET6, &ipv6->sin6_addr, address->string, sizeof(address->string));
    }
}

static const char *address_text(const struct s3e_inet_address *address, char *buffer,
                                size_t buffer_size) {
    if (!address || !buffer || !buffer_size) {
        return "?";
    }
    const void *source;
    int family;
    if (address->type & S3E_SOCKET_ADDR_IPV6) {
        family = AF_INET6;
        source = address->ipv6_address;
    } else {
        family = AF_INET;
        source = &address->ip_address;
    }
    if (!inet_ntop(family, source, buffer, buffer_size)) {
        snprintf(buffer, buffer_size, "?");
    }
    return buffer;
}

static int resolve_hostname(const char *hostname, struct sockaddr_storage *result,
                            socklen_t *result_length) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    struct addrinfo *addresses = NULL;
    int error = getaddrinfo(hostname, NULL, &hints, &addresses);
    if (error != 0) {
        return error;
    }
    struct addrinfo *selected = addresses;
    while (selected && selected->ai_family != AF_INET && selected->ai_family != AF_INET6) {
        selected = selected->ai_next;
    }
    if (!selected || selected->ai_addrlen > sizeof(*result)) {
        freeaddrinfo(addresses);
        return EAI_NONAME;
    }
    memset(result, 0, sizeof(*result));
    memcpy(result, selected->ai_addr, selected->ai_addrlen);
    *result_length = (socklen_t)selected->ai_addrlen;
    freeaddrinfo(addresses);
    return 0;
}

static void *dns_lookup_thread(void *opaque) {
    struct dns_lookup *lookup = opaque;
    struct sockaddr_storage result;
    socklen_t result_length = 0;
    int error = resolve_hostname(lookup->hostname, &result, &result_length);

    pthread_mutex_lock(&g_dns_mutex);
    lookup->gai_error = error;
    if (error == 0) {
        lookup->result = result;
        lookup->result_length = result_length;
    }
    lookup->done = 1;
    pthread_mutex_unlock(&g_dns_mutex);
    return NULL;
}

static void free_dns_lookup(struct dns_lookup *lookup) {
    if (!lookup) {
        return;
    }
    free(lookup->hostname);
    free(lookup);
}

static void pump_dns_lookup(void) {
    pthread_mutex_lock(&g_dns_mutex);
    struct dns_lookup *lookup = g_dns_lookup;
    if (!lookup || !lookup->done) {
        pthread_mutex_unlock(&g_dns_mutex);
        return;
    }
    g_dns_lookup = NULL;
    pthread_mutex_unlock(&g_dns_mutex);

    (void)pthread_join(lookup->thread, NULL);
    if (!lookup->cancelled && lookup->callback) {
        void *system_data = NULL;
        if (lookup->gai_error == 0) {
            sockaddr_to_address((const struct sockaddr *)&lookup->result, lookup->result_length,
                                lookup->address);
            lookup->address->port = lookup->port;
            set_socket_error(S3E_SOCKET_ERR_NONE);
            system_data = lookup->address;
        } else {
            set_socket_error(S3E_SOCKET_ERR_UNKNOWN_HOST);
        }
        ((s3e_callback_fn)(uintptr_t)lookup->callback)(system_data, lookup->user_data);
    }
    free_dns_lookup(lookup);
}

uint32_t s3eInetHtonl(uint32_t value) {
    return htonl(value);
}

uint32_t s3eInetNtohl(uint32_t value) {
    return ntohl(value);
}

uint16_t s3eInetHtons(uint16_t value) {
    return htons(value);
}

uint16_t s3eInetNtohs(uint16_t value) {
    return ntohs(value);
}

int32_t s3eInetAton(uint32_t *out, const char *address) {
    struct in_addr parsed;
    if (!out || !address || inet_pton(AF_INET, address, &parsed) != 1) {
        set_socket_error(S3E_SOCKET_ERR_PARAM);
        return S3E_RESULT_ERROR;
    }
    *out = parsed.s_addr;
    set_socket_error(S3E_SOCKET_ERR_NONE);
    return S3E_RESULT_SUCCESS;
}

char *s3eInetNtoa(uint32_t address, char *buffer, int32_t length) {
    struct in_addr parsed = {.s_addr = address};
    int allocated = 0;
    if (!buffer) {
        length = INET_ADDRSTRLEN;
        buffer = s3eMallocBase((uint32_t)length, __FILE__, __LINE__);
        allocated = 1;
    }
    if (!buffer || length <= 0 || !inet_ntop(AF_INET, &parsed, buffer, (socklen_t)length)) {
        if (allocated) {
            s3eFreeBase(buffer);
        }
        set_socket_error(S3E_SOCKET_ERR_PARAM);
        return NULL;
    }
    set_socket_error(S3E_SOCKET_ERR_NONE);
    return buffer;
}

const char *s3eInetToString(const struct s3e_inet_address *address, int32_t include_port) {
    static _Thread_local char buffer[INET6_ADDRSTRLEN + 16];
    char ip[INET6_ADDRSTRLEN];
    if (!address || !address_text(address, ip, sizeof(ip))) {
        set_socket_error(S3E_SOCKET_ERR_PARAM);
        return NULL;
    }
    if (include_port) {
        if (address->type & S3E_SOCKET_ADDR_IPV6) {
            snprintf(buffer, sizeof(buffer), "[%s]:%u", ip, ntohs(address->port));
        } else {
            snprintf(buffer, sizeof(buffer), "%s:%u", ip, ntohs(address->port));
        }
    } else {
        snprintf(buffer, sizeof(buffer), "%s", ip);
    }
    set_socket_error(S3E_SOCKET_ERR_NONE);
    return buffer;
}

int32_t s3eInetLookup(const char *hostname, struct s3e_inet_address *address, void *callback,
                      void *user_data) {
    if (!hostname || !address) {
        set_socket_error(S3E_SOCKET_ERR_PARAM);
        return S3E_RESULT_ERROR;
    }
    uint16_t port = address->port ? address->port : htons(80);
    const char *resolved_hostname = s3e_multiplayer_resolve_hostname(hostname);
    if (!callback) {
        struct sockaddr_storage result;
        socklen_t result_length = 0;
        int error = resolve_hostname(resolved_hostname, &result, &result_length);
        if (error != 0) {
            set_socket_error(error == EAI_FAMILY ? S3E_SOCKET_ERR_PARAM
                                                 : S3E_SOCKET_ERR_UNKNOWN_HOST);
            return S3E_RESULT_ERROR;
        }
        sockaddr_to_address((const struct sockaddr *)&result, result_length, address);
        address->port = port;
        set_socket_error(S3E_SOCKET_ERR_NONE);
        return S3E_RESULT_SUCCESS;
    }

    struct dns_lookup *lookup = calloc(1, sizeof(*lookup));
    if (!lookup) {
        set_socket_error(S3E_SOCKET_ERR_UNAVAIL);
        return S3E_RESULT_ERROR;
    }
    lookup->hostname = strdup(resolved_hostname);
    if (!lookup->hostname) {
        free(lookup);
        set_socket_error(S3E_SOCKET_ERR_UNAVAIL);
        return S3E_RESULT_ERROR;
    }
    lookup->address = address;
    lookup->callback = callback;
    lookup->user_data = user_data;
    lookup->port = port;

    pthread_mutex_lock(&g_dns_mutex);
    if (g_dns_lookup) {
        pthread_mutex_unlock(&g_dns_mutex);
        free_dns_lookup(lookup);
        set_socket_error(S3E_SOCKET_ERR_ALREADY);
        return S3E_RESULT_ERROR;
    }
    g_dns_lookup = lookup;
    pthread_mutex_unlock(&g_dns_mutex);

    int error = pthread_create(&lookup->thread, NULL, dns_lookup_thread, lookup);
    if (error != 0) {
        pthread_mutex_lock(&g_dns_mutex);
        g_dns_lookup = NULL;
        pthread_mutex_unlock(&g_dns_mutex);
        free_dns_lookup(lookup);
        set_socket_error(S3E_SOCKET_ERR_UNAVAIL);
        return S3E_RESULT_ERROR;
    }
    set_socket_error(S3E_SOCKET_ERR_NONE);
    return S3E_RESULT_SUCCESS;
}

void s3eInetLookupCancel(void) {
    pthread_mutex_lock(&g_dns_mutex);
    if (g_dns_lookup) {
        g_dns_lookup->cancelled = 1;
        g_dns_lookup->callback = NULL;
    }
    pthread_mutex_unlock(&g_dns_mutex);
}

void *s3eSocketCreate(uint32_t type, int32_t domain) {
    int family = socket_family(domain);
    int kind = socket_kind(type);
    if (family < 0 || kind < 0) {
        set_socket_error(S3E_SOCKET_ERR_UNSUPPORTED);
        return NULL;
    }
    int fd = socket(family, kind, 0);
    if (fd < 0) {
        set_socket_error(map_socket_errno(errno));
        return NULL;
    }
    if (set_nonblocking(fd) < 0) {
        int error = errno;
        close(fd);
        set_socket_error(map_socket_errno(error));
        return NULL;
    }
    if (type == S3E_SOCKET_UDP) {
        int enabled = 1;
        (void)setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled));
    }
    int slot = allocate_socket_slot(fd, (int32_t)type, domain);
    if (slot < 0) {
        close(fd);
        return NULL;
    }
    void *handle = slot_handle((uint32_t)slot);
    set_socket_error(S3E_SOCKET_ERR_NONE);
    return handle;
}

int32_t s3eSocketClose(void *socket_handle_value) {
    int slot = handle_slot(socket_handle_value);
    if (slot < 0) {
        set_socket_error(S3E_SOCKET_ERR_PARAM);
        return S3E_RESULT_ERROR;
    }
    close_socket_slot((uint32_t)slot);
    set_socket_error(S3E_SOCKET_ERR_NONE);
    return S3E_RESULT_SUCCESS;
}

int32_t s3eSocketBind(void *socket_handle_value, const struct s3e_inet_address *address,
                      uint8_t reuse_address) {
    struct socket_slot *slot = resolve_socket(socket_handle_value);
    if (!slot) {
        return S3E_RESULT_ERROR;
    }
    struct s3e_inet_address any_address = {0};
    if (!address) {
        address = &any_address;
    }
    struct sockaddr_storage storage;
    socklen_t length;
    if (address_to_sockaddr(address, &storage, &length) < 0) {
        return S3E_RESULT_ERROR;
    }
    if (reuse_address) {
        int enabled = 1;
        if (setsockopt(slot->fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) < 0) {
            return fail_with_errno(errno);
        }
    }
    if (bind(slot->fd, (const struct sockaddr *)&storage, length) < 0) {
        return fail_with_errno(errno);
    }
    set_socket_error(S3E_SOCKET_ERR_NONE);
    return S3E_RESULT_SUCCESS;
}

int32_t s3eSocketListen(void *socket_handle_value, uint16_t backlog) {
    struct socket_slot *slot = resolve_socket(socket_handle_value);
    if (!slot) {
        return S3E_RESULT_ERROR;
    }
    if (listen(slot->fd, backlog) < 0) {
        return fail_with_errno(errno);
    }
    set_socket_error(S3E_SOCKET_ERR_NONE);
    return S3E_RESULT_SUCCESS;
}

void *s3eSocketAccept(void *socket_handle_value, struct s3e_inet_address *address, void *callback,
                      void *user_data) {
    struct socket_slot *slot = resolve_socket(socket_handle_value);
    if (!slot) {
        return NULL;
    }
    struct sockaddr_storage peer = {0};
    socklen_t peer_length = sizeof(peer);
    int fd = accept(slot->fd, (struct sockaddr *)&peer, &peer_length);
    if (fd < 0) {
        int error = errno;
        if ((error == EAGAIN || error == EWOULDBLOCK) && callback) {
            slot->accept_callback = callback;
            slot->accept_user_data = user_data;
            slot->readable_callback = NULL;
            slot->readable_user_data = NULL;
        }
        set_socket_error(map_socket_errno(error));
        return NULL;
    }
    if (set_nonblocking(fd) < 0) {
        int error = errno;
        close(fd);
        set_socket_error(map_socket_errno(error));
        return NULL;
    }
    int accepted_slot = allocate_socket_slot(fd, S3E_SOCKET_TCP, slot->domain);
    if (accepted_slot < 0) {
        close(fd);
        return NULL;
    }
    slot->accept_callback = NULL;
    slot->accept_user_data = NULL;
    if (address) {
        sockaddr_to_address((const struct sockaddr *)&peer, peer_length, address);
    }
    void *accepted = slot_handle((uint32_t)accepted_slot);
    set_socket_error(S3E_SOCKET_ERR_NONE);
    return accepted;
}

int32_t s3eSocketConnect(void *socket_handle_value, const struct s3e_inet_address *address,
                         void *callback, void *user_data) {
    struct socket_slot *slot = resolve_socket(socket_handle_value);
    if (!slot) {
        return S3E_RESULT_ERROR;
    }
    struct sockaddr_storage storage;
    socklen_t length;
    if (address_to_sockaddr(address, &storage, &length) < 0) {
        return S3E_RESULT_ERROR;
    }
    int result = connect(slot->fd, (const struct sockaddr *)&storage, length);
    if (result == 0) {
        slot->connect_pending = callback != NULL;
        slot->connect_callback = callback;
        slot->connect_user_data = callback ? user_data : NULL;
        set_socket_error(S3E_SOCKET_ERR_NONE);
        return S3E_RESULT_SUCCESS;
    }
    int error = errno;
    if (error == EINPROGRESS || error == EALREADY) {
        slot->connect_pending = 1;
        slot->connect_callback = callback;
        slot->connect_user_data = callback ? user_data : NULL;
        set_socket_error(S3E_SOCKET_ERR_INPROGRESS);
        return S3E_RESULT_SUCCESS;
    }
    return fail_with_errno(error);
}

static int send_flags(int32_t flags) {
    (void)flags;
    int native_flags = 0;
#ifdef MSG_NOSIGNAL
    native_flags |= MSG_NOSIGNAL;
#endif
    return native_flags;
}

int32_t s3eSocketSend(void *socket_handle_value, const char *buffer, uint32_t length,
                      int32_t flags) {
    struct socket_slot *slot = resolve_socket(socket_handle_value);
    if (!slot || (!buffer && length)) {
        set_socket_error(S3E_SOCKET_ERR_PARAM);
        return -1;
    }
    ssize_t sent = send(slot->fd, buffer, length, send_flags(flags));
    int native_errno = sent < 0 ? errno : 0;
    if (sent < 0) {
        set_socket_error(map_socket_errno(native_errno));
        return -1;
    }
    set_socket_error(S3E_SOCKET_ERR_NONE);
    return (int32_t)sent;
}

int32_t s3eSocketSendTo(void *socket_handle_value, const char *buffer, uint32_t length,
                        int32_t flags, const struct s3e_inet_address *address) {
    struct socket_slot *slot = resolve_socket(socket_handle_value);
    if (!slot || (!buffer && length)) {
        set_socket_error(S3E_SOCKET_ERR_PARAM);
        return -1;
    }
    struct sockaddr_storage storage;
    socklen_t address_length;
    if (address_to_sockaddr(address, &storage, &address_length) < 0) {
        return -1;
    }
    ssize_t sent = sendto(slot->fd, buffer, length, send_flags(flags),
                          (const struct sockaddr *)&storage, address_length);
    if (sent < 0) {
        set_socket_error(map_socket_errno(errno));
        return -1;
    }
    set_socket_error(S3E_SOCKET_ERR_NONE);
    return (int32_t)sent;
}

int32_t s3eSocketRecv(void *socket_handle_value, char *buffer, uint32_t length, int32_t flags) {
    (void)flags;
    struct socket_slot *slot = resolve_socket(socket_handle_value);
    if (!slot || (!buffer && length)) {
        set_socket_error(S3E_SOCKET_ERR_PARAM);
        return -1;
    }
    ssize_t received = recv(slot->fd, buffer, length, 0);
    if (received < 0) {
        set_socket_error(map_socket_errno(errno));
        return -1;
    }
    set_socket_error(S3E_SOCKET_ERR_NONE);
    return (int32_t)received;
}

int32_t s3eSocketRecvFrom(void *socket_handle_value, char *buffer, uint32_t length, int32_t flags,
                          struct s3e_inet_address *address) {
    (void)flags;
    struct socket_slot *slot = resolve_socket(socket_handle_value);
    if (!slot || (!buffer && length)) {
        set_socket_error(S3E_SOCKET_ERR_PARAM);
        return -1;
    }
    struct sockaddr_storage source;
    socklen_t source_length = sizeof(source);
    ssize_t received =
        recvfrom(slot->fd, buffer, length, 0, (struct sockaddr *)&source, &source_length);
    if (received < 0) {
        set_socket_error(map_socket_errno(errno));
        return -1;
    }
    struct s3e_inet_address source_address;
    sockaddr_to_address((const struct sockaddr *)&source, source_length, &source_address);
    if (address) {
        *address = source_address;
    }
    set_socket_error(S3E_SOCKET_ERR_NONE);
    return (int32_t)received;
}

int32_t s3eSocketReadable(void *socket_handle_value, void *callback, void *user_data) {
    struct socket_slot *slot = resolve_socket(socket_handle_value);
    if (!slot) {
        return S3E_RESULT_ERROR;
    }
    slot->readable_callback = callback;
    slot->readable_user_data = callback ? user_data : NULL;
    slot->accept_callback = NULL;
    slot->accept_user_data = NULL;
    set_socket_error(S3E_SOCKET_ERR_NONE);
    return S3E_RESULT_SUCCESS;
}

int32_t s3eSocketWritable(void *socket_handle_value, void *callback, void *user_data) {
    struct socket_slot *slot = resolve_socket(socket_handle_value);
    if (!slot) {
        return S3E_RESULT_ERROR;
    }
    slot->writable_callback = callback;
    slot->writable_user_data = callback ? user_data : NULL;
    set_socket_error(S3E_SOCKET_ERR_NONE);
    return S3E_RESULT_SUCCESS;
}

int32_t s3eSocketGetInt(uint32_t key) {
    switch (key) {
    case S3E_SOCKET_MAX_SOCKETS:
        return S3E_SOCKET_SLOT_COUNT;
    case S3E_SOCKET_NETWORK_AVAILABLE:
    case S3E_SOCKET_UDP_AVAILABLE:
        return 1;
    case S3E_SOCKET_NETWORK_TYPE:
        return S3E_NETWORK_TYPE_WLAN;
    default:
        set_socket_error(S3E_SOCKET_ERR_PARAM);
        return -1;
    }
}

int32_t s3eSocketGetError(void) {
    int32_t error = g_socket_error;
    g_socket_error = S3E_SOCKET_ERR_NONE;
    return error;
}

const char *s3eSocketGetString(uint32_t key) {
    static _Thread_local char hostname[256];
    switch (key) {
    case S3E_SOCKET_DOMAINNAME: {
        if (gethostname(hostname, sizeof(hostname)) != 0) {
            hostname[0] = '\0';
        }
        hostname[sizeof(hostname) - 1] = '\0';
        char *dot = strchr(hostname, '.');
        set_socket_error(S3E_SOCKET_ERR_NONE);
        return dot ? dot + 1 : "";
    }
    case S3E_SOCKET_HOSTNAME:
        if (gethostname(hostname, sizeof(hostname)) != 0) {
            hostname[0] = '\0';
        }
        hostname[sizeof(hostname) - 1] = '\0';
        set_socket_error(S3E_SOCKET_ERR_NONE);
        return hostname;
    case S3E_SOCKET_HTTP_PROXY:
        set_socket_error(S3E_SOCKET_ERR_NONE);
        return "";
    default:
        set_socket_error(S3E_SOCKET_ERR_PARAM);
        return NULL;
    }
}

static int32_t get_socket_name(void *socket_handle_value, struct s3e_inet_address *address,
                               int peer) {
    struct socket_slot *slot = resolve_socket(socket_handle_value);
    if (!slot || !address) {
        set_socket_error(S3E_SOCKET_ERR_PARAM);
        return S3E_RESULT_ERROR;
    }
    struct sockaddr_storage storage;
    socklen_t length = sizeof(storage);
    int result = peer ? getpeername(slot->fd, (struct sockaddr *)&storage, &length)
                      : getsockname(slot->fd, (struct sockaddr *)&storage, &length);
    if (result < 0) {
        return fail_with_errno(errno);
    }
    sockaddr_to_address((const struct sockaddr *)&storage, length, address);
    set_socket_error(S3E_SOCKET_ERR_NONE);
    return S3E_RESULT_SUCCESS;
}

int32_t s3eSocketGetLocalName(void *socket_handle_value, struct s3e_inet_address *address) {
    return get_socket_name(socket_handle_value, address, 0);
}

int32_t s3eSocketGetPeerName(void *socket_handle_value, struct s3e_inet_address *address) {
    return get_socket_name(socket_handle_value, address, 1);
}

static int slot_matches(uint32_t index, uint32_t generation) {
    return index < S3E_SOCKET_SLOT_COUNT && g_socket_slots[index].in_use &&
           g_socket_slots[index].generation == generation;
}

static void invoke_socket_callback(void *callback, void *socket, void *system_data,
                                   void *user_data) {
    if (callback) {
        ((s3e_socket_callback_fn)(uintptr_t)callback)(socket, system_data, user_data);
    }
}

static void dispatch_socket_event(const struct socket_poll_entry *entry) {
    uint32_t index = entry->slot;
    if (!slot_matches(index, entry->generation)) {
        return;
    }
    struct socket_slot *slot = &g_socket_slots[index];
    void *handle = slot_handle(index);
    short events = entry->pollfd.revents;

    if (slot->connect_pending && (events & (POLLOUT | POLLERR | POLLHUP | POLLNVAL))) {
        int native_error = 0;
        socklen_t error_length = sizeof(native_error);
        if (getsockopt(slot->fd, SOL_SOCKET, SO_ERROR, &native_error, &error_length) < 0) {
            native_error = errno;
        }
        void *callback = slot->connect_callback;
        void *user_data = slot->connect_user_data;
        slot->connect_pending = 0;
        slot->connect_callback = NULL;
        slot->connect_user_data = NULL;
        int32_t result = native_error == 0 ? S3E_RESULT_SUCCESS : S3E_RESULT_ERROR;
        set_socket_error(map_socket_errno(native_error));
        invoke_socket_callback(callback, handle, &result, user_data);
        if (!slot_matches(index, entry->generation)) {
            return;
        }
        slot = &g_socket_slots[index];
    }

    if (slot->accept_callback && (events & (POLLIN | POLLERR | POLLHUP | POLLNVAL))) {
        void *callback = slot->accept_callback;
        void *user_data = slot->accept_user_data;
        slot->accept_callback = NULL;
        slot->accept_user_data = NULL;
        set_socket_error(S3E_SOCKET_ERR_NONE);
        invoke_socket_callback(callback, handle, NULL, user_data);
        if (!slot_matches(index, entry->generation)) {
            return;
        }
        slot = &g_socket_slots[index];
    }

    if (slot->readable_callback && (events & (POLLIN | POLLERR | POLLHUP | POLLNVAL))) {
        void *callback = slot->readable_callback;
        void *user_data = slot->readable_user_data;
        slot->readable_callback = NULL;
        slot->readable_user_data = NULL;
        set_socket_error(S3E_SOCKET_ERR_NONE);
        invoke_socket_callback(callback, handle, NULL, user_data);
        if (!slot_matches(index, entry->generation)) {
            return;
        }
        slot = &g_socket_slots[index];
    }

    if (slot->writable_callback && (events & (POLLOUT | POLLERR | POLLHUP | POLLNVAL))) {
        void *callback = slot->writable_callback;
        void *user_data = slot->writable_user_data;
        slot->writable_callback = NULL;
        slot->writable_user_data = NULL;
        set_socket_error(S3E_SOCKET_ERR_NONE);
        invoke_socket_callback(callback, handle, NULL, user_data);
    }
}

void s3e_socket_pump(void) {
    pump_dns_lookup();

    struct socket_poll_entry entries[S3E_SOCKET_SLOT_COUNT];
    nfds_t count = 0;
    for (uint32_t i = 0; i < S3E_SOCKET_SLOT_COUNT; ++i) {
        struct socket_slot *slot = &g_socket_slots[i];
        if (!slot->in_use) {
            continue;
        }
        short events = 0;
        if (slot->accept_callback || slot->readable_callback) {
            events |= POLLIN;
        }
        if (slot->connect_pending || slot->writable_callback) {
            events |= POLLOUT;
        }
        if (!events) {
            continue;
        }
        entries[count].pollfd.fd = slot->fd;
        entries[count].pollfd.events = events;
        entries[count].pollfd.revents = 0;
        entries[count].slot = i;
        entries[count].generation = slot->generation;
        ++count;
    }
    if (!count) {
        return;
    }

    struct pollfd pollfds[S3E_SOCKET_SLOT_COUNT];
    for (nfds_t i = 0; i < count; ++i) {
        pollfds[i] = entries[i].pollfd;
    }
    int ready = poll(pollfds, count, 0);
    if (ready <= 0) {
        return;
    }
    for (nfds_t i = 0; i < count; ++i) {
        if (!pollfds[i].revents) {
            continue;
        }
        entries[i].pollfd.revents = pollfds[i].revents;
        dispatch_socket_event(&entries[i]);
    }
}

void s3e_socket_shutdown(void) {
    for (uint32_t i = 0; i < S3E_SOCKET_SLOT_COUNT; ++i) {
        if (g_socket_slots[i].in_use) {
            close_socket_slot(i);
        }
    }
    pthread_mutex_lock(&g_dns_mutex);
    struct dns_lookup *lookup = g_dns_lookup;
    if (lookup) {
        lookup->cancelled = 1;
        lookup->callback = NULL;
    }
    pthread_mutex_unlock(&g_dns_mutex);
    if (lookup) {
        (void)pthread_join(lookup->thread, NULL);
        pthread_mutex_lock(&g_dns_mutex);
        if (g_dns_lookup == lookup) {
            g_dns_lookup = NULL;
        }
        pthread_mutex_unlock(&g_dns_mutex);
        free_dns_lookup(lookup);
    }
    set_socket_error(S3E_SOCKET_ERR_NONE);
}
