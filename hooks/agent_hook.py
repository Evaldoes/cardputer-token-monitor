#!/usr/bin/env python3
"""Hook do Claude Code: registra o estado da sessão e, com --approve, pede decisão ao aparelho."""
import json
import os
import re
import sys
import time
import uuid
from pathlib import Path

STATE = Path(os.environ.get("CARDPUTER_STATE") or Path.home() / ".cache" / "cardputer-token-monitor")
APPROVE_SECONDS = 25
BRIDGE_FRESH_SECONDS = 15


def state_for(event, data):
    if event in ("UserPromptSubmit", "PreToolUse", "PostToolUse"):
        return "working"
    if event in ("Stop", "SessionStart"):
        return "idle"
    if event == "PermissionRequest":
        return "waiting"
    if event == "Notification":
        kind = data.get("notification_type")
        return {"permission_prompt": "waiting", "idle_prompt": "idle"}.get(kind)
    return None


def record(data):
    sid = re.sub(r"[^\w-]", "", data.get("session_id", ""))
    if not sid:
        return
    path = STATE / "sessions" / f"{sid}.json"
    event = data.get("hook_event_name", "")
    if event == "SessionEnd":
        path.unlink(missing_ok=True)
        return
    state = state_for(event, data)
    if state is None:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(f".{os.getpid()}.tmp")
    tmp.write_text(json.dumps({"state": state, "cwd": data.get("cwd", ""), "tool": data.get("tool_name", "")}))
    tmp.replace(path)


def bridge_running():
    try:
        return time.time() - (STATE / "bridge.alive").stat().st_mtime < BRIDGE_FRESH_SECONDS
    except OSError:
        return False


def describe(tool_input):
    if isinstance(tool_input, dict):
        for key in ("command", "file_path", "url", "pattern"):
            if tool_input.get(key):
                return str(tool_input[key])
    return ""


def wait_decision(data, timeout=APPROVE_SECONDS):
    """Devolve "y", "n" ou None. Sem resposta, o terminal decide: nunca aprova por omissão."""
    if not bridge_running():
        return None
    rid = uuid.uuid4().hex
    pending = STATE / "pending" / f"{rid}.json"
    response = STATE / "responses" / rid
    pending.parent.mkdir(parents=True, exist_ok=True)
    tmp = pending.with_suffix(".tmp")
    tmp.write_text(json.dumps({"id": rid, "tool": data.get("tool_name", ""),
                               "detail": describe(data.get("tool_input")), "expires": time.time() + timeout}))
    tmp.replace(pending)
    try:
        deadline = time.time() + timeout
        while time.time() < deadline:
            if response.exists():
                value = response.read_text().strip()
                return value if value in ("y", "n") else None
            time.sleep(0.25)
        return None
    finally:
        pending.unlink(missing_ok=True)
        response.unlink(missing_ok=True)


def decision_output(value):
    if value == "y":
        decision = {"behavior": "allow"}
    else:
        decision = {"behavior": "deny", "message": "Negado no Cardputer."}
    return json.dumps({"hookSpecificOutput": {"hookEventName": "PermissionRequest", "decision": decision}})


def main():
    try:
        data = json.load(sys.stdin)
        record(data)
        if "--approve" in sys.argv and data.get("hook_event_name") == "PermissionRequest":
            value = wait_decision(data)
            if value:
                print(decision_output(value))
    except Exception as exc:  # um erro aqui jamais pode atrapalhar o Claude Code
        print(f"cardputer hook: {exc}", file=sys.stderr)


if __name__ == "__main__":
    main()
