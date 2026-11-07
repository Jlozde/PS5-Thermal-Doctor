# Build and install

**English** · [Türkçe](INSTALL.tr.md)

The quickest way is to download a ready `ps5-thermal-doctor.elf` from the [Releases](https://github.com/Jlozde/PS5-Thermal-Doctor/releases/latest) page and skip to [Sending to the PS5](#sending-to-the-ps5).

To build it yourself there are three ways. On Windows the easiest is **A**: unpack two zip files, no admin rights and no WSL needed.

## A) Windows without WSL (`tools\build.ps1`)

Both requirements are portable (nothing to install):

1. The **ps5-payload-sdk** zip: `https://github.com/ps5-payload-dev/sdk/releases/latest/download/ps5-payload-sdk.zip`.
   Unpack it under `%LOCALAPPDATA%\ps5-toolchain\sdk\`. The file `...\sdk\ps5-payload-sdk\win\prospero-lld.exe` must exist; the SDK's official Windows wrappers live there.
2. **clang + lld**: `llvm-mingw-<date>-ucrt-x86_64.zip` (about 180 MB) from the [llvm-mingw](https://github.com/mstorsjo/llvm-mingw/releases) releases page.
   Unpack it and rename the folder to `%LOCALAPPDATA%\ps5-toolchain\llvm`, so that `llvm\bin\clang.exe` exists.
   An installed LLVM works too (`winget install LLVM.LLVM`, version 18 or newer): pass `-LlvmBin "C:\Program Files\LLVM\bin"`.

```powershell
$t = "$env:LOCALAPPDATA\ps5-toolchain"; New-Item -ItemType Directory -Force "$t\dl" | Out-Null
Invoke-WebRequest https://github.com/ps5-payload-dev/sdk/releases/latest/download/ps5-payload-sdk.zip -OutFile "$t\dl\sdk.zip"
Expand-Archive "$t\dl\sdk.zip" "$t\sdk"
# download the llvm-mingw zip too and unpack it as "$t\llvm", then:
powershell -ExecutionPolicy Bypass -File .\tools\build.ps1        # -> ps5-thermal-doctor.elf
```

- Other locations: `-Sdk <path>` / `-LlvmBin <path>`, or the `PS5_PAYLOAD_SDK` / `PS5_LLVM_BIN` environment variables.
- `-ShowCommands` prints the commands it runs. `-NoStrip` keeps the symbols; combine it with `-Out build\debug.elf` to write a separate file.
- The script uses the same flags as the Makefile and the logic of the SDK's `bin/prospero-clang` wrapper. With clang 20+ the driver adds the crt objects itself (`SCE_PROSPERO_SDK_DIR`).
- If the project is on a network share (`\\server\...`), PowerShell blocks unsigned scripts with "not digitally signed". That is why the `powershell -ExecutionPolicy Bypass -File ...` form is used. A permanent fix is `Set-ExecutionPolicy -Scope CurrentUser Bypass`; that security choice is yours.
- Tested combination: SDK v0.45 (2026-10-21) + llvm-mingw 20261104 (clang 23.1.5), giving a ~260 KB ELF.

## B) GitHub Actions

1. Push this folder to a GitHub repository.
2. The "Build PS5 payload" workflow runs in the Actions tab. When it finishes, download `ps5-thermal-doctor.elf` from **Artifacts → ps5-thermal-doctor**.
3. The workflow also runs the PC tests (`make test`).
4. Pushing a tag such as `v0.2.0` publishes a GitHub Release with the ELF, `SHA256SUMS.txt` and the notes from `.github/release-notes/<tag>.md`. The tag must match `PS5TD_VERSION` in `src/ps5td.h`.

## C) WSL (Windows) or Linux

```powershell
wsl --install -d Ubuntu          # once; needs admin rights and a restart
```

Inside Ubuntu:

```bash
sudo apt-get update
sudo apt-get install -y bash clang-18 lld-18 wget unzip make python3
wget https://github.com/ps5-payload-dev/sdk/releases/latest/download/ps5-payload-sdk.zip
sudo unzip -d /opt ps5-payload-sdk.zip
echo 'export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk' >> ~/.bashrc && source ~/.bashrc

cd "/mnt/<project path>"          # for a network share: sudo mkdir /mnt/ps5 && sudo mount -t drvfs '\\server\share\ps5-thermal-doctor' /mnt/ps5
make                              # -> ps5-thermal-doctor.elf
make test                         # PC tests
make deploy PS5_HOST=192.168.1.50 # send to elfldr (port 9021)
```

SDK v0.45 contains kernel offsets for firmware 3.00–14.10. On a firmware the SDK does not know, the payload exits silently before reaching `main()`. For a new firmware, update the SDK.

## Sending to the PS5

- The console must be jailbroken with etaHEN (or just elfldr) loaded.
- PowerShell:
  ```powershell
  powershell -ExecutionPolicy Bypass -File .\tools\send_payload.ps1 -Ip <PS5 IP> -Elf .\ps5-thermal-doctor.elf -Watch
  ```
  `-Watch` shows the payload's log output live; press Ctrl+C to quit, the payload keeps running. `-WatchSeconds 60` listens for that long and then quits on its own.
- The SDK's own tool works too: `<sdk>\win\prospero-deploy.cmd -h <PS5 IP> ps5-thermal-doctor.elf`
- Linux/macOS: `nc -q0 <PS5 IP> 9021 < ps5-thermal-doctor.elf`
- On success the screen shows a "PS5 Thermal Doctor 0.2.0 started · http://<ip>:8088" notification.

If the payload is sent a second time, the old instance is stopped automatically: its pid is found through the port and it is terminated.

## Language

- Web UI: the language menu in the header. English, Turkish, German, French, Spanish, Russian, Korean and Simplified Chinese are available.
- PS5 notifications: `lang = auto | en | tr | de | fr | es | ru | ko | zh` in `/data/ps5td/config.ini`, also editable in the Settings tab of the web UI. `auto` follows the PS5 system language; a language the app does not have falls back to English.
- The log and payload output are in Turkish when the console language is Turkish, otherwise in English.
- The startup log prints the chosen language, for example `Language: en (config auto, PS5 system language code 1).`

## Temperature unit

- Celsius by default. Choose Fahrenheit in the Settings tab, or set `temp_unit = f` in `config.ini`.
- The unit changes the web UI and the PS5 notifications. The log and the thresholds in `config.ini` always stay in °C; the Settings tab converts them for display.

## Home-screen tile

- When the payload starts and the tile is missing, it adds **PS5 Thermal Doctor** to the media row of the PS5 home screen, next to the other apps. Selecting it opens `http://127.0.0.1:8088` (the web UI port) in the PS5 browser. The tile holds no program: the payload still has to be sent after every console restart, as before.
- How: the files `param.json` (with `"applicationCategoryType": 65536`, the media row) and `icon0.png` are written to `/user/app/FAKE08088/sce_sys/`, and the system is asked to register that folder with `sceAppInstUtilAppInstallTitleDir`. This is the same way websrv, ShadowMountPlus and other homebrew launchers add their tiles. The batch call `sceAppInstUtilAppInstallAll` is never used, because it would also register other titles waiting under `/user/app`.
- The result is shown at the bottom of the Maintenance card in the Settings tab and in the log.
- To turn it off, clear **Home screen tile** in the Settings tab (`home_tile = 0`), then delete the tile on the PS5 (Options → Delete). With the setting on, a deleted tile comes back at the next start.
- If the tile is out of date, for example after `http_port` was changed or when it was made by an older version, the payload writes its files again and registers it again at the next start. No notification is shown for that.
- Firmware 12.00 and later no longer offer the per-title call. There the payload skips the tile and says so.

## Auto-start (etaHEN)

Copy the ELF to `/data/etaHEN/payloads/` (FTP on port 1337). In etaHEN Toolbox → Payloads, pick the file and enable **auto-start**; etaHEN then loads it at every boot. It only has to be loaded before you start a game. It can also be loaded during a game; that session is then recorded partially.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| No notification, port not open | The SDK does not know this firmware. Check the output with `-Watch`; no output at all means the payload never reached main. |
| Fan shows `–` | `sceKernelGetCurrentFanDuty` is missing on this firmware; the log says "FanDuty:MISSING". |
| FPS shows `–` | `/dev/dce` could not be opened (errno in the log). If privilege escalation is off, try `escalate = 1`. |
| Power shows `–` | `sceKernelGetSocPowerConsumption` is missing or has an unexpected layout (see the log). |
| No tile on the home screen | Check the line at the bottom of the Maintenance card and the log. A code means the system refused it; it is tried again at the next start. If privilege escalation is off, try `escalate = 1`. |
| Game shows "none" while a game is open | `sceSystemServiceGetAppIdOfRunningBigApp` does not answer. If the pfsmnt fallback is empty too, share the log. |
| Web UI does not open | Are you on the same network as the PS5? The port may have been changed in `config.ini`. |
| Texts in the wrong language | Web UI: use the language menu in the header. Notifications: set **Console language** in the Settings tab (for example `lang = de`). |

Log: `http://<ip>:8088/api/log?lang=en` or `/data/ps5td/ps5td.log`.
