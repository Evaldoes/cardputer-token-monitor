#!/usr/bin/env python3
"""Lê os registros locais do Claude Code e envia um resumo ao Cardputer."""
import argparse
import getpass
import json
import re
import socket
import subprocess
import time
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from pathlib import Path

PROJECTS = Path.home() / ".claude" / "projects"
STATE = Path.home() / ".cache" / "cardputer-token-monitor"
CONFIG = Path.home() / ".config" / "cardputer-token-monitor.json"
RETENTION = timedelta(days=400)
BLOCK = timedelta(hours=5)
ESPRESSIF_VID = 0x303A
UDP_PORT = 47474
DEFAULTS = {"goal_daily_tokens": 0, "window_warn_minutes": 30, "commands": {}, "wifi_broadcast": False, "device_serial": ""}

# US$ por MTok: entrada, saída, leitura de cache. Escrita: 1,25x (5 min) ou 2x (1 h) a entrada.
PRICES = {
    "claude-fable-5-1": (10, 50, 0.25), "claude-fable-5": (10, 50, 1),
    "claude-opus-5-5": (4, 20, 0.20), "claude-opus-5": (5, 25, 0.5),
    "claude-opus-4-8": (5, 25, 0.5), "claude-opus-4-7": (5, 25, 0.5), "claude-opus-4-6": (5, 25, 0.5),
    "claude-sonnet-5-5": (2, 10, 0.20), "claude-sonnet-5": (2, 10, 0.2), "claude-sonnet-4-6": (3, 15, 0.3),
    "claude-haiku-5-5": (0.1, 0.5, 0.01), "claude-haiku-4-5": (1, 5, 0.1),
}


@dataclass
class Entry:
    ts: datetime
    model: str
    session: str
    tokens: int  # entrada + saída + criação de cache
    cache_read: int
    cost: float = 0.0
    project: str = ""


def usage_cost(model, usage):
    """Custo equivalente na API; 0 para modelo sem preço conhecido."""
    price = PRICES.get(model)
    if not price:
        return 0.0
    inp, out, read = price
    created = usage.get("cache_creation_input_tokens") or 0
    split = usage.get("cache_creation") or {}
    w1h = split.get("ephemeral_1h_input_tokens") or 0
    cost = ((usage.get("input_tokens") or 0) * inp + (usage.get("output_tokens") or 0) * out
            + (usage.get("cache_read_input_tokens") or 0) * read
            + w1h * inp * 2 + max(created - w1h, 0) * inp * 1.25) / 1e6
    return cost * (2 if usage.get("speed") == "fast" else 1)


def parse_line(line):
    """Devolve (chave, Entry) de uma linha com uso, ou None."""
    try:
        d = json.loads(line)
        msg = d["message"]
        usage = msg["usage"]
        ts = datetime.fromisoformat(d["timestamp"].replace("Z", "+00:00"))
    except (ValueError, KeyError, TypeError, AttributeError):
        return None
    if msg.get("model") in (None, "<synthetic>"):
        return None
    tokens = sum(usage.get(k) or 0 for k in
                 ("input_tokens", "output_tokens", "cache_creation_input_tokens"))
    # Uma resposta ocupa várias linhas no registro, com o mesmo id: a chave evita contá-la de novo.
    key = (msg.get("id"), d.get("requestId")) if msg.get("id") else d.get("uuid")
    project = Path(d.get("cwd") or "").name
    return key, Entry(ts, msg["model"], d.get("sessionId", ""), tokens,
                      usage.get("cache_read_input_tokens") or 0,
                      usage_cost(msg["model"], usage), project)


def model_label(model):
    """claude-opus-5-5 -> Opus 5.5"""
    parts = model.split("-")
    if len(parts) >= 4 and parts[0] == "claude":
        return f"{parts[1].capitalize()} {parts[2]}.{parts[3]}"
    return model[:12]


def block_start(entries, now):
    """Início da janela de 5h aberta (estimativa), ou None."""
    start = None
    for ts in sorted(e.ts for e in entries):
        if start is None or ts >= start + BLOCK:
            start = ts.replace(minute=0, second=0, microsecond=0)
    return start if start is not None and now < start + BLOCK else None


def block_remaining(entries, now):
    """Minutos restantes da janela de 5h atual (estimativa); 0 se não houver janela aberta."""
    start = block_start(entries, now)
    return int((start + BLOCK - now).total_seconds() // 60) if start else 0


def records(entries, now):
    """Sequência de dias com uso, recorde diário e níveis 0-4 dos últimos 28 dias."""
    days = {}
    for e in entries:
        day = e.ts.astimezone(now.tzinfo).date()
        days[day] = days.get(day, 0) + e.tokens
    today = now.date()
    # Um dia ainda sem uso não quebra a sequência antes de terminar.
    day = today if days.get(today) else today - timedelta(days=1)
    streak = 0
    while days.get(day):
        streak += 1
        day -= timedelta(days=1)
    top = max(days.values(), default=0)
    level = lambda v: 0 if not v or not top else min(4, 1 + v * 4 // (top + 1))
    return {"st": streak, "rec": top,
            "hm": [level(days.get(today - timedelta(days=27 - i), 0)) for i in range(28)]}


def read_sessions(state_dir=STATE, now_ts=None):
    """Sessões registradas pelos hooks. Estado antigo vira parado: a sessão pode ter morrido sem avisar."""
    now_ts = now_ts or time.time()
    out = []
    for path in (state_dir / "sessions").glob("*.json"):
        try:
            age = now_ts - path.stat().st_mtime
            s = json.loads(path.read_text())
        except (OSError, ValueError):
            continue
        if age > 24 * 3600:
            continue
        state = s.get("state", "idle")
        if state == "working" and age > 600:
            state = "idle"
        out.append({"n": Path(s.get("cwd") or "?").name[:14], "st": state, "tool": s.get("tool", "")[:14]})
    order = {"waiting": 0, "working": 1, "idle": 2}
    return sorted(out, key=lambda s: order.get(s["st"], 3))


def read_pending(state_dir=STATE, now_ts=None):
    """Pedido de aprovação mais antigo ainda dentro do prazo."""
    now_ts = now_ts or time.time()
    found = []
    for path in (state_dir / "pending").glob("*.json"):
        try:
            p = json.loads(path.read_text())
            if p.get("expires", 0) > now_ts:
                found.append(p)
            else:
                path.unlink()
        except (OSError, ValueError):
            continue
    if not found:
        return None
    p = min(found, key=lambda x: x["expires"])
    return {"id": p["id"], "tool": p.get("tool", "")[:16], "d": p.get("detail", "")[:60],
            "ok": bool(p.get("can_allow")), "dir": p.get("dir", "")[:18]}


def summarize(entries, now, sessions=(), pending=None, config=None):
    """now precisa ter fuso: define o que é "hoje" e as horas do gráfico."""
    config = {**DEFAULTS, **(config or {})}
    entries = sorted(entries, key=lambda e: e.ts)
    day_start = now.replace(hour=0, minute=0, second=0, microsecond=0)
    week_start = now - timedelta(days=7)
    hour_end = now.replace(minute=0, second=0, microsecond=0) + timedelta(hours=1)
    hourly = [0] * 12
    today = [e for e in entries if e.ts >= day_start]
    week = [e for e in entries if e.ts >= week_start]
    last = entries[-1] if entries else None
    session = [e for e in entries if last and e.session == last.session]
    for e in entries:
        hours_ago = (hour_end - e.ts) // timedelta(hours=1)
        if 0 <= hours_ago < 12:
            hourly[11 - hours_ago] += e.tokens
    recent = sum(e.tokens for e in entries if now - e.ts <= timedelta(minutes=5))
    hour = sum(e.tokens for e in entries if now - e.ts <= timedelta(minutes=60))
    day_total = sum(e.tokens for e in today)
    # Projeta só as próximas 2h no ritmo da última hora: mais que isso seria chute.
    left = (day_start + timedelta(days=1) - now).total_seconds() / 60
    forecast = day_total + int(hour / 60 * min(left, 120))
    remaining = block_remaining(entries, now)
    projects = {}
    for e in week:
        if e.project:
            projects[e.project] = projects.get(e.project, 0) + e.tokens
    top = sorted(projects.items(), key=lambda kv: -kv[1])[:4]
    goal = config["goal_daily_tokens"]
    alert = ""
    if goal and day_total >= goal:
        alert = "meta"
    elif remaining and remaining <= config["window_warn_minutes"]:
        alert = "janela"
    states = [s["st"] for s in sessions]
    agent = next((a for a in ("waiting", "working", "idle") if a in states), "off")
    return {
        "t": now.strftime("%H:%M"), "ts": int(now.timestamp()), "dt": now.strftime("%Y-%m-%d"),
        "d": day_total, "dc": sum(e.cache_read for e in today),
        "w": sum(e.tokens for e in week), "wc": sum(e.cache_read for e in week),
        "s": sum(e.tokens for e in session), "sc": sum(e.cache_read for e in session),
        "cd": round(sum(e.cost for e in today), 2), "cw": round(sum(e.cost for e in week), 2),
        "cs": round(sum(e.cost for e in session), 2),
        "r": recent // 5, "f": forecast, "goal": goal,
        "m": model_label(last.model) if last else "-",
        "b": remaining, "h": hourly, "a": agent, "al": alert,
        "ss": [{"n": s["n"], "st": s["st"], "tool": s["tool"]} for s in sessions[:4]],
        "pr": [{"n": k[:14], "v": v} for k, v in top],
        "p": pending,
        **records(entries, now),
    }


def public_view(summary):
    """O que pode circular na rede local: só totais, nunca nomes nem pedidos de permissão."""
    return {k: v for k, v in summary.items() if k not in ("ss", "pr", "p", "m")}


class Reader:
    """Lê só o que foi acrescentado a cada arquivo desde a última passagem."""

    def __init__(self, root=PROJECTS):
        self.root = root
        self.offsets = {}
        self.entries = {}

    def scan(self, now):
        cutoff = (now - RETENTION).timestamp()
        for path in self.root.rglob("*.jsonl"):
            try:
                st = path.stat()
                if st.st_mtime < cutoff or self.offsets.get(path) == st.st_size:
                    continue
                with path.open("rb") as f:
                    f.seek(self.offsets.get(path, 0))
                    data = f.read()
            except OSError:
                continue
            # Linha ainda incompleta fica para a próxima passagem.
            end = data.rfind(b"\n") + 1
            self.offsets[path] = self.offsets.get(path, 0) + end
            for line in data[:end].splitlines():
                parsed = parse_line(line)
                if parsed:
                    self.entries[parsed[0]] = parsed[1]
        limit = now - RETENTION
        self.entries = {k: e for k, e in self.entries.items() if e.ts >= limit}
        return list(self.entries.values())


def load_config(path=CONFIG):
    try:
        return {**DEFAULTS, **json.loads(path.read_text())}
    except (OSError, ValueError):
        return dict(DEFAULTS)


def save_config(config, path=CONFIG):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(".tmp")
    tmp.write_text(json.dumps(config, indent=2) + "\n")
    tmp.replace(path)


def handle_device_line(line, config, state_dir=STATE, run=subprocess.Popen, trusted=False, config_path=CONFIG):
    """Trata uma linha vinda do aparelho. Devolve "pause" se ele pediu para alternar a pausa."""
    parts = line.strip().split()
    if trusted and len(parts) == 2 and parts[0] == "B" and parts[1] in ("0", "1"):
        save_config({**load_config(config_path), "wifi_broadcast": parts[1] == "1"}, config_path)
        return None
    if parts[:1] == ["P"]:
        return "pause"
    if not trusted:  # aparelho não pareado não decide nem executa nada
        return None
    if len(parts) == 3 and parts[0] == "A" and re.fullmatch(r"[0-9a-f]{32}", parts[1]) and parts[2] in ("y", "n"):
        responses = state_dir / "responses"
        responses.mkdir(parents=True, exist_ok=True)
        (responses / parts[1]).write_text(parts[2])
    elif len(parts) == 2 and parts[0] == "C":
        # Só roda o que o dono da máquina listou na configuração, sem passar por shell.
        argv = config.get("commands", {}).get(parts[1])
        if isinstance(argv, list) and argv:
            run(argv)
    return None


def find_port(serial_number=""):
    """Com aparelho pareado, aceita só ele. Na dúvida entre vários, não conecta."""
    from serial.tools import list_ports
    found = [p.device for p in list_ports.comports()
             if p.vid == ESPRESSIF_VID and (not serial_number or p.serial_number == serial_number)]
    return found[0] if len(found) == 1 else None


def pair():
    from serial.tools import list_ports
    found = [p for p in list_ports.comports() if p.vid == ESPRESSIF_VID]
    if len(found) != 1:
        raise SystemExit(f"Conecte exatamente um aparelho Espressif (encontrei {len(found)}).")
    save_config({**load_config(), "device_serial": found[0].serial_number})
    print(f"Pareado com {found[0].serial_number} em {found[0].device}.")


def open_port(port):
    import serial
    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = port, 115200, 0
    # Sem isso, abrir a porta pode reiniciar o ESP32-S3.
    ser.dtr = ser.rts = False
    ser.exclusive = True
    ser.open()
    return ser


def send_wifi(args):
    password = getpass.getpass(f"Senha de {args.wifi}: ")
    # A senha só vai para uma porta escolhida por quem digita, nunca para a primeira encontrada.
    ser = open_port(args.port)
    ser.write(f"W {args.wifi}\t{password}\n".encode())
    ser.close()
    print("Credenciais enviadas; ficam gravadas na memória do aparelho.")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", help="porta serial; por padrão, procura um Espressif")
    ap.add_argument("--interval", type=float, default=5)
    ap.add_argument("--once", action="store_true", help="imprime o resumo e sai, sem usar a serial")
    ap.add_argument("--wifi", metavar="SSID", help="grava as credenciais de Wi-Fi no aparelho e sai")
    ap.add_argument("--pair", action="store_true", help="pareia com o aparelho conectado e sai")
    args = ap.parse_args()
    if args.pair:
        pair()
        return
    if args.wifi and not args.port:
        ap.error("--wifi exige --port")
    if args.wifi:
        send_wifi(args)
        return
    reader = Reader()
    ser = None
    udp = None
    buffer = b""
    paused = False
    next_send = 0.0
    last_key = None
    connected_as = ""  # número de série verificado ao abrir a porta; "" se não pareado
    private_ok = False
    while True:
        now = datetime.now(timezone.utc).astimezone()
        config = load_config()
        STATE.mkdir(parents=True, exist_ok=True)
        (STATE / "bridge.alive").touch()
        paired = config["device_serial"]
        if ser is not None and paired != connected_as:
            # O pareamento mudou: a conexão aberta não vale mais, nem o que estava no buffer.
            ser.close()
            ser, buffer = None, b""
        sessions, pending = read_sessions(), read_pending() if paired else None
        key = ([s["st"] for s in sessions], pending and pending["id"])
        try:
            if time.time() >= next_send or key != last_key:
                summary = summarize(reader.scan(now), now, sessions, pending, config)
                payload = json.dumps(summary, separators=(",", ":"))
                if args.once:
                    print(payload)
                    return
                if config["wifi_broadcast"] and not paused:
                    if udp is None:
                        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                        udp.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
                    pub = json.dumps(public_view(summary), separators=(",", ":")).encode()
                    udp.sendto(pub, ("255.255.255.255", UDP_PORT))
                if ser is None:
                    port = find_port(paired) if paired else args.port or find_port()
                    if port:
                        ser = open_port(port)
                        connected_as = paired
                        # Sem pareamento nem porta escolhida à mão, é um aparelho qualquer.
                        private_ok = bool(paired or args.port)
                        print(f"conectado em {port}", flush=True)
                if ser is not None and not paused:
                    sent = payload if private_ok else json.dumps(public_view(summary), separators=(",", ":"))
                    ser.write(sent.encode() + b"\n")
                next_send = time.time() + args.interval
                last_key = key
            if ser is not None:
                buffer += ser.read(256)
                while b"\n" in buffer:
                    line, buffer = buffer.split(b"\n", 1)
                    if handle_device_line(line.decode(errors="ignore"), config, trusted=bool(connected_as)) == "pause":
                        paused = not paused
        except OSError as exc:  # serial.SerialException herda de OSError
            print(f"sem conexão: {exc}", flush=True)
            if ser is not None:
                ser.close()
            ser = None
        time.sleep(0.5)


if __name__ == "__main__":
    main()
