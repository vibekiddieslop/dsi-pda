# Third-party code (GPLv2 or later)

- `nds-bootloader/`: devkitPro nds-bootloader, commit 35f54d8e036ac2326331914ea6d2f38d11353b8d
  (the submodule pinned by nds-hb-menu v0.9.0). Built into `load.bin`, which runs on the ARM7
  from VRAM bank C and loads an `.nds` file by its first FAT cluster.
- `hbmenu/bootstub.s`: from devkitPro nds-hb-menu v0.9.0. Installed at 0x02FF4000 so that
  `exit()` in a launched app reloads BOOT.NDS.

Both are GPLv2 or later (see `hbmenu/COPYING`). Distributing the PDA binary makes it GPL too.
Local changes are listed in `PATCHES.md`.
