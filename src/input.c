#include <stdint.h>

#include <zgdk/input.h>

#include "input.h"

enum {
    EVENT_NOT_PRESSED = 0,
    EVENT_PRESSED = 1,
    INPUT_USE_CONTROLLER = 1,
};

static uint16_t previous_input = 0;

static uint16_t read_controller_input(void)
{
    uint16_t controller_input = controller_read();
    uint16_t unused_bits = (uint16_t)(
        BUTTON_UNUSED1 |
        BUTTON_UNUSED2 |
        BUTTON_UNUSED3 |
        BUTTON_UNUSED4);

    /*
     * Some emulator/no-pad setups leave unused controller lines floating.
     * If those bits appear active, ignore the pad sample so keyboard edges
     * remain usable.
     */
    if (controller_input & unused_bits) {
        return 0;
    }

    return controller_input;
}

static uint16_t read_combined_input(void)
{
    return (uint16_t)(keyboard_read() | read_controller_input());
}

zos_err_t input_events_init(void)
{
    zos_err_t err = input_init(INPUT_USE_CONTROLLER);
    if (err != ERR_SUCCESS) {
        return err;
    }

    previous_input = read_combined_input();
    return ERR_SUCCESS;
}

void input_events_flush(void)
{
    input_flush();
    previous_input = read_combined_input();
}

void input_poll_events(KeyEvents* ev)
{
    uint16_t current_input = read_combined_input();
    uint16_t pressed_input = (uint16_t)(current_input & (uint16_t)~previous_input);

    ev->up = (pressed_input & BUTTON_UP) ? EVENT_PRESSED : EVENT_NOT_PRESSED;
    ev->down = (pressed_input & BUTTON_DOWN) ? EVENT_PRESSED : EVENT_NOT_PRESSED;
    ev->left = (pressed_input & BUTTON_LEFT) ? EVENT_PRESSED : EVENT_NOT_PRESSED;
    ev->right = (pressed_input & BUTTON_RIGHT) ? EVENT_PRESSED : EVENT_NOT_PRESSED;
    ev->action = (pressed_input & BUTTON_B) ? EVENT_PRESSED : EVENT_NOT_PRESSED;
    ev->start = (pressed_input & BUTTON_START) ? EVENT_PRESSED : EVENT_NOT_PRESSED;
    ev->quit = (pressed_input & BUTTON_SELECT) ? EVENT_PRESSED : EVENT_NOT_PRESSED;
    ev->toggle_audio_mode = (pressed_input & BUTTON_X) ? EVENT_PRESSED : EVENT_NOT_PRESSED;

    previous_input = current_input;
}
