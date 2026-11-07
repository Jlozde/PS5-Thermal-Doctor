# PS5 Thermal Doctor

**English** · [Türkçe](README.tr.md)

A background payload for jailbroken PS5 consoles. While you play, it records **temperature, fan speed, power draw and FPS**. When the game closes it answers **"does this console need maintenance?"** and shows the result as a PS5 notification. A web UI, opened from a phone or PC, shows live charts, the session history and the long-term trend.

> **Note:** It does not run on a normal (non-jailbroken) PS5. It needs firmware 4.03–14.10 and the etaHEN / elfldr loader.

Everything a user sees is available in **English and Turkish**: the web UI, PS5 notifications, the log, `config.ini` comments, API texts, tool output and these documents. See [Language](#language).

## What it reads

| Data | Source |
|---|---|
| CPU and SoC temperature (8 channels) | `sceKernelGetCpuTemperature`, `sceKernelGetSocSensorTemperature` |
| Fan speed (%) | `sceKernelGetCurrentFanDuty` (raw value 0–1024) |
| Power (W): GPU, CPU and memory rails | `sceKernelGetSocPowerConsumption` |
| FPS | `/dev/dce` display flip counter (does not touch the game) |
| Running game | `sceSystemServiceGetAppIdOfRunningBigApp` + `/mnt/sandbox/pfsmnt` |
| System language | `sceSystemServiceParamGetInt` (for `lang = auto`) |

It **changes nothing**: it never writes the fan threshold and never injects into the game process. It only reads.

## How the verdict is made (short)

1. **Absolute rules**, from the first session: if the fan is high **and** the temperature is still high, cooling is insufficient.
   Default thresholds: WATCH 60% / 76 °C · CLEAN 70% / 80 °C · SERVICE 85% / 86 °C.
2. **Comparison with a baseline**: the first 5 sessions form the baseline. Each session gets a *cooling demand index*:
   `CDI = T_soc + 0.30·(fan% − 40) − 0.25·(W − 150)`. A console that does the same work hotter and with a faster fan gets a higher CDI. Against the baseline, +4 °C is WATCH, +8 °C is CLEAN and +14 °C is SERVICE.
3. **Trend**: with 6+ sessions over 14+ days, the monthly slope of the CDI is computed. +2 °C per month or more raises a warning.

After cleaning, the **"New baseline"** button in the web UI resets the comparison. Details: [docs/ANALYSIS.md](docs/ANALYSIS.md).

Verdict levels: **OK** → **WATCH** → **CLEAN** (dust cleaning) → **SERVICE** (cleaning plus a service check of the thermal interface / liquid metal).

## Setup

1. Get the ELF: on Windows run `powershell -ExecutionPolicy Bypass -File .\tools\build.ps1` (SDK + portable clang, no WSL needed), use the [GitHub Actions artifact](.github/workflows/build.yml), or run `make` under Linux/WSL. See [docs/INSTALL.md](docs/INSTALL.md).
2. Make sure the PS5 is jailbroken and etaHEN (or elfldr) is running.
3. Send the payload:
   ```powershell
   powershell -ExecutionPolicy Bypass -File .\tools\send_payload.ps1 -Ip 192.168.1.50 -Elf .\ps5-thermal-doctor.elf -Watch
   ```
   On Linux: `nc -q0 192.168.1.50 9021 < ps5-thermal-doctor.elf`
4. The PS5 shows a "PS5 Thermal Doctor started · http://IP:8088" notification. Open that address in a phone or PC browser.
5. Start a game and play for at least **15 minutes**. A summary notification arrives when the game closes.

The payload has to be sent again after every console restart. etaHEN can start it automatically: copy the ELF to `/data/etaHEN/payloads/` and enable auto-start in the Toolbox.

## Language

| Where | How the language is chosen |
|---|---|
| Web UI | The **EN / TR** buttons in the header. The first visit follows the browser language; the choice is remembered in the browser. |
| PS5 notifications, log file, payload output | The `lang` key in `config.ini`: `auto` (PS5 system language, Turkish → Turkish, anything else → English), `en` or `tr`. |
| JSON API | `?lang=en` or `?lang=tr` on `/api/status` and `/api/log`. Without it, the console language is used. |
| `config.ini` | Every key has a comment line in both languages. |

The in-memory log keeps every line in both languages, so switching the web UI language also switches the log view.

## Files on the console

```
/data/ps5td/config.ini      settings (editable from the web UI)
/data/ps5td/history.csv     session summaries
/data/ps5td/sessions/*.csv  raw data per session, every 5 s
/data/ps5td/state.ini       baseline counter
/data/ps5td/ps5td.log       log
```

## Project layout

```
src/analysis.c       diagnosis logic (portable, tested on a PC)
src/i18n.c           language selection (en / tr)
src/log.c            bilingual log (ring buffer + file)
src/platform.c       PS5 hardware access (sensors, dce, notifications, system language)
src/sampler.c        1 Hz sampling, session handling, CSV
src/http.c           web server + JSON API
web/index.html       web UI (embedded into the ELF)
tools/build.ps1      Windows build without WSL
tools/send_payload.ps1   sends the ELF to elfldr
tools/devserver.py   fake server for trying the UI on a PC
test/                PC tests and syntax check (test\host-check.ps1)
```

## Development

```powershell
powershell -ExecutionPolicy Bypass -File .\test\host-check.ps1   # diagnosis tests + syntax check (MinGW gcc)
python tools\devserver.py                                        # http://localhost:8088, healthy console
$env:PS5TD_SIM="dusty"; python tools\devserver.py                 # dusty console scenario
$env:PS5TD_LANG="tr"; python tools\devserver.py                   # simulate a Turkish console
```

On Linux: `make test`.

## Changes in 0.2.0

- Bilingual (English / Turkish) UI, notifications, log, config and API (`lang` setting, `?lang=`).
- The CPU power rail is counted as live only when it really goes up and down; on FW 11.60 it is a peak-hold counter and used to inflate the total power.
- Fixed live charts growing taller on every redraw on HiDPI screens.

## Credits / sources

The API details were learned from these open source projects: [etaHEN](https://github.com/etaHEN/etaHEN), [OnionHEN](https://github.com/aydencharles/onionHEN), [PS5 Cooling & System Center](https://github.com/strongt1me/ps5-cooling-system-center-pro), [ps5-fan-control](https://github.com/owendswang/ps5-fan-control), [PS5 Overlay KUS](https://github.com/smoxa/ps5-new-overlay), PHU Games Tools (ArkSama) and [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk). License: GPL-3.0-or-later ([LICENSE](LICENSE)).
