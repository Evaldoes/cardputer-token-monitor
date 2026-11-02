# Claude Code Token Monitor for M5Stack Cardputer

**A pocket-sized desk display for your Claude Code token usage.** See today's tokens, burn rate and your estimated 5-hour window at a glance, on an ESP32-S3 Cardputer connected by USB.

![Illustrative mockup of the token monitor screen](docs/screen-mockup.svg)

*Illustrative mockup with sample numbers.*

## Features

- **Today, last 7 days and current session:** press any key to switch views
- **Burn rate:** tokens per minute over the last 5 minutes
- **12-hour chart:** usage per hour
- **5-hour window:** estimated time remaining
- **Cache reads shown separately:** so they don't drown out the main total
- **Stale data warning:** the screen says so when the PC stops sending
- **Private by design:** no Wi-Fi, no API key, no account; only totals leave your PC, over the cable

## Requirements

- M5Stack Cardputer (ESP32-S3)
- Linux PC running [Claude Code](https://claude.com/claude-code); other systems are untested
- [uv](https://docs.astral.sh/uv/) and a USB-C data cable

## Quick start

```bash
git clone https://github.com/Evaldoes/cardputer-token-monitor.git
cd cardputer-token-monitor

# Back up your current firmware: flashing replaces it, including any launcher
mkdir -p backup
uvx esptool --port /dev/ttyACM0 read-flash 0 0x800000 backup/firmware-original-8MB.bin

# Flash and start sending data
(cd firmware && uvx --from platformio pio run -t upload --upload-port /dev/ttyACM0)
uv run --with pyserial bridge/token_bridge.py
```

Use `--port` if your device isn't on `/dev/ttyACM0`. To preview the data without a device, run `python3 bridge/token_bridge.py --once`.

To restore your firmware:

```bash
uvx esptool --port /dev/ttyACM0 write-flash 0 backup/firmware-original-8MB.bin
```

Never publish `backup/`: it may contain Wi-Fi passwords. It is already in `.gitignore`.

## How it works

```text
~/.claude/projects/**/*.jsonl -> bridge/token_bridge.py -> USB serial (JSON, every 5 s) -> Cardputer
```

## What the numbers mean

- **Total:** input + output + cache creation. Cache reads appear separately.
- **5h:** an estimate from your local logs, not your plan's real limit.
- **Scope:** Claude Code on this PC only, excluding claude.ai and other machines.
- **No percentage of your plan limit:** the local logs used here don't provide it.

## Tests

```bash
uvx pytest tests
```

## Contributing

Issues and pull requests are welcome, especially Cardputer ADV reports, macOS and Windows support, and Wi-Fi mode.

## License

[MIT](LICENSE). Independent community project, not affiliated with Anthropic or M5Stack.

---

🇧🇷 **Em português:** monitor do consumo de tokens do Claude Code na tela do M5Stack Cardputer, conectado por USB. Siga o *Quick start* acima.
