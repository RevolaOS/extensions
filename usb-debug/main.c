/* SPDX-License-Identifier: GPL-3.0-only */

/* USB Debug. While enabled, a plugged-in iPod stays in the UI and answers vendor
 * control requests on EP0 for `revola debug` (protocol in README.md).
 *
 * Disk mode: the OS disk client owns the volume from boot, and USB mass storage
 * asks it to yield before every command that needs the medium. Declining keeps
 * the volume, so SCSI answers medium not present and the UI never hears of USB.
 *
 * Requests run on USBDeviceTask and must not block. The controller sends IN
 * data by DMA from the buffer the handler returns, after the handler returned
 * and over several packets, so a reply stays valid until the next request and
 * its cache lines are written back first. Apple ends the data stage when the
 * bytes run out and never sends a zero-length packet, so a reply shorter than
 * wLength must not end on a packet boundary; such replies get 4 bytes of
 * padding, and every reply buffer has room for them. */

#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_firmware.h"
#include "rv_sites.h"
#include "rv_ui.h"

#define PROTOCOL_VERSION 2
#define MAX_TRANSFER 4096
#define INFO_BYTES 64
#define SCREEN_BYTES 2048
#define SCREEN_RECORD 17     /* bytes of a record before its text */
#define CAPTURE_MS 50        /* period of `timer`: capture steps and queued work, RESET too */
#define DISPLAY_WINDOW 0x1000 /* the display pipe's register window */
#define CACHE_LINE 32
#define LIFT_THRESHOLD 6     /* what Wheel_HandleInterrupt stores on lift */
#define PADDING 4
#define CLOCK_HZ 1000000     /* Clock_GetMicroseconds */
#define STACK_MARGIN 512     /* left unfilled below the stack pointer */
#define STACK_PATTERN 0x4b415453u /* "STAK" */
#define FLUSH_KEY 1          /* setting the FLUSH sequence number is stored in */

#define VENDOR_IN 0xc0
#define VENDOR_OUT 0x40
enum {
    INFO = 0xd0, READ, SET_ADDRESS, WRITE32, INPUT, PROFILE, SAMPLES, SCREEN, SCREEN_DATA,
    CLOCK, BOOT, HEAP_WATCH, HEAP_STATS, STACK, FAULT, RESET, FLUSH,
    TIME_SITE, TIMINGS, EVENTS, EVENT_LOG, HOLD, EXTENSION, EXTENSION_RESULT,
    LAST_REQUEST = EXTENSION_RESULT
};
enum { INPUT_BUTTONS = 1, INPUT_TOUCH, INPUT_LIFT };
enum { CAPTURE_IDLE, CAPTURE_ARMED, CAPTURE_BOUND, CAPTURE_READY };
enum { FAULT_UNDEFINED = 1, FAULT_ABORT, FAULT_DATA_ABORT };
enum { HOLD_RELEASE, HOLD_LOCKED, HOLD_UNLOCKED };
enum { RESULT_IDLE, RESULT_PENDING, RESULT_DONE };
/* EVENT_LOG kinds beyond rv_trace_kind, from the poll on `timer`. */
enum { EVENT_DISK_POWER = 0x100, EVENT_UNDERRUN };
#define TIMED_SLOTS 8
#define NAME_BYTES 32
#define FLUSH_THEN_RESET 1

struct setup {
    uint8_t type;
    uint8_t request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
};

/* PC samples, written by the IRQ handler, drained by SAMPLES. One slot stays
 * free to tell full from empty. */
struct ring {
    uint32_t rate;     /* samples per second */
    uint32_t last;
    uint32_t head;     /* IRQ only */
    uint32_t tail;     /* USB task only */
    uint32_t slots;
    uint32_t dropped;  /* IRQ only, running count */
    uint32_t reported; /* USB task only: dropped at the last drain */
    uint32_t samples[]; /* (pc, lr, µs) per slot */
};
#define SAMPLE_WORDS 3

static const struct rv_api *api;
static rv_extension_handle self;
static void *reply; /* the last IN reply the controller may still be sending */
static volatile uint8_t stopping; /* set by unload, which runs while the hooks are bound */
static uint32_t write_address;
static struct ring *volatile ring;
static uint8_t *screen; /* SCREEN_BYTES, while a capture is pending or unread */
static volatile uint8_t capture;
static uint32_t timer[EventTimer_bytes / 4];
/* Work a request leaves for the UI task, done at the next tick of `timer`. Each
 * flag is set by the USB task and cleared by the UI task before the work. */
static volatile uint8_t pending_stack, pending_fault, pending_reset, pending_flush, pending_time,
    pending_hold, pending_extension, pending_events;
static uint8_t fault_kind;
static uint8_t flush_mode;      /* FLUSH_THEN_RESET */
static uint16_t flush_delay;    /* 100 µs units from the flush's start to the reset */
static volatile uint8_t reset_armed;
static uint32_t reset_at;       /* µs */

/* µs at the entry, at the first main menu and at the first tick of a UI timer
 * started at the entry; the FLUSH sequence number the settings held at the
 * entry and the last one written. */
static struct {
    uint32_t entry, main_menu, first_idle, flush_at_boot, flush_written;
} boot;

static const void *heap_object;
static uint32_t heap_timer[EventTimer_bytes / 4];
static volatile uint32_t heap_period; /* ms, 0 when not watching */
static struct {
    uint32_t samples, latest, min, max;
} heap;

static uint32_t stack_base, stack_top; /* the UI stack filled from base to top; 0 when not */

static const struct rv_debug_info *debug;  /* NULL on a core without one */
static uint32_t time_request;              /* site id for the next TIME_SITE, 0 stops all */
static uint8_t hold_request;               /* HOLD_* */
static uint16_t events_request;            /* ring entries for the next EVENTS, 0 stops */
static uint8_t extension_enable;
static uint32_t extension_name[NAME_BYTES / 4]; /* written by the host through WRITE32 */
static volatile uint8_t extension_state;   /* RESULT_* */
static int32_t extension_result;

static uint32_t word(const void *base, uint32_t offset)
{
    return *(const volatile uint32_t *)((const uint8_t *)base + offset);
}

RV_ARM static void clean_dcache(const void *start, uint32_t bytes)
{
    uint32_t line = (uint32_t)start & ~(CACHE_LINE - 1), end = (uint32_t)start + bytes;
    for (; line < end; line += CACHE_LINE)
        __asm__ volatile("mcr p15, 0, %0, c7, c10, 1" ::"r"(line) : "memory");
    __asm__ volatile("mcr p15, 0, %0, c7, c10, 4" ::"r"(0) : "memory");
}

/* Event_Signal returns through IRQ_RestoreState with `mov pc, lr`. */
RV_ARM static void signal_wheel(void)
{
    Event_Signal(WheelEvent_Signal);
    __asm__ volatile("" ::: "memory");
}

static void *new_reply(uint32_t bytes)
{
    reply = api->alloc(self, bytes + PADDING);
    return reply;
}

static int in_dram(uint32_t address, uint32_t bytes)
{
    return address >= DRAM_BASE && address < DRAM_END && bytes <= DRAM_END - address;
}

static int in_iram(uint32_t address, uint32_t bytes)
{
    return address >= IRAM_BASE && address < IRAM_END && bytes <= IRAM_END - address;
}

static int in_display(uint32_t address, uint32_t bytes)
{
    return address >= DISPLAY_PIPE_BASE && address < DISPLAY_PIPE_BASE + DISPLAY_WINDOW &&
           bytes <= DISPLAY_PIPE_BASE + DISPLAY_WINDOW - address && !(address & 3) && !(bytes & 3);
}

/* Aligned DRAM goes out in place; IRAM, unaligned DRAM and the display pipe's
 * registers are copied, the registers a word at a time. */
static void *read_memory(uint32_t address, uint32_t bytes)
{
    if (in_dram(address, bytes) && !(address & 3))
        return (void *)address;
    uint8_t *copy;
    if (in_display(address, bytes)) {
        if (!(copy = new_reply(bytes)))
            return 0;
        for (uint32_t i = 0; i < bytes; i += 4)
            *(uint32_t *)(copy + i) = word((const void *)address, i);
    } else if (in_dram(address, bytes) || in_iram(address, bytes)) {
        if (!(copy = new_reply(bytes)))
            return 0;
        for (uint32_t i = 0; i < bytes; i++)
            copy[i] = ((const volatile uint8_t *)address)[i];
    } else {
        return 0;
    }
    return copy;
}

struct info {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t firmware;
    uint32_t core_build;
    uint32_t max_transfer;
    uint32_t display;
    uint32_t heap_total;
    uint32_t heap_free;
    uint32_t extensions;
    uint16_t record[6]; /* next, owner, file, live, peak, failure */
    uint32_t clock_hz;
    uint32_t extension_name; /* where the host writes EXTENSION's file name */
    uint32_t unused[2];
};
_Static_assert(sizeof(struct info) == INFO_BYTES, "INFO is 64 bytes");

static void *info(void)
{
    struct info *b = new_reply(sizeof *b);
    if (!b)
        return 0;
    b->magic = 0x44555652; /* RVUD */
    b->version = PROTOCOL_VERSION;
    b->firmware = RV_FIRMWARE_ID;
    b->max_transfer = MAX_TRANSFER;
    b->display = DISPLAY_PIPE_BASE;
    b->clock_hz = CLOCK_HZ;
    b->extension_name = (uint32_t)extension_name;
    if (RV_HAS(api, debug_info)) {
        const struct rv_debug_info *d = api->debug_info();
        b->core_build = d->core_build;
        b->heap_total = word(d->heap, MainHeap_total_bytes);
        b->heap_free = word(d->heap, MainHeap_free_bytes);
        b->extensions = (uint32_t)d->extensions;
        b->record[0] = d->next;
        b->record[1] = d->owner;
        b->record[2] = d->file;
        b->record[3] = d->live;
        b->record[4] = d->peak;
        b->record[5] = d->failure;
    }
    return b;
}

/* Input: what Wheel_HandleInterrupt does, then wake the wheel task, which
 * dispatches buttons and accumulates motion itself. One step per request: the
 * wheel task has to run between steps, so the host paces them. */

static int input(uint32_t kind, uint32_t argument)
{
    uint8_t *w = WheelState;
    switch (kind) {
    case INPUT_BUTTONS:
        *(volatile uint32_t *)(w + WheelState_buttons) = argument;
        break;
    case INPUT_TOUCH:
        if (argument >= WheelPositions)
            return 0;
        *(volatile uint32_t *)(w + WheelState_touch) = argument | WheelTouch_Down;
        break;
    case INPUT_LIFT:
        *(volatile uint32_t *)(w + WheelState_touch) = 0;
        *(volatile uint32_t *)(w + WheelState_threshold) = LIFT_THRESHOLD;
        *(volatile uint32_t *)(w + WheelState_motion) = 0;
        break;
    default: /* the hold switch is HOLD */
        return 0;
    }
    w[WheelState_skip_packet] = 0;
    signal_wheel();
    return 1;
}

/* The IRQ hook serves the profiler and a reset armed by FLUSH. Every IRQ
 * passes through IRQ_Dispatch with the interrupted task's frame; tasks run in
 * SVC mode, so its banked LR is the task's. */

/* SetWarmBootModeTag only copies the tag, so this also runs in an IRQ. */
RV_ARM static void reset_now(void)
{
    SetWarmBootModeTag(WarmBootMode_None);
    ResetSocAndWait();
}

static uint32_t on_irq(uint32_t *frame, const void *next)
{
    uint32_t now = Clock_GetMicroseconds();
    if (reset_armed && (int32_t)(now - reset_at) >= 0)
        reset_now();
    struct ring *r = ring;
    if (r) {
        uint32_t elapsed = now - r->last;
        if (elapsed >= 1000000 || elapsed * r->rate >= 1000000) {
            uint32_t head = r->head, following = head + 1 == r->slots ? 0 : head + 1;
            r->last = now;
            if (following == r->tail) {
                r->dropped++;
            } else {
                uint32_t *slot = r->samples + head * SAMPLE_WORDS;
                slot[0] = frame[IRQFrame_pc / 4];
                slot[1] = frame[IRQFrame_lr / 4];
                slot[2] = now;
                r->head = following;
            }
        }
    }
    return api->hook_next(next, (uint32_t)frame, 0, 0, 0, 0, 0);
}

static const struct rv_hook irq_hook = RV_HOOK_FILTER(RV_SITE_IRQ_Dispatch_entry, on_irq);

static int irq_bind(void)
{
    int result = api->hook_bind(self, &irq_hook);
    return result == RV_OK || result == RV_ALREADY_BOUND;
}

/* An armed reset keeps the hook until it fires. RV_NOT_BOUND when neither
 * user bound it is fine. */
static void irq_unbind_unused(void)
{
    if (!ring && !reset_armed)
        api->hook_unbind(self, &irq_hook);
}

/* No IRQ handler can be running while a task runs, so the ring is free once
 * it is unpublished and the hook unbound. */
static void profile_stop(void)
{
    struct ring *r = ring;
    if (!r)
        return;
    ring = 0;
    irq_unbind_unused();
    api->free(self, r);
}

static int profile(uint32_t rate, uint32_t entries)
{
    profile_stop();
    if (!rate && !entries)
        return 1;
    if (!rate || !entries)
        return 0;
    struct ring *r = api->alloc(self, sizeof *r + (entries + 1) * SAMPLE_WORDS * 4);
    if (!r)
        return 0;
    r->rate = rate;
    r->slots = entries + 1;
    r->last = Clock_GetMicroseconds();
    ring = r;
    if (!irq_bind()) {
        ring = 0;
        api->free(self, r);
        return 0;
    }
    return 1;
}

static void *samples(uint32_t capacity)
{
    struct ring *r = ring;
    uint32_t count = 0, dropped = 0;
    if (r) {
        uint32_t head = r->head;
        count = head >= r->tail ? head - r->tail : r->slots - r->tail + head;
        if (count > (capacity - 8) / (SAMPLE_WORDS * 4))
            count = (capacity - 8) / (SAMPLE_WORDS * 4);
        dropped = r->dropped - r->reported;
        r->reported += dropped;
    }
    uint32_t *b = new_reply(8 + count * SAMPLE_WORDS * 4);
    if (!b)
        return 0;
    b[0] = count;
    b[1] = dropped;
    for (uint32_t i = 0, tail = r ? r->tail : 0; i < count; i++) {
        for (uint32_t k = 0; k < SAMPLE_WORDS; k++)
            b[2 + i * SAMPLE_WORDS + k] = r->samples[tail * SAMPLE_WORDS + k];
        tail = tail + 1 == r->slots ? 0 : tail + 1;
        r->tail = tail;
    }
    return b;
}

/* Screen capture: every string the UI draws goes through TextView_DrawString,
 * which leaves the view's graphics context holding the string's colour and clip
 * in screen coordinates. A UI timer binds it, invalidates the root view, and
 * unbinds once the redraw has run. Records follow the header, 4-byte aligned. */

struct screen_header {
    uint32_t magic;
    uint16_t status; /* 0 pending, 1 ready */
    uint16_t count;
    uint32_t bytes;  /* header and records */
};

struct screen_record {
    int16_t top, left, bottom, right;
    uint32_t color; /* 0xRRGGBB */
    uint32_t view_class;
    uint8_t length;
    char text[];
};

static void record(const void *view, const uint8_t *context, const char *text)
{
    struct screen_header *h = (struct screen_header *)screen;
    uint32_t length = 0;
    while (length < 255 && text[length])
        length++;
    uint32_t end = (h->bytes + SCREEN_RECORD + length + 3) & ~3u;
    if (end > SCREEN_BYTES - PADDING)
        return;
    struct screen_record *r = (struct screen_record *)(screen + h->bytes);
    r->top = word(context, GraphicsContext_clip_top);
    r->left = word(context, GraphicsContext_clip_left);
    r->bottom = word(context, GraphicsContext_clip_bottom);
    r->right = word(context, GraphicsContext_clip_right);
    const uint8_t *rgba = context + GraphicsContext_color;
    r->color = rgba[0] << 16 | rgba[1] << 8 | rgba[2];
    r->view_class = word(view, 0);
    r->length = length;
    for (uint32_t i = 0; i < length; i++)
        r->text[i] = text[i];
    h->count++;
    h->bytes = end;
}

static uint32_t on_draw(uint8_t *view, const char *text, uint32_t argument, const void *next)
{
    uint32_t result = api->hook_next(next, (uint32_t)view, (uint32_t)text, argument, 0, 0, 0);
    if (text)
        record(view, view + TextView_context, text);
    return result;
}

static const struct rv_hook draw_hook = RV_HOOK_FILTER(RV_SITE_TextView_DrawString_entry, on_draw);

/* A marquee view's text never passes through a draw call; its colour is set
 * just before the pre-rendered text is copied, with the context's clip set. */
static uint32_t on_marquee(uint8_t *context, const uint8_t *rgba, uint8_t *view, const void *next)
{
    uint32_t result = api->hook_next(next, (uint32_t)context, (uint32_t)rgba, (uint32_t)view, 0,
                                     0, 0);
    const char *text = *(const char *const *)(view + TextView_text);
    if (text)
        record(view, context, text);
    return result;
}

static const struct rv_hook marquee_hook =
    RV_HOOK_FILTER(RV_SITE_MarqueeTextView_DrawColor_call, on_marquee);

static void capture_done(void)
{
    api->hook_unbind(self, &draw_hook);
    api->hook_unbind(self, &marquee_hook);
    ((struct screen_header *)screen)->status = 1;
    __asm__ volatile("" ::: "memory"); /* the records are complete before READY */
    capture = CAPTURE_READY;
}

static void do_work(void);

/* UI task. */
static int on_timer(void *recipient, uint8_t *event)
{
    (void)recipient;
    if (word(event, Event_kind) != Event_Timer)
        return 0;
    do_work();
    if (capture == CAPTURE_ARMED) {
        if (api->hook_bind(self, &draw_hook) != RV_OK) {
            capture_done(); /* ready, with nothing in it */
            return 1;
        }
        api->hook_bind(self, &marquee_hook); /* a core without the site misses marquees */
        uint8_t *root = UI_GetRootView();
        capture = CAPTURE_BOUND;
        View_InvalidateRect(root, (const int32_t *)(root + View_bounds));
        Timer_Start(timer);
    } else if (capture == CAPTURE_BOUND) {
        capture_done();
    }
    return 1;
}

static void *const timer_vtable[EventRecipient_event_method / 4 + 1] = {
    [EventRecipient_event_method / 4] = on_timer,
};
static const struct {
    void *const *vtable;
} timer_recipient = {timer_vtable};

static void screen_header(struct screen_header *h)
{
    h->magic = 0x43535652; /* RVSC */
    h->status = 0;
    h->count = 0;
    h->bytes = sizeof *h;
}

static int screen_arm(void)
{
    if (capture == CAPTURE_ARMED || capture == CAPTURE_BOUND)
        return 1;
    if (!screen && !(screen = api->alloc(self, SCREEN_BYTES)))
        return 0;
    screen_header((struct screen_header *)screen);
    capture = CAPTURE_ARMED;
    Timer_Start(timer);
    return 1;
}

/* The finished description once; a pending header before that. */
static void *screen_data(void)
{
    if (capture != CAPTURE_READY) {
        struct screen_header *h = new_reply(sizeof *h);
        if (h)
            screen_header(h);
        return h;
    }
    reply = screen;
    screen = 0;
    capture = CAPTURE_IDLE;
    return reply;
}

/* Clock and boot times. */

static void *clock_now(void)
{
    uint32_t *b = new_reply(4);
    if (b)
        b[0] = Clock_GetMicroseconds();
    return b;
}

static void *boot_times(void)
{
    uint32_t *b = new_reply(sizeof boot);
    if (b) {
        const uint32_t *from = (const uint32_t *)&boot;
        for (uint32_t i = 0; i < sizeof boot / 4; i++)
            b[i] = from[i];
    }
    return b;
}

/* Heap watch: Apple's free bytes sampled on a UI timer. */

static void heap_sample(void)
{
    uint32_t free = word(heap_object, MainHeap_free_bytes);
    if (!heap.samples++ || free < heap.min)
        heap.min = free;
    if (free > heap.max)
        heap.max = free;
    heap.latest = free;
}

static int on_heap_timer(void *recipient, uint8_t *event)
{
    (void)recipient;
    if (word(event, Event_kind) != Event_Timer)
        return 0;
    if (heap_period) {
        heap_sample();
        Timer_Start(heap_timer);
    }
    return 1;
}

static void *const heap_vtable[EventRecipient_event_method / 4 + 1] = {
    [EventRecipient_event_method / 4] = on_heap_timer,
};
static const struct {
    void *const *vtable;
} heap_recipient = {heap_vtable};

static int heap_watch(uint32_t period)
{
    if (!heap_object)
        return 0;
    heap_period = 0;
    if (!period)
        return 1;
    heap.samples = heap.latest = heap.min = heap.max = 0;
    Timer_SetPeriod(heap_timer, period);
    heap_period = period;
    Timer_Start(heap_timer);
    return 1;
}

static void *heap_stats(void)
{
    uint32_t *b = new_reply(20);
    if (!b)
        return 0;
    b[0] = heap.samples;
    b[1] = heap.latest;
    b[2] = heap.min;
    b[3] = heap.max;
    b[4] = heap_object ? word(heap_object, MainHeap_total_bytes) : 0;
    return b;
}

/* Stack watermark: the UI stack grows down from its record's base plus
 * UITask_stack_bytes. Everything below the stack pointer, less a margin, is
 * filled with a pattern on the UI task; the lowest word that changed since is
 * the deepest the stack went. IRQ frames land on the interrupted task's stack
 * and count too. */

RV_ARM static void *current_thread(void)
{
    return Thread_GetCurrentRecord();
}

static void stack_fill(void)
{
    uint32_t base = word(current_thread(), ThreadRecord_stack_base), sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    stack_base = 0;
    if (sp <= base + STACK_MARGIN || sp > base + UITask_stack_bytes)
        return; /* not on the UI stack */
    uint32_t top = (sp - STACK_MARGIN) & ~3u;
    for (volatile uint32_t *p = (uint32_t *)base; p < (uint32_t *)top; p++)
        *p = STACK_PATTERN;
    stack_top = top;
    stack_base = base;
}

static void *stack_mark(void)
{
    uint32_t base = stack_base;
    if (!base)
        return 0;
    uint32_t *b = new_reply(16);
    if (!b)
        return 0;
    const volatile uint32_t *p = (const uint32_t *)base;
    while ((uint32_t)p < stack_top && *p == STACK_PATTERN)
        p++;
    b[0] = base;
    b[1] = UITask_stack_bytes;
    b[2] = stack_top;
    b[3] = (uint32_t)p;
    return b;
}

/* Faults, raised on the UI task so the panic screen and the crash record see
 * this extension's code. */

RV_ARM static void undefined_instruction(void)
{
    __asm__ volatile(".word 0xe7f000f0"); /* permanently undefined */
}

RV_ARM static void software_abort(void)
{
    Runtime_Abort();
}

static void data_abort(void)
{
    (void)*(volatile uint32_t *)MMU_UNMAPPED_ADDRESS; /* no first-level entry */
}

/* FLUSH stores a new sequence number, so the store is dirty and the flush
 * writes; after a reset, the number found at boot tells which copy survived. */
static void flush_now(void)
{
    uint32_t sequence = boot.flush_written + 1;
    if (api->setting_write(self, FLUSH_KEY, RV_SETTING_TYPE_BLOB, &sequence, 4) == RV_OK)
        boot.flush_written = sequence;
    int then_reset = flush_mode & FLUSH_THEN_RESET;
    if (then_reset && flush_delay) {
        reset_at = Clock_GetMicroseconds() + flush_delay * 100u;
        __asm__ volatile("" ::: "memory");
        if (irq_bind())
            reset_armed = 1;
    }
    api->settings_flush();
    if (then_reset && !reset_armed)
        reset_now(); /* no delay, or the IRQ hook could not be bound */
}

/* Site timing: a filter in front of every other handler on the site measures
 * the call through api->hook_next, the other filters and Apple's code included.
 * Each slot has its own handler, since a handler is not told its site. A
 * handler receives the site's register arguments, `next`, then its stack
 * arguments; a seven-word signature covers every core site. */

struct timed {
    uint32_t site;
    uint8_t registers, stack;
    uint32_t calls, total, min, max; /* µs */
};

static struct timed timed[TIMED_SLOTS];
static struct rv_hook timed_hooks[TIMED_SLOTS];

static uint32_t time_call(struct timed *t, const uint32_t *p)
{
    uint32_t a[6] = {0}, n = 0;
    for (uint32_t i = 0; i < t->registers; i++)
        a[n++] = p[i];
    for (uint32_t i = 0; i < t->stack; i++)
        a[n++] = p[t->registers + 1 + i];
    uint32_t start = Clock_GetMicroseconds();
    uint32_t result = api->hook_next((const void *)p[t->registers], a[0], a[1], a[2], a[3], a[4], a[5]);
    uint32_t took = Clock_GetMicroseconds() - start;
    uint32_t state = api->irq_save();
    if (!t->calls++ || took < t->min)
        t->min = took;
    if (took > t->max)
        t->max = took;
    t->total += took;
    api->irq_restore(state);
    return result;
}

#define TIMED(n)                                                                                \
    static uint32_t timed_##n(uint32_t p0, uint32_t p1, uint32_t p2, uint32_t p3, uint32_t p4,   \
                              uint32_t p5, uint32_t p6)                                         \
    {                                                                                           \
        const uint32_t p[7] = {p0, p1, p2, p3, p4, p5, p6};                                     \
        return time_call(&timed[n], p);                                                         \
    }
TIMED(0) TIMED(1) TIMED(2) TIMED(3) TIMED(4) TIMED(5) TIMED(6) TIMED(7)
#undef TIMED
static void *const timed_handlers[TIMED_SLOTS] = {timed_0, timed_1, timed_2, timed_3,
                                                  timed_4, timed_5, timed_6, timed_7};

static void time_stop_all(void)
{
    for (uint32_t i = 0; i < TIMED_SLOTS; i++)
        if (timed[i].site) {
            api->hook_unbind(self, &timed_hooks[i]);
            timed[i].site = 0;
        }
}

/* UI task. A slot with site 0 is free; the result of a bind that failed is
 * not kept, so TIMINGS simply lacks the site. */
static void time_site(uint32_t site)
{
    if (!site) {
        time_stop_all();
        return;
    }
    int argc = debug && RV_HAS(debug, site_argc) ? debug->site_argc(site) : -1;
    uint32_t i = 0;
    while (i < TIMED_SLOTS && timed[i].site && timed[i].site != site)
        i++;
    if (argc < 0 || i == TIMED_SLOTS || timed[i].site)
        return;
    struct timed *t = &timed[i];
    t->registers = argc & 15;
    t->stack = argc >> 4;
    t->calls = t->total = t->min = t->max = 0;
    timed_hooks[i] = (struct rv_hook)RV_HOOK_FILTER_AT(site, timed_handlers[i], INT16_MIN);
    if (api->hook_bind(self, &timed_hooks[i]) == RV_OK)
        t->site = site;
}

struct timing_record {
    uint32_t site, calls, total, min, max;
};

/* {count}, then a record per timed site. */
static void *timings(uint32_t *bytes)
{
    uint32_t *b = new_reply(4 + TIMED_SLOTS * sizeof(struct timing_record));
    if (!b)
        return 0;
    struct timing_record *out = (struct timing_record *)(b + 1);
    uint32_t n = 0;
    for (uint32_t i = 0; i < TIMED_SLOTS; i++) {
        const struct timed *t = &timed[i];
        uint32_t state = api->irq_save();
        if (t->site)
            out[n++] = (struct timing_record){t->site, t->calls, t->total, t->min, t->max};
        api->irq_restore(state);
    }
    b[0] = n;
    *bytes = 4 + n * sizeof(struct timing_record);
    return b;
}

/* Event log: the core's tracepoints, and what the poll on `timer` sees change:
 * the ATA controller's clock gate (open while the disk can be accessed; a
 * powered disk may still be in standby) and the audio output queue's underrun
 * count (one per output buffer filled with silence because the queue was
 * empty). Written on the UI task and drained on the USB task; one slot stays
 * free to tell full from empty. */

struct event {
    uint32_t time; /* µs */
    uint32_t kind; /* rv_trace_kind or EVENT_* */
    uint32_t argument;
};

struct event_ring {
    uint32_t head, tail, slots, dropped, reported;
    uint32_t powered, underruns; /* as the last poll saw them */
    struct event events[];
};

static struct event_ring *volatile events;

static void event_add(uint32_t kind, uint32_t argument)
{
    struct event_ring *r = events;
    if (!r)
        return;
    uint32_t state = api->irq_save();
    uint32_t head = r->head, following = head + 1 == r->slots ? 0 : head + 1;
    if (following == r->tail) {
        r->dropped++;
    } else {
        r->events[head] = (struct event){Clock_GetMicroseconds(), kind, argument};
        r->head = following;
    }
    api->irq_restore(state);
}

static void on_trace(uint32_t kind, uint32_t argument)
{
    event_add(kind, argument);
}

static uint32_t disk_powered(void)
{
    return !(word((const void *)ClockRegister_PowerGate0, 0) & PowerGate0_ATA);
}

static uint32_t underruns(void)
{
    return word(AudioOutputQueue, AudioOutputQueue_underruns);
}

static void events_poll(void)
{
    struct event_ring *r = events;
    if (!r)
        return;
    uint32_t powered = disk_powered(), count = underruns();
    if (powered != r->powered)
        event_add(EVENT_DISK_POWER, r->powered = powered);
    if (count != r->underruns)
        event_add(EVENT_UNDERRUN, r->underruns = count);
}

/* UI task, like the tracepoints, so the probe is never cleared under one. A
 * drain runs with interrupts masked, so it never holds the ring freed here. */
static void events_stop(void)
{
    if (debug && RV_HAS(debug, trace))
        *debug->trace = 0;
    uint32_t state = api->irq_save();
    struct event_ring *r = events;
    events = 0;
    api->irq_restore(state);
    if (r)
        api->free(self, r);
}

static int events_start(uint32_t entries)
{
    events_stop();
    if (!entries)
        return 1;
    struct event_ring *r = api->alloc(self, sizeof *r + (entries + 1) * sizeof(struct event));
    if (!r)
        return 0;
    r->slots = entries + 1;
    r->powered = disk_powered();
    r->underruns = underruns();
    events = r;
    if (debug && RV_HAS(debug, trace))
        *debug->trace = on_trace;
    return 1;
}

/* {count, dropped since the last drain}, then the events. */
static void *event_log(uint32_t capacity)
{
    uint32_t *b = new_reply(capacity);
    if (!b)
        return 0;
    uint32_t state = api->irq_save();
    struct event_ring *r = events;
    uint32_t count = 0, dropped = 0;
    if (r) {
        uint32_t head = r->head;
        count = head >= r->tail ? head - r->tail : r->slots - r->tail + head;
        if (count > (capacity - 8) / sizeof(struct event))
            count = (capacity - 8) / sizeof(struct event);
        dropped = r->dropped - r->reported;
        r->reported += dropped;
    }
    b[0] = count;
    b[1] = dropped;
    struct event *out = (struct event *)(b + 2);
    for (uint32_t i = 0, tail = r ? r->tail : 0; i < count; i++) {
        out[i] = r->events[tail];
        tail = tail + 1 == r->slots ? 0 : tail + 1;
        r->tail = tail;
    }
    api->irq_restore(state);
    return b;
}

/* Hold: Input_HandleButton and idle sleep read the state for themselves, and the
 * system dispatcher compares it with its cached state when a change is posted,
 * which updates the lock icon and releases held keys. So a forced state
 * replaces the read and is then posted. */

static volatile uint32_t hold_forced; /* the state the replaced read returns */

static uint32_t forced_hold(void)
{
    return hold_forced;
}

static const struct rv_hook hold_hook =
    RV_HOOK_REPLACE(RV_SITE_HoldSwitch_GetLogicalState_entry, forced_hold);

RV_ARM static void post_hold_change(void)
{
    HardwareEvent_Post(HardwareSource_HoldChanged);
}

/* UI task. */
static void hold_apply(uint32_t request)
{
    if (request == HOLD_RELEASE) {
        if (api->hook_unbind(self, &hold_hook) != RV_OK)
            return;
    } else {
        hold_forced = request == HOLD_LOCKED;
        int result = api->hook_bind(self, &hold_hook);
        if (result != RV_OK && result != RV_ALREADY_BOUND)
            return;
    }
    post_hold_change();
}

/* Extensions on and off by file name, as Settings > Extensions does; the
 * loader reads the disk and binds sites, so it runs on the UI task. */
static void extension_apply(void)
{
    char *file = (char *)extension_name;
    file[NAME_BYTES - 1] = 0;
    extension_result = debug && RV_HAS(debug, set_enabled)
                           ? debug->set_enabled(file, extension_enable)
                           : RV_NOT_FOUND;
    extension_state = RESULT_DONE;
}

static void *extension_reply(void)
{
    uint32_t *b = new_reply(8);
    if (b) {
        b[0] = extension_state;
        b[1] = (uint32_t)extension_result;
    }
    return b;
}

/* First main menu: at boot the first layout shown without an animation, after
 * the first UI timer tick. The hook is bound at the entry and unbound by its
 * first call; the layout manager does not exist yet at a boot-time entry. */
static uint32_t on_layout_shown(void *manager, void *layout, const void *next);

static const struct rv_hook layout_hook =
    RV_HOOK_FILTER(RV_SITE_UI_DisplayPreparedLayout_call, on_layout_shown);

static uint32_t on_layout_shown(void *manager, void *layout, const void *next)
{
    uint32_t result = api->hook_next(next, (uint32_t)manager, (uint32_t)layout, 0, 0, 0, 0);
    if (word(UI_GetLayoutManager(), LayoutManager_layout) == RES_MainMenus_Main_Layout)
        boot.main_menu = Clock_GetMicroseconds();
    api->hook_unbind(self, &layout_hook);
    return result;
}

static void first_idle(void *arg)
{
    (void)arg;
    boot.first_idle = Clock_GetMicroseconds();
}

static int queue(volatile uint8_t *flag)
{
    *flag = 1;
    Timer_Start(timer);
    return 1;
}

/* UI task, at a tick of `timer`. */
static void do_work(void)
{
    if (pending_stack) {
        pending_stack = 0;
        stack_fill();
    }
    if (pending_flush) {
        pending_flush = 0;
        flush_now();
    }
    if (pending_reset)
        reset_now();
    if (pending_time) {
        pending_time = 0;
        time_site(time_request);
    }
    if (pending_hold) {
        pending_hold = 0;
        hold_apply(hold_request);
    }
    if (pending_extension) {
        pending_extension = 0;
        extension_apply();
    }
    if (pending_events) {
        pending_events = 0;
        events_start(events_request);
    }
    events_poll();
    if (events)
        Timer_Start(timer);
    if (pending_fault) {
        pending_fault = 0;
        if (fault_kind == FAULT_UNDEFINED)
            undefined_instruction();
        else if (fault_kind == FAULT_ABORT)
            software_abort();
        else
            data_abort();
    }
}

/* Requests. Returns -1 when the request is not this extension's, 0 to stall,
 * 1 when handled; IN replies go to *data and *length (the host's wLength on
 * entry). `packet` is EP0's maximum packet size, 0 when there is no USB. */
static int handle(const struct setup *s, void **data, uint32_t *length, uint32_t packet)
{
    if (s->request < INFO || s->request > LAST_REQUEST || stopping)
        return -1;
    if (reply) {
        api->free(self, reply);
        reply = 0;
    }
    uint32_t value = s->value | (uint32_t)s->index << 16;
    if (s->type == VENDOR_OUT) {
        switch (s->request) {
        case SET_ADDRESS:
            write_address = value;
            return 1;
        case WRITE32:
            if ((write_address & 3) ||
                !(in_dram(write_address, 4) || in_iram(write_address, 4)))
                return 0;
            *(volatile uint32_t *)write_address = value;
            write_address += 4;
            return 1;
        case INPUT:
            return input(s->value & 0xff, s->index);
        case PROFILE:
            return profile(s->value, s->index);
        case SCREEN:
            return screen_arm();
        case HEAP_WATCH:
            return heap_watch(s->value);
        case FAULT:
            if (s->value < FAULT_UNDEFINED || s->value > FAULT_DATA_ABORT)
                return 0;
            fault_kind = s->value;
            return queue(&pending_fault);
        case RESET:
            return queue(&pending_reset);
        case FLUSH:
            flush_mode = s->value;
            flush_delay = s->index;
            return queue(&pending_flush);
        case TIME_SITE:
            time_request = value;
            return queue(&pending_time);
        case EVENTS:
            events_request = s->value;
            return queue(&pending_events);
        case HOLD:
            if (s->value > HOLD_UNLOCKED)
                return 0;
            hold_request = s->value;
            return queue(&pending_hold);
        case EXTENSION:
            if (s->value > 1 || extension_state == RESULT_PENDING)
                return 0;
            extension_enable = s->value;
            extension_state = RESULT_PENDING;
            return queue(&pending_extension);
        }
        return 0;
    }
    if (s->type != VENDOR_IN || *length > MAX_TRANSFER)
        return 0;
    uint32_t bytes = *length;
    uint8_t *b;
    switch (s->request) {
    case INFO:
        b = info();
        bytes = bytes < INFO_BYTES ? bytes : INFO_BYTES;
        break;
    case READ:
        b = read_memory(value, bytes);
        break;
    case SAMPLES:
        if (bytes < 8)
            return 0;
        b = samples(bytes);
        if (b)
            bytes = 8 + word(b, 0) * SAMPLE_WORDS * 4;
        break;
    case CLOCK:
        b = clock_now();
        bytes = bytes < 4 ? bytes : 4;
        break;
    case BOOT:
        b = boot_times();
        bytes = bytes < sizeof boot ? bytes : sizeof boot;
        break;
    case TIMINGS:
        b = timings(&bytes);
        break;
    case EVENT_LOG:
        if (bytes < 8)
            return 0;
        b = event_log(bytes);
        if (b)
            bytes = 8 + word(b, 0) * sizeof(struct event);
        break;
    case EXTENSION_RESULT:
        b = extension_reply();
        bytes = bytes < 8 ? bytes : 8;
        break;
    case HEAP_STATS:
        b = heap_stats();
        bytes = bytes < 20 ? bytes : 20;
        break;
    case STACK:
        b = stack_mark();
        bytes = bytes < 16 ? bytes : 16;
        if (b && s->value == 1)
            queue(&pending_stack);
        break;
    case SCREEN_DATA:
        b = screen_data();
        if (b && ((struct screen_header *)b)->bytes < bytes)
            bytes = ((struct screen_header *)b)->bytes;
        break;
    default:
        return 0;
    }
    if (!b)
        return 0;
    if (bytes < *length && packet && !(bytes & (packet - 1)))
        bytes += PADDING;
    clean_dcache(b, bytes);
    *data = b;
    *length = bytes;
    return 1;
}

static const struct setup *setup_of(uint8_t *usb)
{
    return (const struct setup *)(usb + USBDevice_setup);
}

/* USB_DispatchControlRead, bmRequestType 0xC0: (usb, &length) → data or 0. */
static uint32_t on_vendor_in(uint8_t *usb, uint32_t *length, const void *next)
{
    void *data;
    int result = handle(setup_of(usb), &data, length, word(usb, USBDevice_max_packet));
    if (result < 0)
        return api->hook_next(next, (uint32_t)usb, (uint32_t)length, 0, 0, 0, 0);
    return result ? (uint32_t)data : 0;
}

/* USB_HandleSetupPacket, OUT without data: (usb) → handled. */
static uint32_t on_no_data(uint8_t *usb, const void *next)
{
    const struct setup *s = setup_of(usb);
    uint32_t length = 0;
    void *data;
    int result = s->type == VENDOR_OUT ? handle(s, &data, &length, 0) : -1;
    if (result < 0)
        return api->hook_next(next, (uint32_t)usb, 0, 0, 0, 0, 0);
    return result;
}

static uint32_t refuse_yield(void *client)
{
    (void)client;
    return 0;
}

#ifdef USB_DEBUG_MAILBOX
/* Emulator test build: the emulator has no USB controller. An agent writes a
 * setup packet into the mailbox and sets status to MAILBOX_REQUEST; a UI timer
 * runs it through the same handler and leaves the reply's address and length.
 * The mailbox's address is logged through api->log. */
enum { MAILBOX_IDLE, MAILBOX_REQUEST, MAILBOX_DONE, MAILBOX_STALL, MAILBOX_UNKNOWN };
#define MAILBOX_MS 100

static struct {
    char tag[8];
    struct setup setup;
    uint32_t status;
    uint32_t data;
    uint32_t length;
} mailbox = {.tag = "USBDBG"};
static uint32_t mailbox_timer[EventTimer_bytes / 4];

static int on_mailbox(void *recipient, uint8_t *event)
{
    (void)recipient;
    if (word(event, Event_kind) != Event_Timer)
        return 0;
    if (mailbox.status == MAILBOX_REQUEST) {
        void *data = 0;
        uint32_t length = mailbox.setup.length;
        int result = handle(&mailbox.setup, &data, &length, 0);
        mailbox.data = (uint32_t)data;
        mailbox.length = data ? length : 0;
        mailbox.status = result < 0 ? MAILBOX_UNKNOWN : result ? MAILBOX_DONE : MAILBOX_STALL;
    }
    Timer_Start(mailbox_timer);
    return 1;
}

static void *const mailbox_vtable[EventRecipient_event_method / 4 + 1] = {
    [EventRecipient_event_method / 4] = on_mailbox,
};
static const struct {
    void *const *vtable;
} mailbox_recipient = {mailbox_vtable};
#endif

static void unload(void)
{
    stopping = 1;
#ifdef USB_DEBUG_MAILBOX
    Timer_Stop(mailbox_timer);
#endif
    Timer_Stop(timer);
    heap_period = 0;
    Timer_Stop(heap_timer);
    api->hook_unbind(self, &draw_hook);
    api->hook_unbind(self, &marquee_hook);
    api->hook_unbind(self, &layout_hook);
    if (api->hook_unbind(self, &hold_hook) == RV_OK)
        post_hold_change();
    time_stop_all();
    events_stop();
    profile_stop();
    if (screen)
        api->free(self, screen);
    if (reply)
        api->free(self, reply);
}

static const struct rv_hook hooks[] = {
    RV_HOOK_FILTER(RV_SITE_USB_HandleVendorDeviceRead_call, on_vendor_in),
    RV_HOOK_FILTER(RV_SITE_USB_DispatchNoDataRequest_call, on_no_data),
    RV_HOOK_REPLACE(RV_SITE_DiskClient_Yield_slot, refuse_yield),
};

static const struct rv_descriptor extension = {
    .size = sizeof extension,
    RV_HOOKS(hooks),
    .unload = unload,
};

/* Runs on the UI task, so the timers post their events to its queue. */
const struct rv_descriptor *rv_module_entry(const struct rv_api *core, rv_extension_handle handle)
{
    api = core;
    self = handle;
    boot.entry = Clock_GetMicroseconds();
    stack_fill();
    if (RV_HAS(api, debug_info)) {
        debug = api->debug_info();
        heap_object = debug->heap;
    }
    api->hook_bind(self, &layout_hook); /* a core without the site records no main menu */
    api->setting_read(self, FLUSH_KEY, &boot.flush_at_boot, 4);
    boot.flush_written = boot.flush_at_boot;
    api->timer_start(self, 1, 0, first_idle, 0);
    UI_InitializeEventTimer((void *)&heap_recipient, heap_timer, CAPTURE_MS, 0);
    /* Timer_Start ignores a timer whose period is unset. */
    UI_InitializeEventTimer((void *)&timer_recipient, timer, CAPTURE_MS, 0);
    Timer_SetPeriod(timer, CAPTURE_MS);
#ifdef USB_DEBUG_MAILBOX
    UI_InitializeEventTimer((void *)&mailbox_recipient, mailbox_timer, MAILBOX_MS, 0);
    Timer_SetPeriod(mailbox_timer, MAILBOX_MS);
    Timer_Start(mailbox_timer);
    api->log(self, mailbox.tag);
#endif
    return &extension;
}
