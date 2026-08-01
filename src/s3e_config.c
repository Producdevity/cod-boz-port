#include "s3e_host_internal.h"

static struct s3e_config_entry g_config_entries[S3E_CONFIG_MAX_ENTRIES];
static size_t g_config_entry_count;
static char g_multiplayer_server[128];
static bool g_multiplayer_proxy;
static bool g_voice_chat;

static char *trim(char *text) {
    while (*text && isspace((unsigned char)*text)) {
        text++;
    }
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        *--end = 0;
    }
    return text;
}

static void strip_comment(char *line) {
    int in_quote = 0;
    for (char *p = line; *p; ++p) {
        if (*p == '"') {
            in_quote = !in_quote;
        } else if (*p == '#' && !in_quote) {
            *p = 0;
            return;
        }
    }
}

static void strip_quotes(char *value) {
    size_t len = strlen(value);
    if (len >= 2 && value[0] == '"' && value[len - 1] == '"') {
        memmove(value, value + 1, len - 2);
        value[len - 2] = 0;
    }
}

static void parse_flag(const char *value, bool *out) {
    if (!value || !out || !value[0]) {
        return;
    }
    char *end = NULL;
    errno = 0;
    long parsed = strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != 0) {
        return;
    }
    *out = parsed != 0;
}

static void load_control_config(void) {
    g_multiplayer_server[0] = 0;
    g_multiplayer_proxy = false;
    g_voice_chat = false;

    char path[1200];
    snprintf(path, sizeof(path), "%s/config.txt", g_root);
    FILE *file = fopen(path, "r");
    if (!file) {
        if (errno != ENOENT) {
            fprintf(stderr, "[multiplayer] unable to read %s: %s\n", path, strerror(errno));
        }
        return;
    }

    char line[512];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = 0;
        strip_comment(line);
        char *clean = trim(line);
        if (!clean[0]) {
            continue;
        }
        char *equals = strchr(clean, '=');
        if (!equals) {
            continue;
        }
        *equals = 0;
        char *key = trim(clean);
        char *value = trim(equals + 1);
        strip_quotes(value);

        if (strcmp(key, "multiplayer_server") == 0) {
            snprintf(g_multiplayer_server, sizeof(g_multiplayer_server), "%s", value);
        } else if (strcmp(key, "multiplayer_proxy") == 0) {
            parse_flag(value, &g_multiplayer_proxy);
        } else if (strcmp(key, "voice_chat") == 0) {
            parse_flag(value, &g_voice_chat);
        }
    }
    fclose(file);

    if (g_multiplayer_server[0] && g_multiplayer_proxy) {
        fprintf(stderr, "[multiplayer] proxy mode is not supported\n");
    }
}

static bool multiplayer_server_enabled(void) {
    return g_multiplayer_server[0] != 0 && !g_multiplayer_proxy;
}

const char *s3e_multiplayer_resolve_hostname(const char *hostname) {
    static const char suffix[] = ".demonware.net";
    if (!hostname || !multiplayer_server_enabled()) {
        return hostname;
    }
    size_t hostname_length = strlen(hostname);
    size_t suffix_length = sizeof(suffix) - 1;
    if (hostname_length >= suffix_length &&
        strcasecmp(hostname + hostname_length - suffix_length, suffix) == 0) {
        return g_multiplayer_server;
    }
    return hostname;
}

static int config_find(const char *section, const char *key) {
    for (size_t i = 0; i < g_config_entry_count; ++i) {
        if (strcasecmp(g_config_entries[i].section, section) == 0 &&
            strcasecmp(g_config_entries[i].key, key) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static const char *config_get(const char *section, const char *key) {
    if (!section || !key) {
        return NULL;
    }
    int index = config_find(section, key);
    return index >= 0 ? g_config_entries[index].value : NULL;
}

static void config_set(const char *section, const char *key, const char *value) {
    if (!section || !key || !section[0] || !key[0]) {
        return;
    }
    int index = config_find(section, key);
    if (index < 0) {
        if (g_config_entry_count >= S3E_CONFIG_MAX_ENTRIES) {
            return;
        }
        index = (int)g_config_entry_count++;
    }
    snprintf(g_config_entries[index].section, sizeof(g_config_entries[index].section), "%s",
             section);
    snprintf(g_config_entries[index].key, sizeof(g_config_entries[index].key), "%s", key);
    snprintf(g_config_entries[index].value, sizeof(g_config_entries[index].value), "%s",
             value ? value : "");
}

static int contains_case_insensitive(const char *text, const char *needle) {
    size_t needle_len = strlen(needle);
    if (needle_len == 0) {
        return 1;
    }
    for (const char *p = text; *p; ++p) {
        if (strncasecmp(p, needle, needle_len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int config_platform_matches(const char *value) {
    return contains_case_insensitive(value, "ANY") || contains_case_insensitive(value, "ANDROID");
}

static int config_id_matches(const char *value) {
    if (contains_case_insensitive(value, "ANY")) {
        return 1;
    }
    if (!config_platform_matches(value)) {
        return 0;
    }
    if (!strchr(value, '"')) {
        return 1;
    }
    return contains_case_insensitive(value, "R800i") ||
           contains_case_insensitive(value, "Sony Ericsson Xperia Play");
}

static int config_condition_matches(const char *condition) {
    char copy[256];
    snprintf(copy, sizeof(copy), "%s", condition ? condition : "");
    char *expr = trim(copy);
    if (!expr[0]) {
        return 1;
    }
    if (strncasecmp(expr, "OS=", 3) == 0) {
        return config_platform_matches(expr + 3);
    }
    if (strncasecmp(expr, "ID=", 3) == 0 || strncasecmp(expr, "CLASS=", 6) == 0) {
        char *equals = strchr(expr, '=');
        return equals && config_id_matches(trim(equals + 1));
    }
    if (expr[0] == '[') {
        char *section_end = strchr(expr, ']');
        char *equals = strstr(expr, "==");
        if (!section_end || !equals || equals <= section_end) {
            return 0;
        }
        *section_end = 0;
        *equals = 0;
        char *section = trim(expr + 1);
        char *key = trim(section_end + 1);
        char *value = trim(equals + 2);
        strip_quotes(value);
        const char *current = config_get(section, key);
        return current && strcasecmp(current, value) == 0;
    }
    return 0;
}

static int parse_config_int_value(const char *value, int depth, int32_t *out) {
    if (!value || !out || depth > 4) {
        return 0;
    }
    char copy[256];
    snprintf(copy, sizeof(copy), "%s", value);
    char *text = trim(copy);
    if (!text[0]) {
        return 0;
    }
    if (text[0] == '[') {
        char *section_end = strchr(text, ']');
        if (!section_end) {
            return 0;
        }
        *section_end = 0;
        char *section = trim(text + 1);
        char *key = trim(section_end + 1);
        char *op = strpbrk(key, "+-");
        char operator= 0;
        int32_t adjustment = 0;
        if (op) {
            operator= * op;
            *op = 0;
            char *adj = trim(op + 1);
            char *end = NULL;
            long parsed = strtol(adj, &end, 0);
            if (end == adj) {
                return 0;
            }
            adjustment = (int32_t)parsed;
        }
        int32_t base = 0;
        if (!parse_config_int_value(config_get(section, trim(key)), depth + 1, &base)) {
            return 0;
        }
        if (operator== '+') {
            base += adjustment;
        } else if (operator== '-') {
            base -= adjustment;
        }
        *out = base;
        return 1;
    }
    char *end = NULL;
    long parsed = strtol(text, &end, 0);
    if (end == text) {
        return 0;
    }
    *out = (int32_t)parsed;
    return 1;
}

static int root_asset_exists(const char *name) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/assets/%s", g_root, name);
    return access(path, F_OK) == 0;
}

void s3e_host_set_config(const uint8_t *data, uint32_t size) {
    g_config_entry_count = 0;
    if (data && size) {
        char *text = malloc((size_t)size + 1);
        if (text) {
            memcpy(text, data, size);
            text[size] = 0;
            char section[64] = "";
            int active = 1;
            char *save = NULL;
            for (char *line = strtok_r(text, "\n", &save); line;
                 line = strtok_r(NULL, "\n", &save)) {
                line[strcspn(line, "\r")] = 0;
                strip_comment(line);
                char *clean = trim(line);
                if (!clean[0]) {
                    continue;
                }
                size_t len = strlen(clean);
                if (clean[0] == '{' && clean[len - 1] == '}') {
                    clean[len - 1] = 0;
                    active = config_condition_matches(clean + 1);
                    continue;
                }
                if (clean[0] == '[' && clean[len - 1] == ']') {
                    clean[len - 1] = 0;
                    snprintf(section, sizeof(section), "%s", trim(clean + 1));
                    continue;
                }
                if (!active) {
                    continue;
                }
                char *equals = strchr(clean, '=');
                if (!equals) {
                    continue;
                }
                *equals = 0;
                char *key = trim(clean);
                char *value = trim(equals + 1);
                strip_quotes(value);
                config_set(section, key, value);
            }
            free(text);
        }
    }

    load_control_config();

    config_set("GX", "NumTPages", "512");
    config_set("GX", "NumTPagesNoMipMap", "256");
    config_set("GX", "NumTPageFreeRects", "8192");
    config_set("GX", "MaxTexturesPerTPage", "512");
    config_set("GAME", "ResourceDownloader", "0");
    config_set("GAME", "EnableGC", "0");
    config_set("GAME", "EnableAndroidMarketBilling", "0");
    config_set("GAME", "VoiceChatEnabled", g_voice_chat ? "1" : "0");
    config_set("GAME", "LowEndDevice", "0");
    config_set("GAME", "LowMemoryDevice", "0");
    config_set("GAME", "GuiBucketSize", "2500000");
    config_set("GAME", "FrontendMemoryWarningLevel", "0");
    if (multiplayer_server_enabled()) {
        config_set("GAME", "OnlineAccount", "GENERIC");
        config_set("GAME", "GameVersion", "1.0.11");
    } else {
        config_set("GAME", "OnlineAccount", "NONE");
        config_set("GAME", "OnlineUseGameCenterMM", "0");
        config_set("GAME", "MatchmakingSearchAndPublishMode", "0");
        config_set("Demonware", "OnlineAccount", "NONE");
        config_set("Demonware", "LSGServer", "");
        config_set("Demonware", "AuthServer", "");
        config_set("Demonware", "STUNServer", "");
        config_set("ONLINE", "dispatcher", "");
    }
    config_set("FLASH", "FlashBucketSize", "32000000");
    config_set("FLASH", "FlashBucketHeapAnalyse", "0");
    if (root_asset_exists("blackops_etc.dz")) {
        config_set("RESMANAGER", "ResBuildStyle", "etc");
    } else if (root_asset_exists("blackops_atitc.dz")) {
        config_set("RESMANAGER", "ResBuildStyle", "atitc");
    } else if (root_asset_exists("blackops_dxt.dz")) {
        config_set("RESMANAGER", "ResBuildStyle", "dxt");
    } else {
        config_set("RESMANAGER", "ResBuildStyle", "gles1");
    }
}

int32_t s3eConfigGetInt(const char *section, const char *key, int32_t *out) {
    const char *value = config_get(section, key);
    int32_t parsed = 0;
    int found = value && parse_config_int_value(value, 0, &parsed);
    if (found && multiplayer_server_enabled() && strcasecmp(section, "GAME") == 0 &&
        strcasecmp(key, "MatchmakingSearchAndPublishMode") == 0) {
        int32_t server_mode = s3e_multiplayer_take_matchmaking_mode();
        if (server_mode >= 0) {
            parsed = server_mode;
        }
    }
    if (found && out) {
        *out = parsed;
    }
    return found ? 0 : 1;
}

int32_t s3eConfigGetString(const char *section, const char *key, char *out) {
    const char *value = config_get(section, key);
    if (!value || !out) {
        return 1;
    }
    strcpy(out, value);
    return 0;
}
