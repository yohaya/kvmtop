#!/bin/bash

# Ensure running as root
if [ "$EUID" -ne 0 ]; then
  echo "Please run as root"
  exit 1
fi

SOURCE="kvmtop"
DEST="/usr/bin/kvmtop"
BACKUP="/usr/bin/kvmtop.old"

# Check if source binary exists
if [ ! -f "$SOURCE" ]; then
    echo "Error: '$SOURCE' binary not found in current directory."
    echo "Please build or download it first."
    exit 1
fi

# Backup existing binary
if [ -f "$DEST" ]; then
    echo "Backing up existing $DEST to $BACKUP..."
    mv "$DEST" "$BACKUP"
fi

# Install new binary
echo "Installing $SOURCE to $DEST..."
cp "$SOURCE" "$DEST"
chmod +x "$DEST"

echo "Installation complete. You can now run 'kvmtop'."
