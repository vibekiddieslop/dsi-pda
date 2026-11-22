#include "sfx.h"

#include <stddef.h>
#include <stdint.h>

#include <nds.h>

#define NOISE 0xFF   // duty value: the step is a noise burst

typedef struct {
    uint16_t freq;   // Hz, 0 = rest
    uint8_t frames;
    uint8_t vol;     // 0..127
    uint8_t duty;    // DutyCycle_*, or NOISE
} Step;

// Notes, Hz.
enum {
    G3 = 196, A3 = 220, C5 = 523, D5 = 587, E5 = 659, G5 = 784, B5 = 988, C6 = 1047,
    E6 = 1319, G6 = 1568, A6 = 1760, C7 = 2093, D7 = 2349,
};

#define SQ DutyCycle_25
#define SEQ(name, ...) static const Step name[] = { __VA_ARGS__ }

SEQ(S_TAP, { 2400, 2, 36, DutyCycle_12 });
SEQ(S_OPEN, { B5, 3, 40, SQ }, { E6, 4, 40, SQ });
SEQ(S_BACK, { E6, 3, 36, SQ }, { B5, 4, 36, SQ });
SEQ(S_STAMP, { 900, 2, 72, NOISE }, { A3, 2, 60, DutyCycle_50 }, { G6, 2, 44, SQ },
    { C7, 4, 40, SQ });
SEQ(S_UNCHECK, { G5, 3, 36, SQ }, { D5, 4, 36, SQ });
SEQ(S_SAVED, { C6, 3, 44, SQ }, { E6, 3, 44, SQ }, { G6, 3, 44, SQ }, { C7, 6, 44, SQ });
SEQ(S_DELETE, { 3000, 2, 60, NOISE }, { 1800, 2, 56, NOISE }, { 1000, 3, 50, NOISE },
    { 500, 4, 44, NOISE });
SEQ(S_ERROR, { G3, 5, 60, DutyCycle_50 }, { 0, 2, 0, SQ }, { G3, 7, 60, DutyCycle_50 });
SEQ(S_LAUNCH, { C5, 3, 50, DutyCycle_50 }, { E5, 3, 50, DutyCycle_50 },
    { G5, 3, 50, DutyCycle_50 }, { C6, 3, 50, DutyCycle_50 }, { E6, 3, 50, DutyCycle_50 },
    { G6, 3, 50, DutyCycle_50 }, { C7, 10, 50, DutyCycle_50 });
SEQ(S_TIMER, { G6, 3, 44, SQ }, { 0, 2, 0, SQ }, { G6, 3, 44, SQ }, { 0, 2, 0, SQ },
    { C7, 5, 44, SQ });
SEQ(S_POP, { A6, 2, 30, DutyCycle_12 }, { D7, 2, 30, DutyCycle_12 });
// The alarm: a rising call, twice, then two pips and a breath.
SEQ(S_ALARM, { E6, 6, 110, DutyCycle_50 }, { G6, 6, 110, DutyCycle_50 },
    { C7, 6, 110, DutyCycle_50 }, { G6, 6, 110, DutyCycle_50 }, { E6, 6, 110, DutyCycle_50 },
    { G6, 6, 110, DutyCycle_50 }, { C7, 14, 110, DutyCycle_50 }, { 0, 10, 0, SQ },
    { C7, 5, 110, DutyCycle_50 }, { 0, 3, 0, SQ }, { C7, 5, 110, DutyCycle_50 },
    { 0, 26, 0, SQ });

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
static const struct {
    const Step *seq;
    int n;
} TABLE[SFX_COUNT] = {
    [SFX_TAP] = { S_TAP, COUNT(S_TAP) },         [SFX_OPEN] = { S_OPEN, COUNT(S_OPEN) },
    [SFX_BACK] = { S_BACK, COUNT(S_BACK) },      [SFX_STAMP] = { S_STAMP, COUNT(S_STAMP) },
    [SFX_UNCHECK] = { S_UNCHECK, COUNT(S_UNCHECK) },
    [SFX_SAVED] = { S_SAVED, COUNT(S_SAVED) },   [SFX_DELETE] = { S_DELETE, COUNT(S_DELETE) },
    [SFX_ERROR] = { S_ERROR, COUNT(S_ERROR) },   [SFX_LAUNCH] = { S_LAUNCH, COUNT(S_LAUNCH) },
    [SFX_TIMER] = { S_TIMER, COUNT(S_TIMER) },   [SFX_POP] = { S_POP, COUNT(S_POP) },
};

typedef struct {
    const Step *seq;   // NULL: silent
    int n, i, left;
    int ch;            // sound id of the step playing, -1 for none
    bool loop;
} Voice;

static Voice fx = { NULL, 0, 0, 0, -1, false };
static Voice ring = { NULL, 0, 0, 0, -1, true };
static bool enabled = true;

static void voice_quiet(Voice *v)
{
    if (v->ch >= 0)
        soundKill(v->ch);
    v->ch = -1;
}

static void voice_start_step(Voice *v)
{
    voice_quiet(v);
    const Step *st = &v->seq[v->i];
    v->left = st->frames;
    if (st->freq == 0)
        return;
    if (st->duty == NOISE)
        v->ch = soundPlayNoise(st->freq, st->vol, 64);
    else
        v->ch = soundPlayPSG((DutyCycle)st->duty, st->freq, st->vol, 64);
}

static void voice_play(Voice *v, const Step *seq, int n)
{
    v->seq = seq;
    v->n = n;
    v->i = 0;
    voice_start_step(v);
}

static void voice_stop(Voice *v)
{
    voice_quiet(v);
    v->seq = NULL;
}

static void voice_tick(Voice *v)
{
    if (v->seq == NULL || --v->left > 0)
        return;
    if (++v->i >= v->n)
    {
        if (!v->loop)
        {
            voice_stop(v);
            return;
        }
        v->i = 0;
    }
    voice_start_step(v);
}

void sfx_set_enabled(bool on)
{
    enabled = on;
    if (!on)
        voice_stop(&fx);
}

bool sfx_enabled(void)
{
    return enabled;
}

void sfx_play(Sfx id)
{
    if (!enabled || id >= SFX_COUNT || ring.seq != NULL)
        return;
    voice_play(&fx, TABLE[id].seq, TABLE[id].n);
}

void sfx_alarm(bool on)
{
    if (on && ring.seq == NULL)
    {
        voice_stop(&fx);
        voice_play(&ring, S_ALARM, COUNT(S_ALARM));
    }
    else if (!on && ring.seq != NULL)
    {
        voice_stop(&ring);
    }
}

void sfx_tick(void)
{
    voice_tick(&fx);
    voice_tick(&ring);
}
