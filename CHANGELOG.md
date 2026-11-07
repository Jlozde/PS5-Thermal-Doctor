# Changelog

**English** · [Türkçe](CHANGELOG.tr.md)

Downloads are on the [Releases](https://github.com/Jlozde/PS5-Thermal-Doctor/releases) page.

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
