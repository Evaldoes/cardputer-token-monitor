import json
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / "bridge"))
from token_bridge import Entry, Reader, block_remaining, model_label, parse_line, summarize

TZ = timezone(timedelta(hours=-4))
NOW = datetime(2026, 11, 1, 21, 30, tzinfo=TZ)


def line(mid="m1", ts="2026-11-02T01:00:00Z", model="claude-opus-5-5", out=10):
    return json.dumps({"timestamp": ts, "requestId": "r", "sessionId": "s",
                       "message": {"id": mid, "model": model, "usage": {
                           "input_tokens": 1, "output_tokens": out,
                           "cache_creation_input_tokens": 100,
                           "cache_read_input_tokens": 5000}}})


def entry(ts, tokens=100, session="s", cache=0):
    return Entry(ts, "claude-opus-5-5", session, tokens, cache)


def test_parse_separa_leitura_de_cache():
    _, e = parse_line(line())
    assert e.tokens == 111 and e.cache_read == 5000


def test_parse_ignora_invalidas_e_sinteticas():
    assert parse_line("nao json") is None
    assert parse_line('{"type":"user"}') is None
    assert parse_line(line(model="<synthetic>")) is None


def test_model_label():
    assert model_label("claude-opus-5-5") == "Opus 5.5"


def test_hoje_respeita_fuso_local():
    # 03:59 UTC do dia 2 ainda é dia 1 em -04; 03:59 UTC do dia 1 já é ontem.
    s = summarize([entry(datetime(2026, 11, 2, 0, 59, tzinfo=timezone.utc)),
                   entry(datetime(2026, 11, 1, 3, 59, tzinfo=timezone.utc))], NOW)
    assert s["d"] == 100 and s["w"] == 200


def test_grafico_coloca_hora_atual_no_fim():
    s = summarize([entry(NOW - timedelta(minutes=1)), entry(NOW - timedelta(hours=1))], NOW)
    assert s["h"][11] == 100 and s["h"][10] == 100 and sum(s["h"]) == 200


def test_ritmo_usa_ultimos_cinco_minutos():
    s = summarize([entry(NOW - timedelta(minutes=1), 1000), entry(NOW - timedelta(minutes=9), 9999)], NOW)
    assert s["r"] == 200


def test_sessao_e_a_da_ultima_mensagem():
    s = summarize([entry(NOW - timedelta(hours=1), 50, "a"), entry(NOW, 7, "b")], NOW)
    assert s["s"] == 7


def test_janela_de_5h():
    assert block_remaining([entry(NOW - timedelta(hours=1))], NOW) == 210
    assert block_remaining([entry(NOW - timedelta(hours=6))], NOW) == 0
    assert block_remaining([], NOW) == 0


def test_sem_dados():
    s = summarize([], NOW)
    assert s["d"] == 0 and s["m"] == "-" and s["b"] == 0


def test_reader_nao_conta_duas_vezes_e_le_incremental(tmp_path):
    f = tmp_path / "proj" / "a.jsonl"
    f.parent.mkdir()
    f.write_text(line() + "\n" + line() + "\n")  # mesma resposta repetida
    r = Reader(tmp_path)
    now = datetime.now(timezone.utc).astimezone()
    # O arquivo acabou de ser escrito; o carimbo fixo das linhas não pode expirar aqui.
    novo = now.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    f.write_text(line(ts=novo) + "\n" + line(ts=novo) + "\n")
    assert len(r.scan(now)) == 1
    with f.open("a") as h:
        h.write(line("m2", ts=novo) + "\n" + line("m3", ts=novo)[:20])  # última linha incompleta
    assert len(r.scan(now)) == 2
