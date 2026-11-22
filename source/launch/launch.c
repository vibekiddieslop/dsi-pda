// Chainloader built on the nds-hb-menu v0.9.0 bootstub and the devkitPro
// nds-bootloader (GPLv2 or later, see third_party/).
//
// Two copies of "bootstub + loader" are used:
//  - The return stub at 0x02FF4000 loads BOOT.NDS by name. BlocksDS exit()
//    jumps into it, so any launched app that exits lands in BOOT.NDS.
//  - A launch copy in a static buffer carries the target's first cluster and
//    argv. The PDA jumps into it directly, the same way exit() would.

#include "launch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <nds.h>
#include <nds/arm9/dldi.h>

#include "bootstub_bin.h"
#include "load_bin.h"

// Loader header words (see third_party/nds-bootloader/source/load_crt0.s).
enum {
    LDR_CLUSTER = 1,
    LDR_INIT_DISC = 2,
    LDR_PATCH_DLDI = 3,
    LDR_ARG_START = 4,
    LDR_ARG_SIZE = 5,
    LDR_DSI_SD = 7,
    LDR_DSI_MODE = 8,
};

#define CLUSTER_BOOT_NDS 0x0FFFFFFFu     // Loader looks up BOOT.NDS by name.
#define BOOTSTUB_REAL    0x02FF4000u     // Where the return stub executes.
#define BOOTSTUB_SLOT    0x6000u         // Up to the exception handler at 0x02FFA000.

// Bootstub header (bootstub.s): "bootstub", arm9reboot off, arm7reboot off, size.
typedef struct {
    u64 sig;
    u32 arm9reboot;
    u32 arm7reboot;
    u32 loader_size;
} StubHeader;

static u8 launch_buf[BOOTSTUB_SLOT + 4096] __attribute__((aligned(32)));

// ---------------------------------------------------------------- DLDI

// Port of dldiPatchLoader() from nds-hb-menu v0.9.0 nds_loader_arm9.c.
enum {
    DO_driverSize = 0x0D, DO_fixSections = 0x0E, DO_allocatedSpace = 0x0F,
    DO_text_start = 0x40, DO_data_end = 0x44, DO_glue_start = 0x48, DO_glue_end = 0x4C,
    DO_got_start = 0x50, DO_got_end = 0x54, DO_bss_start = 0x58, DO_bss_end = 0x5C,
    DO_ioType = 0x60, DO_startup = 0x68, DO_isInserted = 0x6C, DO_readSectors = 0x70,
    DO_writeSectors = 0x74, DO_clearStatus = 0x78, DO_shutdown = 0x7C, DO_code = 0x80,
};
#define DEVICE_TYPE_DLDI 0x49444C44  // "DLDI": the empty placeholder driver.

static s32 rd(const u8 *mem, s32 off)
{
    s32 v;
    memcpy(&v, mem + off, 4);
    return v;
}

static void wr(u8 *mem, s32 off, s32 v)
{
    memcpy(mem + off, &v, 4);
}

// The loader's DLDI area uses a different magic so normal patchers skip it.
static const u8 loader_dldi_magic[] = "\xEE\xA5\x8D\xBF Chishm";

static s32 find_magic(const u8 *data, size_t len)
{
    for (size_t i = 0; i + sizeof(loader_dldi_magic) <= len; i += 4)
    {
        if (memcmp(data + i, loader_dldi_magic, sizeof(loader_dldi_magic)) == 0)
            return (s32)i;
    }
    return -1;
}

static void fix_range(u8 *pAH, const u8 *pDH, s32 from_off, s32 to_off,
                      s32 ddmemStart, s32 ddmemEnd, s32 reloc)
{
    for (s32 a = rd(pDH, from_off) - ddmemStart; a < rd(pDH, to_off) - ddmemStart; a++)
    {
        s32 v = rd(pAH, a);
        if (ddmemStart <= v && v < ddmemEnd)
            wr(pAH, a, v + reloc);
    }
}

static bool dldi_patch_loader(u8 *ldr, size_t ldr_size)
{
    s32 patch = find_magic(ldr, ldr_size);
    if (patch < 0)
        return false;

    const u8 *pDH = (const u8 *)io_dldi_data;
    u8 *pAH = ldr + patch;
    if (pDH == NULL || rd(pDH, DO_ioType) == DEVICE_TYPE_DLDI)
        return false;
    if (pDH[DO_driverSize] > pAH[DO_allocatedSpace])
        return false;

    s32 size = 1 << pDH[DO_driverSize];
    s32 memOffset = rd(pAH, DO_text_start);
    if (memOffset == 0)
        memOffset = rd(pAH, DO_startup) - DO_code;
    s32 ddmemStart = rd(pDH, DO_text_start);
    s32 ddmemEnd = ddmemStart + size;
    s32 reloc = memOffset - ddmemStart;
    u8 allocated = pAH[DO_allocatedSpace];

    memcpy(pAH, pDH, (size_t)size);
    pAH[DO_allocatedSpace] = allocated;

    static const s32 header_ptrs[] = {
        DO_text_start, DO_data_end, DO_glue_start, DO_glue_end, DO_got_start,
        DO_got_end, DO_bss_start, DO_bss_end, DO_startup, DO_isInserted,
        DO_readSectors, DO_writeSectors, DO_clearStatus, DO_shutdown,
    };
    for (size_t i = 0; i < sizeof(header_ptrs) / sizeof(header_ptrs[0]); i++)
        wr(pAH, header_ptrs[i], rd(pAH, header_ptrs[i]) + reloc);

    u8 fix = pDH[DO_fixSections];
    if (fix & FIX_ALL)
        fix_range(pAH, pDH, DO_text_start, DO_data_end, ddmemStart, ddmemEnd, reloc);
    if (fix & FIX_GLUE)
        fix_range(pAH, pDH, DO_glue_start, DO_glue_end, ddmemStart, ddmemEnd, reloc);
    if (fix & FIX_GOT)
        fix_range(pAH, pDH, DO_got_start, DO_got_end, ddmemStart, ddmemEnd, reloc);
    return true;
}

// ---------------------------------------------------------------- loader

static bool is_sd_path(const char *path)
{
    return strncmp(path, "sd:", 3) == 0;
}

// Writes bootstub + patched loader into dst. Returns the total byte count,
// or 0 on failure (*err set).
static size_t build_stub(u8 *dst, size_t dst_size, u32 cluster, const char *argv0,
                         LaunchResult *err)
{
    size_t arg_len = strlen(argv0) + 1;
    u8 *ldr = dst + bootstub_bin_size;

    u32 arg_start;
    memcpy(&arg_start, load_bin + LDR_ARG_START * 4, 4);
    arg_start = (arg_start + 3) & ~3u;
    size_t ldr_total = (arg_start + arg_len + 3) & ~3u;
    if (bootstub_bin_size + ldr_total > dst_size)
    {
        *err = LAUNCH_NO_SPACE;
        return 0;
    }

    memcpy(dst, bootstub_bin, bootstub_bin_size);
    memset(ldr, 0, ldr_total);
    memcpy(ldr, load_bin, load_bin_size);

    u32 *w = (u32 *)ldr;
    w[LDR_CLUSTER] = cluster;
    w[LDR_INIT_DISC] = 1;
    w[LDR_DSI_MODE] = isDSiMode() ? 1 : 0;
    w[LDR_ARG_START] = arg_start;
    w[LDR_ARG_SIZE] = arg_len;
    memcpy(ldr + arg_start, argv0, arg_len);

    if (is_sd_path(argv0))
    {
        w[LDR_DSI_SD] = 1;
        w[LDR_PATCH_DLDI] = 0;
    }
    else
    {
        w[LDR_DSI_SD] = 0;
        w[LDR_PATCH_DLDI] = 1;
        if (!dldi_patch_loader(ldr, load_bin_size))
        {
            *err = LAUNCH_DLDI_FAILED;
            return 0;
        }
    }

    StubHeader *h = (StubHeader *)dst;
    h->loader_size = ldr_total;
    return bootstub_bin_size + ldr_total;
}

LaunchResult launch_install_return_stub(const char *drive)
{
    char boot[32];
    snprintf(boot, sizeof(boot), "%sBOOT.NDS", drive);

    static u8 tmp[BOOTSTUB_SLOT] __attribute__((aligned(32)));
    LaunchResult err = LAUNCH_OK;
    size_t total = build_stub(tmp, sizeof(tmp), CLUSTER_BOOT_NDS, boot, &err);
    if (total == 0)
        return err;

    StubHeader *h = (StubHeader *)tmp;
    h->arm9reboot += BOOTSTUB_REAL;
    h->arm7reboot += BOOTSTUB_REAL;

    // On the ARM9, DTCM can hide 0x02FF4000; write through the mirror.
    memcpy((void *)__system_bootstub, tmp, total);
    DC_FlushAll();
    return LAUNCH_OK;
}

LaunchResult launch_nds(const char *path)
{
    struct stat st;
    if (strncmp(path, "sd:/", 4) != 0 && strncmp(path, "fat:/", 5) != 0)
        return LAUNCH_BAD_PATH;
    if (stat(path, &st) != 0 || S_ISDIR(st.st_mode))
        return LAUNCH_NOT_FOUND;

    char drive[8];
    snprintf(drive, sizeof(drive), "%.*s", (int)(strchr(path, '/') - path + 1), path);
    LaunchResult r = launch_install_return_stub(drive);
    if (r != LAUNCH_OK)
        return r;

    // BlocksDS sets st_ino to the file's first FAT cluster.
    r = LAUNCH_OK;
    size_t total = build_stub(launch_buf, sizeof(launch_buf), (u32)st.st_ino, path, &r);
    if (total == 0)
        return r;

    StubHeader *h = (StubHeader *)launch_buf;
    VoidFn entry = (VoidFn)((uintptr_t)launch_buf + h->arm9reboot);

    // Same hand-off sequence as BlocksDS _exit() with a bootstub present.
    DC_FlushAll();
    REG_IME = 0;
    CP15_MPUDisable();
    if (isDSiMode())
        REG_SCFG_EXT |= SCFG_EXT_RAM_DEBUG | SCFG_EXT_RAM_TWL;

    void __libnds_mpu_jump_past_dtcm(VoidFn address);
    __libnds_mpu_jump_past_dtcm(entry);

    while (1)
        ; // Not reached.
}

const char *launch_error_text(LaunchResult r)
{
    switch (r)
    {
        case LAUNCH_OK: return "OK";
        case LAUNCH_NOT_FOUND: return "File not found";
        case LAUNCH_BAD_PATH: return "Bad path. Use sd:/ or fat:/";
        case LAUNCH_NO_SPACE: return "Loader too big.";
        case LAUNCH_DLDI_FAILED: return "No DLDI driver.";
    }
    return "Unknown error";
}
