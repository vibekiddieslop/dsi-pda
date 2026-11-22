#include "device.h"

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include <nds.h>

#include "app.h"
#include "sfx.h"
#include "ui/gfx.h"

// ---------------------------------------------------------------- sounds

void device_click(void)
{
    sfx_play(SFX_TAP);
}

// ---------------------------------------------------------------- battery

// 10 seconds: each poll is a round trip to the ARM7 and the power chip, and
// the level only moves over minutes.
#define BATTERY_POLL_FRAMES 600

static int battery_frames = 0;

// DSi: bits 0-3 are the level (0, 1, 3, 7, 11 or 15), bit 7 is set while
// the charger is connected.
static void battery_poll(void)
{
    static bool warned = false;
    u32 v = getBatteryLevel();
    int level = (int)(v & BATTERY_LEVEL_MASK);
    int bars = level >= 15 ? 4 : level >= 11 ? 3 : level >= 7 ? 2 : level >= 3 ? 1 : 0;
    bool charging = (v & BATTERY_CHARGER_CONNECTED) != 0;
    if (gfx_set_battery(bars, charging))
        ui_dirty_top();   // The icon is in the top screen's header.
    // One reminder per discharge, when the DSi reports its lowest level.
    if (bars == 0 && !charging && !warned)
    {
        warned = true;
        ui_toast("Battery low. Please charge.");
    }
    else if (charging || bars > 1)
    {
        warned = false;
    }
}

// ---------------------------------------------------------------- brightness

// DSi backlight levels 1..5 (PM_BACKLIGHT_MIN..MAX). Level 0 turns the
// backlight off, so it is never used. The DSi keeps the level across power
// off by itself; libnds cannot read it back, so CONFIG.INI remembers the
// last level set here and steps start from it.
#define BRIGHT_MIN PM_BACKLIGHT_MIN
#define BRIGHT_MAX PM_BACKLIGHT_MAX

static int bright = -1;           // -1 until known
static bool bright_changed = false;
// Frames until the level set from Settings is written to CONFIG.INI. Each
// write is a full crash-safe save on the SD card, too slow to do for every
// key repeat while Left or Right is held.
#define BRIGHT_SAVE_FRAMES 30
static int bright_save_frames = 0;

static void bright_save(void)
{
    bright_save_frames = 0;
    char v[4];
    snprintf(v, sizeof(v), "%d", bright);
    app_set_config("ui", "brightness", v);
}

// After a minute without a button or the stylus the backlight drops to its
// lowest level, and comes back on the first input (or when an alarm rings).
// Only once the level is known: the level to come back to has to be.
#define DIM_AFTER_FRAMES (60 * 60)
static int idle_frames = 0;
static bool dimmed = false;

void device_wake(void)
{
    idle_frames = 0;
    if (dimmed)
    {
        dimmed = false;
        systemSetBacklightLevel((u32)bright);
    }
}

static void idle_tick(bool active)
{
    if (active)
    {
        device_wake();
    }
    else if (!dimmed && bright > BRIGHT_MIN && ++idle_frames >= DIM_AFTER_FRAMES)
    {
        dimmed = true;
        systemSetBacklightLevel(BRIGHT_MIN);
    }
}

// The ARM9 runs at half speed (67 MHz) to spare the battery, and at full
// speed only where it has real work: reading handwriting and swipes.
void device_cpu_fast(bool fast)
{
    if (isDSiMode())
        setCpuClock(fast);
}

static int bright_clamp(int level)
{
    return level < BRIGHT_MIN ? BRIGHT_MIN : level > BRIGHT_MAX ? BRIGHT_MAX : level;
}

static void bright_step(int d)
{
    if (bright < 0)
        bright = 3;               // Unknown: a middle level.
    bright = bright_clamp(bright + d);
    systemSetBacklightLevel((u32)bright);
    bright_changed = true;
    ui_meter("Light", bright, BRIGHT_MAX);
}

// ---------------------------------------------------------------- volume

// The custom ARM7 (third_party/arm7) sends the DSi volume register (0..31)
// on FIFO_USER_01 whenever the rocker changes it, and once at start.
#define FIFO_PDA_VOLUME FIFO_USER_01
#define VOLUME_MAX 31

static volatile int volume_new = -1;
static int volume_shown = -1;

static void volume_handler(u32 value, void *userdata)
{
    (void)userdata;
    volume_new = (int)(value & 0xFF);   // IRQ context: just remember it
}

static int volume_frames = 0;    // since start, up to 120

static void volume_tick(void)
{
    if (volume_frames < 120)
        volume_frames++;
    int v = volume_new;
    if (v < 0 || v == volume_shown)
        return;
    // The first value at start is the current level, not a rocker press.
    // Anything later is a press, even if that start value was missed.
    if (volume_shown >= 0 || volume_frames >= 120)
        ui_meter(v == 0 ? "Muted" : "Volume", v, VOLUME_MAX);
    volume_shown = v;
}

// ---------------------------------------------------------------- input

static bool chord = false;        // L+R held: Up and Down set the backlight
static uint32_t shoulder_pending = 0;
static int shoulder_wait = 0;

#define SHOULDER_DELAY 4          // Frames to wait for the second shoulder.

void device_filter_input(Input *in)
{
    idle_tick(in->down || in->held || in->touching || in->touch_up);
    const uint32_t LR = KEY_L | KEY_R;
    if ((in->held & LR) == LR && !chord)
    {
        chord = true;
        shoulder_pending = 0;
        shoulder_wait = 0;
    }
    if (chord)
    {
        if (in->repeat & KEY_UP)
            bright_step(+1);
        if (in->repeat & KEY_DOWN)
            bright_step(-1);
        in->down &= ~(LR | KEY_UP | KEY_DOWN);
        in->repeat &= ~(LR | KEY_UP | KEY_DOWN);
        if ((in->held & LR) == 0)
        {
            chord = false;
            if (bright_changed)
            {
                bright_changed = false;
                bright_save();
            }
        }
        return;
    }

    // One shoulder alone: hold it back briefly in case the other follows.
    uint32_t newly = in->down & LR;
    if (newly)
    {
        shoulder_pending |= newly;
        shoulder_wait = SHOULDER_DELAY;
    }
    if (shoulder_wait > 0)
    {
        in->down &= ~LR;
        in->repeat &= ~LR;
        if (--shoulder_wait == 0)
        {
            in->down |= shoulder_pending;
            in->repeat |= shoulder_pending;
            shoulder_pending = 0;
        }
    }
}

// ---------------------------------------------------------------- init

void device_init(void)
{
    // [ui] sounds=on/off; the older clicks= key is still read.
    const char *snd = ini_get(&g_cfg, "ui", "sounds", ini_get(&g_cfg, "ui", "clicks", "on"));
    sfx_set_enabled(strcasecmp(snd, "off") != 0);
    gfx_set_theme(gfx_theme_from_key(ini_get(&g_cfg, "ui", "theme", "classic")));

    // Only remembered, not applied: the console already restored it.
    const char *b = ini_get(&g_cfg, "ui", "brightness", "");
    if (b[0] >= '1' && b[0] <= '9')
        bright = bright_clamp(atoi(b));

    fifoSetValue32Handler(FIFO_PDA_VOLUME, volume_handler, NULL);
    device_cpu_fast(false);
    battery_poll();
}

void device_tick(void)
{
    sfx_tick();
    volume_tick();
    if (++battery_frames >= BATTERY_POLL_FRAMES)
    {
        battery_frames = 0;
        battery_poll();
    }
    if (bright_save_frames > 0 && --bright_save_frames == 0)
        bright_save();
}

void device_flush(void)
{
    device_wake();
    device_cpu_fast(true);   // what a launched app expects
    if (bright_save_frames > 0)
        bright_save();
}

// ---------------------------------------------------------------- owner

const char *device_user_name(void)
{
    static char name[24];
    const char *set = ini_get(&g_cfg, "ui", "name", "");
    if (set[0])
    {
        snprintf(name, sizeof(name), "%.20s", set);
        return name;
    }
    // The console's own profile name (UTF-16; plain letters only).
    int n = 0, len = PersonalData->nameLen;
    for (int i = 0; i < len && i < 10; i++)
    {
        unsigned c = (unsigned)PersonalData->name[i] & 0xFFFF;
        if (c >= 32 && c < 127)
            name[n++] = (char)c;
    }
    name[n] = '\0';
    return name;
}

// ---------------------------------------------------------------- settings

int device_brightness(void)
{
    return bright < 0 ? 0 : bright;
}

void device_set_brightness(int level)
{
    bright = bright_clamp(level);
    systemSetBacklightLevel((u32)bright);
    bright_save_frames = BRIGHT_SAVE_FRAMES;   // saved once the steps stop
}

void device_set_theme(int theme)
{
    gfx_set_theme(theme);
    app_set_config("ui", "theme", gfx_theme_key(gfx_theme()));
    ui_dirty();
}

void device_set_sounds(bool on)
{
    sfx_set_enabled(on);
    app_set_config("ui", "sounds", on ? "on" : "off");
}

static int holidays_shown = -1;   // -1 until read from CONFIG.INI

bool device_holidays(void)
{
    if (holidays_shown < 0)
        holidays_shown = strcasecmp(ini_get(&g_cfg, "ui", "holidays", "on"), "off") != 0;
    return holidays_shown != 0;
}

void device_set_holidays(bool on)
{
    holidays_shown = on;
    app_set_config("ui", "holidays", on ? "on" : "off");
    ui_dirty();
}
