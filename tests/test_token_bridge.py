import json
import os
import time
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


from token_bridge import handle_device_line, public_view, read_pending, read_sessions, records, usage_cost


def test_custo_opus_5_5():
    u = {"input_tokens": 1_000_000, "output_tokens": 1_000_000, "cache_read_input_tokens": 1_000_000,
         "cache_creation_input_tokens": 2_000_000, "cache_creation": {"ephemeral_1h_input_tokens": 1_000_000}}
    # 4 + 20 + 0,20 + 1h: 8 + 5min: 5
    assert abs(usage_cost("claude-opus-5-5", u) - 37.2) < 1e-9


def test_custo_modelo_desconhecido_e_modo_rapido():
    u = {"output_tokens": 1_000_000}
    assert usage_cost("modelo-novo", u) == 0
    assert usage_cost("claude-opus-5-5", {**u, "speed": "fast"}) == 40


def test_previsao_e_alertas():
    es = [entry(NOW - timedelta(minutes=30), 600)]
    s = summarize(es, NOW)
    assert s["f"] == 600 + 10 * 120
    assert summarize(es, NOW, config={"goal_daily_tokens": 500})["al"] == "meta"
    assert summarize(es, NOW)["al"] == ""
    assert summarize([entry(NOW - timedelta(hours=4, minutes=20))], NOW)["al"] == "janela"


def test_recordes_e_sequencia():
    es = [entry(NOW - timedelta(days=d), 100 * (d + 1)) for d in (0, 1, 2, 5)]
    r = records(es, NOW)
    assert r["st"] == 3 and r["rec"] == 600 and len(r["hm"]) == 28 and r["hm"][27] > 0 and r["hm"][26] > 0


def test_sequencia_nao_quebra_antes_do_dia_acabar():
    assert records([entry(NOW - timedelta(days=1))], NOW)["st"] == 1
    assert records([entry(NOW - timedelta(days=2))], NOW)["st"] == 0


def test_ranking_de_projetos():
    es = [Entry(NOW, "m", "s", 5, 0, 0, "a"), Entry(NOW, "m", "s", 9, 0, 0, "b")]
    assert [p["n"] for p in summarize(es, NOW)["pr"]] == ["b", "a"]


def test_sessoes_prioridade_e_estado_velho(tmp_path):
    d = tmp_path / "sessions"
    d.mkdir()
    (d / "a.json").write_text('{"state":"working","cwd":"/x/a"}')
    (d / "b.json").write_text('{"state":"waiting","cwd":"/x/b"}')
    (d / "c.json").write_text('{"state":"working","cwd":"/x/c"}')
    os.utime(d / "c.json", (1, time.time() - 3600))
    s = read_sessions(tmp_path)
    assert s[0]["n"] == "b" and {x["n"]: x["st"] for x in s}["c"] == "idle"
    assert summarize([], NOW, s)["a"] == "waiting"


def test_pendente_expirado_some(tmp_path):
    d = tmp_path / "pending"
    d.mkdir()
    (d / "x.json").write_text(json.dumps({"id": "x", "expires": time.time() - 1}))
    assert read_pending(tmp_path) is None
    (d / "y.json").write_text(json.dumps({"id": "y", "tool": "Bash", "expires": time.time() + 9}))
    assert read_pending(tmp_path)["id"] == "y"


def test_wifi_nao_expoe_nomes_nem_aprovacao():
    s = summarize([Entry(NOW, "claude-opus-5-5", "s", 5, 0, 0, "segredo")], NOW,
                  [{"n": "segredo", "st": "waiting", "tool": "Bash"}], {"id": "1", "tool": "Bash", "d": "rm"})
    text = json.dumps(public_view(s))
    assert "segredo" not in text and "rm" not in text and '"p"' not in text


def test_linha_do_aparelho(tmp_path):
    rid = "a" * 32
    handle_device_line(f"A {rid} y", {}, tmp_path, trusted=True)
    assert (tmp_path / "responses" / rid).read_text() == "y"
    handle_device_line("A ../../x y", {}, tmp_path, trusted=True)
    assert len(list((tmp_path / "responses").iterdir())) == 1
    assert handle_device_line("P", {}, tmp_path) == "pause"


def test_aparelho_nao_pareado_nao_decide(tmp_path):
    calls = []
    cfg = {"commands": {"1": ["echo"]}}
    handle_device_line("A " + "a" * 32 + " y", cfg, tmp_path, calls.append, trusted=False)
    handle_device_line("C 1", cfg, tmp_path, calls.append, trusted=False)
    assert not (tmp_path / "responses").exists() and calls == []


def test_aparelho_pareado_liga_transmissao_wifi(tmp_path):
    cfg = tmp_path / "c.json"
    cfg.write_text('{"device_serial": "x"}')
    handle_device_line("B 1", {}, tmp_path, trusted=False, config_path=cfg)
    assert "wifi_broadcast" not in json.loads(cfg.read_text())
    handle_device_line("B 1", {}, tmp_path, trusted=True, config_path=cfg)
    saved = json.loads(cfg.read_text())
    assert saved["wifi_broadcast"] is True and saved["device_serial"] == "x"


def test_resumo_informa_transmissao_so_pela_usb():
    s = summarize([], NOW, config={"wifi_broadcast": True})
    assert s["wb"] is True and "wb" not in public_view(s)


def test_orcamento_modelos_e_semana_anterior():
    e = Entry(NOW, "claude-opus-5-5", "s", 10, 0, 6.0, "p")
    old = Entry(NOW - timedelta(days=8), "claude-opus-5-5", "s", 7, 0, 0, "p")
    s = summarize([e, old], NOW, config={"budget_daily_usd": 5})
    assert s["al"] == "custo" and s["ml"] == [{"n": "Opus 5.5", "v": 10}] and s["wp"] == 7
    assert s["td"] == 10 and s["tdn"] == "01/11" and s["ia"] == 0


def test_wifi_nao_expoe_modelos_nem_eventos():
    s = {**summarize([Entry(NOW, "claude-opus-5-5", "s", 1, 0)], NOW), "ed": 1, "ee": 1}
    assert not {"ml", "tdn", "ed", "ee", "ia"} & set(public_view(s))


def test_varios_aparelhos_pareados():
    from token_bridge import paired_serials
    assert paired_serials({"device_serial": "a", "device_serials": ["b"]}) == ["a", "b"]
    assert paired_serials({"device_serials": ["a", "b"]}) == ["a", "b"]
    assert paired_serials({}) == []


def test_comando_so_roda_se_listado(tmp_path):
    calls = []
    cfg = {"commands": {"1": ["echo", "oi"]}}
    handle_device_line("C 1", cfg, tmp_path, calls.append, trusted=True)
    handle_device_line("C 2", cfg, tmp_path, calls.append, trusted=True)
    assert calls == [["echo", "oi"]]
