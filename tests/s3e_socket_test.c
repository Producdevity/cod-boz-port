#include "s3e_host_internal.h"

#include <assert.h>

enum {
    TEST_RESULT_SUCCESS = 0,
    TEST_RESULT_ERROR = 1,
    TEST_HANDLE_FIRST = 3000,
    TEST_IO_TIMEOUT_MS = 5000,
};

char g_root[1024];

static int g_readable_count;
static int g_accept_count;
static int g_connect_count;
static int g_connect_result;
static int g_dns_count;
static void *g_expected_readable_socket;
static void *g_expected_readable_user_data;
static void *g_expected_accept_socket;
static void *g_expected_accept_user_data;
static void *g_expected_connect_socket;
static void *g_expected_connect_user_data;
static void *g_expected_dns_user_data;
static struct s3e_inet_address *g_expected_dns_address;

void *s3eMallocBase(uint32_t size, const char *file, int line) {
    (void)file;
    (void)line;
    return calloc(1, size ? size : 1);
}

void s3eFreeBase(void *pointer) {
    free(pointer);
}

uint64_t monotonic_ms(void) {
    struct timespec now;
    int result = clock_gettime(CLOCK_MONOTONIC, &now);
    assert(result == 0);
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static void pump_until(int *counter) {
    uint64_t deadline = monotonic_ms() + TEST_IO_TIMEOUT_MS;
    while (!*counter && monotonic_ms() < deadline) {
        s3e_socket_pump();
        struct timespec pause = {.tv_nsec = 1000000L};
        (void)nanosleep(&pause, NULL);
    }
    assert(*counter == 1);
}

static struct s3e_inet_address ipv4_address(const char *ip, uint16_t port) {
    struct s3e_inet_address address = {0};
    address.type = S3E_SOCKET_ADDR_IPV4;
    int result = inet_pton(AF_INET, ip, &address.ip_address);
    assert(result == 1);
    address.port = htons(port);
    return address;
}

static int32_t readable_callback(void *socket, void *system_data, void *user_data) {
    assert(socket == g_expected_readable_socket);
    assert(system_data == NULL);
    assert(user_data == g_expected_readable_user_data);
    ++g_readable_count;
    return 0;
}

static int32_t accept_callback(void *socket, void *system_data, void *user_data) {
    assert(socket == g_expected_accept_socket);
    assert(system_data == NULL);
    assert(user_data == g_expected_accept_user_data);
    ++g_accept_count;
    return 0;
}

static int32_t connect_callback(void *socket, void *system_data, void *user_data) {
    assert(socket == g_expected_connect_socket);
    assert(system_data != NULL);
    assert(user_data == g_expected_connect_user_data);
    g_connect_result = *(int32_t *)system_data;
    ++g_connect_count;
    return 0;
}

static int32_t dns_callback(void *system_data, void *user_data) {
    assert(system_data == g_expected_dns_address);
    assert(user_data == g_expected_dns_user_data);
    ++g_dns_count;
    return 0;
}

static void write_multiplayer_config(void) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/config.txt", g_root);
    FILE *file = fopen(path, "w");
    assert(file);
    int result = fputs("multiplayer_server=127.0.0.1\nmultiplayer_proxy=0\n", file);
    assert(result >= 0);
    result = fclose(file);
    assert(result == 0);
    s3e_host_set_config(NULL, 0);
}

static void remove_multiplayer_config(void) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/config.txt", g_root);
    int result = unlink(path);
    assert(result == 0);
    s3e_host_set_config(NULL, 0);
}

static void test_inet_helpers(void) {
    uint32_t address = 0;
    assert(s3eInetAton(&address, "127.0.0.1") == TEST_RESULT_SUCCESS);
    assert(s3eSocketGetError() == S3E_SOCKET_ERR_NONE);

    char buffer[INET_ADDRSTRLEN];
    assert(s3eInetNtoa(address, buffer, sizeof(buffer)) == buffer);
    assert(strcmp(buffer, "127.0.0.1") == 0);

    char *allocated = s3eInetNtoa(address, NULL, 0);
    assert(allocated);
    assert(strcmp(allocated, "127.0.0.1") == 0);
    s3eFreeBase(allocated);

    struct s3e_inet_address full = ipv4_address("127.0.0.1", 28960);
    assert(strcmp(s3eInetToString(&full, 0), "127.0.0.1") == 0);
    assert(strcmp(s3eInetToString(&full, 1), "127.0.0.1:28960") == 0);

    assert(s3eInetAton(NULL, "not-an-address") == TEST_RESULT_ERROR);
    assert(s3eSocketGetError() == S3E_SOCKET_ERR_PARAM);
    assert(s3eSocketGetError() == S3E_SOCKET_ERR_PARAM);
    assert(s3eSocketGetInt(S3E_SOCKET_MAX_SOCKETS) == 32);
    assert(s3eSocketGetError() == S3E_SOCKET_ERR_NONE);
    assert(s3eSocketGetInt(UINT32_MAX) == -1);
    assert(s3eSocketGetError() == S3E_SOCKET_ERR_PARAM);
}

static void test_handle_limit(void) {
    void *sockets[32];
    for (size_t i = 0; i < 32; ++i) {
        sockets[i] = s3eSocketCreate(S3E_SOCKET_UDP, S3E_SOCKET_INET);
        assert(sockets[i] == (void *)(uintptr_t)(TEST_HANDLE_FIRST + i));
    }
    assert(s3eSocketCreate(S3E_SOCKET_UDP, S3E_SOCKET_INET) == NULL);
    assert(s3eSocketGetError() == S3E_SOCKET_ERR_TOO_MANY);
    for (size_t i = 0; i < 32; ++i) {
        assert(s3eSocketClose(sockets[i]) == TEST_RESULT_SUCCESS);
    }
}

static void test_udp(void) {
    void *receiver = s3eSocketCreate(S3E_SOCKET_UDP, S3E_SOCKET_INET);
    void *sender = s3eSocketCreate(S3E_SOCKET_UDP, S3E_SOCKET_INET);
    assert(receiver && sender);
    struct s3e_inet_address receiver_address = ipv4_address("127.0.0.1", 0);
    assert(s3eSocketBind(receiver, &receiver_address, 1) == TEST_RESULT_SUCCESS);
    assert(s3eSocketGetLocalName(receiver, &receiver_address) == TEST_RESULT_SUCCESS);
    assert(receiver_address.type == S3E_SOCKET_ADDR_IPV4);
    assert(receiver_address.port != 0);

    int marker = 17;
    g_expected_readable_socket = receiver;
    g_expected_readable_user_data = &marker;
    g_readable_count = 0;
    assert(s3eSocketReadable(receiver, readable_callback, &marker) == TEST_RESULT_SUCCESS);

    const char payload[] = "udp-loopback";
    assert(s3eSocketSendTo(sender, payload, sizeof(payload), 0, &receiver_address) ==
           (int32_t)sizeof(payload));
    pump_until(&g_readable_count);

    char received[64];
    struct s3e_inet_address source;
    assert(s3eSocketRecvFrom(receiver, received, sizeof(received), 0, &source) ==
           (int32_t)sizeof(payload));
    assert(memcmp(received, payload, sizeof(payload)) == 0);
    assert(source.type == S3E_SOCKET_ADDR_IPV4);

    assert(s3eSocketClose(sender) == TEST_RESULT_SUCCESS);
    assert(s3eSocketClose(receiver) == TEST_RESULT_SUCCESS);
}

static void test_tcp(void) {
    void *listener = s3eSocketCreate(S3E_SOCKET_TCP, S3E_SOCKET_INET);
    void *client = s3eSocketCreate(S3E_SOCKET_TCP, S3E_SOCKET_INET);
    assert(listener && client);

    struct s3e_inet_address address = ipv4_address("127.0.0.1", 0);
    assert(s3eSocketBind(listener, &address, 1) == TEST_RESULT_SUCCESS);
    assert(s3eSocketGetLocalName(listener, &address) == TEST_RESULT_SUCCESS);
    assert(s3eSocketListen(listener, 1) == TEST_RESULT_SUCCESS);

    int accept_marker = 23;
    g_expected_accept_socket = listener;
    g_expected_accept_user_data = &accept_marker;
    g_accept_count = 0;
    assert(s3eSocketAccept(listener, NULL, accept_callback, &accept_marker) == NULL);
    assert(s3eSocketGetError() == S3E_SOCKET_ERR_WOULDBLOCK);

    int connect_marker = 29;
    g_expected_connect_socket = client;
    g_expected_connect_user_data = &connect_marker;
    g_connect_count = 0;
    g_connect_result = TEST_RESULT_ERROR;
    assert(s3eSocketConnect(client, &address, connect_callback, &connect_marker) ==
           TEST_RESULT_SUCCESS);
    pump_until(&g_connect_count);
    assert(g_connect_result == TEST_RESULT_SUCCESS);

    pump_until(&g_accept_count);

    struct s3e_inet_address peer;
    void *server = s3eSocketAccept(listener, &peer, NULL, NULL);
    assert(server);
    assert(peer.type == S3E_SOCKET_ADDR_IPV4);

    const char payload[] = "tcp-loopback";
    assert(s3eSocketSend(client, payload, sizeof(payload), 0) == (int32_t)sizeof(payload));
    char received[64];
    int32_t count = -1;
    uint64_t deadline = monotonic_ms() + TEST_IO_TIMEOUT_MS;
    while (count < 0 && monotonic_ms() < deadline) {
        count = s3eSocketRecv(server, received, sizeof(received), 0);
        if (count < 0) {
            assert(s3eSocketGetError() == S3E_SOCKET_ERR_WOULDBLOCK);
        }
    }
    assert(count == (int32_t)sizeof(payload));
    assert(memcmp(received, payload, sizeof(payload)) == 0);

    assert(s3eSocketClose(server) == TEST_RESULT_SUCCESS);
    assert(s3eSocketClose(client) == TEST_RESULT_SUCCESS);
    assert(s3eSocketClose(listener) == TEST_RESULT_SUCCESS);
}

static void test_dns_redirect_and_callback(void) {
    struct s3e_inet_address resolved = {.port = htons(3074)};
    assert(s3eInetLookup("unresolvable.invalid.demonware.net", &resolved, NULL, NULL) ==
           TEST_RESULT_SUCCESS);
    struct s3e_inet_address loopback = ipv4_address("127.0.0.1", 3074);
    assert(resolved.type == S3E_SOCKET_ADDR_IPV4);
    assert(resolved.ip_address == loopback.ip_address);
    assert(resolved.port == loopback.port);

    int marker = 41;
    memset(&resolved, 0, sizeof(resolved));
    resolved.port = htons(3478);
    g_expected_dns_address = &resolved;
    g_expected_dns_user_data = &marker;
    g_dns_count = 0;
    assert(s3eInetLookup("localhost", &resolved, (void *)(uintptr_t)dns_callback, &marker) ==
           TEST_RESULT_SUCCESS);
    pump_until(&g_dns_count);
    assert(resolved.type == S3E_SOCKET_ADDR_IPV4);
    assert(ntohs(resolved.port) == 3478);
}

int main(void) {
    char template[] = "/tmp/codboz-socket.XXXXXX";
    char *directory = mkdtemp(template);
    assert(directory);
    snprintf(g_root, sizeof(g_root), "%s", directory);

    test_inet_helpers();
    test_handle_limit();
    test_udp();
    test_tcp();
    write_multiplayer_config();
    test_dns_redirect_and_callback();
    remove_multiplayer_config();
    s3e_socket_shutdown();

    int result = rmdir(g_root);
    assert(result == 0);
    puts("s3e socket tests passed");
    return 0;
}
