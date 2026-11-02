#!/usr/bin/env python3
"""Lê os registros locais do Claude Code e envia um resumo ao Cardputer pela USB."""
import argparse
import json
import time
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from pathlib import Path

PROJECTS = Path.home() / ".claude" / "projects"
RETENTION = timedelta(days=8)
BLOCK = timedelta(hours=5)


@dataclass
class Entry:
    ts: datetime
    model: str
    session: str
    tokens: int  # entrada + saída + criação de cache
    cache_read: int


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
    return key, Entry(ts, msg["model"], d.get("sessionId", ""), tokens,
                      usage.get("cache_read_input_tokens") or 0)


def model_label(model):
    """claude-opus-5-5 -> Opus 5.5"""
    parts = model.split("-")
    if len(parts) >= 4 and parts[0] == "claude":
        return f"{parts[1].capitalize()} {parts[2]}.{parts[3]}"
    return model[:12]


def block_remaining(entries, now):
    """Minutos restantes da janela de 5h atual (estimativa); 0 se não houver janela aberta."""
    start = None
    for ts in sorted(e.ts for e in entries):
        if start is None or ts >= start + BLOCK:
            start = ts.replace(minute=0, second=0, microsecond=0)
    if start is None or now >= start + BLOCK:
        return 0
    return int((start + BLOCK - now).total_seconds() // 60)


def summarize(entries, now):
    """now precisa ter fuso: define o que é "hoje" e as horas do gráfico."""
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
    return {
        "t": now.strftime("%H:%M"),
        "d": sum(e.tokens for e in today), "dc": sum(e.cache_read for e in today),
        "w": sum(e.tokens for e in week), "wc": sum(e.cache_read for e in week),
        "s": sum(e.tokens for e in session), "sc": sum(e.cache_read for e in session),
        "r": recent // 5,
        "m": model_label(last.model) if last else "-",
        "b": block_remaining(entries, now),
        "h": hourly,
    }


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


def open_port(port):
    import serial
    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = port, 115200, 1
    # Sem isso, abrir a porta pode reiniciar o ESP32-S3.
    ser.dtr = ser.rts = False
    ser.open()
    return ser


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--interval", type=float, default=5)
    ap.add_argument("--once", action="store_true", help="imprime o resumo e sai, sem usar a serial")
    args = ap.parse_args()
    reader = Reader()
    ser = None
    while True:
        now = datetime.now(timezone.utc).astimezone()
        payload = json.dumps(summarize(reader.scan(now), now), separators=(",", ":"))
        if args.once:
            print(payload)
            return
        try:
            if ser is None:
                ser = open_port(args.port)
                print(f"conectado em {args.port}", flush=True)
            ser.write(payload.encode() + b"\n")
        except OSError as exc:  # serial.SerialException herda de OSError
            print(f"sem conexão: {exc}", flush=True)
            if ser is not None:
                ser.close()
            ser = None
        time.sleep(args.interval)


if __name__ == "__main__":
    main()
