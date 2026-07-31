#include "s3e_host_internal.h"

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
    assert(!s3e_multiplayer_server_enabled());
    assert(!s3e_multiplayer_proxy_enabled());
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

    assert(s3e_multiplayer_server_enabled());
    assert(!s3e_multiplayer_proxy_enabled());
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
    write_control_config("multiplayer_server=\n"
                         "multiplayer_proxy=not-a-number\n");
    s3e_host_set_config((const uint8_t *)embedded_config, (uint32_t)strlen(embedded_config));
    assert(!s3e_multiplayer_server_enabled());
    assert(!s3e_multiplayer_proxy_enabled());

    write_control_config("multiplayer_server=relay.example\n"
                         "multiplayer_proxy=-1\n");
    s3e_host_set_config((const uint8_t *)embedded_config, (uint32_t)strlen(embedded_config));
    assert(!s3e_multiplayer_server_enabled());
    assert(s3e_multiplayer_proxy_enabled());
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

int main(void) {
    char template[] = "/tmp/codboz-config.XXXXXX";
    char *directory = mkdtemp(template);
    assert(directory);
    snprintf(g_root, sizeof(g_root), "%s", directory);

    test_offline_defaults();
    test_direct_multiplayer_server();
    test_invalid_values_and_proxy_flag();
    test_voice_chat_opt_in();

    int result = rmdir(g_root);
    assert(result == 0);
    puts("s3e config tests passed");
    return 0;
}
