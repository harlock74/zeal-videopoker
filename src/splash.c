#include <stdint.h>
#include <stdlib.h>

#include <zos_sys.h>
#include <zvb_gfx.h>
#include <zgdk.h>

#include "main.h"
#include "app_state.h"
#include "audio.h"
#include "splash.h"
#include "input.h"

/* Keep splash prompt cadence local to the splash module. */
static const uint8_t kSplashBlinkFrames = 24;

void splash_run_blocking(void (*draw_prompt)(uint8_t visible))
{
    uint8_t blink_counter = 0;
    uint8_t prompt_visible = 1;

    /* Start from a clean input state so first press is always accepted. */
    input_events_flush();

    start_splash_music();

    while (1) {
        KeyEvents ev;

        sound_loop();
        tick_current_music();
        entropy++;
        input_poll_events(&ev);

        if (ev.start) {
            goto splash_pressed;
        }
        if (ev.quit) {
            deinit();
            exit(0);
        }

        gfx_wait_vblank(&vctx);
        gfx_wait_end_vblank(&vctx);

        blink_counter++;
        if (blink_counter >= kSplashBlinkFrames) {
            blink_counter = 0;
            prompt_visible = (uint8_t)!prompt_visible;
            draw_prompt(prompt_visible);
        }
    }

splash_pressed:
    stop_current_music();
    msleep(40);
    while (1) {
        KeyEvents ev;

        input_poll_events(&ev);
        if (!ev.start) {
            break;
        }
        gfx_wait_vblank(&vctx);
        gfx_wait_end_vblank(&vctx);
    }

    input_events_flush();
}
