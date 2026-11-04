/*
 * Auto Mute - Native Linux TeamSpeak 3 Client Plugin (API v26)
 * Original Windows Plugin Concept: CubE (CubE135)
 * Linux Port & Debian Packaging: Emiel (pdtweepd@gmail.com)
 *
 * Automatically mutes microphone and/or speakers when joining a TeamSpeak 3 server.
 */

#define _GNU_SOURCE
#include "ts3_plugin_sdk.h"
#include <dlfcn.h>
#include <link.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static struct TS3Functions ts3Functions;
static char* pluginID = NULL;
static char configFilePath[PATH_BUFSIZE + 32] = {0};

static int cfg_always_mute = 1;
static int cfg_mute_mic = 1;
static int cfg_mute_speaker = 1;
static char cfg_server_list[2048] = {0};
static int g_forced_client_muted = -1;

typedef struct {
    int ref;
    int alloc;
    int begin;
    int end;
} QListDataHeader;

typedef struct {
    int ref;
    int size;
    uint32_t alloc;
    int64_t offset;
} QArrayDataHeader;

static void log_info(uint64_t scHandlerID, const char* msg) {
    if (ts3Functions.logMessage) {
        ts3Functions.logMessage(msg, LogLevel_INFO, "AutoMute", scHandlerID);
    }
}

static void init_config_path(void) {
    char cfgDir[PATH_BUFSIZE] = {0};
    if (ts3Functions.getConfigPath) {
        ts3Functions.getConfigPath(cfgDir, sizeof(cfgDir));
    }

    if (!cfgDir[0]) {
        const char* home = getenv("HOME");
        if (home) {
            struct stat st;
            char flatpakDir[PATH_BUFSIZE];
            snprintf(flatpakDir, sizeof(flatpakDir), "%s/.var/app/com.teamspeak.TeamSpeak3/.ts3client", home);
            if (stat(flatpakDir, &st) == 0 && S_ISDIR(st.st_mode)) {
                snprintf(cfgDir, sizeof(cfgDir), "%s", flatpakDir);
            } else {
                snprintf(cfgDir, sizeof(cfgDir), "%s/.ts3client", home);
            }
        }
    }

    if (cfgDir[0]) {
        size_t len = strlen(cfgDir);
        if (cfgDir[len - 1] == '/') {
            snprintf(configFilePath, sizeof(configFilePath), "%sAutoMute.ini", cfgDir);
        } else {
            snprintf(configFilePath, sizeof(configFilePath), "%s/AutoMute.ini", cfgDir);
        }
    } else {
        snprintf(configFilePath, sizeof(configFilePath), "AutoMute.ini");
    }
}

static void save_config(void) {
    if (!configFilePath[0]) init_config_path();
    if (!configFilePath[0]) return;

    FILE* f = fopen(configFilePath, "w");
    if (!f) {
        const char* home = getenv("HOME");
        if (home) {
            char alt[PATH_BUFSIZE];
            snprintf(alt, sizeof(alt), "%s/.ts3client/AutoMute.ini", home);
            f = fopen(alt, "w");
            if (f) {
                snprintf(configFilePath, sizeof(configFilePath), "%s", alt);
            }
        }
    }
    if (!f) return;

    fprintf(f, "[General]\n");
    fprintf(f, "always_mute=%s\n", cfg_always_mute ? "true" : "false");
    fprintf(f, "mute_mic=%s\n", cfg_mute_mic ? "true" : "false");
    fprintf(f, "mute_speaker=%s\n", cfg_mute_speaker ? "true" : "false");
    fprintf(f, "server_list=%s\n", cfg_server_list);
    fclose(f);
}

static void load_config(void) {
    if (!configFilePath[0]) init_config_path();
    if (!configFilePath[0]) return;

    FILE* f = fopen(configFilePath, "r");
    if (!f) {
        const char* home = getenv("HOME");
        if (home) {
            char alt[PATH_BUFSIZE];
            snprintf(alt, sizeof(alt), "%s/.ts3client/AutoMute.ini", home);
            f = fopen(alt, "r");
            if (f) {
                snprintf(configFilePath, sizeof(configFilePath), "%s", alt);
            }
        }
    }

    if (!f) {
        /* Preserve current in-memory settings and create file */
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

struct FindContext {
    const char* sym_name;
    void* result;
};

static int phdr_callback(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size;
    struct FindContext* ctx = (struct FindContext*)data;
    if (ctx->result) return 1;
    if (info->dlpi_name && info->dlpi_name[0]) {
        void* handle = dlopen(info->dlpi_name, RTLD_NOLOAD | RTLD_LAZY);
        if (handle) {
            void* s = dlsym(handle, ctx->sym_name);
            dlclose(handle);
            if (s) {
                ctx->result = s;
                return 1;
            }
        }
    }
    return 0;
}

static void* resolve_qt_sym(const char* name) {
    void* s = dlsym(RTLD_DEFAULT, name);
    if (s) return s;
    struct FindContext ctx = { name, NULL };
    dl_iterate_phdr(phdr_callback, &ctx);
    return ctx.result;
}

static void (*fn_allWidgets)(void*) = NULL;
static void (*fn_actions)(void*, void*) = NULL;
static void (*fn_text)(void*, void*) = NULL;
static void (*fn_setIcon)(void*, const void*) = NULL;
static void (*fn_fromUtf8)(void*, const char*, int) = NULL;
static void (*fn_iconCtor)(void*, const void*) = NULL;
static void (*fn_iconDtor)(void*) = NULL;
static void (*fn_qlistDispose)(void*) = NULL;
static void (*fn_qarrayDealloc)(void*, size_t, size_t) = NULL;
static int qt_syms_loaded = 0;

static int init_qt_symbols(void) {
    if (qt_syms_loaded == 1) return 1;
    if (qt_syms_loaded == -1) return 0;

    fn_allWidgets = (void (*)(void*))resolve_qt_sym("_ZN12QApplication10allWidgetsEv");
    fn_actions = (void (*)(void*, void*))resolve_qt_sym("_ZNK7QWidget7actionsEv");
    fn_text = (void (*)(void*, void*))resolve_qt_sym("_ZNK7QAction4textEv");
    fn_setIcon = (void (*)(void*, const void*))resolve_qt_sym("_ZN7QAction7setIconERK5QIcon");
    fn_fromUtf8 = (void (*)(void*, const char*, int))resolve_qt_sym("_ZN7QString15fromUtf8_helperEPKci");
    fn_iconCtor = (void (*)(void*, const void*))resolve_qt_sym("_ZN5QIconC1ERK7QString");
    fn_iconDtor = (void (*)(void*))resolve_qt_sym("_ZN5QIconD1Ev");
    fn_qlistDispose = (void (*)(void*))resolve_qt_sym("_ZN9QListData7disposeEPNS_4DataE");
    fn_qarrayDealloc = (void (*)(void*, size_t, size_t))resolve_qt_sym("_ZN10QArrayData10deallocateEPS_mm");

    if (fn_allWidgets && fn_actions && fn_text && fn_setIcon && fn_fromUtf8 && fn_iconCtor && fn_iconDtor) {
        qt_syms_loaded = 1;
        return 1;
    }
    qt_syms_loaded = -1;
    return 0;
}

static void get_icon_paths(char* red_path, char* green_path, size_t maxLen) {
    const char* home = getenv("HOME");
    red_path[0] = '\0';
    green_path[0] = '\0';

    if (home) {
        snprintf(red_path, maxLen, "%s/.var/app/com.teamspeak.TeamSpeak3/.ts3client/plugins/auto_mute/mute_red.png", home);
        snprintf(green_path, maxLen, "%s/.var/app/com.teamspeak.TeamSpeak3/.ts3client/plugins/auto_mute/mute_green.png", home);
        if (access(red_path, R_OK) != 0) {
            snprintf(red_path, maxLen, "%s/.ts3client/plugins/auto_mute/mute_red.png", home);
            snprintf(green_path, maxLen, "%s/.ts3client/plugins/auto_mute/mute_green.png", home);
        }
    }
    if (access(red_path, R_OK) != 0 && ts3Functions.getResourcesPath) {
        char resPath[PATH_BUFSIZE] = {0};
        ts3Functions.getResourcesPath(resPath, sizeof(resPath));
        if (resPath[0]) {
            snprintf(red_path, maxLen, "%splugins/auto_mute/mute_red.png", resPath);
            snprintf(green_path, maxLen, "%splugins/auto_mute/mute_green.png", resPath);
        }
    }
}

static void update_live_menu_icons(void) {
    if (!init_qt_symbols()) {
        log_info(0, "AutoMute: Qt symbols not found for live menu update");
        return;
    }

    char red_icon_path[PATH_BUFSIZE] = {0};
    char green_icon_path[PATH_BUFSIZE] = {0};
    get_icon_paths(red_icon_path, green_icon_path, sizeof(red_icon_path));

    if (access(red_icon_path, R_OK) != 0 || access(green_icon_path, R_OK) != 0) {
        log_info(0, "AutoMute: Icon files not accessible on disk");
        return;
    }

    void* qstr_red = NULL;
    void* qstr_green = NULL;
    fn_fromUtf8(&qstr_red, red_icon_path, (int)strlen(red_icon_path));
    fn_fromUtf8(&qstr_green, green_icon_path, (int)strlen(green_icon_path));
    if (!qstr_red || !qstr_green) return;

    void* icon_red[2] = {0, 0};
    void* icon_green[2] = {0, 0};
    fn_iconCtor(icon_red, &qstr_red);
    fn_iconCtor(icon_green, &qstr_green);

    /* Determine live state for mute function */
    int client_is_muted = 0;
    if (g_forced_client_muted >= 0) {
        client_is_muted = g_forced_client_muted;
    } else if (ts3Functions.getCurrentServerConnectionHandlerID && ts3Functions.getClientSelfVariableAsInt) {
        uint64 scHandlerID = ts3Functions.getCurrentServerConnectionHandlerID();
        if (scHandlerID) {
            int mic_m = 0, spk_m = 0;
            ts3Functions.getClientSelfVariableAsInt(scHandlerID, CLIENT_INPUT_MUTED, &mic_m);
            ts3Functions.getClientSelfVariableAsInt(scHandlerID, CLIENT_OUTPUT_MUTED, &spk_m);
            if (mic_m || spk_m) {
                client_is_muted = 1;
            }
        }
    }

    /* Iterate all widgets and update matching action icons */
    void* wListD = NULL;
    fn_allWidgets(&wListD);
    int updated_count = 0;

    if (wListD) {
        QListDataHeader* wHdr = (QListDataHeader*)wListD;
        void** wArray = (void**)(((char*)wListD) + 16);
        for (int i = wHdr->begin; i < wHdr->end; i++) {
            void* widget = wArray[i];
            if (!widget) continue;
            void* aListD = NULL;
            fn_actions(&aListD, widget);
            if (!aListD) continue;
            QListDataHeader* aHdr = (QListDataHeader*)aListD;
            void** aArray = (void**)(((char*)aListD) + 16);
            for (int j = aHdr->begin; j < aHdr->end; j++) {
                void* action = aArray[j];
                if (!action) continue;
                void* qstrText = NULL;
                fn_text(&qstrText, action);
                if (!qstrText) continue;
                QArrayDataHeader* tHdr = (QArrayDataHeader*)qstrText;
                if (tHdr->size > 0 && tHdr->offset > 0) {
                    uint16_t* utf16 = (uint16_t*)(((char*)qstrText) + tHdr->offset);
                    char ascii[128] = {0};
                    int maxChars = tHdr->size < 127 ? tHdr->size : 127;
                    for (int k = 0; k < maxChars; k++) {
                        ascii[k] = (char)(utf16[k] & 0x7F);
                    }
                    ascii[maxChars] = '\0';

                    if (strstr(ascii, "Mute Microphone on Join")) {
                        fn_setIcon(action, cfg_mute_mic ? icon_red : icon_green);
                        updated_count++;
                    } else if (strstr(ascii, "Mute Speakers on Join")) {
                        fn_setIcon(action, cfg_mute_speaker ? icon_red : icon_green);
                        updated_count++;
                    } else if (strstr(ascii, "Mute on All Servers")) {
                        fn_setIcon(action, cfg_always_mute ? icon_red : icon_green);
                        updated_count++;
                    } else if (strstr(ascii, "Mute Microphone & Speakers Now")) {
                        fn_setIcon(action, client_is_muted ? icon_red : icon_green);
                        updated_count++;
                    }
                }
                if (qstrText) {
                    int* ref = (int*)qstrText;
                    if (__sync_sub_and_fetch(ref, 1) == 0 && fn_qarrayDealloc) {
                        fn_qarrayDealloc(qstrText, 2, 2);
                    }
                }
            }
            if (aListD) {
                int* ref = (int*)aListD;
                if (__sync_sub_and_fetch(ref, 1) == 0 && fn_qlistDispose) {
                    fn_qlistDispose(aListD);
                }
            }
        }
        if (wListD) {
            int* ref = (int*)wListD;
            if (__sync_sub_and_fetch(ref, 1) == 0 && fn_qlistDispose) {
                fn_qlistDispose(wListD);
            }
        }
    }

    fn_iconDtor(icon_red);
    fn_iconDtor(icon_green);

    if (qstr_red) {
        int* ref = (int*)qstr_red;
        if (__sync_sub_and_fetch(ref, 1) == 0 && fn_qarrayDealloc) {
            fn_qarrayDealloc(qstr_red, 2, 2);
        }
    }
    if (qstr_green) {
        int* ref = (int*)qstr_green;
        if (__sync_sub_and_fetch(ref, 1) == 0 && fn_qarrayDealloc) {
            fn_qarrayDealloc(qstr_green, 2, 2);
        }
    }

    char logMsg[128];
    snprintf(logMsg, sizeof(logMsg), "AutoMute: update_live_menu_icons updated %d actions", updated_count);
    log_info(0, logMsg);
}

static void apply_mute(uint64_t scHandlerID) {
    if (!ts3Functions.flushClientSelfUpdates) return;

    /* Handle Microphone mute/unmute independently */
    if (cfg_mute_mic) {
        if (ts3Functions.setClientSelfVariableAsInt) {
            ts3Functions.setClientSelfVariableAsInt(scHandlerID, CLIENT_INPUT_MUTED, MUTEINPUT_MUTED);
        }
        if (ts3Functions.setClientSelfVariableAsString) {
            ts3Functions.setClientSelfVariableAsString(scHandlerID, CLIENT_INPUT_MUTED, "1");
        }
    } else {
        if (ts3Functions.setClientSelfVariableAsInt) {
            ts3Functions.setClientSelfVariableAsInt(scHandlerID, CLIENT_INPUT_MUTED, MUTEINPUT_NONE);
        }
        if (ts3Functions.setClientSelfVariableAsString) {
            ts3Functions.setClientSelfVariableAsString(scHandlerID, CLIENT_INPUT_MUTED, "0");
        }
    }

    /* Handle Speaker mute/unmute independently */
    if (cfg_mute_speaker) {
        if (ts3Functions.setClientSelfVariableAsInt) {
            ts3Functions.setClientSelfVariableAsInt(scHandlerID, CLIENT_OUTPUT_MUTED, MUTEOUTPUT_MUTED);
        }
        if (ts3Functions.setClientSelfVariableAsString) {
            ts3Functions.setClientSelfVariableAsString(scHandlerID, CLIENT_OUTPUT_MUTED, "1");
        }
    } else {
        if (ts3Functions.setClientSelfVariableAsInt) {
            ts3Functions.setClientSelfVariableAsInt(scHandlerID, CLIENT_OUTPUT_MUTED, MUTEOUTPUT_NONE);
        }
        if (ts3Functions.setClientSelfVariableAsString) {
            ts3Functions.setClientSelfVariableAsString(scHandlerID, CLIENT_OUTPUT_MUTED, "0");
        }
    }

    ts3Functions.flushClientSelfUpdates(scHandlerID, NULL);
    update_live_menu_icons();
}

static int get_sound_path(const char* sound_name, char* out_path, size_t maxLen) {
    if (ts3Functions.getResourcesPath) {
        char resPath[PATH_BUFSIZE / 2] = {0};
        ts3Functions.getResourcesPath(resPath, sizeof(resPath));
        if (resPath[0]) {
            size_t len = strlen(resPath);
            if (resPath[len - 1] == '/') {
                snprintf(out_path, maxLen, "%ssound/default/%s", resPath, sound_name);
            } else {
                snprintf(out_path, maxLen, "%s/sound/default/%s", resPath, sound_name);
            }
            if (access(out_path, R_OK) == 0) return 1;
        }
    }

    const char* fallbacks[] = {
        "/app/extra/sound/default",
        "/usr/share/teamspeak3/sound/default",
        "/opt/teamspeak3/sound/default"
    };
    for (size_t i = 0; i < sizeof(fallbacks)/sizeof(fallbacks[0]); i++) {
        snprintf(out_path, maxLen, "%s/%s", fallbacks[i], sound_name);
        if (access(out_path, R_OK) == 0) return 1;
    }

    out_path[0] = '\0';
    return 0;
}

struct MuteThreadArg {
    uint64_t scHandlerID;
};

static void* delayed_mute_thread(void* param) {
    struct MuteThreadArg* arg = (struct MuteThreadArg*)param;
    uint64_t scHandlerID = arg->scHandlerID;
    free(arg);

    /*
     * 1. If microphone mute is configured, mute it immediately upon join so that
     *    no microphone audio leaks into the channel.
     */
    if (cfg_mute_mic) {
        if (ts3Functions.setClientSelfVariableAsInt) {
            ts3Functions.setClientSelfVariableAsInt(scHandlerID, CLIENT_INPUT_MUTED, MUTEINPUT_MUTED);
        }
        if (ts3Functions.setClientSelfVariableAsString) {
            ts3Functions.setClientSelfVariableAsString(scHandlerID, CLIENT_INPUT_MUTED, "1");
        }
        if (ts3Functions.flushClientSelfUpdates) {
            ts3Functions.flushClientSelfUpdates(scHandlerID, NULL);
        }
    } else {
        if (ts3Functions.setClientSelfVariableAsInt) {
            ts3Functions.setClientSelfVariableAsInt(scHandlerID, CLIENT_INPUT_MUTED, MUTEINPUT_NONE);
        }
        if (ts3Functions.setClientSelfVariableAsString) {
            ts3Functions.setClientSelfVariableAsString(scHandlerID, CLIENT_INPUT_MUTED, "0");
        }
        if (ts3Functions.flushClientSelfUpdates) {
            ts3Functions.flushClientSelfUpdates(scHandlerID, NULL);
        }
    }
    update_live_menu_icons();

    /* 2. Short pause (300ms) for channel audio connection to stabilize */
    usleep(300000);

    /*
     * 3. Play voice announcement using TeamSpeak 3's built-in sound files:
     *    - When both are muted: "Microphone muted" then "Sound muted"
     *    - When only mic is muted: "Microphone muted"
     *    - When only speakers are muted: "Sound muted"
     *    Speakers are kept unmuted during playback so the user can hear the announcement.
     */
    char mic_wav[PATH_BUFSIZE] = {0};
    char sound_wav[PATH_BUFSIZE] = {0};
    int has_mic_wav = get_sound_path("mic_muted.wav", mic_wav, sizeof(mic_wav));
    int has_sound_wav = get_sound_path("sound_muted.wav", sound_wav, sizeof(sound_wav));

    if (cfg_mute_mic && cfg_mute_speaker) {
        if (has_mic_wav && ts3Functions.playWaveFile) {
            ts3Functions.playWaveFile(scHandlerID, mic_wav);
            usleep(1100000);
        }
        if (has_sound_wav && ts3Functions.playWaveFile) {
            ts3Functions.playWaveFile(scHandlerID, sound_wav);
            usleep(1100000);
        }
        log_info(scHandlerID, "AutoMute: Played voice announcements (mic & speakers muted)");
    } else if (cfg_mute_mic) {
        if (has_mic_wav && ts3Functions.playWaveFile) {
            ts3Functions.playWaveFile(scHandlerID, mic_wav);
            usleep(1100000);
        }
        log_info(scHandlerID, "AutoMute: Played voice announcement (mic muted)");
    } else if (cfg_mute_speaker) {
        if (has_sound_wav && ts3Functions.playWaveFile) {
            ts3Functions.playWaveFile(scHandlerID, sound_wav);
            usleep(1100000);
        }
        log_info(scHandlerID, "AutoMute: Played voice announcement (speakers muted)");
    }

    /*
     * 4. Now apply full mute state (including speaker mute) and flush updates.
     */
    apply_mute(scHandlerID);
    usleep(150000);
    apply_mute(scHandlerID);
    return NULL;
}

const char* ts3plugin_name(void) {
    return "Auto Mute";
}

const char* ts3plugin_version(void) {
    return "1.2.0";
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
    init_config_path();
    load_config();
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

    load_config();

    /* Green when not muted, red when muted */
    const char* icon_always = cfg_always_mute ? "mute_red.png" : "mute_green.png";
    const char* icon_mic = cfg_mute_mic ? "mute_red.png" : "mute_green.png";
    const char* icon_spk = cfg_mute_speaker ? "mute_red.png" : "mute_green.png";

    /* Determine live mute state for the mute function (item 5): green when not muted, red when muted */
    int is_muted = 0;
    if (ts3Functions.getCurrentServerConnectionHandlerID && ts3Functions.getClientSelfVariableAsInt) {
        uint64 scHandlerID = ts3Functions.getCurrentServerConnectionHandlerID();
        if (scHandlerID) {
            int mic_m = 0, spk_m = 0;
            ts3Functions.getClientSelfVariableAsInt(scHandlerID, CLIENT_INPUT_MUTED, &mic_m);
            ts3Functions.getClientSelfVariableAsInt(scHandlerID, CLIENT_OUTPUT_MUTED, &spk_m);
            if (mic_m || spk_m) {
                is_muted = 1;
            }
        }
    }
    const char* icon_mute_func = is_muted ? "mute_red.png" : "mute_green.png";

    (*menuItems)[0] = createMenuItem(PLUGIN_MENU_TYPE_GLOBAL, 1, "Toggle: Mute on All Servers", icon_always);
    (*menuItems)[1] = createMenuItem(PLUGIN_MENU_TYPE_GLOBAL, 2, "Toggle: Mute Microphone on Join", icon_mic);
    (*menuItems)[2] = createMenuItem(PLUGIN_MENU_TYPE_GLOBAL, 3, "Toggle: Mute Speakers on Join", icon_spk);
    (*menuItems)[3] = createMenuItem(PLUGIN_MENU_TYPE_GLOBAL, 4, "Toggle: Current Server in AutoMute List", "settings.png");
    (*menuItems)[4] = createMenuItem(PLUGIN_MENU_TYPE_GLOBAL, 5, "Mute Microphone & Speakers Now", icon_mute_func);
    (*menuItems)[5] = NULL;

    *menuIcon = (char*)malloc(PLUGIN_MENU_BUFSZ);
    if (*menuIcon) {
        snprintf(*menuIcon, PLUGIN_MENU_BUFSZ, "icon.png");
    }
}

void ts3plugin_onMenuItemEvent(uint64_t scHandlerID, enum PluginMenuType type, int menuItemID, uint64_t selectedItemID) {
    (void)selectedItemID;
    if (type != PLUGIN_MENU_TYPE_GLOBAL) return;

    if (menuItemID == 1) {
        cfg_always_mute = !cfg_always_mute;
        save_config();
        update_live_menu_icons();
        if (ts3Functions.printMessageToCurrentTab) {
            char feedback[256];
            snprintf(feedback, sizeof(feedback), "AutoMute: Mute on All Servers is now %s.", cfg_always_mute ? "MUTED [RED]" : "ACTIVE [GREEN]");
            ts3Functions.printMessageToCurrentTab(feedback);
        }
    } else if (menuItemID == 2) {
        cfg_mute_mic = !cfg_mute_mic;
        save_config();
        /* Apply immediately to current connection */
        if (scHandlerID) {
            if (cfg_mute_mic) {
                if (ts3Functions.setClientSelfVariableAsInt) ts3Functions.setClientSelfVariableAsInt(scHandlerID, CLIENT_INPUT_MUTED, MUTEINPUT_MUTED);
                if (ts3Functions.setClientSelfVariableAsString) ts3Functions.setClientSelfVariableAsString(scHandlerID, CLIENT_INPUT_MUTED, "1");
            } else {
                if (ts3Functions.setClientSelfVariableAsInt) ts3Functions.setClientSelfVariableAsInt(scHandlerID, CLIENT_INPUT_MUTED, MUTEINPUT_NONE);
                if (ts3Functions.setClientSelfVariableAsString) ts3Functions.setClientSelfVariableAsString(scHandlerID, CLIENT_INPUT_MUTED, "0");
            }
            if (ts3Functions.flushClientSelfUpdates) ts3Functions.flushClientSelfUpdates(scHandlerID, NULL);
        }
        update_live_menu_icons();
        if (ts3Functions.printMessageToCurrentTab) {
            char feedback[256];
            snprintf(feedback, sizeof(feedback), "AutoMute: Mute Microphone on Join is now %s.", cfg_mute_mic ? "MUTED [RED]" : "ACTIVE [GREEN]");
            ts3Functions.printMessageToCurrentTab(feedback);
        }
    } else if (menuItemID == 3) {
        cfg_mute_speaker = !cfg_mute_speaker;
        save_config();
        /* Apply immediately to current connection */
        if (scHandlerID) {
            if (cfg_mute_speaker) {
                if (ts3Functions.setClientSelfVariableAsInt) ts3Functions.setClientSelfVariableAsInt(scHandlerID, CLIENT_OUTPUT_MUTED, MUTEOUTPUT_MUTED);
                if (ts3Functions.setClientSelfVariableAsString) ts3Functions.setClientSelfVariableAsString(scHandlerID, CLIENT_OUTPUT_MUTED, "1");
            } else {
                if (ts3Functions.setClientSelfVariableAsInt) ts3Functions.setClientSelfVariableAsInt(scHandlerID, CLIENT_OUTPUT_MUTED, MUTEOUTPUT_NONE);
                if (ts3Functions.setClientSelfVariableAsString) ts3Functions.setClientSelfVariableAsString(scHandlerID, CLIENT_OUTPUT_MUTED, "0");
            }
            if (ts3Functions.flushClientSelfUpdates) ts3Functions.flushClientSelfUpdates(scHandlerID, NULL);
        }
        update_live_menu_icons();
        if (ts3Functions.printMessageToCurrentTab) {
            char feedback[256];
            snprintf(feedback, sizeof(feedback), "AutoMute: Mute Speakers on Join is now %s.", cfg_mute_speaker ? "MUTED [RED]" : "ACTIVE [GREEN]");
            ts3Functions.printMessageToCurrentTab(feedback);
        }
    } else if (menuItemID == 4) {
        char* serverName = NULL;
        if (ts3Functions.getServerVariableAsString && ts3Functions.getServerVariableAsString(scHandlerID, VIRTUALSERVER_NAME, &serverName) == 0 && serverName) {
            toggle_server_in_list(serverName);
            save_config();
            if (ts3Functions.printMessageToCurrentTab) {
                int inList = server_in_list(cfg_server_list, serverName);
                char feedback[512];
                snprintf(feedback, sizeof(feedback), "AutoMute: Server '%s' %s AutoMute list.", serverName, inList ? "ADDED to" : "REMOVED from");
                ts3Functions.printMessageToCurrentTab(feedback);
            }
            if (ts3Functions.freeMemory) ts3Functions.freeMemory(serverName);
        }
    } else if (menuItemID == 5) {
        /* Check current mute state: toggle between mute and unmute */
        int mic_m = 0, spk_m = 0;
        if (ts3Functions.getClientSelfVariableAsInt) {
            ts3Functions.getClientSelfVariableAsInt(scHandlerID, CLIENT_INPUT_MUTED, &mic_m);
            ts3Functions.getClientSelfVariableAsInt(scHandlerID, CLIENT_OUTPUT_MUTED, &spk_m);
        }

        if (mic_m || spk_m || g_forced_client_muted == 1) {
            /* Currently muted -> unmute (turn green) */
            g_forced_client_muted = 0;
            if (ts3Functions.setClientSelfVariableAsInt) {
                ts3Functions.setClientSelfVariableAsInt(scHandlerID, CLIENT_INPUT_MUTED, MUTEINPUT_NONE);
                ts3Functions.setClientSelfVariableAsInt(scHandlerID, CLIENT_OUTPUT_MUTED, MUTEOUTPUT_NONE);
            }
            if (ts3Functions.setClientSelfVariableAsString) {
                ts3Functions.setClientSelfVariableAsString(scHandlerID, CLIENT_INPUT_MUTED, "0");
                ts3Functions.setClientSelfVariableAsString(scHandlerID, CLIENT_OUTPUT_MUTED, "0");
            }
            if (ts3Functions.flushClientSelfUpdates) {
                ts3Functions.flushClientSelfUpdates(scHandlerID, NULL);
            }
            update_live_menu_icons();
            if (ts3Functions.printMessageToCurrentTab) {
                ts3Functions.printMessageToCurrentTab("AutoMute: Microphone & Speakers UNMUTED (Active / Green).");
            }
        } else {
            /* Currently unmuted -> mute (turn red) */
            g_forced_client_muted = 1;
            if (ts3Functions.setClientSelfVariableAsInt) {
                ts3Functions.setClientSelfVariableAsInt(scHandlerID, CLIENT_INPUT_MUTED, MUTEINPUT_MUTED);
                ts3Functions.setClientSelfVariableAsInt(scHandlerID, CLIENT_OUTPUT_MUTED, MUTEOUTPUT_MUTED);
            }
            if (ts3Functions.setClientSelfVariableAsString) {
                ts3Functions.setClientSelfVariableAsString(scHandlerID, CLIENT_INPUT_MUTED, "1");
                ts3Functions.setClientSelfVariableAsString(scHandlerID, CLIENT_OUTPUT_MUTED, "1");
            }
            if (ts3Functions.flushClientSelfUpdates) {
                ts3Functions.flushClientSelfUpdates(scHandlerID, NULL);
            }
            update_live_menu_icons();
            if (ts3Functions.printMessageToCurrentTab) {
                ts3Functions.printMessageToCurrentTab("AutoMute: Microphone & Speakers MUTED (Muted / Red).");
            }
        }
    }

    char msg[512];
    snprintf(msg, sizeof(msg), "AutoMute: always_mute=%s, mute_mic=%s, mute_speaker=%s, server_list=[%s]",
             cfg_always_mute ? "true" : "false",
             cfg_mute_mic ? "true" : "false",
             cfg_mute_speaker ? "true" : "false",
             cfg_server_list);
    log_info(scHandlerID, msg);
}

void ts3plugin_onConnectStatusChangeEvent(uint64_t scHandlerID, int newStatus, unsigned int errorNumber) {
    (void)errorNumber;
    if (newStatus != STATUS_CONNECTION_ESTABLISHED) return;

    g_forced_client_muted = -1;
    load_config();
    int should_mute = cfg_always_mute;

    if (!should_mute && cfg_server_list[0]) {
        char* serverName = NULL;
        if (ts3Functions.getServerVariableAsString && ts3Functions.getServerVariableAsString(scHandlerID, VIRTUALSERVER_NAME, &serverName) == 0 && serverName) {
            if (server_in_list(cfg_server_list, serverName)) {
                should_mute = 1;
            }
            if (ts3Functions.freeMemory) ts3Functions.freeMemory(serverName);
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

void ts3plugin_onClientSelfVariableUpdateEvent(uint64_t scHandlerID, int flag, const char* oldValue, const char* newValue) {
    (void)scHandlerID;
    (void)oldValue;
    (void)newValue;
    if (flag == CLIENT_INPUT_MUTED || flag == CLIENT_OUTPUT_MUTED) {
        g_forced_client_muted = -1;
        update_live_menu_icons();
    }
}
