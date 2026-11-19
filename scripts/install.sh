#!/usr/bin/env bash
# CeeBase CLI Installer for Linux and macOS
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/MackenzieCampbelAndross/CeeBase/main/scripts/install.sh | bash

set -e

REPO="MackenzieCampbelAndross/CeeBase"
INSTALL_DIR="$HOME/.ceebase/bin"
EXE_PATH="$INSTALL_DIR/ceebase"

echo "========================================================"
echo "  CeeBase CLI Installer (Linux / macOS)"
echo "========================================================"
echo ""

mkdir -p "$INSTALL_DIR"

OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"

case "$ARCH" in
  x86_64|amd64) ARCH="x64" ;;
  aarch64|arm64) ARCH="arm64" ;;
  *) echo "Unsupported architecture: $ARCH"; exit 1 ;;
esac

echo "[+] Detecting platform: $OS-$ARCH"

INSTALLED=false

# 1. Local build check
if [ -f "tools/cli/target/release/ceebase" ]; then
  echo "[+] Found local build: tools/cli/target/release/ceebase"
  cp "tools/cli/target/release/ceebase" "$EXE_PATH"
  chmod +x "$EXE_PATH"
  INSTALLED=true
elif [ -f "build/ceebase" ]; then
  echo "[+] Found local build: build/ceebase"
  cp "build/ceebase" "$EXE_PATH"
  chmod +x "$EXE_PATH"
  INSTALLED=true
fi

# 2. Download from GitHub Release
if [ "$INSTALLED" = false ]; then
  echo "[+] Fetching latest release from GitHub..."
  TARBALL="ceebase-$OS-$ARCH.tar.gz"
  DOWNLOAD_URL="https://github.com/$REPO/releases/latest/download/$TARBALL"
  
  if curl -fSL -o "/tmp/$TARBALL" "$DOWNLOAD_URL" 2>/dev/null; then
    tar -xzf "/tmp/$TARBALL" -C "$INSTALL_DIR"
    chmod +x "$EXE_PATH"
    rm -f "/tmp/$TARBALL"
    INSTALLED=true
  fi
fi

# 3. Cargo fallback
if [ "$INSTALLED" = false ] && command -v cargo >/dev/null 2>&1; then
  echo "[+] Compiling via cargo..."
  cargo install --path tools/cli --root "$HOME/.ceebase"
  INSTALLED=true
fi

if [ ! -f "$EXE_PATH" ]; then
  echo "[-] Could not find or build ceebase binary."
  exit 1
fi

# 4. Add to Shell Profile
SHELL_PROFILE=""
if [ -n "$ZSH_VERSION" ] || [ -f "$HOME/.zshrc" ]; then
  SHELL_PROFILE="$HOME/.zshrc"
elif [ -f "$HOME/.bashrc" ]; then
  SHELL_PROFILE="$HOME/.bashrc"
elif [ -f "$HOME/.profile" ]; then
  SHELL_PROFILE="$HOME/.profile"
fi

if [ -n "$SHELL_PROFILE" ]; then
  if ! grep -q ".ceebase/bin" "$SHELL_PROFILE" 2>/dev/null; then
    echo "export PATH=\"\$HOME/.ceebase/bin:\$PATH\"" >> "$SHELL_PROFILE"
    echo "[+] Added ~/.ceebase/bin to $SHELL_PROFILE"
  fi
fi

export PATH="$INSTALL_DIR:$PATH"

echo ""
echo "========================================================"
echo "  CeeBase CLI is successfully installed!"
echo "========================================================"
echo ""
"$EXE_PATH" --version
echo ""
echo "To start CeeBase, open any terminal and type:"
echo "  ceebase"
echo ""
