#!/usr/bin/env python3
"""Hook do Claude Code: registra o estado da sessão e, com --approve, pede decisão ao aparelho."""
import json
import os
import re
import sys
import time
import uuid
from pathlib import Path

STATE = Path.home() / ".cache" / "cardputer-token-monitor"
APPROVE_SECONDS = 25
BRIDGE_FRESH_SECONDS = 15
DETAIL_MAX = 60
DIR_MAX = 18
BASH_KEYS = {"command", "description", "timeout"}


def private_dir(path):
    path.mkdir(parents=True, exist_ok=True, mode=0o700)
    STATE.chmod(0o700)
    path.chmod(0o700)


def write_private(path, text):
    """Troca o arquivo de uma vez, legível só pelo dono."""
    tmp = path.with_name(f"{path.name}.{os.getpid()}.tmp")
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        f.write(text)
    tmp.replace(path)


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
    private_dir(path.parent)
    write_private(path, json.dumps({"state": state, "cwd": data.get("cwd", ""), "tool": data.get("tool_name", "")}))


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


def fully_visible(data):
    """Só um Bash curto, sem opção extra e com a pasta legível, cabe inteiro na tela."""
    tool_input = data.get("tool_input")
    if data.get("tool_name") != "Bash" or not isinstance(tool_input, dict) or set(tool_input) - BASH_KEYS:
        return False
    detail = describe(tool_input)
    folder = Path(data.get("cwd") or "").name
    return (0 < len(detail) <= DETAIL_MAX and detail.isprintable() and detail.isascii()
            and 0 < len(folder) <= DIR_MAX and folder.isprintable() and folder.isascii())


def wait_decision(data, timeout=APPROVE_SECONDS):
    """Devolve "y", "n" ou None. Sem resposta, o terminal decide: nunca aprova por omissão."""
    if not bridge_running():
        return None
    rid = uuid.uuid4().hex
    pending = STATE / "pending" / f"{rid}.json"
    response = STATE / "responses" / rid
    private_dir(pending.parent)
    tool = data.get("tool_name", "")
    detail = describe(data.get("tool_input"))
    write_private(pending, json.dumps({"id": rid, "tool": tool, "detail": detail[:DETAIL_MAX],
                                       "can_allow": fully_visible(data),
                                       "dir": Path(data.get("cwd") or "").name[:DIR_MAX],
                                       "expires": time.time() + timeout}))
    try:
        deadline = time.time() + timeout
        while time.time() < deadline:
            if response.exists():
                value = response.read_text().strip()
                if value == "y" and not fully_visible(data):
                    return None
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
