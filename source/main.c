// DSi PDA launcher: to-do, notes, calendar, timers, and a chainloader for
// TWiLight Menu++ / other homebrew.
#include <time.h>

#include <nds.h>

#include "app.h"
#include "device.h"
#include "launch/launch.h"
#include "screens/screens.h"
#include "timers.h"
#include "ui/editor.h"
#include "ui/gfx.h"
#include "ui/ui.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;  // Unlaunch passes a device list; paths come from the drive.

    defaultExceptionHandler();
    gfx_init();
    app_init();
    editor_init();
    soundEnable();
    device_init();
    timers_init(time(NULL));

    ui_set_overlay(&g_ring_overlay);
    ui_init(&SCR_HOME);
    if (app_last_error()[0])
        ui_toast("%s", app_last_error());

    while (1)
    {
        swiWaitForVBlank();
        device_tick();
        timers_tick(time(NULL));
        ui_frame();
        launch_poll();
    }
    return 0;
}
