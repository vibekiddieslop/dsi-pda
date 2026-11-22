# DSi PDA Launcher: Implementation Plan

> The original plan, kept for its research (chainloading, the Unlaunch
> fallback). The code has moved on since: the file layout and the screens
> described here are not the current ones. See `README.md` for those.

## Context

The goal is a DSi homebrew PDA (to-do, notes, calendar, timers) that Unlaunch boots on power-on. A "Games / Apps" block hands off to TWiLight Menu++ (`sd:/BOOT.NDS`) or another `.nds`. The build machine is Windows with WSL Ubuntu 24.04.

Decisions from the clarifying questions:
- DSi dumps for melonDS come later. Phases 0 to 4 test in melonDS DS mode with a DLDI SD folder (`fat:/`). DSi-mode tests start once the dumps exist.
- An app that exits returns to `BOOT.NDS`, which is standard hbmenu bootstub behavior.
- Calendar events support simple repeat rules (daily, weekly, monthly, yearly).

Facts verified against source:
- BlocksDS `fat_stat()` sets `st_ino = fno.fclust`, the file's first cluster (`libnds/source/arm9/libc/fat_device.c:245`). The hbmenu loader needs exactly this value.
- BlocksDS `exit()` calls `__libnds_exit()`. When a bootstub with `BOOTSIG` ("bootstub") sits at `fake_heap_end` (DSi: `0x02FF4000`), it calls `bootstub->arm9reboot`. Otherwise the console powers off. Unlaunch installs no bootstub, so the PDA must install its own.
- The devkitPro `nds-bootloader` loads `storedFileCluster`. With cluster 0 it looks up `BOOT.NDS` by name. A `dsiSD` flag selects the DSi SD driver over DLDI.
- `nds-hb-menu` v0.9.0 `source/nds_loader_arm9.c` holds `installBootStub()` and `runNdsFile()` written against classic libnds. Newer tags (v0.10+) use calico and do not fit BlocksDS. The license is GPLv2 or later.
- Unlaunch passes a device list (40-character paths, 8.3 names) in place of argv. BlocksDS parses it.
- TWiLight's `unlaunchboot.cpp` documents the Unlaunch `AutoLoadInfo` block at `0x02000800`. This is the fallback launch method.

## Toolchain and environment (Phase 0)

- Install BlocksDS in WSL Ubuntu 24.04 through the Wonderful toolchain: `wf-pacman -S blocksds-toolchain`. Then set `BLOCKSDS=/opt/wonderful/thirdparty/blocksds/core`.
- Build from the repo folder inside WSL with `make`. Edit the sources from Windows.
- Use the latest melonDS Windows release from the official site. Set DS mode, enable DLDI, and sync the SD image to the repo's `sdroot\` folder.
- `tools/deploy.ps1` copies `pda.nds` and the `pda/` seed files into `sdroot\` or onto a real SD card drive letter.

## Project structure

```
DsiLauncher/
  Makefile                 from the BlocksDS ARM9-only template (default ARM7)
  icon.bmp                 32x32 banner icon
  source/
    main.c                 init, main loop, screen stack
    ui/ui.c, ui.h          block grid, hit testing, top-screen text, dirty redraw
    ui/kbd.c               wrapper around libnds Keyboard (show, hide, read line)
    ui/editor.c            line editor shared by To-Do, Notes, Calendar
    screens/home.c todo.c notes.c calendar.c timers.c apps.c
    store/storage.c        root drive detection, atomic save, .bak recovery
    store/todo_io.c notes_io.c cal_io.c alarm_io.c ini.c    parsers and writers
    launch/launch.c        bootstub install, cluster patch, exit
    platform/clock.c sound.c   RTC wrappers, PSG beeps
  third_party/
    nds-bootloader/        vendored devkitPro source at a pre-calico commit, builds load.bin
    hbmenu/bootstub.s      from nds-hb-menu v0.9.0
    hbmenu/nds_loader.c    installBootStub() port from v0.9.0
  data/                    generated load.bin and bootstub.bin, embedded by bin2o
  graphics/                grit PNGs: paper background, block tiles, 8x8 font
  tests/host/              native gcc tests for every parser and writer
  tools/deploy.ps1, make_testboot/   test ROM that prints argv, used as a fake BOOT.NDS
  sdroot/                  melonDS SD folder, ignored by git
```

The parsers in `store/` hold no libnds calls. WSL gcc compiles them natively for `tests/host/`.

## UI architecture

- The main engine drives the top screen. It holds a paper-texture bitmap on BG3 and a text console on BG0 with a custom 8x8 font.
- The sub engine drives the bottom screen. It holds a tiled grid of 64x64 blocks (4x3) on BG2, labels on BG0, and the libnds keyboard on its own layer. The keyboard stays hidden until an edit starts.
- Each screen is a struct with `enter`, `leave`, `update(keys, touch)`, `draw_top` and `draw_bottom`. A 4-deep stack handles push and pop. B pops, so every screen has a hardware-button exit.
- D-pad and A mirror touch on the block grid. Redraws run only when a dirty flag is set.

| Screen | Top screen | Bottom blocks |
|---|---|---|
| Home | clock, date, today's events, next alarm, first 3 open to-dos | To-Do, Notes, Calendar, Timers, Games, Apps |
| To-Do | scrolling list with cursor | Add, Done, Edit, Delete, Up, Down |
| Notes | note list, then note text with scroll | New, Open, Edit line, Add line, Delete |
| Calendar | month grid, then the selected day's events | Prev, Next, Today, Add, Delete |
| Timers | running countdowns and alarms | +1m, +5m, Start/Stop, New alarm, Toggle |
| Games | confirmation text for BOOT.NDS | Launch, Cancel |
| Apps | entries from `config.ini` | Up, Down, Launch |

## SD card files and persistence

Root drive comes from `fatInitDefault()` plus `fatGetDefaultDrive()`, which return `sd:` on DSi SD and `fat:` under DLDI. Paths never come from argv, because Unlaunch passes a device list. All names are 8.3. Files are UTF-8 with LF line endings. CRLF and `#` comment lines are accepted on read.

```
<drive>/pda/TODO.TXT     [ ] Buy milk
                         [x] Call bank due:2026-11-21
<drive>/pda/CAL.TXT      2026-11-20|14:00|-|Dentist
                         2026-01-05|--:--|Y|Sam's birthday     (repeat: - D W M Y)
<drive>/pda/ALARMS.TXT   07:30|MTWTF--|on|Wake up
<drive>/pda/CONFIG.INI   [launch] boot=/BOOT.NDS
                         [apps] Moonshell=/apps/msh.nds
<drive>/pda/NOTES/N0001.TXT   first line is the title, 16 KB cap per note
```

Save sequence in `storage.c`: write `X.TMP`, `fclose`, delete `X.BAK`, rename `X` to `X.BAK`, rename `X.TMP` to `X`. FatFs rename fails when the target exists, so each delete runs first. Load reads `X`, then falls back to `X.BAK` when `X` is missing or fails to parse. Each change saves immediately. Launch and power-off save first.

Countdown timers live in RAM only. RAM survives lid-close sleep, and a chainload ends them by design.

## Chainload method

Primary method, `launch_nds(const char *path)` in `launch/launch.c`:
1. `stat(path)` gives `st_ino`, the first cluster. For BOOT.NDS, cluster 0 makes the loader find `BOOT.NDS` by name.
2. Copy `bootstub.bin` and `load.bin` to `fake_heap_end` (`0x02FF4000` on DSi). Relocate `arm9reboot` and `arm7reboot` the way `installBootStub()` does in nds-hb-menu v0.9.0.
3. Patch the loader header: set `storedFileCluster`, set `dsiSD` when the path starts with `sd`, and write argv[0] as the full path. Under `fat:`, patch the loader with the running DLDI driver.
4. Save all data, run `DC_FlushAll()`, then call `exit(0)`. BlocksDS `__libnds_exit()` finds `BOOTSIG` and runs the bootstub. That is the same path a BlocksDS app takes when it exits back to hbmenu.

The launched app inherits the bootstub, so its own exit lands in `BOOT.NDS`.

Fallback method for real hardware only: write the `AutoLoadInfo` block (magic `AutoLoadInfo` at `0x02000800`, length `0x3F0`, flags `BIT(0)|BIT(1)`, UTF-16 `sdmc:/` path at `0x02000838`, `swiCRC16` over `0x02000810..+0x3F0`). Then warm-reboot through the ARM7 I2C power controller. Unlaunch then boots the path natively. This needs a custom ARM7 and the combined template, plus `FAT_getShortNameFor()` for 8.3 paths. It ships only if the primary method fails with TWiLight on hardware.

## Phased roadmap

### Phase 0: toolchain and emulator
- Install BlocksDS in WSL, build one SDK example, install melonDS, create `sdroot\`.
- Check: the example ROM builds with 0 errors and boots in melonDS DS mode.

### Phase 1: vertical slice
- Home screen with To-Do and Games blocks. To-Do supports add (keyboard), toggle, delete, and saves `TODO.TXT` atomically. Games calls `launch_nds("<drive>/BOOT.NDS")`. A missing BOOT.NDS shows an error line on the top screen.
- Build `make_testboot`, a ROM that prints argv and the drive, for use as a fake `BOOT.NDS`.
- Check, build: `make clean && make` passes with `-Wall -Wextra` and 0 warnings. `tests/host` passes for `TODO.TXT` round-trips (empty file, CRLF, 200 items, 120-char lines).
- Check, melonDS DS mode: add 3 items, mark 1 done, close melonDS, reopen. All 3 show with the same state. `sdroot\pda\TODO.TXT` holds the 3 lines.
- Check, melonDS chainload: with the test ROM as `BOOT.NDS`, Games shows it printing `argv[0] = fat:/BOOT.NDS`. With `BOOT.NDS` removed, the PDA shows the error and keeps running.
- Check, hardware: launch `PDA.NDS` from the Unlaunch file menu. Add items and power-cycle; the items persist. Games opens TWiLight Menu++. Then set Unlaunch NO BUTTON to `PDA.NDS` and one hotkey to `BOOT.NDS`. A cold boot opens the PDA, and the hotkey opens TWiLight.

### Phase 2: notes and storage hardening
- Notes list, viewer and line editor. `.BAK` recovery for every file.
- Check: host tests cover note round-trips at the 16 KB cap. In melonDS, 20 notes list and a 10 KB note scrolls to its end. Delete `TODO.TXT` by hand and keep `TODO.BAK`; the PDA loads the backup.

### Phase 3: calendar
- Month grid, day view, repeat rules, Home summary from the RTC.
- Check: host tests expand repeats (Feb 29 yearly, the 31st monthly skips short months, weekly across a year end). In melonDS the grid matches the host date. On hardware the date matches DSi System Settings.

### Phase 4: timers and alarms
- Countdowns and daily alarms with PSG beeps. On lid open, alarms passed during sleep show a "missed" banner.
- Check: a 1-minute timer beeps in melonDS within 1 second of target. On hardware, an alarm 2 minutes ahead rings with the lid open. Closing the lid across the alarm time gives the missed banner on open.

### Phase 5: apps list and DSi-mode coverage
- `[apps]` launching with argv. The Unlaunch `AutoLoadInfo` fallback is built only if Phase 1 hardware tests failed. Add melonDS DSi mode once the dumps exist.
- Check: launch the BlocksDS NitroFS example from a path over 40 characters; it reads its NitroFS files, which proves argv reaches it. In melonDS DSi mode, Games reports `argv[0] = sd:/BOOT.NDS` from the test ROM.

### Phase 6: polish
- Paper and keyboard skin, banner icon, settings screen for `CONFIG.INI`.
- Check: a 30-minute hardware session across all screens with no crash, and every file stays readable on a PC.

## Risks and trade-offs

- The loader code is GPLv2 or later. Shipping the PDA binary makes the PDA GPL as well. Writing a loader from scratch costs weeks, so reuse wins.
- The v0.9.0 port may hit BlocksDS API gaps (DLDI dump helpers, `fake_heap_end` layout). Phase 1 does this port first. The `AutoLoadInfo` fallback avoids the loader entirely on hardware.
- Until dumps exist, melonDS tests cover only the `fat:` and DLDI branch. The `dsiSD` branch runs first on real hardware in Phase 1. A wrong boot target is recoverable because the Unlaunch hotkey still reaches BOOT.NDS.
- TWiLight Menu++ is built to run as hbmenu's BOOT.NDS, so the bootstub path should work (likely). The PDA runs in DSi mode under Unlaunch, and the loader keeps SCFG unlocked.
- Alarms cannot fire with the lid closed. The missed banner reports them on open.
- The libnds keyboard and the block grid share the sub engine's VRAM. Bank layout gets fixed in Phase 1. The loader reuses VRAM_C only at exit, when graphics are already discarded.
- Power loss mid-save leaves either `X` or `X.BAK` whole, since each step is one FAT rename or delete.
- Assets are compiled into the ROM through grit and NitroFS goes unused. Unlaunch's missing argv then has no effect on the PDA.
