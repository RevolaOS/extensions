/* SPDX-License-Identifier: GPL-3.0-only */

/* The rv_api the host tests hand compressor.c: interrupt masking that counts. */

#include "compressor.h"
#include "rv_api.h"

static uint32_t irq_calls;

static uint32_t irq_save(void)
{
    irq_calls++;
    return 0;
}

static void irq_restore(uint32_t state)
{
    (void)state;
    irq_calls++;
}

uint32_t test_irq_calls(void)
{
    return irq_calls;
}

static const struct rv_api api = {
    .size = sizeof api, .irq_save = irq_save, .irq_restore = irq_restore};

__attribute__((constructor)) static void start(void)
{
    compressor_start(&api);
}
