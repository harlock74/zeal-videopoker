#pragma once

#include <stdint.h>
#include <zvb_gfx.h>

typedef struct {
    gfx_context* vctx;
    uint16_t* entropy;
    void (*quit_cb)(void);
} SplashBindings;

void splash_bind(const SplashBindings* bindings);

/* Blocks on splash until Enter/Space is pressed, with blinking prompt callback. */
void splash_run_blocking(void (*draw_prompt)(uint8_t visible));
