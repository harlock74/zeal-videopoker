#include <stdint.h>
#include <stdio.h>

#include <zvb_sound.h>
#include <zgdk.h>
#include <zgdk/sound/tracker.h>

#include "main.h"
#include "app_state.h"
#include "assets.h"
#include "audio.h"

/* Keep audio tuning local to the audio module. */
#define CARD_SOUND 0
#define CARD_SFX_BASE_FREQ 10
#define CARD_SFX_JITTER_MASK 0x03
#define CARD_SFX_DURATION 1
#define CARD_SFX_WAVEFORM WAV_SAWTOOTH

/* Tracker storage owned by audio module. */
static pattern_t music_pattern0;
static pattern_t music_pattern1;
static pattern_t music_pattern2;
static pattern_t music_pattern3;
static pattern_t music_pattern4;
static pattern_t music_pattern5;
static pattern_t music_pattern6;
static pattern_t music_pattern7;
static track_t music_track = {
    .title = "Music",
    .patterns = {
        &music_pattern0,
        &music_pattern1,
        &music_pattern2,
        &music_pattern3,
        &music_pattern4,
        &music_pattern5,
        &music_pattern6,
        &music_pattern7,
    }
};

/* Audio state owned by this module. */
static uint8_t splash_music_ready = 0;
static uint8_t game_music_ready = 0;
static uint8_t current_music_mode = 0; /* 0=off, 1=splash, 2=game */
static uint8_t loaded_music_index = 0xFF;
/* 0 = music-only (card SFX muted), 1 = card-SFX-only (gameplay track paused). */
static uint8_t game_cards_sfx_mode = 0;

void audio_init_tracks(void)
{
    if (load_zmt(&music_track, 0) == ERR_SUCCESS) {
        splash_music_ready = 1;
        loaded_music_index = 0;
    } else {
        splash_music_ready = 0;
        printf("Warning: failed to load splash music track\n");
    }

    if (load_zmt(&music_track, 1) == ERR_SUCCESS) {
        game_music_ready = 1;
        loaded_music_index = 1;
    } else {
        game_music_ready = 0;
        printf("Warning: failed to load gameplay music track\n");
    }

    if (splash_music_ready) {
        /* Ensure splash music is staged for first screen. */
        if (load_zmt(&music_track, 0) == ERR_SUCCESS) {
            loaded_music_index = 0;
        } else {
            splash_music_ready = 0;
            loaded_music_index = 0xFF;
        }
    }
}

void start_splash_music(void)
{
    if (!splash_music_ready) {
        current_music_mode = 0;
        return;
    }
    if (loaded_music_index != 0) {
        if (load_zmt(&music_track, 0) == ERR_SUCCESS) {
            loaded_music_index = 0;
        } else {
            current_music_mode = 0;
            return;
        }
    }
    zmt_sound_off();
    zmt_track_reset(&music_track, 1);
    current_music_mode = 1;
}

void start_game_music(void)
{
    if (game_cards_sfx_mode) {
        current_music_mode = 0;
        return;
    }
    if (!game_music_ready) {
        current_music_mode = 0;
        return;
    }
    if (loaded_music_index != 1) {
        if (load_zmt(&music_track, 1) == ERR_SUCCESS) {
            loaded_music_index = 1;
        } else {
            current_music_mode = 0;
            return;
        }
    }
    zmt_sound_off();
    zmt_track_reset(&music_track, 1);
    current_music_mode = 2;
}

void tick_current_music(void)
{
    if (current_music_mode == 1 && splash_music_ready) {
        /* Splash track authored with arrangement flow. */
        zmt_tick(&music_track, 1);
    } else if (current_music_mode == 2 && game_music_ready) {
        /* Gameplay track uses arrangement flow (zmt_tick(..., 1)). */
        zmt_tick(&music_track, 1);
    }
}

void stop_current_music(void)
{
    zmt_sound_off();
    current_music_mode = 0;
}

static void apply_game_audio_mode(void)
{
    if (game_cards_sfx_mode) {
        /* Card-SFX-only mode: silence gameplay music. */
        if (current_music_mode == 2) {
            stop_current_music();
        }
        /*
         * After tracker shutdown, restore sound/tracker runtime state so
         * plain sound_play() effects remain audible in SFX-only mode.
         */
        zmt_reset(VOL_50);
    } else {
        /* Music-only mode: ensure gameplay track is active outside splash. */
        if (state != STATE_BET || !pending_bankrupt_reset) {
            start_game_music();
        }
    }
}

void audio_toggle_game_audio_mode(void)
{
    game_cards_sfx_mode ^= 1;
    apply_game_audio_mode();
}

void play_card_place_sound(void)
{
    if (game_cards_sfx_mode == 0) {
        return;
    }
    /*
     * Short, low-pitch pluck with tiny random jitter to avoid identical
     * transients per card while still sounding like the same action.
     */
    uint16_t freq = (uint16_t)(CARD_SFX_BASE_FREQ + (rand8_quick() & CARD_SFX_JITTER_MASK));
    Sound* tap = sound_get(CARD_SOUND);
    if (tap != NULL) {
        tap->waveform = CARD_SFX_WAVEFORM;
    }
    tap = sound_play(CARD_SOUND, freq, CARD_SFX_DURATION);
    if (tap != NULL) {
        tap->waveform = CARD_SFX_WAVEFORM;
    }
    entropy ^= (uint16_t)(freq << 1);
}
