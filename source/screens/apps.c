// Games & Apps, top to bottom:
//   DSi Mode          TWiLight Menu++ ([launch] boot=), always listed
//   built-in entries  TWiLight Menu++'s own tools and GBARunner2, listed
//                     only when the file is on the card
//   pda/apps/*.nds    any homebrew dropped in that folder
//   [apps]            Name=/path.nds lines from CONFIG.INI
// Each launches with argv[0] = its path; a path listed twice shows once.
// Tap an entry to pick it, then Launch (or tap it again).
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <nds.h>

#include "app.h"
#include "device.h"
#include "launch/launch.h"
#include "screens.h"
#include "sfx.h"
#include "ui/editor.h"
#include "ui/widgets.h"

#define APPS_MAX 32
#define BOX_W 26

typedef struct {
    // A copy: CONFIG.INI is re-read when a setting is saved (brightness),
    // and entries can shift, so a pointer into g_cfg could show the wrong
    // name next to an app's path.
    char name[INI_KEY_MAX + 1];
    char path[128];
    const char *about[2];   // two lines on the top screen, or NULL
    int icon;
    bool found;
} AppEntry;

// Known launchers, shown when their file exists.
static const struct {
    const char *name, *path, *about1, *about2;
    int icon;
} BUILTIN[] = {
    { "Game card", "/_nds/TWiLightMenu/slot1launch.srldr", "Runs the game card.",
      "", ICON_PLAY },
    { "GBA games", "/_nds/GBARunner2_arm7dldi_dsi.nds", "Runs GBA games",
      "from the _gba folder.", ICON_PLAY },
    { "TWiLight settings", "/_nds/TWiLightMenu/settings.srldr", "TWiLight settings.",
      "", ICON_GEAR },
};
#define BUILTIN_N ((int)(sizeof(BUILTIN) / sizeof(BUILTIN[0])))

#define APPS_DIR "pda/apps"

static AppEntry apps[APPS_MAX];
static int count = 0;
static List list;
static Bar bar;

// Adds an entry for a path from the card root ("/x.nds"). With `always`
// unset it is left out when the file is missing. A path already listed is
// skipped.
static void add_entry(const char *name, const char *rel, const char *about1,
                      const char *about2, int icon, bool always)
{
    if (count >= APPS_MAX)
        return;
    AppEntry *a = &apps[count];
    snprintf(a->name, sizeof(a->name), "%s", name);
    char full[sizeof(a->path) + 1];
    app_root_path(rel, full, sizeof(full));
    if (strlen(full) >= sizeof(a->path))
        return;   // too long to launch
    snprintf(a->path, sizeof(a->path), "%s", full);
    for (int i = 0; i < count; i++)
        if (strcasecmp(a->path, apps[i].path) == 0)
            return;
    struct stat st;
    a->found = app_storage_ok() && stat(a->path, &st) == 0 && !S_ISDIR(st.st_mode);
    if (!a->found && !always)
        return;
    a->about[0] = about1;
    a->about[1] = about2;
    a->icon = icon;
    count++;
}

static bool is_nds(const char *name)
{
    size_t n = strlen(name);
    return n > 4 && strcasecmp(name + n - 4, ".nds") == 0;
}

// Homebrew in pda/apps: the file name, without .nds, is the entry's name.
static void add_folder(void)
{
    if (!app_storage_ok())
        return;
    char dir[PDA_PATH_MAX];
    app_root_path(APPS_DIR, dir, sizeof(dir));
    DIR *d = opendir(dir);
    if (d == NULL)
        return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && count < APPS_MAX)
    {
        if (e->d_name[0] == '.' || !is_nds(e->d_name))
            continue;
        char name[INI_KEY_MAX + 1], rel[160];
        snprintf(name, sizeof(name), "%.*s", (int)strlen(e->d_name) - 4, e->d_name);
        snprintf(rel, sizeof(rel), "/" APPS_DIR "/%s", e->d_name);
        add_entry(name, rel, "Homebrew app from", "pda/apps.", ICON_PLAY, false);
    }
    closedir(d);
}

static void enter(void)
{
    if (list.height == 0)
        list_init(&list, UI_HEAD_H, UI_BAR_Y - UI_HEAD_H, 0);
    count = 0;
    add_entry("DSi Mode", ini_get(&g_cfg, "launch", "boot", "/BOOT.NDS"),
              "TWiLight Menu++.", "All your games.", ICON_GAMES, true);
    for (int i = 0; i < BUILTIN_N; i++)
        add_entry(BUILTIN[i].name, BUILTIN[i].path, BUILTIN[i].about1, BUILTIN[i].about2,
                  BUILTIN[i].icon, false);
    add_folder();
    for (int i = 0; i < g_cfg.count; i++)
    {
        if (strcasecmp(g_cfg.e[i].section, "apps") == 0)
            add_entry(g_cfg.e[i].key, g_cfg.e[i].value, NULL, NULL, ICON_PLAY, true);
    }
    list.count = count;
    list_fix(&list);
}

void launch_select_games(void)
{
    list.sel = list.scroll = 0;
}

static void draw_top(Surface *s)
{
    draw_clock_header(s, "Games");
    gfx_paper(s, PAPER_Y);
    if (count == 0)
        return;
    list_fix(&list);
    const AppEntry *a = &apps[list.sel];
    int row = title_draw(s, a->name, 0, C_INK) + 1;
    if (a->about[0] != NULL)
    {
        gfx_text(s, 20, PAPER_TEXT(row), a->about[0], C_INK_SOFT);
        if (a->about[1] != NULL)
            gfx_text(s, 20, PAPER_TEXT(row + 1), a->about[1], C_INK_SOFT);
        row += 3;
    }
    gfx_chip(s, 20, PAPER_Y + row * PAPER_ROW, "FILE", SEC_MID(SEC_GAMES), C_INK);
    row++;
    char lines[3][TEXT_WRAP_W + 1];
    int n = text_wrap(a->path, 29, lines, 3);
    for (int i = 0; i < n; i++, row++)
        gfx_text(s, 20, PAPER_TEXT(row), lines[i], a->found ? C_INK : C_ALERT);
    if (!a->found)
        gfx_text(s, 20, PAPER_TEXT(row), "File missing.", C_ALERT);
    else
        gfx_text(s, 20, PAPER_TEXT(row), "Your data is saved first.", C_INK_SOFT);

    if (count == 1)
    {
        gfx_text(s, 20, PAPER_TEXT(10), "More apps: add .nds to", C_INK_SOFT);
        gfx_text(s, 20, PAPER_TEXT(11), "pda/apps, or Name=/path.nds", C_INK_SOFT);
        gfx_text(s, 20, PAPER_TEXT(12), "under [apps] in CONFIG.INI", C_INK_SOFT);
    }
    static const char *const KEYS[] = { "A", "Launch", NULL };
    legend_draw(s, PAPER_TEXT(13), KEYS);
}

static void draw_row(Surface *s, int i, int y, bool selected)
{
    (void)selected;
    gfx_icon(s, 5, y + 1, apps[i].icon, 1);
    gfx_text_n(s, BOX_W + 6, y + 6, apps[i].name, 27, apps[i].found ? C_INK : C_ALERT);
}

static void draw_bottom(Surface *s)
{
    list.count = count;
    list_draw(&list, s, draw_row);
    bar.n = 1;
    bar.item[0] = (BarItem){ "Launch", ICON_PLAY, SEC_GAMES, count > 0 && apps[list.sel].found };
    bar_draw(s, &bar);
}

static void launch_sel(void)
{
    if (count == 0)
        return;
    if (!apps[list.sel].found)
    {
        sfx_play(SFX_ERROR);
        ui_toast("Missing: %s", apps[list.sel].path);
        return;
    }
    request_launch(apps[list.sel].path);
}

static void update(const Input *in)
{
    list.count = count;
    if (bar_update(&bar, in) == 0)
    {
        launch_sel();
        return;
    }
    ListEvent ev = list_update(&list, in);
    if (ev.kind == LE_OPEN || (in->down & KEY_START))
    {
        launch_sel();
        return;
    }
    // SELECT on DSi Mode: return to BOOT.NDS through BlocksDS exit() and the
    // return stub (the same path a launched app takes when it quits).
    if ((in->down & KEY_SELECT) && list.sel == 0 && count > 0 && apps[0].found)
    {
        LaunchResult r = launch_install_return_stub(app_drive());
        if (r == LAUNCH_OK)
        {
            device_flush();
            handwriting_save();
            exit(0);
        }
        ui_toast("%s", launch_error_text(r));
    }
}

const Screen SCR_LAUNCH = {
    .enter = enter,
    .update = update,
    .draw_top = draw_top,
    .draw_bottom = draw_bottom,
    .title = "Games & Apps",
    .icon = ICON_GAMES,
    .sec = SEC_GAMES,
};
