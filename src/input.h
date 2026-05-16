#pragma once

#include <stdint.h>
#include <zos_errors.h>

/*
 * One-shot gameplay events from ZGDK's shared keyboard/controller button map.
 * B/Space/Z confirms in-game actions, A/X toggles holds, and Enter maps to Start.
 */
typedef struct {
    uint8_t up;
    uint8_t down;
    uint8_t left;
    uint8_t right;
    uint8_t hold;
    uint8_t confirm;
    uint8_t start;
    uint8_t quit;
    uint8_t toggle_audio_mode;
} KeyEvents;

zos_err_t input_events_init(void);
void input_events_flush(void);
void input_poll_events(KeyEvents* ev);
