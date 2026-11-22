# Local changes to third-party code

## nds-bootloader (commit 35f54d8e)

- `Makefile.blocksds` (new): builds `load.bin` with the BlocksDS/Wonderful
  `arm-none-eabi-gcc` in place of devkitARM's `ds_rules`.
  - `*.arm.c` files build with `-marm`. The ARM9 jumps into
    `resetMemory2_ARM9` / `startBinary_ARM9` in ARM state; a Thumb build hangs
    the console with a black bottom screen.
  - `-fno-tree-loop-distribute-patterns` stops GCC turning the `memcpy` loop
    in `minilib.c` into a call to itself.
- `source/minilib.c` (new): `memcpy`, `memset`, `memcmp`, `strlen` and
  `dmaSetParams`. devkitARM linked these from newlib and libnds; this build
  uses `-nostdlib`.
- The original `Makefile` is unchanged and unused.

## nds-hb-menu v0.9.0

- `hbmenu/bootstub.s`: unchanged. Built by `tools/bootstub.mk`.
- The DLDI patch routine in `source/launch/launch.c` is a port of
  `dldiPatchLoader()` from `source/nds_loader_arm9.c`.
