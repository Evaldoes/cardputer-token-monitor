import json
import os
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / "hooks"))
sys.path.insert(0, str(Path(__file__).parent.parent / "scripts"))
import agent_hook
import install_hooks


def test_record_e_session_end(tmp_path, monkeypatch):
    monkeypatch.setattr(agent_hook, "STATE", tmp_path)
    agent_hook.record({"session_id": "a", "hook_event_name": "PreToolUse", "cwd": "/x/p", "tool_name": "Bash"})
    f = tmp_path / "sessions" / "a.json"
    assert json.loads(f.read_text())["state"] == "working"
    agent_hook.record({"session_id": "a", "hook_event_name": "SessionEnd"})
    assert not f.exists()


def test_session_id_nao_escapa_da_pasta(tmp_path, monkeypatch):
    monkeypatch.setattr(agent_hook, "STATE", tmp_path)
    agent_hook.record({"session_id": "../../evil", "hook_event_name": "Stop"})
    assert not (tmp_path.parent / "evil.json").exists()


def test_sem_bridge_nao_decide(tmp_path, monkeypatch):
    monkeypatch.setattr(agent_hook, "STATE", tmp_path)
    assert agent_hook.wait_decision({"tool_name": "Bash"}, timeout=1) is None


def test_sem_resposta_nao_aprova(tmp_path, monkeypatch):
    monkeypatch.setattr(agent_hook, "STATE", tmp_path)
    (tmp_path / "bridge.alive").touch()
    assert agent_hook.wait_decision({"tool_name": "Bash"}, timeout=0.5) is None
    assert not list((tmp_path / "pending").glob("*"))


def test_resposta_do_aparelho(tmp_path, monkeypatch):
    monkeypatch.setattr(agent_hook, "STATE", tmp_path)
    (tmp_path / "bridge.alive").touch()

    def responder():
        while not list((tmp_path / "pending").glob("*.json")):
            time.sleep(0.02)
        rid = json.loads(next((tmp_path / "pending").glob("*.json")).read_text())["id"]
        (tmp_path / "responses").mkdir()
        (tmp_path / "responses" / rid).write_text("y")

    threading.Thread(target=responder).start()
    assert agent_hook.wait_decision({"tool_name": "Bash", "tool_input": {"command": "ls"}}, timeout=3) == "y"


def test_saida_da_decisao():
    assert json.loads(agent_hook.decision_output("y"))["hookSpecificOutput"]["decision"]["behavior"] == "allow"
    assert json.loads(agent_hook.decision_output("n"))["hookSpecificOutput"]["decision"]["behavior"] == "deny"


def test_instalador_preserva_hooks_alheios_e_remove_so_os_seus():
    alheio = {"matcher": "Bash", "hooks": [{"type": "command", "command": "outro"}]}
    s = {"model": "x", "hooks": {"PreToolUse": [alheio]}}
    s = install_hooks.add(install_hooks.strip(s), approve=False)
    assert len(s["hooks"]["PreToolUse"]) == 2
    assert "--approve" not in json.dumps(s)
    s = install_hooks.add(install_hooks.strip(s), approve=True)
    assert len(s["hooks"]["PreToolUse"]) == 2 and "--approve" in json.dumps(s)
    s = install_hooks.strip(s)
    assert s == {"model": "x", "hooks": {"PreToolUse": [alheio]}}
