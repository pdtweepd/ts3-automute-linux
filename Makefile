CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -fPIC
LDFLAGS ?= -shared

PLUGIN_NAME = libauto_mute_linux_amd64.so
PKG_NAME = ts3-plugin-automute
VERSION = 1.1.9
ARCH = amd64

BUILD_DIR = build
DEB_ROOT = $(BUILD_DIR)/deb-pkg
TS3_PKG_ROOT = $(BUILD_DIR)/ts3-pkg

.PHONY: all clean test install package-ts3 package-deb

all: $(BUILD_DIR)/$(PLUGIN_NAME) package-ts3 package-deb

$(BUILD_DIR)/$(PLUGIN_NAME): src/auto_mute_linux.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) src/auto_mute_linux.c -o $(BUILD_DIR)/$(PLUGIN_NAME)

test: $(BUILD_DIR)/$(PLUGIN_NAME)
	$(CC) -O2 -Wall -Wextra tests/test_plugin.c -ldl -o $(BUILD_DIR)/test_plugin
	./$(BUILD_DIR)/test_plugin ./$(BUILD_DIR)/$(PLUGIN_NAME)

package-ts3: $(BUILD_DIR)/$(PLUGIN_NAME)
	@rm -rf $(TS3_PKG_ROOT)
	@mkdir -p $(TS3_PKG_ROOT)/plugins/auto_mute
	@cp $(BUILD_DIR)/$(PLUGIN_NAME) $(TS3_PKG_ROOT)/plugins/
	@if [ -d assets/auto_mute ]; then cp -r assets/auto_mute/* $(TS3_PKG_ROOT)/plugins/auto_mute/ 2>/dev/null || true; fi
	@printf "Name = Auto Mute\nType = Plugin\nAuthor = CubE / Linux Port\nVersion = $(VERSION)\nPlatforms = linux_amd64\nDescription = \"AutoMute lets you choose if you want to join servers with your microphone and/or speakers muted.\"\n" > $(TS3_PKG_ROOT)/package.ini
	@cd $(TS3_PKG_ROOT) && zip -rq ../automute_$(VERSION)_linux_$(ARCH).ts3_plugin package.ini plugins/

package-deb: $(BUILD_DIR)/$(PLUGIN_NAME)
	@rm -rf $(DEB_ROOT)
	@mkdir -p $(DEB_ROOT)/DEBIAN
	@mkdir -p $(DEB_ROOT)/usr/share/ts3-plugin-automute/plugins/auto_mute
	@mkdir -p $(DEB_ROOT)/usr/bin
	@cp $(BUILD_DIR)/$(PLUGIN_NAME) $(DEB_ROOT)/usr/share/ts3-plugin-automute/plugins/
	@if [ -d assets/auto_mute ]; then cp -r assets/auto_mute/* $(DEB_ROOT)/usr/share/ts3-plugin-automute/plugins/auto_mute/ 2>/dev/null || true; fi
	@cp install.sh $(DEB_ROOT)/usr/bin/ts3-automute-install
	@chmod 0755 $(DEB_ROOT)/usr/bin/ts3-automute-install
	@printf "Package: $(PKG_NAME)\nVersion: $(VERSION)\nSection: net\nPriority: optional\nArchitecture: $(ARCH)\nDepends: libc6 (>= 2.17)\nMaintainer: Emiel <pdtweepd@gmail.com>\nDescription: Auto Mute plugin for TeamSpeak 3 Client on Linux (API v26)\n Automatically mutes microphone and/or speakers upon connecting to a\n TeamSpeak 3 server. Run 'ts3-automute-install' to enable for your user\n profile (supports both native and Flatpak TeamSpeak 3 installations).\n" > $(DEB_ROOT)/DEBIAN/control
	@dpkg-deb --root-owner-group --build $(DEB_ROOT) $(BUILD_DIR)/$(PKG_NAME)_$(VERSION)_$(ARCH).deb >/dev/null

install: $(BUILD_DIR)/$(PLUGIN_NAME)
	./install.sh

clean:
	rm -rf $(BUILD_DIR)
