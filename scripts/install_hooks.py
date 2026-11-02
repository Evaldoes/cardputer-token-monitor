#!/usr/bin/env python3
"""Adiciona ou remove os hooks do monitor em ~/.claude/settings.json, com backup."""
import argparse
import json
import shutil
import sys
import time
from pathlib import Path

HOOK = Path(__file__).resolve().parent.parent / "hooks" / "agent_hook.py"
SETTINGS = Path.home() / ".claude" / "settings.json"
STATUS_EVENTS = ["SessionStart", "SessionEnd", "UserPromptSubmit", "PreToolUse", "PostToolUse", "PostToolUseFailure", "Notification", "Stop"]


def strip(settings):
    hooks = settings.get("hooks", {})
    for event in list(hooks):
        kept = [g for g in hooks[event]
                if not any(str(HOOK) in h.get("command", "") for h in g.get("hooks", []))]
        if kept:
            hooks[event] = kept
        else:
            del hooks[event]
    if not hooks:
        settings.pop("hooks", None)
    return settings


def add(settings, approve, python=sys.executable):
    hooks = settings.setdefault("hooks", {})
    command = f'"{python}" "{HOOK}"'
    for event in STATUS_EVENTS:
        hooks.setdefault(event, []).append(
            {"hooks": [{"type": "command", "command": command, "timeout": 5}]})
    perm = command + " --approve" if approve else command
    hooks.setdefault("PermissionRequest", []).append(
        {"hooks": [{"type": "command", "command": perm, "timeout": 30 if approve else 5}]})
    return settings


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--approve", action="store_true", help="permite aprovar pedidos de permissão pelo aparelho")
    ap.add_argument("--remove", action="store_true")
    args = ap.parse_args()
    settings = json.loads(SETTINGS.read_text()) if SETTINGS.exists() else {}
    if SETTINGS.exists():
        shutil.copy2(SETTINGS, SETTINGS.with_name(f"settings.json.bak-{int(time.time())}"))
    settings = strip(settings)
    if not args.remove:
        settings = add(settings, args.approve)
    SETTINGS.write_text(json.dumps(settings, indent=2, ensure_ascii=False) + "\n")
    print("Hooks removidos." if args.remove else f"Hooks instalados (aprovação: {'sim' if args.approve else 'não'}).")


if __name__ == "__main__":
    main()
