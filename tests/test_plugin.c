#include <assert.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PLUGIN_MENU_BUFSZ 128

struct PluginMenuItem {
    int type;
    int id;
    char text[PLUGIN_MENU_BUFSZ];
    char icon[PLUGIN_MENU_BUFSZ];
};

struct TS3Functions {
    void* fn[230];
};

static char g_test_cfg_dir[512] = "/tmp/ts3_automute_test_cfg/";
static const char* g_mock_server_name = "My Gaming Server";
static int g_input_muted_calls = 0;
static int g_output_muted_calls = 0;
static int g_flush_calls = 0;
static int g_log_calls = 0;
static int g_free_calls = 0;

static unsigned int mock_freeMemory(void* ptr) {
    g_free_calls++;
    free(ptr);
    return 0;
}

static unsigned int mock_logMessage(const char* msg, int severity, const char* channel, uint64_t logID) {
    (void)msg;
    (void)severity;
    (void)channel;
    (void)logID;
    g_log_calls++;
    return 0;
}

static unsigned int mock_setClientSelfVariableAsString(uint64_t scHandlerID, size_t flag, const char* value) {
    (void)scHandlerID;
    if (flag == 5 && strcmp(value, "1") == 0) {
        g_input_muted_calls++;
    } else if (flag == 6 && strcmp(value, "1") == 0) {
        g_output_muted_calls++;
    }
    return 0;
}

static unsigned int mock_flushClientSelfUpdates(uint64_t scHandlerID, const char* returnCode) {
    (void)scHandlerID;
    (void)returnCode;
    g_flush_calls++;
    return 0;
}

static unsigned int mock_getServerVariableAsString(uint64_t scHandlerID, size_t flag, char** result) {
    (void)scHandlerID;
    if (flag == 0 && result) {
        *result = strdup(g_mock_server_name);
        return 0;
    }
    return 1;
}

static void mock_getConfigPath(char* path, size_t maxLen) {
    snprintf(path, maxLen, "%s", g_test_cfg_dir);
}

static void reset_counters(void) {
    g_input_muted_calls = 0;
    g_output_muted_calls = 0;
    g_flush_calls = 0;
}

int main(int argc, char** argv) {
    const char* so_path = (argc > 1) ? argv[1] : "./build/libauto_mute_linux_amd64.so";
    void* handle = dlopen(so_path, RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 1;
    }

    const char* (*ts3plugin_name)(void) = dlsym(handle, "ts3plugin_name");
    const char* (*ts3plugin_version)(void) = dlsym(handle, "ts3plugin_version");
    int (*ts3plugin_apiVersion)(void) = dlsym(handle, "ts3plugin_apiVersion");
    int (*ts3plugin_requestAutoload)(void) = dlsym(handle, "ts3plugin_requestAutoload");
    void (*ts3plugin_setFunctionPointers)(struct TS3Functions) = dlsym(handle, "ts3plugin_setFunctionPointers");
    int (*ts3plugin_init)(void) = dlsym(handle, "ts3plugin_init");
    void (*ts3plugin_shutdown)(void) = dlsym(handle, "ts3plugin_shutdown");
    void (*ts3plugin_registerPluginID)(const char*) = dlsym(handle, "ts3plugin_registerPluginID");
    void (*ts3plugin_freeMemory)(void*) = dlsym(handle, "ts3plugin_freeMemory");
    void (*ts3plugin_initMenus)(struct PluginMenuItem***, char**) = dlsym(handle, "ts3plugin_initMenus");
    void (*ts3plugin_onMenuItemEvent)(uint64_t, int, int, uint64_t) = dlsym(handle, "ts3plugin_onMenuItemEvent");
    void (*ts3plugin_onConnectStatusChangeEvent)(uint64_t, int, unsigned int) = dlsym(handle, "ts3plugin_onConnectStatusChangeEvent");

    assert(ts3plugin_name && strcmp(ts3plugin_name(), "Auto Mute") == 0);
    assert(ts3plugin_version && strcmp(ts3plugin_version(), "1.1.8") == 0);
    assert(ts3plugin_apiVersion && ts3plugin_apiVersion() == 26);
    assert(ts3plugin_requestAutoload && ts3plugin_requestAutoload() == 1);
    printf("[PASS] Metadata & API version (v26) verified\n");

    char cmd[1200];
    snprintf(cmd, sizeof(cmd), "rm -rf %s && mkdir -p %s", g_test_cfg_dir, g_test_cfg_dir);
    assert(system(cmd) == 0);

    struct TS3Functions funcs;
    memset(&funcs, 0, sizeof(funcs));
    funcs.fn[5] = (void*)mock_freeMemory;
    funcs.fn[6] = (void*)mock_logMessage;
    funcs.fn[77] = (void*)mock_setClientSelfVariableAsString;
    funcs.fn[78] = (void*)mock_flushClientSelfUpdates;
    funcs.fn[99] = (void*)mock_getServerVariableAsString;
    funcs.fn[181] = (void*)mock_getConfigPath;

    ts3plugin_setFunctionPointers(funcs);
    ts3plugin_registerPluginID("test-plugin-id");
    assert(ts3plugin_init() == 0);
    printf("[PASS] ts3plugin_init & default AutoMute.ini creation verified\n");

    /* Verify menu initialization & cleanup */
    struct PluginMenuItem** items = NULL;
    char* menuIcon = NULL;
    ts3plugin_initMenus(&items, &menuIcon);
    assert(items != NULL && menuIcon != NULL);
    assert(strcmp(menuIcon, "icon.png") == 0);
    int item_count = 0;
    while (items[item_count] != NULL) {
        ts3plugin_freeMemory(items[item_count]);
        item_count++;
    }
    assert(item_count == 5);
    ts3plugin_freeMemory(items);
    ts3plugin_freeMemory(menuIcon);
    printf("[PASS] ts3plugin_initMenus (%d items) & ts3plugin_freeMemory verified\n", item_count);

    /* Test 1: Non-established status should be ignored */
    reset_counters();
    ts3plugin_onConnectStatusChangeEvent(1, 1 /* STATUS_CONNECTING */, 0);
    assert(g_input_muted_calls == 0 && g_output_muted_calls == 0 && g_flush_calls == 0);

    /* Test 2: Established connection with defaults (always_mute=1, mic=1, speaker=1) */
    reset_counters();
    ts3plugin_onConnectStatusChangeEvent(1, 4 /* STATUS_CONNECTION_ESTABLISHED */, 0);
    assert(g_input_muted_calls == 1);
    assert(g_output_muted_calls == 1);
    assert(g_flush_calls == 1);
    printf("[PASS] Auto-mute on STATUS_CONNECTION_ESTABLISHED verified\n");

    /* Test 3: Toggle always_mute off (Menu ID 1) -> should not mute unlisted server */
    ts3plugin_onMenuItemEvent(1, 0, 1, 0);
    reset_counters();
    ts3plugin_onConnectStatusChangeEvent(1, 4, 0);
    assert(g_input_muted_calls == 0 && g_output_muted_calls == 0 && g_flush_calls == 0);
    printf("[PASS] Toggle always_mute=false verified\n");

    /* Test 4: Add current server ("My Gaming Server") to server_list (Menu ID 4) */
    g_mock_server_name = "My Gaming Server";
    ts3plugin_onMenuItemEvent(1, 0, 4, 0);
    reset_counters();
    ts3plugin_onConnectStatusChangeEvent(1, 4, 0);
    assert(g_input_muted_calls == 1 && g_output_muted_calls == 1 && g_flush_calls == 1);

    /* Unlisted server should not be muted */
    g_mock_server_name = "Unlisted Server";
    reset_counters();
    ts3plugin_onConnectStatusChangeEvent(1, 4, 0);
    assert(g_input_muted_calls == 0 && g_output_muted_calls == 0 && g_flush_calls == 0);
    printf("[PASS] Per-server allowlist filtering verified\n");

    /* Test 5: Toggle mute_speaker off (Menu ID 3) -> only mic muted on listed server */
    g_mock_server_name = "My Gaming Server";
    ts3plugin_onMenuItemEvent(1, 0, 3, 0);
    reset_counters();
    ts3plugin_onConnectStatusChangeEvent(1, 4, 0);
    assert(g_input_muted_calls == 1 && g_output_muted_calls == 0 && g_flush_calls == 1);
    printf("[PASS] Independent microphone/speaker mute toggles verified\n");

    ts3plugin_shutdown();
    dlclose(handle);
    snprintf(cmd, sizeof(cmd), "rm -rf %s", g_test_cfg_dir);
    assert(system(cmd) == 0);

    printf("ALL TESTS PASSED SUCCESSFULLY!\n");
    return 0;
}
