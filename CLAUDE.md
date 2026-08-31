# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

See also: `README.md` (usage, setup-screen commands), `CHANGELOG.md`. This repo is derived from the [md-microfirmware-template](https://github.com/sidecartridge/md-microfirmware-template); public build/usage docs are at <https://docs.sidecartridge.com/sidecartridge-multidevice/programming/>.

## What this repo is

**Real Time Clock (RTC) emulator** microfirmware app for the Sidecartridge Multi-device targeting Atari ST / STE / MegaST(E). A UF2 image runs on a Raspberry Pi Pico W (RP2040) plugged into the cartridge slot, emulating a ROM cartridge for the Atari. The app fetches the current time from an NTP server over Wi-Fi and serves it to the Atari, emulating either the native `SIDECART` RTC or a `DS1307`-compatible interface, with an optional Y2K patch for older TOS versions. On boot the Atari shows a setup screen for 5 seconds (NTP host/port, UTC offset, Y2K patch, RTC type), then starts the emulation; it survives warm resets and keeps updating the system clock.

## Build

Top-level build is driven by `build.sh` in the repo root:

```bash
# <board_type> = pico | pico_w | sidecartos_16mb   (this app targets pico_w)
# <build_type> = debug | release   (note: CMake always builds Debug — see below)
# <app_uuid_key> = UUID4 identifying this app, must match desc/app.json
./build.sh pico_w release 123e4567-e89b-12d3-a456-426614174000
```

Required host environment:
- ARM GNU Toolchain 14.2 — export `PICO_TOOLCHAIN_PATH` to its `arm-none-eabi/bin` dir.
- `atarist-toolkit-docker` (`stcmd`) — needed for the m68k target. `stcmd` requires a PTY (`pty=true`).
- SDK paths (auto-set from the repo if unset): `PICO_SDK_PATH`, `PICO_EXTRAS_PATH`, `FATFS_SDK_PATH`.

Build flow (orchestrated by `build.sh`):
1. Copies `version.txt` into `rp/` and `target/atarist/`.
2. Builds the Atari ST target (`target/atarist/build.sh`) via `stcmd make`. `BOOT.BIN` is copied to `dist/FIRMWARE.IMG`, checked to be ≤ 64 KB, padded to 64 KB (the full shared region), and converted by `firmware.py` into `rp/src/include/target_firmware.h` (a C byte array embedded in the RP firmware).
3. Builds the RP firmware (`rp/build.sh`): pins submodule versions (pico-sdk 2.1.1, pico-extras sdk-2.1.1, fatfs-sdk v3.6.2), runs CMake, produces `rp/dist/rp-<board>.uf2`.
4. Computes MD5, renames to `dist/<APP_UUID>-<VERSION>.uf2`, and substitutes UUID/MD5/version into `dist/<APP_UUID>.json` from the `desc/app.json` template.

### Build gotchas
- **CMake always builds with `-DCMAKE_BUILD_TYPE=Debug`** regardless of the `<build_type>` argument (the `$BUILD_TYPE` line is left commented in `rp/build.sh`). `<build_type>` only controls the `DEBUG_MODE` macro (serial debug output) and the dist filename.
- `CHARACTER_GAP_MS` must remain defined (700) in `rp/src/include/blink.h` — removing it breaks the RP build.
- Harmless VASM warnings during the m68k build (`target data type overflow`, `trailing garbage after option -D`) can be ignored.
- VASM/`stcmd` errors like `the input device is not a TTY` mean `stcmd` was invoked without a PTY. Unlike the template, `target/atarist/build.sh` here does **not** export `STCMD_NO_TTY=1` — export it yourself when invoking from a non-TTY context (CI, sub-shells, build wrappers). Without it the m68k build can fail silently and the previous `BOOT.BIN` survives — leading to a working RP firmware that displays garbage on the ST because `target_firmware.h` is stale.

### CI / release
- `.github/workflows/build.yml` builds `pico_w` Release on PR.
- `.github/workflows/release.yml` triggers on `v*` tags: builds, attaches UF2 + JSON to the GitHub Release, uploads to `s3://atarist.sidecartridge.com/`.
- `make tag` tags HEAD with the contents of `version.txt` and pushes the tag (which triggers release).

### Tests
There is no test suite. "Verification" is: build succeeds, UF2 boots on hardware, manual interaction over the serial debug console.

## Architecture

The firmware is a **two-target build**: m68k assembly that runs on the Atari ST is compiled into a ROM image, embedded as a C array inside the RP2040 firmware, and served back to the Atari over the cartridge bus that the RP2040 emulates via PIO + DMA.

### Atari ST side (`target/atarist/`)
- `src/main.s` — all the m68k code (there is no `userfw.s` in this app). Cartridge boot, setup-screen terminal, and the RTC client: it saves/hooks the XBIOS vectors, reads date/time from the shared region, applies it to the system clock, and implements the reentry lock so hooked XBIOS calls don't recurse. Lives at `$FA0000` (ROM4 cartridge region).
- Commands to the RP2040 are issued by accessing addresses in the **ROM3 window** (`ROMCMD_START_ADDR = $FB0000`) tagged with `CMD_MAGIC_NUMBER = $ABCD`; app command codes live under `APP_RTCEMUL = $0300` (`CMD_TEST_NTP`, `CMD_READ_DATETME`, `CMD_SAVE_VECTORS`, `CMD_REENTRY_LOCK/UNLOCK`, `CMD_SET_SHARED_VAR`). The RP decodes these in its DMA IRQ handler via `tprotocol_parse` (see `rp/src/include/tprotocol.h`).
- Built via `stcmd make release` (m68k assembler in Docker); the image is padded to 64 KB and converted to `target_firmware.h` for inclusion in the RP build.

### Shared 64 KB cartridge region
The Atari ST sees a 64 KB window at `$FA0000`–`$FAFFFF` (mirrored RP-side at `0x20030000`). Both sides derive every offset symbolically — RP-side from `rp/src/include/` headers, m68k side from `equ`s in `main.s`. **Never hard-code an address inside this region** — always reference the named offset/symbol. Note this layout differs from the current template:

| Offset | Symbol | Size | Purpose |
| --- | --- | --- | --- |
| `$FA0000` | `ROM4_ADDR` | 64 KB | cartridge image (`BOOT.BIN`, padded) |
| `$FA1000` | `TRANSTABLE` | — | translation table for high-resolution rendering |
| `$FA8000` | `FRAMEBUFFER_ADDR` | 8000 B | 320×200 monochrome framebuffer for the setup terminal |
| `$FAF000` | `RANDOM_TOKEN_ADDR` | 4 B + 4 B seed | command-response token + seed |
| `$FAF008` | `RTCEMUL_NTP_SUCCESS`, `RTCEMUL_DATETIME_BCD`, `RTCEMUL_DATETIME_MSDOS`, … | — | RTC exchange area, offsets chained from the token |
| `$FAF040` | `SHARED_VARIABLES` | 16 × 4 B | indexed shared variables |
| `$FB0000` | `ROMCMD_START_ADDR` | (ROM3) | m68k → RP command channel |

### RP2040 side (`rp/src/`)
- `main.c` — only sets clock/voltage (overclocked for the PIO bus emulation), calls `gconfig_init` (global config) then `aconfig_init` (per-app config), and hands off to `emul_start()`. If config init fails it jumps to the **Booster** app via `reset_jump_to_booster()`. **Don't add features to `main.c`** — put them in `emul.c` or `rtc.c`.
- `emul.c` / `emul.h` — the application's main loop: a state machine covering `APP_MODE_SETUP` (terminal setup screen with the 5 s countdown), `APP_EMULATION_INIT` (Wi-Fi up, `rtc_queryNTPTime()`, then `rtc_postinit()` and `dma_setResponseCB(rtc_dma_irq_handler_lookup)`), and `APP_EMULATION_RUNTIME` (`rtc_loop()`).
- `rtc.c` / `include/rtc.h` — **the core module**: NTP client (lwIP UDP, DNS resolution), BCD/MS-DOS datetime conversion, `SIDECART` vs `DS1307` (Dallas) emulation, Y2K patch, and the DMA-IRQ protocol command handler.
- `romemul.c` / `romemul.pio` — PIO programs and runtime that emulate the cartridge ROM bus (GPIOs in `include/constants.h`).
- `gconfig.c` / `aconfig.c` — global vs per-app configuration stored in dedicated flash sectors, on top of `settings/` (a key-value store). App settings: NTP host/port, UTC offset, Y2K patch, RTC type.
- `network.c`, `httpc/` — Wi-Fi (CYW43, lwIP poll mode), HTTP client.
- `display.c`, `display_term.c`, `term.c`, `u8g2/` — terminal-style display rendered into the Atari framebuffer at `FRAMEBUFFER_ADDR`.
- `blink.c`, `select.c`, `reset.c` — LED Morse status; SELECT-button handling (polled on core 1 during setup — a long press erases flash config via `reset_deviceAndEraseFlash`; core 1 is disabled with `select_coreWaitPushDisable()` before emulation runtime); soft reset uses `watchdog_reboot` and jump-to-booster.
- There is **no SD-card code** in this app (`fatfs-sdk` is a pinned submodule but unused by `rp/src`).

### Memory layout (`rp/src/memmap_rp.ld`)
The RP2040's 2 MB flash is sliced into named regions, and code is responsible for not stomping on them:

| Region | Origin | Length | Purpose |
| --- | --- | --- | --- |
| `FLASH` | `0x10000000` | 1024 K | App code |
| `ROM_TEMP` | `0x10100000` | 128 K | Scratch area for loaded ROMs |
| `BOOSTER_APP_FLASH` | `0x10120000` | 768 K | Reserved for the Booster app (do not write from this app) |
| `CONFIG_FLASH` | `0x101E0000` | 120 K | 30 sectors of per-app config |
| `GLOBAL_LOOKUP_FLASH` | `0x101FE000` | 4 K | UUID → config-sector lookup |
| `GLOBAL_CONFIG_FLASH` | `0x101FF000` | 4 K | Global config |
| `RAM` | `0x20000000` | 128 K | Normal RAM |
| `ROM_IN_RAM` | `0x20020000` | 128 K | ROM data mirrored to RAM for fast bus access |

### App identity
`CURRENT_APP_UUID_KEY` (set from the `APP_UUID_KEY` env var at CMake time, with a placeholder default) is the app's UUID4. It must match the `uuid` field in `desc/app.json` and is used as the key into `GLOBAL_LOOKUP_FLASH` to find this app's config sector. Mismatch → app jumps to Booster.

## Editing guardrails

- **Never modify** `pico-sdk/`, `pico-extras/`, or `fatfs-sdk/` — they are git submodules pinned to specific upstream revisions, and the build re-pins them on every run.
- Don't touch `main.c` for feature work — start in `emul.c` or `rtc.c`.
- Match the existing C style (clang-format config in `.clang-format`, clang-tidy in `.clang-tidy` — both wired up via CMake when the binaries are on `PATH`).

---

## Working style

These behavioral guidelines bias toward caution over speed. For trivial tasks, use judgment.

### 1. Think before coding

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them — don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

### 2. Simplicity first

Minimum code that solves the problem. Nothing speculative.
- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

### 3. Surgical changes

Touch only what you must. Clean up only your own mess.
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it — don't delete it.
- When your changes orphan an import/variable/function, remove it. Don't remove pre-existing dead code unless asked.

The test: every changed line should trace directly to the user's request.

### 4. Goal-driven execution

Define success criteria. Loop until verified.
- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan with a verification check per step.

### 5. No AI attribution

Never add AI-tool attribution to commits, PR descriptions, code comments,
docs, or any other artifact. This means **no**:
- "Generated with Claude Code", "Co-authored by Claude", "Made with ChatGPT",
  or any similar phrasing.
- `Co-Authored-By: Claude …`, `Co-Authored-By: ChatGPT …`, or any other
  AI co-author trailer.
- "AI-assisted", "written with the help of an LLM", etc., as comments or
  changelog entries.

Write the message as the human author. Do not mention AI tools used to
produce the work.
