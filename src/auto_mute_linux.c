/*
 * Auto Mute - Native Linux TeamSpeak 3 Client Plugin (API v26)
 * Original Windows Plugin Concept: CubE (CubE135)
 * Linux Port & Debian Packaging: Emiel (pdtweepd@gmail.com)
 *
 * Automatically mutes microphone and/or speakers when joining a TeamSpeak 3 server.
 */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PLUGIN_API_VERSION 26
#define PATH_BUFSIZE 512
#define PLUGIN_MENU_BUFSZ 128

enum PluginMenuType {
    PLUGIN_MENU_TYPE_GLOBAL = 0,
    PLUGIN_MENU_TYPE_CHANNEL = 1,
    PLUGIN_MENU_TYPE_CLIENT = 2
};

enum ConnectStatus {
    STATUS_DISCONNECTED = 0,
    STATUS_CONNECTING = 1,
    STATUS_CONNECTED = 2,
    STATUS_CONNECTION_ESTABLISHING = 3,
    STATUS_CONNECTION_ESTABLISHED = 4
};

enum ClientProperties {
    CLIENT_UNIQUE_IDENTIFIER = 0,
    CLIENT_NICKNAME,
    CLIENT_VERSION,
    CLIENT_PLATFORM,
    CLIENT_FLAG_TALKING,
    CLIENT_INPUT_MUTED,         /* 5: Microphone mute status (MuteInputStatus) */
    CLIENT_OUTPUT_MUTED,        /* 6: Output / speaker mute status (MuteOutputStatus) */
    CLIENT_OUTPUTONLY_MUTED,    /* 7: Output-only mute status */
    CLIENT_INPUT_HARDWARE,
    CLIENT_OUTPUT_HARDWARE,
    CLIENT_INPUT_DEACTIVATED    /* 10: Local mic stream deactivation */
};

enum MuteInputStatus {
    MUTEINPUT_NONE = 0,
    MUTEINPUT_MUTED = 1
};

enum MuteOutputStatus {
    MUTEOUTPUT_NONE = 0,
    MUTEOUTPUT_MUTED = 1
};

enum VirtualServerProperties {
    VIRTUALSERVER_NAME = 0
};

enum LogLevel {
    LogLevel_CRITICAL = 0,
    LogLevel_ERROR = 1,
    LogLevel_WARNING = 2,
    LogLevel_DEBUG = 3,
    LogLevel_INFO = 4,
    LogLevel_DEVEL = 5
};

struct PluginMenuItem {
    enum PluginMenuType type;
    int id;
    char text[PLUGIN_MENU_BUFSZ];
    char icon[PLUGIN_MENU_BUFSZ];
};

struct TS3Functions {
    void* fn[230];
};

typedef unsigned int (*fn_freeMemory)(void* pointer);
typedef unsigned int (*fn_logMessage)(const char* logMessage, int severity, const char* channel, uint64_t logID);
typedef unsigned int (*fn_setClientSelfVariableAsInt)(uint64_t scHandlerID, size_t flag, int value);
typedef unsigned int (*fn_setClientSelfVariableAsString)(uint64_t scHandlerID, size_t flag, const char* value);
typedef unsigned int (*fn_flushClientSelfUpdates)(uint64_t scHandlerID, const char* returnCode);
typedef unsigned int (*fn_getServerVariableAsString)(uint64_t scHandlerID, size_t flag, char** result);
typedef uint64_t (*fn_getCurrentServerConnectionHandlerID)(void);
typedef void (*fn_getConfigPath)(char* path, size_t maxLen);

static struct TS3Functions ts3Functions;
static char* pluginID = NULL;
static char configFilePath[PATH_BUFSIZE + 32] = {0};

static int cfg_always_mute = 1;
static int cfg_mute_mic = 1;
static int cfg_mute_speaker = 1;
static char cfg_server_list[2048] = {0};

static void log_info(uint64_t scHandlerID, const char* msg) {
    fn_logMessage logMsg = (fn_logMessage)ts3Functions.fn[6];
    if (logMsg) {
        logMsg(msg, LogLevel_INFO, "AutoMute", scHandlerID);
    }
}

static void save_config(void) {
    if (!configFilePath[0]) return;
    FILE* f = fopen(configFilePath, "w");
    if (!f) return;
    fprintf(f, "[General]\n");
    fprintf(f, "always_mute=%s\n", cfg_always_mute ? "true" : "false");
    fprintf(f, "mute_mic=%s\n", cfg_mute_mic ? "true" : "false");
    fprintf(f, "mute_speaker=%s\n", cfg_mute_speaker ? "true" : "false");
    fprintf(f, "server_list=%s\n", cfg_server_list);
    fclose(f);
}

static void load_config(void) {
    if (!configFilePath[0]) return;
    FILE* f = fopen(configFilePath, "r");
    if (!f) {
        cfg_always_mute = 1;
        cfg_mute_mic = 1;
        cfg_mute_speaker = 1;
        cfg_server_list[0] = '\0';
        save_config();
        return;
    }
    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        char* nl = strpbrk(line, "\r\n");
        if (nl) *nl = '\0';
        if (strncmp(line, "always_mute=", 12) == 0) {
            cfg_always_mute = (strstr(line + 12, "true") != NULL || strcmp(line + 12, "1") == 0);
        } else if (strncmp(line, "mute_mic=", 9) == 0) {
            cfg_mute_mic = (strstr(line + 9, "true") != NULL || strcmp(line + 9, "1") == 0);
        } else if (strncmp(line, "mute_speaker=", 13) == 0) {
            cfg_mute_speaker = (strstr(line + 13, "true") != NULL || strcmp(line + 13, "1") == 0);
        } else if (strncmp(line, "server_list=", 12) == 0) {
            snprintf(cfg_server_list, sizeof(cfg_server_list), "%s", line + 12);
        }
    }
    fclose(f);
}

static int server_in_list(const char* list, const char* serverName) {
    if (!list || !list[0] || !serverName || !serverName[0]) return 0;
    size_t nameLen = strlen(serverName);
    const char* p = list;
    while (*p) {
        while (*p == ';' || *p == ',' || *p == ' ') p++;
        if (!*p) break;
        const char* end = p;
        while (*end && *end != ';' && *end != ',') end++;
        size_t tokenLen = (size_t)(end - p);
        while (tokenLen > 0 && p[tokenLen - 1] == ' ') tokenLen--;
        if (tokenLen == nameLen && strncmp(p, serverName, nameLen) == 0) {
            return 1;
        }
        p = end;
    }
    return 0;
}

static void toggle_server_in_list(const char* serverName) {
    if (!serverName || !serverName[0]) return;
    if (!server_in_list(cfg_server_list, serverName)) {
        size_t curLen = strlen(cfg_server_list);
        size_t addLen = strlen(serverName);
        if (curLen + addLen + 2 < sizeof(cfg_server_list)) {
            if (curLen > 0) {
                strcat(cfg_server_list, ";");
            }
            strcat(cfg_server_list, serverName);
        }
        return;
    }

    char updated[2048] = {0};
    size_t nameLen = strlen(serverName);
    const char* p = cfg_server_list;
    while (*p) {
        while (*p == ';' || *p == ',' || *p == ' ') p++;
        if (!*p) break;
        const char* end = p;
        while (*end && *end != ';' && *end != ',') end++;
        size_t tokenLen = (size_t)(end - p);
        while (tokenLen > 0 && p[tokenLen - 1] == ' ') tokenLen--;
        if (!(tokenLen == nameLen && strncmp(p, serverName, nameLen) == 0)) {
            if (updated[0]) strcat(updated, ";");
            strncat(updated, p, tokenLen);
        }
        p = end;
    }
    snprintf(cfg_server_list, sizeof(cfg_server_list), "%s", updated);
}

static void apply_mute(uint64_t scHandlerID) {
    fn_setClientSelfVariableAsInt setVarInt = (fn_setClientSelfVariableAsInt)ts3Functions.fn[76];
    fn_setClientSelfVariableAsString setVarStr = (fn_setClientSelfVariableAsString)ts3Functions.fn[77];
    fn_flushClientSelfUpdates flushUpdates = (fn_flushClientSelfUpdates)ts3Functions.fn[78];
    if (!flushUpdates) return;

    if (cfg_mute_mic) {
        if (setVarInt) setVarInt(scHandlerID, CLIENT_INPUT_MUTED, MUTEINPUT_MUTED);
        if (setVarStr) setVarStr(scHandlerID, CLIENT_INPUT_MUTED, "1");
    }
    if (cfg_mute_speaker) {
        if (setVarInt) setVarInt(scHandlerID, CLIENT_OUTPUT_MUTED, MUTEOUTPUT_MUTED);
        if (setVarStr) setVarStr(scHandlerID, CLIENT_OUTPUT_MUTED, "1");
    }
    if (cfg_mute_mic || cfg_mute_speaker) {
        flushUpdates(scHandlerID, NULL);
    }
}

struct MuteThreadArg {
    uint64_t scHandlerID;
};

static void* delayed_mute_thread(void* param) {
    struct MuteThreadArg* arg = (struct MuteThreadArg*)param;
    uint64_t scHandlerID = arg->scHandlerID;
    free(arg);

    /*
     * When STATUS_CONNECTION_ESTABLISHED fires, the TeamSpeak client UI
     * is still finalizing internal channel synchronization.
     * We apply immediately, and then again after 150ms and 500ms to guarantee
     * that the TeamSpeak UI toolbar and tree view icon state changes to muted.
     */
    apply_mute(scHandlerID);
    usleep(150000);
    apply_mute(scHandlerID);
    usleep(350000);
    apply_mute(scHandlerID);
    return NULL;
}

const char* ts3plugin_name(void) {
    return "Auto Mute";
}

const char* ts3plugin_version(void) {
    return "1.1.9";
}

int ts3plugin_apiVersion(void) {
    return PLUGIN_API_VERSION;
}

const char* ts3plugin_author(void) {
    return "CubE / Linux Port";
}

const char* ts3plugin_description(void) {
    return "AutoMute lets you choose if you want to join servers with your microphone and/or speakers muted.";
}

void ts3plugin_setFunctionPointers(const struct TS3Functions funcs) {
    ts3Functions = funcs;
}

int ts3plugin_init(void) {
    char cfgDir[PATH_BUFSIZE] = {0};
    fn_getConfigPath getConfigPath = (fn_getConfigPath)ts3Functions.fn[180];
    if (!getConfigPath) {
        getConfigPath = (fn_getConfigPath)ts3Functions.fn[181];
    }
    if (getConfigPath) {
        getConfigPath(cfgDir, sizeof(cfgDir));
        size_t len = strlen(cfgDir);
        if (len > 0 && cfgDir[len - 1] != '/') {
            snprintf(configFilePath, sizeof(configFilePath), "%s/AutoMute.ini", cfgDir);
        } else {
            snprintf(configFilePath, sizeof(configFilePath), "%sAutoMute.ini", cfgDir);
        }
        load_config();
    }
    return 0;
}

void ts3plugin_shutdown(void) {
    if (pluginID) {
        free(pluginID);
        pluginID = NULL;
    }
}

void ts3plugin_registerPluginID(const char* id) {
    if (!id) return;
    if (pluginID) free(pluginID);
    size_t len = strlen(id) + 1;
    pluginID = (char*)malloc(len);
    if (pluginID) {
        memcpy(pluginID, id, len);
    }
}

void ts3plugin_freeMemory(void* data) {
    free(data);
}

int ts3plugin_requestAutoload(void) {
    return 1;
}

static struct PluginMenuItem* createMenuItem(enum PluginMenuType type, int id, const char* text, const char* icon) {
    struct PluginMenuItem* item = (struct PluginMenuItem*)malloc(sizeof(struct PluginMenuItem));
    if (!item) return NULL;
    item->type = type;
    item->id = id;
    snprintf(item->text, PLUGIN_MENU_BUFSZ, "%s", text);
    snprintf(item->icon, PLUGIN_MENU_BUFSZ, "%s", icon);
    return item;
}

void ts3plugin_initMenus(struct PluginMenuItem*** menuItems, char** menuIcon) {
    const size_t count = 6;
    *menuItems = (struct PluginMenuItem**)malloc(sizeof(struct PluginMenuItem*) * count);
    if (!*menuItems) return;

    (*menuItems)[0] = createMenuItem(PLUGIN_MENU_TYPE_GLOBAL, 1, "Toggle: Mute on All Servers", "settings.png");
    (*menuItems)[1] = createMenuItem(PLUGIN_MENU_TYPE_GLOBAL, 2, "Toggle: Mute Microphone on Join", "settings.png");
    (*menuItems)[2] = createMenuItem(PLUGIN_MENU_TYPE_GLOBAL, 3, "Toggle: Mute Speakers on Join", "settings.png");
    (*menuItems)[3] = createMenuItem(PLUGIN_MENU_TYPE_GLOBAL, 4, "Toggle: Current Server in AutoMute List", "settings.png");
    (*menuItems)[4] = createMenuItem(PLUGIN_MENU_TYPE_GLOBAL, 5, "Mute Microphone & Speakers Now", "icon.png");
    (*menuItems)[5] = NULL;

    *menuIcon = (char*)malloc(PLUGIN_MENU_BUFSZ);
    if (*menuIcon) {
        snprintf(*menuIcon, PLUGIN_MENU_BUFSZ, "icon.png");
    }
}

void ts3plugin_onMenuItemEvent(uint64_t scHandlerID, enum PluginMenuType type, int menuItemID, uint64_t selectedItemID) {
    (void)selectedItemID;
    if (type != PLUGIN_MENU_TYPE_GLOBAL) return;

    load_config();
    if (menuItemID == 1) {
        cfg_always_mute = !cfg_always_mute;
        save_config();
    } else if (menuItemID == 2) {
        cfg_mute_mic = !cfg_mute_mic;
        save_config();
    } else if (menuItemID == 3) {
        cfg_mute_speaker = !cfg_mute_speaker;
        save_config();
    } else if (menuItemID == 4) {
        fn_getServerVariableAsString getServerVar = (fn_getServerVariableAsString)ts3Functions.fn[99];
        fn_freeMemory ts3Free = (fn_freeMemory)ts3Functions.fn[5];
        char* serverName = NULL;
        if (getServerVar && getServerVar(scHandlerID, VIRTUALSERVER_NAME, &serverName) == 0 && serverName) {
            toggle_server_in_list(serverName);
            save_config();
            if (ts3Free) ts3Free(serverName);
        }
    } else if (menuItemID == 5) {
        apply_mute(scHandlerID);
    }

    char msg[512];
    snprintf(msg, sizeof(msg), "AutoMute: always_mute=%s, mute_mic=%s, mute_speaker=%s",
             cfg_always_mute ? "true" : "false",
             cfg_mute_mic ? "true" : "false",
             cfg_mute_speaker ? "true" : "false");
    log_info(scHandlerID, msg);
}

void ts3plugin_onConnectStatusChangeEvent(uint64_t scHandlerID, int newStatus, unsigned int errorNumber) {
    (void)errorNumber;
    if (newStatus != STATUS_CONNECTION_ESTABLISHED) return;

    load_config();
    int should_mute = cfg_always_mute;

    if (!should_mute && cfg_server_list[0]) {
        fn_getServerVariableAsString getServerVar = (fn_getServerVariableAsString)ts3Functions.fn[99];
        fn_freeMemory ts3Free = (fn_freeMemory)ts3Functions.fn[5];
        char* serverName = NULL;
        if (getServerVar && getServerVar(scHandlerID, VIRTUALSERVER_NAME, &serverName) == 0 && serverName) {
            if (server_in_list(cfg_server_list, serverName)) {
                should_mute = 1;
            }
            if (ts3Free) ts3Free(serverName);
        }
    }

    if (should_mute) {
        struct MuteThreadArg* arg = (struct MuteThreadArg*)malloc(sizeof(struct MuteThreadArg));
        if (arg) {
            arg->scHandlerID = scHandlerID;
            pthread_t tid;
            if (pthread_create(&tid, NULL, delayed_mute_thread, arg) == 0) {
                pthread_detach(tid);
            } else {
                free(arg);
                apply_mute(scHandlerID);
            }
        } else {
            apply_mute(scHandlerID);
        }
        log_info(scHandlerID, "AutoMute applied on server connection.");
    }
}
