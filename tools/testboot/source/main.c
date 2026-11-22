// Stand-in for BOOT.NDS during testing. Shows what the loader passed in,
// then START calls exit(0), which must land in <drive>/BOOT.NDS via the
// bootstub that the PDA installed.
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>

#include <fat.h>
#include <nds.h>
#include <nds/arm9/dldi.h>

int main(int argc, char **argv)
{
    consoleDemoInit();
    BG_PALETTE_SUB[0] = RGB15(0, 10, 4);
    printf("\x1b[1;1H== PDA TEST ROM ==\n\n");
    printf("argc = %d\n", argc);
    for (int i = 0; i < argc && i < 4; i++)
        printf("argv[%d] = %s\n", i, argv[i]);
    printf("DSi mode: %s\n", isDSiMode() ? "yes" : "no");
    bool ok = fatInitDefault();
    printf("FAT: %s %s\n", ok ? "ok" : "FAIL", ok ? fatGetDefaultDrive() : "");
    const DLDI_INTERFACE *dl = io_dldi_data;
    printf("DLDI: %.24s\n", dl->friendlyName);
    printf(" feat=%08lX size=2^%d fix=%02X\n", (unsigned long)dl->ioInterface.features,
           dl->driverSize, dl->fixSectionsFlags);
    printf(" text=%08lX\n", (unsigned long)(uintptr_t)dl->dldiStart);
    struct __bootstub *bs = (struct __bootstub *)__system_bootstub;
    printf("bootstub: %s\n", bs->bootsig == BOOTSIG ? "present" : "missing");
    time_t t = time(NULL);
    printf("time: %s\n", ctime(&t));
    printf("START: exit() to BOOT.NDS\n");
    printf("SELECT: power off\n");
    printf("A: write test\n");

    while (1)
    {
        swiWaitForVBlank();
        scanKeys();
        u32 k = keysDown();
        if (k & KEY_START)
            break;
        if (k & KEY_SELECT)
            systemShutDown();
        if ((k & KEY_A) && ok)
        {
            char p[64];
            snprintf(p, sizeof(p), "%sTBDIR", fatGetDefaultDrive());
            printf("mkdir: ");
            int r = mkdir(p, 0777);
            printf("%d\n", r);
            snprintf(p, sizeof(p), "%sTBDIR/T.TXT", fatGetDefaultDrive());
            printf("fopen: ");
            FILE *f = fopen(p, "w");
            printf("%s\n", f ? "ok" : "fail");
            if (f)
            {
                printf("write+close: ");
                fputs("hello\n", f);
                printf("%d\n", fclose(f));
            }
        }
    }
    return 0;
}
