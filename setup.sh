#!/usr/bin/env bash
# Configura o monitor neste PC. Pode ser rodado de novo sem causar problema.
set -euo pipefail

REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SERVICE=cardputer-token-monitor
CONFIG="$HOME/.config/cardputer-token-monitor.json"

YES=0 FLASH=1 APPROVE=0 DRY=0 PORT="" RESTART=0 BOARD=""

usage() {
    cat <<USAGE
Uso: ./setup.sh [opções]

  --yes        aceita todas as etapas, sem perguntar
  --no-flash   só configura o PC: sem backup nem gravação
  --approve    permite aprovar pelo aparelho (sempre pede confirmação digitada)
  --board NOME cardputer ou tembed; obrigatório para gravar com --yes
  --port PORTA porta serial; por padrão, detecta o aparelho
  --dry-run    mostra o que faria, sem alterar nada
  -h, --help   mostra esta ajuda
USAGE
}

while [ $# -gt 0 ]; do
    case "$1" in
        --yes) YES=1 ;;
        --no-flash) FLASH=0 ;;
        --approve) APPROVE=1 ;;
        --dry-run) DRY=1 ;;
        --board) [ $# -ge 2 ] || { echo "--board exige um valor" >&2; exit 2; }; BOARD=$2; shift ;;
        --port) [ $# -ge 2 ] || { echo "--port exige um valor" >&2; exit 2; }; PORT=$2; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Opção desconhecida: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

step() { printf '\n== %s\n' "$1"; }
warn() { printf 'Aviso: %s\n' "$1" >&2; }
die() { printf 'Erro: %s\n' "$1" >&2; exit 1; }

confirm() {
    [ "$YES" = 1 ] && return 0
    [ "$DRY" = 1 ] && return 0
    [ -t 0 ] || die "sem terminal para perguntar; use --yes"
    local answer
    read -r -p "$1 [S/n] " answer
    case "$answer" in ""|s|S|y|Y) return 0 ;; *) return 1 ;; esac
}

run() {
    if [ "$DRY" = 1 ]; then
        printf '[simulação] %s\n' "$*"
    else
        "$@"
    fi
}

has_systemd() { command -v systemctl >/dev/null && systemctl --user show-environment >/dev/null 2>&1; }

# Se algo falhar com o serviço parado, ele volta a rodar mesmo assim.
restore_service() {
    if [ "$RESTART" = 1 ] && [ "$DRY" = 0 ]; then
        systemctl --user start "$SERVICE" || warn "não consegui reiniciar o serviço"
    fi
}
trap restore_service EXIT

find_port() {
    uv run --no-project -q --with pyserial python3 -I - <<'PY'
from serial.tools import list_ports
found = [p.device for p in list_ports.comports() if p.vid == 0x303A]
print(found[0] if len(found) == 1 else "")
PY
}

# Gravar o firmware de outro aparelho deixa a tela sem funcionar: o modelo nunca é presumido.
if [ "$FLASH" = 1 ] && [ -z "$BOARD" ]; then
    if [ "$YES" = 1 ] || [ "$DRY" = 1 ] || [ ! -t 0 ]; then
        die "informe o aparelho com --board cardputer ou --board tembed, ou use --no-flash"
    fi
    read -r -p "Qual aparelho? [cardputer/tembed] " BOARD
fi
case "$BOARD" in
    "") ;;
    cardputer) FIRMWARE="$REPO/firmware" FLASH_SIZE=0x800000 BACKUP="$REPO/backup/firmware-original-8MB.bin" ;;
    tembed) FIRMWARE="$REPO/firmware-tembed" FLASH_SIZE=0x1000000 BACKUP="$REPO/backup/tembed-original-16MB.bin" ;;
    *) die "aparelho desconhecido: $BOARD" ;;
esac

step "Verificando requisitos"
command -v python3 >/dev/null || die "python3 não encontrado"
command -v uv >/dev/null || die "uv não encontrado: veja https://docs.astral.sh/uv/"
echo "python3 e uv encontrados"
[ "$(uname -s)" = Linux ] || warn "só testado no Linux; algumas etapas podem falhar"

if [ "$FLASH" = 1 ] || [ "$APPROVE" = 1 ] || [ -z "$PORT" ]; then
    # O serviço só atrapalha quando precisamos da porta para gravar ou parear.
    if has_systemd && systemctl --user is-active --quiet "$SERVICE"; then
        if [ "$DRY" = 1 ]; then
            printf '[simulação] pararia o serviço para liberar a porta\n'
        else
            echo "Parando o serviço para liberar a porta"
            systemctl --user stop "$SERVICE"
            RESTART=1
        fi
    fi
fi

[ -n "$PORT" ] || PORT=$(find_port)
if [ -n "$PORT" ]; then
    echo "Aparelho em $PORT"
    if [ ! -r "$PORT" ] || [ ! -w "$PORT" ]; then
        warn "sem acesso a $PORT. Rode: sudo usermod -aG dialout \$USER e faça login de novo"
        PORT=""
    fi
else
    warn "nenhum aparelho único encontrado; etapas que usam o aparelho serão puladas"
fi

if [ "$FLASH" = 1 ] && [ -n "$PORT" ]; then
    step "Backup do firmware atual"
    echo "O backup pode conter senhas de Wi-Fi: fica legível só por você e nunca deve ser publicado."
    if [ -e "$BACKUP" ]; then
        echo "Backup já existe; mantido: $BACKUP"
        run chmod 600 "$BACKUP"
    elif confirm "Fazer backup do firmware em backup/?"; then
        run mkdir -p -m 700 "$REPO/backup"
        (umask 077; run uvx esptool --port "$PORT" read-flash 0 "$FLASH_SIZE" "$BACKUP")
    fi

    step "Gravação do firmware"
    echo "Isto substitui o firmware atual do aparelho."
    if [ ! -e "$BACKUP" ] && [ "$DRY" = 0 ]; then
        warn "não há backup do firmware atual"
    fi
    if confirm "Gravar o firmware agora?"; then
        run uvx --from platformio pio run -d "$FIRMWARE" -t upload --upload-port "$PORT"
        [ "$DRY" = 1 ] || sleep 3
    fi
fi

if [ -n "$PORT" ]; then
    step "Pareamento"
    if confirm "Parear com o aparelho conectado?"; then
        run uv run --no-project -q --with pyserial "$REPO/bridge/token_bridge.py" --pair
    fi
fi

step "Hooks do Claude Code"
echo "Edita ~/.claude/settings.json, com backup ao lado, e mantém hooks de outras ferramentas."
if [ "$APPROVE" = 1 ]; then
    cat <<WARNING
Aprovar pelo aparelho NÃO é uma proteção de segurança: qualquer programa rodando
como você poderia falsificar uma resposta, e quem estiver com o aparelho na mão
pode aprovar comandos. Cada pedido também pode atrasar até 25 s no terminal.
WARNING
    if [ "$DRY" = 1 ]; then
        printf '[simulação] pediria que você digitasse APROVAR\n'
    else
        [ -t 0 ] || die "--approve exige confirmação em um terminal"
        read -r -p "Digite APROVAR para ativar: " typed
        if [ "$typed" = APROVAR ]; then
            run python3 "$REPO/scripts/install_hooks.py" --approve
        else
            echo "Não confirmado; instalando só os hooks de status."
            APPROVE=0
        fi
    fi
fi
if [ "$APPROVE" = 0 ] && confirm "Instalar os hooks de status?"; then
    run python3 "$REPO/scripts/install_hooks.py"
fi

if [ "$YES" = 0 ] && [ "$DRY" = 0 ] && [ -t 0 ]; then
    step "Alertas"
    echo "Enter mantém o valor atual; 0 desliga."
    read -r -p "Meta diária de tokens: " goal
    read -r -p "Orçamento diário em US$: " budget
    GOAL=$goal BUDGET=$budget CONFIG=$CONFIG python3 -I - <<'PY'
import json
import os
from pathlib import Path

path = Path(os.environ["CONFIG"])
try:
    config = json.loads(path.read_text())
except FileNotFoundError:
    config = {}
for key, env, kind in (("goal_daily_tokens", "GOAL", int), ("budget_daily_usd", "BUDGET", float)):
    raw = os.environ[env].strip().replace(",", ".")
    if not raw:
        continue
    try:
        value = kind(raw)
    except ValueError:
        print(f"Valor ignorado: {raw}")
        continue
    if value < 0:
        print(f"Valor ignorado: {raw}")
        continue
    config[key] = value
path.parent.mkdir(parents=True, exist_ok=True)
tmp = path.with_suffix(".tmp")
tmp.write_text(json.dumps(config, indent=2) + "\n")
tmp.replace(path)
print(f"Configuração salva em {path}")
PY
fi

step "Início automático"
if has_systemd; then
    if confirm "Iniciar o monitor automaticamente no login?"; then
        run "$REPO/scripts/install_service.sh"
        RESTART=0
    fi
else
    warn "systemd do usuário indisponível. Inicie à mão: uv run --with pyserial bridge/token_bridge.py"
fi

step "Pronto"
cat <<DONE
No aparelho: o abre as configurações (idioma, Wi-Fi, tema) e g abre o bichinho.
Abra uma nova sessão do Claude Code para os hooks valerem.
Não incluído: microSD (formate em FAT32) e Wi-Fi (configure no aparelho).
DONE
