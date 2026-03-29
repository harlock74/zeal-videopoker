#pragma once

#include <stdint.h>

#include <zgdk/sound/tracker.h>

#include "videopoker.h"

typedef struct {
    uint8_t* splash_music_ready;
    uint8_t* game_music_ready;
    uint8_t* current_music_mode;
    uint8_t* loaded_music_index;
    uint8_t* game_cards_sfx_mode;
    uint16_t* entropy;
    GameState* state;
    uint8_t* pending_bankrupt_reset;
    track_t* music_track;
} AudioBindings;

void audio_bind(const AudioBindings* bindings);

void start_splash_music(void);
void start_game_music(void);
void tick_current_music(void);
void stop_current_music(void);
void apply_game_audio_mode(void);
void play_card_place_sound(void);
