// OPL driver for the browser. SPDX-License-Identifier: GPL-2.0-or-later
//
// Chocolate Doom's SDL driver runs the OPL3 emulator inside the audio
// callback, which Emscripten delivers on the main thread, so synthesis
// competed with rendering for the frame budget and any long frame also
// starved the audio device. This driver keeps the emulator on the audio
// thread instead (src/opl_worklet.js, an AudioWorklet), and the engine only
// schedules register writes. Music time is a virtual clock kept LOOKAHEAD_US
// ahead of the audio device; the queued music callbacks run on the main
// thread as that clock advances, and each register write they make is stamped
// with the audio frame at which the worklet must apply it.
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <emscripten.h>
#include <emscripten/eventloop.h>
#include "opl.h"
#include "opl_internal.h"
#include "opl_queue.h"
#include "opl_worklet.h"

// How far ahead of the audio device writes are scheduled. A long frame on the
// main thread shorter than this leaves the music unaffected; it is also how
// long a song change takes to be heard.
#define LOOKAHEAD_US 150000
#define ADVANCE_INTERVAL_MS 20
#define BATCH_WRITES 256
#define READY_TIMEOUT_MS 5000

// Host state lives on the Module object; see OPL_Worklet_Preload.
EM_JS(void, host_preload, (), {
    const opl = Module.opl || (Module.opl = {state: 0, pending: []});
    if (opl.context) return;
    const fail = (cause) => { opl.state = -1; console.warn('[DOOM] Music unavailable:', cause); };
    try {
        const context = opl.context = new AudioContext();
        Promise.all([
            fetch(new URL('opl3.wasm', import.meta.url)).then(async (response) => {
                if (!response.ok) throw new Error(`opl3.wasm: HTTP ${response.status}`);
                return response.arrayBuffer();
            }),
            context.audioWorklet.addModule(new URL('opl-worklet.js', import.meta.url)),
        ]).then(([wasm]) => {
            const node = new AudioWorkletNode(context, 'doom-opl', {
                numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2],
                processorOptions: {wasm},
            });
            node.onprocessorerror = () => fail('worklet processor error');
            node.connect(context.destination);
            opl.node = node;
            for (const message of opl.pending) node.port.postMessage(message.data, message.transfer);
            opl.pending = [];
            opl.state = 1;
        }).catch(fail);
    } catch (cause) { fail(cause); }
});
EM_JS(int, host_state, (), { return Module.opl ? Module.opl.state : 0; });
EM_JS(double, host_now, (), { return Module.opl.context.currentTime; });
EM_JS(int, host_sample_rate, (), { return Module.opl.context.sampleRate; });
EM_JS(void, host_post, (const double *triples, int count, int muted), {
    const opl = Module.opl || (Module.opl = {state: 0, pending: []});
    if (opl.state < 0) return;
    let message;
    if (count >= 0) {
        const view = HEAPF64.slice(triples >> 3, (triples >> 3) + count * 3);
        message = {data: {type: 'writes', triples: view}, transfer: [view.buffer]};
    } else {
        message = {data: {type: 'mute', muted: muted !== 0}, transfer: []};
    }
    if (opl.node) opl.node.port.postMessage(message.data, message.transfer);
    else opl.pending.push(message);
});
EM_JS(void, host_shutdown, (), {
    const opl = Module.opl;
    if (!opl || !opl.context) return;
    if (opl.node) opl.node.disconnect();
    opl.context.close();
    Module.opl = undefined;
});

typedef struct
{
    unsigned int rate;        // Number of times the timer is advanced per sec.
    unsigned int enabled;     // Non-zero if timer is enabled.
    unsigned int value;       // Last value that was set.
    uint64_t expire_time;     // Calculated time that timer will expire.
} opl_timer_t;

static opl_callback_queue_t *callback_queue;
static int initialized;
static int sample_rate;

// Virtual music time in microseconds. It stops while paused.
static uint64_t current_time;

// Audio context time (seconds) corresponding to current_time zero. Each pause
// pushes it forward by the pause's length, so music resumes where it stopped.
static double base_seconds;
static double paused_at;
static int engine_paused, browser_paused, effectively_paused;

static double triples[BATCH_WRITES * 3];
static int batched;

static int register_num;
static opl_timer_t timer1 = { 12500, 0, 0, 0 };
static opl_timer_t timer2 = { 3125, 0, 0, 0 };
static int advance_interval = -1;

static void Flush(void)
{
    if (batched > 0)
    {
        host_post(triples, batched, 0);
        batched = 0;
    }
}

static void ScheduleWrite(unsigned int reg, unsigned int value)
{
    double *triple = triples + batched * 3;

    triple[0] = round((base_seconds + current_time / 1e6) * sample_rate);
    triple[1] = reg;
    triple[2] = value;

    if (++batched == BATCH_WRITES)
    {
        Flush();
    }
}

// Run every callback due by `target`, then move the clock there.

static void RunUntil(uint64_t target)
{
    opl_callback_t callback;
    void *callback_data;
    uint64_t due;

    while (!OPL_Queue_IsEmpty(callback_queue)
        && (due = OPL_Queue_Peek(callback_queue)) <= target)
    {
        if (!OPL_Queue_Pop(callback_queue, &callback, &callback_data))
        {
            break;
        }

        if (due > current_time)
        {
            current_time = due;
        }

        callback(callback_data);
    }

    if (target > current_time)
    {
        current_time = target;
    }
}

static uint64_t AudioTime(void)
{
    double seconds = host_now() - base_seconds;

    return seconds > 0 ? (uint64_t) (seconds * 1e6) : 0;
}

// Interval timer. Independent of the game loop, so music keeps time through
// blocking waits such as the screen melt.

static void Advance(void *unused)
{
    (void) unused;

    if (!effectively_paused)
    {
        RunUntil(AudioTime() + LOOKAHEAD_US);
    }

    Flush();
}

static void UpdatePaused(void)
{
    int paused = engine_paused || browser_paused;

    if (paused == effectively_paused)
    {
        return;
    }

    effectively_paused = paused;

    if (!initialized)
    {
        return;
    }

    if (paused)
    {
        paused_at = host_now();
    }
    else
    {
        base_seconds += host_now() - paused_at;
    }

    Flush();
}

void OPL_Worklet_Preload(void)
{
    host_preload();
}

void OPL_Worklet_SetBrowserPaused(int paused)
{
    browser_paused = paused != 0;
    UpdatePaused();
}

void OPL_Worklet_SetMuted(int muted)
{
    host_post(NULL, -1, muted);
}

static int OPL_Worklet_Init(unsigned int port_base)
{
    int waited;

    (void) port_base;

    host_preload();

    // Yield to the browser until the worklet has loaded. Asyncify makes this
    // wait possible from the engine's synchronous initialisation.

    for (waited = 0; host_state() == 0 && waited < READY_TIMEOUT_MS; waited += 10)
    {
        emscripten_sleep(10);
    }

    if (host_state() != 1)
    {
        printf("OPL_Worklet_Init: AudioWorklet unavailable; music disabled.\n");
        return 0;
    }

    sample_rate = host_sample_rate();
    callback_queue = OPL_Queue_Create();
    current_time = 0;
    base_seconds = host_now();
    batched = 0;
    engine_paused = effectively_paused = 0;
    initialized = 1;

    // A pause requested while loading takes effect now.
    UpdatePaused();

    advance_interval = emscripten_set_interval(Advance, ADVANCE_INTERVAL_MS, NULL);

    return 1;
}

static void OPL_Worklet_Shutdown(void)
{
    if (!initialized)
    {
        return;
    }

    emscripten_clear_interval(advance_interval);
    Flush();
    host_shutdown();
    OPL_Queue_Destroy(callback_queue);
    callback_queue = NULL;
    initialized = 0;
}

static unsigned int OPL_Worklet_PortRead(opl_port_t port)
{
    unsigned int result = 0;

    if (port == OPL_REGISTER_PORT_OPL3)
    {
        return 0xff;
    }

    if (timer1.enabled && current_time > timer1.expire_time)
    {
        result |= 0x80;   // Either have expired
        result |= 0x40;   // Timer 1 has expired
    }

    if (timer2.enabled && current_time > timer2.expire_time)
    {
        result |= 0x80;   // Either have expired
        result |= 0x20;   // Timer 2 has expired
    }

    return result;
}

static void OPLTimer_CalculateEndTime(opl_timer_t *timer)
{
    int tics;

    // If the timer is enabled, calculate the time when the timer
    // will expire.

    if (timer->enabled)
    {
        tics = 0x100 - timer->value;
        timer->expire_time = current_time
                           + ((uint64_t) tics * OPL_SECOND) / timer->rate;
    }
}

static void WriteRegister(unsigned int reg_num, unsigned int value)
{
    switch (reg_num)
    {
        case OPL_REG_TIMER1:
            timer1.value = value;
            OPLTimer_CalculateEndTime(&timer1);
            break;

        case OPL_REG_TIMER2:
            timer2.value = value;
            OPLTimer_CalculateEndTime(&timer2);
            break;

        case OPL_REG_TIMER_CTRL:
            if (value & 0x80)
            {
                timer1.enabled = 0;
                timer2.enabled = 0;
            }
            else
            {
                if ((value & 0x40) == 0)
                {
                    timer1.enabled = (value & 0x01) != 0;
                    OPLTimer_CalculateEndTime(&timer1);
                }

                if ((value & 0x20) == 0)
                {
                    timer2.enabled = (value & 0x02) != 0;
                    OPLTimer_CalculateEndTime(&timer2);
                }
            }

            break;

        default:
            ScheduleWrite(reg_num, value);
            break;
    }
}

static void OPL_Worklet_PortWrite(opl_port_t port, unsigned int value)
{
    if (port == OPL_REGISTER_PORT)
    {
        register_num = value;
    }
    else if (port == OPL_REGISTER_PORT_OPL3)
    {
        register_num = value | 0x100;
    }
    else if (port == OPL_DATA_PORT)
    {
        WriteRegister(register_num, value);
    }
}

static void OPL_Worklet_SetCallback(uint64_t us, opl_callback_t callback,
                                    void *data)
{
    OPL_Queue_Push(callback_queue, callback, data, current_time + us);
}

static void OPL_Worklet_ClearCallbacks(void)
{
    OPL_Queue_Clear(callback_queue);
}

// Callbacks only run from the interval timer, never concurrently with the
// engine, so there is nothing to lock.

static void OPL_Worklet_Lock(void)
{
}

static void OPL_Worklet_Unlock(void)
{
}

static void OPL_Worklet_SetPaused(int paused)
{
    engine_paused = paused != 0;
    UpdatePaused();
}

static void OPL_Worklet_AdjustCallbacks(float factor)
{
    OPL_Queue_AdjustCallbacks(callback_queue, current_time, factor);
}

// Chip detection waits for a timer to expire. Time is virtual here, so the
// wait is simply the clock moving on; the pending writes go out in order.

static void OPL_Worklet_Delay(uint64_t us)
{
    RunUntil(current_time + us);
    Flush();
}

opl_driver_t opl_worklet_driver =
{
    "AudioWorklet",
    OPL_Worklet_Init,
    OPL_Worklet_Shutdown,
    OPL_Worklet_PortRead,
    OPL_Worklet_PortWrite,
    OPL_Worklet_SetCallback,
    OPL_Worklet_ClearCallbacks,
    OPL_Worklet_Lock,
    OPL_Worklet_Unlock,
    OPL_Worklet_SetPaused,
    OPL_Worklet_AdjustCallbacks,
    OPL_Worklet_Delay,
};
