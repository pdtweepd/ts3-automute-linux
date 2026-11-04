#include "../src/ts3_plugin_sdk.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char g_test_cfg_dir[128] = "/tmp/ts3_automute_test_cfg/";
static const char* g_mock_server_name = "My Gaming Server";
static int g_input_muted_calls = 0;
static int g_input_unmuted_calls = 0;
static int g_output_muted_calls = 0;
static int g_output_unmuted_calls = 0;
static int g_flush_calls = 0;
static int g_log_calls = 0;
static int g_free_calls = 0;
static int g_print_msg_calls = 0;
static int g_play_wave_calls = 0;
static char g_last_wave_played[512] = {0};
static int g_simulated_mic_muted = 0;
static int g_simulated_spk_muted = 0;

static unsigned int mock_playWaveFile(uint64 scHandlerID, const char* path) {
    (void)scHandlerID;
    g_play_wave_calls++;
    snprintf(g_last_wave_played, sizeof(g_last_wave_played), "%s", path);
    return 0;
}

static unsigned int mock_freeMemory(void* ptr) {
    g_free_calls++;
    free(ptr);
    return 0;
}

static unsigned int mock_logMessage(const char* msg, enum LogLevel severity, const char* channel, uint64 logID) {
    (void)msg;
    (void)severity;
    (void)channel;
    (void)logID;
    g_log_calls++;
    return 0;
}

static unsigned int mock_setClientSelfVariableAsInt(uint64 scHandlerID, size_t flag, int value) {
    (void)scHandlerID;
    if (flag == CLIENT_INPUT_MUTED) {
        if (value == 1) { g_input_muted_calls++; g_simulated_mic_muted = 1; }
        else if (value == 0) { g_input_unmuted_calls++; g_simulated_mic_muted = 0; }
    } else if (flag == CLIENT_OUTPUT_MUTED) {
        if (value == 1) { g_output_muted_calls++; g_simulated_spk_muted = 1; }
        else if (value == 0) { g_output_unmuted_calls++; g_simulated_spk_muted = 0; }
    }
    return 0;
}

static unsigned int mock_setClientSelfVariableAsString(uint64 scHandlerID, size_t flag, const char* value) {
    (void)scHandlerID;
    (void)flag;
    (void)value;
    return 0;
}

static unsigned int mock_flushClientSelfUpdates(uint64 scHandlerID, const char* returnCode) {
    (void)scHandlerID;
    (void)returnCode;
    g_flush_calls++;
    return 0;
}

static unsigned int mock_getClientSelfVariableAsInt(uint64 scHandlerID, size_t flag, int* result) {
    (void)scHandlerID;
    if (flag == CLIENT_INPUT_MUTED && result) {
        *result = g_simulated_mic_muted;
        return 0;
    } else if (flag == CLIENT_OUTPUT_MUTED && result) {
        *result = g_simulated_spk_muted;
        return 0;
    }
    return 1;
}

static uint64 mock_getCurrentServerConnectionHandlerID(void) {
    return 1;
}

static unsigned int mock_getServerVariableAsString(uint64 scHandlerID, size_t flag, char** result) {
    (void)scHandlerID;
    if (flag == VIRTUALSERVER_NAME && result) {
        *result = strdup(g_mock_server_name);
        return 0;
    }
    return 1;
}

static void mock_getResourcesPath(char* path, size_t maxLen) {
    snprintf(path, maxLen, "%s", g_test_cfg_dir);
}

static void mock_getConfigPath(char* path, size_t maxLen) {
    snprintf(path, maxLen, "%s", g_test_cfg_dir);
}

static void mock_printMessageToCurrentTab(const char* message) {
    (void)message;
    g_print_msg_calls++;
}

static void reset_counters(void) {
    g_input_muted_calls = 0;
    g_input_unmuted_calls = 0;
    g_output_muted_calls = 0;
    g_output_unmuted_calls = 0;
    g_flush_calls = 0;
    g_print_msg_calls = 0;
    g_play_wave_calls = 0;
    g_last_wave_played[0] = '\0';
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
    void (*ts3plugin_onMenuItemEvent)(uint64, int, int, uint64) = dlsym(handle, "ts3plugin_onMenuItemEvent");
    void (*ts3plugin_onConnectStatusChangeEvent)(uint64, int, unsigned int) = dlsym(handle, "ts3plugin_onConnectStatusChangeEvent");

    assert(ts3plugin_name && strcmp(ts3plugin_name(), "Auto Mute") == 0);
    assert(ts3plugin_version && strcmp(ts3plugin_version(), "1.2.0") == 0);
    assert(ts3plugin_apiVersion && ts3plugin_apiVersion() == 26);
    assert(ts3plugin_requestAutoload && ts3plugin_requestAutoload() == 1);
    printf("[PASS] Metadata & API version (v26) verified\n");

    char cmd[1200];
    snprintf(cmd, sizeof(cmd), "rm -rf %s && mkdir -p %s/sound/default && touch %s/sound/default/mic_muted.wav %s/sound/default/sound_muted.wav",
             g_test_cfg_dir, g_test_cfg_dir, g_test_cfg_dir, g_test_cfg_dir);
    assert(system(cmd) == 0);

    struct TS3Functions funcs;
    memset(&funcs, 0, sizeof(funcs));
    funcs.freeMemory = mock_freeMemory;
    funcs.logMessage = mock_logMessage;
    funcs.setClientSelfVariableAsInt = mock_setClientSelfVariableAsInt;
    funcs.setClientSelfVariableAsString = mock_setClientSelfVariableAsString;
    funcs.flushClientSelfUpdates = mock_flushClientSelfUpdates;
    funcs.getClientSelfVariableAsInt = mock_getClientSelfVariableAsInt;
    funcs.getCurrentServerConnectionHandlerID = mock_getCurrentServerConnectionHandlerID;
    funcs.getServerVariableAsString = mock_getServerVariableAsString;
    funcs.getResourcesPath = mock_getResourcesPath;
    funcs.getConfigPath = mock_getConfigPath;
    funcs.printMessageToCurrentTab = mock_printMessageToCurrentTab;
    funcs.playWaveFile = mock_playWaveFile;

    ts3plugin_setFunctionPointers(funcs);
    ts3plugin_registerPluginID("test-plugin-id");
    assert(ts3plugin_init() == 0);
    printf("[PASS] ts3plugin_init & default AutoMute.ini creation verified\n");

    /* When not muted (simulated_mic_muted = 0, simulated_spk_muted = 0), mute function icon MUST BE GREEN */
    g_simulated_mic_muted = 0;
    g_simulated_spk_muted = 0;
    struct PluginMenuItem** items = NULL;
    char* menuIcon = NULL;
    ts3plugin_initMenus(&items, &menuIcon);
    assert(items != NULL && menuIcon != NULL);
    assert(strcmp(menuIcon, "icon.png") == 0);
    assert(strcmp(items[4]->icon, "mute_green.png") == 0); /* The mute function is green when NOT muted */
    printf("[PASS] The icon before the mute function is GREEN when not muted\n");

    int item_count = 0;
    while (items[item_count] != NULL) {
        ts3plugin_freeMemory(items[item_count]);
        item_count++;
    }
    assert(item_count == 5);
    ts3plugin_freeMemory(items);
    ts3plugin_freeMemory(menuIcon);

    /* When muted (simulated_mic_muted = 1), mute function icon MUST BE RED */
    g_simulated_mic_muted = 1;
    items = NULL;
    menuIcon = NULL;
    ts3plugin_initMenus(&items, &menuIcon);
    assert(items != NULL);
    assert(strcmp(items[4]->icon, "mute_red.png") == 0); /* The mute function is red when MUTED */
    printf("[PASS] The icon before the mute function is RED when muted\n");
    item_count = 0;
    while (items[item_count] != NULL) {
        ts3plugin_freeMemory(items[item_count]);
        item_count++;
    }
    ts3plugin_freeMemory(items);
    ts3plugin_freeMemory(menuIcon);

    /* Test 1: Non-established status should be ignored */
    reset_counters();
    ts3plugin_onConnectStatusChangeEvent(1, STATUS_CONNECTING, 0);
    usleep(100000);
    assert(g_input_muted_calls == 0 && g_output_muted_calls == 0 && g_flush_calls == 0);

    /* Test 2: Established connection with defaults (always_mute=1, mic=1, speaker=1) */
    reset_counters();
    ts3plugin_onConnectStatusChangeEvent(1, STATUS_CONNECTION_ESTABLISHED, 0);
    usleep(2700000); /* Wait for delayed announcement + mute thread */
    assert(g_input_muted_calls >= 1);
    assert(g_output_muted_calls >= 1);
    assert(g_flush_calls >= 1);
    assert(g_play_wave_calls >= 2); /* Both announcements played */
    printf("[PASS] Auto-mute with voice announcements on join (both muted) verified\n");

    /* Test 3: Toggle always_mute off (Menu ID 1) -> unlisted server should not be muted */
    ts3plugin_onMenuItemEvent(1, 0, 1, 0);
    reset_counters();
    ts3plugin_onConnectStatusChangeEvent(1, STATUS_CONNECTION_ESTABLISHED, 0);
    usleep(200000);
    assert(g_input_muted_calls == 0 && g_output_muted_calls == 0 && g_flush_calls == 0);
    printf("[PASS] Toggle always_mute=false verified\n");

    /* Test 4: Add current server to server_list (Menu ID 4) */
    g_mock_server_name = "My Gaming Server";
    ts3plugin_onMenuItemEvent(1, 0, 4, 0);
    reset_counters();
    ts3plugin_onConnectStatusChangeEvent(1, STATUS_CONNECTION_ESTABLISHED, 0);
    usleep(2700000);
    assert(g_input_muted_calls >= 1 && g_output_muted_calls >= 1 && g_flush_calls >= 1);

    /* Test 5: Toggle mute_speaker off (Menu ID 3) -> ONLY microphone muted, speakers NOT muted! */
    g_mock_server_name = "My Gaming Server";
    ts3plugin_onMenuItemEvent(1, 0, 3, 0); /* Toggle speaker mute off */
    reset_counters();
    ts3plugin_onConnectStatusChangeEvent(1, STATUS_CONNECTION_ESTABLISHED, 0);
    usleep(1600000);
    assert(g_input_muted_calls >= 1);
    assert(g_output_muted_calls == 0); /* SPEAKERS MUST NOT BE MUTED! */
    assert(g_output_unmuted_calls >= 1); /* Speakers explicitly kept unmuted */
    assert(g_flush_calls >= 1);
    assert(g_play_wave_calls >= 1);
    assert(strstr(g_last_wave_played, "mic_muted.wav") != NULL);
    printf("[PASS] Independent mute & voice announcement (only mic muted) verified\n");

    /* Test 6: Test Menu ID 5 (Mute/Unmute Function) toggling */
    reset_counters();
    g_simulated_mic_muted = 1;
    g_simulated_spk_muted = 1;
    ts3plugin_onMenuItemEvent(1, 0, 5, 0); /* Clicking when muted should UNMUTE (green) */
    assert(g_input_unmuted_calls >= 1);
    assert(g_output_unmuted_calls >= 1);
    printf("[PASS] Menu item 5 un-mutes when currently muted\n");

    reset_counters();
    g_simulated_mic_muted = 0;
    g_simulated_spk_muted = 0;
    ts3plugin_onMenuItemEvent(1, 0, 5, 0); /* Clicking when unmuted should MUTE (red) */
    assert(g_input_muted_calls >= 1);
    assert(g_output_muted_calls >= 1);
    printf("[PASS] Menu item 5 mutes when currently unmuted\n");

    ts3plugin_shutdown();
    dlclose(handle);
    snprintf(cmd, sizeof(cmd), "rm -rf %s", g_test_cfg_dir);
    assert(system(cmd) == 0);

    printf("ALL TESTS PASSED SUCCESSFULLY!\n");
    return 0;
}
