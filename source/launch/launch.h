// Chainloading other .nds files with the nds-hb-menu loader.
#ifndef LAUNCH_LAUNCH_H
#define LAUNCH_LAUNCH_H

typedef enum {
    LAUNCH_OK = 0,          // Never seen by the caller: success does not return.
    LAUNCH_NOT_FOUND,       // stat() failed.
    LAUNCH_BAD_PATH,        // Path does not start with "sd:/" or "fat:/".
    LAUNCH_NO_SPACE,        // Bootstub + loader + args exceed the 24 KiB slot.
    LAUNCH_DLDI_FAILED,     // fat:/ build without a usable DLDI driver.
} LaunchResult;

// Installs the "return to BOOT.NDS" bootstub at 0x02FF4000. Called by
// launch_nds() and safe to call at startup.
LaunchResult launch_install_return_stub(const char *drive);

// Boots `path` (for example "sd:/BOOT.NDS"). The launched program gets
// argv[0] = path. When it exits, the console boots <drive>/BOOT.NDS.
// Returns only on failure.
LaunchResult launch_nds(const char *path);

const char *launch_error_text(LaunchResult r);

#endif
