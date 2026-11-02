#!/bin/sh
# Inicia o envio de dados no login, via systemd do usuário (Linux).
set -e
repo=$(cd "$(dirname "$0")/.." && pwd)
uv=$(command -v uv)
unit="$HOME/.config/systemd/user/cardputer-token-monitor.service"
mkdir -p "$(dirname "$unit")"
cat > "$unit" <<UNIT
[Unit]
Description=Cardputer token monitor

[Service]
ExecStart=$uv run --with pyserial $repo/bridge/token_bridge.py
Restart=always
RestartSec=5

[Install]
WantedBy=default.target
UNIT
systemctl --user daemon-reload
systemctl --user enable --now cardputer-token-monitor.service
systemctl --user is-active cardputer-token-monitor.service
