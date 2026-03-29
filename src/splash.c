#include <stdint.h>
#include <stdlib.h>

#include <zos_sys.h>
#include <zos_vfs.h>
#include <zos_keyboard.h>
#include <zvb_gfx.h>
#include <zgdk.h>

#include "videopoker.h"
#include "audio.h"
#include "splash.h"

static SplashBindings g_splash = {0};
/* Static input buffers reduce stack usage in splash loop on SDCC. */
static uint8_t g_splash_read_buf[32];
static uint8_t g_splash_release_buf[16];

void splash_bind(const SplashBindings* bindings)
{
    g_splash = *bindings;
}

/* Keep splash prompt cadence local to the splash module. */
static const uint8_t kSplashBlinkFrames = 24;

void splash_run_blocking(void (*draw_prompt)(uint8_t visible))
{
    uint8_t blink_counter = 0;
    uint8_t prompt_visible = 1;

    /* Start from a clean input state so first press is always accepted. */
    keyboard_flush();
    controller_flush();

    start_splash_music();

    while (1) {
        uint16_t size = sizeof(g_splash_read_buf);

        sound_loop();
        tick_current_music();
        (*g_splash.entropy)++;

        if (read(DEV_STDIN, g_splash_read_buf, &size) == ERR_SUCCESS && size > 0) {
            for (uint16_t i = 0; i < size; i++) {
                uint8_t key = g_splash_read_buf[i];
                if (key == KB_KEY_ENTER || key == KB_KEY_SPACE) {
                    goto splash_pressed;
                }
                if (key == KB_KEY_QUOTE || key == KB_RIGHT_SHIFT) {
                    g_splash.quit_cb();
                    exit(0);
                }
            }
        }

        gfx_wait_vblank(g_splash.vctx);
        gfx_wait_end_vblank(g_splash.vctx);

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
        uint16_t size = sizeof(g_splash_release_buf);
        uint8_t held = 0;

        if (read(DEV_STDIN, g_splash_release_buf, &size) == ERR_SUCCESS && size > 0) {
            for (uint16_t i = 0; i < size; i++) {
                if (g_splash_release_buf[i] == KB_KEY_ENTER || g_splash_release_buf[i] == KB_KEY_SPACE) {
                    held = 1;
                    break;
                }
            }
        }
        if (!held) {
            break;
        }
        gfx_wait_vblank(g_splash.vctx);
        gfx_wait_end_vblank(g_splash.vctx);
    }

    keyboard_flush();
    controller_flush();
}
