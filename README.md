# cardputer-token-monitor

Monitor de tokens do Claude Code na tela de um M5Stack Cardputer, pelo cabo USB.

O programa do PC lê `~/.claude/projects`, resume o uso e envia ao aparelho a cada 5 s.
Só totais saem do PC; o conteúdo das conversas não é enviado.

## Uso

```bash
cd firmware && uvx --from platformio pio run -t upload   # grava o firmware
uv run --with pyserial bridge/token_bridge.py            # envia os dados
```

No aparelho, qualquer tecla alterna entre hoje, semana e sessão.

## O que os números significam

- **Total:** entrada + saída + criação de cache. A leitura de cache aparece à parte.
- **5h:** estimativa do tempo restante da janela, não é o limite real do plano.
- **Escopo:** só o Claude Code deste PC, sem claude.ai nem outras máquinas.

## Testes

```bash
uvx pytest tests
```

## Restaurar o firmware anterior

Faça antes um backup da flash, que não vai para o repositório porque pode conter senhas de Wi-Fi:

```bash
uvx esptool --port /dev/ttyACM0 read-flash 0 0x800000 backup/firmware-original-8MB.bin
uvx esptool --port /dev/ttyACM0 write-flash 0 backup/firmware-original-8MB.bin
```
