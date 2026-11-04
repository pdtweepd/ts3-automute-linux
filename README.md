# TeamSpeak 3 Auto Mute Plugin for Linux (Debian / Ubuntu / Flatpak)

A lightweight, native Linux port (`libauto_mute_linux_amd64.so`) of the **Auto Mute** plugin for the **TeamSpeak 3 Client** (Plugin API v26).

Automatically mutes your microphone and/or speakers as soon as you connect to a TeamSpeak 3 server—either globally across all servers or only on specific servers in your allowlist.

## Features
- **Zero external runtime dependencies**: Written in pure C (`libc6` only), avoiding C++ stdlib (`libc++` vs `libstdc++`) and Qt5 ABI conflicts across Debian, Ubuntu, Mint, Pop!_OS, and Flatpak runtimes.
- **Supports both Native & Flatpak TeamSpeak 3**:
  - Native Linux client: `~/.ts3client/plugins/`
  - Flatpak (`com.teamspeak.TeamSpeak3`): `~/.var/app/com.teamspeak.TeamSpeak3/.ts3client/plugins/`
- **In-Client Menu Controls**: Toggle settings directly from **Plugins** $\rightarrow$ **Auto Mute** inside TeamSpeak 3:
  1. `Toggle: Mute on All Servers`
  2. `Toggle: Mute Microphone on Join`
  3. `Toggle: Mute Speakers on Join`
  4. `Toggle: Current Server in AutoMute List`
  5. `Mute Microphone & Speakers Now`
- **Compatible `AutoMute.ini` configuration**: Reads and writes `AutoMute.ini` in your TeamSpeak 3 configuration folder.

---

## Installation on Debian / Ubuntu / Linux

### Option 1: Quick Installer Script (Recommended)
Clone the repository and run the installer script (auto-detects both Native and Flatpak TeamSpeak 3):

```bash
git clone https://github.com/<your-username>/ts3-automute-linux.git
cd ts3-automute-linux
bash install.sh
```

### Option 2: Debian / Ubuntu `.deb` Package
Build and install the `.deb` package:

```bash
sudo apt update && sudo apt install -y build-essential zip dpkg-dev
make package-deb
sudo apt install ./build/ts3-plugin-automute_1.2.0_amd64.deb
ts3-automute-install
```

### Option 3: `.ts3_plugin` Package
Build `build/automute_1.2.0_linux_amd64.ts3_plugin` and extract it to your TeamSpeak 3 client directory:

```bash
make package-ts3
unzip -o build/automute_1.2.0_linux_amd64.ts3_plugin "plugins/*" -d ~/.ts3client/
```

---

## Building & Running Tests

```bash
make all
make test
```

## Configuration (`AutoMute.ini`)
Located in `~/.ts3client/AutoMute.ini` (or `~/.var/app/com.teamspeak.TeamSpeak3/.ts3client/AutoMute.ini`):

```ini
[General]
always_mute=true
mute_mic=true
mute_speaker=true
server_list=My Gaming Server;Another Server
```

## Credits & License
- Original Windows AutoMute plugin concept by **CubE (CubE135)**.
- Native Linux port, test suite, and Debian packaging by **Emiel** (`pdtweepd@gmail.com`).
- Licensed under the [MIT License](LICENSE).
