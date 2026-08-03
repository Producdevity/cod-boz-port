#include "s3e_host_internal.h"
#include "s3e_image.h"

#include <assert.h>

char g_root[1024];

static const char embedded_config[] = "[Demonware]\n"
                                      "LSGServer=codboh-iphone-lobby.prod.demonware.net:3074\n"
                                      "AuthServer=codboh-iphone-auth.prod.demonware.net:3074\n"
                                      "[STUN]\n"
                                      "STUNServer=codboh-iphone.stun.demonware.net\n"
                                      "STUNServerPort=3478\n"
                                      "[GAME]\n"
                                      "OnlineAccount=GC\n"
                                      "GameVersion=1.0.9\n"
                                      "MatchmakingSearchAndPublishMode=1\n"
                                      "[ONLINE]\n"
                                      "dispatcher=/isonline/dispatcher.php\n";

static void write_control_config(const char *contents) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/config.txt", g_root);
    FILE *file = fopen(path, "w");
    assert(file);
    int result = fputs(contents, file);
    assert(result >= 0);
    result = fclose(file);
    assert(result == 0);
}

static void remove_control_config(void) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/config.txt", g_root);
    int result = unlink(path);
    assert(result == 0);
}

static void assert_config_string(const char *section, const char *key, const char *expected) {
    char value[256];
    memset(value, 0, sizeof(value));
    assert(s3eConfigGetString(section, key, value) == 0);
    assert(strcmp(value, expected) == 0);
}

static void test_offline_defaults(void) {
    s3e_host_set_config((const uint8_t *)embedded_config, (uint32_t)strlen(embedded_config));
    assert_config_string("GAME", "OnlineAccount", "NONE");
    assert_config_string("GAME", "MatchmakingSearchAndPublishMode", "0");
    assert_config_string("Demonware", "AuthServer", "");
    assert_config_string("Demonware", "LSGServer", "");
    assert_config_string("ONLINE", "dispatcher", "");
    assert_config_string("GAME", "VoiceChatEnabled", "0");
}

static void test_direct_multiplayer_server(void) {
    write_control_config("multiplayer_server=192.0.2.10\n"
                         "multiplayer_proxy=0\n");
    s3e_host_set_config((const uint8_t *)embedded_config, (uint32_t)strlen(embedded_config));

    assert_config_string("GAME", "OnlineAccount", "GENERIC");
    assert_config_string("GAME", "GameVersion", "1.0.11");
    assert_config_string("GAME", "MatchmakingSearchAndPublishMode", "1");
    assert_config_string("Demonware", "AuthServer", "codboh-iphone-auth.prod.demonware.net:3074");
    assert_config_string("Demonware", "LSGServer", "codboh-iphone-lobby.prod.demonware.net:3074");
    assert_config_string("ONLINE", "dispatcher", "/isonline/dispatcher.php");

    assert(strcmp(s3e_multiplayer_resolve_hostname("codboh-iphone-auth.prod.demonware.net"),
                  "192.0.2.10") == 0);
    assert(strcmp(s3e_multiplayer_resolve_hostname(".demonware.net"), "192.0.2.10") == 0);
    assert(strcmp(s3e_multiplayer_resolve_hostname("example.net"), "example.net") == 0);
    assert(strcmp(s3e_multiplayer_resolve_hostname("host.Demonware.net"), "192.0.2.10") == 0);
    assert(s3e_multiplayer_resolve_hostname(NULL) == NULL);

    remove_control_config();
}

static void test_invalid_values_and_proxy_flag(void) {
    write_control_config("multiplayer_server=direct.example\n"
                         "multiplayer_proxy=not-a-number\n");
    s3e_host_set_config((const uint8_t *)embedded_config, (uint32_t)strlen(embedded_config));
    assert_config_string("GAME", "OnlineAccount", "GENERIC");
    assert(strcmp(s3e_multiplayer_resolve_hostname("auth.demonware.net"), "direct.example") == 0);

    write_control_config("multiplayer_server=relay.example\n"
                         "multiplayer_proxy=-1\n");
    s3e_host_set_config((const uint8_t *)embedded_config, (uint32_t)strlen(embedded_config));
    assert_config_string("GAME", "OnlineAccount", "NONE");
    assert(strcmp(s3e_multiplayer_resolve_hostname("auth.demonware.net"), "auth.demonware.net") ==
           0);
    remove_control_config();
}

static void test_voice_chat_opt_in(void) {
    write_control_config("voice_chat=1\n");
    s3e_host_set_config((const uint8_t *)embedded_config, (uint32_t)strlen(embedded_config));
    assert_config_string("GAME", "VoiceChatEnabled", "1");

    write_control_config("voice_chat=invalid\n");
    s3e_host_set_config((const uint8_t *)embedded_config, (uint32_t)strlen(embedded_config));
    assert_config_string("GAME", "VoiceChatEnabled", "0");
    remove_control_config();
}

static void test_player_name(void) {
    write_control_config("player_name=Producdevity\n");
    s3e_host_set_config((const uint8_t *)embedded_config, (uint32_t)strlen(embedded_config));
    assert(strcmp(s3e_host_player_name(), "Producdevity") == 0);

    write_control_config("player_name=too-long-player-name\n");
    s3e_host_set_config((const uint8_t *)embedded_config, (uint32_t)strlen(embedded_config));
    assert(strcmp(s3e_host_player_name(), "Player") == 0);
    remove_control_config();
}

static void test_player_name_patch(void) {
    enum {
        reference_offset = 0x18f74c,
        reference_pc_offset = 0x18f60a,
        format_offset = 0x3af131,
    };
    size_t size = format_offset + sizeof("Player-%d");
    uint8_t *memory = calloc(1, size);
    assert(memory);
    memcpy(memory + format_offset, "Player-%d", sizeof("Player-%d"));
    uint32_t original = format_offset - reference_pc_offset;
    memcpy(memory + reference_offset, &original, sizeof(original));

    struct s3e_loaded_image loaded = {.base = memory, .map_size = size};
    uint32_t name_address = 0x12345678;

    memory[format_offset] = 'X';
    assert(!codboz_override_player_name(&loaded, name_address));
    memory[format_offset] = 'P';

    assert(codboz_override_player_name(&loaded, name_address));
    uint32_t patched;
    memcpy(&patched, memory + reference_offset, sizeof(patched));
    assert(patched == name_address - ((uint32_t)(uintptr_t)memory + reference_pc_offset));
    free(memory);
}

int main(void) {
    char template[] = "/tmp/codboz-config.XXXXXX";
    char *directory = mkdtemp(template);
    assert(directory);
    snprintf(g_root, sizeof(g_root), "%s", directory);

    test_offline_defaults();
    test_direct_multiplayer_server();
    test_invalid_values_and_proxy_flag();
    test_voice_chat_opt_in();
    test_player_name();
    test_player_name_patch();

    int result = rmdir(g_root);
    assert(result == 0);
    puts("s3e config tests passed");
    return 0;
}
