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
  TARBALL="ceebase-$OS-$ARCH.tar.gz"
  DOWNLOAD_URL="https://github.com/$REPO/releases/latest/download/$TARBALL"
  echo "[+] Downloading $TARBALL from GitHub Releases..."
  
  if curl -fSL -o "/tmp/$TARBALL" "$DOWNLOAD_URL"; then
    tar -xzf "/tmp/$TARBALL" -C "$INSTALL_DIR"
    chmod +x "$EXE_PATH"
    chmod +x "$INSTALL_DIR"/libceebase.* 2>/dev/null || true
    xattr -d com.apple.quarantine "$INSTALL_DIR"/* 2>/dev/null || true
    rm -f "/tmp/$TARBALL"
    INSTALLED=true
    echo "[+] Downloaded and extracted to $INSTALL_DIR"
  else
    echo "[-] Release asset download failed. Checking cargo fallback..."
  fi
fi

# 3. Cargo fallback
if [ "$INSTALLED" = false ] && command -v cargo >/dev/null 2>&1; then
  echo "[+] Compiling via cargo..."
  if [ -d "tools/cli" ]; then
    cargo install --path tools/cli --root "$HOME/.ceebase"
  else
    cargo install --git "https://github.com/$REPO.git" --root "$HOME/.ceebase"
  fi
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
  fi
fi

# 5. Link into global standard PATH (/usr/local/bin or /opt/homebrew/bin)
# This makes 'ceebase' immediately executable in the current terminal without requiring 'source ~/.zshrc'
LINKED=false
for bin_dir in "/usr/local/bin" "/opt/homebrew/bin"; do
  if [ -d "$bin_dir" ] && [ -w "$bin_dir" ]; then
    ln -sf "$EXE_PATH" "$bin_dir/ceebase"
    ln -sf "$INSTALL_DIR"/libceebase.* "$bin_dir/" 2>/dev/null || true
    LINKED=true
    break
  fi
done

if [ "$LINKED" = false ]; then
  if sudo -n true 2>/dev/null; then
    sudo ln -sf "$EXE_PATH" /usr/local/bin/ceebase 2>/dev/null && LINKED=true
    sudo ln -sf "$INSTALL_DIR"/libceebase.* /usr/local/bin/ 2>/dev/null || true
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
echo "Launch CeeBase anytime with:"
echo "  ceebase"
echo ""
