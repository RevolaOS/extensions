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
#include <stddef.h>

#define PROTOCOL_VERSION 1
#define MAX_TRANSFER 4096
#define INFO_BYTES 64
#define SCREEN_BYTES 2048
#define SCREEN_RECORD 17     /* bytes of a record before its text */
#define CAPTURE_MS 50        /* from arming to binding, and from binding to ready */
#define DISPLAY_WINDOW 0x1000 /* the display pipe's register window */
#define CACHE_LINE 32
#define LIFT_THRESHOLD 6     /* what Wheel_HandleInterrupt stores on lift */
#define PADDING 4

#define VENDOR_IN 0xc0
#define VENDOR_OUT 0x40
enum { INFO = 0xd0, READ, SET_ADDRESS, WRITE32, INPUT, PROFILE, SAMPLES, SCREEN, SCREEN_DATA };
enum { INPUT_BUTTONS = 1, INPUT_TOUCH, INPUT_LIFT };
enum { CAPTURE_IDLE, CAPTURE_ARMED, CAPTURE_BOUND, CAPTURE_READY };

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
    uint32_t samples[]; /* (pc, lr) per slot */
};

static const struct rv_api *api;
static rv_handle self;
static void *reply; /* the last IN reply the controller may still be sending */
static volatile uint8_t stopping; /* set by unload, which runs while the hooks are bound */
static uint32_t write_address;
static struct ring *volatile ring;
static uint8_t *screen; /* SCREEN_BYTES, while a capture is pending or unread */
static volatile uint8_t capture;
static uint32_t timer[EventTimer_bytes / 4];

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
    uint16_t record[6]; /* next, owner, directory, live, peak, failure */
    uint32_t unused[4];
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
    if (api->size > offsetof(struct rv_api, debug_info)) {
        const struct rv_debug_info *d = api->debug_info();
        b->core_build = d->core_build;
        b->heap_total = word(d->heap, MainHeap_total_bytes);
        b->heap_free = word(d->heap, MainHeap_free_bytes);
        b->extensions = (uint32_t)d->extensions;
        b->record[0] = d->next;
        b->record[1] = d->owner;
        b->record[2] = d->directory;
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
    default: /* TODO: hold (kind 4) needs a site on the hold switch read */
        return 0;
    }
    w[WheelState_skip_packet] = 0;
    signal_wheel();
    return 1;
}

/* Profiler: every IRQ passes through IRQ_Dispatch with the interrupted task's
 * frame; tasks run in SVC mode, so its banked LR is the task's. */

static uint32_t on_irq(uint32_t *frame, const void *next)
{
    struct ring *r = ring;
    if (r) {
        uint32_t now = Clock_GetMicroseconds(), elapsed = now - r->last;
        if (elapsed >= 1000000 || elapsed * r->rate >= 1000000) {
            uint32_t head = r->head, following = head + 1 == r->slots ? 0 : head + 1;
            r->last = now;
            if (following == r->tail) {
                r->dropped++;
            } else {
                r->samples[head * 2] = frame[IRQFrame_pc / 4];
                r->samples[head * 2 + 1] = frame[IRQFrame_lr / 4];
                r->head = following;
            }
        }
    }
    return api->next(next, (uint32_t)frame, 0, 0, 0, 0, 0);
}

static const struct rv_hook irq_hook = {RV_SITE_IRQ_Dispatch_entry, RV_HOOK_FILTER, 0, 0, on_irq};

/* No IRQ handler can be running while a task runs, so the ring is free once
 * it is unpublished and the hook unbound. */
static void profile_stop(void)
{
    struct ring *r = ring;
    if (!r)
        return;
    ring = 0;
    api->hook_unbind(self, &irq_hook);
    api->free(self, r);
}

static int profile(uint32_t rate, uint32_t entries)
{
    profile_stop();
    if (!rate && !entries)
        return 1;
    if (!rate || !entries)
        return 0;
    struct ring *r = api->alloc(self, sizeof *r + (entries + 1) * 8);
    if (!r)
        return 0;
    r->rate = rate;
    r->slots = entries + 1;
    r->last = Clock_GetMicroseconds();
    ring = r;
    if (api->hook_bind(self, &irq_hook) != RV_OK) {
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
        if (count > (capacity - 8) / 8)
            count = (capacity - 8) / 8;
        dropped = r->dropped - r->reported;
        r->reported += dropped;
    }
    uint32_t *b = new_reply(8 + count * 8);
    if (!b)
        return 0;
    b[0] = count;
    b[1] = dropped;
    for (uint32_t i = 0, tail = r ? r->tail : 0; i < count; i++) {
        b[2 + i * 2] = r->samples[tail * 2];
        b[3 + i * 2] = r->samples[tail * 2 + 1];
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
    uint32_t result = api->next(next, (uint32_t)view, (uint32_t)text, argument, 0, 0, 0);
    if (text)
        record(view, view + TextView_context, text);
    return result;
}

static const struct rv_hook draw_hook = {RV_SITE_TextView_DrawString_entry, RV_HOOK_FILTER, 0, 0,
                                         on_draw};

static void capture_done(void)
{
    api->hook_unbind(self, &draw_hook);
    ((struct screen_header *)screen)->status = 1;
    __asm__ volatile("" ::: "memory"); /* the records are complete before READY */
    capture = CAPTURE_READY;
}

/* UI task. */
static int on_timer(void *recipient, uint8_t *event)
{
    (void)recipient;
    if (word(event, Event_kind) != Event_Timer)
        return 0;
    if (capture == CAPTURE_ARMED) {
        if (api->hook_bind(self, &draw_hook) != RV_OK) {
            capture_done(); /* ready, with nothing in it */
            return 1;
        }
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

/* Requests. Returns -1 when the request is not this extension's, 0 to stall,
 * 1 when handled; IN replies go to *data and *length (the host's wLength on
 * entry). `packet` is EP0's maximum packet size, 0 when there is no USB. */
static int handle(const struct setup *s, void **data, uint32_t *length, uint32_t packet)
{
    if (s->request < INFO || s->request > SCREEN_DATA || stopping)
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
            bytes = 8 + word(b, 0) * 8;
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
        return api->next(next, (uint32_t)usb, (uint32_t)length, 0, 0, 0, 0);
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
        return api->next(next, (uint32_t)usb, 0, 0, 0, 0, 0);
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
    api->hook_unbind(self, &draw_hook);
    profile_stop();
    if (screen)
        api->free(self, screen);
    if (reply)
        api->free(self, reply);
}

static const struct rv_hook hooks[] = {
    {RV_SITE_USB_HandleVendorDeviceRead_call, RV_HOOK_FILTER, 0, 0, on_vendor_in},
    {RV_SITE_USB_DispatchNoDataRequest_call, RV_HOOK_FILTER, 0, 0, on_no_data},
    {RV_SITE_DiskClient_Yield_slot, RV_HOOK_REPLACE, 0, 0, refuse_yield},
};

static const struct rv_extension extension = {
    .size = sizeof extension,
    .hooks = hooks,
    .hook_count = sizeof hooks / sizeof hooks[0],
    .unload = unload,
};

/* Runs on the UI task, so the timers post their events to its queue. */
const struct rv_extension *rvx_entry(const struct rv_api *core, rv_handle handle)
{
    api = core;
    self = handle;
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
