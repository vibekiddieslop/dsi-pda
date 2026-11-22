#include "widgets.h"

#include <string.h>

#include <nds.h>

#include "device.h"

// ---------------------------------------------------------------- press

int press_update(Press *p, const Input *in, const Rect *rects, int n)
{
    if (in->touch_down)
    {
        p->held = 0;
        for (int i = 0; i < n; i++)
            if (rect_has(&rects[i], in->tx, in->ty))
            {
                p->held = i + 1;
                p->hot = true;
                device_click();
                ui_dirty_bottom();
                break;
            }
        return -1;
    }
    if (p->held == 0)
        return -1;
    if (p->held > n)
    {
        // The set shrank while held (an action bar changed): drop the press
        // rather than read past rects[].
        p->held = 0;
        ui_dirty_bottom();
        return -1;
    }
    if (in->touching)
    {
        bool hot = rect_has(&rects[p->held - 1], in->tx, in->ty);
        if (hot != p->hot)
        {
            p->hot = hot;
            ui_dirty_bottom();
        }
        return -1;
    }
    int fired = in->touch_up && p->hot ? p->held - 1 : -1;
    p->held = 0;
    ui_dirty_bottom();
    return fired;
}

// ---------------------------------------------------------------- bar

static void bar_rects(const Bar *b, Rect *r)
{
    // Narrow keys (arrows) take 32 px; the others share what is left.
    int gap = 4, narrow = 0;
    for (int i = 0; i < b->n; i++)
        narrow += (b->narrow >> i) & 1;
    int wide = b->n - narrow;
    int w = (SCREEN_W - gap * (b->n + 1) - 32 * narrow) / (wide > 0 ? wide : 1);
    int x = gap;
    for (int i = 0; i < b->n; i++)
    {
        int iw = ((b->narrow >> i) & 1) ? 32 : w;
        r[i] = (Rect){ (short)x, UI_BAR_Y + 4, (short)iw, UI_BAR_H - 7 };
        x += iw + gap;
    }
}

void bar_draw(Surface *s, const Bar *b)
{
    gfx_fill(s, 0, UI_BAR_Y, SCREEN_W, UI_BAR_H, C_LEATHER);
    gfx_hline(s, 0, UI_BAR_Y, SCREEN_W, C_LEATHER_SH);
    gfx_dotted_hline(s, 2, UI_BAR_Y + 2, SCREEN_W - 4, C_LEATHER_HI);
    Rect r[5];
    bar_rects(b, r);
    for (int i = 0; i < b->n; i++)
    {
        const BarItem *it = &b->item[i];
        gfx_button(s, r[i].x, r[i].y, r[i].w, r[i].h, it->label, it->icon, it->sec,
                   it->enabled && press_is(&b->press, i), it->enabled);
    }
}

int bar_update(Bar *b, const Input *in)
{
    Rect r[5];
    bar_rects(b, r);
    int i = press_update(&b->press, in, r, b->n);
    return i >= 0 && b->item[i].enabled ? i : -1;
}

// ---------------------------------------------------------------- list

void list_init(List *l, int top, int height, int box_w)
{
    memset(l, 0, sizeof(*l));
    l->top = top;
    l->height = height;
    l->box_w = box_w;
    l->press_row = -1;
}

static int max_scroll(const List *l)
{
    int m = l->count * UI_ROW_H - l->height;
    return m > 0 ? m : 0;
}

void list_fix(List *l)
{
    if (l->sel >= l->count)
        l->sel = l->count - 1;
    if (l->sel < 0)
        l->sel = 0;
    if (l->scroll > max_scroll(l))
        l->scroll = max_scroll(l);
    if (l->scroll < 0)
        l->scroll = 0;
}

void list_show(List *l, int index)
{
    l->sel = index;
    list_fix(l);
    int y = l->sel * UI_ROW_H;
    if (y < l->scroll)
        l->scroll = y;
    if (y + UI_ROW_H > l->scroll + l->height)
        l->scroll = y + UI_ROW_H - l->height;
    list_fix(l);
}

ListEvent list_update(List *l, const Input *in)
{
    ListEvent ev = { LE_NONE, -1 };
    list_fix(l);
    if (in->touch_down)
    {
        l->tracking = in->ty >= l->top && in->ty < l->top + l->height;
        if (!l->tracking)
            return ev;
        l->dragging = false;
        l->start_y = in->ty;
        l->start_scroll = l->scroll;
        l->press_row = (in->ty - l->top + l->scroll) / UI_ROW_H;
        if (l->press_row >= l->count)
            l->press_row = -1;
        ui_dirty_bottom();
        return ev;
    }
    if (l->tracking && in->touching)
    {
        int dy = in->ty - l->start_y;
        if (!l->dragging && (dy > 6 || dy < -6))
        {
            l->dragging = true;
            l->press_row = -1;
            ui_dirty_bottom();
        }
        if (l->dragging)
        {
            int s = l->start_scroll - dy;
            s = s < 0 ? 0 : s > max_scroll(l) ? max_scroll(l) : s;
            if (s != l->scroll)
            {
                l->scroll = s;
                ui_dirty_bottom();
            }
        }
        return ev;
    }
    if (l->tracking)
    {
        // Lifted.
        l->tracking = false;
        int i = l->press_row;
        l->press_row = -1;
        ui_dirty_bottom();
        if (!in->touch_up || l->dragging || i < 0)
            return ev;
        device_click();
        if (l->box_w > 0 && in->tx < l->box_w)
        {
            l->sel = i;
            ev = (ListEvent){ LE_BOX, i };
        }
        else if (i == l->sel)
        {
            ev = (ListEvent){ LE_OPEN, i };
        }
        else
        {
            l->sel = i;
            ev = (ListEvent){ LE_SELECT, i };
        }
        ui_dirty();
        return ev;
    }

    if (l->count == 0)
        return ev;
    int old = l->sel, page = l->height / UI_ROW_H;
    if (in->repeat & KEY_UP)
        l->sel--;
    if (in->repeat & KEY_DOWN)
        l->sel++;
    if (in->repeat & KEY_L)
        l->sel -= page;
    if (in->repeat & KEY_R)
        l->sel += page;
    if (l->sel != old)
    {
        list_show(l, l->sel);
        if (l->sel != old)
            ev = (ListEvent){ LE_SELECT, l->sel };
        ui_dirty();
    }
    else if (in->down & KEY_A)
    {
        ev = (ListEvent){ l->box_w > 0 ? LE_BOX : LE_OPEN, l->sel };
    }
    return ev;
}

void list_draw(List *l, Surface *s, ListRow row)
{
    list_fix(l);
    gfx_fill(s, 0, l->top, SCREEN_W, l->height, C_PAPER);
    int first = l->scroll / UI_ROW_H;
    for (int i = first; i * UI_ROW_H - l->scroll < l->height; i++)
    {
        int y = l->top + i * UI_ROW_H - l->scroll;
        gfx_hline(s, 0, y + UI_ROW_H - 1, SCREEN_W, C_RULE);
        if (i >= l->count)
            continue;
        if (i == l->sel)                       // a stroke of highlighter
            gfx_round_fill(s, l->box_w + 2, y + 2, SCREEN_W - l->box_w - 8, UI_ROW_H - 5, 3, C_SEL);
        else if (i == l->press_row)
            gfx_round_fill(s, l->box_w + 2, y + 2, SCREEN_W - l->box_w - 8, UI_ROW_H - 5, 3,
                           C_PAPER_SH);
    }
    if (l->box_w > 0)
        gfx_vline(s, l->box_w, l->top, l->height, C_MARGIN);
    for (int i = first; i < l->count && i * UI_ROW_H - l->scroll < l->height; i++)
        row(s, i, l->top + i * UI_ROW_H - l->scroll, i == l->sel);

    // Scroll thumb at the right edge.
    int total = l->count * UI_ROW_H;
    if (total > l->height)
    {
        int th = l->height * l->height / total;
        if (th < 12)
            th = 12;
        int ty = l->top + (l->height - th) * l->scroll / max_scroll(l);
        gfx_round_fill(s, SCREEN_W - 4, ty, 3, th, 1, C_INK_SOFT);
    }
}

// ---------------------------------------------------------------- legend

void legend_draw(Surface *s, int y, const char *const *pairs)
{
    int x = 20;
    for (int i = 0; pairs[i] != NULL && pairs[i + 1] != NULL; i += 2)
    {
        gfx_pad_badge(s, x, y - 2, pairs[i][0]);
        x = gfx_text(s, x + 14, y, pairs[i + 1], C_INK_SOFT) + 10;
    }
}
