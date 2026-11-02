#!/usr/bin/env python3
"""Gera as ilustrações das telas em docs/screens, com dados de exemplo."""
from html import escape
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "docs" / "screens"
BG, FG, DIM, ACCENT, WARN = "#000", "#fff", "#848484", "#d97757", "#ffc400"
CORAL = "#de7356"
MASCOT = ["001111111100", "001211112100", "111111111111", "111111111111",
          "001111111100", "001111111100", "001010010100", "001010010100"]
HOURS = [5, 9, 12, 22, 31, 14, 9, 25, 34, 20, 12, 27]
HEAT = [1, 1, 0, 3, 0, 0, 0, 3, 2, 4, 3, 1, 0, 0, 1, 4, 1, 2, 1, 2, 3, 1, 1, 1, 1, 2, 0, 1]
HEAT_COLORS = ["#2e2e2e", "#57301f", "#824730", "#ad5f43", ACCENT]

T = {
    "en": dict(today="TODAY", week="WEEK", session="SESSION", est="est", projects="PROJECTS 7D", sessions="SESSIONS",
               active="active", waiting="waiting", idle="idle", idle_for="idle: 2 min", records="RECORDS",
               streak="streak: 12 days", record="record: 4,20 M", goal="goal: 1,24 M / 2,00 M",
               weekly="WEEK vs PREVIOUS", this="This: 7,50 M", prev="Previous: 6,12 M", top_day="Top day: 30/10 1,40 M",
               top_project="Project: my-api", models="MODELS TODAY", budget="Budget: $12.40 / $20.00",
               pet="MASCOT  level 3", food="Food", energy="Energy", joy="Joy", mood="very happy!",
               hint1="f feed  b play  h pet", hint2="s sleep  j game  g exit", game="key jumps  ` exit", best="best 31",
               settings="SETTINGS", language="Language", lang="English", theme="Theme", mascot="Mascot", color="Coral",
               sound="Sound", on="on", off="off", bright="Brightness", wifi="Wi-Fi", forget="Forget Wi-Fi",
               menu="; . move  enter change  ` exit", networks="WI-FI NETWORKS", net="; . move  enter select  ` back",
               password="PASSWORD FOR MyWifi", pass_hint="enter connect  fn+` cancel", request="PERMISSION REQUEST",
               approve="y approve (2x)    n deny"),
    "pt": dict(today="HOJE", week="SEMANA", session="SESSAO", est="prev", projects="PROJETOS 7D", sessions="SESSOES",
               active="ativo", waiting="aguarda", idle="parado", idle_for="sem atividade: 2 min", records="RECORDES",
               streak="sequencia: 12 dias", record="recorde: 4,20 M", goal="meta: 1,24 M / 2,00 M",
               weekly="SEMANA x ANTERIOR", this="Esta: 7,50 M", prev="Anterior: 6,12 M", top_day="Maior dia: 30/10 1,40 M",
               top_project="Projeto: minha-api", models="MODELOS HOJE", budget="Orcamento: $12.40 / $20.00",
               pet="MASCOTE  nivel 3", food="Fome", energy="Energia", joy="Alegria", mood="muito feliz!",
               hint1="f comer  b brincar  h carinho", hint2="s dormir  j jogar  g sair", game="tecla pula  ` sai", best="recorde 31",
               settings="CONFIGURACOES", language="Idioma", lang="Portugues", theme="Tema", mascot="Mascote", color="Coral",
               sound="Som", on="ligado", off="desligado", bright="Brilho", wifi="Wi-Fi", forget="Esquecer Wi-Fi",
               menu="; . mover  enter alterar  ` sair", networks="REDES WI-FI", net="; . mover  enter escolher  ` voltar",
               password="SENHA DE MyWifi", pass_hint="enter conectar  fn+` cancelar", request="PEDIDO DE PERMISSAO",
               approve="y aprovar (2x)    n negar"),
}


class Screen:
    """Coordenadas iguais às do firmware (240x135), ampliadas na imagem."""

    def __init__(self):
        self.parts = []

    def text(self, value, x, y, color=FG, size=13, anchor="start", bold=False):
        weight = ' font-weight="bold"' if bold else ""
        self.parts.append(f'<text x="{x}" y="{y + size * 0.85:.1f}" fill="{color}" font-size="{size}" '
                          f'text-anchor="{anchor}"{weight}>{escape(value)}</text>')

    def rect(self, x, y, w, h, color):
        self.parts.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{color}"/>')

    def outline(self, x, y, w, h, color):
        self.parts.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="none" stroke="{color}"/>')

    def header(self, title, right="21:14"):
        self.text(title, 4, 2, ACCENT)
        self.text(right, 236, 2, DIM, anchor="end")

    def mascot(self, x, y, px, hat=True, glasses=True):
        for r, row in enumerate(MASCOT):
            for c, v in enumerate(row):
                if v != "0":
                    self.rect(x + c * px, y + r * px, px, px, "#000" if v == "2" else CORAL)
        if hat:
            self.rect(x + 3 * px, y - 2 * px, 6 * px, 2 * px, "#222")
            self.rect(x + 2 * px, y - px / 2, 8 * px, max(1, px / 2), "#222")
        if glasses:
            self.outline(x + 2 * px, y + px / 2, 8 * px, 2 * px, DIM)

    def bars(self, x, y, w, h):
        step = w / 12
        for i, v in enumerate(HOURS):
            bh = v * h / max(HOURS)
            self.rect(x + i * step, y + h - bh, step - 2, bh, ACCENT if i == 11 else DIM)

    def svg(self):
        body = "".join(self.parts)
        return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="-8 -8 256 151" role="img" shape-rendering="crispEdges">'
                '<rect x="-8" y="-8" width="256" height="151" rx="7" fill="#26282c"/>'
                '<rect width="240" height="135" fill="#000"/>'
                f'<g font-family="DejaVu Sans Mono, Menlo, Consolas, monospace">{body}</g></svg>\n')


def totals(t, title, total, cost, cache):
    s = Screen()
    s.header(title)
    s.text(total, 4, 20, FG, 26, bold=True)
    s.mascot(184, 24, 4)
    s.text(cost, 4, 60)
    s.text(f"cache {cache}", 64, 60, DIM)
    s.bars(4, 80, 120, 34)
    s.text("18,0 k/min", 132, 78)
    s.text("5h: 2h13", 132, 94)
    s.text(f"{t['est']} 1,86 M", 4, 118, DIM)
    s.text("Opus 5.5", 236, 118, DIM, anchor="end")
    return s


def ranked(s, rows, top, start, step):
    for i, (name, value, amount) in enumerate(rows):
        y = start + i * step
        s.text(name, 4, y)
        s.text(value, 236, y, anchor="end")
        s.rect(4, y + 17, amount * 232 / top, 4, ACCENT)


def build(t):
    screens = {
        "today": totals(t, t["today"], "1,24 M", "$12.40", "42,1 M"),
        "week": totals(t, t["week"], "7,50 M", "$98.20", "310 M"),
        "session": totals(t, t["session"], "218,9 k", "$2.10", "3,10 M"),
    }

    s = screens["projects"] = Screen()
    s.header(t["projects"])
    ranked(s, [("my-api", "2,14 M", 214), ("website", "1,48 M", 148), ("mobile-app", "990,2 k", 99), ("scripts", "410,5 k", 41)], 214, 24, 27)

    s = screens["sessions"] = Screen()
    s.header(t["sessions"])
    for i, (name, tool, state, color) in enumerate([("my-api", "Bash", t["active"], ACCENT), ("website", "", t["waiting"], WARN), ("scripts", "", t["idle"], DIM)]):
        y = 26 + i * 26
        s.parts.append(f'<circle cx="10" cy="{y + 8}" r="4" fill="{color}"/>')
        s.text(name, 22, y)
        s.text(tool, 128, y, DIM)
        s.text(state, 236, y, color, anchor="end")
    s.text(t["idle_for"], 4, 118, DIM)

    s = screens["records"] = Screen()
    s.header(t["records"])
    s.text(t["streak"], 4, 22)
    s.text(t["record"], 4, 40)
    s.text(t["goal"], 4, 58)
    for i, level in enumerate(HEAT):
        s.rect(4 + (i % 14) * 16, 86 + (i // 14) * 22, 14, 20, HEAT_COLORS[level])

    s = screens["weekly"] = Screen()
    s.header(t["weekly"])
    s.text(t["this"], 4, 24)
    s.text(t["prev"], 4, 44)
    s.text("+22%", 236, 34, WARN, anchor="end")
    s.text(t["top_day"], 4, 74, DIM)
    s.text(t["top_project"], 4, 94, DIM)

    s = screens["models"] = Screen()
    s.header(t["models"])
    ranked(s, [("Opus 5.5", "1,12 M", 112), ("Sonnet 5.5", "98,4 k", 10), ("Haiku 5.5", "21,6 k", 2)], 112, 22, 26)
    s.text(t["budget"], 4, 112, DIM)

    s = screens["pet"] = Screen()
    s.text(t["pet"], 4, 2, ACCENT)
    s.mascot(14, 44, 6)
    for label, value, y in ((t["food"], 72, 24), (t["energy"], 58, 42), (t["joy"], 91, 60)):
        s.text(label, 104, y, FG, 11)
        s.outline(160, y + 3, 74, 10, DIM)
        s.rect(162, y + 5, value * 70 / 100, 6, ACCENT)
    s.text(t["mood"], 104, 84)
    s.text(t["hint1"], 4, 110, DIM, 9)
    s.text(t["hint2"], 4, 122, DIM, 9)

    s = screens["game"] = Screen()
    s.text("12", 4, 2, ACCENT)
    s.text(t["best"], 236, 2, DIM, anchor="end")
    s.rect(0, 112, 240, 1, DIM)
    s.rect(130, 96, 8, 16, WARN)
    s.mascot(20, 72, 2, hat=False, glasses=False)
    s.text(t["game"], 4, 117, DIM)

    s = screens["settings"] = Screen()
    s.text(t["settings"], 4, 2, ACCENT)
    rows = [(t["language"], t["lang"]), (t["theme"], "Claude"), (t["mascot"], t["color"]), (t["sound"], t["on"]),
            (t["bright"], "3/5"), (t["wifi"], "MyWifi"), (t["forget"], "")]
    for i, (label, value) in enumerate(rows):
        color = ACCENT if i == 0 else FG
        y = 19 + i * 14
        s.text(("> " if i == 0 else "  ") + label, 4, y, color, 12)
        s.text(value, 236, y, color, 12, anchor="end")
    s.text(t["menu"], 4, 122, DIM, 9)

    s = screens["wifi"] = Screen()
    s.text(t["networks"], 4, 2, ACCENT)
    for i, name in enumerate(["MyWifi", "Office-5", "Guest", "Cafe"]):
        s.text(("> " if i == 0 else "  ") + name, 4, 20 + i * 16, ACCENT if i == 0 else FG)
    s.text(t["net"], 4, 122, DIM, 9)

    s = screens["password"] = Screen()
    s.text(t["password"], 4, 2, ACCENT)
    s.text("********k", 4, 50)
    s.rect(4, 68, 232, 1, DIM)
    s.text(t["pass_hint"], 4, 122, DIM, 9)

    s = screens["permission"] = Screen()
    s.text(t["request"], 4, 2, WARN)
    s.mascot(190, 6, 3, hat=False, glasses=False)
    s.text("Bash", 4, 24, FG, 15, bold=True)
    s.text("my-api", 4, 42, ACCENT)
    s.text("npm run test", 4, 60, DIM)
    s.text(t["approve"], 4, 116)
    return screens


def main():
    for lang, strings in T.items():
        folder = OUT / lang
        folder.mkdir(parents=True, exist_ok=True)
        for name, screen in build(strings).items():
            (folder / f"{name}.svg").write_text(screen.svg())


if __name__ == "__main__":
    main()
