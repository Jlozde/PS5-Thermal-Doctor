# Build and install

**English** · [Türkçe](INSTALL.tr.md)

There are three ways to get the ELF. On Windows the easiest is **A**: unpack two zip files, no admin rights and no WSL needed.

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
- Tested combination: SDK v0.45 (2025-10-21) + llvm-mingw 20261104 (clang 23.1.5), giving a ~165 KB ELF.

## B) GitHub Actions

1. Push this folder to a GitHub repository.
2. The "Build PS5 payload" workflow runs in the Actions tab. When it finishes, download `ps5-thermal-doctor.elf` from **Artifacts → ps5-thermal-doctor**.
3. The workflow also runs the PC tests (`make test`).

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

- Web UI: the **EN / TR** buttons in the header.
- Notifications, log and payload output: `lang = auto | en | tr` in `/data/ps5td/config.ini`, also editable in the web UI under "Maintenance and settings". `auto` follows the PS5 system language.
- The startup log prints the chosen language, for example `Language: en (config auto, PS5 system language code 1).`

## Auto-start (etaHEN)

Copy the ELF to `/data/etaHEN/payloads/` (FTP on port 1337). In etaHEN Toolbox → Payloads, pick the file and enable **auto-start**; etaHEN then loads it at every boot. It only has to be loaded before you start a game. It can also be loaded during a game; that session is then recorded partially.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| No notification, port not open | The SDK does not know this firmware. Check the output with `-Watch`; no output at all means the payload never reached main. |
| Fan shows `–` | `sceKernelGetCurrentFanDuty` is missing on this firmware; the log says "FanDuty:MISSING". |
| FPS shows `–` | `/dev/dce` could not be opened (errno in the log). If privilege escalation is off, try `escalate = 1`. |
| Power shows `–` | `sceKernelGetSocPowerConsumption` is missing or has an unexpected layout (see the log). |
| Game shows "none" while a game is open | `sceSystemServiceGetAppIdOfRunningBigApp` does not answer. If the pfsmnt fallback is empty too, share the log. |
| Web UI does not open | Are you on the same network as the PS5? The port may have been changed in `config.ini`. |
| Texts in the wrong language | Web UI: use the EN / TR buttons. Notifications: set `lang = en` or `lang = tr`. |

Log: `http://<ip>:8088/api/log?lang=en` or `/data/ps5td/ps5td.log`.
