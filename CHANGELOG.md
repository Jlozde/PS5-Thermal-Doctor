# Changelog

**English** · [Türkçe](CHANGELOG.tr.md)

Downloads are on the [Releases](https://github.com/Jlozde/PS5-Thermal-Doctor/releases) page.

## 0.2.3 (2026-11-07)

### Added
- **Logo.** A fan with a pulse line across it, in the colours of the web UI. It is in the README, in the browser tab and in the web UI header.
- **Home-screen tile.** When the payload starts and the tile is missing, it adds PS5 Thermal Doctor to the media row of the PS5 home screen. The tile opens the web UI in the PS5 browser. An out-of-date tile (another port, an older version) is registered again at the next start. It is registered with `sceAppInstUtilAppInstallTitleDir`, like other homebrew launchers; the batch install call is never used.
- `home_tile = 1 | 0` in `config.ini` and in the Settings tab (on by default). The Maintenance card shows the tile state.
- `tile`, `tile_code`, `tile_port` and `tile_want` fields in `/api/status`; `/icon.png` serves the icon.

### Changed
- The ELF links `libSceAppInstUtil` and embeds the 512 × 512 icon (about 40 KB).

## 0.2.2 (2026-11-07)

### Added
- **Six more app languages:** German, French, Spanish, Russian, Korean and Simplified Chinese, next to English and Turkish. They cover the web UI, the verdicts, reasons and advice, and the PS5 notifications.
- `lang = de | fr | es | ru | ko | zh` in `config.ini` and in the Settings tab. `lang = auto` now maps these PS5 system languages too; any other system language uses English.
- `?lang=` on the web UI and the API accepts the new codes.
- `tools/i18n_texts.py` holds every app text in one table; `tools/gen_i18n.py` generates `src/texts.h` and the web UI text table from it and checks that every translation takes the same values as the English text. CI runs the check.

### Changed
- The **EN / TR** buttons in the web UI header became a language menu.
- Percentages and durations follow the chosen language's style (`38 %` in German and French, `%38` in Turkish).
- The log and the `config.ini` comments stay in English and Turkish. With another language the log is shown in English, and the Settings tab says so.

## 0.2.1 (2026-11-07)

### Added
- **Settings tab** in the web UI. Every option has its own field with a short explanation, grouped as general, PS5 notifications, measurement, sessions, verdict thresholds and baseline comparison. Changes are marked until saved and can be discarded. Out-of-range values are flagged before saving.
- **Temperature unit:** `temp_unit = c | f`. Celsius is the default; Fahrenheit changes the web UI and the PS5 notifications. Values, thresholds and the log stay in °C.
- `temp_unit` field in `/api/status`.

### Changed
- The web UI has two tabs: Overview and Settings. Maintenance actions and the log moved to the Settings tab.
- The raw `config.ini` text box was removed from the web UI. The file can still be edited by hand.
- Saving from the web UI sends only the changed keys; the payload merges them into its settings.

## 0.2.0 (2026-11-07)

### Added
- Bilingual English / Turkish text everywhere a user looks: web UI, PS5 notifications, log, `config.ini` comments, API texts and tool output.
- `lang = auto | en | tr` setting. `auto` follows the PS5 system language.
- EN / TR switch in the web UI, remembered per browser. The address also accepts `?lang=en` or `?lang=tr`.
- `?lang=` on `/api/status` and `/api/log`, plus the `lang`, `console_lang`, `config_lang` and `overall.badge` fields.
- Release workflow: pushing a `v*` tag builds the ELF and publishes a GitHub Release.
- Screenshots and a "how it works" diagram in the README.
- A disclaimer in the README, the release notes and the web UI footer: verdicts are estimates, not a guarantee.

### Changed
- Verdict codes in the API are English: `OK`, `WATCH`, `CLEAN`, `SERVICE`, `NO_DATA`.
- CI pins ps5-payload-sdk v0.45 and runs the host tests before the build.

### Fixed
- The CPU power rail is counted only when it really rises and falls. On firmware 11.60 it is a peak-hold counter and used to inflate the total power.
- Live charts kept growing taller on every redraw on high-DPI screens.
- A stale settings write-back in the sampling loop could undo a settings change made from the web UI.
- Turkish characters in PowerShell 5.1 output were garbled; the scripts now carry a UTF-8 BOM.

## 0.1.0 (2026-11-07)

First working version, run on a PS5 with firmware 11.60. It was never published as a release.

- 1 Hz sampling of CPU and SoC temperature, fan speed, power rails and FPS.
- Game sessions with warm-up handling, steady-state summary and per-session CSV files.
- Verdict from absolute rules, the cooling demand index against a baseline, and the long-term trend.
- Web UI on port 8088 with live charts and session history, plus PS5 notifications.
- Windows build without WSL, payload sender, fake devserver and PC tests.
