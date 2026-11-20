#!/usr/bin/env python3
"""
CeeBase Studio: Native Desktop Application (.exe)
Embeds the CeeBase engine and web studio into a dedicated standalone Windows window.
"""

import os
import socketserver
import sys
import threading
import time
from pathlib import Path
import webview

# Add paths for studio and ceebase bindings
REPO_ROOT = Path(__file__).resolve().parent.parent.parent
STUDIO_DIR = REPO_ROOT / "tools" / "studio"
BINDINGS_DIR = REPO_ROOT / "bindings" / "python"

sys.path.insert(0, str(STUDIO_DIR))
sys.path.insert(0, str(BINDINGS_DIR))

from src.platform.gateway import GatewayHandler, PlatformManager
import server

def run_embedded_server():
    backend = server.StudioBackend("ceebase.cbdb")
    platform = PlatformManager()
    GatewayHandler.platform = platform
    server.StudioHandler.backend = backend
    server.StudioHandler.platform = platform
    port = 8080
    for attempt in range(10):
        try:
            with socketserver.TCPServer(("", port), server.StudioHandler) as httpd:
                httpd.serve_forever()
                break
        except OSError:
            port += 1

def main():
    # Start local backend in background thread
    t = threading.Thread(target=run_embedded_server, daemon=True)
    t.start()
    time.sleep(0.5)

    # Open standalone desktop window
    window = webview.create_window(
        title="CeeBase Studio - Desktop Edition",
        url="http://localhost:8080",
        width=1280,
        height=820,
        min_size=(900, 600)
    )
    webview.start()

if __name__ == "__main__":
    main()
