# Claude Code Token Monitor for M5Stack Cardputer

**A pocket-sized desk companion for Claude Code.** See your tokens, estimated cost and agent status at a glance on an ESP32-S3 Cardputer, and look after a pixel pet that levels up as you work.

![Illustrative mockup of the token monitor screen](docs/screen-mockup.svg)

*Illustrative mockup with sample numbers. The real screens have more detail.*

🇧🇷 [Resumo em português](#em-português)

## Contents

- [Features](#features)
- [Requirements](#requirements)
- [Quick start](#quick-start)
- [Screens](#screens)
- [Keys](#keys)
- [Virtual pet](#virtual-pet)
- [Claude Code hooks](#claude-code-hooks)
- [Everything you can configure](#everything-you-can-configure)
- [Wi-Fi](#wi-fi)
- [microSD history](#microsd-history)
- [Approving from the device](#approving-from-the-device)
- [What the numbers mean](#what-the-numbers-mean)
- [How it works](#how-it-works)
- [Privacy and security](#privacy-and-security)
- [Troubleshooting](#troubleshooting)
- [Uninstall](#uninstall)
- [Project status](#project-status)
- [Development](#development)

## Features

- **Pixel mascot:** walks while Claude works, jumps when it needs you, panics on alerts, sleeps when no session is active or the PC stops sending
- **Virtual pet:** feed, play, pet and put your mascot to bed; it levels up as you use tokens and never dies
- **Mini-game and accessories:** a jumping game, plus a hat, glasses and crown unlocked by level
- **Reactions:** celebrates finished tasks and flinches at failed tools
- **Agent status:** working, idle or waiting for permission, with a beep when it needs you
- **Estimated cost:** today, week and session at API list prices
- **Burn rate and forecast:** tokens per minute and a 2-hour projection
- **Alerts:** daily budget, daily token goal and 5-hour window running out
- **Rankings:** top projects, per-model usage and a week-over-week comparison
- **Parallel sessions:** live status and current tool for up to 4 sessions
- **Streak, record and 28-day heatmap**
- **Pomodoro:** 25/5 focus cycles
- **10 themes and 8 mascot colors**, chosen separately
- **3 languages:** Portuguese, English and Spanish
- **On-device settings:** no PC needed to change the look or join Wi-Fi
- **USB or Wi-Fi:** full data over USB, totals only over Wi-Fi
- **Screen timeout:** a key only wakes the screen, so nothing is pressed blindly
- **microSD history:** daily totals in a CSV file
- **Shortcut keys:** `1`-`9` run commands you choose on your PC
- **Approve from the device:** optional, off by default, and not a security boundary

## Requirements

- M5Stack Cardputer (ESP32-S3, 8 MB flash)
- PC running [Claude Code](https://claude.com/claude-code). Developed on Linux; macOS and Windows are untested
- [uv](https://docs.astral.sh/uv/) and a USB-C data cable
- On Linux, access to the serial port (usually the `dialout` group)
- Optional: microSD card in FAT32 and a 2.4 GHz Wi-Fi network

## Quick start

```bash
git clone https://github.com/Evaldoes/cardputer-token-monitor.git
cd cardputer-token-monitor

# 1. Back up your current firmware: flashing replaces it, including any launcher
mkdir -p backup
uvx esptool --port /dev/ttyACM0 read-flash 0 0x800000 backup/firmware-original-8MB.bin

# 2. Flash the firmware
(cd firmware && uvx --from platformio pio run -t upload --upload-port /dev/ttyACM0)

# 3. Pair, so this device can receive project and session data
uv run --with pyserial bridge/token_bridge.py --pair

# 4. Send data
uv run --with pyserial bridge/token_bridge.py
```

Recommended next steps:

```bash
python3 scripts/install_hooks.py   # agent status and reactions
./scripts/install_service.sh       # start on login (Linux, systemd)
```

To restore your original firmware:

```bash
uvx esptool --port /dev/ttyACM0 write-flash 0 backup/firmware-original-8MB.bin
```

Never publish `backup/`: it may contain Wi-Fi passwords. It is already in `.gitignore`.

### Web installer

`.github/workflows/pages.yml` builds the firmware and publishes the installer in `site/` to GitHub Pages, for use in Chrome or Edge. Pages must be enabled for the repository. You still need the PC bridge afterwards.

## Screens

| Screen | Shows |
| --- | --- |
| Today, Week, Session | Tokens, cost, cache reads, 12-hour chart, rate, 5-hour window, forecast, model and mascot |
| Projects | Top 4 projects over 7 days |
| Sessions | Up to 4 sessions with status and current tool, and minutes since the last activity |
| Records | Streak, daily record, goal progress and 28-day heatmap |
| Weekly | This week vs the previous one, top day and top project |
| Models | Top 3 models today, and cost vs budget |
| Pet | Stats, mood, level and actions |
| Settings | All on-device options |

The top right shows the time of the last update, `NO DATA` after 20 s without updates, or `waiting for PC` before the first one.

## Keys

| Key | Action |
| --- | --- |
| `,` / most other keys | Previous / next screen |
| `o` | Settings |
| `g` | Open or close the pet screen |
| `r` | Start or stop Pomodoro |
| `t` | Next theme |
| `c` | Next mascot color |
| `m` | Mute or unmute |
| `p` | Pause or resume updates from the PC |
| `1`-`9` | Run a command from your config |
| `y` `y` / `n` | Approve / deny a pending request |

**In Settings:** `;` and `.` move, Enter changes or selects, `` ` `` goes back. While typing a password, Enter connects, Del erases and `fn` + `` ` `` cancels.

## Virtual pet

| Key | Action |
| --- | --- |
| `f` | Feed: +25 food, refused when full |
| `b` | Play: +20 joy, -10 energy, -5 food; refused when exhausted or starving |
| `h` | Pet: +10 joy |
| `s` | Sleep or wake up; wakes by itself when fully rested |
| `j` | Jumping game: any key jumps, Enter or Space restarts, `` ` `` exits |
| `g` | Leave the pet screen |

**Stats** go from 0 to 100, starting at 80:

| Stat | Per hour |
| --- | --- |
| Food | -6 awake, -3 asleep |
| Energy | -5 awake, +30 asleep |
| Joy | -4 awake, -2 asleep; doubled when food or energy is below 20 |

- **Earning joy:** each game point gives +1, up to 30 per game, and a game costs 5 energy. A finished Pomodoro focus gives +5 and a finished Claude task gives +1.
- **Time away:** stats catch up when the PC next connects by USB, up to 7 days.
- **Death:** there is none. It only gets sad until it gets attention.
- **Saving:** at most every 10 minutes to spare the flash, so the last few actions can be lost on power-off.

**Levels** come from tokens seen by USB while the device is on:

| Level | Tokens | Unlocks |
| --- | --- | --- |
| 2 | 500 thousand | Hat |
| 3 | 2 million | Glasses |
| 4 | 4.5 million | |
| 5 | 8 million | Crown |

Each level N+1 needs 500 thousand x N² tokens, up to level 99.

## Claude Code hooks

Token totals work without hooks because they come from local logs. Hooks add live status, sessions and reactions.

```bash
python3 scripts/install_hooks.py            # install status hooks
python3 scripts/install_hooks.py --approve  # also allow approving from the device
python3 scripts/install_hooks.py --remove   # remove only this project's hooks
```

The installer edits `~/.claude/settings.json`, saves a backup next to it as `settings.json.bak-<timestamp>`, and keeps hooks from other tools. Run it again after updating this project, because new features may need new events. New hooks should apply to new Claude Code sessions.

| Event | Effect |
| --- | --- |
| `SessionStart` | Session shown as idle |
| `UserPromptSubmit`, `PreToolUse`, `PostToolUse` | Session shown as working, with the tool name |
| `PostToolUseFailure` | Working, and the mascot flinches |
| `Notification` | `permission_prompt` shows waiting, `idle_prompt` shows idle |
| `PermissionRequest` | Waiting; with `--approve`, also asks the device |
| `Stop` | Idle, and the mascot celebrates |
| `SessionEnd` | Session removed |

The hook is `hooks/agent_hook.py` and uses only the Python standard library. Its errors go to stderr and are never meant to disturb Claude Code. The status hooks have a 5 s timeout, and the approval hook has 30 s.

**State files** live in `~/.cache/cardputer-token-monitor` with private permissions:

| Path | Contents |
| --- | --- |
| `sessions/<id>.json` | State, working folder and tool of a session |
| `pending/<id>.json` | Approval request waiting for an answer |
| `responses/<id>` | Answer from the device |
| `event_done`, `event_error` | Time of the last finished task and failed tool |
| `bridge.alive` | Shows that the bridge is running |

A session marked as working with no event for 10 minutes is shown as idle, and one with no event for 24 hours disappears.

## Everything you can configure

### On the device (`o`)

Saved on the device and kept across restarts.

| Option | Values | Default |
| --- | --- | --- |
| Language | Português, English, Español | Português |
| Theme | Claude, Retro, Matrix, Oceano, Ciano, Meia-noite, Rosa, Lavanda, Algodao, Pessego | Claude |
| Mascot color | Coral, Blue, Cyan, Pink, Lilac, Green, Yellow, White | Coral |
| Sound | On, off | On |
| Brightness | 1 to 5 | 3 |
| Wi-Fi | Scan and choose a network | None |
| Forget Wi-Fi | Erases the network and turns PC broadcast off | |
| PC broadcast | On, off | Off |
| Screen off | 1, 5 or 15 minutes, never | 5 minutes |

The font has no accents, so text appears without them on the device.

### On the PC

Create `~/.config/cardputer-token-monitor.json`. It is re-read on every cycle, so no restart is needed. Every key is optional.

```json
{
  "goal_daily_tokens": 2000000,
  "budget_daily_usd": 20,
  "window_warn_minutes": 30,
  "wifi_broadcast": false,
  "device_serial": "set by --pair",
  "commands": {
    "1": ["code", "/path/to/repos"],
    "2": ["playerctl", "play-pause"],
    "3": ["notify-send", "Break", "Time to stand up"]
  }
}
```

| Key | Purpose | Default |
| --- | --- | --- |
| `goal_daily_tokens` | Alert when today's tokens reach this; `0` turns it off | `0` |
| `budget_daily_usd` | Alert when today's estimated cost reaches this; `0` turns it off | `0` |
| `window_warn_minutes` | Alert when the 5-hour window has this many minutes left | `30` |
| `wifi_broadcast` | Broadcast totals on the local network | `false` |
| `device_serial` | Paired device, set by `--pair`; keep it when editing | Empty |
| `commands` | Key `1`-`9` mapped to a program and its arguments | Empty |

If several alerts apply, only one shows: budget, then goal, then window.

**Commands** run without a shell, so `|`, `&&` and `~` don't work; call a script if you need them. Only a paired device can run them, and the screen doesn't show the result. Graphical programs may not open when the bridge runs as a service, depending on your desktop session. Anyone holding the device can trigger them, so avoid destructive ones.

### Bridge options

```bash
uv run --with pyserial bridge/token_bridge.py [options]
```

| Option | Purpose | Default |
| --- | --- | --- |
| `--port PORT` | Serial port. Ignored once paired, which uses the paired device | Find an Espressif device |
| `--interval SECONDS` | Time between updates | `5` |
| `--once` | Print one summary and exit, without a device | |
| `--pair` | Pair with the only connected Espressif device and exit | |
| `--wifi SSID` | Send Wi-Fi credentials by USB and exit; requires `--port` | |

Without pairing or `--port`, a device found automatically receives totals only.

### Autostart

```bash
./scripts/install_service.sh
systemctl --user status cardputer-token-monitor
journalctl --user -u cardputer-token-monitor -f
systemctl --user restart cardputer-token-monitor
```

Stop the service before flashing, because it holds the serial port:

```bash
systemctl --user stop cardputer-token-monitor
```

### In the source code

| What | Where |
| --- | --- |
| Model prices | `PRICES` in `bridge/token_bridge.py` |
| Days of history read | `RETENTION` in `bridge/token_bridge.py` (400) |
| Wi-Fi port | `UDP_PORT` in both programs (47474) |
| No-data warning delay | `STALE_MS` in `firmware/src/main.cpp` (20 s) |
| Themes, mascot colors and shape | `THEMES`, `MASCOT_COLORS`, `MASCOT` in `firmware/src/main.cpp` |
| Translations | `STRINGS` in `firmware/src/main.cpp` |
| Pomodoro length | `handleKey` and `loop` in `firmware/src/main.cpp` |
| microSD pins | `SD_SCK`, `SD_MISO`, `SD_MOSI`, `SD_CS` in `firmware/src/main.cpp` |
| Approval wait | `APPROVE_SECONDS` in `hooks/agent_hook.py` (25 s) |

## Wi-Fi

1. Press `o`, choose **Wi-Fi**, pick a network and type its password.
2. With the paired device on USB, turn on **PC broadcast** in the same menu. The choice is re-sent until the PC confirms it, and after that the PC's setting wins. Without pairing, set `wifi_broadcast` to `true` in the config.
3. When the status shows the network name, unplug the cable.

- The PC must stay on and on the same network, and the bridge must be running.
- USB takes priority when connected.
- Over Wi-Fi you get totals only: no projects, sessions, model, reactions, pet progress or approvals.
- Hidden networks aren't listed, and a scan shows at most 6 networks.
- Routers with client isolation can block the broadcast.
- For a password containing `` ` ``, use `token_bridge.py --port /dev/ttyACM0 --wifi "MyNetwork"`.

## microSD history

With a FAT32 card inserted when the device starts, a line is added to `/token-history.csv` when the date changes:

```csv
2026-11-01,1234567,12.34
```

The columns are date, tokens and estimated cost. Only USB data is written, and only while the device is on when the date changes.

## Approving from the device

**Off by default. This is a convenience, not a security boundary.** Decisions travel through files owned by your user, so any process running as you, including one started by Claude, could forge a response. The USB serial number used for pairing can be spoofed, so it only prevents mix-ups. Anyone holding the device can approve commands. Don't enable this if those risks matter to you.

```bash
uv run --with pyserial bridge/token_bridge.py --pair
python3 scripts/install_hooks.py --approve
```

- **What can be approved:** only Bash commands of up to 60 printable ASCII characters on one line, with no extra options and a short folder name, so that everything is shown. Anything else can only be denied on the device or answered in the terminal.
- **To approve:** press `y` twice within 3 s. `n` denies. Other keys do nothing, and keys are ignored for 0.7 s after a request appears.
- **With no answer or no bridge:** the decision goes back to the terminal; it never approves by default.
- **Delay:** each request can wait up to 25 s before reaching the terminal.

## What the numbers mean

- **Total:** input + output + cache creation. Cache reads appear separately because they would dwarf everything else.
- **Rate:** tokens per minute over the last 5 minutes.
- **Forecast:** today so far plus the last hour's rate for up to 2 more hours.
- **5h:** an estimate from your local logs, starting at the full hour of the first message. It isn't your plan's real limit.
- **Cost:** an estimate at list prices, not your bill. On a flat-rate plan, it shows what the usage would cost on the API. Cache writes count as 1.25x input for 5 minutes and 2x for 1 hour, and fast mode as 2x. Unknown models count as $0, so update `PRICES` when new models appear.
- **Week:** the last 7 days, not the calendar week.
- **Streak:** days in a row with usage. Today doesn't break it until the day ends.
- **Scope:** Claude Code on this PC only, excluding claude.ai and other machines.
- **No percentage of your plan limit:** the local logs used here don't provide it.

## How it works

```text
~/.claude/projects/**/*.jsonl ─┐
Claude Code hooks → state files ┴→ bridge/token_bridge.py ─┬→ USB serial (JSON, every 5 s) → Cardputer
                                                           └→ UDP broadcast, totals only   → Cardputer
Cardputer keys → USB serial → bridge → approvals, commands, pause, broadcast setting
```

Repository layout:

| Path | Contents |
| --- | --- |
| `bridge/token_bridge.py` | Reads logs, builds the summary, talks to the device |
| `hooks/agent_hook.py` | Records session state and handles approvals |
| `firmware/` | PlatformIO project for the Cardputer |
| `scripts/` | Hook and service installers |
| `site/`, `.github/workflows/pages.yml` | Web installer |
| `tests/` | Python tests |

## Privacy and security

- **No account or API key:** everything comes from local files, and nothing is sent to the internet.
- **USB data:** totals, project and session names, tool names and, with approval on, short commands. Conversation content is never sent.
- **Wi-Fi broadcasts:** unauthenticated. Anyone on your network can read the totals or spoof what the screen shows, including the mascot's mood. They never carry project names or approvals, make no sound, and are never written to the microSD.
- **Wi-Fi password:** stored unencrypted on the device, so anyone holding it could extract it. Use **Forget Wi-Fi** before lending it, and remember that firmware backups may contain the password.
- **Unpaired devices:** can't approve requests, run commands or change settings on the PC.
- **Physical access:** with pairing, anyone holding the device can trigger your configured commands, and approvals if enabled.

## Troubleshooting

| Problem | Try |
| --- | --- |
| `waiting for PC` or `NO DATA` | Check the bridge with `systemctl --user status cardputer-token-monitor`, and that updates aren't paused with `p` |
| Port busy when flashing | Stop the service first |
| Permission denied on the port | Add yourself to the group with `sudo usermod -aG dialout $USER`, then log in again |
| Device not found | Use a data cable, and connect only one Espressif device or use `--port` |
| Projects and sessions are empty | Pair the device; for sessions, also install the hooks and start a new Claude Code session |
| No reaction to failures | Run the hook installer again |
| Wrong Wi-Fi data or none | Turn on PC broadcast, use the same network, and check for client isolation |
| No microSD history | Use FAT32 with the card inserted before starting the device |
| Text too faint on a theme | Press `t`, because Algodao is the only light theme |
| Cost is $0 | Add the model to `PRICES` |

## Uninstall

```bash
python3 scripts/install_hooks.py --remove
systemctl --user disable --now cardputer-token-monitor
rm ~/.config/systemd/user/cardputer-token-monitor.service
rm -r ~/.cache/cardputer-token-monitor ~/.config/cardputer-token-monitor.json
uvx esptool --port /dev/ttyACM0 write-flash 0 backup/firmware-original-8MB.bin
```

## Project status

- **Tested on a real device (Linux):** mascot, totals, keys, themes, Projects and Sessions screens, and agent status through hooks.
- **Not yet confirmed on a real device:** pet, mini-game, accessories, reactions, Pomodoro, screen timeout, Weekly and Models screens, settings, languages, Wi-Fi, microSD history, approval, shortcut keys, web installer, Cardputer ADV, macOS and Windows.

## Development

```bash
uvx pytest tests                                  # Python tests
(cd firmware && uvx --from platformio pio run)    # build the firmware
python3 bridge/token_bridge.py --once             # preview the data
```

The tests cover the Python code. The firmware only has a build check, so screen and key changes need a real device.

Issues and pull requests are welcome, especially reports from Cardputer ADV, macOS and Windows.

## License

[MIT](LICENSE). Independent community project, not affiliated with Anthropic or M5Stack.

## Em português

Monitor de tokens do Claude Code com bichinho virtual para o M5Stack Cardputer. Mostra consumo, custo estimado e status do agente na tela do aparelho, por USB ou Wi-Fi.

1. Siga o [Quick start](#quick-start) para gravar o firmware, parear e enviar os dados.
2. Rode `python3 scripts/install_hooks.py` para ativar o status do agente.
3. No aparelho, aperte `o` para configurar idioma, tema, som, brilho e Wi-Fi, e `g` para abrir o bichinho.
