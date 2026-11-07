# Diagnosis logic

**English** · [Türkçe](ANALYSIS.tr.md)

This document explains what `src/analysis.c` does and why. Every threshold can be changed in `config.ini`.

## Physics

The chip temperature is roughly:

```
T_chip = T_ambient + P × R_th(fan)
```

- `P` is the power the chip draws, in watts. The heavier the game, the higher it is.
- `R_th` is the thermal resistance. It drops as the fan speeds up. It rises with **dust** (clogged heatsink fins) and with a **degraded thermal interface** (liquid metal that has moved or oxidised).

The PS5 fan controller tries to hold the temperature steady, so it spins the fan up as cooling gets worse. Looking at the temperature alone is therefore misleading. **Fan, temperature and power** have to be read together.

## Session

A session starts when a game appears in the foreground and ends when no game has been seen for 15 s. The first `warmup_s` (600 s) are skipped as warm-up; later samples go into the **steady-state** average. With less than `min_ss_s` (300 s) of steady data the session counts as "not enough data", but its peak values are still recorded.

Summary of each session:

| Field | Meaning |
|---|---|
| `ss_soc`, `ss_cpu` | steady-state average SoC temperature (hottest of channels 1–7) and CPU temperature |
| `ss_fan` | steady-state fan percentage |
| `ss_power` | steady-state total power (GPU + memory + the CPU rail when it is live) |
| `ss_fps` | average display flip rate |
| `warm_rate` | warm-up rate over the first 5 minutes, °C/min |
| `hot_share` | share of time with fan ≥ 80% **and** SoC ≥ 80 °C |
| `danger_s` | seconds spent at SoC ≥ `hot_soc_c` (95 °C) |
| `cdi` | cooling demand index (below) |

## 1. Absolute rules

These work from the first session. The idea: a high temperature with a **low** fan is normal, because the firmware has not spun the fan up yet. If the temperature is still high with the fan **high**, the system is using all the cooling it has and it is not enough.

| Verdict | Condition (defaults) |
|---|---|
| SERVICE | fan ≥ 85% **and** SoC ≥ 86 °C; **or** 60 s+ at SoC ≥ 95 °C; **or** SoC ≥ 70 °C with the fan below 5% (fan not spinning) |
| CLEAN | fan ≥ 70% **and** SoC ≥ 80 °C; **or** fan ≥ 80% and SoC ≥ 80 °C for at least half of the session |
| WATCH | fan ≥ 60% **and** SoC ≥ 76 °C |
| OK | anything else |

These thresholds are first estimates based on community measurements: a healthy PS5 runs at SoC 65–78 °C and fan 30–45% in games. Tune them in `config.ini` once a few sessions from your own console are recorded.

## 2. Comparison with a baseline (CDI)

```
CDI = T_soc + k_fan · (fan% − 40) − k_pow · (P − 150 W)
      k_fan = 0.30 °C per %      k_pow = 0.25 °C per W
```

- The fan term: if the console holds the same temperature with a faster fan, it is "working harder", so the CDI rises.
- The power term: a heavy game (more watts) is naturally hotter, so it is subtracted to normalise. If power cannot be read, the term is skipped; comparing similar games is then more accurate.

The first `baseline_sessions` (5) steady sessions are the **baseline**; their mean and standard deviation are stored. "Now" is the mean CDI of the last `recent_sessions` (3) sessions after the baseline.

| Drift (now − baseline) | Verdict |
|---|---|
| ≥ +14 °C | SERVICE |
| ≥ +8 °C | CLEAN |
| ≥ +4 °C | WATCH |

The coefficients are first estimates too. They can be measured on the console: the temperature difference divided by the fan difference in the same game with different fan thresholds (etaHEN fan setting) gives `k_fan`; two games with different power give `k_pow`.

**Baseline counter (epoch):** the "New baseline" button increments the counter. Older sessions stay in the file but are left out of the comparison, and the new baseline is built from the next 5 sessions. Always use it after cleaning or service; otherwise the improvement does not show up as drift and the baseline stays dirty.

## 3. Trend

With 6+ steady sessions over 14+ days, the least-squares slope of the CDI over time is computed (°C per 30 days). +2 °C per month or more raises the verdict to at least WATCH, because dust builds up slowly over months.

## Final verdict

`max(absolute, relative, trend)`. The web UI shows the reason for each one separately.

## Why FPS is not a criterion

A hot PS5 does not lower its FPS: it spins the fan up and, if that is not enough, shuts down. FPS is only recorded as **load context**: the same game locked at 60 fps and in a 120 fps mode draws different power. The `/dev/dce` flip counter follows V-sync and shows 60 at 60 Hz.

## Language of the texts

Every reason, label and piece of advice is produced in both English and Turkish at the same time (`an_overall` fills both). The API returns the language asked for with `?lang=`. Notifications and the log use the `lang` setting.

## Known limits

- The ambient temperature cannot be measured. A session recorded in a 32 °C room in summer is naturally a few degrees hotter than one recorded in winter, so WATCH alone is not an alarm.
- The CPU power rail may not be a live reading. Some consoles return a fixed budget value. On FW 11.60 it is a peak-hold counter: it resets at game start, then only rises and stays high after the game closes. The code counts the rail as live only if it both rose at least twice and fell at least twice in the last 60 s; otherwise it is left out of the total and marked with `*` in the UI.
- The memory rails (4–7) always read 0 on the tested console. In practice the total power is the GPU rail.
- SoC channel 0 sometimes read as low as 17 °C and lagged behind, so the "chip temperature" is the hottest of channels 1–7. Channel 7 is the sensor closest to the GPU side.
- Thresholds may differ on the PS5 Slim and Pro; the Pro runs a hotter SoC and draws more power.
