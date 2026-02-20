# Linux Network Monitor (netmon)

A lightweight, C++17 daemon for Linux that monitors network interface traffic via `/proc/net/dev`. It categorizes interfaces into Physical and Virtual, logging daily consumption (Upload, Download, Total) in Megabytes.

## Features

- **Non-Root**: Designed to run entirely within user-space.
- **Persistence**: Survives reboots (when configured).
- **Categorization**: Automatically separates physical hardware (WiFi/Ethernet) from virtual interfaces (VPNs, Docker, Loopback).
- **Daily Rotation**: Logs data organized by date.

## Installation

### Build from Source

The provided `install.sh` script handles compilation and places the binary in your local user path.

```bash
chmod +x install.sh
./install.sh
```

**What the script does:**

1. Verifies `g++` is installed.
2. Compiles `netmon.cpp` with C++17 standards.
3. Creates `~/NetworkUsage` for data storage.
4. Installs the binary to `~/.local/bin/netmon`.

## Service Setup (Manual)

To ensure the monitor runs in the background and starts on boot without using `sudo`, choose the method that matches your init system.

### Option A: Systemd (Ubuntu, Arch, Fedora, Debian)

Systemd provides a "User Bus" specifically for background tasks like this.

1. **Create the unit file:**

   ```bash
   mkdir -p ~/.config/systemd/user
   nano ~/.config/systemd/user/netmon.service
   ```

2. **Paste this configuration:**

   ```ini
   [Unit]
   Description=Network Usage Monitor
   After=network.target

   [Service]
   ExecStart=%h/.local/bin/netmon
   Restart=always
   RestartSec=10

   [Install]
   WantedBy=default.target
   ```

3. **Enable and Start:**

   ```bash
   systemctl --user daemon-reload
   systemctl --user enable netmon
   systemctl --user start netmon
   ```

### Option B: OpenRC (Gentoo, Alpine, Artix)

Since OpenRC's `/etc/init.d/` is restricted to root, the standard user-level approach is using Cron.

1. **Open your crontab:**

   ```bash
   crontab -e
   ```
   -> if the command not found install `cronie` and then
   ```
   sudo mkdir -p /var/spool/cron/crontabs
   ```

2. **Add the reboot trigger:**

   ```
   @reboot /home/YOUR_USERNAME/.local/bin/netmon
   ```

   **Note:** Replace `YOUR_USERNAME` with your actual Linux login name.

### Option C: Runit (Void Linux) or Generic Fallback

If you don't use Cron or Systemd, you can trigger the monitor when you log into your shell.

1. **Edit your shell profile:**

   ```bash
   nano ~/.bashrc
   ```

   or

   ```bash
   nano ~/.zshrc
   ```

2. **Add the auto-launch line:**

   ```bash
   # Start netmon if it isn't already running
   [[ ! $(pgrep -x "netmon") ]] && ~/.local/bin/netmon &
   ```

## Usage and Monitoring

### Data Location

Usage data is stored as plain text files:

- **Physical Devices:** `~/.cache/NetworkUsage/PhysicalInterfaces/`
- **Virtual Devices:** `~/.cache/NetworkUsage/VirtualInterfaces/`

### Commands

- **Check if running:**

  ```bash
  pgrep -fl netmon
  ```

- **View live logs:**

  ```bash
  tail -f ~/.cache/NetworkUsage/PhysicalInterfaces/TotalPhysicalUsage
  ```

- **Stop the monitor:**

  ```bash
  pkill netmon
  ```
