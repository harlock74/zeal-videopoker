#pragma once

#include <stdint.h>

/* Blocks on splash until Start/Enter is pressed, with blinking prompt callback. */
void splash_run_blocking(void (*draw_prompt)(uint8_t visible));
