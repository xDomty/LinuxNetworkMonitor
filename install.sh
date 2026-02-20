#!/bin/bash

APP_NAME="netmon"
SOURCE_FILE="netmon.cpp"
LOCAL_BIN="$HOME/.local/bin"
SYSTEMD_USER_DIR="$HOME/.config/systemd/user"

echo "--- Starting Universal Installation for $APP_NAME ---"

# 1. Dependency Check
if ! command -v g++ &> /dev/null; then
    echo "ERROR: g++ not found."
    exit 1
fi

# 2. Setup Directories
mkdir -p "$LOCAL_BIN"
mkdir -p "$HOME/.cache/NetworkUsage"

# 3. Compilation
echo "Compiling $SOURCE_FILE..."
g++ -O3 -std=c++17 "$SOURCE_FILE" -o "$APP_NAME" -lstdc++fs
if [ $? -eq 0 ]; then
    mv "$APP_NAME" "$LOCAL_BIN/$APP_NAME"
    chmod +x "$LOCAL_BIN/$APP_NAME"
    echo "Binary installed to $LOCAL_BIN/$APP_NAME"
else
    echo "ERROR: Compilation failed."
    exit 1
fi
